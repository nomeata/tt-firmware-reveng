# GME game-format completeness audit — 2N (MT) interpreter

> READ-ONLY audit for GOAL 2. Catalogs every opcode, condition, game-type, media/codec path
> and header table the MT GME interpreter handles, with a **documented / undocumented** verdict
> per item and an overall "can we run arbitrary GMEs" assessment. All addresses = unified runtime
> base **0x08009000** (`data/PROG.bin` flat load). Evidence: **[P]** = read from
> `out/decomp_named/` / PROG.bin bytes; **[I]** = inferred.
> Companions: `gme-timer-counter-random.md`, `book-discovery-and-load.md`, `media-pipeline.md`,
> `statechart-full-map.md`. External cross-ref: tttool `tip-toi-reveng/GME-Format.md`.

---

## 0. Verdict (one paragraph)

The MT GME model is **complete enough to run arbitrary standard (script-driven) GMEs**, not just
`taschenrechner`. Every action opcode, every conditional, the play-script / media / playlist /
additional-script / register / game / OID-range tables, the codec dispatch and all six play-modes
are fully reversed and byte-for-byte in view. **Three previously-open items are resolved here:**
(1) the tttool "raw→real XOR" mystery (`GME-Format.md` §165) — it is a **256-byte lookup table
in firmware ROM @0x080381da**, `real = table[raw]`, verified `table[0x39]=0xAD`;
(2) the header `0x8C` "media-flag table" of unknown meaning — the firmware uses it in `play_media`
as a per-media **"VoiceNumberNeedRep"** repeat-suppression list; (3) the dual-playlist for GME
**game-type 12**. The only genuinely **undocumented opcodes** are the minor **0xFFA1** (coin-flip
play, resolved 2026-07-20 — §1) and the **0xFFFC** conditional (an Eq alias) — both handled by the firmware, neither in
tttool, neither exercised by taschenrechner. The parts that would break "arbitrary" GMEs are **not
format gaps** but two content classes that carry their own code/decoders: embedded **ZC3202N
main-binary** GMEs (hdr@0xA8 → ARM code run via a ~90-entry system_api vtable) execute real content
code, and the **codec-0x13 system-voice** decode path (media-pipeline §6). For pure play-script +
built-in-game-type GMEs: complete.

---

## 1. Opcode inventory — `gme_exec_command` @0x08034da0 [P, full function in view]

Signature `gme_exec_command(u16 reg_idx, u16 opcode, u8 is_const, u16 operand)`; register file =
u16[] @0x081da350. Opcode = 16-bit LE (`0xHHLL`), matching the firmware `switch`.

### Action opcodes (all cases enumerated from the decomp)

