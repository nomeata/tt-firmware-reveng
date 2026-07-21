/* firmware-re/ghidra_types.h -- reverse-engineered struct/type definitions for the 2N (ZC3202N) PROG.
 * Parsed by ghidra_rename.py via Ghidra's C parser (creates the types); globals + function
 * signatures that use them are then applied. Offsets are from RE; unknown gaps are explicit padding.
 * Inline field comments document each field (Ghidra keeps struct-member comments). */

/* QP-style hierarchical statechart control block.
 * Instance @0x080078e8; pointer global DAT_080e87d4 -> it. Dispatched by app_init_main line 65:
 *   handler = *( *(descriptor_table + cur_state*4) ); handler(); */
struct StateChart {
    unsigned char  cur_state;             /* +0x00 current (top-of-stack) state index byte */
    unsigned char  flag_01;               /* +0x01 */
    unsigned short pad_02;                /* +0x02 */
    unsigned short stack_depth;           /* +0x04 hierarchy state-stack depth */
    unsigned char  pad_06[14];            /* +0x06 */
    void *state_stack;           /* +0x14 -> top of the 0xc-byte state-stack entries */
    void *descriptor_table;      /* +0x18 state -> descriptor (entry fn @+0, event fn @+0xc) */
    void *event_action_table;    /* +0x1c action-id -> event-action fn */
    void *transition_action_table;/* +0x20 action-id -> transition-action fn */
    unsigned char  event_ring[0xc4];      /* +0x24 active-object event ring: 16 x 0xc slots, head u16 @+0xc0, tail @+0xc2 */
};

/* OID / GME runtime context (allocated 0xdf0 by akoid_init; reached via gb->akoidpara).
 * Explicit-offset: only verified fields are named; everything else stays undefined bytes. */
struct AkOidPara { /* size 0xdf0 */
    @0x016 unsigned short product_id;      /* active product id */
    @0x018 unsigned short first_oid;       /* first used OID code */
    @0x01a unsigned short last_oid;        /* last used OID code */
    @0x0b7 unsigned char  game_phase_idx;  /* current game phase index (set by game_play_phase_cue) */
    @0x0c8 unsigned int   cur_playlist_off;/* current phase playlist file offset (game_play_phase_cue) */
    @0x134 unsigned char  raw_xor;         /* raw XOR byte (GME header 0x1c) */
    @0x184 unsigned int   playlist_slot_lo[100]; /* per-slot cache, init 0xffffffff by game_load_playlist_offsets */
    @0x314 unsigned int   playlist_slot_hi[100]; /* per-slot cache, init 0 by game_load_playlist_offsets */
    @0xdd0 unsigned short jump_flag;       /* set by J() opcode */
    @0xdd2 unsigned short jump_target;     /* jump target OID */
};

/* Game context (0x240; allocated by game_start; reached via PTR_g_pMeGame). Verified fields only. */
struct MeGame { /* size 0x240 */
    @0x000 unsigned char phase;            /* current game phase */
    @0x001 unsigned char event;            /* pending in-game event / control code */
    @0x024 unsigned int  playlists_base;   /* base offset of the record's playlist-list table */
    @0x08c unsigned char voice_idx_for_oid;/* 48 x 6 permutation table of voice-variant indices */
    @0x19c unsigned int  item_count;       /* number of items/targets in the subgame */
    @0x1ac unsigned char playtimes;        /* per-item play count */
    @0x1dc unsigned char played_bitmask;   /* per-item already-played/found bitmask */
    @0x20e unsigned char current_voice;    /* currently selected voice variant */
};

/* Global control block (0x100). AkOidPara must be defined above (typed pointer). */
struct GlobalBlock { /* size 0x100 */
    @0x020 AkOidPara *akoidpara;           /* -> OID/GME runtime context */
    @0x024 unsigned char usb_boot_latch;   /* USB-boot pin br_gpio_read(0xb) latched at init */
};

/* ---- Function signatures (parsed + applied; each preceded by its docstring) ---- */

