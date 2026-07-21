#!/usr/bin/env python3
"""Compute the GME-format / script-engine call-graph closure and (re)generate the campaign
tracking inventory:  <FW>/docs/gme-callgraph-inventory.{md,tsv}

Source of truth for the "name+signature+docstring the whole GME region" campaign. Reads the
CURRENT named decompilation (out/decomp) to build the caller->callee graph, BFS from the
GME/script/game-engine roots, stops descent at boundary services, then cross-references
input/ghidra_types.h (docstrings, prototypes) + input/names.csv to fill the status columns.

Usage:  FW=2N-update3202MT python3 tools/gme_inventory.py   (default FW=2N-update3202MT)
Re-run after every regen to refresh the inventory.
"""
import os, re, collections

FW = os.environ.get("FW", "2N-update3202MT")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FWD = FW if os.path.isabs(FW) else os.path.join(ROOT, FW)
DEC = os.path.join(FWD, "out/decomp")
HDR = open(os.path.join(FWD, "input/ghidra_types.h")).read()

# ---- name<->addr + bodies from the decomp file headers ----
addr2name = {}; name2addr = {}; body = {}
for fn in os.listdir(DEC):
    if not fn.endswith(".c"): continue
    txt = open(os.path.join(DEC, fn), encoding="latin1").read()
    m = re.match(r'//\s+(\S+)\s+@(0x[0-9a-fA-F]+)', txt)
    if not m: continue
    a = int(m.group(2), 16); addr2name[a] = m.group(1); name2addr[m.group(1)] = a; body[a] = txt

def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S); return re.sub(r'//.*', '', t)

calls = collections.defaultdict(set)
for a, txt in body.items():
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', strip_comments(txt)):
        cal = m.group(1)
        if cal in name2addr: calls[a].add(name2addr[cal])
        else:
            mm = re.match(r'(?:FUN_0|func_0x0)([0-9a-fA-F]{7})$', cal)
            if mm: calls[a].add(int('0' + mm.group(1), 16))

ROOT_NAMES = [
 'gme_oid_dispatch','gme_oid_dispatch_alt','gme_exec_command','gme_parse_actions',
 'gme_parse_media_offsets','gme_parse_playlist','gme_parse_check_conditions','gme_check_condition',
 'gme_parse_media_offsets2','gme_parse_start_end_oid','gme_reset_registers','gme_parse_header',
 'gme_parse_additional_script','gme_oid_to_playscript','gme_clear_script_state','gme_rand_in_range',
 'gme_check_language','gme_mount_check_product','gme_read_game_type','gme_read_main_binary_table',
 'gme_launch_binary_build_sysapi','gme_alloc_binary_region','gme_power_on_playlist_play',
 'gme_play_system_voice','game_binary_launch','game_start','game_oid_dispatch',
 'game_load_playlist_offsets','game_play_phase_cue','game_ctx_init_flags','game_load_header',
 'game_pick_voice','game_reading_sm','game_wronglimit_sm','game_quiz_sm','game_findtarget_sm',
 'game_record_load_outer','game_record_load','playlistlist_parse','playlistlist_play','native_cue_play',
 'game14_entry','game14_phase_engine','game14_subgame_parse','game14_pick_unplayed_subgame',
 'game14_match_target_oids','game14_match_decoy_oids','game14_match_other_oids',
 'game15_entry','game15_engine','game15_subgame_parse','game15_draw_subgame',
 'game16_entry','game16_record_load','game16_engine','game16_subgame_parse','game16_subgame_match','game16_pick_unplayed_subgame',
 'game17_entry','game17_record_load','game17_engine','game17_subgame_parse','game17_pick_unplayed_group','game17_load_group_ids',
 'game18_entry','game18_record_load','game18_select_handler','game18_match_select_oid','game18_subgame_parse',
 'game19_entry','game19_record_load','game19_engine','game19_match_extra_oids','game19_subgame_parse','game19_match_target_ordered',
 'game60_entry','game60_record_load','game60_engine','game60_subgame_parse',
 'game46_read_game_record','game47_read_game_record','game48_read_game_record','game49_read_game_record',
 'game46_engine','game31_entry','game31_engine',
 # session 3: GME game types 17-23 = statechart states 58/59/61/62/63/66/68 (+ the desc+4
 # exits, which are only reachable via the descriptor table, so they are roots too).
 'game58_entry','game58_engine','game58_teardown',
 'game59_entry','game59_engine','game59_teardown',
 'game61_entry','game61_engine','game61_teardown',
 'game62_entry','game62_engine','game62_teardown',
 'game63_entry','game63_engine','game63_teardown',
 'game66_entry','game66_engine','game66_teardown',
 'game68_entry','game68_engine','game68_teardown',
 'game19_teardown',
]
BOUNDARY_RE = re.compile(r'^(fs_|aud_|audio_|ao_|dac_|medialib_|media_|play_media|hal_|br_|nb_|sm_|'
  r'i2c_|codepage_|nftl_|nand|mtd_|fat_|Fat_|fatvol|dir_|physpool_|heap_|malloc|memcpy|memset|'
  r'event_queue|event_loop|event_pump|is_audio|akoid_|Utl_|str_|strlen|rt_|lcg_|rng_|cluster_|'
  r'fwl_|sysapi_|mp_pump|stream_|Fwl_|file_close|thunk_|dbg_|ptr_nonnull|sf_exception|battery_)')

