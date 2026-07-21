#!/usr/bin/env python3
"""Assemble names.csv (addr,name) for the Ghidra rename pass, from verified
findings + string evidence. Emits $FW/input/names.csv (default fw/2N-Update3202)."""
import os, xref, nav
from collections import defaultdict

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_FW = os.environ.get("FW", "fw/2N-Update3202")
_NAMES_OUT = os.environ.get("NAMES_CSV",
    (_FW if os.path.isabs(_FW) else os.path.join(_REPO, _FW)) + "/input/names.csv")

# ---- 1. curated, verified names (from FINDINGS.md analysis) ----
CUR = {
 # GME script engine (internal interpreter)
 0x0802cbe4:"gme_parse_header", 0x0802cb00:"gme_parse_start_end_oid",
 0x0802cb6c:"gme_reset_registers", 0x0802c868:"gme_oid_to_playscript",
 0x0802c74c:"gme_parse_check_conditions", 0x0802c4e8:"gme_check_condition",
 0x0802c38c:"gme_parse_actions", 0x0802bc64:"gme_exec_command",
 0x0802c2ac:"gme_parse_playlist", 0x0802c1fc:"gme_parse_media_offsets",
 0x0802ca50:"gme_parse_media_offsets2", 0x0802c930:"gme_clear_script_state",
 0x0802aff4:"gme_mount_check_product", 0x0802ae74:"gme_check_language",
 0x0802d160:"gme_oid_dispatch", 0x0802d0ec:"gme_parse_additional_script",
 0x0802bc38:"gme_rand_in_range",
 # binary launch / system_api
 0x080a0894:"gme_read_main_binary_table", 0x080a03f0:"gme_launch_binary_build_sysapi",
 0x0802fb20:"gme_alloc_binary_region",
 # file API (low-level FS used by engine)
 0x08004ed4:"fs_open", 0x08004f7c:"fs_read", 0x08004f8c:"fs_seek",
 0x08005024:"fs_read_xor_decrypt", 0x080050e4:"fs_read_plain",
 # system_api-exposed file ops
 0x0800ced4:"sysapi_open", 0x0800cf7c:"sysapi_read", 0x0800cf8c:"sysapi_seek",
 0x080aaec8:"sysapi_write", 0x080aaef8:"sysapi_close",
 # memory / core
 0x08030870:"heap_init", 0x0802f868:"malloc", 0x0802f488:"malloc_core",
 0x08037bbc:"sysapi_malloc", 0x08037bc8:"sysapi_free",
 0x08000cf4:"memset0", 0x08000878:"ptr_nonnull_check", 0x0800088c:"dbg_assert_stub",
 # events / audio
 0x0800235c:"event_queue_post", 0x08002024:"is_audio_playing",
 0x080a1198:"play_media", 0x080a1090:"play_media_setup",
 0x08008890:"printf", 0x0800a024:"sysapi_is_audio_playing",
 0x080a9198:"sysapi_play_sound",
 # OID sensor driver (akoid.c)
 0x080e44cc:"akoid_init", 0x080e4090:"akoid_decode_frame",
 0x080e47ec:"akoid_sensor_poll", 0x080e4b2c:"akoid_sensor_config",
 0x080e5030:"akoid_reg_write", 0x080e5050:"akoid_reg_read",
 # MediaLib (audio codec front-end)
 0x080e67cc:"medialib_open", 0x080f4d70:"medialib_detect_codec",
 # game engine
 0x0805f4f8:"game_load_header", 0x0805fb98:"game_pick_voice",
 0x0806003c:"game_state_machine", 0x08061594:"game_oid_dispatch",
 # app init / main
 0x0802fe20:"app_init_main",
}

# ---- 2. string-evidence-derived names ----
def derive(strs):
    j=" | ".join(strs).lower()
    table=[
      ("game five init","game5_init"),("game four init","game4_init"),
      ("game seven start","game7_init"),("minigame six init","game6_mini_init"),
      ("minigame two init","game2_mini_init"),("minithree","game3_mini_init"),
      ("spe one start","game_spe1_init"),("ask question","game_ask_question"),
      ("enter charge","charge_main"),("chargercount","charge_count"),
      ("usb_detect_begin","usb_detect"),("handle usb state","usb_state_handler"),
      ("switch to usb power","usb_power_switch"),("paint usb state","usb_paint_state"),
      ("start update","fw_update"),("update success","fw_update_run"),
      ("delete update file","fw_update_cleanup"),("battery:%d","fw_update_battery"),
      ("system is low power","low_power_warn"),
      ("mtdlib - init","mtd_init"),("nandmtd","nand_mtd"),("nftl_init","nftl_init"),
      ("it fails to load mtd","mtd_reload"),
      ("getserialno","get_serial_no"),("syspoweron","sys_power_on"),
      ("aud_playerstop","aud_player_stop"),
      ("g_paudplayer is null","fwl_voice_play"),
      ("utl_strlen","str_lib_fn"),("utl_ustrfnd","ustr_lib_fn"),
      ("fat_format","fat_format"),("fatlib - version","fat_version"),
      ("standby event","standby_handler"),
    ]
    for key,nm in table:
        if key in j: return nm
    return None

# function -> strings
fstrs=defaultdict(list)
for a,s in xref.strs.items():
    for site,mode,kind in xref.refs.get(a,[]):
        f=nav.func_of(site)
        if f is not None and len(s)>=3: fstrs[f].append(s)

names=dict(CUR)
auto=0
for f,ss in fstrs.items():
    if f in names: continue
    nm=derive(ss)
    if nm:
        # disambiguate duplicates by suffixing address
        base=nm; k=2
        while nm in names.values(): nm=f"{base}_{k}"; k+=1
        names[f]=nm; auto+=1

with open(_NAMES_OUT,"w") as out:
    for a in sorted(names):
        out.write(f"0x{a:08x},{names[a]}\n")
print(f"curated={len(CUR)} auto-string={auto} total={len(names)}")
print("wrote", _NAMES_OUT)
