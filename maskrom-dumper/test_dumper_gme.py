#!/usr/bin/env python3
"""test_dumper_gme.py -- end-to-end test of the ROM-dumper *as delivered in a .gme*.

Chain under test:  make_gme.py/inject_gme.py  ->  (firmware-loader) extract the binary  ->  emulate
it against a synthesized system_api  ->  assert it (a) writes the ROM to the right files and (b)
plays the right media via play_sound.

The synthesized system_api mirrors the real one field-for-field (offsets from api.h / the launcher).
play_sound + read + seek are modelled against the ACTUAL bytes of the GME under test, so a play_sound
call resolves to a media-table index exactly as the firmware would.  Real audio decode is a hardware
behaviour we cannot emulate; the verifiable bar is "play_sound called with the correct (offset,length)
from a well-formed media table" -- i.e. the right media INDEX -- which is what we assert.

  usage: test_dumper_gme.py --gme dumper.gme --pen 2N
         test_dumper_gme.py --bin build/2N.bin --pen 2N          # emulate a raw .bin (no media checks)
"""
import argparse, os, struct
from unicorn import *
from unicorn.arm_const import *
import inject_gme, make_gme

# Per-pen config. system_api field offsets = field_index*4 (from api.h / the firmware launcher).
PEN = {
    "2N": dict(
        CODE=0x08132000, ROM=0x00000000, SENT=0x09000000, STACK=0x081f0000,
        MAP=[(0x00000000, 0x10000), (0x07ff8000, 0x8000), (0x08000000, 0x380000), (0x09000000, 0x1000)],  # full fw RAM (live dump)
        SCRATCH=0x08160000, FWBASE=0x08000000,
        F_AUDIO=0x0c, F_OPEN=0x14, F_READ=0x18, F_WRITE=0x1c, F_CLOSE=0x20, F_SEEK=0x24,
        F_PLAY=0x2c, F_OID=0x34, F_GME_FH=0x38, FIRST_TIME_EXEC=0xdec,
    ),
    # ZC3201: fields 0..11 match 2N (open/read/write/close/seek/play_sound all verified same); the
    # struct diverges at field 13 (fpAkOidPara @0x3c, ptr to akoidpara+0x22 = F_OID_BIAS). play_sound
    # is verified @0x2c, so the media cue uses the SAME path; F_GME_FH (field 14) offset is set below.
    "ZC3201": dict(
        CODE=0x080ea000, ROM=0x00000000, SENT=0x09000000, STACK=0x080f8000,
        MAP=[(0x00000000, 0x10000), (0x07ff8000, 0x8000), (0x08000000, 0x380000), (0x09000000, 0x1000)],  # full fw RAM (live dump)
        SCRATCH=0x08160000, FWBASE=0x08000000,
        F_AUDIO=0x0c, F_OPEN=0x14, F_READ=0x18, F_WRITE=0x1c, F_CLOSE=0x20, F_SEEK=0x24,
        F_PLAY=0x2c, F_OID=0x3c, F_OID_BIAS=0x22, F_GME_FH=0x4c, FIRST_TIME_EXEC=0xd6a,
    ),
}


