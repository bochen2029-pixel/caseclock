# caseclock — development log

## 2026-09-03 · 0.1.0: the floor, the tape, the sign-out, the strip

Built in one session from the blueprint: `src/` (ten translation units, ~3,900 lines), `build.bat`
with the import-table gate, `tests/reference_dump.py` recording the reference's outputs,
`--selftest` at 86 checks, all green. Both exes 786 KB, static CRT, imports `user32 gdi32 gdiplus
crypt32 bcrypt shell32 kernel32` and nothing else.

**Measured (this box: 225 % DPI, llama-server and the speech stack running).**

| what | number |
|---|---|
| closure, 61 events, 200 closures averaged, /O2 | 51 µs each |
| strip, first paint, 3840 × 90 physical px at dpi 216 | 6.5 ms |
| working set at the shot, one case, 11-row tape | 11.1 MB (peak 11.2) |
| exe, static, either subsystem | 786 KB |
| `--selftest` | 86 checks, ~0.3 s |

**Parity, by equality.** `tests/reference_dump.py` imports `C:/REGISTRAR/floor/closure.py` and
`core/tape.py` and records, per fixture, `names`, every constraint's `render()`, `D`, `nxt`,
`origin`, `consistent`, every window, every `binding_path`, every `explain()` text, the negative
cycle, its constraints, `short_by`, the `report()` text and exit code, `hhmm()` samples; plus one
scripted 11-row tape (unicode, every JSON escape, nesting, bools, null, negative ints, a
`supersedes`) and hashlib's BLAKE2b/SHA-256 vectors. The C++ selftest asserts all of it by
equality. A live `--report` on both fixtures is byte-identical to the Python output (CR stripped:
Python prints CRLF when redirected on Windows). REGISTRAR's `tape.py` opens and verifies a tape
caseclock wrote: `chain: INTACT`.

**Traps, the day they bit.**

- **The tape is BLAKE2b, not SHA-256.** BLUEPRINT §4 said `SHA-256(canonical(seq, at, kind, body,
  prev))`; the reference does `blake2b(digest_size=32)` over `prev · NUL · payload` with `prev`
  outside the payload. Those are different bytes, and parity by equality is the acceptance
  criterion, so the blueprint was wrong and is corrected. BLAKE2b is transcribed from RFC 7693 into
  `src/hash.cpp` (parameter word `0x01010020`); the vectors include 127/128/129-byte inputs because
  the "keep the last block in the buffer" rule is where transcriptions go wrong. `bcrypt` still
  does SHA-256 for file, rule-set and sign-out hashes. CLAUDE.md rule 1 reworded to match.
- **Python's canonical JSON is precise.** `ensure_ascii=False`: non-ASCII raw, `0x7f` raw, only
  the C0 controls escaped, `\b \f \n \r \t` by name and the rest as lowercase `\u00xx`; keys sort
  by code point, which is UTF-8 byte order, so `std::string <` is right. And `f"{s:<62}"` pads by
  code points, not bytes: a label with an em dash (the transport fixture has one) misaligns by two
  columns unless the padding counts code points (`pad_cp`).
- **In-place Floyd–Warshall on an infeasible network.** The reference mutates row k while reading
  it once `D[k][k] < 0`, and the port must do the same in the same order to match: `Dik` captured
  once per row as the Python local is, `Dk[j]` and `nxt[i][k]` read live as the lists are. The
  transport fixture's D (`D[0][0] = −30`, `D[4][0] = −1020`) matches entry for entry. `D` is
  `int64` for headroom; `INF` stays `0x3f3f3f3f`.
- **The negative cycle can include T0.** After a wrong fact (serology drawn at 22:20, leaving the
  lab 355 of the 360 minutes it needs) the reference walks `T0 → cross_clamp → … → serology_drawn
  → T0`, short by 5. The port reports the same ring; "as the reference walks it" is the contract,
  not "the prettiest cycle".
- **`GetUserNameW` lives in advapi32**, which is not on the import list; the build gate is about
  network DLLs but rule 1 is about all of them. The Windows user comes from `USERNAME` via
  kernel32.
