# 2N-update3202MT — firmware provenance

The **ANYKANB1 "2N" tiptoi pen firmware, MT variant** (chip ZC3202N, Micron NAND). This is
the flagship analysis target: fully named, and the build that matches the maintainer's real
pen. The PROG image begins with the version string `N0038MT` / build date `20131009` — a
later 2013 build than the sibling `2N-Update3202` (`N0038` / `20120419`).

## How to obtain it

Nothing here is committed; `tools/fetch_firmware.py` obtains and verifies it:

```bash
tools/fetch_firmware.py 2N-update3202MT
```

This downloads the vendor update file from Ravensburger

    https://cdn.ravensburger.de/db/firmware/update3202MT.upd

(or uses an existing copy already placed at `data/update3202MT.upd`), verifies it, and
slices out the blobs. All output lands in the git-ignored `data/`.

If you already have the `.upd` (e.g. from the official tiptoi Manager or your pen), drop it
at `data/update3202MT.upd` and the script will verify rather than re-download.

## `.upd` container layout

The vendor `.upd` is an `ANYKA106` container that describes its own contents: the header
gives the producer / NANDboot / PROG offsets and sizes, and a small table of records names
the images (`PROG`, `codepage`). `tools/fetch_firmware.py` reads these from the container —
nothing here is hard-coded in the manifest. The blobs and their byte ranges for this build:

| file          | .upd offset | size (bytes)          | role |
|---------------|-------------|-----------------------|------|
| `producer.bin`| `0x3400`    | 117,500  (0x1CAFC)    | factory USB-flashing / provisioning stage |
| `nandboot.bin`| `0x20000`   | 32,384   (0x7E80)     | `ANYKANB1` NANDboot blob; mapped at `0x07ff8000` |
| `PROG.bin`    | `0x28000`   | 3,670,016 (0x380000)  | **full firmware image**; the primary Ghidra import |
| `codepage.bin`| `0x3A8000`  | 879,820  (0xD6CCC)    | GB/codepage tables |

Only `PROG.bin` (the image to name) and `nandboot.bin` (which supplies the bootrom HAL
names) are used by the naming pipeline.

### Loader base

`PROG.bin` is a flat load: file offset `F` maps to runtime address `0x08009000 + F`. The
Ghidra image base (`loader_base` in the manifest) is therefore **`0x08009000`**, so Ghidra
addresses equal the true runtime addresses (and the pen's RAM dumps, and the emulator).
Verified anchors: `open` at file `0x4e20` → runtime `0x0800de20`; `play_media` at file
`0xa27b4` → runtime `0x080ab7b4`.

## Checksums

The `.upd` SHA-256 is pinned in `firmware.json` and verified by `tools/fetch_firmware.py`;
because the blobs are a deterministic slice of the verified `.upd`, their checksums follow.

```
8e37af0a3d3c126189447964784fd84ccf0356cb7425b5ab478e86b3352741f9  update3202MT.upd
252777cbe66d310ebf920f90df2aa79e4c9f62f0f2de5d8126dce6a96377aa2c  PROG.bin
fec9d9ab0bd364c153ad69fa326ca6bb91b8fd69d701d4179f59a49b01f60477  nandboot.bin
d64d7c8a296872815fed5d20f74809ea1140702527cc901f03342df1adffa322  producer.bin
db4549ffa8ab47cb0358b744112872d7006dcdaf82dceaed81227c49fe001cf9  codepage.bin
```

## Not from the `.upd`

The 2N boot **mask ROM** (captured from real hardware) is *not* part of this container and
is not required by the naming pipeline. Analyses that need it (deep boot-from-reset work)
obtain it separately via a hardware capture; it is not distributed here.

## Relation to the real pen

Of the 106 in-PROG-range function pointers in a real pen's live mask-ROM sysapi table, 101
appear as literal words inside this MT `PROG.bin` (vs only 35 in `2N-Update3202`), and every
pointer the older build accounts for, MT also accounts for. The pen's sysapi sub-table for
entries `+0x6c..+0xac` is present verbatim in MT and absent from `2N-Update3202`. **The real
pen is the MT family.**
