# Ghidra headless: (1) map the recovered bootrom @0x07ff8000 and name its routines,
# (2) apply names.csv to firmware functions, (3) re-decompile everything to decomp_named/.
import os, jarray
from ghidra.program.model.symbol import SourceType
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

# Per-firmware paths come from the environment (set by regen.sh via tools/fwenv.sh);
# the defaults below target the flagship fw/2N-update3202MT so the script also works when
# run standalone. (Derived from REPO_ROOT, which fwenv.sh exports, with a relative fallback;
# not from __file__, which Ghidra's Jython runner does not define.)
_DEF=os.path.join(os.environ.get("REPO_ROOT", ""), "fw", "2N-update3202MT")
NAMES=os.environ.get("NAMES_CSV",   _DEF+"/input/names.csv")
HDR  =os.environ.get("TYPES_H",     _DEF+"/input/ghidra_types.h")
DEC  =os.environ.get("DECOMP_OUT",  _DEF+"/out/decomp_named")
try: os.makedirs(DEC)
except: pass

fm=currentProgram.getFunctionManager()
st=currentProgram.getSymbolTable()
mem=currentProgram.getMemory()
af=currentProgram.getAddressFactory().getDefaultAddressSpace()
def A(x): return af.getAddress(x)

# The hardcoded PROG-space labels below (DATA_NAMES, statechart globals) were written against
# the legacy Ghidra image base 0x08000000. When a FW is imported at a different LOADER_BASE
# (e.g. fw/2N-update3202MT at 0x08009000 == runtime), shift PROG-space addresses by the delta
# so they still land on the same file offset. Bootrom (0x07ff...) addresses are HAL-mapped and
# are NOT shifted. names.csv / ghidra_types.h addresses are ALREADY authored at the FW's base,
# so they are consumed as-is (no shift).
CANON_BASE  = 0x08000000
LOADER_BASE = int(os.environ.get("LOADER_BASE", "0x08000000"), 16)
_SHIFT = LOADER_BASE - CANON_BASE
def P(x):  # shift a hardcoded PROG-space (>=0x08000000) address to the active loader base
    return x + _SHIFT if x >= CANON_BASE else x

# ---- 1. map bootrom blob at 0x07ff8000 ----
BR_PATH=os.environ.get("NANDBOOT_BIN", _DEF+"/data/nandboot.bin")
BR_BASE=0x07ff8000
if os.path.exists(BR_PATH):
    data=open(BR_PATH,"rb").read()
    if mem.getBlock(A(BR_BASE)) is None:
        blk=mem.createInitializedBlock("bootrom", A(BR_BASE), len(data), 0, ConsoleTaskMonitor(), False)
        blk.setExecute(True); blk.setRead(True); blk.setWrite(True)
    arr=jarray.zeros(len(data),'b')
    for i in range(len(data)):
        b=ord(data[i]) if isinstance(data[i],str) else data[i]
        arr[i]=(b-256) if b>127 else b
    mem.setBytes(A(BR_BASE), arr)
    print("mapped bootrom @0x%08x (%d bytes)"%(BR_BASE,len(data)))

