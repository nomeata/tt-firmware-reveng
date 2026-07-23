/* firmware-re/ghidra_types.h -- reverse-engineered struct/type definitions for the 2N (ZC3202N) PROG.
 * Parsed by ghidra_rename.py via Ghidra's C parser (creates the types); globals + function
 * signatures that use them are then applied. Offsets are from RE; unknown gaps are explicit padding.
 * Inline field comments document each field (Ghidra keeps struct-member comments). */

/* QP-style hierarchical statechart control block.
 * Instance @0x080108e8; pointer global DAT_080e87d4 -> it. Dispatched by app_init_main line 65:
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
    @0x125 unsigned char  evt_dispatch_count; /* free-running book-mode event counter: ++ once at the TOP of every gme_oid_dispatch call, for ANY event (0x1046 heartbeat ~10/s, 0x1060 tap, 0x30 timer, keys). Sole writer. Entropy source: P* (FFE0) pick = count % playlist_len; FFA1 plays iff (count&1)==0. Proven (asm + tt-emu 2026-07-20) */
    @0x128 unsigned char  line_matched;    /* a script line's conditions matched -> decode + run its actions this dispatch */
    @0x129 unsigned char  addscript_line_idx; /* index of the matched additional-script line */
    @0x12a unsigned char  actq_walk_mode;  /* action-queue walk mode: 0 idle; 1 EXEC (run each pending action at an audio boundary via gme_exec_command); 2 REPLAY (re-issue plays after an interruption WITHOUT re-rolling: P*->pstar_last_idx, P(b-a)->prand_last_idx, FFA1->ffa1_play_idx if ffa1_did_play) */
    @0x12b unsigned char  actq_consumed;   /* walk position into actq_pending_order (decomp offset 299) */
    @0x12c unsigned char  plseq_mode;      /* sequential playlist player: 0 off; 1 = PA* (FFE1, whole list); 2 = PA(b-a) (FB00 range) (decomp offset 300) */
    @0x12d unsigned char  plseq_pos;       /* sequential playlist position */
    @0x12e unsigned char  plseq_end;       /* PA(b-a) inclusive end index (FB00 operand low byte) */
    @0x12f unsigned char  game_begin_pending; /* set 1 by G(m) FD00 */
    @0x131 unsigned char  game_index;      /* FD00 operand: game-table record to launch */
    @0x132 unsigned char  game_launch_phase; /* game-launch handshake; ==2 -> dispatch replays the current action (REPLAY special-cases) and sets 3 */
    @0x141 unsigned char  pstar_last_idx;  /* last P* (FFE0) playlist pick = evt_dispatch_count % playlist_len; replayed on resume */
    @0x142 unsigned char  prand_last_idx;  /* last P(b-a) (FC00) playlist pick (gme_rand_in_range); replayed on resume */
    @0x134 unsigned char  raw_xor;         /* raw XOR byte (GME header 0x1c) */
    @0x184 unsigned int   playlist_slot_lo[100]; /* per-slot cache, init 0xffffffff by game_load_playlist_offsets */
    @0x314 unsigned int   playlist_slot_hi[100]; /* per-slot cache, init 0 by game_load_playlist_offsets */
    @0x4aa unsigned char  phase_cue_replaying; /* set 1 just before a gRepeatOID tap replays the current phase cue (gameNN engines); the replay uses game_phase_idx/cur_playlist_off */
    @0xdcf unsigned char  menu_unlock_dcf; /* game-menu unlock flag A: set by game61 (type 19) top-tier win when product==0xC and game record==0x16; gates a locked gameSelect id in game18_select_handler */
    @0xdd8 unsigned int   deferred_cue_pll_off; /* playlist-list offset armed by the play script for control OID 0xF3D ("play the deferred cue"); consumed by the type 7/8 (game17/18) and 19/20/21 (game61/62/63) engines */
    @0xdde unsigned char  menu_unlock_dde; /* game-menu unlock flag B: set by game66 (type 22) top-tier win when product != 0x13; read by game18_select_handler + game18_entry (product 0x12) */
    @0xdd0 unsigned short jump_flag;       /* set by J() opcode */
    @0xdd2 unsigned short jump_target;     /* jump target OID */
    @0xddf unsigned char  ffa1_did_play;   /* FFA1 "coin-flip play" replay latch: 1 = the last executed FFA1 rolled "play" (evt_dispatch_count was even). Cleared at every FFA1 exec + on book entry. NOT cleared by replay */
    @0xde0 unsigned char  ffa1_play_idx;   /* playlist index the last FFA1 played (operand & 0xff; the immediate play uses the full u16 operand). 0xff = none (book_state_entry) */
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

/* ---- enums: GME opcode set / conditional comparators / event ids / statechart states ----
 * Created by apply_sigs.py + ghidra_rename.py (step a0) as Ghidra EnumDataType. Typing a
 * discriminant variable (a param via a signature below, or a local via @local) as one of these
 * makes the decompiler render its constants symbolically (opcode == OP_PLAY_COINFLIP, not 0xffa1).
 * int-sized to match the uint/int variables they type. Values are Proven from the docstrings. */
enum GmeOpcode {
    OP_ARITH_SET     = 0xFFF0,   /* register-arithmetic block FFF0..FFF8 (FFF3=Div, FFF4=Mod proven; */
    OP_ARITH_F1      = 0xFFF1,   /* the rest are arithmetic/logic ops not yet individually verified) */
    OP_ARITH_F2      = 0xFFF2,
    OP_DIV           = 0xFFF3,   /* Div (rt_udiv) */
    OP_MOD           = 0xFFF4,   /* Mod (rt_udiv remainder) */
    OP_ARITH_F5      = 0xFFF5,
    OP_ARITH_F6      = 0xFFF6,
    OP_ARITH_F7      = 0xFFF7,
    OP_ARITH_F8      = 0xFFF8,
    OP_PLAY          = 0xFFE8,   /* P: play playlist[operand] (the play tail @0x08035288) */
    OP_PLAY_RANDCOUNT= 0xFFE0,   /* P*: pick = evt_dispatch_count % playlist_len -> pstar_last_idx */
    OP_PLAY_SEQ_ALL  = 0xFFE1,   /* PA*: sequential player over the whole line playlist */
    OP_PLAY_SEQ_RANGE= 0xFB00,   /* PA(a,b): sequential player over an index range */
    OP_PLAY_RAND_RANGE=0xFC00,   /* P(a,b): gme_rand_in_range pick -> prand_last_idx (heartbeat entropy) */
    OP_PLAY_COINFLIP = 0xFFA1,   /* coin-flip play: iff (evt_dispatch_count&1)==0 play playlist[operand] */
    OP_GAME_BEGIN    = 0xFD00,   /* G(m): begin game record m */
    OP_JUMP          = 0xF8FF,   /* J: deferred jump to another OID (jump_flag/jump_target) */
    OP_CANCEL        = 0xFAFF,   /* cancel/clear */
    OP_TIMER_ARM     = 0xFE00,   /* arm PERIODIC sw-timer (hal_timer_register(0,m*100,autoreload=1)) */
    OP_TIMER_CANCEL  = 0xFEFF,   /* cancel the GME sw-timer (handle @0x08121ecc) */
    OP_RAND_TICK     = 0xFF00,   /* reg = sys_get_tick() % (m+1) ("Rand(%d)=%d") */
    OP_SNDPROF0      = 0xFEE0,   /* FEE0..FEE7 -> sm_set_sound_profile(0..7) */
    OP_SNDPROF1      = 0xFEE1,
    OP_SNDPROF2      = 0xFEE2,
    OP_SNDPROF3      = 0xFEE3,
    OP_SNDPROF4      = 0xFEE4,
    OP_SNDPROF5      = 0xFEE5,
    OP_SNDPROF6      = 0xFEE6,
    OP_SNDPROF7      = 0xFEE7,
    COND_EQ          = 0xFFF9,   /* conditional comparators (gme_check_condition) share the FFFx space */
    COND_GT          = 0xFFFA,
    COND_LT          = 0xFFFB,
    COND_GEQ         = 0xFFFD,
    COND_LEQ         = 0xFFFE,
    COND_NEQ         = 0xFFFF
};
/* Event ids routed to the book-mode dispatcher (gme_oid_dispatch event_id param). */
enum GmeEvent {
    EV_TIMER         = 0x0030,   /* GME software-timer fired (args[0]==*0x08121ecc) */
    EV_HEARTBEAT     = 0x1046,   /* OID-poll / heartbeat tick (~10/s) -- drives the action-queue walk */
    EV_OID_PARTIAL   = 0x105F,   /* partial OID / button code */
    EV_OID_TAP       = 0x1060    /* a fully decoded OID tap (the main book event) */
};

/* ---- @noreturn: mark halt/panic paths so callers stop showing bogus fall-through ----
 * @noreturn 0x08009354   (hang_forever: infinite do{}while(true) panic loop)
 */

/* ---- @local: rename/retype locals inside a function (HighFunction DB commit) ----
 * Syntax: @local 0xFUNC <key> <newname> [<type>]   key = "s<±0xNN>" stack offset (stable) or
 * "=curname" (current decomp name). gme_parse_actions scratch (validated example):
 * @local 0x080354c8 =local_24 action_field
 * @local 0x080354c8 =uVar7 action_idx
 */

/* ---- @slot: typed literal-pool pointer slots (parsed by ghidra_rename.py step (a2)) ----
 * The GME script engine reaches its state through pc-relative literal-pool slots; typing the
 * slots makes the decomp render them by name and propagates AkOidPara/GlobalBlock field names.
 * Slots aliasing the same RAM cell (one pool per function) get _2/_3 suffixes; the RAM address
 * each slot points to is noted. RE 2026-07-20 (0xFFA1 path), all values read from PROG.bin.
 *
 * GlobalBlock pointer (-> 0x080089a4; ->akoidpara @+0x20 = 0x080089c4):
 * @slot 0x08034ad0 GlobalBlock* p_gb            (pool of gme_exec_command/book_state_entry)
 * @slot 0x08036194 GlobalBlock* p_gb_2          (pool of gme_oid_dispatch, first half)
 * @slot 0x08037820 GlobalBlock* p_gb_3          (pool of gme_oid_dispatch, second half)
 *
 * GME file/timer/registers:
 * @slot 0x080352f8 unsigned int* p_gme_file     (-> 0x08121ed0 GME file handle)
 * @slot 0x08036730 unsigned int* p_gme_file_2
 * @slot 0x08035304 unsigned int* p_gme_timer_handle (-> 0x08121ecc, FE00/FEFF sw-timer)
 * @slot 0x0803530c unsigned short* p_gme_regs   (-> 0x081da350 $register file)
 *
 * Current line's decoded ACTION QUEUE (gme_parse_actions; <=8 actions, count clamped):
 * @slot 0x08035600 unsigned char* p_actq_count  (-> 0x081da0a8)
 * @slot 0x080377f8 unsigned char* p_actq_count_2
 * @slot 0x0803561c unsigned short* p_actq_reg   (-> 0x081da0ea u16[8] register operand)
 * @slot 0x080377f0 unsigned short* p_actq_reg_2
 * @slot 0x08035618 unsigned short* p_actq_opcode (-> 0x081da0fa u16[8] command opcode)
 * @slot 0x080377e0 unsigned short* p_actq_opcode_2
 * @slot 0x08035614 unsigned char* p_actq_is_literal (-> 0x081da10a u8[8] operand-type byte)
 * @slot 0x080377ec unsigned char* p_actq_is_literal_2
 * @slot 0x08035620 unsigned short* p_actq_operand (-> 0x081da112 u16[8] m operand)
 * @slot 0x080377e8 unsigned short* p_actq_operand_2
 * @slot 0x08037818 unsigned char* p_actq_pending_order (-> 0x081da5e4 u8[8]: queue positions of the non-immediate actions, walked at audio boundaries)
 * @slot 0x0803618c unsigned char* p_actq_pending_order_2
 * @slot 0x0803781c unsigned char* p_actq_pending_count (-> 0x081da5ec)
 * @slot 0x08036188 unsigned char* p_actq_pending_count_2
 *
 * Current line's PLAYLIST + resolved media (gme_parse_playlist/gme_parse_media_offsets):
 * @slot 0x080352fc unsigned short* p_playlist_len (-> 0x081da122)
 * @slot 0x08037814 unsigned short* p_playlist_len_2
 * @slot 0x080355ec unsigned short* p_playlist_entries (-> 0x081da126 u16[]: media-table indices)
 * @slot 0x08035300 unsigned int* p_pl_media_off (-> 0x081da4e4 u32[32]: per-playlist-entry media file offset)
 * @slot 0x08036728 unsigned int* p_pl_media_off_2
 * @slot 0x08035308 unsigned int* p_pl_media_size (-> 0x081da564 u32[32]: per-playlist-entry media size)
 * @slot 0x0803672c unsigned int* p_pl_media_size_2
 * @slot 0x080355e8 unsigned int* p_mediatable_off (-> 0x081da098 media-table file offset)
 * @slot 0x080355fc unsigned int* p_cur_line_off (-> 0x081da0a4 current script-line file offset)
 * @slot 0x080355f0 unsigned int* p_line_playlist_off (-> 0x081da1a8 file offset of the line's playlist, = line_off + 2 + nconds*8 + 2 + nactions*7)
 *
 * Plain pool CONSTANTS (not pointers) that otherwise render as bare DAT_:
 * @slot 0x080377e4 unsigned int k_opcode_ffe1   (= 0x0000ffe1; the queue walks classify play opcodes via (op-0xFFE8==0 || op==0xFFE1 || op==0xFB00))
 * @slot 0x080366f8 unsigned int k_event_oid_tap (= 0x00001060; synthetic re-dispatch event id)
 * @slot 0x08036704 unsigned int k_off_jump_target (= 0x00000dd2 = offsetof(AkOidPara, jump_target))
 */

/* ---- Function signatures (parsed + applied; each preceded by its docstring) ---- */

/* 0x08034da0 gme_exec_command -- see the docstring further down; signature so the decomp
   names the four action fields. is_literal: 1 = operand is a literal, else a register id. */
void gme_exec_command(unsigned int reg_idx, GmeOpcode opcode, unsigned int is_literal, unsigned int operand);

/* 0x0803629c gme_oid_dispatch -- see the docstring further down. */
unsigned long long gme_oid_dispatch(GmeEvent event_id, int *event_arg, int a3, unsigned int a4);

/* 0x080354c8 gme_parse_actions -- see the docstring further down. */
void gme_parse_actions(void);

/* 0x0803a274 boot_read_serial_and_codepage: read the pen serial + load the codepage; arg is the
   codepage selector (from map_gb0_to_codepage_sel), forwarded to codepage_get_header. */
unsigned int boot_read_serial_and_codepage(int codepage_sel);

/* 0x080316e8 map_gb0_to_codepage_sel: 20-entry ARM jump table, gb[0] -> codepage selector (default 0x17). */
int map_gb0_to_codepage_sel(int gb0_selector);

/* 0x08030e78 codepage_get_header: load the codepage header for codepage_sel (offset = codepage_sel<<5). */
unsigned int codepage_get_header(int codepage_sel);

