# System-voice / audio-feedback subsystem — Chomp_Voice, `fwl_play_voice_by_id`, and the voice-ID map

Static analysis of the named decompilation (unified base **0x08009000**; PROG-byte and
pen-RAM-dump pool resolutions cited inline). Evidence tags: **[Proven]** = decomp / PROG bytes /
`Chomp_Voice.bin` bytes / pen-RAM-dump pool values; **[Inferred]** = deduction (reason given);
**[Open]** = unresolved. `Chomp_Voice.bin` and the pen RAM dump are firmware-derived content
(extracted / captured from hardware) and are **not included** in this repository. Companions:
`media-pipeline.md` (the player underneath), `upd-system-partition-layout.md` (where
Chomp_Voice.bin comes from), `pmu-power-management.md` (battery/off voices),
`anticlone-auth-check.md` (0x23/0x24), `statechart-full-map.md` (states 1/2/13).

The firmware module is Anyka's **`chomp\zc_fwl\Fwl_pfVoice.c`** (source-path string
@0x080ab5a8). "Voice" in this module = *system prompt*, as opposed to GME media. [Proven]

---

## 0. Headline

- A system voice is played by **`fwl_play_voice_by_id(id)` @0x080ab9ac**: it opens
  **`A:/VOIMG/Chomp_Voice.bin`** (string @0x080ab990), reads the u32 offset table at the file
  head (`seek id·4; read start; read end`), and plays the byte range `[start, end)` through the
  ordinary media player (`play_media_setup`, codec-hint **3 = WAV/PCM**). **[Proven]**
- `Chomp_Voice.bin` = 49-entry u32 offset table + **48 concatenated plain RIFF/WAVE files**
  (PCM mono 16-bit; 16 k/32 k/44.1 kHz mixed). IDs run **0x00–0x2F**. **[Proven, parsed]**
- **No XOR.** The media read callback (`fs_read_xor_decrypt` 0x0800df70) only decrypts when the
  flag byte **@0x08008c00** is 1; `play_media` (GME media) sets it to **1**,
  `fwl_play_voice_by_id` sets it to **0** before playing. Voices are stored and read plain.
  **[Proven — see §2.2]**
- Voices are **single-language** (the German `.upd` ships German content); the only per-language
  system audio is the two updater WAVs in `A:/Language/` selected via `B:/LanguageInfoMT.txt`
  (§3.2). **[Proven mechanism]**
- Prompts play iff `A:/VOIMG/Chomp_Voice.bin` exists on the A: FAT (missing file ⇒ silent skip,
  no crash) — so any tool running the firmware needs the voice archive on A: to hear prompts
  (§5). **[Proven]**

---

## 1. The voice table — `Chomp_Voice.bin` layout and lookup (Q1)

### 1.1 The consumer

```c
// fwl_play_voice_by_id @0x080ab9ac   [reconstructed pseudocode]
void fwl_play_voice_by_id(int id) {
    dbg("voiID=%d.\r\n", id);                       // string @0x080ab9d0
    if (g_voice_fd == -1)                            // g_voice_fd @0x081226f0 (pool 0x080ab5a4)
        g_voice_fd = fs_open("A:/VOIMG/Chomp_Voice.bin", 0, 0);   // string @0x080ab990
    if (g_voice_fd == -1) return;                    // no file -> silent no-op
    fs_seek(g_voice_fd, id*4, 0);
    fs_read(g_voice_fd, &start, 4);                  // table[id]
    fs_read(g_voice_fd, &end,   4);                  // table[id+1]
    *g_media_xor_enable = 0;                         // @0x08008c00 (pool 0x080ab974) — no decrypt
    play_media_setup(g_voice_fd, start, end - start, 3);   // 3 = WAV/PCM codec hint
}
```

Mechanism: **index into an in-file u32 offset table**; entry *i* is the byte offset of voice
*i*, entry *i+1* delimits it. There is **no bounds check** — an id ≥ 48 reads two "offsets"
out of voice-0's PCM data and would try to play garbage. The file handle is opened once and
**cached forever** in `g_voice_fd` @0x081226f0. [Proven]

### 1.2 The file (`Chomp_Voice.bin` = `.upd` to_udisk file #0; firmware content, not included)

