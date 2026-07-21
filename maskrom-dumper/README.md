# maskrom-dumper — recover a tiptoi pen's on-chip boot mask ROM from real hardware

This is a small **homebrew program that runs on a real tiptoi pen and dumps the pen's on-chip
boot mask ROM (and a few other boot fingerprints) to the USB drive** — no soldering, no JTAG, no
UART. It ships as a normal `.gme` product file: you copy it onto the pen's USB storage, power the
pen on, and it writes the captured regions to the drive's root, which you then read back on your PC.

Everything here is **our own code**, derived from our reverse engineering of the pen firmware. It is
included in this repository as **reference material** for anyone who wants to recover the mask ROM
from their own hardware, and as a worked example of building and running a homebrew ARM binary
against the firmware's `system_api`.

> The dumped mask ROM itself is **vendor firmware** and is deliberately **not** included here — this
> repository ships only the tool that lets you capture your own. See the repository root README for
> the project's rule against committing anything firmware-derived.

## Why the mask ROM matters

The tiptoi pen's SoC (an Anyka ARM926 part) boots from a small **mask ROM** baked into the chip. That
ROM is the *keystone* of the boot chain — it is the very first code that runs and it sets up the
early hardware — but it is **not contained in the `.upd` firmware update** you can download from
Ravensburger. The firmware analysis in the rest of this repository can reconstruct almost the whole
system from the `.upd`, but the on-chip boot ROM has to come from **real silicon**. Historically that
meant JTAG or chip-off. This tool recovers it purely in software, using a capability the firmware
already hands to any embedded GME binary: the `system_api` file-write and memory-read primitives.

At runtime the pen's low memory (from address `0x00000000`) reads back as this boot ROM, so the tool
simply reads that range and writes it to a file — carefully (see **Safety**).

## What it captures

When the pen loads the product, the binary runs **once** (guarded by the firmware's
`First_time_exec` flag) and writes to the **root of the USB drive** (`B:/`):

| file(s) | contents |
|---|---|
| `mrom_XXXXXXXX.bin` | the boot **mask ROM** — progressive dumps of `[4 .. size)`, one file per size (hex), smallest first (see Safety). The **largest** one is the real capture. |
| `mrom_sysapi.bin` | the live `system_api` struct handed to the binary (0x200 B) — a firmware fingerprint |
| `mrom_fwhead.bin` | the firmware base header (0x100 B) — a version fingerprint |
| `cpregs.bin` | CP15 system-control registers (MIDR/SCTLR/TTBR0/DACR/…), a fixed 32-byte struct — proves MMU/cache state; zero-risk register reads |
| `l1pt.bin`, `l2_XXXXXXXX.bin` | the live MMU page tables (L1 + walked L2 tables), if the MMU is on |
| `mXXXXXXXX.bin` | a handful of live firmware RAM regions, named by hex start address (boot/runtime fingerprints) |
| `TTMDUMP.LOG` | a growing text log, rewritten after every step — a crashed run still leaves a complete log up to the last step |
| `TTMDONE.TXT` | written when the safe phase completed |

The **mask ROM** is the headline deliverable; the other files are cheap, useful boot/hardware
fingerprints captured in the same pass. Reading the mask ROM at low memory is the reason the tool
exists; the CP15 and page-table captures explain *why* low memory reads back as ROM (they record the
MMU state directly, at no risk).

## Safety — why it dumps progressively

Two hardware facts, established empirically on real pens, drive the whole design:

1. **Reading *unmapped* memory powers the pen off** (a hardware fault, not a hang).
2. **Reading address `0` faults**, but address `4` works — bytes 0–3 (the reset vector) are skipped.

Also: the firmware's `write()` **hangs on writes ≥ ~1 MB**, so every write is a single ≤ 32 KB chunk.

So the tool is written **safe-first**: it dumps the zero-risk CP15 registers, then the live page
tables (guarded to firmware-RAM addresses), then the fingerprints, and **last** the progressive
mask-ROM probe — reading *increasing* ranges into *separate* files, **smallest first, flushing each
before the next larger read.** If a large read runs past the end of the mapped ROM and the pen powers
off, every smaller file is already safely on disk, and `TTMDUMP.LOG` (flushed per line) names the last
size that was attempted. A known brick-risk region (the low firmware hole below `0x08007000`) is
explicitly excluded, and the one untested address is ordered **last** so a fault there cannot cost the
rest of the run.

➡️ **After a run, keep the largest `mrom_*.bin`** — its size tells you how much ROM is mapped.

### Confirm it's really the mask ROM

The one assumption is that low memory at runtime is the mask ROM (not RAM remapped over `0x0`). Check
the bytes: **real ROM** starts with ARM branch instructions and contains `ANYKA`/`DESIGNE` strings and
the snowbird boot code; **remapped RAM** would look like a tiny vector table then zeros. `cpregs.bin`
records the MMU/TTBR0 state so you can verify the mapping directly.

### NAND is out of scope

The NAND flash sits behind its controller (not memory-mapped) and the `system_api` exposes no
NAND-read call, so it is **not** dumped here — a safe NAND dump would need version-specific bootrom HAL
internals that can brick the pen.

## Dependencies

The build is **self-contained** and needs only:

- **`arm-none-eabi-gcc`** + **`arm-none-eabi-objcopy`** (a bare-metal ARM toolchain)
- **`python3`** (standard library only — for the GME builder and the cue-sound generator)