/* ---- Function notes (docstrings only; migrated from descriptions.csv) ---- */
/* 0x08038e20 app_init_main: the INIT task. HW bring-up (codec_clock/oid_sensor/clock_periph, GPIO incl. read(0xb)=USB-boot pin), timer/heap/ram-bitmap, fs_storage_mount_init (hangs forever on failure), akoid_init, sm_set_state(3,0), audio volume, serial+codepage, battery. Then installs the 3 statechart tables into the control block obj@0x080108e8 (+0x18=state-descriptor table 0x08128650, +0x1c=event-action 0x08013988, +0x20=transition-action 0x080135fc), event_queue_init, and dispatches the initial state's handler. NOTE: the descriptor table 0x08128650 is bss and stays empty through this function (verified by write-watchpoint) -> it is populated earlier, by crt0, which runs before app_init_main. */
/* 0x0800b714 sm_set_state(state,mode): clamp state to 0..6; if mode==0 run FUN_08019528(stateTable[state]) (an audio fade); store the current top-state index byte at *0x081e4a00. */
/* 0x0804e850 sm_get_state_descriptor_table(): returns 0x08128650 (bss, runtime-populated) - the state->descriptor pointer array; descriptor+0x0=entry handler, +0xc=event handler. */
/* 0x0804e840 sm_get_event_action_table(): returns 0x08013988 (static) - indexed by an action id from a state's event handler. */
/* 0x0804e848 sm_get_transition_action_table(): returns 0x080135fc (static) - transition-action functions, indexed by action id. */
/* 0x080f17b8 sm_state_entry: run the current state's entry handler = (*(*(desc_tbl[state_index])))(). desc_tbl=obj+0x18, state_index=**(obj+0x14). */
/* 0x080f1624 sm_dispatch_event(state,ev,arg,mode): call desc_tbl[state]->eventHandler(+0xc); on its return code run the event-action (obj+0x1c) or transition-action (obj+0x20) table entry; recurse up the hierarchy / to event_loop_dispatch. */
/* 0x080f171c sm_dispatch_to_hierarchy: pump one event through the hierarchical statechart (dispatch to current state, walk up on unhandled, run init action), then drain the event queue. */
/* 0x080f14e4 event_loop_dispatch: handle statechart LCA/exit bookkeeping - pops states off the state stack (obj+0x14, 0xc-byte entries, exit handler at entry-4) down to the transition target depth. */
/* 0x080f179c sm_current_state_byte: returns **(obj+0x14) = the current (top-of-stack) state byte. */
/* 0x0803a2f8 app_input_ctx_init: zero-init the ~10-byte state struct at DAT_0803133c, then set [0xa]=br_byte_getter() (a HW config/status byte), [0xb]=[0xc]=0, [0x10]=0xff, [0x14]=0. */
/* 0x080ee184 stub_ret0: stub; returns 0 (no-op in this build). */
/* 0x0803a6a4 store_cfg_pair(a,b): store two config bytes (app_init passes gb+0x58, gb+0x59) at DAT_080316b4+1/+2. */
/* 0x080396f8 gpio_config_unpack: unpack the packed 32-bit config at ctx+0x5c/+0x60 into 8 nibble fields and call br_gpio_config per field -> initial GPIO pin configuration. */
/* 0x0803a2e4 init_loop_str: iterate the NUL-terminated string at DAT_080312ec, calling bootrom func 0x07ffba3c once per char; return char count. Purpose uncertain (per-char register write or timed init). */
/* 0x080390a4 nop_stub: empty function (no-op). */
/* 0x0800b55c sm_event_pump_thread: driver loop — GPIO5 heartbeat + audio_wait_tick, then drain the active-object ring (obj+0x24, head@+0xc0/tail@+0xc2, 16x0xc-byte {id,arg} slots) dispatching each event via sm_dispatch_to_hierarchy. */
/* 0x0800b35c event_queue_post: enqueue an event {id,arg} onto the cascade/linked event queue for handler follow-ups. */
/* 0x0800b024 is_audio_playing: return nonzero while an audio sample is still playing. */
/* 0x0800ded4 fs_open(wpath,...): VFS open of a file on the FAT/NAND filesystem (entry point; FatLib internals out of scope). */
/* 0x0800df7c fs_read(handle,buf,n): VFS read (entry point). */
/* 0x0800df8c fs_seek(handle,off,whence): VFS seek (entry point). */
/* 0x0800e024 fs_read_xor_decrypt: read file bytes then XOR-decrypt media with the active key (bytes 0x00/0xFF/key/key^0xFF pass through). */
/* 0x0800e0e4 fs_read_plain: read file bytes with no decryption. */
/* 0x080b2198 sysapi_play_sound: system_api wrapper to play a sound, exposed to embedded GME binaries. */
/* 0x08033e74 gme_check_language: compare the GME's language string against the pen's configured language; reject on mismatch. */
/* 0x08033ff4 gme_mount_check_product: check a GME file's product id / mount it as the active product. */
/* 0x08034c38 gme_rand_in_range(lo,hi): PRNG helper returning a value in [lo,hi], used by the random-play commands (FC00/FFE0). */
/* 0x08034c64 gme_exec_command: execute one script action opcode — FFF0..FFF9 register arithmetic, FFE8/FFE0/FFE1/FB00/FC00 play, FD00 begin game, F8FF jump, FAFF cancel, FE00 arm delayed timer. */
/* 0x080351fc gme_parse_media_offsets: read the (offset,size) media-table entries referenced by the current line's playlist. */
/* 0x080352ac gme_parse_playlist: read a script line's playlist (a count + 16-bit media-table indices). */
/* 0x080354c8 gme_parse_actions: decode the current script line's action list (7-byte actions: u16 reg, u16 command, u8 type, u16 operand) into the parallel arrays p_actq_reg/p_actq_opcode/p_actq_is_literal/p_actq_operand; count -> p_actq_count, CLAMPED to 8 (a line can declare more; the firmware silently drops them). Leaves p_line_playlist_off = line_off + 2 + nconds*8 + 2 + nactions*7. */
/* 0x080354e8 gme_check_condition: evaluate one conditional comparator (==,>,<,>=,<=,!=) of register vs value/register. */
/* 0x0803574c gme_parse_check_conditions: parse a line's conditional list and evaluate it; gate the rest of the line. */
/* 0x08035868 gme_oid_to_playscript(oid): index the script table (base+(oid-first_oid)*4) to a play-script; read its line count + 32-bit line offsets. */
/* 0x08035930 gme_clear_script_state: reset the per-tap script interpreter state (line count/offsets/action buffers). */
/* 0x08035a50 gme_parse_media_offsets2: variant media (offset,size) reader for a playlist. */
/* 0x08035b00 gme_parse_start_end_oid: read the script table header's last/first used OID codes into AkOidPara. */
/* 0x08035b6c gme_reset_registers: load the GME register-init table (count + 16-bit init values) into the register array. */
/* 0x08035be4 gme_parse_header: read the GME header offsets (script/media/add-script/game tables, product id@0x14, reg-init@0x18, raw-xor@0x1c, power-on playlistlist@0x71, special-OID list@0x94) into globals+AkOidPara. */
/* 0x080360ec gme_parse_additional_script: parse the additional-script table (header 0x0c); usually empty. */
/* 0x08036160 gme_oid_dispatch: top-level book-OID handler — mount product, derive the media XOR key, look up and run the OID's play script. */
/* 0x08038b20 gme_alloc_binary_region: allocate a RAM region to load an embedded GME ARM binary into. */
/* 0x08038f2c get_serial_no: return the pen's serial number. */
/* 0x08039870 heap_init: initialize the dynamic-memory heap used by malloc. */
/* 0x0803ceb0 usb_power_switch: switch USB (drive-mode) power on/off. */
/* 0x08041f20 low_power_warn_2: play a low-battery warning cue (variant). */
/* 0x080421d8 low_power_warn: play the low-battery warning. */
/* 0x080436dc nftl_init: initialize the NAND flash-translation layer (entry point; MtdLib internals out of scope). */
/* 0x08043b48 nftl_init_2: secondary NFTL init (entry point). */
/* 0x0804fb4c usb_state_handler: statechart state handler for USB mass-storage (drive) mode. */
/* 0x0806903c game_reading_sm: the reading / 'find all X' built-in game state machine (2N; twin of ZC3201 game_reading_sm_dispatch). */
/* 0x0806a510 game_start: allocate the 0x210 game context, load the record's playlist offsets, then build 48 random non-repeating permutations of 6 voice variants (the +0x8c slot table). */
/* 0x0806a594 game_oid_dispatch: routes a tapped OID into the active built-in game's state machine. */
/* 0x08073638 game_load_playlist_offsets: read the current game record from the GME game table into the pMeGame playlist-offset fields (+0x24..0x88). */
/* 0x080689c8 game_play_phase_cue(offset,index): play a game phase's playlist-list (start/round/finish cue) by index. */
/* 0x080684dc game_ctx_init_flags: set the initial flag bytes in the game context. */
/* 0x080684f8 game_load_header: read the game record header fields. */
/* 0x08068b98 game_pick_voice: choose a (non-repeating) voice variant for the current game slot. */
/* 0x0806dd00 game_wronglimit_sm: the multiple-choice / wrong-attempt-limited built-in game state machine (2N). */
/* 0x0808c448 game_quiz_sm: the quiz / 'Prof.Mad' Q&A built-in game state machine (2N). */
/* 0x080919bc game_findtarget_sm: the find/touch-the-target built-in game state machine (2N). */
/* 0x080a93f0 gme_launch_binary_build_sysapi: build the system_api function table and jump into an embedded GME ARM binary (2N-only feature). */
/* 0x080a9894 gme_read_main_binary_table: read the embedded main-binary descriptor from GME header offset 0xA8 (address,length). */
/* 0x080a9ee4 fwl_voice_play: play a firmware voice/prompt sample. */
/* 0x080aa004 fwl_voice_play_2: play a firmware voice sample (variant). */
/* 0x080aa090 play_media_setup: universal audio choke point — (file handle, offset, size) set up decode+playback of a media sample; all audio sources funnel here. */
/* 0x080aa198 play_media: play a media-table sample given (offset,size). */
/* 0x08031b7c aud_player_stop: stop the audio player. */
/* 0x08031c74 aud_player_stop_2: stop the audio player (variant). */
/* 0x080ed4cc akoid_init: allocate+zero the 0xdf0 AkOidPara OID context and set defaults (last_range/last_rand_media=0xff, first-oid tracking, etc.). */
/* 0x080ed7ec akoid_sensor_poll: poll the OID camera sensor and return a decoded OID code if present. */
/* 0x080edb2c akoid_sensor_config: configure the OID camera sensor. */
/* 0x080ee030 akoid_reg_write: write an OID-sensor hardware register. */
/* 0x080ee050 akoid_reg_read: read an OID-sensor hardware register. */
/* 0x080ed090 akoid_decode_frame: decode a captured camera frame into an OID code. */
/* 0x080316e8 map_gb0_to_codepage_sel: 20-entry ARM jump table mapping gb[0] (0..0x13) to a small code (default 0x17). Its return (in r0) is the argument to boot_read_serial_and_codepage -> codepage_get_header, i.e. a codepage/encoding selector. (Ghidra decomp of app_init_main drops this return->arg dataflow, making it look discarded - it is NOT.) */
/* 0x080ed6fc akoid_shift_out: bit-bang a byte MSB-first to the OID sensor over GPIO2=clock, GPIO9=data. */
/* 0x080ed04c str_compare: strcmp-style byte-string compare; returns 0 if equal, -1 on first mismatch. */
/* 0x080f1f60 i2c_write_bytes: GPIO bit-bang I2C write (SDA=pin 0xb, SCL=pin 0xc): init+start, send device + param_3 data bytes from param_2, stop; returns 1 if all ACKed. */
/* 0x080f1e0c i2c_send_byte: bit-bang one byte MSB-first on the I2C SDA/SCL GPIOs; returns the ACK bit. */
/* 0x080f1f08 i2c_gpio_init: configure the two I2C GPIO pins (SDA 0xb / SCL 0xc) direction+output registers. */
/* 0x080f1e94 i2c_start: I2C start condition on the bit-bang GPIOs (maybe start vs setup). */
/* 0x080f1db0 i2c_stop: I2C stop condition on the bit-bang GPIOs (maybe). */
/* 0x0800af74 gpio_pin_lookup: map a logical pin/channel id (switch) to a physical GPIO pin/bit via config tables. */
/* 0x080abef8 file_close_if_open: close the file handle if != -1; return 1 if closed else 0. */
/* 0x08009354 hang_forever: infinite do{}while(true) loop -- panic/hang. */
/* 0x08009320 wait_hw_bits_clear: busy-wait until the given bit(s) clear in HW reg 0x04000004 (maybe GPIO/IRQ ready). */
/* 0x080f288c list_is_empty: true if param_1 is null or param_1->next(+0xc) is null (maybe). */
int str_compare(char *a, char *b);
int i2c_write_bytes(int dev, unsigned char *buf, unsigned int len);
int file_close_if_open(int fh);
/* 0x0803a274 boot_read_serial_and_codepage: reads serial + loads codepage(codepage_sel). */
int boot_read_serial_and_codepage(int codepage_sel);
/* 0x080316e8 map_gb0_to_codepage_sel: gb[0] -> codepage selector (jump table). */
/* 0x08030e78 codepage_get_header: load codepage header (offset = codepage_sel<<5). */
int codepage_get_header(int codepage_sel);

/* ===== Audio decode path (Fwl_pfVoice -> aud_player -> MediaLib) =====
 * Full sound path from a GME binary:
 *   sysapi.play_sound (field +0x2c = 0x080b2198) -> play_media (0x080aa198) -> play_media_setup
 *   (0x080aa090) -> aud_player_reset(vol) + aud_player_set_source(...,codec) + aud_player_play.
 * aud_player_set_source calls medialib_open, which (when no codec is forced) calls
 * medialib_detect_codec to sniff the codec FROM THE STREAM HEADER. All file reads in this path
 * pass through the media XOR-decrypt read callback (fs_read_xor_decrypt 0x0800e024, media key
 * derived in gme_oid_dispatch). So a sample only plays if its bytes, AFTER XOR-decrypt, form a
 * header that detect_codec recognises AND map to a supported codec id; otherwise medialib_open
 * hits the "Unsupported" branch, set_source returns 0, and aud_player_play is silent (no error
 * is surfaced to the caller). */
/* 0x080b2198 sysapi_play_sound: system_api field +0x2c wrapper -> play_media. Args (filehandle, offset, size). */
/* 0x080fdd70 medialib_detect_codec: read first 0x40 bytes of the (decrypted) stream and sniff the
 *   codec id: WAV/AVI/ANKA via medialib_detect_wav, ASF/WMA via medialib_detect_asf, "#!AMR"->0x0d,
 *   OggS+0x7f"FLAC"->0x12(Ogg-FLAC), "fLaC"->0x11(FLAC), OggS+"vorbis"->0x14(Vorbis), plus mp3/id3
 *   probes. Returns 0 = unknown/unsupported. */
/* 0x080fd854 medialib_detect_wav: sniff a RIFF container. Header must be "RIFF" (or "ANKA") then a
 *   form id: "AVI "->2, "ANKA"->1, "WAVE"-> requires "fmt " chunk IMMEDIATELY after "WAVE"; then the
 *   fmt tag u16 decides codec: tag 0x11 (IMA-ADPCM) or 0x02 (MS-ADPCM) or ((~tag & 0x50)!=0) -> 3,
 *   else -> 10. Non-canonical WAV (chunk before "fmt ", or unexpected fmt tag) -> returns 0. */
/* 0x080fd9f0 medialib_detect_asf: sniff an ASF/WMA stream by its 16-byte GUID; returns codec 8. (tentative) */
/* 0x080fd588 stream_length: remaining/total length of the media stream (used to guard header reads). */
/* 0x080ef7cc medialib_open: alloc 0x1b4 MediaCtx, pick codec (forced field, else detect_codec), then
 *   switch on codec id installing the per-codec decoder vtable at +0x38.. :
 *     2/3/8/0x0a = WAV/PCM/IMA-ADPCM family, 0x0d = AMR, 0x11 = FLAC, 0x12 = Ogg-FLAC/Vorbis,
 *     0x14 = video; any other id -> "MediaLib: ERROR: Open: Unsupported" -> returns 0 (=> silence). */
/* 0x08032874 aud_player_set_source: build the media descriptor (filehandle/offset/size + fs callbacks),
 *   store codec id at +0xe9, and (unless codec==0x13) call medialib_open. If medialib_open returns 0,
 *   set_source returns 0 and NOTHING plays. IMA-ADPCM decoder = ima_adpcm_decode 0x0810d2b8. */
/* 0x08032450 aud_player_reset: latch playback params into the player object: +0x29,+0x2a, and +0x2b = volume. */

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

/* ---- Batch: functions resolved from pen runtime dumps + content-match vs 2N-Update3202 (2026-07) ----
 * Method: MT strings are ADR-referenced; each fn's referenced assert/__FILE__ strings were matched
 * against the byte-shifted sibling build fw/2N-Update3202 (same strings, different addresses) and
 * against MT's own "Foo():arg" self-asserts. Proven = self-named or >=2 shared distinctive strings. */
/* 0x08030558 Utl_Atoi(str): ASCII decimal string -> int (Eng_String lib). Self-asserts "Utl_Atoi()". Proven. */
int Utl_Atoi(unsigned char *str);
/* 0x080acba4 Utl_UStrRevFndChr(strMain,ch,?): reverse find of a wide char in a UCS-2 string. Proven (self-assert). */
int Utl_UStrRevFndChr(int strMain, unsigned int ch, int arg3);
/* 0x080acc80 Utl_UStrFnd(strMain,strSub,?): find UCS-2 substring. Proven (self-assert "Utl_UStrFnd(): strSub"). */
int Utl_UStrFnd(int strMain, int strSub, unsigned int arg3);
/* 0x080ace28 Utl_UStrRevFnd(strMain,strSub,?): reverse find of a UCS-2 substring. Proven (self-assert). */
int Utl_UStrRevFnd(int strMain, int strSub, unsigned int arg3);
/* 0x080acf9c Utl_UStrCatChr(dst,ch,?): append one wide char to a UCS-2 string. Proven (self-assert). */
int Utl_UStrCatChr(int dst, unsigned short ch, int arg3);
/* 0x080ad0b8 Utl_UStrCmpNC(a,b,n): case-insensitive compare of n UCS-2 chars. Proven (self-assert). */
unsigned int Utl_UStrCmpNC(unsigned short *a, unsigned short *b, unsigned int n);
/* 0x08041324 usb_report_speed: report/log the negotiated USB bus speed ("high speed!"/"full speed!").
   Content-match vs Update3202 usb_report_speed 0x08040438. Proven. */
/* 0x080421c8 medium_format(dev,bootsec,?,?): parse a partition/boot sector (FAT vs EXFAT via the "EXFAT"
   tag + BPB fields) and set up the medium's cluster/FAT geometry; handles PC-side reformat.
   Content-match vs Update3202 medium_format 0x080412ac (4 shared strings incl. "PC is formating the driver!").
   Proven. */
void medium_format(int dev, char *bootsec, char *arg3, unsigned int arg4);
/* 0x080433bc nandmtd_format(a,b,c): NAND MTD format / bad-block scan; asserts
   "NandMtd_Format: the tatal bad block is %d in MTD!". Content-match vs Update3202 nand_mtd 0x0804240c. Inferred. */
int nandmtd_format(int arg1, int arg2, unsigned int arg3);
/* 0x08043a74 nftl_core_rw_sector(nftl,sector,ctx): NFTL read/write of a logical sector with bad-block
   remap + retry (asserts "it fails to read sector :B:%x,P:%x,N:%d!"). Content-match vs Update3202
   nftl_core_rw_sector 0x08042ac0. Proven. */
unsigned int nftl_core_rw_sector(int nftl, unsigned int sector, int ctx);
/* 0x080aadd8 gme_read_main_binary_table: read the GME's embedded main-binary descriptor (firmware load
   address + file length; logs "Firmware address = %x,File len=%d."). Feeds gme_launch_binary_build_sysapi.
   Content-match vs Update3202 gme_read_main_binary_table 0x080a9894. Proven. */
void gme_read_main_binary_table(void);
/* 0x080ab22c fwl_voice_need_repeat: decide whether the current voice sample must be repeated
   (logs "VoiceNumberNeedRep = %d."). Content-match vs Update3202 fwl_voice_need_repeat 0x080a9c10. Proven. */
void fwl_voice_need_repeat(void);
/* 0x080ae65c aud_list_set_need_update(list): mark the audio/music list dirty so it is rebuilt; asserts
   "Aud_listSetNeedUpdate" (log_aud_musiclist.c). Proven (self-assert). */
unsigned int aud_list_set_need_update(void *list);
/* 0x08105084 pr_p_burn_mode: producer/flashing "PR_P" partition burn-mode selector ("PR_P burn mode:%d").
   Content-match vs Update3202 pr_p_burn_mode 0x08103a38. Proven. */
/* 0x08105118 pr_p_write_maps: producer "PR_P" writer for the partition bin-info/zone/map/config records
   ("PR_P write bin info/zone info/map/config success"). Content-match vs Update3202 0x08103acc. Proven. */
/* 0x08106844 pr_b_compare: producer "PR_B" boot-block verify/compare ("PR_B Compare fail").
   Content-match vs Update3202 pr_b_compare 0x081051e8. Proven. */