def run_dumper(code: bytes, cfg: dict, gme: bytes = None):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN)
    for b, s in cfg["MAP"]:
        uc.mem_map(b, s)
    uc.mem_write(cfg["CODE"], code)
    uc.mem_write(cfg["FWBASE"], b"BIOS\x00\x00\x00\x00" + b"\xa5" * 0x100)
    rom = b"".join(struct.pack("<I", 0xA0000000 | a) for a in range(0, 0x10000, 4))
    uc.mem_write(cfg["ROM"], rom)
    SCRATCH, SENT = cfg["SCRATCH"], cfg["SENT"]
    AKOID = SCRATCH + 0x800
    GME_FH = 0x100                       # our fake GME filehandle value
    FH_CELL = SCRATCH + 0xb00            # p_filehandle_current_gme points HERE (holds GME_FH)
    uc.mem_write(AKOID, b"\x00" * 0x2000)
    uc.mem_write(FH_CELL, struct.pack("<I", GME_FH))
    sys = bytearray(0x200)
    for off in range(0, 0x200, 4):
        struct.pack_into("<I", sys, off, SENT + 0xf00)
    slots = [(cfg["F_AUDIO"], 0x00), (cfg["F_OPEN"], 0x10), (cfg["F_WRITE"], 0x20), (cfg["F_CLOSE"], 0x30)]
    if cfg.get("F_CHOMP") is not None:
        slots.append((cfg["F_CHOMP"], 0x40))
    if cfg.get("F_PLAY") is not None:
        slots += [(cfg["F_READ"], 0x50), (cfg["F_SEEK"], 0x60), (cfg["F_PLAY"], 0x70)]
    for off, s in slots:
        struct.pack_into("<I", sys, off, SENT + s)
    struct.pack_into("<I", sys, cfg["F_OID"], AKOID + cfg.get("F_OID_BIAS", 0))
    if cfg.get("F_GME_FH") is not None:
        struct.pack_into("<I", sys, cfg["F_GME_FH"], FH_CELL)
    uc.mem_write(SCRATCH, bytes(sys))
    uc.mem_write(SENT + 0xf00, struct.pack("<I", 0xe12fff1e))  # bx lr

    # --- Seed a synthetic MMU page table so the dumper's L1->L2 walk is exercised
    # structurally.  We set TTBR0 (CP15 c2,c0,0) to a table WE control but leave
    # SCTLR's MMU-enable bit at the emulator default 0 -- so cpregs still honestly
    # reports MMU=off (Unicorn's real state) while the dumper's page-table code
    # path (guard -> L1 dump -> coarse-descriptor walk -> dedup -> L2 dumps) runs
    # against a known table.  On a real pen TTBR0 is the pen's live value.
    TTBR0_SEED = 0x08004000
    seeded = False
    try:
        uc.reg_write(UC_ARM_REG_CP_REG, (15, 0, 0, 2, 0, 0, 0, TTBR0_SEED))  # TTBR0 = c2,c0,0
        seeded = True
    except Exception as e:                                    # older unicorn without CP_REG
        print("note: could not seed TTBR0 (%s) -- page-table walk will be skipped" % e)
    if seeded:
        L1 = bytearray(0x4000)                               # 4096 entries
        def _l1(i, v): struct.pack_into("<I", L1, i * 4, v)
        _l1(0, 0x08010000 | 1)   # coarse PT descriptor -> L2 base 0x08010000
        _l1(1, 0x08010000 | 1)   # duplicate base            -> dedup (no 2nd file)
        _l1(2, 0x08010400 | 1)   # coarse PT descriptor -> L2 base 0x08010400
        _l1(3, 0x00000002)       # 1MB section (type 10)     -> not an L2, skipped
        _l1(4, 0x00000000)       # fault (type 00)           -> skipped
        _l1(5, 0x00000001)       # coarse but base 0         -> guard skip (out of RAM range)
        uc.mem_write(TTBR0_SEED, bytes(L1))
        uc.mem_write(0x08010000, b"\xa1" * 0x400)            # recognizable L2 #0
        uc.mem_write(0x08010400, b"\xb2" * 0x400)            # recognizable L2 #1

    # (The physical-NAND boot-block dump was removed from the dumper -- it powered
    # a real pen off and isn't needed for the MMU/page-table goal -- so there is
    # no NAND read primitive to stub here anymore.)

    files, nh, cursor = {}, [10], {GME_FH: 0}
    calls = dict(open=0, write=0, close=0, audio=0, chomp=0, read=0, seek=0, play=0)
    plays = []                            # (offset, length) passed to play_sound, in call order

    def rd_wstr(a):
        s = b""
        while True:
            w = uc.mem_read(a, 2); a += 2
            if w == b"\x00\x00":
                break
            s += w
        return s.decode("utf-16-le")

    def on_code(u, addr, size, ud):
        base = addr - SENT
        if base == 0x00:
            calls["audio"] += 1; u.reg_write(UC_ARM_REG_R0, 0)      # idle, so the finish cue fires
        elif base == 0x10:
            p = rd_wstr(u.reg_read(UC_ARM_REG_R0)); h = nh[0]; nh[0] += 1
            files[h] = dict(path=p, data=bytearray()); calls["open"] += 1
            u.reg_write(UC_ARM_REG_R0, h)
        elif base == 0x20:
            fh = u.reg_read(UC_ARM_REG_R0); buf = u.reg_read(UC_ARM_REG_R1); n = u.reg_read(UC_ARM_REG_R2)
            files.setdefault(fh, dict(path="?", data=bytearray()))["data"] += bytes(u.mem_read(buf, n))
            calls["write"] += 1; u.reg_write(UC_ARM_REG_R0, n)
        elif base == 0x30:
            calls["close"] += 1; u.reg_write(UC_ARM_REG_R0, 0)
        elif base == 0x40:
            calls["chomp"] += 1
        elif base == 0x50:                                          # read(fh, buf, n)
            fh = u.reg_read(UC_ARM_REG_R0); buf = u.reg_read(UC_ARM_REG_R1); n = u.reg_read(UC_ARM_REG_R2)
            data = (gme or b"")[cursor.get(fh, 0):cursor.get(fh, 0) + n]
            u.mem_write(buf, bytes(data) + b"\x00" * (n - len(data))); cursor[fh] = cursor.get(fh, 0) + n
            calls["read"] += 1; u.reg_write(UC_ARM_REG_R0, len(data))
        elif base == 0x60:                                          # seek(fh, pos, 0)
            cursor[u.reg_read(UC_ARM_REG_R0)] = u.reg_read(UC_ARM_REG_R1)
            calls["seek"] += 1; u.reg_write(UC_ARM_REG_R0, 0)
        elif base == 0x70:                                          # play_sound(fh, offset, size)
            plays.append((u.reg_read(UC_ARM_REG_R1), u.reg_read(UC_ARM_REG_R2)))
            calls["play"] += 1; u.reg_write(UC_ARM_REG_R0, 0)
        else:
            return
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, on_code, begin=SENT, end=SENT + 0x1000)
    RET = cfg["CODE"] + 0x7000
    uc.reg_write(UC_ARM_REG_SP, cfg["STACK"]); uc.reg_write(UC_ARM_REG_LR, RET)
    uc.reg_write(UC_ARM_REG_R0, SCRATCH); uc.reg_write(UC_ARM_REG_CPSR, 0x13)
    # re-enter main() like the firmware's ~10Hz event loop until the done-marker lands (the
    # imager streams one region per call), capped so a bug can't spin forever.
    for _ in range(300):
        uc.reg_write(UC_ARM_REG_SP, cfg["STACK"]); uc.reg_write(UC_ARM_REG_LR, RET)
        uc.reg_write(UC_ARM_REG_R0, SCRATCH); uc.reg_write(UC_ARM_REG_CPSR, 0x13)
        try:
            uc.emu_start(cfg["CODE"], RET, count=5_000_000)
        except UcError as e:
            print("emu stopped:", e, "PC=0x%08x" % uc.reg_read(UC_ARM_REG_PC)); break
        if any("TTMDONE" in files[h]["path"] for h in files):
            break
    fte = uc.mem_read(AKOID + cfg.get("F_OID_BIAS", 0) + cfg["FIRST_TIME_EXEC"], 1)[0]
    return files, calls, rom, fte, plays


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gme"); ap.add_argument("--bin"); ap.add_argument("--pen", default="2N")
    ap.add_argument("--out")
    a = ap.parse_args()
    cfg = PEN.get(a.pen)
    if cfg is None:
        raise SystemExit("no emulation config for pen %r yet" % a.pen)
    gme = None
    if a.gme:
        gme = open(a.gme, "rb").read()
        r = inject_gme.extract(gme, a.pen)
        if r is None:
            raise SystemExit("could not extract %s binary from %s" % (a.pen, a.gme))
        _, _, code = r
        print("source: %s -> firmware-loader-extracted %d-byte %s binary" % (a.gme, len(code), a.pen))
    elif a.bin:
        code = open(a.bin, "rb").read(); print("source: %s (%d bytes)" % (a.bin, len(code)))
    else:
        raise SystemExit("give --gme or --bin")

    files, calls, rom, fte, plays = run_dumper(code, cfg, gme)
    print("calls:", calls)
    for h in sorted(files):
        print("  fh=%d  %-26s  %d bytes" % (h, files[h]["path"], len(files[h]["data"])))
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for h in sorted(files):
            open(os.path.join(a.out, files[h]["path"].split("/")[-1]), "wb").write(files[h]["data"])

    ok = True
    # canary + done marker must be written to B: ROOT (no directory)
    for p in ("B:/TTMDUMP.LOG", "B:/TTMDONE.TXT"):
        if not any(x["path"] == p for x in files.values()):
            ok = False; print("FAIL: %s (root canary) missing" % p)
        else:
            print("PASS: %s present" % p)
    for p, n in (("B:/mrom_sysapi.bin", 0x200), ("B:/mrom_fwhead.bin", 0x100),
                 ("B:/cpregs.bin", 0x20)):
        f = next((x for x in files.values() if x["path"] == p), None)
        if not f or len(f["data"]) != n:
            ok = False; print("FAIL: %s missing/short" % p)
        else:
            print("PASS: %s (%d bytes)" % (p, n))

    # cpregs.bin: fixed struct -- magic "CP15" then MIDR/SCTLR/TTBR0/DACR/DFSR/FAR/FCSE.
    # The emulated pen's MMU is off (SCTLR bit0 == 0); we only assert the file is
    # well-formed (magic + decodable SCTLR/TTBR0), not the pen's real values.
    ttbr0 = 0
    cp = next((x for x in files.values() if x["path"] == "B:/cpregs.bin"), None)
    if cp and len(cp["data"]) == 0x20:
        magic = bytes(cp["data"][:4])
        sctlr = struct.unpack("<I", bytes(cp["data"][8:12]))[0]
        ttbr0 = struct.unpack("<I", bytes(cp["data"][12:16]))[0]
        if magic != b"CP15":
            ok = False; print("FAIL: cpregs.bin magic %r (expect b'CP15')" % magic)
        else:
            print("PASS: cpregs.bin magic 'CP15', SCTLR=0x%08x MMU=%s, TTBR0=0x%08x"
                  % (sctlr, "ON" if sctlr & 1 else "off", ttbr0))

    # Page tables: dumped only when TTBR0 points into firmware RAM.  When the test
    # seeds TTBR0 (0x08004000) we get a full run of the L1 dump + L1->L2 walk and
    # assert the structure; otherwise (TTBR0 not seeded / MMU off) they're absent.
    l1 = next((x for x in files.values() if x["path"] == "B:/l1pt.bin"), None)
    l2s = {x["path"]: x for x in files.values() if x["path"].startswith("B:/l2_")}
    if 0x08000000 <= (ttbr0 & ~0x3fff) < 0x08380000:
        if not l1 or len(l1["data"]) != 0x4000:
            ok = False; print("FAIL: l1pt.bin missing/short (expect 16384 bytes)")
        else:
            print("PASS: l1pt.bin = 16384 bytes (full L1 table)")
        # synthetic L1 seeds distinct in-range L2 bases 0x08010000 and 0x08010400
        # (plus a duplicate -> deduped, and a base-0 -> guard-skipped).
        want = {"B:/l2_08010000.bin", "B:/l2_08010400.bin"}
        got = set(l2s)
        if got == want and all(len(l2s[p]["data"]) == 0x400 for p in want):
            print("PASS: L1->L2 walk produced %s (deduped, guarded, 1KB each)"
                  % sorted(x.split('/')[-1] for x in got))
        else:
            ok = False
            print("FAIL: L2 dumps = %r (expect exactly %r, 1KB each)"
                  % (sorted(got), sorted(want)))
        if "B:/l2_00000000.bin" in l2s:
            ok = False; print("FAIL: out-of-range L2 base 0 was read (guard breached)")
    else:
        if l1 or l2s:
            ok = False; print("FAIL: page tables dumped despite TTBR0 out of range")
        else:
            print("PASS: page tables absent (TTBR0 not in firmware RAM / MMU off)")

    print("First_time_exec after run:", fte, "(expect 1)")
    ok = ok and fte == 1

    # media/audio-cue check: resolve each play_sound (offset,length) to a media-table index.
    if gme is not None:
        media = make_gme.media_entries(gme)
        idx_of = {(o, l): i for i, (o, l) in enumerate(media)}
        played_idx = [idx_of.get(p, "?") for p in plays]
        print("play_sound calls -> media indices:", played_idx, "(media table has %d entries)" % len(media))
        if media:
            if played_idx and played_idx[0] == 0:
                print("PASS: start cue plays media[0] (MEDIA_START)")
            else:
                ok = False; print("FAIL: start cue did not play media[0] first (got %r)" % (played_idx[:1],))
            if 1 in played_idx:
                print("PASS: finish cue plays media[1] (MEDIA_DONE)")
            else:
                ok = False; print("FAIL: finish cue (media[1]) never played")
            if any(p == "?" for p in played_idx):
                ok = False; print("FAIL: a play_sound offset didn't match any media-table entry")

    print("\nRESULT:", "ALL CHECKS PASS" if ok else "FAILURES ABOVE")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
