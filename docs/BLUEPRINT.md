# caseclock — blueprint

*Design 0.1 · 2026-09-02 · Bo Chen (operator) + Claude Fable 5.1 (synthesis) · MIT · Windows 10
1809+ / 11, x64, C++20, one static exe, no runtime, no network stack, no model.*

## §0 · The one job

**caseclock keeps the case clock.** Every deadline a donor case implies, derived from the facts
already entered, counted down on a strip pinned over the record, written to a tape with the
chain of constraints that produced it. Its text form is the sign-out.

It exists because a case under a clock fails in the transitive closure of rules that are each,
separately, satisfied. Every field is green, no timer has expired, and the morning OR window is
already gone. The deadline nobody wrote down was 22:15 the previous evening. REGISTRAR's floor
computes that deadline and its citation in Python; caseclock is the same floor as one Windows
exe a coordinator can run on a locked-down workstation without installing anything.

## §1 · Acceptance: the six house rules

| rule | how caseclock keeps it |
|---|---|
| 01 one job each | the clock; the sign-out is its text form, not a second job |
| 02 free and open, MIT, no tiers, no funnel | the whole tool, on GitHub; nothing unlocks |
| 03 runs beside the work, pins on top, stays small | a 28-px strip; never wants the screen the record is on |
| 04 keyboard first, printed | shortcuts live in the strip's status text |
| 05 built on a machine that runs models | tested with llama-server and the speech stack loaded |
| 06 same answer for humans and agents | `--json`, `--signout`, `--mcp`: one line an agent can paste |

Plus the two the vertical adds: **nothing leaves the building** (no network stack, verified at
build) and **a human signs** (the tool derives; it never writes a fact it was not given).

## §2 · The floor: the closure, ported bit for bit

The model is REGISTRAR's `floor/closure.py`, a Simple Temporal Network:

- Events are named time points; `T0` is the reference (index 0), all times whole minutes from it.
- A constraint is `x_later − x_earlier ≤ w`, with a `label` and a `layer` (L0 policy, L1 medicine,
  L2 site, L3 integration, L4 the case record). Four kinds in the case file: `at_least`,
  `at_most`, `window` (both bounds from the reference), `at` (a completed event pinned to a
  known minute, both bounds equal).
- The distance graph has an edge `earlier → later` of weight `w`; the tightest per edge wins
  (strict `<`), and the winning constraint is remembered as the edge's origin.
- Floyd–Warshall in (min, +) with `INF = 0x3f3f3f3f`, in place, in the reference's exact loop
  order, with `nxt[i][j]` recovery of the first hop.
- Consistent iff no `D[i][i] < 0`. `latest(e) = D[0][e]`, `earliest(e) = −D[e][0]`,
  `slack(e, now) = latest(e) − now`, negative means breached.
- `binding_path(e)`: the constraints along the shortest path `T0 → e`, the chain that realises
  the bound. `negative_cycle()` and `cycle_constraints()`: the events and constraints of an
  infeasible plan, so the contradiction names itself and says how short it is.
- `hhmm(m)`: `HH:MM` with a `(±Nd)` day offset; `unbounded` beyond `INF / 2`.

**Correctness discipline.** Integer arithmetic only; no floating point anywhere. `--selftest`
runs every fixture in `floor/cases/` and compares `D`, `nxt`, every window, every chain, and
every rendered line against expected outputs recorded from the reference implementation,
asserted by equality. Hand-checked expectations for the two shipped fixtures:

| fixture | expectation |
|---|---|
| `tr-4118.synthetic.json` | `serology_drawn` latest **−105 = 22:15 (−1d)**; `serology_resulted` 255; `match_run` 255; `primary_acceptance` 435; `or_scheduled` 435; `incision` 555; `cross_clamp` window [360, 600]; chain for `serology_drawn` has 7 hops; with `now = −20` (23:40 the previous evening) slack is −85, BREACHED |
| `infeasible-transport.synthetic.json` | inconsistent; cycle as the reference walks it, `cross_clamp → implant → arrival → organ_out → cross_clamp`; short by **30** minutes |

A 60-event case closes in well under a millisecond; a recompute happens on every fact.

## §3 · The case file, and rules as data

The case file is REGISTRAR's fixture format, verbatim, so caseclock, REGISTRAR and VIGIL read
the same file:

```json
{ "id": "TR-4118", "synthetic": true, "note": "...", "reference": "midnight of the recovery day",
  "now": -20, "watch": ["serology_drawn", "..."], "explain": ["serology_drawn"],
  "constraints": [
    { "kind": "window",   "event": "cross_clamp", "opens": 360, "closes": 600, "label": "...", "layer": "L2" },
    { "kind": "at_least", "later": "incision", "earlier": "or_scheduled", "minutes": 120, "label": "...", "layer": "L3" },
    { "kind": "at",       "event": "cross_clamp", "minutes": 480, "label": "...", "layer": "L4" } ] }
```

Two additions, both optional and both ignored by the reference implementation:

- `"source"` on a constraint: the citation (a policy section, a protocol name, a person and a
  date). `"verified_by"` and `"verified_on"` on the file. The sign-out prints the rule-set hash
  and the verification line under its header.
- `"facts"`: an append-only list of `at` constraints entered live, each with `entered_at`
  (wall clock) and `by` (the coordinator's name), which the tool writes as the coordinator enters
  times. A fact is a constraint with layer L4.

**Rules are data, layered, owned.** L0 and L1 are the same for every OPO and ship as
`floor/rules/*.json` once they carry citations; until then the repository ships no rule pack,
only synthetic cases that carry their own constraints. L2 and L3 are the site's and live in the
site's git. The tool loads rule packs before the case and reports which layers are present in the
strip's status text (`rules L0 L1 · site L2 missing`).

Every number in every shipped file is illustrative and says so in its first line. See
`floor/cases/README.md` and REGISTRAR's `PROVENANCE.md` §4.

## §4 · The tape

REGISTRAR's `core/tape.py` format, so any REGISTRAR or VIGIL fold reads it:

```
{"case_id":"TR-4118"}
{"seq":0,"at":-20,"kind":"note","body":{"text":"SYNTHETIC..."},"prev":"000…0","digest":"…"}
```

`digest = SHA-256(canonical(seq, at, kind, body, prev))`; `prev` is the previous digest; the
first row's `prev` is 64 zeros. Canonical form: the reference's `json.dumps(sort_keys=True,
separators=(",", ":"))`; the C++ writer produces the identical bytes, and `--selftest` verifies a
tape written by the tool against a tape written by the reference for the same rows.

caseclock's row kinds:

| kind | body | when |
|---|---|---|
| `note` | text | the synthetic banner; operator notes |
| `rules` | layers, file hashes, `verified_by`, `verified_on` | at load |
| `fact` | event, minutes, by | a time entered (an `at` constraint) |
| `derived` | event, latest, earliest, chain (rendered constraints), rules_hash | after every closure, one row per watched event whose bound changed |
| `withdrawn` | event, previous latest, cause (the fact that changed it) | a bound that moved or vanished |
| `said` | event, latest, slack, reason (`lead_60` · `lead_15` · `due` · `breach` · `infeasible` · `withdrawn`), chain | the strip spoke |
| `held` | reason (`next_due`, `lead_not_reached`, `rate`) | a boundary where nothing was said |
| `silence` | hour, open (count) | every hour mark with nothing due |
| `infeasible` | cycle (events), constraints, short_by | the plan cannot be met |
| `signout` | by, hash of the printed text, rows (count) | a sign-out was printed |

There is no `delete` and no `update`; a correction is a new `fact` that supersedes an older one,
and the closure reruns. The tape file is `%LOCALAPPDATA%\caseclock\<case_id>.tape.jsonl`,
encrypted at rest with DPAPI (`CryptProtectData`, per user); `--export` writes it in the clear for
the auditor, on the coordinator's command.

## §5 · Speaking, by rule

Gear zero only. The tool speaks when a rule says so, never otherwise, and writes the silences:

- **Lead times**: 60 and 15 minutes before a deadline, at the deadline, and on breach; the
  defaults are in the rule pack and the OPO may change them (`"lead_minutes": [60, 15, 0]`).
- **Infeasible**: immediately, naming the cycle and how short it is. This is the one catch a
  flat timer list can never make, and the reason the tool exists.
- **Withdrawn**: when a new fact removes or moves a deadline that had been said.
- **Rate**: at most one line per minute; anything more waits with a `held` row (`rate`).
- **Silence**: at every hour mark with nothing due within the next lead time, a `silence` row,
  and the strip shows the next due time and the count of open deadlines.

The line the strip shows is composed from the tape row, never free text:
`TR-4118 · serology_drawn by 22:15 (−1d) · breached 85 min · chain 7 · Ctrl+E`.

## §6 · The strip

facet's window with the rail and the table removed: one row.

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ ▍ TR-4118 · serology_drawn by 22:15 (−1d) · BREACHED 85 min · chain 7        3 open  ⏎ │
│   Ctrl+L fact · Ctrl+E explain · Ctrl+S sign-out · Ctrl+O case · Ctrl+T pin · Esc        │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Geometry**: 28 logical px tall, docked to the bottom of the primary monitor by default,
  draggable to the top, width the monitor's or a chosen fraction, always on top (`Ctrl+T`
  toggles), physical pixels scaled by the system DPI. The second line, the keys, is 12 px and
  can be hidden; the first-run default shows it, house rule four.
