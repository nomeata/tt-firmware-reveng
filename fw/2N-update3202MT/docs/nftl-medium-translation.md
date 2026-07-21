# NFTL medium sector → physical-row translation at authentic geometry

**Goal:** spec the AU‑scaled medium translation that the tiptoi 2N (MT) firmware applies
between a partition‑relative FAT LBA and the physical NAND row, at the authentic geometry
`dev[+0x1c]=0x1000` (AU = 4 KB, f = 8).

Decomp base `0x08009000`; all addresses are runtime. Proven = read directly from decomp
and/or confirmed by trace; Inferred = derived but not byte‑confirmed.

---

## 0. TL;DR (the translation)

At authentic geometry the FS‑area medium addresses the NAND in **AU units** (1 AU =
`dev[0x18]·dev[0x1c]` = `1·0x1000` = 4096 B = **8× 512‑B sectors = 2 physical pages**).
The physical read leaf `FUN_0800fb00` presents its NB read as

```
r1 = row = dev[0x14]·phys_block + AU_index      (dev[0x14] = 256 sectors/block; AU_index = 0..31)
r2 = sub-sector selector (0 for the FAT/NFTL data path; set for the codepage/FHA path)
```

which decodes to a flat 512‑B physical sector (Proven, confirmed for both the codepage
decode and the FAT path by trace §3):

```
phys_512B_sector = 256·(row>>8)  +  8·(row & 0xff)  +  (r2 // eccsize)
                 = 256·phys_block + AU_secs·AU_index + sub_sector
   AU_secs = (dev[0x18]·dev[0x1c]) >> 9 = 0x1000>>9 = 8
```

so a full‑AU FAT read covers 8 consecutive 512‑B sectors and a codepage sub‑sector read
covers 1. The physical placement is contiguous; the AU scaling lives entirely in the
**read decode**, together with the AddrCnt units / partition spans.

> **AddrCnt-unit discrepancy (unresolved):** the zone-table `AddrCnt` unit differs across
> sources — [`ab-drive-layout.md`](ab-drive-layout.md) reads it as LE 2‑KiB pages,
> `producer-run-results` derived a BE `sizeMB·0x20000`, and this doc's trace decodes it in
> AU units. The three have not been reconciled, and the implied A‑partition size differs
> accordingly. Treat the exact `AddrCnt` unit as an open question.

---

## 1. The device geometry object (Proven, pen‑confirmed)

`dev = 0x08008cc4` (Hynix HY27UF084G2B chip‑detect result; 32/32 words == pen dump
`m08007000.bin` at authentic values):

| field | value | meaning |
|---|---|---|
| dev+0x08 / +0x0c | 2 / 2 | plane / die count |
| dev+0x10 | 0x800 | page = 2048 B |
| dev+0x14 | 0x100 = 256 | **sectors per erase block** (128 KiB / 512) |
| dev+0x18 | 1 | AU multiplier |
| **dev+0x1c** | **0x1000** | **AU size = 4096 B** (emu hack was 0x200) |
| dev+0x04 | 0x40041 | flags: `0x40000` set, wear bit `0x10000000` **clear** ⇒ LINEAR resolver |
| dev+0x24 | 0x0800fb00 | data read leaf (`FUN_0800fb00`) |
| dev+0x28/0x2c/0x30 | f92c / f9cc / faa4 | data+tag / tag / OOB leaves |

Derived constants at authentic geometry:
* **AU = dev[0x18]·dev[0x1c] = 0x1000 = 4096 B**
* **AU_secs = AU >> 9 = 8** (512‑B sectors per AU)
* **f = AU / 512 = 8** (the "page→sector" factor of `flash_erase_region`)
* **AU per block = 256 / 8 = 32**  (so AU_index ∈ 0..31, lives in `row & 0xff`)
* one AU = **2 physical pages** (4096 / 2048)

---

## 2. The read chain (Proven from decomp)

