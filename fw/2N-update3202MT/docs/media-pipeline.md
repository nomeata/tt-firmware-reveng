# Media / audio playing pipeline (MT firmware)

Full chain from "OID tap / state action" to sound out of the speaker — the high-level overview;
the register-level audio-out boundary is in `audio-output-hw.md`. All addresses are the unified
runtime address (Ghidra base = runtime = 0x08009000). Confirmed against the working audio (a book's
OGG cue plays on real hardware).

## 1. Trigger → dispatch
OID tap → `gme_oid_dispatch` (0x0803629c) / statechart handler → the state's action calls the sysapi
`play_sound` = **`play_media` (0x080ab7b4)**. (Voice/OID prompts go via `fwl_voice_play`/`fwl_voice_play_2`
0x080ab500/0x080ab620 → `fwl_play_voice_by_id` → the same player.)

## 2. play_media (0x080ab7b4)  — args (filehandle, offset, length)
Loops the media-file table to match the sound, stores (offset,length) into the repeat buffers, does
`fs_seek(handle,0x8c,0)` to consult the media-flag table, then calls **`play_media_setup`**. (Preempts;
does not gate on `is_audio_playing`.)

## 3. play_media_setup → aud_player_set_source (0x080329b0)
Requires the audio-player object (`g_pAudPlayer`), reads the volume byte, builds a media descriptor
{filehandle, offset, size, fs read/seek callbacks}, stores the codec id at **player+0xe9**, and calls
`medialib_open`. **If `medialib_open` returns 0, nothing plays and no error propagates → silence.**

## 4. medialib_open (0x080f0e20) — codec detect + decoder install
`medialib_detect_codec` (0x080ff3d8) sniffs the **first 0x40 DECRYPTED bytes** and installs the
per-codec decoder vtable. Supported codec ids: **2/3/8/0x0a = WAV/PCM/IMA-ADPCM**, 0x0d = AMR,
0x11 = FLAC, **0x12 = Ogg/Vorbis**, 0x14 = video. Sniffers: `medialib_detect_wav` (0x080feebc,
RIFF/WAVE/fmt + fmt-tag), `medialib_detect_asf` (0x080ff058), and OggS/fLaC/#!AMR probes. Unknown id →
"MediaLib: ERROR: Open: Unsupported" → return 0 → silence.

## 5. Read decryption (the audio-only XOR)
All media reads go through the fs read callback → `fs_read_xor_decrypt` (media magic **0xAD**, pass-through
{0x00,0xFF,0xAD,0x52}). The GME header stores the RAW xor 0x39 @+0x1C; the firmware derives 0xAD for
media. So `detect_codec` sees the plaintext (e.g. "OggS") only when the media was encrypted with 0xAD —
note the media XOR key (0xAD) differs from the GME header/table key (0x39).

## 6. aud_player_play (0x08032c94) → decode → output
`FUN_08031fa8` (setup) → if codec `player+0xe9 == 0x13` a special path `FUN_08038320`, else
**`medialib_play` (0x080f1c38)** → on success `FUN_08032c04` kicks off output. The engine is
**`aud_player_decode_loop` (0x080325dc)**: pulls encoded data via the fs+XOR read callback, decodes a
frame through the installed codec, buffers PCM.

## 7. Codec decoders (present in MT)
- **IMA-ADPCM**: `ima_adpcm_decode` (0x0810e96c).
- **Ogg/Vorbis** (the dumper's cues): `ogg_page_scan` (0x0810b874), `vorbis_parse_id_header`
  (0x08104064), `meta_parse_vorbiscomment` (0x0810bc78), `oggpack_write` (0x08110408).
- **FLAC**: `flac_parse_metadata` (0x0810b9a4), `flac_read_streaminfo` (0x0810bd8c).
- **Anyka frame**: `akoid_decode_frame` (0x080ee6e4).

## 8. Output HW
`codec_clock_init` (0x0803abd4) + `audio_amp_enable`/`disable` (0x080abb0c/0x080ab3ec) bring up the
codec/amp; `aud_player_set_volume` (0x080abb60) sets volume. Decoded PCM is fed to the DAC via DMA —
the full register-level path (ring buffer, DMA submit, IRQ) is traced in `audio-output-hw.md`.
`is_audio_playing` (0x0800b024) reads the player status; `audio_wait_tick` (0x0800b2ec) is the
~event-loop pacing. `g_pAudPlayer` itself is constructed by state 13's ENTER hook
(`audio-player-construction.md`).

## Status
Pipeline understood end-to-end down to the DAC/DMA registers (`audio-output-hw.md`). This is why a
book's OGG cue plays: encrypted with 0xAD → detect finds OggS → Vorbis decoder → PCM → DAC.
