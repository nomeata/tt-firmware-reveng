# Settings & config — what the 2N (MT) pen persists, where, and how

> Static decomp analysis of the settings/config subsystem (unified base **0x08009000**). Evidence
> tags: **[Proven]** = read from decomp/disasm/literal pools/hardware capture, **[Inferred]** = inferred (reason
> given). Companions: `pmu-power-management.md` (buttons/auto-off), `statechart-full-map.md` (states),
> `book-discovery-and-load.md` (`.lst` cache, language check), `codepage-what-is-it.md` (GBK),
> `audio-output-hw.md` (audio ring / Q10 gain), `hardware-external-refs.md` (FM24C02B).

---

## 0. Executive summary

1. **The pen has almost no user settings.** The only user-adjustable setting is **volume**
   (buttons → index 0..5), and it is **RAM-only — reset to 3 at every boot**. Language is the
   compile-time constant `"GERMAN"`; codepage is fixed GBK; the ~30 s standby auto-off is a
   **hardcoded literal 300**; there is **no settings menu state** anywhere in the 70-state chart. [Proven]
2. What the pen *does* persist across power cycles (all as **FAT files**, written only on the
   **graceful power-off path**, read at splash/book entry):
   - **`A:/SYSTEM/profile.dat`** (0x21B0 bytes, XOR-checksummed records) — of which this firmware
     uses exactly **one record (@0xC): {power-off tick, tap-log counter, 1 byte}**. [Proven]
   - **`B:/Questionstatus.txt`** — a 9×256 digit matrix of quiz "question already asked" flags. [Proven]
   - **`B:/FLAG.bin`** — the firmware-update resume marker (presence = flag). [Proven]
   - Append-only telemetry: **`A:/Firmware log file.bin`** + **`B:/.tiptoi.log`** (header =
     serial + fw version + `"GERMAN"` + `"3202N"`, then (product-id, OID) u16 pairs per tap). [Proven]
3. **The FM24C02B EEPROM stores no pen settings.** Every I²C transaction in the firmware targets
   device **0x94** (the dormant Anyka on-SoC sensor config); no 0xA0/0xA1 (24C02) traffic exists.
   The EEPROM hangs off the Sonix OID decoder's private bus (decoder config), invisible to the SoC. [Proven]

---

## 1. Settings inventory (Q1)

| setting | runtime location | range / default | persisted? | changed by |
|---|---|---|---|---|
| **Volume (master)** | u8 index `@0x081db904` | 0..5 via buttons (table has 7 slots 0..6); **boot default 3**; healthy-boot splash sets 2 | **NO** — RAM only | buttons (0x105F codes 5/6), GME opcodes 0xFEE0–0xFEE7, firmware itself |
| Volume (secondary, per-player) | `game_ctx+0x11` → player`+0x2b` | 0..0x10; **boot default 8** | NO | `aud_player_set_volume` 0x080abb60 — only static caller is `app_init_main` |
| **Language** | `g_pen_language` `@0x08121ec0` = `"GERMAN"` | compile-time `.data` constant | n/a (constant) | **never written** — all 5 literal refs are reads (gme_check_language + 4 log-header copies) [Proven] |
| Codepage | G0 selector = 0 → GBK | fixed | n/a | see `codepage-what-is-it.md` (no writer) |
| **Auto-off timeout** | u16 idle counter `@0x081da07c` | threshold = **literal `300`** in `standby_handler` 0x08051b0c | **NO** — hardcoded, not configurable | — |
| Quiz question-status | `game_subctx+0x4bc` (9×0x100 bytes) | 0/1 flags per question | **YES** → `B:/Questionstatus.txt` | quiz games (0x0809d3c4 marks, 0x0809d858 clears a row) |
| Update-resume flag | `game_ctx+0x24` | 0/1 | **YES** → presence of `B:/FLAG.bin` | fw-update writes it; splash deletes it + sets flag |
| Power-off tick | `game_subctx+0xcc` | u32 sys tick | **YES** → `profile.dat@0xC+0` | saved at graceful off, restored at splash |
| Tap-log counter | `game_subctx+0x13c` | u32 | **YES** → `profile.dat@0xC+8` | `cover_oid_classifier` 0x08037cec increments per tap; zeroed when `A:/Firmware log file.bin` is recreated (`standby_entry`) |
| (unknown byte) | `game_subctx+0x140` | u8 | YES → `profile.dat@0xC+0xC` | write-only in this build — no other reader found [P absence] |