| opcode | tttool name | firmware effect | doc verdict |
|---|---|---|---|
| 0xFFF0 | Inc | `reg += v` | ✅ documented |
| 0xFFF1 | Dec | `reg -= v` | ✅ |
| 0xFFF2 | Mult | `reg *= v` | ✅ |
| 0xFFF3 | Div | `reg = reg / v` (nandboot `rt_udiv`) | ✅ |
| 0xFFF4 | Mod | `reg = reg % v` | ✅ |
| 0xFFF5 | And | `reg &= v` | ✅ |
| 0xFFF6 | Or | `reg \|= v` | ✅ |
| 0xFFF7 | XOr | `reg ^= v` | ✅ |
| 0xFFF8 | Neg | `reg = (reg<=1) ? 1-reg : 0` (logical-not) | ✅ |
| 0xFFF9 | Set | `reg = v` | ✅ |
| 0xFF00 | Timer (tttool) | **Rand**: `reg = tick@0x08008d24 % (m+1)`; prints "Rand(%d)=%d" | ✅ (gme-timer §3a) |
| 0xFE00 | Timer (real) | arm periodic sw-timer, delay `m*100`, auto-reload, handle @0x08121ecc | ✅ (gme-timer §4) |
| 0xFEFF | — | **cancel** the GME timer | ✅ (gme-timer; NOT in tttool) |
| 0xFC00 | Random a b | play `playlist[heartbeat_ctr % (a-b+1) + b]` via `gme_rand_in_range` | ✅ (gme-timer §3b) |
| 0xFFE0 | RandomVariant `P*` | round-robin `playlist[dispatch_ctr%len]` (ctr @gamectx+0x125, pick @+0x141) | ✅ |
| 0xFFE1 | PlayAllVariant | play entry 0, play-all mode (gamectx+0x12c=1, ++0x12d) | ✅ |
| 0xFFE8 | Play n | play `playlist[n]` | ✅ |
| 0xFB00 | PlayAll | play from `hi`, mode 2 (gamectx+0x12e=hi) | ✅ |
| 0xFAFF | Cancel | "Exit A Game", posts event 2 | ✅ |
| 0xF8FF | Jump | deferred: gamectx+0xdd0=1, target→+0xdd2 (taken when audio stops) | ✅ |
| 0xFD00 | Game | game-select: reads hdr, posts event in state 0x0B; sets ctx+0x132/0x131 | ✅ |
| 0xFEE0..0xFEE7 | — | `sm_set_sound_profile(0..7)` — sound/rate-profile set (NOT a state jump) | ✅ (corrected in statechart-map) |
| 0xFEE8 | — | nop (returns) | ✅ |
| **0xFFA1** | — | **coin-flip play** `P½(m)`: clear `ffa1_did_play`; iff `evt_dispatch_count` (ctx+0x125) is EVEN, play `playlist[m]` **immediately** (shared FFE8 play tail) and latch `ffa1_play_idx`=m&0xff, `ffa1_did_play`=1 (replay-after-interruption record); odd count = complete no-op — see the 2026-07-20 resolution below | ⚠️ **UNDOCUMENTED** (not in tttool) |

**Undocumented action opcode: 0xFFA1 = "coin-flip play" — RESOLVED 2026-07-20** (asm read of the
handler @0x080352cc + parity-instrumented tt-emu experiment; supersedes both earlier readings
below). Semantics when the action executes (like any queued play action, at its slot in the
audio-boundary walk):

1. `ffa1_did_play` (ctx+0xddf) is **always cleared** first.
2. If `evt_dispatch_count` (ctx+0x125) is **odd → complete no-op** (no play, stays disarmed).
3. If **even** → play `playlist[m]` **immediately** — the handler falls through into the very
   same play tail as `P(m)`/FFE8 @0x08035288 (full u16 operand as index, no bounds check) — and
   record the roll: `ffa1_play_idx` (ctx+0xde0) = m&0xff, `ffa1_did_play` = 1.

So 0xFFA1 is `P(m)` executed with probability ~½: the "gate" is the LSB of the **free-running
book-mode event counter** `evt_dispatch_count`, which `gme_oid_dispatch` increments once at the
top of EVERY invocation — i.e. for every event book(13) receives (0x1046 OID-poll heartbeat
~10/s, taps, GME-timer 0x30, keys). Since a queued action executes at an audio boundary whose
timing depends on the preceding clip's length, the parity at execution time is unpredictable in
practice — a deliberate cheap Bernoulli(½), the exact sibling of P* (same counter, modulo) and
Rand/P(b-a) (tick / heartbeat counters). The latch is **not** a deferred play: it exists so the
REPLAY walks (`actq_walk_mode==2` and the game-launch/voice-stop resume paths, which re-issue
plays without re-rolling — P*→`pstar_last_idx`, P(b-a)→`prand_last_idx`) can reproduce the same
outcome: they play `playlist[ffa1_play_idx]` iff `ffa1_did_play`, i.e. the coin is flipped once
per execution, never per replay. Operand is literal-only (`is_literal` unchecked); the immediate
play uses the full u16 operand while the latch is byte-truncated. `book_state_entry` resets the
latch (0xddf=0, 0xde0=0xff).