/* 0x08106a48 pr_b_read_bin(a,b,..): producer "PR_B" read/remap of a boot bin block
   ("PR_B Read bin fail", "%d_%02x->%02x "). Content-match vs Update3202 pr_b_read_bin 0x081053e8. Proven. */
/* 0x08106bd8 pr_b_map_index: producer "PR_B" boot-block map-index lookup ("PR_B map index:%d pos err").
   Content-match vs Update3202 pr_b_map_index 0x08105578. Proven. */

/* ---- Audio-player / DAC-ring construction + statechart handlers (RE 2026-07-03, ttrun-gme.md HANDOFF
 * 2026-07-03g/h; hw-io-oid-and-dac.md). All PROG-range (base 0x08009000), verified against the decomp. */
/* 0x080ab47c aud_player_construct: lazy audio-player constructor (FUN_080ab47c). If not already built
   (flag &0x40), dac_bringup(0x08033334) + aud_player_alloc(0x08032320), store g_pAudPlayer at obj+8,
   then aud_player_reset(volume). Only fires inside the audio-playing sub-state. Proven. */
/* 0x08032320 aud_player_alloc: allocate+zero the 0xec audio-player object, seed the static DAC ring
   descriptor (DAT_080323d0..), and build the SDRAM audio-out ring via audio_ring_setup. Proven. */
/* 0x080223a8 audio_ring_setup: reorder the DAC-descriptor fields into the ring-build argument frame and
   call audio_ring_build. Proven. */
/* 0x08022aa0 audio_ring_build: build the SDRAM audio-output ring singleton (*0x08008d2c): memset 0x4c,
   copy the descriptor, set state 0x3000, alloc the PCM buffer via the descriptor callback -> ring[+0x44]
   (frees + returns 0 on alloc failure). Proven. */
/* 0x08022c38 ao_mix_write_ring: volume-scale decoded PCM (sample*ring[+0x14]>>10, Q10) into the audio-out
   ring; mono sources (ring[+0x48]!=2) duplicated to L/R -> ring is always stereo-interleaved S16.
   Handles the ring wrap. The resampler/mixer choke point feeding the DAC ring. Proven. */
/* 0x08022f0c medialib_open_ring_check: audio-output ring/state control (cmd 0..6 set the ring state bytes
   obj+0x20/+0x24/+0x25/+0x26/+0x34); returns 0 if the AO ring (*0x08008d2c) is not yet built -> the first
   gate medialib_open checks before opening a decoder. Proven. */
/* 0x0804eb08 book_mode_handler: statechart state-13 (book / OID-listening) event handler. Switches on
   0x10xx events, mapping each to an action id; the content/product event resolves to result 0x11 ->
   event-action-table[17] = gme_oid_dispatch (the OID/book mount+play dispatch). Proven. */
/* 0x08050c28 state8_entry: statechart state-8 (USB-charger / boot-settle) entry handler. Resets the
   settle counter (*0x081da078) and arms the periodic 0x30 settle-timer via the nandboot hal_timer_register
   (0x0800780c), storing the returned slot handle at *0x08121e64. Proven. */

/* ---- Runtime pointer-table findings from the pen live-memory dumps (~/tiptoi/m*.bin) ----
 * Ground-truth: 5 dumps (m08128000/m081c8000/m081d0000/m081e0000/m081e8000) are byte-identical to
 * PROG.bin (pure XIP flash, no runtime state). Runtime state lives in m08007000 (RAM, 6% flash-match),
 * m0811e000/m08120000 (57-72%) and m081d8000 (97%). Resolved tables:
 *  - 0x0812ad44: 70-entry pointer array -> 0x10-byte records at 0x080b9378.. (GB2312 label text, e.g.
 *    0x661f 0x671f = "星期"/weekday). A localized menu/label pointer table, NOT a statechart table.
 *  - 0x0812b468: 163-entry array, STATIC=0 (runtime-populated), all entries point within fs\fat.c fn
 *    0x080f6304 (+0x46..+0x5c) -> a FAT runtime dispatch/handle table built by the FAT layer.
 *  - 0x080148d4 (in the RAM dump): 68 code pointers into 61 distinct game/statechart handler fns
 *    (game_start, game_wronglimit_sm, gametype_handler_*, study_return_handler, game2_mini_init,
 *    special_game2_handler, ...) - a live handler/return-address region, confirms those fns are the
 *    active book/game state machine. */

/* ---- Statechart 8->13 + product-mount + a:/ discovery (RE 2026-07-03/04; state8-to-13-transition.md,
 * product-init-and-runtime-tables.md, ttrun-gme.md HANDOFF). PROG-range (base 0x08009000), each verified
 * against the decomp. Note: the switch-product call (doc "0x08036508") and first-load poster site
 * (doc "0x0803660c") are call SITES INSIDE gme_oid_dispatch, not function entries, so they are not named;
 * the first-load "poster" is fwl_play_voice_by_id 0x080ab9ac (already named). */
/* 0x08037cec cover_oid_classifier: state-9 transition-action[5] classifier. Reads the tapped OID
   (akoid_buf+4) and tests it against the per-slot cover/product-OID families (0xc00/0xa00/0x1600/0xec4..,
   slot = *DAT_080381a4); on a cover/first-page match sets the cover-accept flag (ctx+0x4a4) and page
   counters (ctx+0x155/+0x156), else returns 0 to propagate to book_mode_handler. First-load classifier. */
/* 0x08034300 booklist_scan_bnl: statechart state-12 (mount) entry. Resets the product/OID context, then
   scans B:/ (DAT_08034534="B:/") for *.bnl (DAT_08034538="*.bnl") via FUN_080ad7c0 to build the retail
   book list; on empty sets ctx+0x38=0xff / ctx+0x14=1. The only udisk book-enumeration site. */
/* 0x08051d60 soft_reboot: teardown/soft-restart. Tears down statechart states 9/12/13 (func_0x0800774c +
   FUN_080f3268), re-inits the timer subsystem, clears the interrupt/PMU regs 0x0400004c/0x040000cc/
   0x04000038/0x04000034, then halts to restart. Reached from the book/standby teardown path. */
/* 0x080f013c udisk_gme_discovery: a:/*.gme discovery scan entry (= old FUN_080e713c). Sets up the scan
   config (bounds/positions in the struct at param_1) and calls udisk_scan_iter_init; the enumeration
   writes a:/oidfilelist.lst, read back to build the product list. The clean standby-entry discovery path. */
/* 0x080f0048 udisk_scan_iter_init: scan-iterator/cursor constructor used by udisk_gme_discovery (= old
   FUN_080e7048). Allocates the enumeration context (param+6 via FUN_080ae23c/FUN_080ae2c0=FUN_080a52c0)
   and computes the iteration bounds (count param+1, cur index param+2/+3). */
/* 0x080af6dc udisk_dir_scan_recurse: the real recursive a:/ directory scanner (= old FUN_080a66dc).
   opendir (FUN_080fb8ac) then readdir loop (FUN_080f3fb8): for each file (dirent attr+0x18 & 0x10 == 0)
   append the name (FUN_0803089c) and write a 0x214-byte record via FUN_080ad4e4 to a:/oidfilelist.lst
   ("write music list failed" on short write); for each subdir (skipping "."/"..") recurse into itself;
   closedir (FUN_080fbb4c). The file-handler code is at 0x080af834. */

/* ---- Audio/DAC/OID HAL (PROG side; RE 2026-07-03, hw-io-oid-and-dac.md tables). All Proven, base
 * 0x08009000, verified as function entries. (The nandboot-side HAL <0x08009000 is named in the BOOTROM
 * dict in tools/ghidra_rename.py, not here.) */
/* 0x080eef54 akoid_poll_wrapper_thunk: thunk of the OID poll wrapper 0x080eef10. */
/* 0x080eef68 akoid_rearm: re-arm the OID sensor for the next frame (akoid_shift_out(0x56); clear done-latch). */
/* 0x080aaf70 gme_oid_dispatch_alt: sibling OID-gate handler for another game mode (twin of gme_oid_dispatch). */
/* 0x0800b4a4 event_pump_ring: drain the active-object event ring 0x08008898 into sm_dispatch_to_hierarchy. */
/* 0x0802220c ao_pull_decoder: pull decoded PCM from the codec into scratch via the decoder callback obj+0x74. */
/* 0x0800be78 mp_pump_state1: media-player pump (state 1) - decode a chunk then feed ao_mix_write_ring. */
/* 0x08022c14 ao_set_fmt: store the audio-out channel count/bits at the ring object +0x48/+0x4a. */
/* 0x080324d0 aud_set_format: read the stream header (medialib_get_info) -> set the audio format + dac_set_rate. */
/* 0x08033258 dac_set_rate: program the internal DAC sample-rate divider (regs 0x04000008 / 0x04000064). */
/* 0x08033334 dac_bringup: enable the internal DAC (0x04080000 |= 1) + its clocks; default 8000 Hz. */
/* 0x08038ce4 physpool_alloc: allocate a block from the physical (SDRAM) memory pool (e.g. the audio-out ring). */
/* 0x08038be0 physpool_free: free a physical-pool block allocated by physpool_alloc. */

/* ---- NAND/NFTL storage stack + upd/profile + init-gaps + GME timer/counter/random
 * (RE 2026-07-04: nftl-resolver-for-A.md, nand-write-path.md, audio-player-construction.md,
 * upd-system-partition-layout.md, from-entry-init-gaps.md, gme-timer-counter-random.md).
 * All Proven, unified base 0x08009000; verified as function entries against out/decomp_named/. */

/* 0x0800ea38 nftl_partition_read: partition->medium read wrapper (partition vtable +0x10, set by nand_device_create). desc=*(part+0x20); if sector >= part[0xc] bound it CLAMPS sector to desc[1]=0xffffffff, then calls the medium read op (**(*desc+0x10)). The out-of-range clamp that (with an undersized A:) feeds the NFTL RLE underflow. */
/* 0x0800f3ac nftl_read_resolve: NFTL logical->physical READ resolver. Finds the 16-entry window (part+0x48+i*2), reads the physical rel-block from the dense map (part+0x8c); on no window match the index stays 0 (silent mis-resolve). Then walks the RLE (+0x90) via nftl_rle_total/nftl_rle_find_run: run==0xff -> insert, else compact memmove len=((rle_total-base)-run)*2 (underflows on a garbage index). */
/* 0x08047510 nftl_build_log2phy: invert the per-block spare-tag [4:6] field into the dense log2phy map: map[tag[4:6] & 0x7fff] = relative_block (logical->physical). Called at the end of mtd_init's block scan. */
/* 0x080438a4 nftl_rle_compact: NFTL log2phy RLE-map compaction (NOT a wide-string util - the *2 is because entries are u16 shorts). Called from nftl_core_rw_sector; same memmove pattern as nftl_read_resolve. */
/* 0x0800f020 nftl_rle_find_run: walk the RLE region (part+0x90) for the run whose base short == target, accumulating base; returns &run byte. */
/* 0x0800efc0 nftl_rle_total: sum of the RLE run-length bytes (part[0x90]+0x27f downward until 0xff); 0 when the RLE is empty. */
/* 0x0803be74 nftl_build_fs_medium: build the FS-area medium over [fs_start, total), split into 0x2000-block groups; per group calls mtd_init(span=min(0x2000,remaining), reserved=fakemap[g]*ngroups). */
/* 0x0803aa3c nftl_zonetab_addrcnt: zone-table AddrCnt lookup by Symbol (0='A',1='B'); returns the partition sector count word[+4] (LE, raw). */
/* 0x0803a84c nand_device_create: NAND device-object (0x08008cc4) constructor + vtable seeder: seeds dev+0x20=nand_program_page_leaf, +0x28=nand_program_page_tag_leaf, +0x2c=nand_read_page_tag_leaf, +0x3c=nand_erase_block_leaf (and +0x24/+0x30/+0x34/+0x38); sizes it (wear flag 0x10000000 / 0x800), returns the device obj. */
/* 0x0800fcc4 nand_program_page_leaf: device write-page leaf (dev+0x20). row=dev[0x14]*block+page; 0x081226f8 gap-skip; stages data, toggles HW-ECC, calls nandboot nb_nand_program_page (0x08002044); returns bool ok. */
/* 0x0800f92c nand_program_page_tag_leaf: device write page+tag leaf (dev+0x28); single-shot page+8B-tag program -> nb_nand_program_page. Metadata/info-page writes. */
/* 0x0800f9cc nand_read_page_tag_leaf: device read data+tag leaf (dev+0x2c) -> nandboot nb_nand_read_page (0x0800260c). Consumed by mtd_init's block scan (corrects the nftl-layout +0x28/+0x2c swap). */
/* 0x0801027c nftl_write_op: NFTL per-sector write op (nftl+0x24c): resolver (nftl+0x26c)->phys (block,page), FUN_0800feac die-split, calls the dev+0x20 program leaf. */
/* 0x0800f5f4 nftl_rw_multi_sector: multi-sector R/W helper; loops sectors calling the per-sector +0x250/+600 ops -> nftl_core_rw_sector. */
/* 0x08044110 mtd_rw_sector_dispatch: MtdLib sector R/W dispatch -> nftl_core_rw_sector; read twin passes ctx=0xffff, write twin (b 0x08044218) passes ctx=data buffer (!=0xffff). */
/* 0x0803d76c nand_erase_block_leaf: device erase leaf (dev+0x3c): row=dev[0xc]*dev[0x10]*block+page, calls nandboot nb_nand_erase_block (0x080007d8), then sets the bad-block bitmap bit. */
/* 0x080345cc book_state_entry: statechart state-13 (book mode) ENTRY hook (descTbl[13][+0]). Calls aud_player_construct() unconditionally - the sole autonomous g_pAudPlayer @0x081db228 constructor; first-ever entry plays the "book open" prompt voice 0x13. */
/* 0x0803440c state12_default_action: statechart state-12 (mount) default action; posts event 0x1059 to advance to state 13 (book), returns an event-in-range predicate. */
/* 0x0804b8cc profile_write_record: create A:/SYSTEM/ + profile.dat (UTF-16 @0x080affd4) on demand; if missing or size != 0x21B0, mkdirs + writes the 0x21B0 default profile from profile_init_defaults. */
/* 0x0804bb20 profile_read_record: reader twin of profile_write_record (A:/SYSTEM/profile.dat, 0x21B0 bytes). */
/* 0x080391e4 profile_init_defaults: fill the 0x21B0-byte default profile record (defaults incl. the UTF-16 B:/ scan path at profile+0xD4C). */
/* 0x080511a0 standby_entry: statechart standby entry; sets game_ctx[0x1d]=2 (fresh-standby marker) - the primary setter. */
/* 0x0803cf7c usb_present_query: query USB/charger cable present (returns FUN_0803ced8()==1; 0 on battery) and configures the USB-detect GPIO/MMIO. splash_entry uses it to set game_ctx[0x1e]. */
/* 0x08031aac codepage_convert_devpath: codepage device-path converter (FUN_080318e0->FUN_0803193c); converts the static "B:"/"A:" scan root into the runtime drive path (needs codepage_load, else garbles). */
/* 0x080ae2c0 discovery_ctx_build: build the udisk-discovery scan context; selects the .lst path table + scan root ("B:", DAT_080ae3d0->0x08122708) and calls discovery_ctx_set_root. */
/* 0x080af960 discovery_ctx_set_root: append '/' (0x2f) to the scan root at ctx+8 (the drive path consumed by discovery_scan_wrapper). */
/* 0x0804c1d4 splash_entry: statechart splash (state 1) entry. Sets game_ctx[0x1e]=usb_present_query() when 0, game_ctx[0x1d]=2; in the B:/FLAG.bin update-resume branch calls aud_player_construct + resume/battery-low voices. */
/* 0x080afa80 discovery_scan_wrapper: udisk scan wrapper; builds the scan path from *(ctx+8), scans (FUN_0803089c copies the root), then overwrites the drive letter to 'A' (0x41) for the 2nd pass. */
/* 0x08030c88 sys_get_tick: return the system tick *(0x08008d20+4)=*0x08008d24 (incremented per timer IRQ by nandboot timer_dispatch). Firmware-wide time API (~90 callers); entropy for GME opcode 0xFF00 (reg=tick%(m+1)); field 0 of the embedded-binary system_api struct. */
/* 0x0804fda8 sys_get_heartbeat_count: return *(0x081da010+4)=*0x081da014, the free-running heartbeat counter (++ per event 0x1046 = per OID-poll timer period, in heartbeat_1046_handler). Entropy for gme_rand_in_range (opcode 0xFC00). */
/* 0x0800acfc heartbeat_1046_handler: statechart state-9 transition-action[0], bound to event 0x1046. Increments the heartbeat counter *0x081da014, toggles the audio amp while is_audio_playing, returns 1. */
/* 0x080ac5d8 rng_seed_once: LCG seed = sys_get_tick() * sys_get_heartbeat_count() into *0x081db900. Called lazily from rng_below (seed-once flag *0x081226f4). */
/* 0x0805c0f0 game_binary_launch: build the system_api struct on the stack (sys_get_tick value first field, GME timer handle 0x08121ecc), gme_alloc_binary_region + read the embedded game binary, then jump to it. */

/* ---- GME script-engine functions named 2026-07-20 (name+signature campaign) ---- */
/* 0x0803454c gme_reset_media_cache: reset the current script line's resolved-media caches -- fill the
   32-entry per-playlist-entry media-offset array (p_pl_media_off, init 0xffffffff) and media-size array
   (p_pl_media_size, init 0), and clear two script-state flag bytes. Called by gme_oid_dispatch before
   resolving a new line's playlist media. */
/* 0x08034900 sm_ao_all_rings_idle: returns 1 iff NO event is pending in ANY of the ~40 active-object
   event rings it polls (sm_ao_event_in_ring over the DAT_08034b58.. ring-pointer table); returns 0 as
   soon as one ring is non-empty. The "engine fully drained" gate used to defer GME work (jump / queued
   plays) until audio + all AO queues are idle. */