```
FatLib fs_read  (partition-relative 512-B LBA S)
  └─ nftl_partition_read 0x0800ea38     part+0x10 vtable op
       clamp S to part[0xc]; medium_sector = piVar6[2] + S          piVar6=*(part+0x20)
       call (**(*piVar6+0x10))(*piVar6, buf, medium_sector, count)  *piVar6 = the FS-area medium
  └─ FUN_0800e760  (FS-area medium read op = medium[0x10], = DAT_0803c330)
       AU_secs = (dev[0x18]·dev[0x1c]) >> medium[9]   (= 0x1000>>9 = 8)
       cap check: uVar1 = AU_secs · medium[0xc]
       reads the region in AU (8-sector) chunks; per chunk → FUN_0800e6b0 → nftl_rw_multi_sector
  └─ nftl_rw_multi_sector 0x0800f5f4
       block = medium_sector / 256   (divmod by dev[0x14]·chip[0x64] = 256·1)
       nftl_read_resolve(block)      → physical rel-block via dense window part+0x8c
       per-AU: (**(part+0x250))(...) = nftl_read_op_leaf
  └─ nftl_read_op_leaf 0x08010078
       phys = (**(chip+0x26c))(chip, S, &buf)   chip+0x26c = FUN_0800fef0 (LINEAR) [dev flag]
       FUN_0800feac(dev,&block,&page)           die/plane split
       (**(*(dev+0x54)+0x24))(chip54, buf, block, page, …)   dev+0x24 = FUN_0800fb00
  └─ FUN_0800fb00  (data read leaf)
       row = dev[0x14]·block + page = 256·block + AU_index
       func_0x0800260c(buf, row, 0, datadesc, tagdesc)   ← r2 = 0 for this path
```

`flash_erase_region` 0x0803c784 (= `Medium_CreatePartition`, called twice by
`fs_storage_mount_init` 0x0803a484) fixes the units (Proven):

```
uVar9 = 0x200 (sector size, clamped ≤0x1000)
uVar10 = dev[0x18]·dev[0x1c] & 0xffff          (= 0x1000 = AU)
f  = uVar10 / uVar9  = 8                        (via func_0x080031d0)
partition base  piVar4[2] = f · start           (FS-medium sector of the partition origin)
partition cap   part[0xc] = param_3(AddrCnt) << log2(f) = AddrCnt · f   (in 512-B sectors)
part+9  = log2(0x200) = 9   (the sector shift 1<<9=512 used everywhere)
part+0xb = log2(f)   = 3   (0 at f=1)
```

`fs_storage_mount_init` calls:
```
cntA = nftl_zonetab_addrcnt(0)                              A: AddrCnt
A    = flash_erase_region(FS_medium, 0,    cntA, 0x200, 1)  A: start=0
cntB = nftl_zonetab_addrcnt(1)                              B: AddrCnt
B    = flash_erase_region(FS_medium, cntA, cntB, 0x200, 0)  B: start=cntA
```
So **B’s FS‑medium base (sectors) = f · cntA = f · AddrCnt_A**, and B’s partition‑relative
sector `S` → FS‑medium sector `f·AddrCnt_A + S`.

**AddrCnt unit (Proven, conformance‑audit §3 + producer image):** AddrCnt is stored in
**AU units = blocks·32** (LE u32). Producer `producer_nand.img` blk0/pg63:
`AddrCnt_A = 0x3C00` (480 blocks · 32 = 60 MB), `AddrCnt_B = 0x8000` (1024 blocks · 32 =
128 MB). f then converts AU→sectors (×8) downstream in `flash_erase_region`.

---

## 3. Traced LBA → row decode (authentic `dev[0x1c]=0x1000`)

Traced through the A: `fs_partition_scan` (partition `0x08145b90`, base[+8]=0) at the
authentic geometry. `ea38` = the partition‑relative sectors FatLib requests; `row NB_DATA`
= the physical row the leaf presents; the last column is the flat 512-B sectors the AU
decode (§0) resolves that row to:

| FatLib request (ea38) | leaf row (NB_DATA) | flat 512-B sector | firmware MEANS (AU decode) |
|---|---|---|---|
| VBR   `sector=0  count=1` | `0x8600` blk134 au0 | blk134 **sec 0** (512 B) | blk134 sec 0..7  |
| FAT   `sector=4  count=8` | `0x8601` blk134 au1 | blk134 **sec 1** (512 B) | blk134 sec **8..15** |
| FAT   `sector=12 count=8` | `0x8602` blk134 au2 | blk134 **sec 2** | blk134 sec **16..23** |
| FAT   `sector=20 count=8` | `0x8603` blk134 au3 | blk134 **sec 3** | blk134 sec **24..31** |
| FAT   `sector=28..60`     | `0x8604..0x8607`    | blk134 sec 4..7 | blk134 sec 32..63 |
| B: VBR `sector=0 count=1` | (part `0x08145c00`, **base[+8]=0x78000**) | — | 0x78000 = f·AddrCnt_A |

**The mismatch, concretely:**
* The medium reads in **AU (8‑sector) chunks** (`ea38 count=8`, AU‑aligned to the FAT start).
* The leaf row increments by **1 per AU** (`0x8600, 0x8601, …`), i.e. `row = 256·blk + AU_index`.
* The emulator decodes `row` as a **flat 512‑B sector** (`block=row//256, sec=row%256`) and
  returns **only 512 B**. So AU_index k is served as physical **sector k** instead of the
  physical **sector span [k·8 .. k·8+8)**, and 7/8 of the AU is never filled.
* Result: FatLib’s FAT/root‑dir bytes are wrong → the `*.gme` scan enumerates nothing →
  `oidfilelist.lst` written without the game record → no `play_media` (the `a14374d6` wall).

**Why the current default (`dev[0x1c]=0x200`) works:** at f=1, AU = 1 sector, so
`row = 256·blk + sector` directly and the AU chunk is a single 512‑B sector — the flat
`read_sectors(row,1)` decode happens to be correct. It is a compensating hack, not authentic.

The B: row (`base[+8]=0x78000`) also confirms the unit chain: with AddrCnt left in the emu’s
sector units (0xF000) the base came out `f·0xF000 = 0x78000` (block 1920) — wrong, because
AddrCnt must be in **AU units** (0x3C00) so that `f·0x3C00 = 0x1E000` = FS‑medium block 480.

The mount‑time NFTL block scan (OOB leaf, `LR 0x0800fa84`) reads `row = 256·blk` for every
FS block 134..4095 → decodes to `256·blk + 0 + 0` under the new formula too (unchanged) —
so the scan and `spare_for_row` (keyed by block) are unaffected.

---

## 6. Evidence index

* Decomp: `flash_erase_region` 0x0803c784, `FUN_0800e760`, `nftl_build_fs_medium` 0x0803be74,
  `nftl_partition_read` 0x0800ea38, `nftl_rw_multi_sector` 0x0800f5f4, `nftl_read_op_leaf`
  0x08010078, `FUN_0800fb00`, `fha_read_row` 0x08030da4, `codepage_load` 0x08030e4c,
  `nand_device_create` 0x0803a84c, `Nand_CreatePartition` 0x080443d0, `mtd_init` 0x08045e80.
* Live pen: device struct @0x08008cc4 (32/32 words at authentic values) — confirms
  `dev[0x1c]=0x1000` (the AU factor) on hardware.
* Producer image: block-0 page-63 AddrCnt A=0x3C00 / B=0x8000 (AU units).
* Trace: the LBA→row table in §3 (mount `fs_partition_scan` at authentic
  `dev[0x1c]=0x1000`). See also [`fatvol-medium-layering.md`](fatvol-medium-layering.md),
  [`ab-drive-layout.md`](ab-drive-layout.md), [`nftl-layout.md`](nftl-layout.md).