/* 0x08031274 boot_read_serial_and_codepage: read the pen serial + load the codepage; arg is the
   codepage selector (from map_gb0_to_codepage_sel), forwarded to codepage_get_header. */
unsigned int boot_read_serial_and_codepage(int codepage_sel);

/* 0x080286e8 map_gb0_to_codepage_sel: 20-entry ARM jump table, gb[0] -> codepage selector (default 0x17). */
int map_gb0_to_codepage_sel(int gb0_selector);

/* 0x08027e78 codepage_get_header: load the codepage header for codepage_sel (offset = codepage_sel<<5). */
unsigned int codepage_get_header(int codepage_sel);

/* ---- Function notes (docstrings only; migrated from descriptions.csv) ---- */
/* 0x0802fe20 app_init_main: the INIT task. HW bring-up (codec_clock/oid_sensor/clock_periph, GPIO incl. read(0xb)=USB-boot pin), timer/heap/ram-bitmap, fs_storage_mount_init (hangs forever on failure), akoid_init, sm_set_state(3,0), audio volume, serial+codepage, battery. Then installs the 3 statechart tables into the control block obj@0x080078e8 (+0x18=state-descriptor table 0x0811f650, +0x1c=event-action 0x0800a988, +0x20=transition-action 0x0800a5fc), event_queue_init, and dispatches the initial state's handler. NOTE: the descriptor table 0x0811f650 is bss and stays empty through this function (verified by write-watchpoint) -> it is populated earlier, by crt0, which runs before app_init_main. */
/* 0x08002714 sm_set_state(state,mode): clamp state to 0..6; if mode==0 run FUN_08019528(stateTable[state]) (an audio fade); store the current top-state index byte at *0x081dba00. */
/* 0x08045850 sm_get_state_descriptor_table(): returns 0x0811f650 (bss, runtime-populated) - the state->descriptor pointer array; descriptor+0x0=entry handler, +0xc=event handler. */
/* 0x08045840 sm_get_event_action_table(): returns 0x0800a988 (static) - indexed by an action id from a state's event handler. */
/* 0x08045848 sm_get_transition_action_table(): returns 0x0800a5fc (static) - transition-action functions, indexed by action id. */
/* 0x080e87b8 sm_state_entry: run the current state's entry handler = (*(*(desc_tbl[state_index])))(). desc_tbl=obj+0x18, state_index=**(obj+0x14). */
/* 0x080e8624 sm_dispatch_event(state,ev,arg,mode): call desc_tbl[state]->eventHandler(+0xc); on its return code run the event-action (obj+0x1c) or transition-action (obj+0x20) table entry; recurse up the hierarchy / to event_loop_dispatch. */
/* 0x080e871c sm_dispatch_to_hierarchy: pump one event through the hierarchical statechart (dispatch to current state, walk up on unhandled, run init action), then drain the event queue. */
/* 0x080e84e4 event_loop_dispatch: handle statechart LCA/exit bookkeeping - pops states off the state stack (obj+0x14, 0xc-byte entries, exit handler at entry-4) down to the transition target depth. */
/* 0x080e879c sm_current_state_byte: returns **(obj+0x14) = the current (top-of-stack) state byte. */
/* 0x080312f8 app_input_ctx_init: zero-init the ~10-byte state struct at DAT_0803133c, then set [0xa]=br_byte_getter() (a HW config/status byte), [0xb]=[0xc]=0, [0x10]=0xff, [0x14]=0. */
/* 0x080e5184 stub_ret0: stub; returns 0 (no-op in this build). */
/* 0x080316a4 store_cfg_pair(a,b): store two config bytes (app_init passes gb+0x58, gb+0x59) at DAT_080316b4+1/+2. */
/* 0x080306f8 gpio_config_unpack: unpack the packed 32-bit config at ctx+0x5c/+0x60 into 8 nibble fields and call br_gpio_config per field -> initial GPIO pin configuration. */
/* 0x080312e4 init_loop_str: iterate the NUL-terminated string at DAT_080312ec, calling bootrom func 0x07ffba3c once per char; return char count. Purpose uncertain (per-char register write or timed init). */
/* 0x080300a4 nop_stub: empty function (no-op). */
/* 0x0800255c sm_event_pump_thread: driver loop — GPIO5 heartbeat + audio_wait_tick, then drain the active-object ring (obj+0x24, head@+0xc0/tail@+0xc2, 16x0xc-byte {id,arg} slots) dispatching each event via sm_dispatch_to_hierarchy. */
/* 0x0800235c event_queue_post: enqueue an event {id,arg} onto the cascade/linked event queue for handler follow-ups. */
/* 0x08002024 is_audio_playing: return nonzero while an audio sample is still playing. */
/* 0x08004ed4 fs_open(wpath,...): VFS open of a file on the FAT/NAND filesystem (entry point; FatLib internals out of scope). */
/* 0x08004f7c fs_read(handle,buf,n): VFS read (entry point). */
/* 0x08004f8c fs_seek(handle,off,whence): VFS seek (entry point). */
/* 0x08005024 fs_read_xor_decrypt: read file bytes then XOR-decrypt media with the active key (bytes 0x00/0xFF/key/key^0xFF pass through). */
/* 0x080050e4 fs_read_plain: read file bytes with no decryption. */
/* 0x0800a024 sysapi_is_audio_playing: system_api wrapper (exposed to embedded GME binaries) around is_audio_playing. */
/* 0x080a9198 sysapi_play_sound: system_api wrapper to play a sound, exposed to embedded GME binaries. */
/* 0x0802ae74 gme_check_language: compare the GME's language string against the pen's configured language; reject on mismatch. */
/* 0x0802aff4 gme_mount_check_product: check a GME file's product id / mount it as the active product. */
/* 0x0802bc38 gme_rand_in_range(lo,hi): PRNG helper returning a value in [lo,hi], used by the random-play commands (FC00/FFE0). */
/* 0x0802bc64 gme_exec_command: execute one script action opcode — FFF0..FFF9 register arithmetic, FFE8/FFE0/FFE1/FB00/FC00 play, FD00 begin game, F8FF jump, FAFF cancel, FE00 arm delayed timer. */
/* 0x0802c1fc gme_parse_media_offsets: read the (offset,size) media-table entries referenced by the current line's playlist. */
/* 0x0802c2ac gme_parse_playlist: read a script line's playlist (a count + 16-bit media-table indices). */
/* 0x0802c38c gme_parse_actions: parse a script line's action list (7-byte actions: reg, command, type, operand). */
/* 0x0802c4e8 gme_check_condition: evaluate one conditional comparator (==,>,<,>=,<=,!=) of register vs value/register. */
/* 0x0802c74c gme_parse_check_conditions: parse a line's conditional list and evaluate it; gate the rest of the line. */
/* 0x0802c868 gme_oid_to_playscript(oid): index the script table (base+(oid-first_oid)*4) to a play-script; read its line count + 32-bit line offsets. */
/* 0x0802c930 gme_clear_script_state: reset the per-tap script interpreter state (line count/offsets/action buffers). */
/* 0x0802ca50 gme_parse_media_offsets2: variant media (offset,size) reader for a playlist. */
/* 0x0802cb00 gme_parse_start_end_oid: read the script table header's last/first used OID codes into AkOidPara. */
/* 0x0802cb6c gme_reset_registers: load the GME register-init table (count + 16-bit init values) into the register array. */
/* 0x0802cbe4 gme_parse_header: read the GME header offsets (script/media/add-script/game tables, product id@0x14, reg-init@0x18, raw-xor@0x1c, power-on playlistlist@0x71, special-OID list@0x94) into globals+AkOidPara. */
/* 0x0802d0ec gme_parse_additional_script: parse the additional-script table (header 0x0c); usually empty. */
/* 0x0802d160 gme_oid_dispatch: top-level book-OID handler — mount product, derive the media XOR key, look up and run the OID's play script. */
/* 0x0802fb20 gme_alloc_binary_region: allocate a RAM region to load an embedded GME ARM binary into. */
/* 0x0802ff2c get_serial_no: return the pen's serial number. */
/* 0x08030870 heap_init: initialize the dynamic-memory heap used by malloc. */
/* 0x08033eb0 usb_power_switch: switch USB (drive-mode) power on/off. */
/* 0x08038f20 low_power_warn_2: play a low-battery warning cue (variant). */
/* 0x080391d8 low_power_warn: play the low-battery warning. */
/* 0x0803a6dc nftl_init: initialize the NAND flash-translation layer (entry point; MtdLib internals out of scope). */
/* 0x0803ab48 nftl_init_2: secondary NFTL init (entry point). */
/* 0x08046b4c usb_state_handler: statechart state handler for USB mass-storage (drive) mode. */
/* 0x0806003c game_reading_sm: the reading / 'find all X' built-in game state machine (2N; twin of ZC3201 game_reading_sm_dispatch). */
/* 0x08061510 game_start: allocate the 0x210 game context, load the record's playlist offsets, then build 48 random non-repeating permutations of 6 voice variants (the +0x8c slot table). */
/* 0x08061594 game_oid_dispatch: routes a tapped OID into the active built-in game's state machine. */
/* 0x0806a638 game_load_playlist_offsets: read the current game record from the GME game table into the pMeGame playlist-offset fields (+0x24..0x88). */
/* 0x0805f9c8 game_play_phase_cue(offset,index): play a game phase's playlist-list (start/round/finish cue) by index. */
/* 0x0805f4dc game_ctx_init_flags: set the initial flag bytes in the game context. */
/* 0x0805f4f8 game_load_header: read the game record header fields. */
/* 0x0805fb98 game_pick_voice: choose a (non-repeating) voice variant for the current game slot. */
/* 0x08064d00 game_wronglimit_sm: the multiple-choice / wrong-attempt-limited built-in game state machine (2N). */
/* 0x08083448 game_quiz_sm: the quiz / 'Prof.Mad' Q&A built-in game state machine (2N). */
/* 0x080889bc game_findtarget_sm: the find/touch-the-target built-in game state machine (2N). */
/* 0x080a03f0 gme_launch_binary_build_sysapi: build the system_api function table and jump into an embedded GME ARM binary (2N-only feature). */
/* 0x080a0894 gme_read_main_binary_table: read the embedded main-binary descriptor from GME header offset 0xA8 (address,length). */
/* 0x080a0ee4 fwl_voice_play: play a firmware voice/prompt sample. */
/* 0x080a1004 fwl_voice_play_2: play a firmware voice sample (variant). */
/* 0x080a1090 play_media_setup: universal audio choke point — (file handle, offset, size) set up decode+playback of a media sample; all audio sources funnel here. */
/* 0x080a1198 play_media: play a media-table sample given (offset,size). */
/* 0x08028b7c aud_player_stop: stop the audio player. */
/* 0x08028c74 aud_player_stop_2: stop the audio player (variant). */
/* 0x080e44cc akoid_init: allocate+zero the 0xdf0 AkOidPara OID context and set defaults (last_range/last_rand_media=0xff, first-oid tracking, etc.). */
/* 0x080e47ec akoid_sensor_poll: poll the OID camera sensor and return a decoded OID code if present. */
/* 0x080e4b2c akoid_sensor_config: configure the OID camera sensor. */
/* 0x080e5030 akoid_reg_write: write an OID-sensor hardware register. */
/* 0x080e5050 akoid_reg_read: read an OID-sensor hardware register. */
/* 0x080e4090 akoid_decode_frame: decode a captured camera frame into an OID code. */
/* 0x080286e8 map_gb0_to_codepage_sel: 20-entry ARM jump table mapping gb[0] (0..0x13) to a small code (default 0x17). Its return (in r0) is the argument to boot_read_serial_and_codepage -> codepage_get_header, i.e. a codepage/encoding selector. (Ghidra decomp of app_init_main drops this return->arg dataflow, making it look discarded - it is NOT.) */
/* 0x080e46fc akoid_shift_out: bit-bang a byte MSB-first to the OID sensor over GPIO2=clock, GPIO9=data. */
/* 0x080e404c str_compare: strcmp-style byte-string compare; returns 0 if equal, -1 on first mismatch. */
/* 0x080e8f60 i2c_write_bytes: GPIO bit-bang I2C write (SDA=pin 0xb, SCL=pin 0xc): init+start, send device + param_3 data bytes from param_2, stop; returns 1 if all ACKed. */
/* 0x080e8e0c i2c_send_byte: bit-bang one byte MSB-first on the I2C SDA/SCL GPIOs; returns the ACK bit. */
/* 0x080e8f08 i2c_gpio_init: configure the two I2C GPIO pins (SDA 0xb / SCL 0xc) direction+output registers. */
/* 0x080e8e94 i2c_start: I2C start condition on the bit-bang GPIOs (maybe start vs setup). */
/* 0x080e8db0 i2c_stop: I2C stop condition on the bit-bang GPIOs (maybe). */
/* 0x08001f74 gpio_pin_lookup: map a logical pin/channel id (switch) to a physical GPIO pin/bit via config tables. */
/* 0x080a2ef8 file_close_if_open: close the file handle if != -1; return 1 if closed else 0. */
/* 0x08000354 hang_forever: infinite do{}while(true) loop -- panic/hang. */
/* 0x08000320 wait_hw_bits_clear: busy-wait until the given bit(s) clear in HW reg 0x04000004 (maybe GPIO/IRQ ready). */
/* 0x080e988c list_is_empty: true if param_1 is null or param_1->next(+0xc) is null (maybe). */
int str_compare(char *a, char *b);
int i2c_write_bytes(int dev, unsigned char *buf, unsigned int len);
int file_close_if_open(int fh);
/* 0x08031274 boot_read_serial_and_codepage: reads serial + loads codepage(codepage_sel). */
int boot_read_serial_and_codepage(int codepage_sel);
/* 0x080286e8 map_gb0_to_codepage_sel: gb[0] -> codepage selector (jump table). */
/* 0x08027e78 codepage_get_header: load codepage header (offset = codepage_sel<<5). */
int codepage_get_header(int codepage_sel);

