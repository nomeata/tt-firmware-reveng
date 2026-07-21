# NFTL library identification — it is the Anyka SDK flash stack (FHA + MtdLib/FsLib/NFTL)

**Research question.** Is the 2N (MT) pen's NAND flash-translation-layer an off-the-shelf
library, and if so can we get authoritative struct layouts to corroborate our RE?

**Answer (high confidence).** Yes. The pen's storage stack is the **SoC-vendor's own
software**: the **Anyka** ("安凯微电子" / Anyka Guangzhou) **flash SDK**. It is *not* a generic
Linux MTD/INFTL and *not* a third-party FTL. Two independent lines of evidence pin it:

1. **The firmware's own strings** are Anyka SDK component banners (`MtdLib`, `FSLib`,
   `FatLib`, `NFTL_V1.2.11`, `Medium_CreatePartition`, `Nand_CreatePartition`,
   `nftl_core.c`, `nftl_init.c`, `medium.c`, `ProdLib`, and the tell-tale
   **`your ic isn't anyka ic!`**). See §1.
2. **Anyka's own open-source headers** (the `mach-anyka` FHA library shipped in the
   officially Anyka-co-managed `onyx-intl/ak98_kernel` Linux tree) contain the *exact*
   magic (`ASA`), the *exact* NAND-geometry struct our `.upd` `flash_ic` table parses
   into (`T_NAND_PHY_INFO`), and an `E_FHA_CHIP_TYPE` enum whose members are literally
   named **`FHA_CHIP_11XX, //snowbird2`** — our SoC. See §2–§3.

**Caveat on "source".** The open Anyka code is the **FHA burn/mount + HAL headers**
(open) and the Linux `ak98-nand` glue (open). The **NFTL/MtdLib/FsLib core** itself
(`nftl_core.c`, `medium.c`, `Nand_CreatePartition`, `Medium_CreatePartition`,
`Zone_Group`) ships in Anyka SDKs as a **precompiled `.a`** and its `.c` source is **not**
in the public trees found. So the vendor headers give **authoritative struct definitions
for geometry, the spare/OOB callback contract, the ASA area, and ECC-type encoding**,
while the internal NFTL zone/log2phy structs remain RE-derived — now *corroborated* by the
vendor's naming/parameters rather than fully specced. See §5.

> **Third-party material.** The Anyka header definitions cited below are quoted only as
> field/offset summaries with attribution to their public source; the verbatim vendor
> source is not reproduced here. Those headers are © 2006 Anyka (Guangzhou) and carry their
> own licence — consult the linked repository (§Sources) for the originals.

All firmware citations are unified base `0x08009000`; string evidence from the pen's
`PROG.bin`. Anyka header citations reference the public repository listed under §Sources.

---

## 1. The firmware literally runs the Anyka flash SDK (string proof)

A handful of tell-tale component banners in `PROG.bin` name the library outright — the
source-file names `nftl_core.c` / `nftl_init.c` / `medium.c`, the version stamps
`NFTL_V1.2.11` and `FatLib_V1.2.1A_svn_1511`, the API names `Medium_CreatePartition` /
`Nand_CreatePartition`, and the self-ID guard **`your ic isn't anyka ic!`**. The
diagnostic format strings alongside them are all of the shape `"MtdLib - Init:: …"`,
`"FSLib - Nand_CreatePartition():: …"`, `"NandMtd_Format: the tatal bad block is %d"`,
`"ProdLib version:%d.%d.%d"` — the canonical Anyka userware library prefixes.

`MtdLib`, `FSLib`/`FsLib`, `FatLib`, `NFTL`, `ProdLib`, `MedLib`/`medium.c` are the
canonical Anyka userware library names (they appear identically in Anyka camera/MP4/BSP
builds; `MtdLib` = "Memory Technology Device Lib", `FSLib` = filesystem, `ProdLib` = the
factory-programming "producer"). The banner **`your ic isn't anyka ic!`** is a self-ID
guard the SDK prints when the chip-identity magic (our `ANYKANB1/2`) doesn't validate —
this is Anyka SDK code checking it is running on an Anyka SoC. Combined with the SoC being
Anyka Snowbird2 (ZC3202N is the Chomptech-branded part), **the storage stack is Anyka's own
SDK, full stop.**

## 2. Anyka's open FHA headers — the authoritative pieces we *do* get

Source: `onyx-intl/ak98_kernel` — a Linux BSP whose header says *"Copyright (C) 2006 Anyka
(GuangZhou) Software Technology Co., Ltd."* and whose repo description is *"Managed by both
Onyx and Anyka"*. The relevant tree:

```
include/mach-anyka/fha.h          fha_asa.h   nand_list.h   anyka_types.h
drivers/mtd/nand/ak98-nand/       wrap_nand.c wrap_nand.h   nand_control.c ...
drivers/mtd/nand/ak88-nand/       (aspen3 variant, same FHA API)
```

Key `fha.h` definitions (verbatim):

- **`E_FHA_CHIP_TYPE`** — the SoC family enum. Members:
  `FHA_CHIP_880X //aspen3`, `FHA_CHIP_10XX //snowbirds`, `FHA_CHIP_980X //aspen3s`,
  `FHA_CHIP_37XX //Sundance3`, **`FHA_CHIP_11XX //snowbird2`**.
  → Our SoC is **Snowbird2**; the SDK has a first-class code path for it.
- **`E_FHA_DATA_TYPE` = {FHA_DATA_BOOT, FHA_DATA_ASA, FHA_DATA_BIN, FHA_DATA_FS,
  FHA_GET_NAND_PARAM}** — this is exactly the producer's burn-object taxonomy our
  `nand-init-from-producer.md` reverse-engineered (BOOT block 0; **ASA** blocks 1..5; BINs
  PROG+codepage; FS partition area).
- **`FHA_Write/FHA_Read` callback signature** carries `(nChip, nPage, pData, nDataLen,
  pOob, nOobLen, eDataType)` — i.e. the flash HAL is a **page + OOB(spare)** interface,
  matching our device-leaf model (`0x0800fb00` data, `0x0800faa4` OOB, `0x0800f92c`
  data+tag).
- **`FHA_get_maplist(file_name, T_U16 *map_data, ...)`** returns a **u16 block map** per
  bin, with an explicit `bBackup` (origin vs backup copy) argument — this is precisely the
  producer's **per-bin log2phy map of `{u16 RB_block, u16 UB_block}` pairs** (the RB/UB
  "written twice" pairing in `nand-init-from-producer.md`).
- **`FHA_get_resv_zone_info / FHA_set_resv_zone_info`** manage a **reserve "zone"** at the
  top of the medium — the vendor's name for the reserved/wear region.

`fha_asa.h`:

- The **ASA** ("Anyka Special/Storage Area") module: `FHA_asa_scan`, `FHA_asa_format`,
  `FHA_set_bad_block`, `FHA_check_bad_block`, `FHA_get_bad_block`, `FHA_asa_write_file`,
  `FHA_asa_read_file`, with `ASA_MAIN_VER 1 / ASA_SUB_VER 5` and formats
  `ASA_FORMAT_NORMAL/EWR/RESTORE`. → This is the module that writes our **`ANYKAASA`**
  magic block(s) 1..5 (bad-block bitmap ×2 + "times" page). `asa_format@0x0800ed98` in the
  producer *is* `FHA_asa_format`.

`nand_list.h` — **`T_NAND_PHY_INFO` (authoritative geometry struct)**. Field/offset
summary (from the public `nand_list.h`; see §Sources — reproduced here as a layout table,
not verbatim source):

| field | type | meaning |
|---|---|---|
| `chip_id` | u32 | chip id |
| `page_size` | u16 | page size (bytes) |
| `page_per_blk` | u16 | pages per block |
| `blk_num` | u16 | total blocks |
| `group_blk_num` | u16 | blocks per die/group |
| `plane_blk_num` | u16 | blocks per plane |
| `spare_size` | u8 | spare bytes (high bits via `flag` bits 8-9, unit 256B) |
| `col_cycle`, `lst_col_mask`, `row_cycle`, `delay_cnt` | u8×4 | address/timing cycles |
| `custom_nd` | u8 | vendor family (Samsung=1, Hynix=2, Toshiba=3, …) |
| `flag` | u32 | plane/ECC/seq-write attribute bitfield (see below) |
| `cmd_len`, `data_len` | u32×2 | command/data lengths |
| `des_str` | u8[32] | description string |

This is a **field-for-field match** to the pen's `.upd` `flash_ic` row that
`nand-init-from-producer.md` parses (`{chip_id addc1095, pagesize 2048, pagesperblock 64,
totalblk 4096, planeblk 2048, spare 64, flag 0x1, des_str}`). **Our `flash_ic` table entry
IS a `T_NAND_PHY_INFO`.**

The `flag` bitfield is documented in `nand_list.h` comments (authoritative):