BOOTROM={
 0x07ffab38:"br_udivmod",0x07ffaa80:"br_memcpy",0x07ffb24c:"br_memcpy_fast",
 0x07ffb914:"br_gpio_read",0x07ffb908:"br_gpio_write",0x07ffb920:"br_gpio_config",
 0x07ffb89c:"br_gpio_dispatch",0x07ffbd7c:"br_delay_ms",0x07ffa90c:"br_delay_us",
 0x07ffa3f4:"br_nand_read",0x07ffbae0:"br_timer_schedule",0x07ffad2c:"br_timer_subsys",
 0x07ffef10:"br_clock_query",0x07ffaa78:"br_dbg_print_stub",0x07ffafd4:"br_sched_time_query",
 0x07ffad74:"br_wait_state_flag",0x07ff9610:"br_cache_clean_range",0x07ff98d8:"br_cache_region_check",
 0x07ffba2c:"br_byte_getter",0x07ffeed8:"br_table_lookup",0x07ffaddc:"br_reg_pack",
 # ---- Resident nandboot HAL (RE 2026-07-03/04, hw-io-oid-and-dac.md + state8-to-13-transition.md +
 # ttrun-gme.md; all Proven, valid ARM prologues). Their runtime home is 0x08000000 but the decomp maps
 # nandboot.bin at 0x07ff8000, so the ghidra address = runtime - 0x8000 (shown as "-> rt 0x0800xxxx").
 0x07ff8110:"irq_vector_stub",     # -> rt 0x08000110  IRQ vector entry stub: push {r0-r12,lr}; sb=pending&enable
 0x07ffd534:"irq_aggregate",       # -> rt 0x08005534  1st-level IRQ aggregator (timer bit 0x400), ends bx lr
 0x07ffdc8c:"timer_dispatch",      # -> rt 0x08005c8c  timer dispatch: inc tick *(0x08008d24), ack, walk sw-timer slots
 0x07fff80c:"hal_timer_register",  # -> rt 0x0800780c  install cb into a sw-timer slot @0x0800895c, return slot index
 0x07ffd5a4:"hal_oid_shift_in",    # -> rt 0x080055a4  IRQs-off capture: clock state.bit_count (23/32) bits GPIO9 MSB-first -> raw_word 0x08008C14, set frame_ready
 0x07ffd560:"hal_oid_bus_idle",    # -> rt 0x08005560  release OID two-wire bus: GPIO9->input(pull-high), GPIO2->output low
 0x07ffd764:"hal_oid_capture_decode23", # -> rt 0x08005764  standby 23-bit capture+decode: require type "10", drop 0x3FFFC, store code -> akoid_buf+8, validate (was hal_oid_decode)
 0x07ffd7cc:"hal_oid_timer_cb",    # -> rt 0x080057cc  periodic OID poll timer cb -> hal_oid_capture_decode23; on hit sets akoid_buf[0]=1 and posts 0x1060
 0x07ffd8b0:"hal_oid_timer_start", # -> rt 0x080058b0  (re)arm the OID poll soft-timer: hal_timer_register(0,40,1,hal_oid_timer_cb) -> state.timer_handle
 0x07ffb994:"oid_change_notifier", # -> rt 0x08003994  the 0x30 poster: posts 0x30{slot} iff game_ctx+4!=slot
 0x07ffb768:"hal_event_post",      # -> rt 0x08003768  coalescing post into AO ring 0x08008898
 0x07ffb71c:"hal_event_dedup",     # -> rt 0x0800371c  dedup back-to-back events before append
 0x07ffb644:"hal_event_append",    # -> rt 0x08003644  append {id,arg} to the AO event ring
 0x07ffba74:"hal_ao_submit_tick",  # -> rt 0x08003a74  get chunk -> virt_to_phys -> hal_dma_submit
 0x07ffb530:"hal_dma_submit",      # -> rt 0x08003530  write DMA regs 0x04010004/08/0c/00 (DAC feed)
 # ---- Resident nandboot NAND op leaves + misc (RE 2026-07-04, nand-write-path.md +
 # gme-timer-counter-random.md; all Proven via disasm). ghidra addr = runtime - 0x8000.
 0x07ffa044:"nb_nand_program_page",# -> rt 0x08002044  PAGE-PROGRAM: data->SRAM 0x08006800, cmd 0x63, program-confirm + status poll
 0x07ffa60c:"nb_nand_read_page",   # -> rt 0x0800260c  READ data: SRAM->caller, cmd 0x64 (program twin)
 0x07ffaaf8:"nb_nand_block_erase", # -> rt 0x08002af8  NAND 0x60/0xD0 BLOCK ERASE, fail-bit retry <=16 ("NF:12/13"). CORRECTED: was nb_nand_read_oob (no OOB-read primitive; spare reads go via nb_nand_read_page r3=0)
 0x07ffaa38:"nb_read_cp",          # -> rt 0x08002a38  nandboot codepage/FHA row read (used by nb_nftl_load_info / FHA_get_maplist)
 0x07ffac18:"nb_nand_probe_id",    # -> rt 0x08002c18  reset + 4x read-ID vs cfg@0x0800003c (K9GAG08U0M), program timing, fill dev/state, set randomizer flag
 0x07ffaefc:"nb_nand_status",      # -> rt 0x08002efc  status-read op (NFC cmd 0x70; [0x0404A000+0x150]&0xff = NAND status)
 0x07ffadb4:"nb_nand_ready",       # -> rt 0x08002db4  ready-busy (RB) poll
 0x07fff7d8:"nb_erase_region", # -> rt 0x080007d8  region-erase wrapper: per-block nb_nand_block_erase + bad-block bitmap bookkeeping. CORRECTED: was nb_nand_erase_block (that primitive is 0x08002af8)
 0x07ffdbe8:"hal_timer_unregister",# -> rt 0x08005be8  free sw-timer slot (table @0x0800895c), returns 0xff
 0x07ffb1d0:"rt_udiv",             # -> rt 0x080031d0  ADS __rt_udiv: r0=divisor,r1=dividend -> quotient r0 / remainder r1
 0x07ff81fc:"lcg_rand_below",      # -> rt 0x080001fc  ARM-ADS LCG *state=*state*0x91E6D6A5+0x91E6D6A5; return (*state*n)>>32 (game-side rng_below)
 0x07ffb930:"hal_event_id_for",    # -> rt 0x08003930  event-id mapper: 0->0x1046 (poll tick), 1->0x105f, 2->0x1060
 # ---- Resident nandboot FHA / codepage maplist (RE 2026-07-04, fatvol-medium-layering.md +
 # codepage-language-selector.md; all Proven prologues). ghidra addr = runtime - 0x8000. ----
 0x07ff8c40:"FHA_get_maplist",     # -> rt 0x08000c40  FHA_get_maplist(name,u16* map,max,bBackup): per-cluster {origin,backup} physical-block map for a named FHA area (e.g. "codepage"->{36..42}); origin, else backup on ASA bad-block
 0x07ff8d90:"codepage_maplist_recovery", # -> rt 0x08000d90  codepage RECOVERY lookup: scan maplist, origin->backup remap on failed block; fatal if backup also bad
 0x07ff89c8:"nb_nftl_load_info",   # -> rt 0x080009c8  nandboot FHA header/entry reader (rows dev[0x14]-2/-3 via nb_read_cp), name-match, returns entry size-in-sectors. DISTINCT from PROG nftl_load_info 0x0804855c
 # ---- Resident nandboot audio/DMA + IRQ mask + copyback + key-scan HAL (RE 2026-07-06, conformance-audit-hw{,2}.md
 # + test-mode.md; all Proven via disasm). ghidra addr = runtime - 0x8000. ----
 0x07ffb9d4:"hal_audio_irq_handler",# -> rt 0x080039d4  audio DMA-done ISR: clear kick bit16 on 0x04010000, honor swallow 0x08008c91 / override cb 0x08008c64, resubmit next chunk or set idle 0x08008c60 bit0
 0x07ffbdbc:"hal_ao_get_chunk",     # -> rt 0x08003dbc  dequeue next 0x400-B PCM chunk from the audio ring (src=ring[+0x44]+ring[+0x38]), handle wrap +0x40 / count +0x20 / zero-fill +0x34
 0x07ff90a8:"hal_dma_spurious_check",# -> rt 0x080010a8  DMA-completion spurious filter: read status 0x0401001c; returns 0 (legit) unless all masked channel bits set
 0x07ffe740:"hal_gpio_read",        # -> rt 0x08006740  read one GPIO input pin level (0x040000bc); leaf that all key/USB-detect/comparator polls go through (DISTINCT from br_gpio_read 0x07ffb914)
 0x07ffb39c:"irq_mask_push",        # -> rt 0x0800339c  critical-section enter: save + zero INT_ENABLE 0x04000034
 0x07ffb3e4:"irq_mask_pop",         # -> rt 0x080033e4  critical-section exit: restore INT_ENABLE 0x04000034
 0x07ffa3bc:"nb_nand_copyback",     # -> rt 0x080023bc  HW NAND copy-back leaf (r0=plane/CE, r1=SOURCE row, r2=DEST row); target of dev_copyback_leaf
 0x07ffeb2c:"hal_key_read",         # -> rt 0x08006b2c  read buttons: GPIO0==0->5 (VOL+ active-low), GPIO1==1->6 (VOL-), GPIO11==1->8 (POWER active-high), else 0xFF
 0x07ffd8f0:"key_scan_start",       # -> rt 0x080058f0  periodic ~120-tick key scan -> hal_key_read; on a key arms a 20-tick debounce (key_debounce_cb)
 0x07ffd954:"key_debounce_cb",      # -> rt 0x08005954  debounce re-read; same key -> hold tracker (key_hold_cb), else back to scan
 0x07ffd998:"key_hold_cb",          # -> rt 0x08005998  hold/repeat tracker (count @0x08008c1c, state @0x08008c1a): short/hold/repeat -> key_event_post
 0x07ffebfc:"key_event_post",       # -> rt 0x08006bfc  post hal_event 0x105F {code,sub} (dedup when sub in {1,2}); hook ptr *0x08008c5c
}