/* Docstring updates for already-named GME interpreter functions (unified-base addresses). */
/* 0x08034da0 gme_exec_command: GME action opcode dispatcher (reg_idx,opcode,is_literal,operand); regs @0x081da350. FFF0..FFF9 arith, FFE8/FFE0/FFE1/FB00/FC00 play, FE00 arm PERIODIC sw-timer (hal_timer_register(0,m*100,autoreload=1,cb=0x08003994), handle @0x08121ecc), FEFF cancel, FF00 reg=tick%(m+1) ("Rand(%d)=%d"), FC00 via gme_rand_in_range (pick -> prand_last_idx), FFE0 P* pick = evt_dispatch_count%playlist_len -> pstar_last_idx, FEE0..7 sm_set_sound_profile(0..7). FFA1 @0x080352cc = COIN-FLIP PLAY: always clears ffa1_did_play, then iff (evt_dispatch_count&1)==0 plays playlist[operand] IMMEDIATELY (falls into the FFE8 play tail @0x08035288, full u16 operand, no bounds check) and latches ffa1_did_play=1 + ffa1_play_idx=operand&0xff for REPLAY walks; odd count = complete no-op. Proven by asm + tt-emu parity experiment 2026-07-20. Quirks: FE00/FF00/FFA1 use the raw operand (is_literal not checked). */
/* 0x08034d74 gme_rand_in_range: (sys_get_heartbeat_count() % (hi-lo+1)) + lo; playlist pick for opcode 0xFC00 (Random a b -> [b..a]). */
/* 0x08035624 gme_check_condition: evaluate one conditional (FFF9 Eq / FFFA Gt / FFFB Lt / FFFD GEq / FFFE LEq / FFFF NEq) over the regfile @0x081da350; returns 0/1. */
/* 0x08036228 gme_parse_additional_script: load the line-offset table of the GME additional-script (header +0x0C, the timer script); evaluated by gme_oid_dispatch on each GME-timer firing (event 0x30, args[0]==*0x08121ecc): the first line whose conditions hold executes. */
/* 0x0803629c gme_oid_dispatch: book(13) default event handler (EA[17]) - receives EVERY event routed to book mode and FIRST increments akoidpara->evt_dispatch_count (sole writer; the GME engine's entropy: P* modulo pick, FFA1 parity gate). On a tap (0x1060): mount/switch product, derive the media XOR key, look up the OID's play script, run the matched line: gme_parse_actions decodes <=8 actions into p_actq_*; arithmetic/FF00 run immediately, the rest are queued in p_actq_pending_order and consumed ONE PER AUDIO BOUNDARY on later dispatches (heartbeat 0x1046 ~10/s drives the polling) - actq_walk_mode 1 EXECs each via gme_exec_command; mode 2 (and the game-launch/voice-stop resume paths) REPLAYS instead: P*->pstar_last_idx, P(b-a)->prand_last_idx, FFA1-> play ffa1_play_idx iff ffa1_did_play (no re-roll). Also consumes event 0x30 for the GME timer (handle @0x08121ecc): when idle re-evaluates the additional-script table; executes deferred Jump (jump_flag/jump_target) once audio stops. */

/* Resident nandboot NAND-op leaves + timer/rng helpers (ghidra addr = runtime - 0x8000). */
/* 0x07ffa044 nb_nand_program_page: PAGE-PROGRAM (rt 0x08002044): copy caller data -> NFC staging SRAM 0x08006800, cmd 0x63, program-confirm + status poll (NAND FAIL bit). Was mislabeled NB_READ_DTAG. */
/* 0x07ffa60c nb_nand_read_page: READ data (rt 0x0800260c): SRAM 0x08006800 -> caller, cmd 0x64 (program twin). */
/* 0x07ffaaf8 nb_nand_block_erase: NAND 0x60/0xD0 BLOCK ERASE (rt 0x08002af8); status fail-bit retry <=16 ("NF:12/13"). CORRECTED: previously mislabeled nb_nand_read_oob/NB_READ_OOB - there is no OOB-read primitive; spare/tag reads go through nb_nand_read_page (0x0800260c) with the data descriptor r3=0. */
/* 0x07ffaefc nb_nand_status: status-read op (rt 0x08002efc): NFC cmd 0x70, [0x0404A000+0x150]&0xff = NAND status byte. */
/* 0x07ffadb4 nb_nand_ready: ready poll (rt 0x08002db4). */
/* 0x07fff7d8 nb_erase_region: region-erase wrapper (rt 0x080007d8): calls nb_nand_block_erase per block plus bad-block bitmap bookkeeping. CORRECTED: previously nb_nand_erase_block - the actual erase primitive is 0x08002af8. */
/* 0x07ffac18 nb_nand_probe_id: reset + 4x READ-ID vs the static flash-config id @0x0800003c (0x9551D3EC = Samsung K9GAG08U0M) (rt 0x08002c18); programs TIMING0/1, fills dev/state structs, sets 3 row cycles, derives the randomizer flag (OFF here). Nandboot boot path only. */
/* 0x07ff8c40 FHA_get_maplist(name,map,max,bBackup): resident FHA "get maplist" (rt 0x08000c40); returns the per-cluster {u16 origin_physical, u16 backup_physical} block map for a named area (e.g. "codepage"->{36..42}); takes origin, falls to backup if origin's ASA bad-block bit is set (both bad -> fatal). max 8 clusters. */
/* 0x07ff89c8 nb_nftl_load_info: nandboot FHA header/entry-table reader (rt 0x080009c8): reads FHA rows dev[0x14]-2 (=254 header) and -3 (=253 entry table) via nb_read_cp (0x08002a38), name-matches the entry, reads its maplist row; returns the entry's size-in-sectors. DISTINCT from PROG nftl_load_info 0x0804855c. */
/* 0x07ffdbe8 hal_timer_unregister: free a sw-timer slot (table @ rt 0x0800895c), disables the timer IRQ source if no slot of that class remains, returns 0xff (rt 0x08005be8). Used by the GME Timer opcode 0xFE00 cancel path. */
/* 0x07ffb1d0 rt_udiv: ADS __rt_udiv (rt 0x080031d0): r0=divisor, r1=dividend -> quotient r0 / remainder r1. Used by GME Div/Mod (FFF3/FFF4), FF00, gme_rand_in_range, hal_timer_register period scaling. */
/* 0x07ff81fc lcg_rand_below: ARM-ADS LCG (rt 0x080001fc): *state = *state*0x91E6D6A5 + 0x91E6D6A5; return (*state * n) >> 32, uniform [0,n). Game-side RNG (rng_below wrapper); NOT used by the GME interpreter. */

/* ===== Harvest #3 (RE 2026-07-05: statechart-full-map / oid-sensor-read-protocol / nfc-controller-
 * registers / pmu-power-management / nftl-zone-build / nb-read-data-conventions / partition-a-fat-vs-mbr /
 * fatvol-medium-layering / codepage-language-selector / autonomous-mount-state8). Unified base 0x08009000;
 * each PROG address verified as a real function entry against out/decomp_named/; each nandboot address
 * verified by ARM prologue. ===== */

/* OID capture state shared by the nandboot OID HAL and PROG polls (@0x08008c08); raw_word lives at the
 * absolute address 0x08008C14, sensor_type at 0x08008C18. */
struct OidCaptureState {
    unsigned char  frame_ready;   /* +0x00 set by hal_oid_shift_in after a full frame */
    unsigned char  bit_count;     /* +0x01 bits to clock: 23 (standby decode) or 0x20=32 (PROG polls) */
    unsigned char  decode_valid;  /* +0x02 set by validate: akoid_buf+8 holds 0x400000|index */
    unsigned char  unk3;          /* +0x03 cleared by akoid_init */
    unsigned char  unk4;          /* +0x04 cleared by akoid_init */
    unsigned char  unk5;          /* +0x05 cleared by akoid_init */
    unsigned char  unk6;          /* +0x06 cleared by akoid_init */
    unsigned char  done_latch;    /* +0x07 1 = sensor asleep; polls skipped until akoid_rearm() */
    unsigned int   timer_handle;  /* +0x08 standby poll soft-timer id (0xff = none) */
    unsigned int   raw_word;      /* +0x0C last shifted frame, MSB-first (abs 0x08008C14) */
    unsigned char  sensor_type;   /* +0x10 0 = Sonix two-wire (this pen); 1/3 = alt I2C sensors (dormant) */
};

/* ---- CORRECTIONS applied this harvest ---- */
/* 0x0800b65c sm_set_sound_profile(level,mode): NOT the statechart/AO state setter. Clamps level to [0,6],
   indexes the u16 gain/rate table @0x0800b9e4 {0x18,0x60,0xA8,0x108,0x138,0x180,0}, calls FUN_0802265c
   (sample-rate math, clamp <=0x400), stores the level byte @0x081db904. 7-level; driven by GME opcodes
   FEE0..FEE7. app_init_main's sm_set_sound_profile(3,0) = "set sound profile 3", NOT "initial leaf=standby".
   (was mis-named sm_set_state.) */
/* 0x080ee6e4 file_checksum_verify(a,b): reads sectors of a file by name (via NAND read op 0x08002a38) and
   verifies a trailing 32-bit byte-sum. Nothing OID-related. (was mis-named akoid_decode_frame.) */
/* Statechart correction: the active-object (AO) control block is @0x08008874. Current leaf id = *(AO+0x14)
   (state-stack ptr); AO+4 = stack DEPTH (u16); AO+0x18/0x1c/0x20 = descriptor/event-action/transition-action
   table ptrs. Real tables: descriptor @0x08121d44, event-action @0x0800b8d4, transition-action @0x0800b544.
   The earlier StateChart @0x080108e8 / desc-table 0x08128650 reading is superseded. */

/* ---- Statechart accessors + heartbeat (docstring refresh) ---- */
/* 0x0804fd84 sm_get_event_action_table(): returns the event-action table ptr (status-0 targets, indexed by sm_dispatch_event). */
/* 0x0804fd8c sm_get_transition_action_table(): returns the transition-action table ptr (status-1 targets; only state 9 emits status 1). */
/* 0x0804fd94 sm_get_state_descriptor_table(): returns the 70-entry state->descriptor table ptr {+0 entry,+4 exit,+8 unhandled,+0xc mapper}. */

/* ---- OID sensor HAL (PROG side) ---- */
/* 0x080eed50 akoid_cmd_write: bit-bang an 8-bit command MSB-first to the OID sensor over GPIO2=clock/GPIO9=data, data valid on falling edge; bus idle at end. (was akoid_shift_out.) */
/* 0x080eedfc akoid_sensor_sleep: send 0xA0,0xAC,0xA6 (100-unit gaps) and set the done-latch; sensor power-down handshake. */
/* 0x080eee40 akoid_poll_status32: 32-bit poll loop (<=20 frames): verify valid bit + check byte, store raw>>9 -> akoid_buf+4; status 0x60FFF8/0x60FFF1 -> akoid_sensor_sleep. Standby/splash only. (was akoid_sensor_poll.) */
/* 0x080eef84 akoid_poll_status32_once: single-frame variant of akoid_poll_status32. */
/* 0x080eef10 akoid_poll_trigger: if done-latch clear, ~100 ms GPIO2-high trigger pulse then IRQs-off akoid_poll_status32. */
/* 0x080eef58 akoid_poll_from_idle: hal_oid_bus_idle() then akoid_poll_trigger(). */
/* 0x080ef114 oid_sensor_power_on: 0x04000058 bits[25:24]:=01; sensor_type:=0 (Sonix two-wire). (was oid_sensor_enable.) */
/* 0x080ef180 akoid_sensor_config_i2c: I2C(dev 0x94) register setup for alternate sensor types 1/3; dormant on this pen (type==0). (was akoid_sensor_config.) */

/* ---- PROG raw NAND/NFC ops (producer/direct path) ---- */
/* 0x080ee224 fw_nand_program_page_direct: producer info/page writer (0x1d8+0x28 info pages) driving the NFC/ECC registers directly. */
/* 0x080ee468 fw_nand_read_page_direct: raw 514-B/sector (512+2) page read; waits L2 buffer level==8. */
/* 0x080edc44 fw_nand_erase_blocks_direct: direct multi-block erase. */
/* 0x0803d868 dev_copyback_leaf: NAND device copyback leaf -> nandboot nb_nand_copyback. */
/* 0x08030da4 fha_read_row: main-fw raw physical data read (sole caller: codepage_load); row = dev[0x14]*block+page, 0x200-byte sectors, ecc mode from geometry; no NFTL resolver (raw by design). */

/* ---- Power / battery / USB-PHY (PMU doc) ---- */
/* 0x0800b3c8 battery_adc_read: set 0x04000064 bit9, average 4x of 0x04000070[19:10], return x2 (scaled raw). System-ctrl ADC (NOT PMU/USB). */
/* 0x0800b174 battery_ring_push: push a sample into the 4-entry ring @0x081db928. */
/* 0x0800b15c battery_voltage_mv: average the ring (drop min/max), scale to mV via calibration bytes. */
/* 0x0800ada4 battery_level_bin8: map mV -> battery level 1..8 (Li-ion breakpoints); charge-screen animation. */
/* 0x080afd78 battery_monitor_tick: per-0x1046 monitor; <0x300 x3 -> warn (b9=1), <0x2C0 x10 -> final (b9=2); stage @0x08008c0b, flag 0x081da086 bit4; reads battery_adc_read. */
/* 0x0800b464 battery_meas_enable: toggle 0x04000004 bit15 around ADC/full-speed bursts (measurement/load enable). */
/* 0x0800a9c8 button_dispatch_105f: 0x105F codes: 5=vol+, 6=vol-, 8/1=power button -> voice 0x14 -> post 0x1062 -> off. Vol idx @0x081db904, gains @0x0800b9e4. */
/* 0x0800b6b4 volume_up: volume-up (index @0x081db904 0..6, DAC gain table 0x0800b9e4). */
/* 0x0800b6e0 volume_down: volume-down (same index/gain table). */
/* 0x0800b6a4 volume_get: read the current volume index. */
/* 0x080508f8 power_off: teardown + power-hold GPIO15=0 + spin-until-dead. (was sys_reset - it is a shutdown, not a CPU reset.) */
/* 0x08050a8c power_off_or_charge_park: repeated GPIO15=0; survives only while VBUS present (off-but-charging park loop). (was charge_usb_state_machine.) */
/* 0x08050864 poweroff_prep_action: state-7 (poweroff-prep) default action; re-posts 0x1062 (-> state 10 -> system_off). */
/* 0x08041ce4 usb_wait_host_sof: MUSB POWER(0x04070001)=0x20, poll INTRUSB(0x0407000A) SOF <=~0xC3500 iters, clear PHY 0x04070348 bit0; returns 1 = host alive. USB controller 0x04070000, NOT PMU. */
/* 0x080413fc usb_phy_off: MUSB POWER(0x04070001)=0 + sysctrl PHY teardown. */
/* 0x08050bc4 usb_phy_arm_detect: MUSB POWER(0x04070001)=0x21, 0x04070348&=~1, sysctrl 0x04000058|=4 (base *0x08050c10=0x04070000). (was usb_detect_2.) */
/* 0x0803ced8 usb_vbus_present: read(GPIO8)==1 via 0x040000BC; VBUS/charger detect. */
/* 0x080abbc8 pwr_path_usb: run from USB - GPIO6=1, wait, GPIO15=0 (release battery latch); flags @0x08008c01. */
/* 0x080abc34 pwr_path_battery: back on battery latch - GPIO15=1, GPIO6=0 (reverses pwr_path_usb). */
/* 0x0804c47c authchip_challenge_gpio5_10: anti-clone bit-bang challenge/response on GPIO5/10; fail -> akoid_buf[0xb4]=1 -> power-off kill. */
/* 0x08043190 nand_update_write_fail_hang: NAND write-failure handler ("system is low power, please change power" is a vendor misnomer, not a real low-power path). */

/* ---- Codepage / NLS (codepage + fatvol docs) ---- */
/* 0x0803a9f0 nls_compute_shifts: derive the codepage sector/cluster shifts (NLS state +2/+4) from the NAND geometry @0x08008cc4 (page shift = log2(min(page,0x1000)); cluster shift). */
/* 0x080318e0 codepage_get_sel: read G0 (@0x080089a4) and re-run the same selector switch as map_gb0_to_codepage_sel; returns the sel for gating (does not reload the header). */
/* 0x08031044 codepage_table_lookup_a: single-byte (table A) codepage lookup; reads u16 at A+idx*2 with A=*(state+0x20), 512-B cache @0x080089e0. */
/* 0x0803136c codepage_convert_char: per-char codepage conversion via the A/B/C table lookups. */
/* 0x080310c4 codepage_ready_flag: return byte 0 (READY flag) of the NLS state struct @0x081db730. */
/* 0x0803193c codepage_convert_string: MultiByteToWideChar analog; ready-flag 0 -> zero-extend copy, flag 1 -> real per-char conversion. */
/* 0x08031630 codepage_dbcs_convert: DBCS conversion core; probes the lead-byte table B then falls to codepage_table_lookup_a. */
/* 0x08031a70 codepage_wc_to_mb: reverse WideChar->MultiByte conversion entry (calls codepage_wc_to_mb_worker); uses the cached header via codepage_get_sel. */
/* 0x080ac4a4 codepage_wc_to_mb_worker: WideChar->MultiByte conversion worker. */

