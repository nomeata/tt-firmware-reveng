# Contributing / how the pipeline works

The whole project is built so that **the named decompilation is a pure function of two
committed inputs** — `input/names.csv` and `input/ghidra_types.h` — applied to a firmware
image you obtain yourself. Nothing derived from the firmware is committed; it is all
regenerated on demand.

## The golden rule

**Never commit the firmware or anything derived from it.** The `.gitignore` blocks
`fw/*/data/`, `fw/*/out/`, `fw/*/ghidra/`, and stray `*.upd`/`*.bin`/`*.img`. Before every
commit, sanity-check that `git status` shows no firmware images, decompiled C, or extracted
binary dumps. If you write a new doc, keep it to commentary, addresses, struct layouts and
short illustrative snippets — not wholesale pasted decompiler output or extracted string/byte
tables.

## Data flow

```
firmware.json ──tools/fetch_firmware.py──▶ data/PROG.bin, data/nandboot.bin   (git-ignored)
                                                     │
                              tools/make_base.sh (once) │  Ghidra import + auto-analysis
                                                     ▼
                                            ghidra/base   (pristine, git-ignored)
                                                     │
input/names.csv ─────┐                 tools/regen.sh │  copies base → work, then:
input/ghidra_types.h ┼──────────────────────────────▶│    1. apply_sigs.py  (signatures, enums)
                     │                                │    2. ghidra_rename.py (names, structs,
                     │                                │       docstrings, typed globals, decompile)
                     ▼                                ▼
              (edited by hand)              out/decomp_named/*.c   (git-ignored output)
```

* **`tools/fetch_firmware.py`** — reads `fw/<variant>/firmware.json`, downloads the `.upd`
  if absent, verifies size + SHA-256 (catching truncated downloads), and slices the blobs
  out at the manifest offsets, verifying each. All output is git-ignored `data/`.
* **`tools/fwenv.sh`** — the single source of truth for per-variant paths, retargeted by the
  `FW` environment variable (default `fw/2N-update3202MT`). It reads `loader_base` from the
  manifest and exports the paths the Ghidra scripts consume.
* **`tools/make_base.sh`** — one-time: imports `PROG.bin` at `loader_base` and runs Ghidra
  auto-analysis into a cached pristine `ghidra/base`. No manual edits.
* **`tools/regen.sh`** — the pure regen: copies the base into a throwaway `ghidra/work`,
  applies signatures then names/types/docstrings, and decompiles into `out/decomp_named/`.
  It is **atomic and fail-loud**: output is swapped into place only on verified completion.
* **`tools/gme_inventory.py`** — rebuilds the GME call-graph coverage inventory
  (`docs/gme-callgraph-inventory.{md,tsv}`) from the regenerated decompilation + the types
  header.

Static-analysis helpers (`fw.py` capstone disassembly, `xref.py` literal/ADR cross-refs,
`analyze.py`) and `make_names.py` (assembles `names.csv` from evidence) honour the same `FW`
variable.

## Editing the databases

* **`input/names.csv`** — `address,name` per line (hex address, at `loader_base`-relative
  runtime addresses). One name per address.
* **`input/ghidra_types.h`** — a C-ish header consumed by `apply_sigs.py`/`ghidra_rename.py`:
  * `struct` definitions (applied to typed globals / context pointers);
  * function prototypes (applied by address, via the name in `names.csv`);
  * `enum Name { M = 0xNN };` blocks (rendered symbolically in the decompilation);
  * `/* 0xADDR name : one-line docstring */` plate comments;
  * directives: `@slot` (type a literal-pool constant), `@local <fn> <sel> name [type]`
    (rename/retype a local or parameter), `@noreturn <addr>`.

After editing, `FW=<variant> tools/regen.sh` and read `out/decomp_named/` to confirm the
result reads cleanly — readability of the regenerated C is the acceptance criterion.

## Adding a firmware variant

1. Create `fw/<variant>/` with `input/` (start with empty `names.csv` / `ghidra_types.h`).
2. Write `fw/<variant>/firmware.json`: the `.upd` URL(s), its size + SHA-256, the
   `loader_base`, and the blob offset/size/SHA-256 table. Verify the offsets by extracting
   from a real `.upd` before committing the checksums.
3. `tools/fetch_firmware.py <variant>` and `FW=fw/<variant> tools/make_base.sh`, then start
   naming.

## The emulator lives elsewhere

Running the firmware (as opposed to statically naming it) is [`tt-emu`](https://github.com/nomeata/tt-emu).
Findings here that were confirmed dynamically say so, but this repository does not carry the
emulator harness.
