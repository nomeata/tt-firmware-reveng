# tiptoi 2N (MT) firmware — overview & understanding map

Single reference tying together what we know about the pen firmware (the **MT** build,
`N0038MT`/20131009, ZC3202N / Anyka Snowbird2 "ANYKANB2" chip). Detailed docs cross-referenced below.

> **Address base.** Every address in this doc set is the single unified runtime address: Ghidra image
> base = runtime = **0x08009000**; `input/names.csv` / `input/ghidra_types.h` match. Code, data and
> bss globals sit at their own fixed absolute addresses.

## 1. Which firmware the pen runs
The pen runs **update3202MT** (proven via the live sysapi-pointer match: 101/106 in MT vs 35 in
Update3202). Everything pen-related uses `fw/2N-update3202MT/`. `fw/2N-Update3202/` is the older 2012
sibling used for cross-reference.

## 2. Boot chain (maskROM → SPL → PROG)
- **Mask ROM** (base 0x0): ANYKA "SNOWBIRD2-BIOS". Reset→boot-mode strap (GPIO 0x040000BC)→storage
  boot (SPI-NOR/NAND), loads ONE image to 0x08000000, requires exact 8-byte magic **"ANYKANB2"**,
  jumps 0x08000000. The mask ROM is a hardware capture and is **not** included in this repo; its
  behaviour is documented in `maskrom-boot-vs-recovery.md`.
- **nandboot / SPL** (ANYKANB2 on the pen; the `.upd` carries the ANYKANB1 sibling
  `data/nandboot.bin`): inits HW/clock/NFTL, builds the low-RAM tables, then jumps to the PROG entry
  (0x08039100) — it does **not** itself stream PROG into RAM (the ROM/boot image placement covers
  that; proven in `nand-reconstruction-and-from-reset.md` §B.2). The ANYKANB1/ANYKANB2 digit is a
  **per-generation header stamp** on largely shared SPL code (`nandboot-generation-puzzle.md`); the
  mask ROM requires ANYKANB2, so the `.upd`'s SPL cannot cold-boot the pen. The pen's exact ANYKANB2
  SPL is the one artifact that cannot be dumped (0x07ff8000 reads back as zeros).
- **PROG** = the `.upd` "PROG" section (`data/PROG.bin`, 3.5MB; +`codepage` = the
  character-**encoding** conversion DB — a Win32-NLS-style codepage subsystem (61 codepages,
  MultiByteToWideChar-style) for FAT/UTF-16 filename conversion, NOT fonts/labels; see
  `codepage-what-is-it.md`).