**Empirical (tt-emu, 2026-07-20, deterministic pacing):** GME line `P(eins) 0xFFA1(1) P(drei)`
over playlist `[eins,zwei,drei]`, PC watch on the FFA1 handler reading ctx+0x125: every tap with
even count at FFA1-execution → media order **eins, zwei, drei** (the FFA1 plays ITS operand at
ITS queue slot, in the middle) + latch armed; every odd → **eins, drei** + latch clear; 8/8 taps
correlate exactly. This also explains the earlier "3 vs 2 media events" result and kills the
"even-dispatch-parity coin-flip *arming*, played later" placeholder: nothing is deferred.

> **History.** First reading (2026-07-18) called it a "deferred play-index consumed by a playlist
> sentinel" — wrong on both counts (the walk is over the decoded **action queue**, and the play is
> immediate). Second reading (2026-07-19, nomeata/tip-toi-reveng PR #7/#10) fixed the queue-vs-
> playlist confusion but still modelled it as a two-phase latch-then-insert with "coin-flip
> arming". A 0xFFA1 placed in a *playlist* still plays nothing (it is a media index there, OOB →
> playback wedges). GME-Format.md's 0xFFA1 paragraph should be updated to the resolved story.

### Conditional opcodes (`gme_check_condition` @0x08035624) [P]

| opcode | meaning | doc verdict |
|---|---|---|
| 0xFFF9 | Eq | ✅ |
| 0xFFFA | Gt | ✅ |
| 0xFFFB | Lt | ✅ |
| 0xFFFC | — | **Eq alias** (`case 0xfffc: break;` shares the Eq compare) | ⚠️ **UNDOCUMENTED** (harmless; not in tttool) |
| 0xFFFD | GEq | ✅ |
| 0xFFFE | LEq | ✅ |
| 0xFFFF | NEq | ✅ |

All conditions are pure u16 compares over the register file, with the `is_const` flag selecting
literal-vs-register on each side. Any unlisted opcode → default → returns 0 (condition false).

**Opcode completeness: COMPLETE.** The `switch` has no reachable case we do not name. The only
gaps vs. our prior docs are the two ⚠ items above (0xFFA1, 0xFFFC), both now catalogued.

---

## 2. Game-types — `FUN_08034cbc` (type read) + dispatch mapping [P]

The GME **game table** (hdr@0x10) is walked by `FUN_08034cbc` (0x08034cbc), which returns the
tapped OID's **game-type byte** (via ctx+0x131 record index). `gme_oid_dispatch` maps
(game-type, product-id) → a **launch event 0x1015–0x1045** → one of 56 game/activity leaf states.
The full table is in `statechart-full-map.md` §2 and is **complete**:

| game-type | product/variant | → state | doc |
|---|---|---|---|
| 1,2,3,5,10 | — | 14 (GME play-script game) | ✅ |
| 4 / 40 | — | 15 | ✅ |
| 6 | — | 16 | ✅ |
| 7 | — | 17 | ✅ |
| 8 | — | 18 | ✅ |
| 9 | products 9/0xB (var 0-3), 7, 0xE, 0xF, else | 46-49 / 42-45 / 32 / 64 / 65 / 30 | ✅ |
| 16 (0x10) | product 2 / else | 60 / 19 | ✅ |
| 17 | — | 58 | ✅ |
| 18 | — | 59 | ✅ |
| 19 | — | 61 | ✅ |
| 20 | — | 62 | ✅ |
| 21 | — | 63 | ✅ |
| 22 | — | 66 | ✅ |
| 23 | — | 68 | ✅ |
| 253 | — | 31 | ✅ |
| — | hdr@0x98 binary table ≠0 | 67 (embedded-binary sub-game) | ◐ path known, never run |
| — | hdr@0xA8 main binary ("separate") | 69 → 67 | ◐ path known, never run |
| — | post-parse type 8 (product switch) | 50 | ✅ |

**Game-type completeness: COMPLETE for the script-engine types (all mapped and named).** The two
**embedded-binary** types (67/69) are format-complete (the loader `gme_read_main_binary_table`
@0x080aadd8 reads hdr@0xA8 → {load addr, len}, `gme_launch_binary_build_sysapi` @0x080aa934 builds a
~90-pointer `system_api` struct on the stack and JUMPs to the loaded ARM blob) but are content code
we have never executed — a runtime gap, not a format gap.