# Verified boundary LEAKAGE that has no useful name yet: FAT/VFS/File internals (0x080ef..-0x0811c..),
# printf/format+misc runtime, and MMIO helpers reached from the GME closure but with zero GME logic.
# Classified boundary so BFS does not descend and they leave the interior-unnamed backlog.
BOUNDARY_ADDRS = {
 0x08009070,  # halt_baddata analysis artifact (bad instruction data)
 0x080093b4,  # timer/MMIO prescaler write helper (|0x10e00000 descriptor)
 0x0800c0f4,  # audio/media list service internal
 0x0800e0e0, 0x0800e104, 0x0800e218, 0x0800eb1c, 0x0800ebfc,  # FS cluster/offset math internals
 0x08031f7c,  # GPIO5 stub (truncated decomp, bad instruction data)
 0x080ae430, 0x080afb78,  # udisk discovery internals
 0x080ef820,  # VFS internal
 0x080f0408, 0x080f0580, 0x080f0618, 0x080f3e98, 0x080f45ac, 0x080f4694,  # VFS/File internals
 0x080f65ac, 0x080f6780, 0x080fac70,  # FAT internals
 0x081070dc, 0x081072a4, 0x08107340, 0x08107534, 0x0810825c,  # FAT/File internals
 0x08111740, 0x081117dc,  # FAT internals
 0x08114678, 0x08114688, 0x08114698, 0x0811473c, 0x08114a68, 0x08114a7a,  # libc/CRT internals
}

roots = set(name2addr[n] for n in ROOT_NAMES if n in name2addr)
def is_boundary(a):
    if a in roots: return False
    if a < 0x08009000: return True
    if a in BOUNDARY_ADDRS: return True
    return bool(BOUNDARY_RE.match(addr2name.get(a, "")))

closure = set(); q = list(roots); boundary = set()
while q:
    a = q.pop()
    if a in closure: continue
    closure.add(a)
    if is_boundary(a): boundary.add(a); continue
    for c in calls.get(a, ()):
        if c not in closure: q.append(c)

# ---- header cross-reference: docstrings (grouped + name-first) / prototypes / @local ----
n2a = {}
for l in open(os.path.join(FWD, "input/names.csv")):
    l = l.strip()
    if l and "," in l:
        aa, nn = l.split(",", 1)
        try: n2a[nn] = int(aa, 16)
        except: pass
docs = set()
_grp = re.compile(r'^((?:0x[0-9a-fA-F]+\s+\w+\s*/\s*)+0x[0-9a-fA-F]+\s+\w+)\s*:\s*(.*)$', re.S)
for m in re.finditer(r'/\*\s*(0x[0-9a-fA-F]+\s.*?)\*/', HDR, re.S):
    block = " ".join(m.group(1).split()); gm = _grp.match(block)
    if gm:
        for a_s, nm in re.findall(r'(0x[0-9a-fA-F]+)\s+(\w+)', gm.group(1)): docs.add(n2a.get(nm, int(a_s, 16)))
    else:
        am = re.match(r'(0x[0-9a-fA-F]+)\s+(\w+)?', block)
        if am: docs.add(n2a.get(am.group(2), int(am.group(1), 16)))