/* ---- FAT / NFTL / storage (nftl-zone-build, partition-a-fat-vs-mbr, fatvol docs) ---- */
/* 0x08039ddc fatvol_construct: FatLib volume ctor (0x64 B, type 10); stores the partition obj @+0x08, base LBA @+0x4c (0 = superfloppy). */
/* 0x0803b078 fat_bpb_parse: BPB parser; requires bytes/sector==0x200; cluster-count <0xff5 reject / 0xff5..0xfff4 FAT16 / >=0xfff5 FAT32; builds the FAT object. */
/* 0x08039d00 fat_format_wrapper: format wrapper - build a base-0 volume then Fat_Format. */
/* 0x0803b310 Fat_Format: write a FAT VBR (templates EB3C90 "MSDOS5.0" / EB5890 "MSWIN4.1") at partition sector 0 (vol+0x4c); zeroes buf+0x1c6..0x1c9; never writes an MBR. */
/* 0x08010078 nftl_read_op_leaf: NFTL read op installed at part+0x250; bounds-check, resolver +0x26c, die-split, calls the device data leaf (0x0800fb00); returns the leaf value (0=fail). */
/* 0x08010144 nftl_tag_read_op: NFTL tag/spare read op installed at part+0x258; calls the device tag leaf (nand_read_page_tag_leaf 0x0800f9cc) - the block-scan path. */
/* 0x08044948 nftl_partition_init_fn: partition-init fn installed at part+0x274; arithmetically computes the zone-size table part+0x1d0/+0x1d2 from StartBlock/span/chip[+0x10] (no flash read). */
/* 0x0811a064 fat_read_fat_sector: FAT-region sector read; abs = vol[+0x4c] + fatobj[+0xc] + n via (**(vol[8]+0x10)). */
/* 0x0803b9cc fat_read_data_sector: root-dir/cluster-region sector I/O; abs = vol[+0x4c] + fatobj[+0x1c] + ((cluster-2)<<secperclus). */
/* 0x080addb4 Fwl_GetRootDir: converts the static ":\" root via codepage_convert_devpath (flags 0) and returns the root dir object. */
/* 0x080fb8ac dir_open: opendir; an empty converted path -> NULL, else File_Open. */
/* 0x08044d28 nftl_init(part,byte_offset,length,dest,mode): NFTL metadata-region reader (6 regions); reads length bytes from one region; 0=fail -> mtd_reload retry. */
/* 0x080466e8 mtd_reload: NFTL fail handler - allocate a fresh 0x280 partition object + arena, re-run mtd_init over the full span (rescan), rewrite all regions, copy tables back, reload the 4 map windows. */
/* 0x07ffb930 hal_event_id_for: event-id mapper (rt 0x08003930): 0->0x1046 (poll/heartbeat tick), 1->0x105f (OID partial), 2->0x1060 (OID decoded). */

/* ===== Harvest #4 (RE 2026-07-06: conformance-audit{,-hw,-hw2} / test-mode / power-on-sound /
 * system-voice-feedback / oid-digit-readout / settings-config / gme-format-completeness /
 * gme-subtype-parameters / ab-drive-layout / nftl-medium-translation / nftl-dir-cow / usb-msc-device
 * / zc90b-model). Unified base 0x08009000; each PROG address verified as a real function entry
 * against out/decomp_named/. ===== */

/* ---- CORRECTIONS applied this harvest ---- */
/* 0x080ab500 fwl_voice_stop_close: NAMING TRAP - it does NOT play. Hard voice STOP + player teardown:
   closes and invalidates the cached g_voice_fd (@0x081226f0 := -1), amp off. Book-state exit path.
   (was mis-named fwl_voice_play.) */
/* 0x080ab620 fwl_voice_stop_sync: synchronous voice STOP (not a play): waits the audio busy flag
   @0x08008c60 bit2, aud_player_stop_2, amp/flag cleanup; called first inside play_media_setup. ~50 call
   sites = "silence the current prompt". (was mis-named fwl_voice_play_2.) */
/* 0x0804bf18 testmode_chord_gate: returns 1 iff GPIO11 (power button) AND GPIO1 (volume-down) both read
   1 - the factory test-mode ENTRY CHORD (both buttons held), sampled 4x by splash_entry. GPIO11/GPIO1
   are BUTTONS, not battery comparators. Logs "11 LEVEL=%d,0 level=%d.". (was battery_comparators_ok /
   battery_get_level.) */
/* 0x0804cecc splash_prodtest_handler: splash(1) EA[0] multiplexer - retail auth then post 0x1014;
   prod-test stage machine (when akoid_buf[0xb3]==1: spotlight/TestFile stages, ticks the digit readout
   prodtest_speak_number_resume at the top of every event); low-batt continuation; idle USB re-poll.
   Prod-test stage byte @0x081DA004+2. (historic name fwupdate_verify_image was wrong.) */
/* 0x080ee6e4 prog_area_dump_checksum: locates the FHA "PROG" area, streams the whole PROG NAND image
   into a handle (spotlight.bin), and byte-sums all-but-trailing-4 vs the trailing LE u32; running sum
   left at *0x081DA000. Prod-test firmware-integrity check. (was file_checksum_verify / akoid_decode_frame.) */
/* 0x0803d1d4 usb_power_switch: MISNOMER (name kept) - it is the blocking USB-MSC BOT/SCSI SERVICE LOOP:
   usb_dev_open, then poll BOT phase + serve transfers, switch to USB power after ~1.2M idle iters,
   periodic flush; phase7 (ANYKA vendor) -> flash_program_region + deliberate park. Attaches zone B as
   the only writable LUN via usb_msc_register_lun(1). */
/* 0x08041ab4 sysapi_free: MISNOMER (name kept) - it is the MUSB per-endpoint CSR flush/clear (index
   0/1/2), NOT a memory free. */
/* 0x08077230 gme_power_on_playlist_play: parses+plays the GME header@0x71 power-on-sound playlist at book
   mount; records the last game-level announcement at gctx+0xc8/+0xb7. CORRECTS the earlier label
   "gme_media_xor_key_setup" (the media XOR-key setup is actually inline in gme_parse_header/gme_oid_dispatch). */

/* ---- Prod-test / number readout / boot / voice sinks ---- */
/* 0x0804bf84 prodtest_speak_number_start: begin decimal readout of akoid_buf+0x64 (gated on +0x60 and
   audio idle): speak the most-significant digit (no leading zeros) as voice digit+9 (0x09..0x12), store
   next place at +0x181. Only caller: splash_prodtest_handler. */
/* 0x0804c090 prodtest_speak_number_resume: speak one digit per call of an in-progress number readout
   (place from akoid_buf+0x181, interior zeros spoken); the ones place clears the active flag +0x60. */
/* 0x0804bee8 prodtest_voice_stop_helper: stop the current system voice before a prod-test single-digit
   play (splash-EA 0x1060 tap branch; decomp tail truncated). */
/* 0x08039100 boot_task_main: boot task - print serial info, call app_init_main, then set the key-scanner
   boot latch akoid_buf[0x57]=1 so the power-on button press cannot fire a spurious key event. */
/* 0x08034bc0 poweroff_voice_sequencer: power-off/low-batt voice sequencer ticked from ~58 handlers (stage
   byte @0x08008c0b): batt-warn 0x17 / final 0x1A / announcement 0x2C, then off-jingle 0x14 -> off event. */
/* 0x0804e570 root_action_boot_decide: root(0) event action - if a pending B:/*.upd exists and GPIO8==0,
   play A:/Language/Update<LANG>.wav and transition to fw_update(2). */
/* 0x080ad564 fs_file_delete: delete a file by path; used by splash_entry to consume (delete) B:/FLAG.bin
   after triggering the post-update resume path. */
/* 0x080f07d0 voicelist_delete_and_invalidate: DEAD (no static caller) - deletes/invalidates the vestigial
   a:/voicelist.lst generic music-list (list-id 3); unrelated to the system voices. */

/* ---- Settings / persistence (A:/SYSTEM/profile.dat, B:/Questionstatus.txt, tap logs) ---- */
/* 0x0804be24 profile_restore_at_splash: read profile.dat record @0xC at splash -> restore last power-off
   tick, tap-log counter, unknown byte into subctx+0xcc/+0x13c/+0x140. */
/* 0x0804be7c profile_save_at_poweroff: write profile.dat record @0xC {sys tick, tap-log counter,
   subctx+0x140}; only caller is power_off_or_charge_park (graceful off). */
/* 0x0804b708 questionstatus_load: read B:/Questionstatus.txt (9x256 '0'/'1' quiz matrix) into subctx+0x4bc
   at book(13) entry; missing file -> matrix stays zeroed. */
/* 0x0804b7d4 questionstatus_save: delete + rewrite B:/Questionstatus.txt from the quiz matrix; called
   from power_off_or_charge_park. */
/* 0x08034588 book_delete_african_fen: open A:/African.fen and delete it if present at book entry;
   vestigial cleanup. */
/* 0x0803444c taplog_append_A: append {u16 current product-id @0x081da08c, u16 OID} to
   A:/Firmware log file.bin (handle @0x08121a84). */
/* 0x080344b8 taplog_append_B: append the same {product-id, OID} tap record to B:/.tiptoi.log (handle
   @0x08121e6c). */
/* 0x080ad6d8 fs_dir_ensure: mkdir-if-missing helper (used to ensure A:/SYSTEM/ before recreating profile.dat). */
/* 0x080ad188 tick_to_date: convert a system tick to a calendar date (div 60/60/24 + leap-year walk);
   result discarded by its splash caller (vestigial RTC continuation). */
/* 0x0809d3c4 quiz_question_mark_used: quiz helper - skip already-asked questions ('1' flags) and mark the
   chosen question used in the Questionstatus matrix. */
/* 0x0809d858 quiz_questionrow_clear: clear one row of the Questionstatus quiz matrix when all its
   questions are exhausted. */
/* 0x0804ff04 calendar_helper1 / 0x080500c4 calendar_helper2 / 0x08050170 calendar_dat_read /
   0x08050378 calendar_helper3: DEAD vendor calendar module (A:/calendar.dat); no static caller in this build. */

/* ---- Audio gain / DAC (settings-config, conformance-audit-hw §1) ---- */
/* 0x0802265c audio_gain_commit: commit a volume gain - rescale by the current sample-rate state, clamp at
   0x400 (unity), store the Q10 multiplier into audio-out ring 0x08008d30 +0x14/+0x18. */
/* 0x0803249c aud_player_apply_volume: re-apply the player's cached volume index (player+0x29) via
   sm_set_sound_profile; called from aud_player_play at every playback start. */
/* 0x08032eb0 dac_flush_silence: teardown silence flush - submit an 0x800-byte silence buffer directly via
   hal_dma_submit and spin on the swallow flag *0x08008c91 until drained. */
/* 0x08033588 dac_clk_apply: program the DAC clock - write the divider into 0x04000010 low byte, clear the
   power-down bit9, set apply/busy bit8 and spin until the hardware self-clears it. */
/* 0x08050898 codec_reinit: quiet-path audio codec/amp re-initialization (with audio_state_reset), invoked
   periodically from book_mode_periodic_service while amp pad GPIO16 reads back on. */
/* 0x080a6a0c book_mode_periodic_service: periodic book-mode idle service - when amp pad GPIO16 reads back
   1, run codec_reinit + audio_state_reset and re-poll the USB-detect pin 8 (game_ctx[0x1e]=2). */

/* ---- A:/B: drive layout + USB-MSC device (ab-drive-layout, usb-msc-device) ----
 * Proven mapping: drive 0 = 'a:' = partition A = SYSTEM (profile/voices/oidfilelist); drive 1 = 'b:' =
 * partition B = the user/game/USB-writable drive. The PC over USB-MSC can only write B:. */
/* 0x0803a1c8 fs_drive_register: store a mounted FAT volume ptr into the drive array (idx 0='a:' system,
   idx 1='b:' user) and bump the drive count. */
/* 0x0803ece4 usb_msc_register_lun: build a USB-MSC LUN record - sel 1 = partition B (*0x081db910) +
   "MP4 Player" inquiry (both live callers pass 1); sel 2 = partition A + "RES" inquiry (no caller, dead). */
/* 0x0803d0cc usb_vendor_format_B: USB vendor "FM" command - format partition B via
   fat_format_wrapper(partB, 0, sizeMB*2048 sectors). Only runtime reformat of B:. */
/* 0x08040440 usb_msc_lun_insert: insert a built LUN record into the MSC LUN array; LUN+0x14 = the medium
   object the READ10/WRITE10 data pump addresses. */
/* 0x080ef760 fs_scan_fail_flag: B:/A: partition-scan-fail handler - sets the soft error flag only (B: is
   never auto-formatted at mount; the state-5 remount format-on-fail targets A:). */
/* 0x080402f8 usb_dev_open: allocate the MSC context sub-objects (LUN array, CBW obj, phase/CSW obj,
   read/write stream descriptors); requires FIFO buffer >= 0x200. msc_ctx @0x081DBABC. */
/* 0x0803db9c usb_dev_open_wrap: allocate the 0x4c USB device object then usb_dev_open. */
/* 0x0803da4c usb_phy_start: build the descriptor-pointer table into the dev object, alloc a 0x1000 EP0
   buffer, wire EP0/bulk callbacks, then usb_report_speed. */
/* 0x0803db24 usb_irq_dispatch: top-level USB device ISR (IRQ line 6, from the vector stub) - read MUSB
   INTRUSB/INTRTX/INTRRX, service EP1/EP2 via usb_bulk_cbw_handler, kick usb_scsi_handler, drive CSW send.
   (runtime addr; 0x08035b24 is only its nandboot static twin.) */
/* 0x0803fb48 usb_scsi_decode: SCSI CDB opcode decoder - TUR/INQUIRY/READ CAPACITY/READ10/12/WRITE10/12/
   MODE SENSE/VERIFY plus vendor opcodes 0xCC-0xE2 (ANYKA/FM/NO/AD). */
/* 0x08040068 usb_cbw_parse: treat a completed 31-byte EP1-OUT transfer as a CBW - check "USBC" signature,
   extract dCBWDataTransferLength + bCBWLUN, BOT phase -> 1. */
/* 0x0803ef94 usb_csw_send: build the 13-byte CSW ("USBS" + CBW tag + residue + status) and send on EP2 IN,
   then BOT phase -> 0 (idle). */
/* 0x08040108 usb_data_phase_pump: BOT data-phase pump - stream READ10/WRITE10 chunks between the 32KB USB
   buffer and the LUN medium (LUN+0x14 vtable) until residue 0; LBAs address B: sectors directly. */
/* 0x0803eea4 usb_medium_ready: medium-ready gate before every SCSI media access (medium-type-3 present +
   unit-attention). */
/* 0x0803dcf0 usb_phase_get / 0x0803df14 usb_phase_set: read/write the BOT phase byte @0x08122718. */
/* 0x08040e7c usb_get_descriptor: GET_DESCRIPTOR (device/config/string/qualifier) via usb_string_descriptor
   / usb_build_config_desc. */
/* 0x0803d9fc usb_string_descriptor: serve string descriptors by index (LANGID 0x0409, "OID", "OID Player",
   "USB 2.0"). */
/* 0x0803d8e0 usb_build_config_desc: assemble the config descriptor, patch EP wMaxPacketSize to 0x200 (HS)
   / 0x40 (FS) per negotiated speed. */
/* 0x08041460 usb_ep_config: per-endpoint max-packet/FIFO setup (EP1 OUT, EP2 IN; 0x200 HS / 0x40 FS). */
/* 0x08041684 usb_fifo_read / 0x080417f4 usb_fifo_write: EP FIFO read/write - PIO for EP0, hal-DMA fast
   path when len%64==0 else word PIO through the L2 windows. */
/* 0x08041b30 musb_csr_read / 0x08041b20 musb_csr_write: read/write an indexed MUSB endpoint CSR through
   the INDEX(+0x0E) window (+0x12 CSR0/TXCSR, +0x16 RXCSR). */
/* 0x08041b64 musb_rxcount: read RXCOUNT(+0x18) for the indexed endpoint. */
/* 0x08041b44 musb_read_intr: read (read-clear) the MUSB interrupt regs INTRUSB(+0x0A)/INTRTX(+2)/INTRRX(+4). */
/* 0x08041bb4 musb_ep_stall: set the SendStall bit on the EP1/EP2 CSR. */
/* 0x0803dbec usb_dev_close: USB session teardown - BOT phase -> 3, free the MSC dev sub-objects, usb_phy_off. */
/* 0x08051138 usb_state_exit_filter: state-5 (USB-PC) event filter - on tick/0x1046 or a 0x105F button
   while the session byte is clear, post event 0x100C back to standby. */
/* 0x08040c6c usb_req_get_status / 0x08040cd8 usb_req_clear_feature / 0x08040d44 usb_req_set_feature /
   0x080411ac usb_req_get_configuration / 0x080411b4 usb_req_set_configuration / 0x08040b38 usb_req_stub:
   EP0 standard control-request handlers wired into the request table @0x08041FA4 (bRequest & 0xF). */

/* ---- NFTL directory / copy-on-write write path (nftl-dir-cow) ---- */
/* 0x080495f0 nftl_medium_write_op: FS-medium write op with a single dirty-2KiB-page cache - RMW-reads the
   whole current page before a partial-page merge, flushes on every page switch. */
/* 0x080494ec nftl_medium_page_flush: flush the medium's dirty cached 2-KiB page to the NFTL write path. */
/* 0x08049448 nftl_page_write_cow: NFTL copy-on-write page write - after every relocate: window fault-in,
   nftl_update_log2phy(L->N), RLE prepend, write-ptr set, then re-resolve. Builds the 8-B spare tag
   ([0]=(seq+1)&0x7f, [2:4]=old head or 0xfffd, [4:6]=logical|0x8000 iff no old head). */
/* 0x0804a68c nftl_write_placement: decide append vs COW for a page write - append iff write-ptr < page,
   or wp unknown and the blank-probe passes. */
/* 0x0804a8a8 nftl_prefold_check: pre-fold trigger - if the RLE chain run length > 10, fold via
   nftl_core_rw_sector. */
/* 0x0804a5ac nftl_wp_probe: write-pointer blank-probe - scan page tags top-down while tag[4:6]==0xffff,
   set the RAM write pointer at the first non-blank tag. */
/* 0x0800ee28 nftl_wp_gate: accept a page only if wp unknown (0xff) or page <= wp. */
/* 0x0800f488 nftl_page_source_pick: read-side page-source selector - newest->oldest chain walk; consumes
   only spare tag [0]&0x80 (obsolete skip) with a single-tail shortcut when chain next==0xfffd. */
/* 0x08043700 nftl_copyback_eligible: copy-back eligibility gate for fold copies - same zone AND source
   successor != 0xfffd (source is a non-tail chain member). */
/* 0x080444f4 nftl_page_copy: NFTL page-copy op (part+0x260) - resolve src/dst rel-blocks, set write ptr,
   die-split, dispatch the dev+0x34 HW copy-back leaf (dev_copyback_leaf). */
/* 0x08010248 nftl_set_writeptr: set the RAM write pointer of an NFTL physical block to the given page. */
/* 0x08042be8 nftl_rle_prepend: prepend the fresh COW block N before the old head H in the RLE chain list. */
/* 0x0800ee5c nftl_window_faultin: dense-window fault-in - persist an evicted dirty window (nand_mtd_3),
   reload via nftl_init; addressed through the RAM page-dir part+0xe. */
/* 0x08042ec0 nftl_recycle_erased_head / 0x0804a20c nftl_alloc_recycle: recycle folded/erased old-head
   blocks back into the NFTL allocator. */