`game_ctx` = 0x080089A4; `game_subctx` = `*(game_ctx+0x20)` (the big akoid/game context).

**Not settings** (checked and ruled out): `A:/calendar.dat` (reader `FUN_08050170` has **no static
caller** — dead vendor calendar), `a:/*.lst` (per-boot caches), `A:/African.fen` (opened and
**deleted** at every book entry, `FUN_08034588` ← book entry 0x080345cc — vestigial cleanup),
`B:/spotlight.bin` (prodtest checksum artifact, healthy-boot path only), `B:/LanguageInfoMT.txt`
(lists the languages contained in the **update package**; used only by `fw_update_cleanup` to pick
the update-voice folder `A:/Language/<lang>/…` by matching the `.upd` filename — not a pen setting),
`B:/App_Demo.bin` (path handed to GME separate-binary sysapi), `B:/Prodtest.txt`, `B:/TestFile/*`
(production test). [Proven]

---

## 2. Storage — where and how (Q2)

### 2.1 Verdict: FAT files on A:/B: only. No EEPROM, no raw-NAND settings area. [Proven]

- **EEPROM:** the bit-banged I²C library (`i2c_gpio_init` 0x080f355c, `i2c_start` 0x080f34e8,
  `i2c_send_byte` 0x080f3460, `i2c_write_bytes` 0x080f35b4, read fns 0x080f37c8/0x080f3754/
  0x080f38b8) has exactly seven call sites with a device address, **all `0x94`** (the Anyka
  on-SoC sensor config, dormant on this pen — `akoid_sensor_config_i2c` 0x080ef180 runs only for
  sensor types 1/3; this build is type 0). No caller passes 0xA0/0xA1. The FM24C02B belongs to the
  SN9P601 OID decoder's own 2-wire bus (its boot config), never to the SoC. [P corpus grep]
- **NAND:** no settings block outside the FAT volumes; everything below goes through `fs_open`/
  `fs_read`/`fs_file_write` on drive letters. [Proven]

### 2.2 `A:/SYSTEM/profile.dat` — the vendor profile store (0x21B0 bytes)

Module: `profile_read_record` 0x0804bb20 / `profile_write_record` 0x0804b8cc /
`profile_init_defaults` 0x080391e4. Path strings: `A:/SYSTEM/profile.dat` (UTF-16 @0x080affd4),
`A:/SYSTEM/` (@0x0803c9f0). [P pools 0x0804becc/d4]

**Self-healing container:** `profile_write_record` opens the file; if missing **or size ≠ 0x21B0**
it ensures `A:/SYSTEM/` exists (`FUN_080ad6d8`), recreates the file and writes the full 0x21B0
default image from `profile_init_defaults`. `profile_read_record` does the same on open failure /
wrong size, and **re-defaults + rewrites the single record when its XOR checksum fails**. [Proven]

Record map (file offset → length; each record ends in an XOR-of-fields checksum byte):

| offset | len | content (defaults) | used in this firmware? |
|---|---|---|---|
| 0x000 | 0x0C | packed date/time fields derived from the fw-version word block (`+0x50/+0x64` of the `N0038MT` header @0x08009000), cksum @+4 | default-written only |
| **0x00C** | **0x910** | **+0 u32 `sys_get_tick()` at write; +4 u8 cksum(tick); +8 u32 tap-log counter (`subctx+0x13c`); +0xC u8 `subctx+0x140`; rest unused** | **YES — the only live record** |
| 0x91C | 0x414 | two path strings (default `L"B:/"`), 3 mode bytes {1,4,5} @+0x410 | default-written only |
| 0xD30 | 0xAC | u32 0x052F83C0, u32 0x0487AB00 (≈ vendor rate/date constants), bytes {10,0,0xFF}, 0x28-word table | default-written only |
| 0xC00 | 0xF68 | 0x352-entry u32 array = 0xFFFFFFFF, `L"B:/"` @+0xD4C, mode bytes @+0xF5C..F61 {0xFF,0,0,0,0,1} | default-written only |
| 0x1D44 / 0x1D60 / 0x1D7C | 0x1C ×3 | play-mode-ish byte sets `{+0xF:8, +0x10:0x14, +0x11:2, +0x12:5, +0x13:1}` | default-written only |
| 0x1D98 | 0x20C | path `L"B:/IMAGE"` + zeroed second path | default-written only |

