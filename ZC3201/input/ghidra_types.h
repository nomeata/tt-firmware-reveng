/* fw/ZC3201/input/ghidra_types.h -- reverse-engineered struct/type/signature/docstring
 * definitions for the first-generation ZC3201 (build v0136/120117) PROG image (@0x08008000).
 *
 * Parsed by tools/ghidra_rename.py (structs via StructureDataType, enums, @slot/@local/@noreturn
 * directives, docstrings) and tools/apply_sigs.py (function signatures BY ADDRESS). Mirrors the
 * conventions of the 2N-MT flagship header (fw/2N-update3202MT/input/ghidra_types.h).
 *
 * ZC3201 is the "Rosetta stone": it retains vendor source-paths and self-naming assert strings
 * that the 2N-MT build stripped. Field/function names carrying a vendor string (e.g. AkOidPara,
 * TempByte, VoiceIndexForOID, Fwl_FileRead, MtdLib-*, FSLib-*) are AUTHORITATIVE. Everything
 * else is an RE inference. Offsets that are not vendor-confirmed are left as explicit padding. */

/* ---- vendor-authoritative struct names (from retained assert/log strings) ----
 * The 2N-MT flagship models these same two runtime blocks as AkOidPara / MeGame but had to GUESS
 * the names; ZC3201 confirms them:
 *   - "gb.AkOidPara->TempByte=0;"  @0x0803dcd8  -> struct AkOidPara, field TempByte
 *   - "pMeGame->VoiceIndexForOID[Offset][PlayIndex]=%d." @0x0805c9b0 -> struct MeGame, VoiceIndexForOID */

/* OID / game runtime context, reached via the global control block (gb.AkOidPara).
 * Only vendor-confirmed fields are named; the rest stays undefined bytes (size not yet pinned
 * on ZC3201 -- sequential-append mode, grows as fields are verified). */
struct AkOidPara {
    unsigned char TempByte;   /* scratch byte set 0/1 by the OID handler (akoid_set_tempbyte 0x0803dbc8); vendor name from "gb.AkOidPara->TempByte=" debug prints */
};

/* Per-game context (allocated by game_start 0x0805e0a8; reached via p_pMeGame_slot 0x081d8854).
 * VoiceIndexForOID is the vendor-named per-OID voice-variant permutation table -- the first-gen
 * data-driven OID->voice mechanism (analogue of the 2N-MT MeGame.voice_idx_for_oid guess). */
struct MeGame {
    unsigned char VoiceIndexForOID; /* [Offset][PlayIndex] 2-D voice-variant index table; vendor name from "pMeGame->VoiceIndexForOID[Offset][PlayIndex]=%d." */
};

/* ---- function signatures (applied BY ADDRESS via names.csv; names must match names.csv) ----
 * String-library primitives (Eng_String.c) -- vendor names from "Utl_Xxx() : ..." asserts. */
unsigned int Utl_StrLen(char *strMain);
int   Utl_StrCmpC(char *str1, char *str2);
char *Utl_StrCpy(char *dst, char *src);
char *Utl_StrCpyN(char *dst, char *src, unsigned int n);
char *Utl_StrCat(char *dst, char *src);
char *Utl_Itoa(int val, char *strDest);
int   Utl_Atoi(char *str);
int   ConverHexToi(char *str_Hex);

/* Filesystem (FatLib_V1.1.7 / FSLib) -- vendor names from "FSLib - Xxx()::" asserts. */
int File_OpenId(int id, unsigned int mode);
int File_Open(char *path, unsigned int mode);
int File_Deltree(char *path);
int File_CopyFile(char *dst, char *src);
int Fat_FdtSetName(int fdt, char *name);
int Fat_AllocFat(int vol, unsigned int nclus);
int Fname_ShortToLong(char *shortname, char *longname);
int FatLib_GetVersion(void);

/* Firmware self-update read primitive -- vendor name from "Fwl_FileRead addr: 0x%X, oid_fp:0x%X." */
int Fwl_FileRead(unsigned int addr, int oid_fp, void *buf, unsigned int len);

/* NAND / NFTL (MtdLib_Base_1.0.10; medium.c/nandmtd.c/nandflash.c) -- vendor names from
 * "MtdLib - Xxx:" log strings. These are the twins to name the 2N-MT nand_mtd cluster. */
int mtd_MapTblInit(int plane, int arg2, int arg3);
int mtd_CheckLogAddr(int plane, unsigned int logAddr);
int mtd_DealReadErr(int plane, int frame, int sector);
int mtd_Replace(int plane, int src, int dst);
int mtd_Try2Write(int plane, int frame, int sector, void *data);
int mtd_MarkBadBlk(int plane, int frame);

/* ---- docstrings: "/* 0xADDR name: description *\/" -> attached as the function's plate comment.
 * Name-first: keyed by the names.csv name where present (address is a fallback). ---- */