---

## 3. Media / playlist model [P]

### 3.1 Selection → play chain
Play-script line → up to 8 **actions** (`gme_parse_actions` @0x080354c8; **clamped to 8/line**,
count @0x081da0a8) each dispatched to `gme_exec_command`, whose play cases resolve a **media index**
and call **`play_media` @0x080ab7b4** (media-pipeline.md §2). Media (offset,size) come from:

- **media table** hdr@0x04 — 8-byte entries `{u32 offset, u32 size}`. `gme_parse_media_offsets`
  @0x08035338 resolves `playlist[i]` → table entry → (offset@0x081da4e4, size@0x081da564). [P]
- **playlist** — `gme_parse_playlist` @0x080353e8 reads a u16 count + u16 media-index list
  (@0x081da126). **Product/game-type 12 (0x0C) carries a SECOND playlist** (count @0x081da124,
  list @0x081da166) resolved by `gme_parse_media_offsets2` @0x08035b8c. ⚠️ the dual-playlist for
  type 12 was not previously documented.

### 3.2 The six play-modes (all in view)
| mode | opcode / path | selection |
|---|---|---|
| single | 0xFFE8 Play n | `playlist[n]` |
| round-robin | 0xFFE0 | `playlist[dispatch_ctr%len]` |
| random | 0xFC00 | `playlist[heartbeat%(range)+b]` |
| play-all fwd | 0xFFE1 / mode-1 loop in dispatch | entries 0..len advancing on audio-stop |
| play-all from hi | 0xFB00 / mode-2 | entries hi..len |
| power-on jingle | hdr@0x71 → `FUN_08077230` @0x08077230 | dedicated power-on playlist, played at mount |

`FUN_08077230` (mislabelled "gme_media_xor_key_setup" in book-discovery-and-load.md §6 —
**correction: it is the power-on-sound playlist parser+player**, hdr@0x71, matching GME-Format
§0x71). The XOR key setup is separate (see §4).

### 3.3 Codec dispatch (media-pipeline.md §4, unchanged — complete)
`medialib_detect_codec` @0x080ff3d8 sniffs the first 0x40 **decrypted** bytes; codec ids:
**2/3/8/0x0A = WAV/PCM/IMA-ADPCM**, **0x0D = AMR**, **0x11 = FLAC**, **0x12 = Ogg/Vorbis**,
**0x14 = video**, plus a **special 0x13 = system-voice** path (`FUN_08038320`, bypasses medialib).
Decoders present: `ima_adpcm_decode`, Ogg/Vorbis, FLAC, `akoid_decode_frame`. **Media read
decryption**: all reads XOR through `fs_read_xor_decrypt`, media magic derived to **0xAD**
(pass-through {0x00,0xFF,0xAD,0x52}).

**Media completeness: COMPLETE.** New items catalogued: type-12 dual playlist, the 8-action clamp,
hdr@0x71 power-on playlist parser identity, hdr@0x8C repeat-flag table (§4).

---

## 4. Table structures — GME header + all tables [P]

`gme_parse_header` @0x08035d20 reads the header field-complete. Confirmed layout (all cited to the
sequential `fs_read`s + the DAT pools, cross-checked with tttool GME-Format.md):

