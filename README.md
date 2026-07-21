# tt-firmware-reveng

Reverse-engineering of the **tiptoi® pen firmware** — the ARM firmware inside the
Ravensburger tiptoi audio pens — with a focus on the **GME file format** and the
pen's **script interpreter and game engine**.

This is a companion to [tip-toi-reveng / `tttool`](https://github.com/entropia/tip-toi-reveng)
(which reads and writes GME files) and to [`tt-emu`](https://github.com/nomeata/tt-emu)
(which emulates the pen and runs real firmware). Where those tools treat the pen as a
black box, this repository documents **how the firmware itself works**, verified against
the real silicon.

## Status

All of the reverse engineering, tooling, and documentation here was produced by **Claude
(Anthropic's AI), working under human supervision** — directed, spot-checked, and corrected,
but not hand-written. Findings were cross-checked against the decompilation and, where it
mattered, against the real firmware running in the [`tt-emu`](https://github.com/nomeata/tt-emu)
emulator; but treat them as a well-supported map, not gospel.

I do not plan to spend much time maintaining or extending this. Issue reports are welcome
but may not get quick action. Because the work is AI-generated, code contributions are not
the ideal form of help — the most useful contributions are precise, detailed corrections or
careful descriptions of what to investigate, which can be fed back to an AI. Empirical
evidence always outranks a decompilation reading.

## What is (and isn't) in this repository

This repository contains **our reverse-engineering work**, not the vendor firmware:

* **naming & signature databases** — for each firmware, an `input/names.csv`
  (address → function/global name) and an `input/ghidra_types.h` (struct layouts,
  function prototypes, enums, and docstrings). These are the heart of the project.
* **a reproducible pipeline** (`tools/`) that applies those databases to the firmware
  in Ghidra and regenerates a fully-named decompilation.
* **documentation** (`<variant>/docs/`) of the findings — the GME format, the script
  opcodes, the game types, the hardware interfaces, the storage stack, and boot.
* **cross-firmware records** — [`correspondences.tsv`](correspondences.tsv) maps equivalent
  functions across the variants (so a name recovered from one firmware's retained symbols can
  be carried to another), and [`firmware-differences.md`](firmware-differences.md) logs
  noteworthy differences in what the *pen* does between generations.

It deliberately contains **no vendor firmware and nothing mechanically derived from it**
(no firmware images, no decompiled C, no extracted string/byte dumps). The firmware is
copyrighted by Ravensburger/Anyka; you obtain it yourself, and the tooling verifies it by
checksum. Everything the pipeline produces lands in git-ignored directories.

## Quick start (flagship variant: 2N-update3202MT)

With [Nix](https://nixos.org/) (flakes enabled) for deterministic dependencies:

```bash
nix develop                              # ghidra + python(+capstone) + tools on PATH

tools/fetch_firmware.py 2N-update3202MT  # download the .upd, verify it, unpack the blobs
FW=2N-update3202MT tools/make_base.sh # one-time Ghidra auto-analysis (a few minutes)
FW=2N-update3202MT tools/regen.sh     # apply our names/types -> named decompilation
```

The result is a fully-named decompilation in `2N-update3202MT/out/decomp/`
(git-ignored). Regenerate the GME call-graph coverage inventory with:

```bash
FW=2N-update3202MT tools/gme_inventory.py
```

Without Nix you need: Ghidra 11.x (providing `ghidra-analyzeHeadless` on `PATH`),
Python 3, and `python3 -m pip install capstone pyyaml` for the static-analysis helpers.

See **[CONTRIBUTING.md](CONTRIBUTING.md)** for how the pipeline fits together and how to
add or extend a variant. The findings are documented under
**[`2N-update3202MT/docs/`](2N-update3202MT/docs/README.md)**.

## Firmware variants

Each analyzed firmware is a self-contained directory at the repository root, sharing the
`tools/` pipeline. A single environment variable `FW` retargets everything.

| variant | pen | status |
|---------|-----|--------|
| **`2N-update3202MT`** | 2nd gen, ZC3202N (Micron NAND), build 20131009 | **flagship** — fully analyzed; matches a real pen |
| `2N-Update3202` | 2nd gen, ZC3202N, earlier build 20120419 | work in progress |
| `ZC3201` | 1st gen (no recording) | work in progress — regenerable; ~220 named. Its retained vendor symbol strings make it the authoritative-name source that feeds the others |
| `3L-Update3203L` | 3rd gen, ZC3203L (audio player) | stub |

Each `<variant>/` holds:

```
firmware.json    manifest: .upd download URL + checksum + loader base
FIRMWARE.md      provenance: where the firmware comes from, how it is unpacked
input/           our RE databases: names.csv, ghidra_types.h        (committed)
docs/            findings and commentary                            (committed)
data/            the .upd and blobs extracted from it               (git-ignored)
out/             generated named decompilation                      (git-ignored)
ghidra/          Ghidra projects                                    (git-ignored)
```

## Legal

The naming, signatures, documentation and scripts here are our own work, released under
the [MIT License](LICENSE). They describe the firmware but do not reproduce it. The
firmware images remain the property of their respective rights holders and are not
distributed here; `tools/fetch_firmware.py` downloads them from Ravensburger's own servers
and verifies them by checksum.