/* 0x0804b204 Fwl_FileRead: firmware/self-update file read: read `len` bytes at flash `addr` for the OID file-pointer `oid_fp` into `buf`. VENDOR name ("Fwl_FileRead addr: 0x%X, oid_fp:0x%X."); authoritative twin for the 2N-MT self-update read path. */
/* 0x0805c730 game_play_oid_voice: first-gen data-driven OID->voice lookup+play. Indexes pMeGame->VoiceIndexForOID[Offset][PlayIndex] ("play voice index =%d."), then plays the selected sample. Twin of 2N-MT game_pick_voice/game_voice_index_for_oid. */
/* 0x0809f374 play_chomp_voice: lazily opens A:/VOIMG/Chomp_Voice.bin (flat sample-offset table), seeks index<<2, reads [start,next), calls voice_play_sample. First-gen system-voice player (no analogue single-.gme path on 2N). */
/* 0x0803dbc8 akoid_set_tempbyte: sets gb.AkOidPara->TempByte to 0/1 in the OID capture/decode path (vendor "gb.AkOidPara->TempByte=" prints). */
/* 0x08041420 fw_update_finish_reset: end of firmware self-update ("Update software End." then "RESET") -> reboots the pen. */
/* 0x0809d5fc study_separate_game0715: study "separate" sub-flow ("Enter game0715,Firmware Flag=%d." / "separate suspend."). */
/* 0x08034014 usb_reset_speed: USB PHY speed (re)negotiation ("reset to high speed!" / "reset to full speed!"). */
/* 0x08030ddc open_product_log: opens the producer/factory log file USBSA:/Product log file.bin. */
/* 0x08030ee8 open_firmware_log: opens A:/Firmware log file.bin. */
/* 0x080af21c producer_update_main: factory producer/update driver ("Init fail"/"Bin fail"/"File fail"/"update over"). */
/* 0x080ae9f4 burn_write_nand_file: Burn_Lib_V1.0.0 secure-area NAND file writer ("WriteNandFileStart/End fail", "file name:%s"). */
/* 0x080aeba0 burn_check_status: Burn_Lib status/precondition check ("not format!"/"bios fail"/"Config fail"). */
/* 0x080aec58 burn_write_udisk_file: Burn_Lib U-disk file writer ("file path:%s"/"open file fail"/"write udisk file fail"). */
/* 0x080af158 burn_read_producer: Burn_Lib producer-image reader ("read producer fail"). */
/* 0x080bec58 nand_write_config: writes the NAND file-system config record ("FileCount/StartBlock/BlockCount", "write nand config fail!"). */
/* 0x080bf0c0 nand_write_fat_info: writes/logs the FAT geometry (fat_size/fat_type/bytes_per_sec/sec_per_clus/offset_fat1..2/offset_root/data_size). */
/* 0x080bf328 nand_write_mtd_data: writes MTD data during format ("fail in write mtd data"). */
/* 0x080bf4d4 nand_write_fat: writes the FAT during format ("fail in write fat"). */
/* 0x080be5b0 nand_erase_block_log: per-block erase in the format path ("Erase block: %d"). */
/* 0x080be468 nand_get_file_config: reads the file config info block ("Get file config info fail"). */
/* 0x080be254 nand_read_update_info: reads the update-info header ("read update info", "bFixNoFs:%d, SB:%d, BCnt:%d,fsb:%d"). */
/* 0x080ad2d0 medialib_get_version: MediaLib V0.6.3 entry/version banner ("--Media Lib %s--"/"--Leave Media Lib %s--"). */
/* 0x080bdc2c media_seek: MediaLib seek ("seek error, seek time > media total time !", "this media does not support seek !"). */
/* 0x080b8970 media_detect_codec: audio container/codec magic sniff ("#!AMR", "OggS", "vorbis", "FLAC"). */
/* 0x080bb70c ape_parse_tag: APE ("APETAGEX") tag parser. */
/* 0x080c0480 asf_parse_wm_metadata: ASF/WMA WM/* metadata (WM/TrackNumber, WM/AlbumTitle, WM/Year, WM/Genre, WM/Title). */
/* 0x080c1fe8 vorbis_parse_comment: Vorbis comment parser (title/artist/album/date/comment/genre/tracknumber). */
/* 0x080c43a4 adpcm_decode: IMA-ADPCM decode ("ADPCM RT -1"). */
/* 0x080ca0b4 crt_signal_desc: C runtime signal/exception description lookup ("Abnormal termination"/"Invalid Operation"/...). */
/* 0x08080094 game_quiz_sm: Prof.Mad/Player-Win quiz state machine ("Begin Ask Question!!!", "Not a Ask question code!!!!", "Prof.Mad Win", "Player Win"). 2N-MT twin: ask_question. */
/* 0x0808ed04 discovery_mode: free-exploration mode ("Enter Discovery Mode"). 2N-MT twin: discovery_mode. */
/* 0x080819f0 game_five_check_result: Game Five win/result check (adjacent "Game Five Init"; "Prof.Mad Win"/"Player Win"). */
/* 0x08082bc4 game_four_check_result: Game Four win/result check (adjacent "Game Four Init"; "Answer Right"/"Player Win"/"Prof.Mad Win"). */
/* 0x08087dc0 minigame_three_sm: MiniThree state machine ("Answer Right1/2/3", "Check All Line Cut?", "Exit MiniThree"). */
/* 0x080894b0 minigame_two_sm: MiniGame Two state machine (adjacent "MiniGame Two Init"; "exit game two"/"exit minigame two"). */
/* 0x080869e4 minigame_six_sm: MiniGame Six state machine (adjacent "MiniGame Six Init"). */
/* 0x0808a704 game_special_one_check_result: Special One result check (adjacent "Spe one start"). */