/* ---- GME format completeness + game-type state machines (gme-format-completeness, gme-subtype-parameters).
 * The 2N built-in game engine dispatches statechart states 14..19/31/60 (GME script game types) and native
 * product-9 states 46..49; each state has an entry, a record-loader, an engine, subgame parser and OID
 * matchers. game-type -> state: type1/2/3/5/10 -> 14, type4/40 -> 15, type6 -> 16, type7 -> 17, type8 -> 18,
 * type16 -> 19 (and product-2 quiz -> 60), type9 native -> 46..49, type253 -> 31. ---- */
/* 0x08034cbc gme_read_game_type: walk the GME game table (hdr@0x10) to the record at index gamectx+0x131
   and return the tapped OID's game-type byte for dispatch. */
/* 0x08037b10 gme_parse_header_alt: DEAD alternate GME header parser (hdr 0x1C/0xBC/0xB8/0x30); no caller. */
/* 0x08038320 gme_play_system_voice: codec-0x13 system-voice playback path - decode/play special voice
   media, bypassing the medialib codec dispatch. */
/* 0x08078014 playlistlist_parse: parse a playlist-list at a GME offset, leaving its playlist count at
   gamectx+0x490. */
/* 0x08078050 playlistlist_play: play playlist `index` of the playlist-list at a GME offset (shared audio helper). */
/* 0x080773c8 game_record_load_outer / 0x080774e0 game_record_load: state-14/15 game-record loader pair -
   9-word header (type, subgameCount, rounds, earlyRounds, repeatOID...), 5 PLL offsets, 10 target scores
   + 10 finish PLLs. */
/* 0x08078e58 game14_entry: entry for statechart state 14 (GME game types 1/2/3/5/10); plays the start PLL. */
/* 0x080785cc game14_phase_engine: state-14 phase engine - round loop, feedback-index modes, retry limits,
   find-all-targets masks, score cascade to finish PLLs. */
/* 0x08077a8c game14_subgame_parse: parse a state-14 subgame record (u0-u5, three OID lists, PLL offsets). */
/* 0x080779ac game14_pick_unplayed_subgame: pick a random not-yet-played subgame via the 128-bit played mask. */
/* 0x08078318 game14_match_target_oids / 0x08078474 game14_match_decoy_oids / 0x08078524 game14_match_other_oids:
   match a tap against subgame OID list 1 (targets) / list 2 (known-wrong decoys) / list 3 (other actives). */
/* 0x08080780 game15_entry / 0x08080988 game15_engine / 0x08080264 game15_subgame_parse /
   0x08080178 game15_draw_subgame: state 15 = GME type 4/40 memory-sequence game (cumulative step sequence). */
/* 0x0808339c game16_entry / 0x08081d18 game16_record_load / 0x08082bf4 game16_engine /
   0x080823a0 game16_subgame_parse / 0x08082950 game16_subgame_match / 0x080822e0 game16_pick_unplayed_subgame:
   state 16 = GME type 6 bonus-stage game (score >= bonusTarget unlocks a bonus stage). */
/* 0x08085370 game17_entry / 0x08083f94 game17_record_load / 0x08084e64 game17_engine /
   0x08084624 game17_subgame_parse / 0x080844bc game17_pick_unplayed_group / 0x0808459c game17_load_group_ids:
   state 17 = GME type 7 subgame-groups game (one group = one round). */
/* 0x080866a8 game18_entry / 0x08085ee4 game18_record_load / 0x080864dc game18_select_handler /
   0x08086410 game18_match_select_oid / 0x0808617c game18_subgame_parse: state 18 = GME type 8 game-select
   menu (a menu tap maps to gameSelect[k]-1, written to gamectx+0x131 and re-dispatched). */
/* 0x08088044 game19_entry / 0x08087110 game19_record_load / 0x08087bf4 game19_engine /
   0x080873f8 game19_match_extra_oids / 0x08087528 game19_subgame_parse / 0x08087858 game19_match_target_ordered:
   state 19 = GME type 16 selector-OID game (tap gExtraOIDs to open subgames; ordered-targets via u1 gate). */
/* 0x0808c610 game60_entry / 0x0808b7ac game60_record_load / 0x0808c3ac game60_engine /
   0x0808bc80 game60_subgame_parse: state 60 = product-2 fixed 8-question quiz (correct-of-8 vs
   targetScores[0..2] -> finishPLL 1..3). */
/* 0x08054f54 game46_read_game_record / 0x08056890 game47_read_game_record / 0x080582ac game48_read_game_record /
   0x0805a6c4 game49_read_game_record: native product-9 (GME type-9 cue-bank) game-table walkers (fs_seek
   hdr@0x10 -> record at gamectx+0x131). */
/* 0x08055864 game46_engine: engine for the product-9 native game states (46..49 family, cue-bank games). */
/* 0x080556b8 native_cue_play: play playlist k of a type-9 cue-bank playlist-list slot (k from counter/score/random). */
/* 0x080aa208 game31_entry / 0x080aa498 game31_engine: statechart state 31 (GME game type 253; empty record). */

/* ---- New resident-nandboot HAL leaves proven this harvest (ghidra addr = runtime - 0x8000). These are
 * NOT yet recognized function entries in the auto-analysis base, so they are recorded here as source-of-truth
 * notes; to attach names/comments they need entries in the BOOTROM dict in tools/ghidra_rename.py. ---- */
/* 0x07ffb9d4 hal_audio_irq_handler (rt 0x080039d4): audio DMA-done ISR - clear kick bit16 on 0x04010000,
   honor swallow flag 0x08008c91 / override cb 0x08008c64, resubmit next chunk or set idle 0x08008c60 bit0. */
/* 0x07ffbdbc hal_ao_get_chunk (rt 0x08003dbc): dequeue the next 0x400-B PCM chunk from the audio ring
   (src=ring[+0x44]+ring[+0x38]), handle wrap +0x40 / count +0x20 / zero-fill +0x34. */
/* 0x07ff90a8 hal_dma_spurious_check (rt 0x080010a8): DMA-completion spurious filter - reads status
   0x0401001c; returns 0 (legit) unless all masked channel bits set. */
/* 0x07ffe740 hal_gpio_read (rt 0x08006740): read the level of one GPIO input pin (0x040000bc); the leaf
   all key / USB-detect / comparator polls go through. */
/* 0x07ffa3bc nb_nand_copyback (rt 0x080023bc): HW NAND copy-back leaf (r0=plane/CE, r1=SOURCE row,
   r2=DEST row); target of dev_copyback_leaf. */
/* 0x07ffb39c irq_mask_push (rt 0x0800339c) / 0x07ffb3e4 irq_mask_pop (rt 0x080033e4): critical-section
   enter/exit - save+zero / restore INT_ENABLE 0x04000034. */
/* 0x07ffeb2c hal_key_read (rt 0x08006b2c): read buttons - GPIO0==0 -> code 5 (VOL+ active-low),
   GPIO1==1 -> code 6 (VOL-), GPIO11==1 -> code 8 (POWER, active-high); else 0xFF. */
/* 0x07ffd8f0 key_scan_start (rt 0x080058f0): periodic ~120-tick key scan -> hal_key_read; on a key arms a
   20-tick debounce (key_debounce_cb). 0x07ffd954 key_debounce_cb (rt 0x08005954): re-read; same -> hold
   tracker. 0x07ffd998 key_hold_cb (rt 0x08005998): hold/repeat tracker (count @0x08008c1c). 0x07ffebfc
   key_event_post (rt 0x08006bfc): post hal_event 0x105F {code,sub}. */

/* ===== GME naming campaign session 2 (2026-07-20): interior game-handler clusters + boundary
 * leakage. Per-cluster naming from decomp reading of the already-named entry/engine functions;
 * all addresses unified base 0x08009000. "seed" below = the per-game random seed set at ctx init
 * (rng_below(akoidpara+0xcc)); picks are seed % count via rt_udiv remainder. ===== */

/* ---- core-gme-script ---- */
/* 0x08034434 gme_clear_special_play_flag: clear akoidpara+0x20 (the special-OID media
   already-played/suppress flag) so the deferred special-OID replay path in gme_oid_dispatch
   (flag DAT_08037cc8 bit5, audio idle) replays the cached media at akoidpara+0x30/+0x34. */

/* ---- game15 (state 15 = GME type 4/40 memory-sequence game) ---- */
/* 0x0808059c game15_match_target_oids(step): scan the u16 OID list at the step's list-1 offset
   (per-step cache DAT_080808cc[step], from subgame +0x464) for the tapped OID (akoidpara+4);
   ctx+0x21 := 1 hit / 0 miss; the scanned OID is left at ctx+0x25e. */
/* 0x0808063c game15_match_wrong_oids(step): scan the step's list-2 (wrong/decoy) OID list
   (DAT_080808d4[step], from subgame +0x47c); ctx+0x21 := match index, 0xff if no match. */
/* 0x08080910 game15_teardown: stop audio (fwl_voice_stop_sync) + audio_mute, HAL delay(5), free
   the game ctx physpool block, clear the 32-entry per-step offset caches. Engine state 0xbb then
   sets akoidpara->game_index := 0x0c and re-enters game15_entry - i.e. finishing the memory
   sequence CHAINS into game record 12 (hardwired record number). */

/* ---- game16 (state 16 = GME type 6 bonus-stage game) ---- */
/* 0x08081a14 game16_ctx_reset: zero the game16 context fields, roll the game seed
   (rng_below(akoidpara+0xcc)) into +0x28, fill the 3 x 128-entry per-item caches with -1. */
/* 0x08081b24 game16_read_u16_field(off): read the u16 at GME offset off into ctx+0x698. */
/* 0x08081b64 game16_play_phase_cue(pll_off,idx): game16 copy of game_play_phase_cue - set
   akoidpara game_phase_idx/cur_playlist_off, resolve playlist idx of the playlist-list at
   pll_off against the media table (hdr+4) and play entry 0; remaining entries walked at audio
   boundaries via the cached count. */
/* 0x08082aac game16_match_decoy_oids: scan the u16 OID list @ctx+0x670 (count -> +0x66a) for the
   tapped OID; ctx+0x25 := match index, 0xff if none (known-wrong decoy list). */
/* 0x08082b5c game16_match_other_oids: scan the u16 OID list @ctx+0x678 (bound ctx+0x674);
   returns 1 if the tap is one of the other active OIDs, else 0. */

/* ---- game17 (state 17 = GME type 7 subgame-groups game) ---- */
/* 0x08083be0 game17_read_pll_count(off): read the u16 entry count at GME offset off (low byte);
   used to size playlist-list picks (shared with game18's select handler). */
/* 0x08083c14 game17_play_phase_cue(pll_off,idx): phase-cue player for state 17: resolves each
   playlist entry of playlist-list[idx] against BOTH media tables - the main one @hdr+4 AND the
   additional media table @hdr+0x60 - into parallel (off,size) arrays, then plays the first
   main-table entry. Types 7/8 are the only script game engines using the hdr+0x60 bank here. */
/* 0x08083e40 game17_ctx_reset: clear the game17 context (group/round state, 128-entry caches,
   32-entry voice slots 0xff), roll the game seed into +0x24. */
/* 0x08084bb0 game17_match_target_oids: scan the group-target u16 OID list @ctx+0x6a4 (count ->
   +0x49c) for the tap; hit: ctx+0x22 := 1, index -> +0x487, and track the per-target found
   bitmask ctx+0x48c (already-found -> ctx+0x22 := 2 / found-count +0x488++, bit cleared). */
/* 0x08084d0c game17_match_decoy_oids: scan the u16 list @ctx+0x6a8 (count -> +0x6a0);
   ctx+0x22 := match index, 0xff if none. */
/* 0x08084dbc game17_match_other_oids: scan the u16 list @ctx+0x6b0 (count -> +0x6ac);
   returns 1 on match else 0. */

/* ---- game18 (state 18 = GME type 8 game-select menu) ---- */
/* 0x08085c64 game18_play_phase_cue(pll_off,idx): state-18 twin of game17_play_phase_cue
   (same dual media-table resolve, hdr+4 + hdr+0x60). */
/* 0x08085e8c game18_ctx_reset: reset the small game18 context; roll the game seed into +0x1c. */
/* 0x0808637c game18_match_menu_oids: scan the u16 OID list @rec+0x2c (count -> +0x28); the
   select handler plays PLL rec+0x10 on match (menu-area tap) vs rec+0x30 (unrelated tap). */

/* ---- game19 (state 19 = GME type 16 selector-OID game) ---- */
/* 0x0808702c game19_ctx_reset: clear the game19 context (32-entry caches, flags), roll the game
   seed into +0x10. */
/* 0x08087494 game19_match_subgame_oids: scan the current subgame's u16 OID list @ctx+0xcc
   (count -> +0xc8); returns 1 if the tap belongs to this subgame. */
/* 0x0808795c game19_match_decoy_oids: scan the u16 list @ctx+0xc4 (count -> +0xba);
   ctx+0xf := match index, 0xff if none. */
/* 0x080879f8 game19_play_phase_cue(pll_off,idx): state-19 phase-cue player (single media table
   @hdr+4; u32 playlist-list entries). */
/* 0x08087ba4 game19_read_u16_at(off): read and return the u16 at GME offset off. */
/* 0x08087bd4 game19_subgame_contains_tap: clear ctx+0xb4, game19_subgame_parse() the current
   subgame, then scan its OID list @ctx+0xcc; returns 1 if the tapped OID is in this subgame
   (used to find which subgame a selector tap opens). */

/* ---- game60 (state 60 = product-2 fixed 8-question quiz over the type-16 record) ---- */
/* 0x0808b708 game60_ctx_reset: clear the quiz context, stamp sys_get_tick() at +0xa0, reset the
   32 per-question answered bitmasks (+0x20[]) and score caches. */
/* 0x0808bfd0 game60_match_question_oids: scan the question u16 OID list @ctx+0x13c for the tap;
   hit: current question index -> ctx+0x14, return 1. */
/* 0x0808c060 game60_match_other_oids: scan the u16 list @ctx+0x158 (count -> +0x154); returns 1
   on match else 0. */
/* 0x0808c0fc game60_match_answer_oids: scan the answer u16 list @ctx+0x148 (count -> +0x144);
   on match check the current question's answered bitmask ctx+0x20[q]: bit set -> ctx+0xd := 2
   (already answered), else ctx+0xd := 1 and set the bit. */
/* 0x0808c1d0 game60_play_phase_cue(pll_off,idx): state-60 phase-cue player (single media table). */
/* 0x0808c37c game60_read_u16_at(off): read and return the u16 at GME offset off. */

/* ---- native type9 (states 46..49 cue-bank games) ---- */
/* 0x080552b8 game46_pick_unplayed_subgame: pick = seed(+0x68) % subgameCount(+1) then advance
   through the unplayed bitmask +0xf0 to the next unplayed subgame; index -> ctx+2, bit cleared. */
/* 0x08055318 game46_subgame_parse: parse the subgame record at +0x70[ctx+2]: skip the 11-u16
   header, compute the 3 OID-list (count,offset) pairs -> +0x14/+0x18, +0x1c/+0x20, +0x24/+0x28,
   then the trailing u32 -> +0x10. */
/* 0x08055520 game46_match_target_oids: scan OID list 1 (@+0x18) for the tap; hit: ctx+0x60 := 1
   and ctx+0x65 := (tapped_oid + 0x60) & 0xff (the cue-slot selector byte). */
/* 0x080555e4 game46_next_cue_phase: step the 4-phase cue sequencer: phases gated by the record
   bytes +4..+7 (0xff = phase absent) and one-shot flags +0xc..+0xf; ctx+3 := phase 1..4,
   decrement the repeat counter +9. */

/* ---- reading / wronglimit built-in games ---- */
/* 0x0806a54c game_oid_alias_equal(a,b): true iff OIDs a and b are the same button across the two
   parallel OID ranges based at 0x15EB and 0x1609 (delta 0x1E): (a-0x15EB == b-0x1609) ||
   (a-0x1609 == b-0x15EB). Cross-range alias test used by the reading game (decomp-only, not
   yet exercised empirically). */
/* 0x0806e478 game_wronglimit_pick_subgame: ctx+6 := seed(+0x10) % subgameCount(u16 @ctx+2). */
/* 0x0806e498 game_wronglimit_subgame_parse: parse the subgame record at +0xa8[ctx+6]: skip the
   11-u16 header, 3 OID lists ((count,off) -> +0x1f/+0x20, +0x24/+0x28, +0x2c/+0x30; per-target
   active flags +0x128[] := 1, remaining count -> +0xd), then 9 u32 playlist-list offsets
   (+0x48,+0x34,+0x38,+0x40,+0x44,+0x3c,+0x4c,+0x50,+0x54). */
/* 0x0806e7a0 game_wronglimit_match_target: scan target list @+0x20 for the tap; found: if its
   +0x128[] flag is still set -> ctx+8 := 1 (fresh find; clear flag, found-count +0xe++,
   remaining +0xd--), else ctx+8 := 2 (repeat find); then also scan list 3 @+0x30 -> index into
   ctx+0xc. */
/* 0x0806e8dc game_wronglimit_match_other: scan the u16 list @+0x30; returns 1 on match else 0. */
/* 0x0806e990 game_wronglimit_play_phase_cue(pll_off,idx): wronglimit phase-cue player (single
   media table; plays entry 0 of the selected playlist, rest at audio boundaries). */
/* 0x0806eb3c game_wronglimit_play_random_cue(pll_off,idx): like the phase-cue player but resolves
   ALL entries of the selected playlist and plays ONE random entry (seed(+0x10) % count);
   no boundary continuation (pending count := 0). */
/* 0x0806ecf4 game_wronglimit_pick_voice: walk the 4-wide voice-variant permutation row
   (DAT_0806f180 + row(+0x17)*4) from cursor +0x18 for the next variant allowed by the bitmask
   +0x15; result -> ctx+0x16. */
/* 0x0806efc8 game_score_max4(a,b,c,d): return max of the four player scores. */
/* 0x0806efe4 game_count_max_winners(a,b,c,d,mask): count how many of the mask-enabled players
   are tied at the maximum score (winner-announcement helper). */