- Size 0x41EBE4 (4,320,228 B). `word[0] = 0xC4` = 49·4 = table size = offset of voice 0;
  `word[48] = 0x41EBE4` = file size (end sentinel) ⇒ **48 voices, ids 0x00–0x2F**. [Proven]
- Every entry is a complete **plain RIFF/WAVE, fmt-tag 1 (PCM), 16-bit**. Rates are mixed per
  voice: ids 0x00–0x12 and 0x1B–0x2A mono 16 kHz; 0x13/0x14/0x16–0x1A/0x2B/0x2C mono 32 kHz;
  0x2D/0x2E mono 44.1 kHz; 0x15 **stereo** 44.1 kHz. Durations 0.16 s – 7.7 s (full table §4).
  [Proven, parsed]
- The WAV container is not stripped: the firmware hands the whole RIFF range to the medialib,
  whose WAV sniffer (`medialib_detect_wav` 0x080feebc) parses it. [Proven chain]

---

## 2. The play path (Q2)

### 2.1 Chain

```
caller (see §4) ──fwl_play_voice_by_id(id) @0x080ab9ac
    ├─ fs_open/seek/read of A:/VOIMG/Chomp_Voice.bin  (table lookup, §1)
    ├─ *0x08008c00 = 0                                 (XOR decrypt OFF, §2.2)
    └─ play_media_setup(fd, start, len, 3) @0x080ab6ac  ("Fwl_pfVoice.c" line 0xda assert)
         ├─ fwl_voice_stop_sync() @0x080ab620           (STOP any current voice, §2.3)
         ├─ aud_player_construct() if needed; volume_get()
         ├─ aud_player_reset + aud_player_set_source(player, 0, {fd,start,len}, 3, 1, 0x32, 0x32, 0)
         │     └─ codec hint 3 stored at player+0xe9; fs callbacks DAT_08032bcc.. installed
         │       (read cb = 0x0800e0bc → fs_read_xor_decrypt 0x0800df70) [Proven, PROG pool]
         └─ aud_player_play → medialib_open → detect WAV → PCM decode → DAC/DMA
                (identical engine to GME media — media-pipeline.md §3–§8)
```

`play_media` (0x080ab7b4, the GME-media entry) calls the **same** `play_media_setup`; system
voices and GME cues share one player, one codec framework, one DAC path. Voice playback
**preempts** whatever is playing (the built-in stop in `play_media_setup`). [Proven]

Most call sites bracket the call with `aud_player_construct()` (player may not exist yet, e.g.
splash) and many then spin `while (is_audio_playing()) audio_wait_tick();` when the sequence
must be strictly ordered (battery-final → off-jingle → power cut). [Proven]

### 2.2 XOR: voices are NOT encrypted — the 0x08008c00 flag

`fs_read_xor_decrypt` @0x0800df70 (the audio read callback for *all* media) gates its XOR loop
on a byte flag: `if (*DAT_0800e028 == 1) { … ^= *DAT_0800e02c … }` with pass-through for
0x00/0xFF/key/key^0xFF. The two pool slots resolve — **from a pen RAM dump** (hardware capture
covering 0x08007000..0x0800F000, not included) — to:

| pool slot | → global | meaning |
|---|---|---|
| 0x0800e028 | **0x08008c00** | XOR-enable flag |
| 0x0800e02c | **0x08121a80** | XOR key byte (0xAD for GME media) |

And 0x08008c00 is **exactly the global that `play_media` sets to 1** (0x080ab7b4 L20,
`*DAT_080ab974 = 1`, pool 0x080ab974 → 0x08008c00 [PROG.bin byte]) **and
`fwl_play_voice_by_id` sets to 0** before `play_media_setup`. So: GME media reads are
XOR-0xAD-decrypted, **system-voice reads are plain** — which matches the plaintext RIFF bytes
in the file. `lang_play_batlow` (§3.2) plays its WAVs without touching the flag (it runs in the
updater flow where no GME source was armed). **[Proven; the lang_play nuance Inferred]**

### 2.3 The two voice-stop functions (0x080ab620, 0x080ab500)

Both are stop functions; neither plays anything. Their raw decompilation names
(`fwl_voice_play_2` / `fwl_voice_play`) are misleading, hence the renames used here and in §6:

- **`fwl_voice_stop_sync` @0x080ab620** — *synchronous voice stop*: waits for the audio flags
  (`*0x08008c60` bit 2, pool 0x080ab970) to clear, then `aud_player_stop_2`, amp/flag cleanup.
  Returns the `"g_pAudPlayer is NULL"` string (@0x080ab5c4) if no player. Its ~50 call sites
  across the game/statechart handlers are all "shut the current prompt up" (e.g. before
  starting a new action, before power-off). `play_media_setup` itself calls it first. [Proven]
- **`fwl_voice_stop_close` @0x080ab500** — *hard stop + close*: `aud_player_stop`, player teardown,
  **closes and invalidates `g_voice_fd`** (`FUN_080ad514(); *g_voice_fd_slot = -1`), amp off.
  Called from the book-state exit path (0x080348a8). [Proven]

---

## 3. Storage + language model (Q3)

### 3.1 Where the voices live

Single archive **`A:/VOIMG/Chomp_Voice.bin`**, written onto A: by the pen's own updater from
the `.upd` **to_udisk** payload (`voice\Chomp_Voice.bin` → dir-map `voice`→`A:VOIMG`), and by
the factory producer path. A: is reformatted on every firmware update, then the 7 to_udisk
files are re-written — Chomp_Voice.bin is the only system-voice store; `A:/SYSTEM` is runtime
state, the `*.lst` files are discovery indexes. Full trace: `upd-system-partition-layout.md`
§2–3. **[Proven]**

There is no other consumer of `A:/VOIMG` in the decomp corpus, and **`a:/voicelist.lst` has
nothing to do with system voices**: it is a vestigial instance of the generic music-list
library (list-id 3), touched only by the dead `voicelist_delete_and_invalidate` @0x080f07d0
(no static caller) — see `book-discovery-and-load.md` §3. **[Proven]**

### 3.2 Language

