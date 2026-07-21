# tt-firmware-reveng

Reverse-engineering of the **tiptoi® pen firmware** — the ARM firmware inside the
Ravensburger tiptoi audio pens — with a focus on the **GME file format** and the
pen's **script interpreter and game engine**.

This is a companion to [tip-toi-reveng / `tttool`](https://github.com/entropia/tip-toi-reveng)
(which reads and writes GME files) and to [`tt-emu`](https://github.com/nomeata/tt-emu)
(which emulates the pen and runs real firmware). Where those tools treat the pen as a
black box, this repository documents **how the firmware itself works**, verified against
the real silicon.

## What is (and isn't) in this repository

This repository contains **our reverse-engineering work**, not the vendor firmware:

* **naming & signature databases** — for each firmware, an `input/names.csv`
  (address → function/global name) and an `input/ghidra_types.h` (struct layouts,
  function prototypes, enums, and docstrings). These are the heart of the project.
* **a reproducible pipeline** (`tools/`) that applies those databases to the firmware
  in Ghidra and regenerates a fully-named decompilation.
* **documentation** (`fw/<variant>/docs/`) of the findings — the GME format, the script
  opcodes, the game types, the hardware interfaces, the storage stack, and boot.

It deliberately contains **no vendor firmware and nothing mechanically derived from it**
(no firmware images, no decompiled C, no extracted string/byte dumps). The firmware is
copyrighted by Ravensburger/Anyka; you obtain it yourself, and the tooling verifies it by
checksum. Everything the pipeline produces lands in git-ignored directories.

## Quick start (flagship variant: 2N-update3202MT)

With [Nix](https://nixos.org/) (flakes enabled) for deterministic dependencies:

```bash
nix develop                              # ghidra + python(+capstone) + tools on PATH

tools/fetch_firmware.py 2N-update3202MT  # download the .upd, verify it, unpack the blobs
FW=fw/2N-update3202MT tools/make_base.sh # one-time Ghidra auto-analysis (a few minutes)
FW=fw/2N-update3202MT tools/regen.sh     # apply our names/types -> named decompilation
```

The result is a fully-named decompilation in `fw/2N-update3202MT/out/decomp_named/`
(git-ignored). Regenerate the GME call-graph coverage inventory with:

```bash
FW=fw/2N-update3202MT tools/gme_inventory.py
```

Without Nix you need: Ghidra 11.x (providing `ghidra-analyzeHeadless` on `PATH`),
Python 3, and `python3 -m pip install capstone pyyaml` for the static-analysis helpers.

See **[CONTRIBUTING.md](CONTRIBUTING.md)** for how the pipeline fits together and how to
add or extend a variant. The findings are documented under
**[`fw/2N-update3202MT/docs/`](fw/2N-update3202MT/docs/README.md)**.

## Firmware variants

Each analyzed firmware is a self-contained directory under `fw/`, sharing the `tools/`
pipeline. A single environment variable `FW` retargets everything.

| variant | pen | status |
|---------|-----|--------|
| **`2N-update3202MT`** | 2nd gen, ZC3202N (Micron NAND), build 20131009 | **flagship** — fully analyzed; matches a real pen |
| `2N-Update3202` | 2nd gen, ZC3202N, earlier build 20120419 | work in progress |
| `ZC3201` | 1st gen (no recording) | work in progress — good cross-reference (retained symbol strings) |
| `3L-Update3203L` | 3rd gen, ZC3203L (audio player) | stub |

Each `fw/<variant>/` holds:

```
firmware.json    manifest: .upd download URL + checksums + blob offsets + loader base
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