/* ---- findtarget (state ~ game_findtarget_sm) ---- */
/* 0x08092704 findtarget_subgame_parse: parse the subgame record @+0x54[+0xd4]: 11-u16 header,
   3 OID lists ((count,off) -> +0x3c/+0x40, +0x44/+0x48, +0x34/+0x38), then 9 u32 playlist-list
   offsets (+0xf4,+0xd8..+0xf0,+0xfc). */
/* 0x08092a58 findtarget_match_target_oids: scan OID list 1 @+0x40 for the tap; +0xf8 := 1 hit /
   0 miss. */
/* 0x08092b58 findtarget_play_phase_cue(pll_off,idx): findtarget phase-cue player (single media
   table). */
/* 0x08092d28 findtarget_claim_target: match the tap against the u16 target array DAT_08092d18
   (count +0x4c); if it is a NOT-yet-found target: mark found (+3[i] := 1), found-count +2++,
   index -> ctx byte 0, return 1; else 0. */
/* 0x08092dac findtarget_claim_target_quiet: twin of findtarget_claim_target without the
   found-count increment; returns 1 only for a fresh find. */
/* 0x08092eac findtarget_pick_unfound_target: pick = seed(+0x24) % count(+0x4c), then advance
   past already-found targets (+3[] flags); next unfound target index -> ctx+0xd4. */

/* ---- quiz (game_quiz_sm cluster) docstrings for already-named functions ---- */
/* 0x0808d298 game_quiz_lookup_question: scan the question u16 OID list @ctx+0x18 (count ->
   +0x14) for the tap; ctx+0x15 := question index, 0xff if none. */
/* 0x0808d3c8 game_quiz_load_profmad_cand_maybe: parse the candidate record @+0x90[+0x114]:
   11-u16 header, 3 OID lists ((count,off) -> +0x4/+0x8, +0xc/+0x10, +0x28/+0x2c), then u32
   playlist-list offsets from +0x30/+0x58/+0x5c on. */
/* 0x0808d6a0 game_quiz_play_cue(pll_off,idx): quiz phase-cue player (single media table,
   entry 0 + audio-boundary continuation). */
/* 0x0808d84c game_quiz_match_answer(k): scan the u16 answer list @ctx+0x118[k] (count -> +0x4)
   for the tap; ctx+0x8c := 1 hit / 0 miss. */
/* 0x0808d8ec game_quiz_match_answerset_maybe: scan the u16 list @ctx+8; ctx+0x8c := 1 hit /
   0 miss (same shape as game_quiz_match_answer but on the base list). */
/* 0x0806f058 game_random_id: pick a random still-active target: idx = seed(+0x10) %
   count(+0x1f) advanced past cleared +0x128[] flags; re-read its OID from list @+0x20
   ("Random ID = %d"), then look up its position in list 3 @+0x30. */
/* 0x0806a3c4 game_voice_pick_norepeat(pll_off,idx,voice): resolve playlist idx of the
   playlist-list at pll_off and play entry `voice` ("Last Voice Index %d") - the per-slot
   voice-variant player behind game_pick_voice. */

/* ---- gme-binary-launch / alt book mode (gme_oid_dispatch_alt cluster). "altbook" = the
 * sibling book mode driven by gme_oid_dispatch_alt with control OIDs 0x899..0x89D. ---- */
/* 0x080a8438 altbook_play_oid_cue(code): play the cue for OID `code`: u16 media index from the
   table @ (hdr@0x10)+10 + code*2, resolved via the media table @hdr+4; skips code 0xFFFE;
   stamps the last-play tick. */
/* 0x080a8518 altbook_load_oid_list(list): copy a 32-entry u16 OID list into the active list
   ctx+0x16 (ptr -> +0x98, cursor +0x9c := 1) and play the first entry's cue; list[0] == -1
   clears the active list. */
/* 0x080a8594 altbook_queue_pending_list: merge the pending list ctx+0x56 into the active list
   ctx+0x16: copy whole if active empty, else append at the first -1 terminator. */
/* 0x080a8670 altbook_load_oid_list_2: twin of altbook_load_oid_list (identical body, second
   compiled copy). */
/* 0x080a86ec altbook_play_next_in_list(list): play the cue of list[cursor +0x9c] and advance;
   at the -1 terminator reset the active list (ptr +0x98 := 0, cursor := 0). */
/* 0x080aa264 altbook_game_teardown: stop audio + audio_mute, HAL delay(5), free the game ctx
   (physpool), clear akoidpara+0x21 and the last-play tick. */
/* 0x080aa2ac altbook_special_oid_handler: handle the alt-book control OIDs: 0x89B = hard audio
   stop (drain DAC, aud_player_stop_2); 0x89C = load the pending OID list (ctx+0x56); 0x89D =
   reset/replay (list len < 3: refill +0x56 with 0xff, reload the default list, re-queue; else
   reload from +0x56); 0x899/0x89A = double-tap counting -> on the 2nd tap reload playlist
   offsets resp. teardown + alloc a fresh 0x240 game ctx (akoidpara+0x21 := 0xb) and clear the
   cover/page counters. Product-cover OIDs 0x300..0x3E7 pass through to the caller. */

/* ---- boundary leakage names (audio/DAC/clock/libc/fs service internals; named for readable
 * call sites only - not GME logic, not descended). Roles from audio-output-hw.md and decomp. ---- */
/* 0x08009df4 memset: ADS runtime byte fill (word-optimized). */
/* 0x08009ee0 strlen: ADS runtime strlen (word-wise zero-byte scan). */
/* 0x080098f4 rt_sdiv64: ADS runtime signed 64/64 divide. */
/* 0x0800a894 rt_udiv64: ADS runtime unsigned 64/64 divide. */
/* 0x0800a906 rt_switch8 / 0x0800a908 rt_switch8_2: ADS __ARM_switch8 jump-table helpers. */
/* 0x08009f50 str_vformat: printf-family format engine (emit-callback based). */
/* 0x0800a4e4 str_vformat_2: second printf-family format worker. */
/* 0x0800a87c str_emit_char: format-engine emit callback - append one char to the buffer ptr. */
/* 0x0803062c rt_udiv10: divide-by-10 via multiply-shift; returns quotient+remainder. */
/* 0x08030824 Utl_UStrNCpy_2: bounded UCS-2 string copy (NUL-terminated). */
/* 0x0803089c Utl_UStrAppend: UCS-2 path/name append helper (udisk .lst scan path builder). */
/* 0x080ad514 file_close_if_valid: close the handle if != -1 (twin of file_close_if_open). */
/* 0x080ad4e4 fs_list_write_record: write one 0x214-byte record to the a:/oidfilelist.lst-style
   list file ("write music list failed" on short write). */
/* 0x080ab424 audio_mute: pop-free mute: GPIO13 := 1 (mute strobe), 1 ms delay, audio_amp_disable
   (GPIO16 := 0); idempotent via amp-flag 0x081db221 bit0. */
/* 0x080ab5dc audio_unmute_req: GPIO13 := 0 (unmute) + amp-on request (flag bit2); amp turns on in
   the next audio_wait_tick unless headphones (GPIO7) are in. */
/* 0x08032cb4 aud_player_dac_teardown: deeper player teardown - drain, bias ramp-down, DAC off
   (per MediaCtx state; codec-0x13 special-cased). */
/* 0x08032d38 dac_power_teardown: DAC power-down twin used with aud_player_dac_teardown. */
/* 0x08033004 audio_codec_power_ctl(on): codec/amp power sequencing (bias reg 0x0400005c ramp,
   GPIO descriptors, HAL delay); on==0 = power-down path. */
/* 0x080333fc dac_analog_powerdown: full analog DAC power-down - internal DAC regs
   (0x04000064/0x04080000/0x04000008/0x04000010) or the external-DAC variant via
   dac_ext_powerdown; then HAL timer stop(5). */
/* 0x080ef09c dac_ext_powerdown: external/alt DAC power-down (per dac type byte: clears
   0x04000008 clock bits + 0x04090000 bit0). [Inferred] */
/* 0x080ef2f4 dac_ext_ctl: external-DAC register/config helper used by dac_ext_powerdown.
   [Inferred] */
/* 0x0800b284 audio_bias_ramp: codec bias ramp helper (reg 0x0400005c stepped ramp). */
/* 0x08022fac audio_vol_clamp(idx): clamp a volume index to 0..6 (0xffffffff if no AO ring). */
/* 0x08023190 audio_pcm_ctl(handle,cmd): PCM/audio-out handle control used by the player
   teardown/loop paths. [Inferred] */
/* 0x0802fe40 audio_clk_pop: pop the top of the audio sample-rate/clock request stack (struct
   @DAT_08030028) and re-arbitrate the system clock. [Inferred] */
/* 0x0802ff04 audio_clk_push: push an audio clock/rate request onto the arbiter stack. [Inferred] */
/* 0x0802ff84 audio_clk_cmp: compare two clock-table entries (rates via HAL 0x08007798). [Inferred] */
/* 0x0802ff4c audio_clk_apply: apply the arbitrated audio clock: cancel the pending rate timer,
   compare PLL table entries (@0x08009274) and reprogram if changed (IRQ-masked). [Inferred] */
/* 0x0800b0f8 audio_clk_drop_to_base: when the required audio rate allows, reprogram the PLL back
   to the base table entry (sanity-compare, hang_forever on table mismatch). [Inferred] */
/* 0x0800b138 audio_clk_raise: raise the PLL/system clock for a higher audio rate (twin of
   audio_clk_drop_to_base). [Inferred] */
/* 0x0800b490 battery_meas_state: return 1 iff the measurement/load-enable bit (0x04000004
   bit15, toggled by battery_meas_enable) is set. */

/* ---- @local commits for the new cluster (key scan variables) ---- */
/* @local 0x08084bb0 =local_24 oid_word
 * @local 0x08084bb0 =uVar7 scan_idx
 * @local 0x0806e7a0 =local_20 oid_word
 * @local 0x0806e7a0 =uVar6 scan_idx
 * @local 0x0808c0fc =local_18 oid_word
 * @local 0x0808c0fc =uVar5 scan_idx
 */

/* ===== GME naming campaign session 3 (2026-07-20): game types 17-23 (statechart states
 * 58/59/61/62/63/66/68) - previously completely unstudied. Per-state clusters follow the same
 * template as states 14-19/60: entry (mode byte 0xb, physpool ctx, record load, start cue) /
 * record_load (fs_seek hdr@0x10 -> game-table record) / subgame_parse (u0..u5 + 3 OID lists +
 * 8 PLL offsets) / match_{target,decoy,other}_oids / play_phase_cue / phase_engine / engine
 * (the EA: script-hybrid wrapper - every content tap ALSO runs its normal play script) /
 * teardown (desc+4 exit). All these states share ONE global cue-resolve machinery:
 * (offset,size)[32] arrays @0x081dac84/0x081dad04 + pending counter @0x081dac7c - the cue
 * players resolve a WHOLE playlist up front, play entry 0, and the engines drain the rest one
 * entry per audio boundary. States 59/61/62/63 additionally resolve every entry against the
 * ADDITIONAL media table (hdr@0x60) into a second array pair - played on the SECOND
 * consecutive gRepeatOID tap (an alternate/slower recording), not in the normal path. ---- */

/* ---- state 58 = GME type 17 ("sequential lesson steps", ctx 0x16c @0x081dad8c) ---- */
/* 0x080894fc game58_entry / 0x0808887c game58_ctx_reset / 0x08089ce0 game58_teardown:
   statechart state 58 = GME game type 17. Entry template; ctx_reset also stamps sys_get_tick
   into ctx+0x18 (the per-game RNG seed advanced once per EA call). */
/* 0x08088910 game58_record_load: type->ctx+1, subgameCount->ctx+2, rounds->ctx+3 (unused),
   c/earlyRounds discarded, repeatOID->ctx+0x130, x/w/v discarded; 5 game PLLs -> ctx+0x1c
   (start) / +0x24 (roundEnd, unused) / +0x20 (finish) / +0x28 / +0x2c (roundStart pair,
   unused); up to 0x40 subgame offsets -> ctx+0x30[]; target scores / finish PLLs NOT read. */
/* 0x08088b70 game58_subgame_parse: u0..u5 -> ctx+8..0xd (only u4=ctx+0xc is read back: the
   OID-list-3 wrong-tap limit), u6-u9 discarded; list-1 pos->ctx+0x13c cnt->0x138, list-2
   pos->0x144 cnt->0x140 (NEVER matched - no decoy logic), list-3 pos->0x14c cnt->0x148;
   subgame PLL 1..8 -> ctx+0x134/0x150/0x158/0x160/0x154/0x15c/0x164/0x168. */
/* 0x08088eac game58_match_target_oids / 0x08089118 game58_match_other_oids: scan subgame
   list-1 (-> ctx+7=1) / list-3 (-> return 1) against the tapped OID akoidpara+4. */
/* 0x080891b4 game58_match_special_pair: HARDWIRED two-stage special sequence - while
   ctx+0x14<2 only OID 0x11EA matches, afterwards only 0x11EB; each hit sets ctx+7=1,
   ctx+0x14++, resets the wrong counter. A per-book weld (OIDs 4586/4587). */
/* 0x08088f54 game58_play_phase_cue: resolve ALL entries of playlist[idx] of the
   playlist-list at pll_off against the MAIN media table (hdr+0x04) into the shared arrays,
   play entry 0, leave the rest for the engine drain (count @0x081dac7c, cursor akoid+0xb1). */
/* 0x08089244 game58_phase_engine: subgames play SEQUENTIALLY (cursor ctx+0xe), no random
   pick. Subgame 0 runs in phases 1/2 with the special-pair matcher: each special hit -> PLL2
   idx 0, third hit (ctx+0x14==3) -> random PLL7 -> next subgame; later subgames run in phases
   4/5 with the normal list-1 matcher (hit -> random PLL7 -> next). Wrong taps: list-3 match
   -> ctx+6++ vs u4 -> random PLL6 (hint) or random PLL8 (give-up -> next subgame); no-list
   tap -> random PLL4. All subgames done -> phase 6: random finish PLL -> phase 0xff:
   gme_reset_registers + repost 0x100c (pop to book). No scoring at all. */
/* 0x08089558 game58_engine: EA[43], script-hybrid wrapper (see block comment): repeatOID
   (ctx+0x130) replays the current phase cue; a content tap advances phase 1->2 / 4->5 at the
   next audio boundary and runs its play script; also hosts a codec watchdog (audio idle +
   tick delta >0x32 -> codec_reinit/audio_state_reset, ticks @0x081da75c). */

/* ---- state 59 = GME type 18 ("scored quiz", ctx 0x130 @0x081dad90) ---- */
/* 0x0808adb0 game59_entry / 0x08089d58 game59_ctx_reset / 0x0808b680 game59_teardown:
   statechart state 59 = GME game type 18. */
/* 0x08089df8 game59_record_load: type discarded, subgameCount->ctx+1, rounds->ctx+2,
   c discarded, earlyRounds->ctx+0x14, repeatOID->ctx+0xb4, x/w/v discarded; 5 game PLLs ->
   +0x20 (start) / +0x28 (roundEnd, unused) / +0x24 (finish, unused) / +0x2c (roundStart) /
   +0x30 (laterRoundStart); up to 0x20 subgame offsets -> ctx+0x34[]; 10 target scores ->
   ctx+0xf0.. and 10 finish PLLs -> ctx+0x104.. (USED - full score cascade). */
/* 0x0808a28c game59_pick_unplayed_subgame: rand(subgameCount, seed ctx+0x1c) + played-mask
   walk (ctx+0x18, max 32). */
/* 0x0808a2ec game59_subgame_parse: u0..u5 -> ctx+7..0xc (u3=ctx+0xa read back), u6-u9
   discarded; lists 1/2/3 -> cnt/pos ctx+0xbc/0xc0, 0xc4/0xc8, 0xcc/0xd0; subgame PLL 1..8 ->
   ctx+0xb8/0xd4/0xdc/0xe4/0xd8/0xe0/0xe8/0xec. */
/* 0x0808a61c game59_match_target_oids / 0x0808a6bc game59_match_decoy_oids /
   0x0808a758 game59_match_other_oids: list-1 -> ctx+6=1; list-2 -> ctx+6=match index (0xff
   none); list-3 -> return 1. */
/* 0x0808a81c game59_play_phase_cue: DUAL-TABLE resolve - every entry of the chosen playlist
   against the main table (hdr+0x04, played now) AND the additional table (hdr+0x60, played on
   the second consecutive repeatOID tap); plays the whole playlist sequentially. */
/* 0x0808aa44 game59_phase_engine: phase 0: random subgame, PLL1 idx 0. Phase 2 (tap): list-1
   hit -> random PLL7, score ctx+0x13++, round over. Wrong taps are SILENT: in list-2 ->
   counted (ctx+4) vs u3 -> exhausted: random PLL8, round over scoreless; in list-3 but not
   list-2 -> ignored, uncounted; no-list tap -> random PLL4. Phase 3: rounds done -> phase 4 score cascade (score vs 10
   target scores -> random pick of finish PLL 1..10, dual-table resolve inline); else
   earlyRounds roundStart/laterRoundStart announce -> phase 0. Phase 4: reset + 0x100c. */
/* 0x0808ae10 game59_engine: EA[44], script-hybrid wrapper; second repeatOID tap plays the
   additional-table copy of the cue. */

/* ---- state 61 = GME type 19 ("beat-the-clock quiz", ctx 0x13c @0x081da9c0) ---- */
/* 0x0807a8e4 game61_entry / 0x08079884 game61_ctx_reset / 0x0807b120 game61_teardown:
   statechart state 61 = GME game type 19. */
/* 0x0807992c game61_record_load: type->ctx+1, subgameCount->ctx+2, rounds->ctx+3,
   c discarded, earlyRounds->ctx+0x1a, repeatOID->ctx+0xbc; 5 game PLLs -> +0x28 (start) /
   +0x30 (roundEnd = the once-per-second COUNTDOWN TICK cue) / +0x2c (finish, unused) /
   +0x34 (roundStart) / +0x38 (laterRoundStart); subgames -> ctx+0x3c[]; 10 target scores ->
   ctx+0x100.., 10 finish PLLs -> ctx+0x114.. (USED). */