This is a generic media-player SDK profile (repeat/EQ/play-path records for products with menus);
tiptoi uses only the @0xC record. **Static callers of the pair: exactly two.** [P corpus grep]

- **Read at boot:** `FUN_0804be24` ← `splash_entry` 0x0804c1d4 (before the battery gate, so it
  runs on every boot incl. the quiet low-battery path): `profile_read_record(0xC)` →
  `subctx+0xcc := saved tick`, `subctx+0x13c := saved counter`, `subctx+0x140 := saved byte`; if
  `saved_tick > current_tick` it converts the saved tick to a calendar date (`FUN_080ad188` —
  div 60/60/24 + leap-year day walk) into a stack buffer whose result is **discarded** (vestigial
  RTC continuation). [Proven]
- **Write at power-off:** `FUN_0804be7c` ← `power_off_or_charge_park` 0x08050a8c (the graceful
  path: power button code 8 / battery-final 0x1062 route). **The standby auto-off and `sys_reset`
  paths do NOT save** — they go straight to GPIO15=0. [Proven]

### 2.3 `B:/Questionstatus.txt` — quiz progress (persisted game state)

- Format: 9 rows × 256 chars `'0'`/`'1'` (with 2 separator bytes after every 32), ASCII. [Proven]
- **Read at book entry:** `FUN_0804b708` (path @0x080b0012) ← book(13) entry 0x080345cc; fills
  `subctx+0x4bc + row*0x100 + col` with 0/1. Missing file → returns 0, matrix stays zeroed. [Proven]
- **Write at graceful power-off:** `FUN_0804b7d4` (its own path copy @0x080b003e) ←
  `power_off_or_charge_park` — deletes and rewrites the whole matrix. [Proven]
- Consumers: the ask-question quiz family — `0x0809d3c4` skips/marks used questions
  (`==1` loop → set 1), `0x0809d858` clears a row when exhausted. [Proven]

### 2.4 The telemetry log files (identity header, not settings)

`standby_entry` 0x080511a0 opens both at every standby entry (handles kept open globally;
confirmed valid on a hardware RAM capture [Proven]):

| file | path global | handle global |
|---|---|---|
| `A:/Firmware log file.bin` | wide str @0x08121e70 | @0x08121a84 |
| `B:/.tiptoi.log` | wide str @0x08121ea2 | @0x08121e6c |

On create (or when the stored identity mismatches), it writes a 0x40-byte header of four
0x10-byte fields (payload padded with 0xFF): **+0x00 pen serial (8 B, `serial_is_valid`),
+0x10 fw version `"N0038MT"` (10 B, from the PROG header @0x08009000), +0x20 language `"GERMAN"`
(10 B, from @0x08121ec0), +0x30 `"3202N"` (10 B, @0x080b0828)**. On open it validates serial
(8 B @0) + fw version (5 B @0x10) and recreates on mismatch — i.e. the log resets when the
firmware is updated or the file moved between pens. [Proven]

Body: every decoded tap appends **u16 current-product-id (`*0x081da08c`) + u16 OID** via
`FUN_0803444c` (A: log) / `FUN_080344b8` (B: log); the record counter is `subctx+0x13c`
(the value persisted in `profile.dat`, `% 30` bookkeeping in the writers). [Proven]

### 2.5 `B:/FLAG.bin` — update-resume marker

Created by the fw-update path (ref 0x08052298 in the state-2 machinery); at `splash_entry`
(ref 0x0804c244): if it exists → close, **delete**, set `game_ctx+0x24 = 1`, play voice 0x19;
`standby_handler` sees `+0x24 == 1` and posts 0x1058 (auto-reopen book after an update). Presence
*is* the value. [Proven]

---

## 3. Volume — the full path (Q3)

### 3.1 Button → index

Buttons arrive as **event 0x105F {code, sub}** and are handled *globally* by state-9's
TA[4] = `button_dispatch_105f` 0x0800a9c8 (works in every state, no menu needed):

| code | sub | action |
|---|---|---|
| 5 | 0 | `volume_up` 0x0800b6b4: `if (idx < 5) idx++` — **user max = 5** |
| 6 | 0 | `volume_down` 0x0800b6e0: `if (idx > 0) idx--` |
| 8 | 1 | power off (voice 0x14 → 0x1062 → off; see pmu doc §6.2) |

Feedback (when idle enough — a chain of "nothing critical playing" gates): stops current audio,
then plays **voice 0x15** (normal blip) or **voice 0x16** when the limit was hit (idx==5 after up,
idx==0 after down), and sets `subctx+0x54=1` so held/repeated presses keep beeping. [Proven]

