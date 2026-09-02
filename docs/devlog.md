# caseclock — development log

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