/* ===== Audio decode path (Fwl_pfVoice -> aud_player -> MediaLib) =====
 * Full sound path from a GME binary:
 *   sysapi.play_sound (field +0x2c = 0x080a9198) -> play_media (0x080a1198) -> play_media_setup
 *   (0x080a1090) -> aud_player_reset(vol) + aud_player_set_source(...,codec) + aud_player_play.
 * aud_player_set_source calls medialib_open, which (when no codec is forced) calls
 * medialib_detect_codec to sniff the codec FROM THE STREAM HEADER. All file reads in this path
 * pass through the media XOR-decrypt read callback (fs_read_xor_decrypt 0x08005024, media key
 * derived in gme_oid_dispatch). So a sample only plays if its bytes, AFTER XOR-decrypt, form a
 * header that detect_codec recognises AND map to a supported codec id; otherwise medialib_open
 * hits the "Unsupported" branch, set_source returns 0, and aud_player_play is silent (no error
 * is surfaced to the caller). */
/* 0x080a9198 sysapi_play_sound: system_api field +0x2c wrapper -> play_media. Args (filehandle, offset, size). */
/* 0x080f4d70 medialib_detect_codec: read first 0x40 bytes of the (decrypted) stream and sniff the
 *   codec id: WAV/AVI/ANKA via medialib_detect_wav, ASF/WMA via medialib_detect_asf, "#!AMR"->0x0d,
 *   OggS+0x7f"FLAC"->0x12(Ogg-FLAC), "fLaC"->0x11(FLAC), OggS+"vorbis"->0x14(Vorbis), plus mp3/id3
 *   probes. Returns 0 = unknown/unsupported. */