def ensure_func(addr, name):
    a=A(addr)
    f=fm.getFunctionAt(a)
    if f is None:
        try: f=createFunction(a, name)
        except: f=None
    if f is None:
        # at least drop a label
        try: st.createLabel(a, name, SourceType.USER_DEFINED)
        except: pass
        return False
    try: f.setName(name, SourceType.USER_DEFINED)
    except: pass
    return True

nb=0
for addr,nm in BOOTROM.items():
    if ensure_func(addr,nm): nb+=1
print("named bootrom funcs:",nb)

# ---- 1b. name key data globals (literal-pool pointer slots used by the GME engine) ----
DATA_NAMES={
 0x0802c1d0:"p_gme_registers", 0x0802c1c4:"p_media_offsets", 0x0802c1cc:"p_media_sizes",
 0x0802c1bc:"p_gme_filehandle", 0x0802c1c0:"p_playlist_count", 0x0802c1c8:"p_cur_playing_id",
 0x0802c1e8:"p_gametable_off", 0x0802c1ec:"p_product_id", 0x0802c4ac:"p_mediatable_off",
 0x0802c4b0:"p_playlist_idx", 0x0802c4b4:"p_playlist_pos", 0x0802c4c0:"p_cur_line_off",
 0x0802c4c4:"p_action_count", 0x0802c4d8:"p_action_types", 0x0802c4dc:"p_action_cmds",
 0x0802c4e0:"p_action_regs", 0x0802c4e4:"p_action_m",
 0x0802d054:"p_scripttable_off", 0x0802d040:"p_scriptoffsets_base", 0x0802d05c:"p_reginit_off",
 0x0802d044:"p_line_count", 0x0802d048:"p_line_offsets", 0x0802d060:"p_reg_count",
 0x0802d070:"p_addscript_off", 0x0802d028:"p_cond_count", 0x0802d058:"pp_gb_d058",
 0x0802b994:"pp_gb_b994", 0x08007a18:"g_gb",
 0x0811f38c:"g_media_xor_key", 0x08007c74:"g_decrypt_enabled",
}
for addr,nm in DATA_NAMES.items():
    try: st.createLabel(A(P(addr)), nm, SourceType.USER_DEFINED)
    except: pass