- **System voices: one language per firmware.** The path is a fixed constant; no language
  component, no selection logic anywhere on the `fwl_play_voice_by_id` path. Our
  `update3202MT.upd` is the German pen firmware ⇒ German prompt set. A different pen language
  = a different `.upd` shipping a different Chomp_Voice.bin. **[Proven mechanism; "other
  languages ship different archives" Inferred]**
- **The `A:/Language/` WAVs are updater-only.** Exactly two per language
  (`Update<LANG>.wav` = "update running", `BatLowUpdate<LANG>.wav` = "battery too low to
  update"), shipped for GERMAN/FRENCH/DUTCH. Player: `lang_play_batlow` @0x080523b4 builds
  `A:/Language` + (`/Update`|`/BatLowUpdate`) + `<LANG>.wav` (strings @0x080524cc/d8/e0) and
  plays the **whole file** via `play_media_setup(fd, 0, size, 3)` — same engine, not part of
  the Chomp table. Missing file → logs `[NO FILE ]`, returns false. **[Proven]**
- **Language choice** for those WAVs: `lang_get_num` @0x08052508 parses **`B:/LanguageInfoMT.txt`**
  (count digit, then per language: name up to `' '`, update-file name up to `'\r'`); the updater
  (`fw_update_scan_b_for_upd` 0x08052768) matches the `.upd` filename found on B: against the
  per-language update-file names — the matching index selects `<LANG>`. No file → default
  `Update3202MT.upd` @0x080ecf12. **[Proven]** (The codepage "language selector" G0 is text
  encoding only and never written — unrelated; `codepage-what-is-it.md`.)

---

## 4. The voice-ID map (Q4)

Parsed from the `Chomp_Voice.bin` archive (format/duration = **Proven bytes**) joined with every
static call site of `fwl_play_voice_by_id` in the corpus (sites = **Proven**; the *meaning*
column is Proven where the call context pins it, else Inferred — we cannot transcribe audio
statically).

| id | dur | fmt | caller(s) | meaning |
|---|---|---|---|---|
| 0x00–0x08 | 0.4–0.7 s | 16 k | **none** (unreferenced) | [Open] — plausibly a legacy digit set 1–9 |
| 0x09–0x12 | 0.4–0.7 s | 16 k | number speakers: state-2 `(n&0xf)+9` (0x0804cecc L613), `FUN_0804bf84`/`FUN_0804c090` `digit+9` | **spoken digits 0–9** (0x09="0" … 0x12="9") [Proven mapping] |
| **0x13** | 1.62 s | 32 k | `book_state_entry` 0x080345cc L137, once per power cycle (gate: bit 1 of 0x081da086, set-only) | **power-on / welcome jingle** — plays at the FIRST book(13) entry, i.e. normally at the **first cover tap**, not at power-on; no-tap at boot only via the `B:/FLAG.bin` resume or low-batt descent (see `power-on-sound.md`) [Proven site; musical-jingle character proven by pitch analysis] |
| **0x14** | 1.66 s | 32 k | power-off everywhere: `button_dispatch_105f` 0x0800a9c8 L138, sequencer 0x08034bc0 L46, state-2 tail 0x0804cecc L771 | **power-off jingle** [Proven] |
| 0x15 | 0.16 s | 44.1 k **stereo** | vol± in 0x0800a9c8 (normal step) | **volume-step blip** [Proven] |
| 0x16 | 0.97 s | 32 k | vol± in 0x0800a9c8 (up at max / down at 0) | **volume end-stop sound** [Proven] |
| **0x17** | 5.15 s | 32 k | batt-warn: 0x08034bc0 L26, 0x0804cecc L750 (stage 1 + pending-flag, `akoid[0xb9]==1`) | **battery-low warning phrase** [Proven] |
| 0x18 | 7.67 s | 32 k | **none** (unreferenced) | [Open] — longest clip in the file |
| 0x19 | 5.54 s | 32 k | `splash_entry` 0x0804c1d4 L45 when `B:/FLAG.bin` exists | **firmware-update resume announcement** [Proven site] |
| **0x1A** | 3.74 s | 32 k | batt-final: 0x08034bc0 L29, 0x0804cecc L753 (then wait → 0x14 → off) | **battery-empty final phrase** [Proven] |
| 0x1B | 0.94 s | 16 k | `splash_entry` L102 — **factory prod-test branch** (GPIO11&&GPIO1 4× — NOT the retail boot; context: `B:/spotlight.bin` checksum, `B:/Prodtest.txt`) | prod-test start announcement (**speech**, not a chime; pitch analysis). NOT a power-on jingle — retail boots skip this branch (`power-on-sound.md` §5) [Proven site] |
| 0x1C | 1.26 s | 16 k | state-2 stage 0x1F/0x20 (first announce) 0x0804cecc L642 | prod-test: sequence start [Inferred wording] |
| 0x1D | 1.27 s | 16 k | state-2 stage 1 (PROG checksum **wrong** path) L675 | prod-test: program-checksum FAIL [Proven branch] |
| 0x1E | 1.22 s | 16 k | state-2 stage 2 (PROG checksum **right** path) L738 | prod-test: program-checksum OK [Proven branch] |
| 0x1F | 1.20 s | 16 k | state-2 button-5 first press L108 | prod-test: next-test announce [Inferred] |
| 0x20 | 1.22 s | 16 k | state-2 OID-sensor test: data seen L710 | prod-test: OID sensor OK [Proven branch] |
| 0x21 | 1.11 s | 16 k | state-2 OID-sensor test: 6 polls, no data L704 | prod-test: OID sensor FAIL [Proven branch] |
| 0x22 | 1.16 s | 16 k | state-2 stage 2→7 L129 (before auth ch/resp) | prod-test: auth-chip test start [Inferred] |
| 0x23 | 1.21 s | 16 k | state-2 `LAB_0804d3d0`: `akoid[0xb4]==0` after `authchip_challenge_gpio5_10` | **auth-chip PASS** [Proven] |
| **0x24** | 1.18 s | 16 k | same site, `akoid[0xb4]!=0` | **auth-chip FAIL (soft path — voice only, no power-off)** [Proven; cf. anticlone doc §3] |
| 0x25 | 1.57 s | 16 k | state-2 test-file stage entry L95 | prod-test: test-file stage announce [Inferred] |
| 0x26 | 1.28 s | 16 k | state-2 L361: all 6 `.bin` test files ok (`puVar1[1]==6`) | prod-test: test files OK [Proven branch] |
| 0x27 | 1.22 s | 16 k | state-2 L364: else | prod-test: test files FAIL [Proven branch] |
| 0x28 | 1.80 s | 16 k | **none** (0x28 only occurs as a *stage* number) | [Open] |
| 0x29 | 1.87 s | 16 k | state-2 L564: prod-test overall pass | prod-test: overall PASS [Proven branch] |
| 0x2A | 1.75 s | 16 k | state-2 L567: else | prod-test: overall FAIL [Proven branch] |
| 0x2B | 3.46 s | 32 k | `gme_oid_dispatch` L153: no book; product id 0 or > 999 | "product not found" variant A [Proven site; wording Inferred] |
| 0x2C | 3.17 s | 32 k | sequencer 0x08034bc0 L18: off stage 1 **without** low-batt pending (bit 4 of 0x081da086 clear) | **power-off announcement** (idle auto-off / non-battery off) [Proven site] |
| **0x2D** | 3.94 s | 44.1 k | `gme_oid_dispatch` L1121 (`gme_mount_check_product`==0) and L153 (product id 1..999) | **product mount-fail voice** ("file missing — use tiptoi Manager") [Proven sites] |
| 0x2E | 6.90 s | 44.1 k | **none** (unreferenced) | [Open] |
| 0x2F | 5.98 s | 16 k | state-2 stage ')' L520 and event-0x1060 exit branch L613 | prod-test: final/summary prompt [Inferred] |

Unreferenced by any static call: **0x00–0x08, 0x18, 0x28, 0x2E** (12 of 48). All variable-id
call sites are accounted for (`uVar18`∈{0x2B,0x2D}, `uVar9`∈{0x15,0x16}, state-2 `uVar10`
constants, digit `n+9`). [Proven corpus grep]

### 4.1 The two voice sequencers

- **Power-off voice sequencer `FUN_08034bc0`** (called from ~58 game handlers as the per-event
  tick): stage byte **@0x08008c0b** (pmu doc "low-batt stage"): stage 1 + low-batt pending
  (0x081da086 bit 4) → **0x17** (warn; re-arm) or **0x1A** (final, wait) → stage 3; stage 1
  without pending → **0x2C**, stage 2; stage 3 → **0x14**, wait, stage 5,
  `sm_ao_event_coalesce` → off event (pmu §6). The state-2 handler tail (0x0804cecc L740-780)
  inlines the same machine. [Proven]
- **Decimal number speaker `FUN_0804bf84` (start) / `FUN_0804c090` (resume)**: speaks
  `game_ctx[+0x64]` in decimal, one digit-group per audio-idle tick (resume stage at +0x181,
  active flag +0x60), each digit → voice `d+9`. Driver: state-2 event **0x1060** (value < 10 →
  single digit; else load +0x64 and start). Used by the prod-test flow to read out
  checksums/counts. [Proven mechanics]

---

## 5. What it takes to hear prompts (Q5)

- **Content:** system prompts play **iff `A:/VOIMG/Chomp_Voice.bin` is present on A:**
  (`fs_open == -1` → silent return — Proven, §1.1). If A: has no VOIMG/Language content every
  `fwl_play_voice_by_id` is a no-op — boot/discovery are unaffected. To make prompts audible,
  A: must carry the voice archive (`VOIMG/Chomp_Voice.bin`) and, optionally, `Language/*.wav`
  for the updater prompts (`upd-system-partition-layout.md` §4). [Proven]
- **No XOR:** the archive goes onto the FAT **as-is** (plain WAVs; the firmware clears the
  decrypt flag itself, §2.2) — it is **not** 0xAD-encrypted like GME media. [Proven]
- **Runtime prerequisites:** `g_pAudPlayer` constructed (state-13 ENTER,
  `audio-player-construction.md`; voice sites also call `aud_player_construct()` on demand),
  and the voice fd is cached (§1.1) so its FAT handle must stay valid across plays. [Proven]
- Expected first audible prompt on a faithful boot: **voice 0x13** (book-state entry jingle),
  gated to once per boot by bit 1 of 0x081da086 — on a clean boot this fires at the **first
  cover tap** (the boot itself is silent); with `B:/FLAG.bin` present it fires with **no tap**,
  preceded by voice **0x19** at splash (`power-on-sound.md`). A missing/mismatched product then
  gives **0x2D/0x2B** from `gme_oid_dispatch`. [Proven sites]

---

## 6. Names / docstrings (names.csv candidates)

| addr | name (proposed) | docstring |
|---|---|---|
| 0x080ab9ac | `fwl_play_voice_by_id` (keep) | Open+cache `A:/VOIMG/Chomp_Voice.bin` (fd @0x081226f0), u32 offset-table lookup `[id*4]`,`[id*4+4]`, clear XOR flag @0x08008c00, `play_media_setup(fd,start,len,3)`. No bounds check (48 voices). Logs `voiID=%d`. |
| 0x080ab6ac | `play_media_setup` (keep) | `chomp\zc_fwl\Fwl_pfVoice.c` L0xda. Stops current voice, ensures player, volume, `aud_player_set_source(...,codec_hint,...)` (hint→player+0xe9; 3=WAV/PCM), play. Shared by GME media, system voices, Language WAVs. |
| 0x080ab620 | `fwl_voice_stop_sync` | STOP, not play: waits busy-flag @0x08008c60 bit2, `aud_player_stop_2`, amp/flag cleanup. ~50 call sites = "silence current prompt". |
| 0x080ab500 | `fwl_voice_stop_close` | Hard stop + player teardown + closes `g_voice_fd` (@0x081226f0 := −1). Book-exit path. |
| 0x08034bc0 | `poweroff_voice_sequencer` | Stage @0x08008c0b: 1+battflag(0x081da086 bit4)→0x17/0x1A; 1 clean→0x2C; 3→0x14 + wait + stage 5 → off event. Tick from ~58 handlers. |
| 0x0804bf84 | `speak_number_start` | Begin decimal readout of game_ctx[+0x64]: pick highest digit group, play voice digit+9, stage→+0x181. |
| 0x0804c090 | `speak_number_resume` | Continue readout per +0x181 when audio idle; digits → voices 0x09–0x12. |
| 0x0800df70 | `fs_read_xor_decrypt` (keep) | XOR loop gated on flag @0x08008c00 (pool 0x0800e028), key @0x08121a80 (pool 0x0800e02c); pass-through 0x00/0xFF/key/key^0xFF. |
| globals | `g_media_xor_enable` @0x08008c00 | 1 = GME-media source (XOR-0xAD reads), 0 = plain (system voices). Set 1 by `play_media`, 0 by `fwl_play_voice_by_id`. |
| globals | `g_media_xor_key` @0x08121a80 | the derived media XOR key byte (0xAD). |
| globals | `g_voice_fd` @0x081226f0 | cached Chomp_Voice.bin handle (−1 = closed). |
| globals | `g_voice_busy_flags` @0x08008c60 | audio busy bits polled by the stop path (bit 2). |
| globals | 0x081da086 | status bits: bit 1 = boot jingle (0x13) already played; bit 4 = low-batt voice pending (pmu). |

Key strings: `A:/VOIMG/Chomp_Voice.bin` @0x080ab990; `voiID=%d.` @0x080ab9d0;
`chomp\zc_fwl\Fwl_pfVoice.c` @0x080ab5a8; `g_pAudPlayer is NULL` @0x080ab5c4;
`VoiceNumberNeedRep = %d.` @0x080ab3c4 (GME repeat machinery in `play_media`, *not* Chomp
voices); `A:/Language` @0x080524cc; `/Update` @0x080524d8; `/BatLowUpdate` @0x080524e0.

---

## 7. Open items

- **[Open]** Wording/transcription of the 48 clips (which German phrase is which) — decode the
  `Chomp_Voice.bin` entries and listen; the WAVs extract trivially (§1.2). Would settle every
  [Inferred] meaning and the 12 unreferenced clips (0x00–0x08, 0x18, 0x28, 0x2E).
- **[Open]** State-2 stage-3 plays a whole external file via path global @0x08121d12 (RAM,
  runtime-built — likely a B: prod-test WAV); resolve when the update/prod-test flow is next
  traced live.
- **[Open]** Whether any non-German `.upd` really re-voices Chomp_Voice.bin (we only hold the
  German MT image).