| hdr off | field | firmware global / use | doc verdict |
|---|---|---|---|
| 0x00 | play-script table offset | @0x081da094; `gme_oid_to_playscript` base | ✅ |
| 0x04 | media table offset | @0x081da098 | ✅ |
| 0x08 | magic 0x238B | checked in `gme_mount_check_product` | ✅ |
| 0x0C | additional-script (timer) table | @0x081da1ac; `gme_parse_additional_script` | ✅ |
| 0x10 | game table | @0x081da1b0; `FUN_08034cbc` game-type read | ✅ |
| 0x14 | product id | @0x081da08c (current product); also ctx+0x16/+0x4a6 | ✅ |
| 0x18 | register-init offset | @0x081da1b4; `gme_reset_registers` | ✅ |
| 0x1C | **raw XOR byte** | → ctx+0x134 → **table lookup @0x080381da** → key @0x08121a80 | ✅ **NEW: table resolved** |
| 0x20 | CHOMPTECH version string | (length-prefixed; not consumed) | ✅ |
| 0x59..0x5F | language field | `gme_check_language` @0x08033fb0 vs pen lang @0x08121ec0 | ✅ |
| 0x60 | **additional media table** | @0x081da1b8; used by native game handlers (0x0807ba04 etc.) | ✅ (was "unknown" in tttool) |
| 0x71 | power-on-sound playlist | `FUN_08077230` (§3.2) | ✅ |
| 0x8C | **media-flag table** | `play_media` "VoiceNumberNeedRep" repeat-suppression | ✅ **NEW: partial resolve** |
| 0x94 | special-OID list | 20×u16 → cover-OID selectors @0x081da714/716/718… | ✅ |
| 0x98 | additional game-binaries table (ZC3202N) | → state 67 | ◐ path known |
| 0xA4 | tail flag (1 byte) | @0x081da088 | ✅ |
| 0xA8 | main-binary table (ZC3202N) | `gme_read_main_binary_table` @0x080aadd8 → state 69 | ◐ path known |
| 0xA0 / 0xC8 / 0xCC | ZC3201 / ZC3203L binary tables | not the MT pen's slots (read by loader variants) | ✅ n/a for MT |

