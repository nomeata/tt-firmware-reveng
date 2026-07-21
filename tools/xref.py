#!/usr/bin/env python3
"""Build a cross-reference map handling ARM+Thumb literal loads and ADR.

For every code location that materialises a constant address (pointer/string),
record (site, target, mode, kind). Self-validating: we keep ADR/ldr hits whose
resolved target lands inside the image (and especially on a known string)."""
import struct, collections, sys, json
import fw
DATA=fw.DATA; BASE=fw.BASE; END=fw.END; n=len(DATA)

def w32(o): return struct.unpack_from("<I",DATA,o)[0]
def w16(o): return struct.unpack_from("<H",DATA,o)[0]

# --- string table ---
strs={}
i=0
while i<n:
    j=i
    while j<n and 32<=DATA[j]<127: j+=1
    if j-i>=3:
        strs[BASE+i]=DATA[i:j].decode('latin1'); i=j
    else: i+=1

refs=collections.defaultdict(list)   # target_addr -> [(site,mode,kind)]
def add(site,tgt,mode,kind):
    if BASE<=tgt<END: refs[tgt].append((site,mode,kind))

# ARM pass (word aligned)
for o in range(0,n-3,4):
    w=w32(o); site=BASE+o
    # ldr Rt,[pc,#imm]   cond=14, 0101 1001 1111  (E59F xxxx); also U bit
    if (w&0x0f7f0000)==0x051f0000:
        imm=w&0xfff
        if not (w&0x00800000): imm=-imm
        pool=BASE+o+8+imm
        if BASE<=pool<END:
            val=w32(pool-BASE)
            add(site,val,"A","ldr=")     # value loaded
    # add/sub Rd,pc,#imm12 (ADR)  E28Fxxxx / E24Fxxxx (cond=14)
    elif (w&0x0fff0000)==0x028f0000:   # add rd,pc,#imm (rotated)
        imm=w&0xff; rot=(w>>8)&0xf; val=(imm>>(2*rot))|((imm<<(32-2*rot))&0xffffffff) if rot else imm
        add(site,(BASE+o+8+val)&0xffffffff,"A","adr+")
    elif (w&0x0fff0000)==0x024f0000:   # sub rd,pc,#imm
        imm=w&0xff; rot=(w>>8)&0xf; val=(imm>>(2*rot))|((imm<<(32-2*rot))&0xffffffff) if rot else imm
        add(site,(BASE+o+8-val)&0xffffffff,"A","adr-")

# THUMB pass (halfword aligned)
for o in range(0,n-1,2):
    h=w16(o); site=BASE+o
    # LDR Rt,[pc,#imm8*4]  : 01001 ttt iiiiiiii  (0x4800..0x4FFF)
    if (h&0xf800)==0x4800:
        imm=(h&0xff)*4
        pool=((BASE+o+4)&~3)+imm
        if BASE<=pool<END:
            add(site,w32(pool-BASE),"T","ldr=")
    # ADR: ADD Rd,pc,#imm8*4 : 10100 ddd iiiiiiii (0xA000..0xA7FF)
    elif (h&0xf800)==0xa000:
        imm=(h&0xff)*4
        add(site,((BASE+o+4)&~3)+imm,"T","adr")

def who_refs(tgt):
    return refs.get(tgt,[])

# string -> referencing sites
str_refs={a:refs[a] for a in strs if a in refs}

if __name__=="__main__":
    cmd=sys.argv[1]
    if cmd=="str":  # find refs to a string by substring
        pat=sys.argv[2]
        for a,s in strs.items():
            if pat in s and a in refs:
                print(f'\n"{s}"  @0x{a:08x}')
                for site,mode,kind in refs[a][:12]:
                    print(f"   ref 0x{site:08x} [{mode}/{kind}]")
    elif cmd=="to":  # who references this address
        t=int(sys.argv[2],16)
        for site,mode,kind in who_refs(t): print(f"0x{site:08x} [{mode}/{kind}]")
    elif cmd=="at":  # what string/addr does a site reference
        s=int(sys.argv[2],16)
        for t,lst in refs.items():
            for site,mode,kind in lst:
                if site==s: print(f"-> 0x{t:08x} [{mode}/{kind}] {strs.get(t,'')[:60]}")
    elif cmd=="stats":
        print("strings:",len(strs),"referenced strings:",len(str_refs),"total ref targets:",len(refs))
