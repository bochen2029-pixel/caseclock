#!/usr/bin/env python3
"""
caseclock · tests · drive.py — drive a running strip through its test seam (BLUEPRINT §6).

    python tests/drive.py fact serology_drawn 22:10 [--by NAME]     post a fact (WM_COPYDATA, dwData WM_APP+7)
    python tests/drive.py find                                      print the strip's window handle

The strip logs what it did to the file named by CASECLOCK_LOG (or --log FILE); read that back to
assert. Zero dependencies: ctypes only. Synthetic cases only.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as w
import sys

WM_APP = 0x8000
WM_COPYDATA = 0x004A
WM_APP_FACT = WM_APP + 7
user32 = ctypes.windll.user32


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [("dwData", ctypes.c_size_t), ("cbData", w.DWORD), ("lpData", ctypes.c_void_p)]


def find() -> int:
    h = user32.FindWindowW("caseclockStrip", None)
    if not h:
        sys.exit("no caseclock strip is running")
    return h


def fact(event: str, time: str, by: str) -> int:
    h = find()
    payload = f"event={event};minutes={time};by={by}".encode("utf-8")
    buf = ctypes.create_string_buffer(payload, len(payload))
    cds = COPYDATASTRUCT(WM_APP_FACT, len(payload), ctypes.cast(buf, ctypes.c_void_p))
    return user32.SendMessageW(h, WM_COPYDATA, 0, ctypes.byref(cds))


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        raise SystemExit(2)
    if a[0] == "find":
        print(hex(find()))
    elif a[0] == "fact" and len(a) >= 3:
        by = a[a.index("--by") + 1] if "--by" in a else "driver"
        print("accepted" if fact(a[1], a[2], by) else "rejected (see the log)")
    else:
        print(__doc__)
        raise SystemExit(2)