print("named data globals:", len(DATA_NAMES), "(loader base 0x%08x, shift 0x%x)"%(LOADER_BASE,_SHIFT))

# ---- 2. apply names.csv ----
nf=0
NAME2ADDR={}                       # name -> addr, so signatures can be applied BY ADDRESS (reliable)
for line in open(NAMES):
    line=line.strip()
    if not line or "," not in line: continue
    addr_s,nm=line.split(",",1)
    addr=int(addr_s,16)
    NAME2ADDR[nm]=addr
    if ensure_func(addr,nm): nf+=1
print("renamed firmware funcs:",nf)

# ---- 2b. process ghidra_types.h -- the SINGLE source for structs, function signatures, and
# function docstrings. Ghidra propagates a committed callee signature (arg count + arg TYPES +
# return) and struct field names to ALL callers, sharpening their decompilation transitively. ----
import os as _os, re as _re2
DESCS={}
if _os.path.exists(HDR):
    hdr=open(HDR).read()
    dtm=currentProgram.getDataTypeManager()
    # (a) build struct definitions with the StructureDataType API (deterministic; CParser was flaky).
    # Parses each "struct X { ... };" block from the .h and appends fields (explicit padding in the
    # header keeps offsets correct). Field type/name/array + inline /* comment */ are all applied.
    from ghidra.program.model.data import (StructureDataType, PointerDataType, ArrayDataType,
        UnsignedCharDataType, UnsignedShortDataType, UnsignedIntegerDataType, DataTypeConflictHandler)
    def _basedt(b):
        b=b.replace("unsigned","").replace("signed","").strip()
        return {"char":UnsignedCharDataType(),"short":UnsignedShortDataType(),
                "int":UnsignedIntegerDataType(),"long":UnsignedIntegerDataType()}.get(b)
    import sys as _sysS
    # (a0) ENUMS: "enum Name : short { MEMBER = 0xNN, ... };" -> EnumDataType. Created BEFORE structs
    # (struct fields / @slot / @local may reference an enum type by name) and BEFORE _basedt resolves
    # names. Typing a discriminant variable (param via signature, or local via @local) as an enum makes
    # the decompiler render its constants (opcodes/events/states) symbolically. Idempotent (REPLACE).
    from ghidra.program.model.data import EnumDataType, CategoryPath
    _ENUM_SZ={"char":1,"short":2,"int":4,"long":4}
    _nenum=0
    for em in _re2.finditer(r'enum\s+(\w+)\s*(?::\s*(unsigned\s+|signed\s+)?(char|short|int|long)\s*)?\{(.*?)\}\s*;', hdr, _re2.S):
        ename=em.group(1); esz=_ENUM_SZ.get(em.group(3) or "int",4); ebody=em.group(4)
        ed=EnumDataType(CategoryPath("/"), ename, esz)
        _nm=0
        for mm in _re2.finditer(r'(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)', ebody):
            val=int(mm.group(2),16) if mm.group(2).lower().startswith("0x") else int(mm.group(2))
            try: ed.add(mm.group(1), val); _nm+=1
            except: pass
        dtm.addDataType(ed, DataTypeConflictHandler.REPLACE_HANDLER); _nenum+=1
        print("  built enum %s (%d members, %d B)"%(ename,_nm,esz))
    print("built enums:",_nenum)
    _nstruct=0
    for sm in _re2.finditer(r'struct\s+(\w+)\s*\{(.*?)\}\s*;', hdr, _re2.S):
        sname=sm.group(1); body=sm.group(2)
        # explicit-offset mode: any "@0xNN type name;" field. Size from a leading "/* size 0xNNN */".
        # Sparse fields are placed via replaceAtOffset in a fixed-size struct (no hand padding, no
        # regression of existing rich types). Otherwise sequential-append mode (grows as added).
        explicit = '@0x' in body
        if explicit:
            szm=_re2.search(r'size\s+(0x[0-9a-fA-F]+)', body)
            s=StructureDataType(sname, int(szm.group(1),16) if szm else 0)
        else:
            s=StructureDataType(sname, 0)
        for line in body.split('\n'):                       # one field per line -> comment pairs correctly
            cm=_re2.search(r'/\*(.*?)\*/', line)
            comment=" ".join(cm.group(1).split()) if cm else None
            decl=" ".join(_re2.sub(r'/\*.*?\*/','',line).split()).strip().rstrip(';').strip()
            if not decl: continue
            off=None
            om=_re2.match(r'@(0x[0-9a-fA-F]+)\s+(.*)$', decl)
            if om: off=int(om.group(1),16); decl=om.group(2).strip()
            fmt=_re2.match(r'((?:unsigned|signed)?\s*(?:char|short|int|long|void)|[A-Za-z_]\w*)\s*(\**)\s*(\w+)\s*(?:\[\s*([0-9a-fxA-FX]+)\s*\])?$', decl)
            if not fmt: print("  struct %s: SKIP %r"%(sname,decl)); continue
            base,stars,fname,arr=fmt.groups()
            _bt=_basedt(base)
            if _bt is None: _bt=dtm.getDataType("/"+base.strip())   # struct/typedef by name (must be defined earlier)
            dt=PointerDataType(_bt,4) if stars else _bt             # typed pointer keeps the chain (AkOidPara*, MeGame*)
            if dt is None: print("  struct %s: bad base %r"%(sname,base)); continue
            if arr:
                n=int(arr,16) if arr.lower().startswith("0x") else int(arr)
                dt=ArrayDataType(dt, n, dt.getLength())
            try:
                if off is not None: s.replaceAtOffset(off, dt, dt.getLength(), fname, comment)
                else: s.add(dt, dt.getLength(), fname, comment)
            except: print("  struct %s: field %s @%s FAIL (%s)"%(sname,fname,hex(off) if off is not None else "seq",_sysS.exc_info()[1]))
        dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER)
        _nstruct+=1; print("  built struct %s = 0x%x bytes"%(sname, s.getLength()))
    print("built structs:",_nstruct)
    # (a2) "@slot 0xADDR type[*] name" directives (in comments in the .h): label AND type a
    # literal-pool pointer slot (or any in-image global). Typing a slot as e.g. GlobalBlock*
    # makes the decompiler render loads through it by name and propagate struct FIELD names
    # into every function that loads the slot (same trick as the StateChart* globals in (d)).
    # Addresses are authored at THIS FW's base (like names.csv) -- consumed as-is, no shift.
    _lst=currentProgram.getListing()
    _nslot=0
    for m in _re2.finditer(r'@slot\s+(0x[0-9a-fA-F]+)\s+((?:unsigned\s+|signed\s+)?[A-Za-z_]\w*)\s*(\**)\s+([A-Za-z_]\w*)', hdr):
        addr=int(m.group(1),16); base=m.group(2); stars=m.group(3); nm=m.group(4)
        bt=_basedt(base)
        if bt is None: bt=dtm.getDataType("/"+base.strip())
        if bt is None:
            print("  @slot %s: unknown type %r"%(nm,base)); continue
        dt=bt
        for _ in stars: dt=PointerDataType(dt,4)
        try: st.createLabel(A(addr), nm, SourceType.USER_DEFINED)
        except: print("  @slot %s: label FAIL (%s)"%(nm,_sysS.exc_info()[1]))
        try:
            _lst.clearCodeUnits(A(addr), A(addr).add(dt.getLength()-1), False)
            _lst.createData(A(addr), dt); _nslot+=1
        except: print("  @slot %s @0x%08x type FAIL (%s)"%(nm,addr,_sysS.exc_info()[1]))
    print("typed @slot globals:",_nslot)
    # (b) function signatures are applied by apply_sigs.py in a SEPARATE pass (run it BEFORE this
    # script). Applying them inline here does not persist -- some later step in this mega-regen
    # reverts same-run signature applies -- whereas a dedicated apply-only pass sticks and then
    # survives this regen. So this step is intentionally omitted; see regen.sh.
    _nsig="(applied by apply_sigs.py -- separate pass)"
    print("applied signatures:",_nsig)
    # (c) docstrings: "/* 0xADDR text */" comments -> function comment (also emitted into the .c).
    # GROUPED blocks share one description across several functions:
    #   "/* 0xA name1 / 0xB name2 / 0xC name3: shared description... */"
    # Split the leading "0xADDR name /"-list off and attach "<name>: <shared desc>" to EACH address,
    # so every grouped function gets its OWN Ghidra comment (not just the group leader). A plain
    # "/* 0xADDR text */" (single) still attaches the whole text to that one address.
    # NAME-FIRST address resolution: the docstring's leading "0xADDR name" -- if `name` is a function
    # in names.csv (NAME2ADDR), attach at THAT (authoritative, MT-base) address, ignoring the literal
    # 0xADDR. Many docstrings were migrated from the sibling 2N-Update3202 build and still carry that
    # build's addresses; keying by name re-points them onto the correct MT functions (and prevents a
    # stale legacy address from silently landing a docstring on an unrelated MT function).
    _grp=_re2.compile(r'^((?:0x[0-9a-fA-F]+\s+\w+\s*/\s*)+0x[0-9a-fA-F]+\s+\w+)\s*:\s*(.*)$', _re2.S)
    def _apply_doc(addr, doc):
        DESCS[addr]=doc
        fn=fm.getFunctionAt(A(addr))
        if fn is not None: fn.setComment(doc)
    for m in _re2.finditer(r'/\*\s*(0x[0-9a-fA-F]+\s.*?)\*/', hdr, _re2.S):
        block=m.group(1)
        gm=_grp.match(" ".join(block.split()))
        if gm:
            pairs=_re2.findall(r'(0x[0-9a-fA-F]+)\s+(\w+)', gm.group(1))
            desc=" ".join(gm.group(2).split())
            for a_s,nm in pairs:
                try: _apply_doc(NAME2ADDR.get(nm, int(a_s,16)), "%s: %s"%(nm,desc))
                except: pass
        else:
            am=_re2.match(r'(0x[0-9a-fA-F]+)\s+(\w+)?(.*)$', " ".join(block.split()), _re2.S)
            if not am: continue
            nm=am.group(2)
            try: _apply_doc(NAME2ADDR.get(nm, int(am.group(1),16)), " ".join((block.split(None,1)[1]).split()))
            except: continue
    print("applied docstrings:",len(DESCS))
    # (d) type the statechart POINTER globals to StateChart* -> struct field names then propagate
    # into every function that loads them. Clear the FULL datatype range first (spurious instrs may
    # sit inside it), and use a bare except (Jython's `except Exception` does NOT catch Java errors).
    import sys as _sys
    from ghidra.program.model.data import PointerDataType
    lst=currentProgram.getListing()
    def _typ(addr, dt, label):
        if dt is None: return
        try:
            lst.clearCodeUnits(A(addr), A(addr).add(dt.getLength()-1), False)
            lst.createData(A(addr), dt); print("  typed %s @0x%08x = %s"%(label,addr,dt.getName()))
        except:
            print("  type-global SKIP 0x%08x (%s)"%(addr, _sys.exc_info()[1]))
    _sc=dtm.getDataType("/StateChart")
    if _sc is not None:
        _scp=PointerDataType(_sc)
        _typ(P(0x080e87d4), _scp, "statechart ptr")    # DAT_080e87d4 -> StateChart*
        _typ(P(0x08031a00), _scp, "statechart ptr2")   # DAT_08031a00 -> StateChart*
    print("typed globals: StateChart=%s"%(_sc is not None))

    # ---- (e) @noreturn directive: mark halt/panic paths no-return. A wrong (implicit-return)
    # prototype makes every caller's decompilation grow a bogus fall-through; setNoReturn fixes the
    # whole call graph. "@noreturn 0xADDR" (addresses authored at this FW's base, consumed as-is). ----
    _nnoret=0
    for m in _re2.finditer(r'@noreturn\s+(0x[0-9a-fA-F]+)', hdr):
        fn=fm.getFunctionAt(A(int(m.group(1),16)))
        if fn is not None:
            try: fn.setNoReturn(True); _nnoret+=1
            except: pass
    print("marked noreturn:",_nnoret)

    # ---- (f) @local directive: rename AND retype a local/param inside a function (HighFunction
    # local-symbol DB commit -- the same mechanism Ghidra's GUI "rename variable" uses). Syntax:
    #   @local 0xFUNC <key> <newname> [<type>[*...]]
    # key selects the variable: "s<+/-0xNN>" = by stack storage offset (STABLE across regens), or
    # "=curname" = by the variable's current decomp name (e.g. =uVar3; convenient but shifts if the
    # decomp renumbers). <type> optional; resolved like struct fields (primitive/struct/enum + stars).
    # Must run AFTER structs/enums exist and BEFORE the final decompile-to-file loop below. ----
    from ghidra.program.model.pcode import HighFunctionDBUtil
    from ghidra.program.model.data import PointerDataType as _PDT
    _di_l=DecompInterface(); _di_l.openProgram(currentProgram); _mon_l=ConsoleTaskMonitor()
    def _resolve(base, stars):
        dt=_basedt(base)
        if dt is None: dt=dtm.getDataType("/"+base.strip())
        if dt is None: return None
        for _ in stars or "": dt=_PDT(dt,4)
        return dt
    _locs={}   # func_addr -> list of (key, newname, typestr_or_None)
    for m in _re2.finditer(r'@local\s+(0x[0-9a-fA-F]+)\s+(\S+)\s+([A-Za-z_]\w*)\s*(?:((?:unsigned\s+|signed\s+)?[A-Za-z_]\w*)\s*(\**))?\s*$', hdr, _re2.M):
        fa=int(m.group(1),16); key=m.group(2); nn=m.group(3); base=m.group(4); stars=m.group(5)
        _locs.setdefault(fa,[]).append((key,nn,base,stars))
    _nloc=0
    for fa,items in _locs.items():
        fn=fm.getFunctionAt(A(fa))
        if fn is None: print("  @local: no func @0x%08x"%fa); continue
        res=_di_l.decompileFunction(fn,60,_mon_l)
        hf=res.getHighFunction() if res else None
        if hf is None: print("  @local: decompile fail @0x%08x"%fa); continue
        syms=list(hf.getLocalSymbolMap().getSymbols())
        for key,nn,base,stars in items:
            target=None
            if key.startswith("s"):
                try: want=int(key[1:],16) if key[1:].lower().startswith(("0x","-0x")) else int(key[1:],16)
                except: want=None
                if want is not None:
                    for hs in syms:
                        stg=hs.getStorage()
                        if stg is not None and stg.isStackStorage() and stg.getStackOffset()==want:
                            target=hs; break
            elif key.startswith("="):
                for hs in syms:
                    if hs.getName()==key[1:]: target=hs; break
            if target is None: print("  @local 0x%08x %s: var not found"%(fa,key)); continue
            dt=_resolve(base,stars) if base else target.getDataType()
            try:
                HighFunctionDBUtil.updateDBVariable(target, nn, dt, SourceType.USER_DEFINED); _nloc+=1
            except:
                print("  @local 0x%08x %s: commit FAIL (%s)"%(fa,key,_sys.exc_info()[1]))
    print("committed locals:",_nloc)