| flag bits | meaning |
|---|---|
| bit31 | supports copyback |
| bit30 | single-plane only |
| bit29 | front/pre-plane |
| bit28 | odd/even plane |
| bit10/11 | round page/block count up (TLC/Toshiba quirks) |
| **bits 8-9** | high bits of `spare_size` (unit 256B) |
| **bits 4-7** | **ECC type**: 0=4bit/512B, 1=8bit/512B, 2=12bit/512B, 3=16bit/512B, 4=24bit/1024B, 5=32bit/1024B |
| **bit0** | pages within a block **must be written sequentially** (MLC) |

→ This **resolves an open item in `nftl-layout.md §5 "ECC scheme"`**: the ECC strength is
not hidden in the NFC — it is selected by **`flash_ic.flag` bits 4-7**. The pen's
`flag=0x1` means **bit0=1 (sequential-write) and ECC-type field = 0 → 4 bit/512B ECC**.
(This refines the doc's "Inferred BCH" note: the encoding is per-512B and the strength is a
table field; `0x1` decodes to the weakest 4-bit setting. Verify against a real spare dump —
but the *mechanism* is now authoritative.) The corresponding ECC enum is echoed in the
Linux glue: `wrap_nand.c` clamps `ecc_type` to `ECC_12BIT_P512B` and the type list is
`ECC_{4,8,12}BIT_P512B / ECC_{24,32}BIT_P1024B`.

## 3. Match table — our RE'd structures vs. Anyka SDK

