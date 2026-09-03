#!/usr/bin/env python3
"""
caseclock · tests · reference_dump.py
Records what REGISTRAR's reference implementation says about every fixture, so that
`caseclock --selftest` can assert equality against it without Python at run time.

    python tests/reference_dump.py [C:/REGISTRAR]

Writes, for every floor/cases/*.json:
    tests/expected/<name>.closure.json   D, nxt, origin, windows, chains, explain, report
and one scripted tape written by core/tape.py:
    tests/expected/tape-script.json      the rows (kind, at, body) the C++ writer replays
    tests/expected/reference-tape.jsonl  the bytes the reference wrote for them

Every number in every fixture is synthetic and illustrative (see floor/cases/README.md).
"""
from __future__ import annotations

import contextlib
import glob
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REG = sys.argv[1] if len(sys.argv) > 1 else "C:/REGISTRAR"
sys.path.insert(0, os.path.join(REG, "floor"))
sys.path.insert(0, os.path.join(REG, "core"))

import closure as C  # noqa: E402  (REGISTRAR/floor/closure.py)
import tape as T  # noqa: E402  (REGISTRAR/core/tape.py)

OUT = os.path.join(HERE, "expected")
os.makedirs(OUT, exist_ok=True)


def render(c: C.Constraint) -> dict:
    return {"later": c.later, "earlier": c.earlier, "weight": c.weight, "label": c.label,
            "layer": c.layer, "render": c.render()}


def dump_case(path: str) -> None:
    stn, doc = C.load_case(path)
    cl = stn.close()
    n = len(stn.names)
    cons = [render(c) for c in stn.constraints]
    origin = []
    for (i, j), c in cl.origin.items():
        origin.append([i, j, stn.constraints.index(c)])
    origin.sort()
    out = {
        "fixture": os.path.basename(path),
        "names": list(stn.names),
        "constraints": cons,
        "D": [row[:] for row in cl.D],
        "nxt": [[(-1 if v is None else v) for v in row] for row in cl.nxt],
        "origin": origin,
        "consistent": cl.consistent,
        "windows": {name: [cl.earliest(name), cl.latest(name)] for name in stn.names},
        "binding_path": {name: [c.render() for c in cl.binding_path(name)] for name in stn.names},
        "explain": {name: cl.explain(name) for name in stn.names},
        "negative_cycle": cl.negative_cycle(),
    }
    cyc = out["negative_cycle"]
    out["cycle_constraints"] = [c.render() for c in cl.cycle_constraints(cyc)] if cyc else []
    if cyc:
        out["short_by"] = -sum(c.weight for c in cl.cycle_constraints(cyc))
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        code = C.report(path)
    out["report"] = buf.getvalue()
    out["report_exit"] = code
    if doc.get("now") is not None:
        out["slack_at_now"] = {name: cl.slack(name, doc["now"]) for name in stn.names}
    out["hhmm"] = {str(m): C.hhmm(m) for m in
                   (0, 1, 59, 60, 61, 599, 600, 1439, 1440, 1441, 2880, -1, -105, -1440, -1441, -2881,
                    C.INF, -C.INF, C.INF // 2, C.INF // 2 - 1, -(C.INF // 2), -(C.INF // 2) + 1, 12345678)}
    name = os.path.basename(path).replace(".json", "")
    with open(os.path.join(OUT, name + ".closure.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print(f"  {name}: {n} events, {len(stn.constraints)} constraints, consistent={cl.consistent}")


def dump_tape() -> None:
    # caseclock's row kinds, with the awkward values a canonical writer must get right:
    # non-ASCII (raw, ensure_ascii=False), every JSON escape, negative ints, bool, null,
    # nested objects with keys out of order, empty strings, empty lists and objects.
    rows = [
        ["note", -20, {"text": "SYNTHETIC. No real donor data; every duration is illustrative."}],
        ["rules", -20, {"layers": ["L0", "L1"], "files": {"z.json": "00ff", "a.json": "ab12"},
                        "verified_by": None, "verified_on": None, "lead_minutes": [60, 15, 0]}],
        ["fact", -20, {"event": "referral_received", "minutes": -600, "by": "J. Alvarez (synthetic)"}],
        ["derived", -20, {"event": "serology_drawn", "latest": -105, "earliest": -1061109567,
                          "chain": ["cross_clamp - T0 <= 600  [L2]  donor-hospital OR availability (06:00-10:00) closes",
                                    "serology_drawn - serology_resulted <= -360  [L3]  reference-lab turnaround"],
                          "rules_hash": ""}],
        ["said", -20, {"event": "serology_drawn", "latest": -105, "slack": -85, "reason": "breach",
                       "text": "TR-4118 \u00b7 serology_drawn by 22:15 (-1d) \u00b7 BREACHED 85 min \u00b7 chain 7"}],
        ["held", -19, {"reason": "rate", "event": "match_run"}],
        ["silence", 0, {"hour": 0, "open": 6}],
        ["withdrawn", 5, {"event": "incision", "previous": 555, "cause": {"event": "cross_clamp", "minutes": 480, "seq": 2}}],
        ["infeasible", 6, {"cycle": ["cross_clamp", "implant", "arrival", "organ_out", "cross_clamp"],
                           "constraints": ["implant - cross_clamp <= 480  [L1]  ISCHEMIA BUDGET \u2014 ILLUSTRATIVE PLACEHOLDER, NOT A TOLERANCE"],
                           "short_by": 30}],
        ["note", 7, {"text": "quote \" backslash \\ slash / tab \t nl \n cr \r bs \b ff \f nul \u0000 bell \u0007 del \u007f e\u0301 \u65e5\u672c \U0001f600",
                     "empty": "", "list": [], "obj": {}, "neg": -1, "big": 1061109567, "t": True, "f": False, "n": None}],
        ["signout", 8, {"by": "J. Alvarez (synthetic)", "hash": "9c1e" * 16, "rows": 10, "supersedes": 9}],
    ]
    tp = T.Tape("TR-4118")
    for kind, at, body in rows:
        tp.append(kind, at, body)
    tp.verify()
    with open(os.path.join(OUT, "tape-script.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"case_id": "TR-4118", "rows": rows}, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    with open(os.path.join(OUT, "reference-tape.jsonl"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(T._canonical({"case_id": tp.case_id}) + "\n")
        fh.write(tp.to_jsonl())
    print(f"  tape: {len(tp)} rows, head {tp.head[:16]}…")
    # hash vectors for the C++ BLAKE2b, from the same hashlib the reference uses
    import hashlib
    vec = {}
    for s in ("", "abc", "The quick brown fox jumps over the lazy dog", "a" * 127, "a" * 128, "a" * 129, "a" * 1000):
        vec[s if len(s) < 60 else f"a*{len(s)}"] = hashlib.blake2b(s.encode(), digest_size=32).hexdigest()
    vec["sha256:abc"] = hashlib.sha256(b"abc").hexdigest()
    with open(os.path.join(OUT, "hash-vectors.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(vec, fh, indent=1)
        fh.write("\n")


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    print(f"reference: {REG}")
    for p in sorted(glob.glob(os.path.join(ROOT, "floor", "cases", "*.json"))):
        dump_case(p)
    dump_tape()
    print(f"written to {OUT}")
