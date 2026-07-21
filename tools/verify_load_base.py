#!/usr/bin/env python3
"""Verify a variant's PROG load base against the image's own absolute pointers.

The load base is a silent, poisonous thing to get wrong (every function address off by a
constant). This checks it independently of names.csv / the emulator: it scans PROG for words
that look like internal code pointers and, for each candidate base, counts how many resolve to
a function prologue. The correct base produces a sharp peak; the wrong base is near-noise.

    tools/verify_load_base.py [VARIANT]      # default: 2N-update3202MT

It reports the peak base and warns if it disagrees with `loader_base` in firmware.json.
Validated: it recovers the 2N-MT's known 0x08009000 and the ZC3201's 0x08008000.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def _prologue(d: bytes, off: int, thumb: bool) -> bool:
    if thumb:
        if off < 0 or off + 2 > len(d):
            return False
        h = struct.unpack_from("<H", d, off)[0]
        return (h & 0xFE00) == 0xB400 and bool(h & 0x0100)  # push {..,lr}
    if off < 0 or off + 4 > len(d):
        return False
    w = struct.unpack_from("<I", d, off)[0]
    return ((w & 0xFFFF0000) == 0xE92D0000 and bool(w & 0x4000)) or w in (0xE52DE004, 0xE1A0C00D)


def _rate(d: bytes, base: int) -> "tuple[int, int]":
    n = len(d)
    lo, hi = base, base + n
    cand = hit = 0
    for off in range(0, n - 4, 4):
        w = struct.unpack_from("<I", d, off)[0]
        if lo <= (w & ~1) < hi:
            cand += 1
            if _prologue(d, (w & ~1) - base, bool(w & 1)):
                hit += 1
    return hit, cand


def main() -> None:
    variant = sys.argv[1] if len(sys.argv) > 1 else "2N-update3202MT"
    variant = variant[3:] if variant.startswith("fw/") else variant
    fw_dir = REPO_ROOT / variant
    manifest = json.loads((fw_dir / "firmware.json").read_text())
    declared = int(str(manifest.get("loader_base", "0x08000000")), 16)
    prog = fw_dir / "data" / "PROG.bin"
    if not prog.exists():
        sys.exit(f"{prog} not present — run tools/fetch_firmware.py {variant} first")
    d = prog.read_bytes()

    # sweep a window of 4 KiB-aligned bases around the usual 0x08000000 region
    results = []
    for base in range(0x08000000, 0x08010000 + 1, 0x1000):
        hit, cand = _rate(d, base)
        results.append((hit, base, cand))
    results.sort(reverse=True)
    peak_hit, peak_base, peak_cand = results[0]

    print(f"variant {variant}: declared loader_base {declared:#010x}")
    print("top candidate bases by prologue-resolution rate:")
    for hit, base, cand in results[:4]:
        mark = "  <-- PEAK" if base == peak_base else ""
        flag = "  (declared)" if base == declared else ""
        print(f"  {base:#010x}: {hit:5d}/{cand:6d} ({100 * hit / max(cand, 1):5.1f}%){mark}{flag}")

    if peak_base == declared:
        print(f"OK: declared base {declared:#010x} matches the evidence peak.")
    else:
        print(
            f"WARNING: evidence peak {peak_base:#010x} != declared loader_base "
            f"{declared:#010x} — the load base is likely wrong."
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