- **The left mark** `▍` is the state: dim when nothing is due within the lead time, amber
  under 60 minutes, red under 15 or breached, violet for infeasible. Colour is never the only
  signal; the text always carries the state.
- **Multiple cases**: the strip shows the nearest deadline across every loaded case; `Ctrl+Tab`
  cycles cases; the count at the right is open deadlines across all of them.
- **Ctrl+L, a fact**: a two-field popover, event name (from the case's known events, with
  completion) and time (`HH:MM`, `HH:MM −1d`, or minutes); Enter writes the `fact` row, reruns
  the closure, writes `derived` and `withdrawn` rows, updates the strip.
- **Ctrl+E, explain**: a small pane under the strip with the chain, one constraint per line with
  its layer and its cumulative time, exactly the reference's `explain()` text; Esc closes it.
- **Ctrl+S, sign-out**: renders §7, shows it in a pane, copies the text to the clipboard, writes
  the `signout` row; `Ctrl+P` prints.
- **Ctrl+O**: open a case file; the tool also watches its own case directory for new files.
- **Esc**: close a pane; twice, minimise the strip to the tray.

Test seam, as facet: `CASECLOCK_LOG=FILE` logs every keystroke, fact, closure and strip state;
`WM_APP+7` posts a fact from a driver; `--shot FILE.png` renders once and exits; `--no-activate`.

## §7 · The sign-out

Generated from the live state at the instant it is asked for, so nothing in it can be stale, and
carrying the delta since the last one printed:

```
CASECLOCK · SIGN-OUT · TR-4118 · 23:40 (−1d) · by J. Alvarez → next: on-call
rules L0 L1 (hash 9c1e…) · site L2 (hash 44ab…) · verified by Dr. Okafor on 2026-08-30
tape a3f9…c1 · 41 rows · 2 said · 38 held · 1 withdrawn · 0 breached before this shift

DUE, IN ORDER
  serology_drawn      22:15 (−1d)   BREACHED 85 min   chain 7   →  the morning OR window cannot be met as planned
  match_run           04:15         slack 275 min     chain 5
  primary_acceptance  07:15         slack 455 min     chain 3
  incision            09:15         slack 575 min     chain 1

SINCE THE LAST SIGN-OUT (19:00)
  fact   21:50  serology_drawn NOT entered — awaiting lab               (entered by J. Alvarez 22:02)
  said   22:40  serology_drawn: due in 15 min
  said   23:16  serology_drawn: BREACHED — the chain is the citation
  held   ×38    nothing else due within 60 min

THE CHAIN FOR serology_drawn
  cross_clamp - T0 <= 600                       [L2]  donor-hospital OR availability (06:00-10:00)   cumulative 10:00
  incision - cross_clamp <= -45                 [L1]  incision to cross-clamp                          cumulative 09:15
  or_scheduled - incision <= -120               [L3]  recovery-team mobilisation                       cumulative 07:15
  primary_acceptance - or_scheduled <= 0        [L2]  acceptance precedes scheduling                   cumulative 07:15
  match_run - primary_acceptance <= -180        [L2]  offer window budgeted at 3h                      cumulative 04:15
  serology_resulted - match_run <= 0            [L0]  serology results precede the match run           cumulative 04:15
  serology_drawn - serology_resulted <= -360    [L3]  reference-lab turnaround                         cumulative 22:15 (-1d)

SYNTHETIC CASE · NO PHI · every duration illustrative        signed: ______________________
```

Determinism: two folds over one tape produce the same sign-out byte for byte; `--selftest`
asserts it. The sign-out is the text form house rule six requires: an agent reads the same page.

## §8 · Agents

- `--json`: `{"case":"TR-4118","now":-20,"consistent":true,"open":[{"event":…,"latest":…,
  "earliest":…,"slack":…,"chain":[…]}],"rules":{…},"tape":"a3f9…"}`; with `--all` every case.