## 3. Runtime memory model — the key structural fact
**PROG loads FLAT at base 0x08009000** — a single uniform image, no relocation of any kind. Ghidra
images `fw/2N-update3202MT` at exactly this base, so Ghidra addr == runtime addr for all code, and
data/bss globals sit at their own fixed absolute addresses. Runtime map:
**0x08000000-0x08009000** = nandboot low-RAM (resident HAL leaves + statechart CB + early globals);
**0x08009000+** = PROG flat; **0x07ffxxxx** = mapped HAL/BIOS. Confirmed dynamically (clean boot,
`app_init_main` returns; see [`tt-emu`](https://github.com/nomeata/tt-emu)) and cross-checked against
a live pen RAM capture (99.9% byte match of the `.data` tables,
`product-init-and-runtime-tables.md` §0).

## 4. Application architecture — hierarchical state machine
QHsm-style, table-driven (`statechart-framework.md`): `sm_dispatch_to_hierarchy` (0x080f2d70) walks
current→parent→grandparent; `sm_dispatch_event` (0x080f2c78) looks up each state's handler from a
descriptor table (AO+0x18), runs transition actions (AO+0x20). The AO (0x08008874) holds current
state, parent ptr, and the tables (descriptor table 0x08121d44). Framework event-queue primitives
live in the 0x07ffxxxx HAL (`func_0x07ffdbc8`/`dbd0`). The complete state map is in
`statechart-full-map.md`.

## 5. Media / audio (`media-pipeline.md`)
OID tap → `gme_oid_dispatch` (0x0803629c) / state action → **`play_media` 0x080ab7b4** → `play_media_setup` →
`aud_player_set_source` (codec@player+0xe9) → **`medialib_open`** (detect codec from first 0x40
*decrypted* bytes; WAV/PCM/IMA-ADPCM/AMR/FLAC/Ogg-Vorbis/video) → `aud_player_play` →
`aud_player_decode_loop` (0x080325dc) → codec decoder (`ima_adpcm_decode`, `ogg_page_scan`+vorbis,
flac) → PCM → DAC/DMA. **Media reads are XOR'd with magic 0xAD** (header stores raw 0x39@+0x1C;
firmware derives 0xAD).

## 6. Storage stack (NAND → NFTL → FAT)
The flash stack is the **Anyka SDK** (FHA + MtdLib + **NFTL_V1.2.11** + FatLib/FsLib;
`nftl-library-identification.md`). Drive map: **A: = the system/index partition** (what the firmware
mounts and scans for content), **B: = the USB-exposed user drive**. Partition A is a bare **FAT16
superfloppy** — VBR/BPB at partition sector 0, **no MBR**; the firmware auto-formats it that way
(`partition-a-fat-vs-mbr.md`). FatLib I/O resolves through ONE partition object → NFTL log2phy
(`fatvol-medium-layering.md` §1); the system bins (PROG, `codepage`) live in the linear FHA system
area below `fs_start`, read raw by `row = 256·block + page` via **NB_READ_DATA** — a single call
convention, buffer always in the r3 descriptor (`nb-read-data-conventions.md`). The FHA maplist
format (`{u16 origin_phys, u16 backup_phys}` per cluster) and the boot codepage load are pinned in
`fatvol-medium-layering.md` §3–4; the codepage **language selector** always picks GBK (G0=0, never
written; ASCII-identity — `codepage-what-is-it.md`). A **producer.bin run** supplied
authoritative ground truth for the ASA area, the block-0 metadata pages (maps/bin-info/zone table =
pages 0..63 of block 0) and the zone/partition table. Reference docs: `nftl-layout.md` (structs),
`nftl-write-consistency.md` (write/COW/fold path), `upd-system-partition-layout.md` (where A:'s
factory content comes from).

## 7. Running the firmware
The firmware can be executed, unmodified, under [`tt-emu`](https://github.com/nomeata/tt-emu): a
hardware-level emulator that boots the real firmware from artifacts alone (the `.upd` + a
reconstructed NAND), FLAT-loaded at 0x08009000, with no firmware hooks — the seams are hardware
(MMIO/IRQ/NAND content). It boots through the authentic Anyka NFTL, recognises A: as a real FAT16
superfloppy, and walks the statechart 0→splash→standby→mount(12)→book mode (state 13) on a decoded
tap. Findings in this doc set that were confirmed dynamically say so.

## Open items
- **Post-13 product load + play.** The 8→13 walk is autonomous (the state-8 "settle freeze" was the
  USB-present GPIO bit — resolved by bit 8 = 0; `autonomous-mount-state8.md`), and A: mounts through
  the real NFTL. What remains is the mount/content cascade inside book mode: (1) the **codepage boot
  read** — the FHA maplist + the 7 codepage clusters (blocks 36–42) so the real GBK header caches and
  `"a:/…"` paths convert (`codepage-what-is-it.md`); (2) the **A: root-dir enumeration**
  (logical block 1 → phys 135) → the firmware's own discovery scan + `gme_mount_check_product`.
- **Audio player construction:** `g_pAudPlayer` is built by state 13's real ENTER hook via the
  authentic 0x1058→ENTER-12→0x1059→ENTER-13 transitions (`audio-player-construction.md`).
- **Write persistence:** the NAND program path (`a:/oidfilelist.lst` must persist) is spec'd in
  `nftl-write-consistency.md`.
- Still undumpable: the ANYKANB2 SPL (0x07ff8000 reads as zeros) and the codec DSP inner loops.