It deliberately does **not** depend on any external homebrew SDK. The `system_api` layout (`api.h`),
the minimal SDK (`sdk.c`), the linker scripts (`2N.ld` / `ZC3201.ld`), and the GME builder
(`make_gme.py` / `inject_gme.py`) are all **our own**, derived from the firmware reverse engineering in
this repository — no [`tt-homebrew`](https://github.com/gauweiler/tt-homebrew) `build.js` / `gmelib` /
Node, and no reference `.gme`. (`tt-homebrew` is the community GME-homebrew toolchain; this project
reimplements just the pieces the dumper needs so it can stand alone here.)

The optional emulation test (`test_dumper_gme.py`) additionally needs the **Unicorn** CPU emulator:
`pip install unicorn`.

## Build

```sh
./build_dumper.sh            # -> dumper.gme  (2N + ZC3201, with start/done cue sounds, product id 42)
```

That one script does everything: generate the cue sounds → regenerate the media header → compile the
per-pen binaries (one `main.c`, `api.h` selects the layout via `-Dbuild_for_2N` / `-Dbuild_for_ZC3201`)
→ build a **dual-pen** `dumper.gme` and self-verify it (it re-extracts each binary and round-trips
each media file exactly as the firmware would). Pass a different product id as `./build_dumper.sh 123`.

`build/`, `dumper.gme`, and the generated `sounds/*.wav` are build artifacts (git-ignored); regenerate
them anytime with the script.

## Run it on a real pen

```sh
./build_dumper.sh
mkdir -p "/run/media/$(whoami)/tiptoi"                         # your pen's USB mount point
cp dumper.gme "/run/media/$(whoami)/tiptoi/ANYGAME.gme"        # onto the pen's USB drive
# unmount and unplug
```

Then **power the pen on** with this as the loaded product. You will hear:

**start beep → a few seconds of dumping → a repeating done beep = safe to power off.**

Power off, reconnect the pen's USB, and copy the `mrom_*.bin` / `cpregs.bin` / `l1pt.bin` / `*.LOG`
files off the drive root. Keep the **largest** `mrom_*.bin` (that is the mask-ROM capture) plus
`cpregs.bin` and `mrom_sysapi.bin`/`mrom_fwhead.bin` as fingerprints.

The audio cues are our own synthetic beeps (`gen_sounds.py`, IMA-ADPCM WAV — no vendor audio bundled).
Audio decode is a hardware behaviour we cannot verify in emulation; worst case a cue is silent (it
cannot fault). If you hear nothing, confirm success by the files on the drive instead of by the beep.

### Product id / OID

The `.gme` is built for **product id 42** by default, which matches the freely-available
"taschenrechner" OID printout, so you can trigger the product with that sheet. Build with a different
id (`./build_dumper.sh <id>`) to match a printout you have.

## Optional: emulate before running on hardware

```sh
pip install unicorn
./build_dumper.sh
python3 test_dumper_gme.py --gme dumper.gme --pen 2N        # and --pen ZC3201
```

`test_dumper_gme.py` extracts the binary from the `.gme` exactly as the firmware loader would, runs it
under a Unicorn ARM emulator against a synthesized `system_api`, and asserts that it writes the
expected files and calls `play_sound` with the correct media indices. It cannot decode audio or model
real hardware faults, so treat it as a structural check, not a substitute for the safety design above.

## `system_api` layout (per pen)

One `main.c` builds for both pen generations; `api.h` selects the struct layout, the `First_time_exec`
guard index, and the firmware base. All offsets are cited in `api.h` against the firmware launcher
`gme_launch_binary_build_sysapi` (2N `@0x080a03f0`, ZC3201 `@0x08095090`):

- **Shared (fields 0–11)** — verified identical on both pens: `is_audio_playing`@0x0c, `open`@0x14,
  `read`@0x18, `write`@0x1c, `close`@0x20, `seek`@0x24, `play_sound`@0x2c.
- **2N**: `fpAkOidPara`@0x34, `p_filehandle_current_gme`@0x38, `First_time_exec`=0xdec, binary loads at
  `0x08132000`.
- **ZC3201**: the struct diverges above field 11 — `fpAkOidPara`@0x3c (→ `akoidpara+0x22`),
  `p_filehandle_current_gme`@0x4c, `First_time_exec`=0xd6a, binary loads at `0x080ea000`.

The full derivation of the ZC3201 offsets (and the story of an earlier mislabelled audio slot) is in
**[`zc3201_sysapi_notes.md`](zc3201_sysapi_notes.md)**.

## Files

| file | role |
|---|---|
| `main.c` | the dumper itself (both pens; regions, CP15, page-table walk, progressive mask-ROM probe) |
| `api.h` | our `system_api` struct layout, per pen (derived from firmware RE) |
| `sdk.c` | minimal SDK — holds the api pointer, plays bundled media by index |
| `startup.s`, `2N.ld`, `ZC3201.ld`, `Makefile` | bare-metal build glue (one binary per pen) |
| `make_gme.py` | build a complete `.gme` from binaries + media (synthesizes the firmware-valid header) |
| `inject_gme.py` | inject an embedded ARM binary into a `.gme` binary table (used by `make_gme.py`) |
| `gen_sounds.py` | generate the start/done cue sounds as IMA-ADPCM WAV (our own, no vendor audio) |
| `gme_media.h` | generated `MEDIA_*` index defines (regenerated by the build from the media list) |
| `build_dumper.sh` | one-command end-to-end build (sounds → binaries → dual-pen `dumper.gme`, self-verified) |
| `test_dumper_gme.py` | optional Unicorn emulation test of the built `.gme` (needs `pip install unicorn`) |
| `zc3201_sysapi_notes.md` | ZC3201 `system_api` derivation notes |
