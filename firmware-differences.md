# Cross-generation firmware behavior differences

User-visible / behavioral differences between the tiptoi pen firmware generations, backed by
concrete evidence (addresses and retained strings). Each section cites the firmwares involved.
Firmwares: **ZC3201** (first-gen, build v0136/120117, PROG @0x08000000) vs the flagship
**2N-update3202MT** (PROG runtime base @0x08009000) and its sibling **2N-Update3202** (legacy
base @0x08000000). Addresses are that build's own. Only evidence-backed differences are logged.

---

## 1. OID -> voice: two-file content model (first-gen) vs single-.gme interpreter (2N)

The first-gen pen packages audio in **two** files and resolves an OID to a voice by indexing a
compiled table, whereas the 2N drives everything through the generic scripted GME interpreter.

- **ZC3201**: `game_play_oid_voice` @0x08054730 indexes the vendor-named per-OID table
  `pMeGame->VoiceIndexForOID[Offset][PlayIndex]` (assert string @0x080549b0,
  "play voice index =%d." @0x08054a00) and plays the selected sample. System voices live in a
  **separate** flat file `A:/VOIMG/Chomp_Voice.bin` (@0x08097348) read by `play_chomp_voice`
  @0x08097374 (entry `i` at offset `i<<2`), distinct from per-game book content.
- **2N-update3202MT**: the same tap is handled by the generic interpreter
  `gme_oid_dispatch` @0x0803629c / `game_pick_voice` @0x0806a0dc over a single `.gme` container
  parsed by the `gme_parse_*` cluster (@0x08035338..0x08036228).

User-visible effect is the same (tap an OID, hear a sound), but the on-media content layout and
the naming/authoring pipeline differ: first-gen ships a fixed `VoiceIndexForOID` permutation +
`Chomp_Voice.bin`; the 2N ships scripted playlists inside the `.gme`.

## 2. Compiled-in German spelling/word game with a hardcoded word list (first-gen only)

ZC3201 contains an on-device word-matching game whose **word list is compiled into the firmware**:
`Apfel, Auto, Baum, Bein, Berg, Birne, Blume, Brot, Euter/Teure/Teuer/Treue, Flasche/Falsche,
Frosch/forsch/Schorf, Hals, Hand, Insel/Linse/Senil, Leiter/Teiler, Name/Amen, Note, Nudel, Rose,
Sand, Schirm, Tasche, Tiger/Gerit, Turm, Wald, Wort, Wurm, Zelt` (strings @0x0809b2ea..0x0809b3cb),
alongside the markers `GERMAN` @0x0809ae19 and `3201`. The matcher is `MakerUpperThenStrCmp`
@0x0808da20 inside `chomp\zc_apl\study\s_study_readinggame6.c` (state
`state_study_readinggame6` @0x0808d98c). The 2N's game engines are data-driven (see §3) and carry
no such compiled-in German word table. This is a genuine first-gen game-content difference.

## 3. Game-engine set: small fixed set (first-gen) vs large generic/data-driven set (2N)

ZC3201 ships a **fixed, hand-coded** set of game state machines, each self-identified by an
on-screen banner: quiz/`ask_question` ("Begin Ask Question", `game_quiz_sm` @0x08078094),
find-target (`game_findtarget_sm` @0x0807d608, "Game Seven Start"), wrong-limit
(`game_wronglimit_sm` @0x080598e8), "Game Four Init" (@0x0807abc4), "Game Five Init"
(@0x080799f0), "MiniGame Two/Six", "MiniThree", "Spe one/two", and `discovery_mode` @0x08086d04
("Enter Discovery Mode").

The 2N-update3202MT exposes a much larger, generically-parameterised engine bank —
`game16`..`game68` families each with `*_engine`, `*_record_load`, `*_play_phase_cue`,
`*_read_pll_count` variants (e.g. `game58_engine`, `game66_engine`, `game18_select_handler`;
`game*_record_load` @0x080773c8..0x0808b7ac). First-gen offers fewer, fixed game mechanics; the
flagship offers many more game types selectable from the `.gme` data.

## 4. Embedded GME ARM-binary launch path is present in BOTH generations (corrects prior note)

An earlier lab note (fw/2N-Update3202/docs/CORRESPONDENCE.md §Divergences) stated ZC3201 shows
"no equivalent" of the 2N's `system_api`-to-embedded-binary mechanism. The ZC3201 symbol harvest
**revises this**: first-gen also carries the launcher and the `system_api` surface —
`gme_launch_binary_build_sysapi` @0x08095090, `gme_read_main_binary_table` @0x0809553c,
`gme_binary_load_region` @0x08023324, and `sysapi_open/write/close/play_sound`
(@0x08008a48/0x08009498/0x080096a8/0x0800c1c4), with a loader path string `B:/App_Demo.bin`
(@0x0809b050). So the embedded-binary capability is architecturally present in both gens (its
first-gen use may be limited to the `App_Demo` test path); it is **not** a 2N-only divergence.

## 5. Firmware self-update: both read update.upd; first-gen uses Burn_Lib

Both generations self-update from `update.upd`. ZC3201 does it through `Burn_Lib_V1.0.0`
(banner @0x080a6954): state `state_set_firmware_update` (`chomp\update\s_set_firmware_update.c`
@0x08039420 region) reads `B:\update.upd` (@0x0809ae88), then prints "Update software End."
(@0x080395f0) and `RESET` (`fw_update_finish_reset` @0x08039420). The 2N routes updates through
its `fw_update`/`fw_update_cleanup` handlers. Same user action (plug in, pen reflashes and
reboots); different internal library naming.

## 6. Music/study playlists present in first-gen

ZC3201 has a media-list subsystem: `A:/musiclist.lst` (@0x0809b7f2) and `A:/studylist.lst`
(@0x0809b803), with `log_aud_musiclist.c` / `Aud_listSetNeedUpdate` (@0x08099afc). The 2N carries
the analogous `aud_list_set_need_update` (@0x080ae65c). Both gens maintain on-media list files;
noted here as a structural parity point (no first-gen *absence* of list-playback should be
inferred from the strings).