sig_names = set(re.findall(r'(?m)^[A-Za-z_][\w \*]*?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;', HDR))
local_funcs = set(int(m.group(1), 16) for m in re.finditer(r'@local\s+(0x[0-9a-fA-F]+)', HDR))

def region(a):
    if a < 0x08009000: return "nandboot-HAL(boundary)"
    if 0x08033e00 <= a < 0x08036400: return "core-gme-script"
    if 0x08036400 <= a < 0x08039000: return "core-gme-dispatch/parse"
    if 0x08077000 <= a < 0x0808d000: return "game-handlers(GME script types / states14-68)"
    if 0x08054000 <= a < 0x0805b000: return "game-handlers(native type9 / states46-49)"
    if 0x0806a000 <= a < 0x0806f400: return "game-reading/wronglimit"
    if 0x0808d000 <= a < 0x08094000: return "game-quiz/findtarget"
    if 0x080a8000 <= a < 0x080ac000: return "gme-binary-launch/voice"
    return "generic-service/other"

cats = collections.defaultdict(list)
for a in closure:
    nm = addr2name.get(a, "?")
    named = not (nm.startswith("FUN_") or nm.startswith("func_") or nm == "?")
    st = dict(named=named, sig=nm in sig_names, doc=a in docs, loc=a in local_funcs, boundary=a in boundary)
    cats[region(a)].append((a, nm, st))

order = ["core-gme-script","core-gme-dispatch/parse","game-handlers(GME script types / states14-68)",
 "game-handlers(native type9 / states46-49)","game-reading/wronglimit","game-quiz/findtarget",
 "gme-binary-launch/voice","generic-service/other","nandboot-HAL(boundary)"]
tot = named = sig = doc = loc = bnd = 0
lines = []; tsv = ["addr\tname\tregion\tnamed\tsig\tdoc\tlocals\tboundary"]
for reg in order:
    if reg not in cats: continue
    items = sorted(cats[reg])
    lines.append("\n### %s  (%d)\n" % (reg, len(items)))
    lines.append("| addr | name | named | sig | doc | locals | boundary |")
    lines.append("|------|------|:---:|:---:|:---:|:---:|:---:|")
    for a, nm, st in items:
        tot += 1; named += st["named"]; sig += st["sig"]; doc += st["doc"]; loc += st["loc"]; bnd += st["boundary"]
        c = lambda x: "Y" if x else ""
        lines.append("| 0x%08x | %s | %s | %s | %s | %s | %s |" % (a, nm, c(st["named"]), c(st["sig"]), c(st["doc"]), c(st["loc"]), c(st["boundary"])))
        tsv.append("0x%08x\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (a, nm, reg, c(st["named"]), c(st["sig"]), c(st["doc"]), c(st["loc"]), c(st["boundary"])))

interior = tot - bnd
hdr = ("# GME-format / script-engine call-graph inventory\n\n"
 "Auto-generated by `tools/gme_inventory.py` from `out/decomp` call edges + "
 "`input/ghidra_types.h` + `input/names.csv`. Re-run after every regen.\n\n"
 "Closure = BFS from the GME/script/game-engine roots (%d roots); descent STOPS at boundary "
 "services (nandboot HAL <0x08009000, and fs_/aud_/medialib_/hal_/nftl_/fat_/codepage_/Utl_/... "
 "which are named+signed so call sites read well but are not entered).\n\n"
 "**Totals:** %d in closure (%d interior + %d boundary) | named %d | prototype %d | docstring %d | "
 "locals-committed %d\n\n"
 "Columns: **named** = has a real name (not FUN_/func_); **sig** = an explicit C prototype in "
 "ghidra_types.h (most funcs rely on Ghidra's auto-derived signature and don't need one); "
 "**doc** = a Ghidra plate comment attaches (name-first resolved); **locals** = has @local "
 "commits; **boundary** = included for readable call sites but not descended.\n"
 % (len(roots), tot, interior, bnd, named, sig, doc, loc))

open(os.path.join(FWD, "docs/gme-callgraph-inventory.md"), "w").write(hdr + "\n".join(lines) + "\n")
open(os.path.join(FWD, "docs/gme-callgraph-inventory.tsv"), "w").write("\n".join(tsv) + "\n")
print(hdr)
print("wrote docs/gme-callgraph-inventory.{md,tsv}")
