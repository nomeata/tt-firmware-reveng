# ZC3201 `system_api` — derivation notes (for building a ZC3201 dumper binary)

> **Status:** the resolved offsets below are now encoded in our own self-contained `api.h`
> (both pens, `#ifdef build_for_2N` / `build_for_ZC3201`), each 2N offset cited against the launcher
> `gme_launch_binary_build_sysapi@0x080a03f0`. The build uses no external SDK; the mentions of
> tt-homebrew's `api.h` in this file are *historical RE cross-checks*, not a build dependency.

Source: ZC3201 launcher `FUN_08095090` (twin of the 2N `gme_launch_binary_build_sysapi`
@0x080a03f0), loader `FUN_0809553c` (reads GME header **0xA0**). `prog.bin` loads at base
`0x08000000`. The launcher builds the struct on the stack from the pointer block at
`DAT_08095434…`, then calls `(*binary)(r0 = auStack_1c8)`, so **struct base = `auStack_1c8`**
(frame `-0x1c8`); a field at frame `-0xXXX` sits at struct offset `0x1c8 - 0xXXX`.

`tt-homebrew/lib/api.h` says the layout is *"the same on 2N and 3L, but fpAkOidPara differs"* —
it does **not** cover the ZC3201. Cross-checking the derived ZC3201 offsets against `api.h`:

## VERIFIED — same offsets as the 2N (code pointers, match api.h field indices)
| field (api.h #) | struct off | ZC3201 fn ptr | note |
|---|---|---|---|
| is_audio_playing (3) | **0x0c** | 0x080a0a88* | *actually the ptr at +0x0c; audio-check family |
| tbd4 (4) | 0x10 | 0x080a0a94 | |
| **open (5)** | **0x14** | 0x08008a48 | matches 2N open slot |
| **read (6)** | **0x18** | (block) | |
| **write (7)** | **0x1c** | 0x08009498 | matches 2N write slot |
| **close (8)** | **0x20** | 0x080096a8 | matches 2N close slot |
| seek (9) | 0x24 | (block) | |
| play_sound (11) | 0x2c | 0x0800c1c4 | |

So **open/write/close (and the fs family) are at the standard 2N offsets** — the file-writing
core of the dumper is safe to reuse.

## DIVERGENT / UNCERTAIN — must be pinned before building a real ZC3201 binary
- **fpAkOidPara (api.h field 13, 2N offset 0x34).** In the ZC3201 launcher the AkOidPara pointer
  is handled at a *different* place: `local_18c = *(gbase+0x40)+0x22` (struct **+0x3c**) and
  `local_180 = gbase+0x40` (struct **+0x48**) — i.e. ZC3201's fpAkOidPara is NOT at 0x34. The
  binary's once-guard `api->fpAkOidPara[First_time_exec]` writes through this pointer, so a wrong
  offset writes to the wrong address. **Disambiguate +0x3c vs +0x48 (and what each points at)
  before use.**
- **First_time_exec index.** The ZC3201 loader clears AkOidPara `+0xd8c/+0xd8e` (vs 2N
  `+0xdec/+0xdee`), so `FIRST_TIME_EXEC = 0xd8c` for ZC3201 (2N uses 0xdec). High confidence.
- **play_chomp_voice (2N field 99 = offset 0x18c).** ZC3201 struct **+0x18c is a DATA pointer**
  (0x081d8716), not a function — so the ZC3201 voice-cue function is at some other offset (or the
  cue must be dropped for ZC3201). Non-fatal (cosmetic), but don't call 0x18c on ZC3201.
- **Load address + entry.** 2N loads to 0x08132000; ZC3201's load region is whatever
  `FUN_08023324` returns in the launcher — derive the ZC3201 `BINARY_OFFSET` for its linker script.

## To build `maskrom_ZC3201.bin` safely
1. Confirm fpAkOidPara offset (+0x3c or +0x48) and that indexing it by 0xd8c lands in AkOidPara.
2. Find the ZC3201 voice-play field offset (or `#ifdef` out the cue for ZC3201).
3. Derive the ZC3201 load address → a `ZC3201.ld` mirroring `2N.ld`.
4. Add a `build_for_ZC3201` path to `main.c` (fwhead base = ZC3201 Bios base; the current `#else`
   0x00800000 is the 3L base, not verified for ZC3201).
5. `inject_gme.py -b ZC3201=maskrom_ZC3201.bin -b 2N=maskrom_2N.bin` → dual-pen GME (mechanism
   already verified).

The GME side (table format, dual-table injection) is done and tested; only the ZC3201 *binary*
is gated on the two uncertain offsets above.

---
## RESOLVED (built + emulator-tested)
- **fpAkOidPara = struct +0x3c** = `akoidpara+0x22` (launcher `local_18c = *(gb+0x40)+0x22`; loader
  and launcher agree `gb+0x40`=akoidpara). Used as the once-guard `char*`.
- **First_time_exec = 0xd6a** = `0xd8c - 0x22` → `fpAkOidPara[0xd6a]` = `akoidpara+0xd8c`, which the
  loader `FUN_0809553c` clears at launch (guaranteed 0). Confirmed: once-guard reads 1 after run.
- **Binary load address = 0x080ea000** (`FUN_08023324` → `DAT_08023370`), 64 KB cap → `ZC3201.ld`.
- **fwhead base = 0x08000000** (ZC3201 firmware base, like 2N — not the 3L 0x00800000).
- **Cues:** ZC3201 `play_chomp_voice` slot unverified → build is **dump-only** (no audio).
Deliverables: `main_zc3201.c`, `ZC3201.ld`, `maskrom_ZC3201.bin` (704 B). Emulator test on a dual GME
(`make_dumper_gme.py -b 2N= -b ZC3201=`) → ALL CHECKS PASS for both pens.

---
## system_api map + the audio-cue finding (verified 2025)

**Method:** parsed both launchers' slot→pointer assignments; 2N struct base = `local_1b4`
(`(*buf)(&local_1b4)`), ZC3201 base = `auStack_1c8` (`(*pcVar3)(auStack_1c8)`); struct_off =
base_frame − local_frame. Calibrated on the 2N against KNOWN sysapi fn addrs — exact match:
is_audio_playing 0x0c=0x0800a024, open 0x14=0x0800ced4, write 0x1c=0x080aaec8, close
0x20=0x080aaef8, play_sound 0x2c=0x080a9198.

**ZC3201 low-region sysapi (VERIFIED, code pointers):**
| off | ZC3201 fn | name |
|---|---|---|
| 0x0c | 0x080a0a88 | sysapi_is_audio_playing |
| 0x14 | 0x08008a48 | sysapi_open |
| 0x1c | 0x08009498 | sysapi_write |
| 0x20 | 0x080096a8 | sysapi_close |
| 0x2c | 0x0800c1c4 | sysapi_play_sound (needs a filehandle → GME media) |

**The play-built-in-voice cue could NOT be verified — and api.h's `play_chomp_voice` is a
mislabel on our firmware image.** The compiled 2N binary's cue calls `[sysapi+0x18c]` (ground
truth, disasm of `maskrom_2N.bin`: `ldr r3,[r3,#0x18c]; mov r0,#1|#6`). In our `firmware_full.bin`
the launcher stores at struct 0x18c a pointer to **string data** (0x080a9390 = UTF-16 `"ook…"`;
the neighbour 0x198 = 0x080a9fdc = `"…herlands"`). So field-99 `play_chomp_voice` in tt-homebrew's
`api.h` does not point at a voice function in this firmware version — the sysapi layout above field
~0x30 does not match api.h here (likely a firmware-version difference).

