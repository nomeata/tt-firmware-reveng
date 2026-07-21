# ZC3201 — firmware provenance

The **1st-generation tiptoi pen firmware** (chip ZC3201; no recording / audiobook). Build
`v0136` / `120117` / `ZC3201`, `ANYKANB0` NANDboot. It shares the pen architecture and the
scripted-GME interpreter with the 2N, and — unlike the stripped 2N-MT build — it retained its
source-path and assert strings, which makes it the authoritative-name source that feeds the
other variants (see the repo-root `correspondences.tsv`).

## How to obtain it

```bash
tools/fetch_firmware.py ZC3201
```

Downloads the vendor `.upd` from Ravensburger, verifies it, and unpacks the `ANYKA106`
container (PROG at file `0x24000`, 1 MiB). All output lands in the git-ignored `data/`.

## Load base — 0x08008000 (verified; do not assume 0x08000000)

**PROG loads at runtime `0x08008000`, not `0x08000000`.** `file offset F → runtime
0x08008000 + F` (the `v0136…ZC3201` text header included, at the base). This mirrors the
2N-MT (`0x08009000`): the resident NANDboot HAL occupies the low RAM window
`0x08000000..0x08008000`, and PROG is loaded **above** it at `0x08008000`.

This was initially — and wrongly — taken to be `0x08000000`, which is a silent, poisonous
error (every function address off by `0x8000`) that also bit the 2N-MT. It is settled here by
a base-independent test: scanning the image's own absolute pointers and checking which load
base makes them resolve to function prologues. The rate peaks **sharply at `0x08008000`
(28.8%)** and is negligible elsewhere (`0x08000000`: 2.3%). The **same method recovers the
2N-MT's known-correct `0x08009000`** (19.1% vs 0.4%), validating it.

Consequences captured in the databases:
- `input/names.csv` function addresses are at the true base `0x08008000`; the two absolute
  data globals (`gb_app_context 0x0800779c`, `p_pMeGame_slot 0x081d8854`) are literal-pool
  values and are **not** base-relative, so they are left as-is. Pre-fix copies are kept as
  `*.base08000000.bak`.
- `firmware.json` `loader_base` is `0x08008000`; the pipeline imports PROG there.
- The `correspondences.tsv` `ZC3201` column uses `0x08008000`-base addresses.

## Checksums

The `.upd` SHA-256 is pinned in `firmware.json` and verified by `tools/fetch_firmware.py`;
the blobs are a deterministic slice of the verified container.