### 3.2 Index → DAC gain

`volume_get` 0x0800b6a4 reads **u8 `@0x081db904`**. The setter everything funnels through is
**`sm_set_sound_profile(level, mode)` 0x0800b65c** (also labelled `sm_set_state` in the statechart
docs): clamp level to [0,6], look up **u16 gain table @0x0800b9e4 = {0x18, 0x60, 0xA8, 0x108, 0x138, 0x180, 0}**,
call `FUN_0802265c(gain)`, store the level byte at 0x081db904. [P, table bytes read from PROG.bin]

`FUN_0802265c` (gain commit): optionally rescales by the current sample-rate state
(`@0x08122710`), clamps to **0x400 = unity**, and writes the result into the **audio-output ring
singleton** `*0x08008d2c` (object 0x08008d30) fields **+0x14** (and +0x18 unless a fade flag
+0x27 is set) — the **Q10 volume multiplier** applied per sample by `ao_mix_write_ring`
(`sample*vol >> 10`, see `audio-output-hw.md`). So user volume 5 = 0x180/0x400 = 37.5 % of
unity; default 3 = 0x108/0x400 ≈ 26 %; slot 6 (gain 0) = true mute, reachable **only** via GME. [Proven]

### 3.3 Who sets it when

| caller | value | when |
|---|---|---|
| `app_init_main` 0x08038f5c | `sm_set_sound_profile(3,0)` + `aud_player_set_volume(8)` | **boot defaults** |
| `splash_entry` 0x0804c1d4 | `(2,0)` | healthy-boot (battery comparators 4×OK) path only — quieter jingle/prodtest |
| `button_dispatch_105f` | ±1 | user buttons |
| `gme_exec_command` 0x08034da0 | **opcodes 0xFEE0..0xFEE7 → profile 0..7** (clamped 6) | GME script content can set/mute volume |
| `standby_handler` auto-off / `power_off` 0x080508f8 | `(0,0)` | quietest before killing the amp |
| `aud_player_play` 0x08032c94 → `FUN_0803249c` | re-applies player`+0x29` | every playback start |

The per-player copy: `aud_player_construct` 0x080ab47c and `play_media_setup` 0x080ab6ac do
`aud_player_reset(player, volume_get(), 0, game_ctx+0x11)` → player`+0x29` = master index,
player`+0x2b` = the secondary 0..0x10 volume (default 8; `aud_player_set_volume` 0x080abb60 is its
only writer and has no caller besides `app_init_main`). Because **every** playback goes through
`play_media_setup` (e.g. `fwl_play_voice_by_id` 0x080ab9ac → `play_media_setup`), which re-caches
the *current* `volume_get()` before `aud_player_play`, a button/GME volume change sticks across
subsequent playbacks — the `+0x29` cache never resurrects a stale value. [Proven]

Side note on the same dispatcher: the **power-off button** branch (code 8, sub 1) is gated on the
boot-phase byte `game_ctx+0x1d ∈ {2, 4}`. `+0x1d` is a generic phase marker (splash entry =1, splash
healthy path / book entry / standby entry =2, 0x0805102c =8), **not** a healthy-boot discriminator.
The volume branches are not phase-gated. [Proven]

**No persistence:** the four literal-pool references to 0x081db904 are all inside the volume
module (0x0800b65c/6a4/6b4/6e0) — nothing saves or restores it. Power-cycle resets volume to 3. [Proven]

### 3.4 Live-pen RAM cross-check

A hardware RAM capture (low working RAM and the .bss config region; not included in this
repository) is consistent with the static model on every settings-relevant field:

- **Volume index = 3**, and the audio-output ring's Q10 gain (0x08008d30 +0x14/+0x18) equals
  gain-table[3] — self-consistent with the index, confirming the gain table and commit path on real
  hardware.