/* 0x080f4854 medialib_detect_wav: sniff a RIFF container. Header must be "RIFF" (or "ANKA") then a
 *   form id: "AVI "->2, "ANKA"->1, "WAVE"-> requires "fmt " chunk IMMEDIATELY after "WAVE"; then the
 *   fmt tag u16 decides codec: tag 0x11 (IMA-ADPCM) or 0x02 (MS-ADPCM) or ((~tag & 0x50)!=0) -> 3,
 *   else -> 10. Non-canonical WAV (chunk before "fmt ", or unexpected fmt tag) -> returns 0. */
/* 0x080f49f0 medialib_detect_asf: sniff an ASF/WMA stream by its 16-byte GUID; returns codec 8. (tentative) */
/* 0x080f4588 stream_length: remaining/total length of the media stream (used to guard header reads). */
/* 0x080e67cc medialib_open: alloc 0x1b4 MediaCtx, pick codec (forced field, else detect_codec), then
 *   switch on codec id installing the per-codec decoder vtable at +0x38.. :
 *     2/3/8/0x0a = WAV/PCM/IMA-ADPCM family, 0x0d = AMR, 0x11 = FLAC, 0x12 = Ogg-FLAC/Vorbis,
 *     0x14 = video; any other id -> "MediaLib: ERROR: Open: Unsupported" -> returns 0 (=> silence). */
