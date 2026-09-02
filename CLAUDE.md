# CLAUDE.md — session rules for caseclock

You are implementing **caseclock**, a Windows tool for the opnaorta.ai tools page: the case
clock for organ-procurement coordinators. Read `README.md`, then `docs/BLUEPRINT.md`, then
`docs/devlog.md`. The machine-wide rules in `C:\Users\user\.claude\CLAUDE.md` apply on top.
The reference implementation you are porting is `C:\REGISTRAR\floor\closure.py` and
`C:\REGISTRAR\core\tape.py`; read both before writing a line.

## Hard rules

1. **C or C++ only, OS APIs only, one static exe.** No runtime, no vendored code. `user32`,
   `gdi32`, `gdiplus`, `crypt32` (DPAPI), `bcrypt` (SHA-256), `shell32`. Nothing else.
2. **No network stack, by construction.** The exe must not import `ws2_32`, `wininet`,
   `winhttp`, `urlmon`, `dnsapi`, or `iphlpapi`. `build.bat` runs `dumpbin /imports` and fails
   the build if any appears. This is a receipt for "nothing leaves your building"; keep it
   printable in `--about`.
3. **Zero PHI, forever, in this repository.** Fixtures are synthetic and say so in their first
   line. No de-identified, sampled, or perturbed real cases. No number in any fixture or doc is
   a clinical or regulatory claim; every duration is labelled illustrative. If you find
   yourself citing one, stop.
4. **The closure is bit-identical to REGISTRAR's.** Whole minutes, integer (min, +),
   `INF = 0x3f3f3f3f`, Floyd–Warshall with the same in-place order, the same `nxt` recovery,
   the same tie-breaking (strict `<`). `--selftest` asserts equality with the reference on
   every fixture, never tolerance.
5. **Only a human writes a fact; only a human signs.** The tool derives, shows, and prints. It
   never reads the EDR, never writes to it, never decides allocation, eligibility, or anything
   about the family. Facts enter through the coordinator's keyboard or a case file they chose.
6. **The tape is append-only and hash-chained** in REGISTRAR's format (`seq`, `at`, `kind`,
   `body`, `prev`, `digest`). There is no delete and no update in the interface; a correction is
   a new row. The tape is encrypted per user with DPAPI at rest.
7. **No model.** The tools page badge says NO AI INSIDE and it stays true. A future resident
   subscribes to the tape through the seam in BLUEPRINT §12; caseclock never needs it.
8. **The six house rules of the tools page** (BLUEPRINT §1) are acceptance criteria, not
   aspirations. A build that breaks one does not ship under this name.

## Discipline

- `build.bat` only; `/W4 /WX`, zero warnings.
- `--selftest` before every commit: closure parity on every fixture, tape chain verification,
  sign-out determinism (two folds over one tape agree byte for byte), the import-table gate.
- Measure before claiming: closure time for a 60-event case, strip paint time on the 225 % box,
  memory. Numbers go in the devlog with the date.
- The devlog is a lab notebook. Traps go there the day they bite.
- Commit at milestones as `caseclock 0.X.0 — …` with the attribution trailer the harness
  requires. Never push unless asked.
- Mirror `C:\facet` for the window shell (the strip is facet's status bar with the rail
  removed), `C:\GPUz` for the console modes, and everywho's blueprint for the docs shape.

## This machine

Windows 11 24H2, 225 % DPI (size in physical pixels from `GetDpiForSystem`), VS 2022, a loaded
GPU: test with llama-server and the speech stack running, per house rule five. Everything 1.4
is running; facet, vramtop, everywhen live at `C:\facet`, `C:\GPUz`, `C:\everywhen`. The harness
blocks PowerShell `Remove-Item` under tool folders; delete with Git Bash `rm`. Paths in shell
commands use forward slashes.

## Refusals

A network feature "just for updates": no. A model "just to phrase the line": no, that is VIGIL.
A real case "just for a demo": no, ever. A shortcut that reads the EDR window: not in this tool.