# ---- 3. re-decompile everything to decomp_named/ ----
# Ghidra emits the setComment() plate comment into the C only inconsistently (and line-wrapped),
# so we always prepend our own single-line docstring and strip any wrapped copy Ghidra emitted
# (keeping /* WARNING ... */ blocks intact).
import re as _re
di=DecompInterface(); di.openProgram(currentProgram); mon=ConsoleTaskMonitor()
cnt=0
for f in fm.getFunctions(True):
    e=f.getEntryPoint().getOffset()
    res=di.decompileFunction(f,45,mon)
    txt=res.getDecompiledFunction().getC() if res and res.decompileCompleted() else "// decompile failed\n"
    doc=DESCS.get(int(e) & 0xffffffff)
    if doc is not None:
        pfx=" ".join(doc.split())[:20]
        txt=_re.sub(r"/\*(.*?)\*/", lambda m:("" if pfx in " ".join(m.group(1).split()) else m.group(0)), txt, flags=_re.S)
    with open(DEC+"/0x%08x.c"%e,"w") as o:
        o.write("// %s @0x%08x\n"%(f.getName(),e))
        if doc is not None: o.write("/** %s */\n"%doc)
        o.write(txt)
    cnt+=1
print("DECOMP_NAMED DONE: %d functions"%cnt)
