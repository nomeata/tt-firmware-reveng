#!/usr/bin/env python3
"""inject_gme.py -- inject an embedded ARM main-binary into a tiptoi .gme, self-contained.

No tt-homebrew / build.js / gmelib / node dependency: this reproduces the GME binary-table
format directly from the firmware loaders we reverse-engineered.

How the pen finds+runs an embedded main binary (from the 2N loader gme_read_main_binary_table
@0x080a0894 and launcher gme_launch_binary_build_sysapi @0x080a03f0; the ZC3201 loader
@0x0809553c is the same routine at a different header offset):

    tableOff = u32 at header[PEN_OFFSET]        # 0xA0 ZC3201, 0xA8 ZC3202N, 0xC8 ZC3203L
    fs_seek(tableOff + 0x10); binOff = u32; binLen = u32     # the real entry (0th is skipped)
    fs_seek(binOff); read binLen bytes into the load region; jump in with r0 = &system_api

Binary-table layout (GME-Format.md, corroborated by the loader reading at +0x10):
    +0x00  u16 length + 14 bytes padding      (16-byte header == the skipped 0th entry slot)
    +0x10  entry[1]: u32 binOff, u32 binLen, char name[8]     <- what the firmware reads
    ...    (more entries would follow at +0x20, +0x30 ...)
    header[0xA4] must be 1 ("the rest of the header contains offsets"), else the pen treats
    the whole offset region as zero and never looks at the binary tables.

We only ever APPEND (table header + entry + binary bytes) to the end of the file, so no
existing script/media/game offset moves. The trailing 4-byte additive checksum (unchecked by
the pen, per GME-Format.md) is recomputed for hygiene.
"""
import argparse, struct, sys

PEN_OFFSET = {"ZC3201": 0xA0, "2N": 0xA8, "ZC3202N": 0xA8, "3L": 0xC8, "ZC3203L": 0xC8}
FLAG_OFF = 0xA4


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def checksum(body: bytes) -> int:
    # simple additive u32 over every byte preceding the 4 checksum bytes
    return sum(body) & 0xFFFFFFFF


def inject(base: bytes, injections):
    """injections: list of (pen_name, binary_bytes, name8). Returns new gme bytes.

    Appends one binary-table + binary per injection and points header[PEN_OFFSET] at each."""
    # drop the existing trailing checksum; we recompute at the end
    body = bytearray(base[:-4])
    body[FLAG_OFF] = 1  # "header contains offsets" — required for the pen to read the tables

    for pen, binary, name in injections:
        if pen not in PEN_OFFSET:
            raise ValueError("unknown pen %r (use %s)" % (pen, "/".join(sorted(PEN_OFFSET))))
        # align table start to 4 bytes
        while len(body) % 4:
            body += b"\x00"
        table_off = len(body)
        # 16-byte header: u16 length (1 real entry) + 14 pad
        body += struct.pack("<H", 1) + b"\x00" * 14
        # entry[1] at table_off+0x10: (binOff, binLen, name[8]); binary goes right after it
        entry_off = len(body)
        bin_off = entry_off + 0x10
        nm = (name.encode() + b"\x00" * 8)[:8]
        body += struct.pack("<II", bin_off, len(binary)) + nm
        assert len(body) == bin_off
        body += binary
        # point the pen's header slot at this table
        struct.pack_into("<I", body, PEN_OFFSET[pen], table_off)
        print("  injected %-8s: header[0x%02X]=0x%06x  entry@0x%06x -> binOff=0x%06x len=0x%x (%s)"
              % (pen, PEN_OFFSET[pen], table_off, entry_off, bin_off, len(binary), name))

    body += struct.pack("<I", checksum(body))
    return bytes(body)


def extract(gme: bytes, pen: str):
    """Re-read a binary exactly as the firmware loader would. Returns (binOff, binLen, bytes)."""
    off = PEN_OFFSET[pen]
    if gme[FLAG_OFF] != 1:
        return None
    table_off = _u32(gme, off)
    if table_off == 0 or table_off + 0x18 > len(gme):
        return None
    bin_off = _u32(gme, table_off + 0x10)
    bin_len = _u32(gme, table_off + 0x14)
    if bin_off == 0 or bin_off + bin_len > len(gme):
        return None
    return bin_off, bin_len, gme[bin_off:bin_off + bin_len]


def main():
    ap = argparse.ArgumentParser(description="Inject an embedded main-binary into a .gme (self-contained).")
    ap.add_argument("-i", "--base", required=True, help="input base .gme")
    ap.add_argument("-o", "--out", required=True, help="output .gme")
    ap.add_argument("-b", "--bin", action="append", default=[], metavar="PEN=FILE",
                    help="binary for a pen, e.g. 2N=build/2N.bin or ZC3201=build/ZC3201.bin (repeatable)")
    ap.add_argument("-n", "--name", default="maskrom", help="8-char entry name (default: maskrom)")
    ap.add_argument("--verify", action="store_true", help="re-parse the output like the firmware and assert bytes match")
    a = ap.parse_args()

    base = open(a.base, "rb").read()
    injections = []
    for spec in a.bin:
        if "=" not in spec:
            ap.error("--bin must be PEN=FILE, got %r" % spec)
        pen, path = spec.split("=", 1)
        injections.append((pen, open(path, "rb").read(), a.name))
    if not injections:
        ap.error("give at least one --bin PEN=FILE")

    print("base: %s (%d bytes)" % (a.base, len(base)))
    out = inject(base, injections)
    open(a.out, "wb").write(out)
    print("wrote %s (%d bytes)" % (a.out, len(out)))

    if a.verify:
        ok = True
        for pen, binary, _ in injections:
            r = extract(out, pen)
            if r is None:
                ok = False; print("  VERIFY FAIL %s: loader could not find the binary" % pen); continue
            _, _, got = r
            same = got == binary
            ok &= same
            print("  VERIFY %-8s: firmware-loader-extracted %d bytes, %s"
                  % (pen, len(got), "identical to injected binary" if same else "MISMATCH"))
        print("VERIFY:", "PASS" if ok else "FAIL")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
