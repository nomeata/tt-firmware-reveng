#!/usr/bin/env python3
"""Call-graph + utility-function finder for the ARM firmware."""
import struct, collections
import fw
DATA=fw.DATA; BASE=fw.BASE; END=fw.END

def is_code(addr): return 0x08009000<=addr<0x08120000

# --- scan all ARM BL / B targets (aligned) ---
bl_sites=collections.defaultdict(list)   # target -> [sites]
b_sites =collections.defaultdict(list)
for o in range(0,len(DATA)-4,4):
    w=struct.unpack_from("<I",DATA,o)[0]
    cond=(w>>28)&0xf
    op=(w>>24)&0xf
    if op==0xb and cond!=0xf:   # BL
        imm=w&0xffffff
        if imm&0x800000: imm-=0x1000000
        tgt=BASE+o+8+imm*4
        bl_sites[tgt].append(BASE+o)
    elif op==0xa and cond!=0xf: # B
        imm=w&0xffffff
        if imm&0x800000: imm-=0x1000000
        tgt=BASE+o+8+imm*4
        b_sites[tgt].append(BASE+o)

# string table (addr->str) for format-string association
strs={}
i=0;n=len(DATA)
while i<n:
    j=i
    while j<n and 32<=DATA[j]<127: j+=1
    if j-i>=3:
        strs[BASE+i]=DATA[i:j].decode('latin1'); i=j
    else: i+=1

def reg_loaded_str_before(site, reg='r0', back=10):
    """Look back `back` ARM instrs for ldr reg,[pc,#x] resolving into strs."""
    res=[]
    for k in range(1,back+1):
        a=site-4*k
        if a<BASE: break
        w=fw.word(a)
        # ldr Rt,[pc,#imm]  : cond 1110, 0101 1001 1111 Rt imm12  -> E59F_xxxx
        if (w&0x0f7f0000)==0x051f0000:
            rt=(w>>12)&0xf
            imm=w&0xfff
            if (w&0x00800000)==0: imm=-imm
            tgt=(a+8)+imm
            if fw.inrange(tgt):
                val=fw.word(tgt)
                if val in strs: res.append((rt,val,strs[val]))
    return res

if __name__=="__main__":
    import sys
    if sys.argv[1]=="top":
        # most-called bl targets
        items=sorted(bl_sites.items(), key=lambda kv:-len(kv[1]))[:40]
        for t,sites in items:
            print(f"0x{t:08x}  calls={len(sites)}  code={is_code(t)}")
    elif sys.argv[1]=="printf":
        # target whose callers most often load a known string into some reg
        score=collections.Counter()
        samples=collections.defaultdict(list)
        for t,sites in bl_sites.items():
            if not is_code(t): continue
            for s in sites[:200]:
                hits=reg_loaded_str_before(s)
                if hits:
                    score[t]+=1
                    if len(samples[t])<5: samples[t].append((hex(s),hits[0][2][:40]))
        for t,c in score.most_common(15):
            print(f"0x{t:08x}  string-callers={c}  total={len(bl_sites[t])}")
            for ex in samples[t]: print("     ",ex)
    elif sys.argv[1]=="callers":
        t=int(sys.argv[2],16)
        for s in bl_sites.get(t,[]): print(f"0x{s:08x}")