### 4.1 The raw→real XOR **lookup table** (resolves GME-Format.md §165)
`gme_oid_dispatch` (L1130) and `gme_parse_header` set `*0x08121a80 = *(u8*)(0x080381da + rawXor)`,
where `rawXor` = hdr@0x1C. **The table @0x080381da is a 256-byte firmware ROM LUT.** Verified in
PROG.bin: `table[0x39] = 0xAD` (matches media-pipeline.md's "raw 0x39 → real 0xAD"). This is the
"unknown algorithm or lookup table" tttool has never had — it is a plain 256-entry substitution
table; a re-implementation can extract it directly from PROG.bin @ file-offset `0x080381da-0x08009000`.

### 4.2 Parsed run-time tables (all reversed)
- **play-script line-offset table** @0x081da1c0 (`gme_oid_to_playscript`: OID→script offset via
  first/last OID range ctx+0x18/+0x1A from `gme_parse_start_end_oid`).
- **conditions** per line: `gme_parse_check_conditions` @0x08035888 → {op@0x081da0a9, lhs@0x081da0b2,
  rhs@0x081da0c2, flags@0x081da0d2, val@0x081da0da}.
- **actions** per line: reg@0x081da0ea, opcode@0x081da0fa, type@0x081da10a, val@0x081da112 (max 8).
- **register file** @0x081da350 (`gme_reset_registers`).
- **additional-script** (timer) line-offset table @0x081da1c0 (`gme_parse_additional_script`).
- **`.lst` booklist / OID→product resolution**: no precomputed OID→action map — resolved by linear
  probe over `a:/oidfilelist.lst` (see book-discovery-and-load.md; complete).

**Table completeness: COMPLETE / field-complete.** No header field or run-time table is unreversed.
`FUN_08037b10` (`gme_parse_header_alt`, reads hdr@0x1C/0xBC/0xB8/0x30) has no static caller (dead).

---

## 5. Multi-book / product switching [P]

Fully documented in `book-discovery-and-load.md`. Summary of switch mechanisms:
- **First load**: cover-tap → classifier posts 0x104A+0x1058 → book_mount(12) → book(13); **second
  tap** of the product OID → `gme_oid_dispatch` product-band (OID ≤ 0x3E7) → `gme_mount_check_product`
  linear probe → `gme_parse_header` mounts.
- **Game-side product switch**: game leaves set `gamectx[0x58]=100`; dispatch preamble replays the
  saved OID (`gamectx+0x74`) as a synthetic 0x1060 → re-mount of a different product.
- **Switch-while-running**: the `gamectx[0x132]==0x11` gated path in dispatch.
- Discovery re-scan on the disc-change GPIO (0xB) + `soft_reboot`.

**Verdict: COMPLETE** — the multi-book / switch machinery is fully reversed.

---

## 6. Completeness assessment — can we run ARBITRARY GMEs?

| aspect | verdict |
|---|---|
| Action opcodes | ✅ complete (2 new: 0xFFA1, 0xFEFF-documented) |
| Conditionals | ✅ complete (0xFFFC alias noted) |
| Game-type dispatch | ✅ complete for script types; ◐ embedded-binary (states 67/69) carries its own ARM blob |
| Media table / playlist / play-modes | ✅ complete (type-12 dual playlist newly noted) |
| Codec dispatch | ✅ complete (WAV/PCM/ADPCM/AMR/FLAC/Ogg/video/voice-0x13) |
| Header + all tables | ✅ field-complete (XOR LUT + 0x8C flag table newly resolved) |
| Multi-book / switching | ✅ complete |
| XOR decryption | ✅ complete (LUT @0x080381da extracted) |

**Would arbitrary GMEs run?** For the overwhelming majority (standard play-script GMEs with the
built-in game types — i.e. every retail Ravensburger book and typical homebrew): **YES, the model
is complete.** The interpreter, all opcodes, the media/playlist/codec chain, the XOR, and the
tables are fully reversed and functionally verified.

**What is special (content that carries its own code/decoders, not a format gap):**
1. **Embedded-binary GMEs** (hdr@0xA8/0x98, states 67/69): the loader + `system_api` vtable are
   reversed, but such GMEs run their own ARM code — a GME that ships a ZC3202N main binary supplies
   that blob and drives the pen through the `system_api` vtable.
2. **Codec-0x13 system-voice** (media-pipeline §6): a special voice decode path distinct from the
   ordinary media codecs.
3. **Undocumented-but-handled minors** (0xFFA1, 0xFFFC, type-12 dual playlist): the *firmware*
   handles them; only external re-implementations (tttool) lack them — no risk to running on the
   real firmware.

**Bottom line:** the GME **format** model is complete. Remaining risk is confined to the two
already-tracked *runtime* items (embedded binaries, codec-0x13), not to any unreversed opcode,
table, or media type.

---

## 7. Corrections / new findings folded in

- **NEW**: raw→real XOR is a **256-byte LUT @0x080381da**, `real = LUT[hdr@0x1C]`, `LUT[0x39]=0xAD`
  (resolves tttool GME-Format.md §165 "unknown algorithm or lookup table").
- **NEW**: header **0x8C** = media-flag ("VoiceNumberNeedRep") table — `play_media` repeat-suppression.
- **NEW**: GME **type-12** carries a second playlist (`gme_parse_media_offsets2` / `gme_parse_playlist`
  second half).
- **NEW / undocumented opcodes**: **0xFFA1** (coin-flip play, resolved 2026-07-20 — see §1), **0xFFFC** (Eq-alias condition).
- **CORRECTION**: `FUN_08077230` @0x08077230 is the **power-on-sound playlist parser+player**
  (hdr@0x71), not the media-XOR-key setup as labelled in book-discovery-and-load.md §6. The XOR key
  setup is the inline `*0x08121a80 = LUT[ctx+0x134]` in `gme_parse_header` / `gme_oid_dispatch`.
- **CONFIRM**: `FUN_08034da0` opcode switch has no unreached/unnamed case; `gme_parse_actions`
  clamps to 8 actions/line.

---

**Correction (2026-07-19, game-type dispatch cross-check):** the "dual playlist when the
selector equals 0x0C" note in §3.1 is keyed on the current **product id** (`iVar19 =
*DAT_08036738`), **not** on game type 12 — game types 11–15 are unhandled by the dispatch
cascade (silent fall-through, like type 0). Further verified details from the same
cross-check: dispatch is on the **low byte** of the record's type word; types 1/3/5 have
**no** internal branch in the shared state-14 engine (true aliases; only `type==2`
hint-chain and `type==10` endless branch); type 16 has a product-id-2 variant (state 60,
fixed 8-question quiz over the same record layout). See also gme-subtype-parameters.md §0
addendum and the game-type discussion on nomeata/tip-toi-reveng PR #2.
