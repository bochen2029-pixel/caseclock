# caseclock — the case clock

**Every deadline the case implies, derived from the facts already entered, counted down on a
strip over the record, and written to a tape with the chain that produced it.**

A donor case is a case under a clock. The rules are all known pairwise: the lab needs six hours,
the team needs two, the OR opens at six. Nobody computes the consequence, so the deadline that
kills the morning OR window was 22:15 the previous evening and it was written nowhere. caseclock
computes it, shows it, and can say why in six lines a clinician checks in seconds.

> **Status: 0.1.0, the floor and the strip, `--selftest` green.** The closure is a bit-identical
> port of [REGISTRAR](https://opnaorta.ai)'s `floor/closure.py` (D, nxt, every chain, every
> explain and report line equal to the reference on every fixture); the tape writes the bytes
> `core/tape.py` writes and the reference verifies it; the speaking rules, the sign-out, DPAPI at
> rest, the read-only MCP, the replay and the 28-px strip are built. Not yet: rule packs with
> citations (none ship; the synthetic cases carry their own constraints), a signed build. The
> design is in `docs/BLUEPRINT.md`; the rules in `CLAUDE.md`; the notebook in `docs/devlog.md`.

## One job

It keeps the case clock. Its text form is the sign-out. That is the whole tool, and the tools
page's six house rules are why it stops there: one job, free and MIT, runs beside the work,
keyboard first, built on a loaded machine, the same answer for humans and agents.

## What it does

- Loads a case: the facts entered so far (referral received, pronouncement, authorization, draw
  times, OR time) and the rules that bind them, as data.
- Runs the closure: a Simple Temporal Network in whole minutes, all-pairs shortest paths in the
  (min, +) semiring. Out come the **implied** deadlines: the last minute each event can still
  happen and leave the case feasible, and the feasible window of everything.
- Shows the nearest one on a 28-pixel strip pinned over the record:
  `TR-4118 · serology_drawn by 22:15 · 35 min · chain 6`
- Speaks at lead times, by rule, not by model: an hour out, fifteen minutes out, at the deadline,
  on breach, and the moment a case becomes infeasible, naming the constraints that cannot all hold.
- Withdraws a deadline the moment a new fact removes it, on the record, with the fact.
- Writes every hour of nothing due as a row: *hour 9, nothing due, said nothing.*
- Prints the sign-out at handoff from the live state at that instant, so the fact that changed
  while the sign-out was being typed is in it.

## What it never does

No network stack: the exe links neither Winsock nor WinINet nor WinHTTP, and the build fails if
it does. No reading of the EDR, no writing to it. Nothing about allocation, eligibility, or the
family. No model: the closure is exact, explicable, and needs no card. The tape is encrypted per
user at rest and leaves the machine on paper, when a human prints the sign-out. A human signs.

## Use

```
caseclock CASE.json                    load a case; the strip appears, pinned (caseclockw.exe: no console)
caseclock --signout CASE.json          the sign-out, now, as text; records a signout row (Ctrl+S in the strip)
caseclock --explain EVENT CASE.json    the chain of constraints behind a deadline
caseclock --fact EVENT TIME CASE.json  enter a time at the terminal; the closure reruns; rows are written
caseclock --json CASE.json             every open deadline with its chain, slack, and the clock
caseclock --report CASE.json           REGISTRAR's report text, byte for byte
caseclock --replay CASE.json           a synthetic case at --speed N (0 = instant) on a scratch tape
caseclock --export FILE CASE.json      the tape in the clear, for the auditor (- = stdout)
caseclock --verify CASE.json           walk the tape's hash chain
caseclock --mcp CASE.json              read-only MCP: case_clock · case_explain · case_signout
caseclock --spool CASE.json            lane "case" for a fusor / TOWER tailer
caseclock --about                      the organ's self-description, its import table included
caseclock --selftest                   closure parity against the reference on every fixture, by equality
```

Keys, printed in the strip: `Ctrl+L fact · Ctrl+E explain · Ctrl+S sign-out · Ctrl+O case · Ctrl+T pin · Esc`
(`Ctrl+Tab` cycles cases, `Ctrl+P` prints the sign-out, `Ctrl+K` hides the key line, Esc twice hides
the strip in the tray). `build.bat` builds both exes with VS 2022 and fails if a network DLL appears
in the import table.

## Files

`src/` (the exe: `closure` the port, `tape` the record, `clock` the speaking rules, `signout`,
`strip` the window, `caseclock` the console modes, `json` `hash` `sys` `casefile` underneath) ·
`tests/` (`reference_dump.py` records the reference's outputs into `expected/`; `drive.py` drives
the strip through its seam) · `floor/cases/` (synthetic cases, zero PHI, forever) ·
`docs/BLUEPRINT.md` (the design, one document) · `CLAUDE.md` (session rules) ·
`docs/DESIGN-BRIEF-tools-page.md` (the brief for the opnaorta.ai tools page) · `docs/devlog.md`.

## Where it sits

caseclock is the free floor. VIGIL, on the products page, is the fitted resident the practice
installs on top of it: the same tape, plus the mind that decides *when* among many catches. The
tool is what a coordinator can download today; the resident is what has to beat it before it ships.

MIT · Access Intellect LLC · built inside an OPO by someone who did the job.