- **GDI+ under `WIN32_LEAN_AND_MEAN` + `NOMINMAX`** needs `<objidl.h>` before `<gdiplus.h>` and
  `using std::min; using std::max;` (facet's include order, copied).
- **The hour-mark rule and the load trace.** Loading exactly on an hour mark writes a `silence`
  row, not the `held lead_not_reached` trace, because nothing is due within the lead; the selftest
  loads at 18:55 so both paths are exercised and named.
- **Ties.** `match_run` and `serology_resulted` share 04:15; the open list sorts by latest, then
  name. The first selftest assertion assumed otherwise.
- **The harness, not the code.** The Bash tool halves backslashes before bash sees a heredoc and
  fails the whole command on an odd number of apostrophes in the body. Source files go through the
  Write/Edit tools; the one substitution Edit could not match was a five-line `.py` run by path.
  Posted to Intercom broadcast; the glance and surveyor sessions hit variants of the same trap.

**Decisions made while building, now in BLUEPRINT §5 "as built".**

- The speaking rules made precise: once per lead and once on breach, the most urgent unsaid
  reason wins, one timer-driven line a minute (`held rate` / `held next_due`), a `held
  lead_not_reached` trace after every silent closure, `infeasible` and `withdrawn` event-driven and
  unlimited, `silence` at hour marks with nothing within the lead. Everything the rules remember is
  `fold_clock(tape)`, so a reopened tape resumes exactly and the replay reproduces the rows.
- Readers write nothing: `--json`, `--explain`, `--mcp`, `--spool` load observe-only (the file's
  facts bind in memory so the answer matches what the strip will show). `--signout` and `--fact`
  write, as does the strip. The MCP's `case_signout` does not record a signout row; a human's
  `Ctrl+S` does.
- DPAPI per row, not per file: the encrypted tape is still an append (`FILE_APPEND_DATA`, one
  write per batch) under a first line `caseclock-tape 1 dpapi`; `--export` writes the reference's
  clear format; `--clear` writes it directly (replays, tests).
- No comdlg32, no winspool, no ole32: `Ctrl+O` is a path box; `Ctrl+P` writes the sign-out as a
  clear-text file under `%LOCALAPPDATA%\caseclock\print\` and hands it to the shell's `print` verb.
  That file is the one clear-text path, created by a human's keystroke, and the status line says so.
- A fact pins the event, so an entered event is no longer a deadline: its bound is withdrawn on the
  tape, and if it had been said, the withdrawal is said. Downstream bounds are re-derived.
- The reference is minutes from a case-specific origin; a case file may add `reference_at`
  (local `YYYY-MM-DDTHH:MM`) and the strip then runs on the wall clock; otherwise the file's `now`
  is the launch minute and the clock advances from it. `--now` overrides for console modes.

**Next.** Rule packs with citations (still none; the honest state). A second synthetic case that
is feasible with zero slack. The strip on a second monitor and with two cases loaded, driven by
`tests/drive.py`. A C++ driver, as the blueprint says. The tools-page capture from
`caseclockw --gui --replay --speed 60`. A signed build on the practice's side.

## 2026-09-02 · the blueprint

- Origin: the opnaorta.ai tools page has two tools shipped (facet, vramtop) and six house
  rules; the products page has five residents at REV 0 none of which a coordinator can run
  today. The general product designed the same day (Still: a resident that mostly says
  nothing, with a strip and a weekly Notice) reduces, under the six rules, to its floor: the
  case clock, no model, with the sign-out as its text form. That floor already exists in Python
  as REGISTRAR's `floor/closure.py`; caseclock is its port to one Windows exe.
- Read the reference: an STN in whole minutes, `INF = 0x3f3f3f3f`, Floyd–Warshall in (min, +)
  with `nxt` recovery, `binding_path` as the citation, `negative_cycle` with the constraints
  that cannot all hold. Hand-derived the shipped fixtures' expectations: TR-4118 serology latest
  −105 (22:15 −1d), chain of 7; the transport case short by 30. The AORTA fold's replay is the
  first fixture with `now = −20`.
- Decisions: no network stack, gated at build by the import table; DPAPI for the tape at rest;
  REGISTRAR's tape JSONL and case JSON verbatim so the resident above can fold the same files;
  gear zero speaking rules (lead 60/15/0, breach, infeasible, withdrawn, rate one per minute,
  hourly silence rows); the sign-out generated at the instant of printing with the delta since
  the last one.
- Rule packs are not shipped until L0/L1 carry citations; synthetic cases carry their own
  constraints. This is the honest state and the first question a medical director will ask.
- Next: `src/closure` first, with parity tests against the reference's outputs on the two
  fixtures, before any window exists.
- Verified the two fixtures against the reference (`python C:/REGISTRAR/floor/closure.py`):
  TR-4118 serology_drawn latest 22:15 (−1d), slack −85 BREACHED at now −20, chain of 7 with the
  cumulative times as written in BLUEPRINT §7; the transport case INFEASIBLE, short by 30.
  The reference walks the cycle `cross_clamp → implant → arrival → organ_out → cross_clamp`;
  the fixture's `expected.cycle` now matches that order, since parity is by equality.