**Consequences:**
- No trustworthy media-independent "play built-in voice" slot was found → the ZC3201 dumper stays
  **dump-only** (do not wire an unverified slot; a wrong call faults the pen).
- The **2N dumper's own audio cue is suspect**: it calls `[sysapi+0x18c]`, which is a *string
  pointer* in our firmware image, not a voice function. The emulator test only counts the call, so
  it can't catch this. The "audible start/finish" feature should be treated as UNVERIFIED on both
  pens until the real voice function + its (version-correct) sysapi slot are identified.
- The dump path itself is unaffected — open/write/close/is_audio_playing are verified in both fw.

## Update: p_filehandle_current_gme (field 14) + media playback (resolved)
- **ZC3201 `p_filehandle_current_gme` = sysapi +0x4c** (verified): launcher `FUN_08095090` line
  `local_17c = DAT_08095458`; struct offset = `0x1c8 - 0x17c = 0x4c`. `DAT_08095458` is the pointer
  the loader `FUN_0809553c` dereferences (`fs_seek(*DAT_08095458, 0xa0, 0)`), i.e. the current-GME
  filehandle pointer. (2N has this at field-14 offset 0x38; the ZC3201 struct has 3 extra fields
  between fpAkOidPara@0x3c and the filehandle.)
- read@0x18, seek@0x24, play_sound@0x2c are fields 6/9/11 — before the divergence — so identical to
  the 2N and already verified. That is the full set needed to play bundled media:
  `play_sound(*p_filehandle_current_gme, offset, length)` after reading header[4] -> media table.
- **Consequence:** the ZC3201 dumper now has the SAME verified audio cue as the 2N (shipped media via
  play_sound), replacing the earlier dump-only state. The old play_chomp_voice slot (0x18c string ptr)
  is no longer used by either build.