/* 0x08029874 aud_player_set_source: build the media descriptor (filehandle/offset/size + fs callbacks),
 *   store codec id at +0xe9, and (unless codec==0x13) call medialib_open. If medialib_open returns 0,
 *   set_source returns 0 and NOTHING plays. IMA-ADPCM decoder = ima_adpcm_decode 0x081042b8. */
/* 0x08029450 aud_player_reset: latch playback params into the player object: +0x29,+0x2a, and +0x2b = volume. */

/* MediaLib context: 0x1b4 bytes, allocated in medialib_open; ptr is param_1 of medialib_play/stop/etc. */
struct MediaCtx {
    void *stream;                /* +0x00 media stream/source object */
    void *log_cb;                /* +0x04 printf-style log callback (copied from opener) */
    unsigned char pad_08[0x10];  /* +0x08 */
    void *malloc_cb;             /* +0x18 */
    void *free_cb;               /* +0x1c */
    unsigned char pad_20[0x10];  /* +0x20 */
    unsigned char state;         /* +0x30 0=init 1=playing 2/3=fast-fwd/rewind 4/5=stopped */
    unsigned char codec_id;      /* +0x31 codec id from detect_codec (2/3/8/0x0a WAV, 0x0d AMR, 0x11 FLAC, 0x12 Vorbis, 0x14 video) */
    unsigned short pad_32;       /* +0x32 */
    void *decoder_inst;          /* +0x34 decoder instance handle */
    void *dec_open;              /* +0x38 per-codec decoder vtable start (open/free/setinput/stop/getinfo/decode/seek...) */
    unsigned char vtable_rest[0x24];/* +0x3c .. +0x5f remaining vtable slots */
    void *audio_out;             /* +0x60 audio-out handle */
    int   file_handle;           /* +0x64 copy of the file handle */
    unsigned char has_audio;     /* +0x68 */
    unsigned char has_video;     /* +0x69 */
    unsigned char fr_capable;    /* +0x6a */
    unsigned char pad_6b[2];     /* +0x6b */
    unsigned char default_1;     /* +0x6d =1 default */
    unsigned char pad_6e[2];     /* +0x6e */
    void *pcm_handle;            /* +0x70 */
    unsigned short samplerate;   /* +0x74 */
    unsigned short channels;     /* +0x76 (approx) */
    unsigned char pad_78[0x12c]; /* +0x78 .. +0x1a3 (status @0x19c.., video bufs @0x80/0x84) */
    unsigned char tail[0x10];    /* +0x1a4 .. +0x1b3 loop_done/loop_enabled/loop range */
};