/* 0x08079e4c game61_subgame_parse: u0..u4 -> ctx+0xf..0x13; the 6th header word u5 ->
   ctx+0x14 = PER-SUBGAME TIME LIMIT in heartbeat ticks x10 (u5*10; 0 -> default 0x32 = ~5s);
   remaining words discarded; lists 1/2/3 -> ctx+0xcc/0xd0, 0xd4/0xd8 (list-2 never matched),
   0xdc/0xe0; subgame PLL 1..8 -> ctx+0xc8/0xe4/0xec/0xf4/0xe8/0xf0/0xf8/0xfc, PLUS a 9th
   PLL offset -> ctx+0xc4 = the TIME-UP feedback cue (the only game type that reads PLL9). */
/* 0x08079dec game61_pick_unplayed_subgame: rand + played-mask ctx+8. */
/* 0x0807a1ac game61_match_target_oids / 0x0807a24c game61_match_other_oids: list-1 ->
   ctx+0xe=1; list-3 -> return 1. */
/* 0x0807a500 game61_read_pll_count: u16 playlist count at pll_off (also used by game63). */
/* 0x0807a2e0 game61_play_phase_cue: dual-table resolve (additional-table pass skipped while
   ctx+0xbf=1, i.e. for the countdown tick cue). */
/* 0x0807a530 game61_phase_engine: phase 0: random subgame, PLL1, arm timer (ctx+0x1c=0
   elapsed, ctx+0x20=1 running). Phase 2 (tap or timeout): elapsed>limit -> tap in list-3 ->
   PLL9 "time's up" else PLL4, round over scoreless; in time: list-1 hit -> random PLL2,
   score ctx+0x1b++, round over; list-3 wrong -> ctx+0xd++ vs u4 -> PLL6 hint (RESETS the
   clock) or PLL8 give-up; no-list -> PLL4 idx 1, clock keeps running. Phase 3: rounds left ->
   earlyRounds announce -> phase 0; done -> score cascade vs 10 tiers -> random finish PLL
   1..10. WELD: after a top-tier win with product==0xC and game record==0x16 it sets
   akoidpara->menu_unlock_dcf=1 (unlocks a locked game18 menu entry). Phase 4: reset+0x100c. */
/* 0x0807a940 game61_engine: EA[33], script-hybrid wrapper + the clock: on heartbeat with
   timer running and audio idle, ctx+0x1c++ and every 10th tick plays the countdown-tick cue
   (roundEnd PLL); elapsed>limit -> force phase 2. OID 0xF3D -> play the deferred cue
   (akoidpara->deferred_cue_pll_off) idx 1. Second consecutive repeatOID tap (flag ctx+0xbe)
   plays the additional-table copy. */

/* ---- state 62 = GME type 20 ("find-N-targets", ctx 0xf8 @0x081da9c4) ---- */
/* 0x0807bd68 game62_entry / 0x0807b198 game62_ctx_reset / 0x0807c5e4 game62_teardown:
   statechart state 62 = GME game type 20. */
/* 0x0807b240 game62_record_load: type discarded, subgameCount->ctx+1, rounds->ctx+2 (loaded,
   never compared - the game is SINGLE-ROUND), c discarded, earlyRounds->ctx+0x16 (unused),
   repeatOID->ctx+0xbc; 5 game PLLs -> +0x28 (start, unused) / +0x30 / +0x2c (finish = the
   round-complete cue) / +0x34 / +0x38; subgames -> ctx+0x3c[]; scores/finish PLLs NOT read. */
/* 0x0807b50c game62_subgame_parse: u0..u4 -> ctx+0xb..0xf; u5 -> ctx+0x10 AND ctx+0x19 =
   the NUMBER OF TARGET TAPS that completes the game; builds the list-1 availability bitmask
   ctx+0x1c; lists -> ctx+0xc4/0xc8, 0xcc/0xd0 (list-2 never matched), 0xd4/0xd8; subgame
   PLL 1..8 -> ctx+0xc0/0xdc/0xe4/0xec/0xe0/0xe8/0xf0/0xf4. */
/* 0x0807b4ac game62_pick_unplayed_subgame: rand + played-mask ctx+4. */
/* 0x0807b84c game62_match_target_oids: find-all matcher - hit -> ctx+0xa=1 (or 2 if that
   target's ctx+0x1c bit was already cleared), clears the bit. 0x0807b924
   game62_match_other_oids: list-3 -> return 1. */
/* 0x0807b9d4 game62_read_pll_count / 0x0807ba04 game62_play_phase_cue: dual-table twin. */
/* 0x0807bc10 game62_phase_engine: phase 0: ONE random subgame, PLL1. Phase 2 (tap): target
   hit (new or repeat) -> ctx+0x17++; reaching u5 hits -> random FINISH PLL (+0x2c) -> phase 3
   (game over); else random PLL2. Wrong: list-3 -> ctx+0x20++ vs u4 -> PLL6 idx 0 or PLL8
   idx 0 -> phase 3; no-list -> PLL4 idx 0. Phase 3: reset + repost 0x100c. No scoring. */
/* 0x0807bdc8 game62_engine: EA[34], script-hybrid wrapper (0xF3D deferred cue, dual-table
   repeat like game61). */

/* ---- state 63 = GME type 21 ("category ladder quiz", ctx 0xfc @0x081da9c8) ---- */
/* 0x0807d440 game63_entry / 0x0807c66c game63_ctx_reset / 0x0807dca4 game63_teardown:
   statechart state 63 = GME game type 21. */
/* 0x0807c718 game63_record_load: type discarded, subgameCount->ctx+1 (must be 13!),
   rounds->ctx+2 (loaded; the engine hardcodes 3 rounds), c discarded, earlyRounds->ctx+0x14,
   repeatOID->ctx+0xb8; 5 game PLLs -> +0x24 (start, unused) / +0x2c (roundEnd = the
   CATEGORY-INTRO playlist-list, played at playlist index = band #) / +0x28 (finish) /
   +0x30 / +0x34; subgames -> ctx+0x38[]; 10 target scores DISCARDED; only the first TWO
   finish-PLL words are kept -> ctx+0xf4 (round SUCCESS >=3 correct) / ctx+0xf8 (round FAIL). */
/* 0x0807c9e0 game63_pick_unplayed_band: pick a random unplayed one of FOUR hardwired
   subgame bands (mask ctx+0x17) -> ctx+0x15. 0x0807ca38 game63_pick_unplayed_subgame_in_band:
   pick a random unplayed subgame inside the band - bands are HARDWIRED slices 0-2 / 3-6 /
   7-9 / 10-12 of the subgame table (mask ctx+0x1c); returns 0 when the band is exhausted. */
/* 0x0807cbd0 game63_subgame_parse: u0..u5 -> ctx+7..0xc (u4=ctx+0xb read back); lists ->
   ctx+0xc0/0xc4, 0xc8/0xcc (list-2 never matched), 0xd0/0xd4; subgame PLL 1..8 ->
   ctx+0xbc/0xd8/0xe0/0xe8/0xdc/0xe4/0xec/0xf0. */
/* 0x0807cf08 game63_match_target_oids / 0x0807d1b4 game63_match_other_oids: list-1 ->
   ctx+6=1; list-3 -> return 1. */
/* 0x0807cfa8 game63_play_phase_cue: dual-table twin (second-repeat flag ctx+0x18). */
/* 0x0807d248 game63_phase_engine: 3 rounds (ctx+0x16). Phase 0: pick band, play its
   category-intro (roundEnd PLL @ index band#), ctx+0x13=0 correct-in-round. Phase 1: next
   subgame in band -> PLL1 idx 0 -> phase 2 (wait); band exhausted -> ctx+0x13>=3 ? random
   round-SUCCESS (+0xf4) : round-FAIL (+0xf8) cue -> phase 0. Phase 3 (tap): list-1 hit ->
   ctx+0x13++, random PLL2 -> phase 1; list-3 wrong -> ctx+5++ vs u4 -> PLL6 idx 0 (retry) or
   random PLL8 (skip subgame); no-list -> PLL4 idx 0 (retry). After 3 rounds -> phase 4:
   random finish PLL (+0x28) -> phase 5: reset + 0x100c. No score cascade. */
/* 0x0807d4a0 game63_actionq_drain: the script-hybrid action-queue walk + pending-cue drain
   of game63_engine, extracted as a helper; ALSO called by game66_engine (they share the
   global cue arrays - its phase-0xff/cancel write goes through the state-63 ctx pointer,
   harmless in state 66 only because the pending counters are then empty). */
/* 0x0807d888 game63_engine: EA[35], script-hybrid wrapper; a content tap in phase 2 cuts
   the question short (fwl_voice_stop_sync) and forces phase 3. */

/* ---- state 66 = GME type 22 ("scavenger hunt", ctx 0x4f4 @0x081da9cc) ---- */
/* 0x0807ebe0 game66_entry: statechart state 66 = GME game type 22. After the record load it
   PRE-DRAWS `rounds` (ctx+3) random subgames: for each it stores the subgame's FIRST list-1
   OID -> ctx+0x480[i] (the task list), its PLL1 -> ctx+0x490[i], and sets bit i of
   ctx+0x4b1. Uses the SHARED game_play_phase_cue (single-table) for all cues. */
/* 0x0807dd50 game66_record_load: type->ctx+1, subgameCount->ctx+2, rounds->ctx+3 (= number
   of pre-drawn tasks), c->ctx+4, earlyRounds->ctx+5 (both unused), repeatOID->ctx+6; 5 game
   PLLs -> +0x14 (start) / +0x1c (roundEnd = TIME-UP cue) / +0x18 (finish, unused) / +0x20 /
   +0x24; subgames -> ctx+0x28[]; 10 target scores -> ctx+0x4b6.., 10 finish PLLs ->
   ctx+0x4cc.. (USED). */
/* 0x0807e21c game66_pick_unplayed_subgame: rand + 128-bit played mask ctx+0x228[4]. */
/* 0x0807e2fc game66_subgame_parse: u0..u5 -> ctx+0x238..0x23d; lists -> ctx+0x248/0x450,
   0x44c/0x454, 0x458/0x45c; subgame PLL 1..8 -> ctx+0x240/0x460/0x468/0x470/0x464/0x46c/
   0x474/0x478. NOTE: the engine's feedback cues use whatever subgame was parsed LAST (the
   final pre-drawn one) - the per-task subgames are never re-parsed during play. */
/* 0x0807e644 game66_read_first_target_oid: read the first entry of the current subgame's
   list-1 (the task target). 0x0807e68c game66_match_drawn_targets: scan the PRE-DRAWN task
   list ctx+0x480[0..rounds): ctx+0xc = 1 new find (clears the ctx+0x4b1 bit) / 2 already
   found / 0 miss. */
/* 0x0807e700 game66_match_other_oids / 0x0807e7a0 game66_match_decoy_oids: list-3 -> return
   1; list-2 -> ctx+0xc = index or 0xff. */
/* 0x0807e848 game66_oid_alias_map: HARDWIRED OID merge for the type-22 book: 0x1A2B->0x1A2A,
   0x1A1F->0x1A20 (adjacent printed areas of the same target; product 0x13, cover 0x1A32). */
/* 0x0807e87c game66_read_pll_count: u16 playlist count at pll_off. */
/* 0x0807e8ac game66_phase_engine: phase 0: read out ALL pre-drawn task cues (ctx+0x490[i],
   cursor ctx+0x4b0) in sequence -> phase 5: arm the 200-tick (~20s) inactivity timer
   (ctx+0x4b2 flag / ctx+0x4b4 counter, run by the EA) -> phase 1. Phase 2 (tap): alias-map,
   match drawn targets: new find -> PLL2, score ctx+8++; already -> PLL5; all found
   (ctx+0x4b1==0) -> phase 3; wrong: list-2 -> u3-limited PLL3 then PLL8 -> phase 3; list-3 ->
   u4-limited PLL6 then PLL8 -> phase 3; no-list -> PLL4. Phase 4 (timeout): play the TIME-UP
   cue (roundEnd PLL) -> phase 3. Phase 3: score cascade vs 10 tiers -> finish PLL 1..10
   idx 0; TOP TIER with product!=0x13 also sets akoidpara->menu_unlock_dde=1; product==0x13
   plays finish PLL 1 at a random index instead. Phase 0xff: reset + 0x100c. */
/* 0x0807ecac game66_engine / 0x0807ef54 game66_teardown: EA[36] script-hybrid wrapper (runs
   the inactivity timer; product 0x13 gets a reminder cue every 10 ticks) / exit. */

/* ---- state 68 = GME type 23 ("find-everything percentage game", ctx 0x4c0 @0x081da9d0) ---- */
/* 0x0807fcbc game68_entry / 0x08080100 game68_teardown: statechart state 68 = GME game type
   23 (no ctx_reset fn - entry memset0s the ctx; keeps a dbg print, "Repeat Addr"). Uses the
   SHARED game_play_phase_cue. */
/* 0x0807f004 game68_record_load: type->ctx+1, subgameCount->ctx+2, rounds->ctx+3, c->ctx+4,
   earlyRounds->ctx+5 (all three unused), repeatOID->ctx+6; 5 game PLLs -> +0x14 (start) /
   +0x1c / +0x18 (finish = the 100% cue) / +0x20 / +0x24; subgames -> ctx+0x28[]; 10 target
   scores -> ctx+0x484.. (loaded, NEVER read - the engine uses fixed percentages), 10 finish
   PLLs -> ctx+0x498.. (only 1..3 used). */
/* 0x0807f4d0 game68_subgame_parse: u0..u5 -> ctx+0x238..0x23d (none read back!); builds the
   list-1 availability bitmask ctx+0x480; lists -> ctx+0x248/0x450, 0x44c/0x454 (list-2 never
   matched), 0x458/0x45c; subgame PLL 1..8 -> ctx+0x240/0x460/0x468/0x470/0x464/0x46c/0x474/
   0x478. */
/* 0x0807f82c game68_match_target_oids: find-all matcher over list-1 with mask ctx+0x480
   (ctx+0xc = 1 new / 2 already). 0x0807f904 game68_match_other_oids: list-3 -> return 1
   (was misnamed study_repeat_addr by the old string harvest). */
/* 0x0807f9a0 game68_oid_alias_map(oid, subgame_idx): HARDWIRED per-subgame OID merge table
   over the 0x1B54..0x1B70 range - e.g. subgame 0: {1B5A,1B60,1B65,1B6B}->1B5A,
   {1B6C,1B6D,1B6E,1B55}->1B55, {1B64,1B6F}->1B64, {1B54,1B70}->1B54; subgame 3 adds
   {1B62,1B5B}->1B5B; subgame 4 only the 1B55 group. Several printed areas = one target. */
/* 0x0807faec game68_phase_engine: SINGLE round. Phase 0: random subgame (no played mask),
   PLL1. Phase 2 (tap): alias-map, find-all match: new find -> PLL2, ctx+8++; all bits clear
   (or 6 finds in subgame 0) -> phase 3; already -> PLL5; a list-3 tap ENDS the round with
   PLL6; no-list -> PLL4, retry. Phase 3 (result): found==count (or the subgame-0 cap) ->
   game finish PLL (+0x18); else fixed percentage tiers: >75% -> finish PLL1, 50..75% ->
   finish PLL2, <50% -> finish PLL3 (all idx 0). Phase 4->5: reset + 0x100c. */
/* 0x0807fd5c game68_engine: EA[37] script-hybrid wrapper (repeatOID replays via
   game_play_phase_cue; "Repeat Addr" debug print). */

/* 0x08088800 game19_teardown: state-19 (GME type 16) desc+4 exit - stop audio, physpool_free,
   clear the shared cue arrays, clear the in-game mode byte (template twin of the other
   gameNN_teardown fns; named late because exits are only reachable via the descriptor table). */

/* session-3 prototypes: cue players are (pll_off, playlist_idx); engines take the event id. */
void game58_play_phase_cue(unsigned int pll_off, unsigned int playlist_idx);
void game59_play_phase_cue(unsigned int pll_off, unsigned int playlist_idx);
void game61_play_phase_cue(unsigned int pll_off, unsigned int playlist_idx);
void game62_play_phase_cue(unsigned int pll_off, unsigned int playlist_idx);
void game63_play_phase_cue(unsigned int pll_off, unsigned int playlist_idx);
unsigned int game58_read_pll_count(unsigned int pll_off);
unsigned int game59_read_pll_count(unsigned int pll_off);
unsigned int game61_read_pll_count(unsigned int pll_off);
unsigned int game62_read_pll_count(unsigned int pll_off);
unsigned int game66_read_pll_count(unsigned int pll_off);
unsigned int game66_read_first_target_oid(void);
void game66_match_drawn_targets(unsigned short oid);
int game66_match_other_oids(unsigned int oid);
void game66_match_decoy_oids(unsigned int oid);
unsigned int game66_oid_alias_map(unsigned int oid);
int game68_oid_alias_map(int oid, int subgame_idx);
void game68_match_target_oids(unsigned int oid);
unsigned int game58_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game59_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game61_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game62_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game63_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game66_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);
unsigned int game68_engine(GmeEvent event_id, unsigned char *event_arg, int a3, unsigned int a4);

/* ---- session-3 @local commits (key scan variables in the un-prototyped matchers) ---- */
/* @local 0x08089118 =uVar4 scan_idx
 * @local 0x0807a1ac =uVar4 scan_idx
 * @local 0x0807b84c =uVar4 scan_idx
 */

/* session-3 fixups: standalone docstrings for members that were mid-text in grouped blocks. */
/* 0x08089214 game58_read_pll_count: u16 playlist count at pll_off (state-58 twin). */
/* 0x0808a7ec game59_read_pll_count: u16 playlist count at pll_off (state-59 twin). */
/* 0x0807b924 game62_match_other_oids: scan subgame OID-list 3 vs the tapped OID -> 1/0. */
/* 0x0807ca38 game63_pick_unplayed_subgame_in_band: pick a random unplayed subgame inside the
   current band - bands are HARDWIRED subgame-table slices 0-2 / 3-6 / 7-9 / 10-12 (played
   mask ctx+0x1c); returns 0 when the band is exhausted. */
/* 0x0807e68c game66_match_drawn_targets: scan the PRE-DRAWN task list ctx+0x480[0..rounds):
   ctx+0xc = 1 new find (clears the ctx+0x4b1 bit) / 2 already found / 0 miss. */
/* 0x0807f904 game68_match_other_oids: scan subgame OID-list 3 vs the tapped OID -> 1/0
   (was misnamed study_repeat_addr by the old string harvest). */