- `--mcp`, stateless 2026-07-28 shape, read-only, three tools: `case_clock` (the open deadlines
  with chains), `case_explain {event}`, `case_signout`. Every result carries the tape digest.
  No tool writes a fact; a fact is a human's keystroke.
- `--spool`: lane `case`, one line per `said` and `infeasible` row, `lane<TAB>text`, for the
  fusor tailer and TOWER.
- `--about`: the organ's self-description, including the import table's absence of any network
  DLL, for `peek env`.

## §9 · Replay and the capture

`caseclock --replay floor/cases/tr-4118.synthetic.json --speed 60` runs the case's `facts` in
order at sixty times wall speed, writing a tape to a scratch path, so the tools-page capture and
the `--shot` PNG show a real strip over a real tape without a real case. The AORTA fold's replay
(TR-4118, "surfaced, unprompted, no timer fired") is this case: no timer was entered; the
deadline was derived.

## §10 · Security and PHI

- **No network stack.** `build.bat` runs `dumpbin /imports caseclock.exe` and fails on
  `ws2_32`, `wininet`, `winhttp`, `urlmon`, `dnsapi`, `iphlpapi`. `--about` prints the imports.
- **At rest**: the tape and the case files the coordinator saves are DPAPI-encrypted per user;
  the rule packs are not (they are not PHI); `--export` decrypts on command.
- **On screen**: the strip shows event names and minutes; it never shows a patient identifier
  unless the case id is one, and the case id is the coordinator's choice.
- **On paper**: the sign-out is the only path off the machine, and a human prints it.
- **Never**: the EDR's window, memory, database, or files; the network; a model; a real case in
  this repository.

## §11 · Build, files, tests

`build.bat`: `cl /std:c++20 /O2 /W4 /WX /permissive- /utf-8 /MT`, static, `caseclock.exe`
(console modes) and `caseclockw.exe` (the strip, no console), then the import-table gate.

| file | owns |
|---|---|
| `src/closure.h/.cpp` | the STN, the closure, `binding_path`, `negative_cycle`, `hhmm`; bit-identical to the reference |
| `src/casefile.h/.cpp` | the JSON case and rule-pack reader (a small strict parser, as facet's), facts, layers, hashes |
| `src/tape.h/.cpp` | the append-only chain, canonical JSON, SHA-256 via `bcrypt`, DPAPI at rest, verification |
| `src/clock.h/.cpp` | the speaking rules, lead times, rate, silence rows, withdrawal |
| `src/signout.h/.cpp` | the sign-out renderer, deterministic |
| `src/strip.cpp` | the window: strip, panes, keys, tray, DPI, the test seam |
| `src/caseclock.cpp` | console modes: `--signout --explain --json --replay --mcp --spool --about --selftest` |
| `src/app_util.h` | options and formatting, from facet |
| `floor/cases/` | synthetic cases, each with a `README` line |
| `tests/` | the driver (C++), expected outputs from the reference for parity |

`--selftest`: closure parity on every fixture (D, nxt, windows, chains, rendered lines);
tape canonical bytes equal to the reference's for the same rows; chain verification detects a
flipped byte; sign-out determinism; the speaking rules on a scripted clock (lead_60, lead_15,
due, breach, withdrawn, infeasible, rate, silence); the import table; DPAPI round trip.

## §12 · The seam above the floor

A resident (VIGIL) subscribes to the tape and may propose, for a `said` row about to be shown,
`hold` or `say` with a margin, and may propose a phrasing. caseclock treats a proposal as advice:
the rule's decision is written first, the proposal beside it, and the OPO decides which is
shown. That is the null discipline in the tool's own shape: the floor's line is always on the
record; the mind must beat it on the record before its line replaces it.

## §13 · Open decisions and risks

- **Rule packs need citations before they ship.** Until L0 and L1 carry sources, the tool ships
  only synthetic cases that carry their own constraints. This is the honest state and the
  first thing a medical director will ask about.
- **The reference is minutes from a case-specific origin.** Real cases span days; the strip
  shows clock times with day offsets, and the case file names the origin. A future revision may
  use absolute local timestamps in the case file and convert at load; the closure stays in
  minutes.
- **Many cases, one strip.** Nearest-deadline-wins is a rule, and a coordinator may want the
  strip pinned to one case; `Ctrl+Tab` cycles, and an `--only CASE` flag pins.
- **Windows only, no installer, no elevation.** A locked-down workstation may block unsigned
  exes; the signed build lives on the practice's side, the source stays MIT.