| our fingerprint (RE'd) | Anyka SDK equivalent | match | source |
|---|---|---|---|
| Magic **`ANYKAASA`**, blocks 1..5, bad-block bitmap + "times" | **ASA** module (`fha_asa.h`), `FHA_asa_format/scan`, ver 1.5 | **Exact** (name+role) | `fha_asa.h`; producer `asa_format@0x0800ed98` |
| Chip magic **`ANYKANB1/2`** + `"your ic isn't anyka ic!"` | Anyka chip-identity self-check; `E_FHA_CHIP_TYPE` incl. `//snowbird2` | **Exact** (it is Anyka's guard) | `PROG.bin` string; `fha.h` enum |
| `flash_ic` geometry row in `.upd` | **`T_NAND_PHY_INFO`** (`nand_list.h`) | **Exact, field-for-field** | `nand_list.h` |
| ECC "per-512B, strength unknown" | `flash_ic.flag` **bits 4-7 = ECC type table**; per-512/1024B | **Exact mechanism** | `nand_list.h` flag comment; `wrap_nand.c` `ECC_*` |
| linear-vs-wear selector (`chip[+4]&0x10000000`) + `flash_ic` bit0x1 | `flag` **bit0 = sequential-write (MLC)**; reserve "zone" via `FHA_*_resv_zone_info` | **Partial** (bit0 role now known; the internal wear-flag is MtdLib-internal) | `nand_list.h`; `fha.h` |
| per-bin **log2phy** `{u16 RB, u16 UB}` map | **`FHA_get_maplist(...,T_U16 *map_data,...,bBackup)`** | **Exact** (u16 map, origin/backup) | `fha.h`; producer `FUN_0800c5d4` |
| **`Medium_CreatePartition` / `Nand_CreatePartition`** | MtdLib/FsLib API of the same name | **Exact** (same symbol) | `PROG.bin` strings |
| burn taxonomy BOOT/ASA/BIN/FS | **`E_FHA_DATA_TYPE`** = BOOT/ASA/BIN/FS/GET_NAND_PARAM | **Exact** | `fha.h` |
| FS partition table read at mount (`{part_cnt; partitions[]}`) | `FHA_get_fs_part` → `struct partitions{name,size,offset,mask_flags}` | **Exact** (Linux glue shows layout) | `wrap_nand.c ak_mount_partitions()` |
| **8-byte spare NFTL tag** (seq/idx/next/head/logical) | *(MtdLib/NFTL-internal; not in open headers)* | **No open struct** | — |
| **29-entry u16 metadata page-directory** (`part+0xe`) | *(NFTL-internal)* | **No open struct** | — |
| **`Zone_Group` / zone table** (`part+0x1d2`, zone-group info descriptor) | vendor "zone" concept present (`resv_zone`, `nBlockStep`); struct not open | **Concept match, no struct** | `fha.h` `nBlockStep`, resv-zone API |

## 4. Why the generic-FTL candidates are *rejected*

Checked against the fingerprints; none fits, because the pen's stack is a vendor RTOS FTL,
not a Linux/standard one:

- **Linux MTD `NFTL`/`INFTL` (M-Systems DiskOnChip)** — different on-flash format
  (`ANAND`/`BNAND` OOB signatures, unit-header `virtualUnitNo/prevUnitNo` chains). Our tag
  is 8 bytes with `0xfffd` terminator and a `0x8000` head flag — **not** the INFTL unit
  header. The `ak98_kernel` even *ships* `inftlcore.c`/`inftlmount.c` but the Anyka driver
  does **not** use them; it routes through FHA/MtdLib instead. **Reject.**
- **Keil RL-ARM / CMSIS NFTL, SmartMedia/xD FTL, SEGGER emFile NAND** — unrelated formats,
  no `ANYKAASA`, no `T_NAND_PHY_INFO`, no `Medium_CreatePartition`. **Reject.**
- The only library that carries **all** the magics/symbols simultaneously is the **Anyka
  SDK**. Fit is unambiguous.

## 5. What the vendor headers corroborate (and what they don't)

**Authoritative from the headers:**
- Geometry struct = `T_NAND_PHY_INFO`; our `.upd flash_ic` fields map 1:1
  (page_size / page_per_blk / blk_num / group_blk_num / plane_blk_num / spare_size) — the
  datasheet view exactly as the vendor models it.
- **ECC is a table-selected strength** via `flag` bits 4-7 (per 512B or 1024B). Pen
  `flag=0x1` → 4-bit/512B ECC + sequential-write — no need to guess BCH strength.
- ASA area semantics (versioned 1.5, EWR/RESTORE formats, dual bad-block bitmap) are the
  `FHA_asa_*` contract — matches our block-1..5 findings.
- The spare/OOB HAL contract (page+OOB, `eDataType` tag) matches the device leaves; the
  RB/UB backup-pair map matches `FHA_get_maplist`.

**Still RE-derived (headers don't expose it):** the **8-byte NFTL spare tag** field split,
the **29-entry metadata page-directory**, the **log2phy window+RLE** structs, and the
**`Zone_Group`/zone-size** table. These live in the closed `MtdLib`/`nftl_core.c` `.a`. The
Ghidra-derived layouts in [`nftl-layout.md`](nftl-layout.md) /
[`nftl-write-consistency.md`](nftl-write-consistency.md) remain the source of truth for
them — now *corroborated* by vendor naming (the firmware's own `nftl_core.c`/`medium.c`
symbols confirm the right library was RE'd), but not replaceable by an open header. (The
spare-tag `[4:6]` field is settled independently from the decomp for the pen's chain width
w=1: it is the **logical** block — see `nftl-layout.md` §5.)

### Where the closed source might still be found (leads, not yet obtained)
- An Anyka **RTOS SDK for the "spot/spr/sword" platform** (`E_FHA_PLATFORM_TYPE`), i.e. the
  non-Linux MP3/MP4/reading-pen BSP that actually contains `nftl_core.c`, `medium.c`,
  `Nand_CreatePartition`, `Medium_CreatePartition`, `Zone_Group`. Likely on Chinese
  code-sharing sites (pudn/csdn/gitee) or leaked vendor SDKs; not located in this pass.
- Anyka AK88xx (aspen3) / AK37xx / AK98xx camera BSPs reuse the same FHA API; a fuller MP-
  series SDK (AK10XX "snowbirds", AK11XX "snowbird2") is the ideal target.
- Anyka NFTL test patent **CN104021816A** (Anyka Guangzhou) confirms the vendor's NFTL uses
  per-block **erase-count**, **BLK_BADFLAG/BLK_ERASEFLAG**, per-page op marks, **OOB
  power-fail recovery**, and dual-plane paired ops — consistent with our `mtd_init` block-
  status codes (`0xfffc/0xfffa/0xfff7`) and danger list. Useful corroboration; no struct
  layouts.

## Sources
- Anyka FHA/NAND headers (repo *"Managed by both Onyx and Anyka"*):
  - https://github.com/onyx-intl/ak98_kernel/blob/master/include/mach-anyka/fha.h
  - https://github.com/onyx-intl/ak98_kernel/blob/master/include/mach-anyka/fha_asa.h
  - https://github.com/onyx-intl/ak98_kernel/blob/master/include/mach-anyka/nand_list.h
  - https://github.com/onyx-intl/ak98_kernel/tree/master/drivers/mtd/nand/ak98-nand (`wrap_nand.c`, `nand_control.c`)
- Anyka NFTL test patent: https://patents.google.com/patent/CN104021816A/en
- Anyka camera BSP trees (same FHA API family): https://github.com/mucephi/anyka_ak3918_kernel , https://github.com/Nemobi/Anyka
- Rejected generic candidates: Linux MTD NFTL/INFTL https://cateee.net/lkddb/web-lkddb/NFTL.html ; Keil RL-ARM NFTL https://www.keil.com/support/man/docs/rlarm/rlarm_fs_nftl.htm
