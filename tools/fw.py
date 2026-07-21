#!/usr/bin/env python3
"""Analysis helpers for the tiptoi ZC3202N firmware (Update3202_dl.upd PROG section).

PROG.bin is a FLAT load: file offset F maps to runtime address BASE + F, header included.
  BASE (fw/2N-update3202MT) = 0x08009000  == runtime == pen RAM dumps == emulator (ttrun_gme.py)
                              == Ghidra image base (LOADER_BASE) after the 2026-07-02 re-base.
  PROG spans  BASE .. BASE+0x380000  (0x08009000 .. 0x08389000 for MT).
  Everything BELOW BASE (0x07ff.. / nandboot scratch) is HAL, NOT part of PROG.
Older notes/tools used BASE=0x08000000 (Ghidra 0x9000 below runtime); add 0x9000 to convert.
BASE is per-FW: read from LOADER_BASE (set by tools/fwenv.sh), else legacy 0x08000000.
"""
import sys, struct, re
# firmware image resolved via the FW env var (see tools/fwenv.sh); default fw/2N-update3202MT.
import os as _os
from capstone import *
from capstone.arm import ARM_OP_MEM, ARM_REG_PC

_REPO = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
_FW = _os.environ.get("FW", "fw/2N-update3202MT")
# Per-FW image base: honor LOADER_BASE from the environment (fwenv.sh); fall back to the
# verified base for MT, else the legacy 0x08000000.
_DEFAULT_BASE = 0x08009000 if _FW.rstrip("/").endswith("2N-update3202MT") else 0x08000000
BASE = int(_os.environ.get("LOADER_BASE", hex(_DEFAULT_BASE)), 16)
_PROG = _FW + "/data/PROG.bin" if _os.path.isabs(_FW) else _os.path.join(_REPO, _FW, "data/PROG.bin")
DATA = open(_os.environ.get("PROG_BIN", _PROG), "rb").read()
END  = BASE + len(DATA)

md_arm   = Cs(CS_ARCH_ARM, CS_MODE_ARM   | CS_MODE_LITTLE_ENDIAN)
md_thumb = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
for m in (md_arm, md_thumb):
    m.detail = True

def b2a(off):  return BASE + off          # file offset -> address
def a2b(addr): return addr - BASE          # address -> file offset
def inrange(addr): return BASE <= addr < END

def word(addr):
    o=a2b(addr); return struct.unpack_from("<I", DATA, o)[0]
def half(addr):
    o=a2b(addr); return struct.unpack_from("<H", DATA, o)[0]
def byte(addr):
    return DATA[a2b(addr)]

def disasm(addr, n=40, thumb=False, end=None):
    """Print n instructions (or until end addr) from addr."""
    md = md_thumb if thumb else md_arm
    o=a2b(addr)
    code=DATA[o: o + (n*4 if end is None else (end-addr)) + 16]
    cnt=0
    for ins in md.disasm(code, addr):
        if end is not None and ins.address>=end: break
        # annotate pc-relative literal loads
        ann=""
        if ins.mnemonic.startswith("ldr") and "[pc" in ins.op_str:
            # capstone resolves pc-rel; compute target
            for op in ins.operands:
                if op.type==ARM_OP_MEM and op.mem.base==ARM_REG_PC:
                    pcbase=(ins.address+ (4 if thumb else 8)) & (~3 if thumb else ~0)
                    tgt=pcbase+op.mem.disp
                    if inrange(tgt):
                        ann=f"  ; =0x{word(tgt):08x}"
        print(f"0x{ins.address:08x}: {ins.bytes.hex():<8} {ins.mnemonic:<7} {ins.op_str}{ann}")
        cnt+=1
        if end is None and cnt>=n: break

def find_word(value, start=0, stop=None):
    """All addresses whose 32-bit LE word == value."""
    stop = stop if stop is not None else len(DATA)
    res=[]
    i=start
    pat=struct.pack("<I", value & 0xffffffff)
    while True:
        j=DATA.find(pat, i, stop)
        if j<0: break
        res.append(b2a(j))
        i=j+1   # allow unaligned; filter later
    return res

def find_word_aligned(value):
    return [a for a in find_word(value) if a%4==0]

def find_bytes(pat, start=0):
    if isinstance(pat,str): pat=bytes.fromhex(pat)
    res=[]; i=start
    while True:
        j=DATA.find(pat,i)
        if j<0: break
        res.append(b2a(j)); i=j+1
    return res

def find_str(s):
    if isinstance(s,str): s=s.encode()
    return find_bytes(s)

def xrefs_bl(target):
    """Find ARM BL/B/BLX immediate branches whose target == addr (scan aligned)."""
    res=[]
    for o in range(0, len(DATA)-4, 4):
        w=struct.unpack_from("<I",DATA,o)[0]
        cond=(w>>28)&0xf
        op=(w>>24)&0xf
        if op in (0xa,0xb):  # B / BL (cond)
            imm=w&0xffffff
            if imm&0x800000: imm-=0x1000000
            tgt=b2a(o)+8+imm*4
            if tgt==target: res.append((b2a(o),"bl" if op==0xb else "b"))
        # BLX (unconditional) cond==0xf, op 0xa/0xb
    return res

def xrefs_word(target):
    """Find aligned .word == target (pointer references / literal pools)."""
    return find_word_aligned(target)

if __name__=="__main__":
    cmd=sys.argv[1] if len(sys.argv)>1 else ""
    if cmd=="d":
        addr=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 40
        thumb = len(sys.argv)>4 and sys.argv[4]=="t"
        disasm(addr,n,thumb)
    elif cmd=="w":
        for a in find_word_aligned(int(sys.argv[2],16)): print(f"0x{a:08x}")
    elif cmd=="s":
        for a in find_str(sys.argv[2]): print(f"0x{a:08x}")
    elif cmd=="bl":
        for a,k in xrefs_bl(int(sys.argv[2],16)): print(f"0x{a:08x} {k}")