- **Secondary (per-player) volume = 8** — the boot default, never changed (matches "no other caller
  than `app_init_main`").
- **`game_ctx+0x24` (update-resume flag) = 1** and a product (id 42) was loaded — i.e. the capture was
  taken on a post-update boot where `B:/FLAG.bin` existed and a book was active.
- **`game_ctx+0x1e` = 0** (no USB at splash); the idle heartbeat counter was well below the 300
  auto-off threshold (pen active).

**Is the live volume 3 a saved value?** No — nothing persists it. 3 equals the `app_init_main` boot
default. Since the healthy splash path overrides to 2, the observed 3 means either (a) the quiet
low-battery boot path ran (splash `(2,0)` skipped, default 3 survives), or (b) a healthy boot
followed by one volume-up press (2→3) / a GME 0xFEE3 opcode. RAM alone cannot discriminate (a) vs
(b), but in every case the value is session-local, **not restored from storage**. [Proven]

---

## 4. Settings-menu statechart states (Q4)

**There are none.** All 70 states are mapped in `statechart-full-map.md`; none is a settings/menu
mode. Volume and power are handled *orthogonally* by the state-9 global pre-handler
(`0x105F → TA[4] button_dispatch_105f`) in whatever state is active. Language/codepage have no
selector UI (constants), auto-off has no UI (literal 300). The UTF-16 resource strings
`"MenuSetting"` (0x080b3cce, 0x080c8f36), `"Settings"` (0x080b4792), `"Configuración…"`
(0x080d2b02) have **zero literal-pool references** — dead vendor SDK menu resources from the
media-player lineage (same lineage as the unused profile.dat records and the dead calendar). [Proven]

The only "settings-like" OIDs are GME content: control opcodes 0xFEE0–0xFEE7 (volume) inside
scripts, i.e. printed volume-control fields in books work by script, not by a firmware menu. [Proven]

---

## 5. Proposed names / docstrings

| addr | proposed name | docstring sketch |
|---|---|---|
| 0x0804bb20 / 0x0804b8cc | keep `profile_read_record` / `profile_write_record` | + "A:/SYSTEM/profile.dat 0x21B0; self-healing (size+XOR cksum → re-default); only record @0xC live: {tick, cksum, tap-counter, byte}" |
| 0x080391e4 | keep `profile_init_defaults` | + "vendor media-SDK profile defaults; records 0/0x91C/0xC00/0xD30/0x1D44../0x1D98 written as defaults, never read back in this build" |
| 0x0804be24 | `profile_restore_at_splash` | read rec@0xC → subctx+0xcc/+0x13c/+0x140; discarded date conversion if tick regressed |
| 0x0804be7c | `profile_save_at_poweroff` | write rec@0xC {tick, subctx+0x13c, subctx+0x140}; only caller: power_off_or_charge_park |
| 0x0804b708 | `questionstatus_load` | B:/Questionstatus.txt → subctx+0x4bc 9×0x100 '0'/'1' matrix; ← book(13) entry |
| 0x0804b7d4 | `questionstatus_save` | delete+rewrite matrix; ← power_off_or_charge_park |
| 0x0804ff04 / 0x080500c4 / 0x08050170 / 0x08050378 | `calendar_*` (dead) | A:/calendar.dat lookup — no static caller |
| 0x08034588 | `book_delete_african_fen` | opens A:/African.fen, deletes if present; vestigial; ← book entry |
| 0x0803444c / 0x080344b8 | `taplog_append_A` / `taplog_append_B` | write {u16 product-id @0x081da08c, u16 OID} to the A:/B: log handles @0x08121a84/0x08121e6c |
| 0x08052508 | keep `lang_get_num` | parse B:/LanguageInfoMT.txt: count + per-language {voice-name, upd-filename}; used to pick A:/Language/<lang> update voices |
| 0x0800b65c | keep `sm_set_sound_profile` | + "volume commit: gains {0x18,0x60,0xA8,0x108,0x138,0x180,0} → FUN_0802265c → ring 0x08008d30+0x14 (Q10, 0x400=unity)" |
| 0x0802265c | `audio_gain_commit` | rate-rescale, clamp 0x400, store ring+0x14/+0x18 |
| 0x0803249c | `aud_player_apply_volume` | sm_set_sound_profile(player+0x29); ← aud_player_play |
| 0x0803258c | keep `aud_player_reset` | (vol_idx, ?, secondary_vol) → player+0x29/2a/2b |
| 0x080abb60 | keep `aud_player_set_volume` | secondary 0..0x10 → game_ctx+0x11 + player+0x2b; only caller app_init_main(8) |
| 0x080ad6d8 | `fs_dir_ensure` | mkdir-if-missing (used for A:/SYSTEM/) |
| globals | | `g_volume_idx` @0x081db904 (u8 0..6, RAM-only); `g_pen_language` @0x08121ec0 (`"GERMAN"`, const); `g_taplog_handle_A` @0x08121a84; `g_taplog_handle_B` @0x08121e6c; `subctx+0xcc` last-off tick; `subctx+0x13c` tap-log counter; `subctx+0x140` persisted byte (unknown, write-only); `subctx+0x4bc` question matrix; `subctx+0x54` vol-feedback latch; `game_ctx+0x11` secondary volume; `game_ctx+0x24` update-resume flag |

Voice IDs: 0x15 volume blip, 0x16 volume limit, 0x19 update-resume, 0x14 power-off (pmu doc). [Proven]

---

## 6. Evidence index

| claim | status | source |
|---|---|---|
| volume idx @0x081db904, 4 refs all in volume module; no persistence | **P** | xref to 081db904 → 0x0800b688/b6a4/b6b4/b6e0 only |
| live pen: vol idx 3 + ring gain 0x0108 = table[3]; game_ctx {+0x11=8, +0x24=1, +0x1e=0}; prod id 42 | **P** | hardware RAM capture |
| every playback re-caches volume_get() (no stale +0x29 resurrection); complete sm_set_sound_profile caller set = {app_init 3, splash 2, GME FEE, apply +0x29, power_off 0, standby-off 0} | **P** | corpus grep sm_set_sound_profile → exactly 0x08038f5c/0x0804c1d4/0x08034da0/0x0803249c/0x080508f8/0x08051b0c |
| power-off button gated on game_ctx+0x1d ∈ {2,4}; +0x1d written 1/2/2/2/8 by splash/splash-healthy/book/standby/0x0805102c | **P** | `0x0800a9c8`, `0x0804c1d4`, `0x080345cc`, `0x080511a0`, `0x0805102c` |
| gain table {0x18,0x60,0xA8,0x108,0x138,0x180,0} @0x0800b9e4; commit → ring+0x14 Q10 clamp 0x400 | **P** | PROG bytes; `0x0800b65c`, `0x0802265c` (obj `*0x08008d2c`), audio-output-hw doc |
| buttons: 0x105F code 5/6 up/down (max 5), voices 0x15/0x16; code 8 sub 1 power | **P** | `0x0800a9c8` |
| GME opcodes 0xFEE0–0xFEE7 → profile 0..7 | **P** | `0x08034da0` |
| boot defaults: profile 3 + player-vol 8 (`app_init_main`); splash 2 on healthy path only | **P** | `0x08038f5c`, `0x0804c1d4` |
| per-play re-apply via player+0x29 (`volume_get()` copy) | **P** | `0x080ab47c`, `0x080ab6ac`, `0x08032c94`, `0x0803249c` |
| language `"GERMAN"` @0x08121ec0 never written; live pen still `"GERMAN"` | **P** | xref (5 read sites); hardware RAM capture |
| auto-off literal 300, not configurable | **P** | `0x08051b0c` (`if (300 < *puVar1)`) |
| profile.dat path/size/records/checksums; only rec@0xC used; read splash / write graceful-off | **P** | `0x0804b8cc`, `0x0804bb20`, `0x080391e4`, `0x0804be24`, `0x0804be7c`; callers grep |
| Questionstatus.txt read at book entry / write at graceful off; matrix @subctx+0x4bc | **P** | `0x0804b708`, `0x0804b7d4`, `0x080345cc`, `0x08050a8c`, `0x0809d3c4`, `0x0809d858` |
| auto-off path does NOT save profile/questionstatus | **P** | `0x08051b0c` off-sequence, `0x080508f8` — no FUN_0804be7c/b7d4 calls |
| log-file header {serial, "N0038MT", "GERMAN", "3202N"}; tap records via 0x0803444c/0x080344b8 | **P** | `0x080511a0`; `0x0803444c`, `0x080344b8` |
| no I²C beyond dev 0x94; EEPROM unreachable from SoC | **P** | corpus grep i2c users; `0x080ef180` (dormant); hardware-external-refs §1/§4 |
| calendar.dat dead; African.fen deleted at book entry; menu strings unreferenced | **P** | caller greps; xref to 080b3cce/080b4792 (empty) |
| FLAG.bin create @update / delete+flag @splash | **P** | xref to 08121a68 → 0x0804c244 (splash), 0x08052298 (update); `0x0804c1d4` |
| `subctx+0x140` semantics | **Open** | persisted but write-only in this build |
| LanguageInfoMT.txt = update-voice language list keyed by .upd filename | **P** | `0x08052508`, `0x08052768`, `0x080523b4` |
