# NAND-init from scratch: producer.bin's format/flash code (and PROG's copy of it)

Research question: *is there code that creates the pen's on-flash NAND data
structures from nothing — partition table, log2phy maps, spare tags, superblock —
and if so, how does it lay them out?*

**Answer: yes.** `producer.bin` (the factory-programming userware carved from the
`.upd`) contains the complete "program a blank pen" implementation, and PROG.bin
(MT) contains a byte-content-matched copy of the same `PR_*` code for self-update.
The producer is a **USB-slave command interpreter** (the PC tool orchestrates), but
every structure-building step is a self-contained function with inputs we have
(the `.upd` container + the flash_ic geometry table inside it). This doc maps the
whole flow and gives the exact on-flash formats it creates (with the magic spare tags).

Conventions: producer addresses = load base 0x08000000
(`fw/2N-Update3202/data/producer.bin`, decomp in
`fw/2N-Update3202/out/ghidra_artifacts/producer_decomp/`);
MT addresses = unified base 0x08009000
(`fw/2N-update3202MT/out/decomp_named/`). Claims tagged **Proven** (decomp/bytes
cited) or **Inferred**.

---

## 0. Big picture — who creates what, when

```
factory (producer.bin, burn mode 0/1):          field update (PROG MT, burn mode 2):
  cmd 5  pr_p_init(flash_ic)                      pr_p_burn_mode(2) [0x080f27f0]
  cmd 6  detect nand param (by chip ID)           pr_p_read_partition_info  → reads blk 63
  cmd 7  ASA mount/format  → blocks 1..~5         upd_build_log2phy_blockmap → reads blk 62,61,maps
  cmd 9  PR_M: zone table + mediums/partitions    pr_b_exchange_blocks (RB/UB ping-pong)
  cmd 10 FAT format (FsLib mkfs) (or PR_I image)  pr_br_write_boot / pr_b_* rewrite bins
  0x10/0x11/0x1c/0x2e  burn bins (BOOT,PROG,codepage)
  cmd 0x22 pr_p_write_maps → blocks 59..63        pr_p_write_maps → same blocks
```

Everything the NFTL/boot chain later consumes is created by the producer in the
**first 64 blocks** plus the bin/FS areas behind them:

| block(s) | content | spare tag (4 B, LE u32) | written by |
|---|---|---|---|
| 0 | SPL/nandboot, mask-ROM page format ("boot pages") | (HAL boot format) | `FUN_0800ca48` / `FUN_0800bb6c` via boot-write op |
| 1..~5 | **ASA** (Anyka Special Area): magic `"ANYKAASA"`, bad-block bitmap ×2, "times" page; up to 5 copies | copy counter 1..5 (and per-copy times) | `asa_format@0x0800ed98` |
| `ppb−3−nbins+i` = 59,60 (nbins=2) | **per-bin log2phy map**, bin *i*: u32 entries `{u16 RB_block, u16 UB_block}` | `0x12121212` | `FUN_0800c5d4` |
| `ppb−3` = **61** | **bin-info array**: nbins × 0x24 records | `0x34343434` | `FUN_0800c760` |
| `ppb−2` = **62** | **bin-info header**: `{u32 3, 0, 0, nbins}` (0x10 B) | `0x56565656` | `FUN_0800c760` |
| `ppb−1` = **63** | **zone/partition table**: Information + Zone_Group[] + fake-zone map | `0x5a5a5a5a` | `FUN_08009470` (= MT `pr_p_write_maps`) |
| ≥64 (host-set) | **bins**: PROG, codepage — each logical block written **twice** (RB+UB pair) | `0` (data page) / `0x11235813` (all-FF page) / `0x12345678` (boot data blocks) | `pr_b_write_block_pair@0x0800bc1c` |
| after bins (`Information[+4]` = "fs Bootblock") | FS area: FAKE-zone medium (erased by `FormatBlk`), partitions A + B, FAT16 volume(s) | 8-B NFTL tag (nftl-layout §5) on written pages only | `PR_M` chain + FsLib format / PR_I image write |

`ppb` = pages per block = **64** for the pen's HY27UF084G2B (flash_ic row 48 in the
`.upd` table: `addc1095, pagesize 2048, pagesperblock 64, totalblk 4096, planeblk
2048, spare 64, flag 0x1`). **Proven** (`.upd` @0x200+48·0x40, parsed).

The metadata indices `ppb−3−nbins+i` / `ppb−3` / `ppb−2` / `ppb−1` (the "59..63"
values in the table above and in §2.3) are **pages of BLOCK 0** — the info block —
not block numbers: `pr_p_write_maps` addresses them as `(block=0, page)`. Read every
"block 59..63" in this doc as "page 59..63 of the info block (block 0)". The spare
tags (`0x12121212`/`0x34343434`/`0x56565656`/`0x5a5a5a5a`), payload layouts, the
zone-table encoding and the ASA layout (§2.1) are all confirmed byte-for-byte as
written.

Two geometry facts underpin the page-directory arithmetic:

- `upd_build_log2phy_blockmap`'s `piVar1[2]` is **pages-per-block (64)**, not total
  blocks. The metadata therefore lives at the top pages of the info block (pages
  61/62/63 of block 0), not at the top of the chip. **Proven**: the struct at producer
  `0x0802afa0` is filled by `FUN_0800b670` (`[0]=pagesize` u16@+4 of flash_ic,
  `[2]=pagesperblock` u16@+6; small-page chips converted to 2048/64), MT has the
  identical initializer `FUN_08105c60@0x08105c60`, and MT `FUN_08106fd4` wraps its
  page counter at the same `[2]` field, so `[2]` is unambiguously pages/block.
- `device[+0x14] = 0x100` (256, 512-B-sector units) is an *emulation seed*, not read
  from hardware. The producer uses `chip[+0x14] = flash_ic.pagesperblock = 64` for the
  zone block; the SPL's own header carries datasheet units (`nandboot.bin` @0x38:
  `00 08` = 2048-B page, `40 00` = 64 pages/block). If a future real-NAND dump shows
  the zone table at block 255 instead of 63, the runtime device struct is in sector
  units; all *relative* relationships in this doc are unit-independent. **Inferred**
  (flagged for verification).

---

## 1. The producer command interpreter (context for "runnable in isolation")

`producer.bin` is an Anyka userware image (strings `"*** This is userware ***"`,
`"producer version:%d.%d.%d_%s"`) loaded at 0x08000000. It executes numbered
commands received over USB from the PC production tool. Dispatchers (**Proven**):

- `FUN_08006738@0x08006738` — cmd 4 = return chip IDs; **5** = `pr_p_init`
  (`FUN_08008080`); **6** = detect nand param (`FUN_080082a0`, builds a flash_ic
  record from the read chip ID); **9** = `pr_m_fs_mtd_init` + `PR_M` (zone/partition
  creation); **10** = `pr_m_driver_format` (on-device FAT mkfs + volume label);
  0xb = `PR_I` set image info; 0x14/0x15/0x16/0x17 = PR_I medium-info / write-img /
  … / read-fat (host-streamed FAT image); 0x1c = `BOOTBIN` select; 0x1d/0x2e/0x30 =
  boot write/read; 0x27 = disk info; 0x28 = `pr_b_end_bin`; 0x29 = bin data write.
- `FUN_08006408@0x08006408` — cmd 7 = ASA mount/format (`FUN_08008324`);
  0xc = `pr_b_end_bin`+reset; 0xd = boot-stream write; 0xf/0x13 = compare/read bin;
  **0x10** = `pr_b_file_info` (declare a bin: len, ld_addr, name);
  **0x11** = `pr_b_write_data`; **0x22** = `pr_p_write_maps`; 0x2d..0x30 boot ops.
  A parallel switch handles the SPI-flash device type (PR_S; not the pen).

Burn **modes** (`pr_p_burn_mode`, ctx+4 @0x0802aee8): 0/1 = factory burn (1 =
"asa format EWR" destructive scan), 2 = update (re-reads existing structures),
3 = erase-everything, 4 = read/verify. PROG's self-update uses mode 2
(`FUN_080f27f0@0x080f27f0` → `pr_p_burn_mode(2)`). **Proven.**

So: producer's *worker functions* are pure (buffers in, NAND-op vtable out); only
the *sequencing* comes from the host. The `.upd` container holds the host's inputs:
partition table @hdr+0x08 (count=2, addr=0xa4: `A: zonetype2 UNSTANDARD 30MB`,
`B: zonetype4 STANDARD rest`), the 125-entry flash_ic table @hdr+0x10, boot @+0x20,
to_udisk files @+0x30, to_NAND bins @+0x38 (`PROG` 0x380000, `codepage` 0xd6ccc —
records are 0x24 B, same shape as the on-flash bin-info). **Proven** (parsed;
matches the `Anyka_UPD_2015-03-01.xml` grammar in `data/`).

---

## 2. What each structure is, and exactly how it's built

### 2.1 ASA — bad-block table area (`asa_format@0x0800ed98`, `asa_init@0x08009c50`)

AsaLib (`"AsaLib_V%d.%d"`, config 0x24 B @0x0802af78 = `{u8 block_step(=chip
factor, 1), u8 dev_type, u16 pages_per_block, u32 pagesize(≤0x1000), u32
blocks_per_die, fn ptrs…}`) manages up to **5 replicated ASA blocks**, scanned at
blocks `step·1, step·2, … step·50` (`asa_init` starts at block `step` and probes ≤50
candidates). **Proven.**

Format (`FUN_0800ed98`, called from `FUN_08008324` cmd 7):

1. Build a **bad-block bitmap**: `total_blocks` bits, MSB-first within each byte
   (`bit = 1 << (7 − (block & 7))`), `npages = ceil(total_blocks / (pagesize·8))`
   pages (4096 blocks / 2 KiB page ⇒ 1 page). Scan mode 0 → `FUN_0800eb4c`
   (non-destructive: factory-marker read scan; also sanity-reads all pages of
   block 0 and fails if >8 zero bits found); mode 1 ("EWR") → `FUN_0800e428`
   (destructive erase-write-read test with a host-given pattern; logs
   `@@@Erase/Write/Read/Spare Changed/Data Changed Bad Block`). **Proven.**
2. Build the **ASA header page**: `[0:8]="ANYKAASA"` (magic @0x0801b1ec), `+8=0,
   +0xc=0, +0x10=2, +0x14=2·npages+1, +0x18=1, +0x1a=npages, +0x20=npages+1,
   +0x22=npages` — a directory of two bitmap copies at pages 1..npages and
   npages+1..2·npages. **Proven** (field stores in decomp).
3. For each good candidate block (≤5, plus 1 spare tracked): erase; write page 0 =
   header, pages 1.. = bitmap copy 1, pages npages+1.. = bitmap copy 2, and the
   header again into the **last page of the block**; every page written with a
   4-byte spare tag = the running copy counter (1..5, wraps >5→0). `asa_init`
   later reads the **last page's spare as the "times"/version counter** and mounts
   the copy with the highest value; `repair_asa_blk`/`FUN_080098e0` resync stale
   copies. **Proven.**

Runtime updates: `asar/asas/asaw` (`0x0800a09c/0x0800a33c/0x0800a530`) restore /
read / rewrite the ASA when new bad blocks appear; `asa_check_block@0x0800e850`
tests membership (the boot-bin writer avoids ASA blocks). **Proven** (log strings +
call sites; internals skimmed only).

The SPL contains both `"ANYKAASA"` and `"PROG"` strings (`nandboot.bin` @0x79d6,
@0x1000) ⇒ the boot chain consumes the ASA and the bin-info. **Proven** (strings);
exact SPL parsing **Inferred**.

### 2.2 Bin burning — PROG/codepage with dual copies + log2phy map (PR_B)

State: geometry `{u32 pagesize; u16 pagesize_cap; u32 pages_per_block}` @0x0802afa0
(`FUN_0800b670`); burn ctx @0x0802afc8 `{u16 bin_ctr; u16 next_block; u32 bin_idx;
u32 page_in_block; u8 type; u8 pad; u16 RB@+0x12; u16 UB@+0x14}`; buffer ptrs
@0x0802afbc `{u16 map_counts[10]; BinInfo bininfo[10] @+0x14; u32 maps[] @+0x17c}`
(one 0x111c-byte allocation, `pr_b_init@0x0800b6c4`). **Proven.**

Per bin (host sends cmd 0x10 with `{u32 len; u32 ld_addr; char name[15] @+9}`):

- `pr_b_file_info@0x0800b98c` creates the **bin-info record** (0x24 B):

  | off | field | source |
  |---|---|---|
  | +0x00 | length in bytes | host |
  | +0x04 | load address | host |
  | +0x08 | bin index → later rewritten to **absolute map block** = `idx + ppb − nbins − 3` (`FUN_0800c5d4` mode-1 adjust) |
  | +0x0c | start block of this bin (`next_block` cursor at creation) |
  | +0x10 | 0 at factory (updater uses `+0x10==0 || +0xc<+0x10` to pick zone-vs-bininfo base) |
  | +0x14 | name[16], NUL-padded |

  and stores `map_counts[idx] = ceil(len / blockbytes)` (blockbytes = 128 KiB).
  **Proven.**
- `pr_b_begin_bin@0x0800b934(0)` allocates the first **RB/UB block pair** =
  `{next_block, next_block+1}`; `pr_b_write_data@0x0800bfc0` (cmd 0x11) rounds the
  stream to 0x1000, splits into 64-page blocks, and for each block calls
  `pr_b_write_block_pair@0x0800bc1c` which **writes the identical pages into both
  RB and UB**, per page with a 4-byte spare tag: `0` for data, **`0x11235813`**
  (Fibonacci) if the whole 2 KiB page is 0xFF. Bad handling: factory-bad check +
  erase + write-verify; on failure the failing copy advances to `max(RB,UB)+1`
  (already-written pages salvaged by copy), ≤0x31 retries. After each full block:
  map entry recorded and `RB,UB = max+1, max+2`. **Proven.**
- The **map entry** for logical block *j* of the bin is
  `u32 { u16 RB_block·chip_factor, u16 UB_block·chip_factor }` at
  `maps_base + map_offset(bin) + j·4` (`chip_factor` @0x0801b1f4 = 1; 2 only for
  dual-die "upscale" chips). **Proven** (`FUN_0800bfc0` map store,
  `FUN_0800cf30`).
- `pr_b_end_bin@0x0800bb04` logs `RB:%d UB:%d`, stores the final cursor. The next
  bin (codepage) continues from there.

Boot is special: `BOOTBIN@0x0800c89c` matches the request name `"BOOTBIN"` (or a
bin-info name) and `FUN_0800ca48` writes the SPL through the HAL **boot-page write
op** (mask-ROM sector format) for the first `boot_pages` pages, then any overflow
as normal data blocks tagged **`0x12345678`**, skipping ASA blocks
(`asa_check_block`) and erasing before write. No bin-info entry, no map.
**Proven** (code); the boot-op's on-flash page format lives in the HAL leaf
(**Inferred**: 512-B sector + ECC layout the mask ROM expects, cf.
docs/maskrom-boot-vs-recovery.md).

Capacity note (**Inferred but forced**): PROG(28 blk)+codepage(7 blk) dual-copied =
70 blocks, which cannot fit below the metadata blocks 59..63; and the map bound
check (`map index < ppb−3`, `FUN_0800c5d4` "pos err") pins the metadata inside the
first 64 blocks. Therefore the host must advance the bin start cursor
(`pr_b_set_start_block@0x0800b7b4`, `"PR_B start block:%d,inc cnt:%d"`) to ≥64
before burning data bins; the FS area then starts after the bins
(`Information[+4]` = the cursor after the last bin, see §2.4).

### 2.3 Writing the metadata blocks (`pr_p_write_maps` = producer `FUN_08009470` ≡ MT `0x08105118`)

(All "block ppb−…" targets below are **pages of block 0**, the info block — see §0.)

Cmd 0x22, and the final step of every PROG self-update (**Proven**, MT
`upd_unpack_bootfile@0x081058a4` calls `pr_br_write_boot` then `pr_p_write_maps`):

1. `FUN_0800c5d4` — for each bin *i*: compose page = `{u32 0x12121212}` + map bytes
   (`map_counts[i]·4`), erase+write **page 0 of block `bininfo[i].mapblock`**
   (= `ppb−3−nbins+i`), spare tag `0x12121212`.
2. `FUN_0800c760` — erase+write block `ppb−3` (61) page 0 = the bin-info array
   (`nbins·0x24`) with a `{u32 0x34343434}` prefix, tag `0x34343434`; then block
   `ppb−2` (62) = 0x10-byte header `{3, 0, 0, nbins}`, tag `0x56565656`.
3. Compose the **zone page**: `Information` (0x10 B) + `Zone_Group[nzones]`
   (0x10 B each) + at offset `Information.TotalLen`: `u16 fake_group_count` +
   0x200 bytes fake-zone group table; erase+write block `chip.ppb−1` (63) page 0,
   tag `0x5a5a5a5a`. **Proven** (both images; tag values read from the binaries).

The all-linear read-back is `pr_p_read_partition_info` (producer `FUN_08007f88`,
MT `FUN_08105344`) and `upd_build_log2phy_blockmap@0x08105924` (MT): reads 62 → n,
61 → bin-info, recomputes per-bin map length as
`ceil(len/blockbytes) + ceil((len/10)/blockbytes)` (**+10% spare-block allowance**;
`FUN_0803062c` = div10), reads each map block into RAM. **Proven.**

### 2.4 Partition/zone table (PR_M chain) — the "partition the pen" step

Input = the `.upd` partition records (0x18 B each: `{char drivesymbol; u8
isuserdisk; u8 protection; u8 zonetype(0=MMI,1=MMIBACKUP,2=UNSTANDARD,
3=UNSTANDARDBACKUP,4=STANDARD,5=FAKE); u32 sizeMB; u8 pad[16]}`), sent by the host
with a reserved-size arg (cmd 9 → `PR_M@0x08008d9c`). **Proven** (XML grammar +
`PR_P PInfo` log fields).

1. `FUN_08008be4` groups partitions by `isuserdisk` and **prepends a synthetic
   FAKE (type 5) entry per group** whose size = the group's sum ⇒ rPInfo. For the
   pen (A+B, both isuserdisk=0): `[FAKE(30MB+rest), A, B]`. **Proven.**
2. `FUN_08008724` builds `Information{u32 TotalLen=0x10+n·0x10; u32
   fs_start_block(= bin cursor, "fs Bootblock"); u16 resv; u8 nzones; u8 0}` and
   the `Zone_Group[i]` records (0x10 B):

   | off | field | value |
   |---|---|---|
   | +0 | StartAddr (**big-endian** u32) | FAKE: `cur_block − fs_start` (blocks); others: running offset · pages_per_block (pages) |
   | +4 | AddrCnt (**big-endian** u32) | FAKE: physical span from `FUN_0800ae60`; others: size · pages_per_block |
   | +8 | Subarea_Flag = 1 | |
   | +9 | Open_Flag (=bOpenZone) | +0xa Type (=zonetype); +0xb Symbol = drive−'A' |
   | +0xc | Nand_NO (fake-zone ordinal) | +0xd Partition_NO (running index) |
   | +0xe/+0xf | Nand_Char / Medium_Char (protection byte ×2) | |

   Zone sizes: `blocks = sizeMB·1048576 / blockbytes` (computed in *float*,
   `0x49800000f = 1048576.0`). For a FAKE zone `FUN_0800ae60` walks physical
   blocks from the start, **skipping factory-bad blocks**, until
   `needed + resv·ceil(needed/0x800)` good ones are covered; the physical span
   (incl. bad) becomes AddrCnt, and it emits the **fake-zone group table**: one u16
   per 0x2000-block group = good-block count + carried base (i.e. a coarse
   bad-block-compensation table the runtime uses to resolve linear addresses
   across bad blocks). The concatenated per-zone records
   (`{u16 len=2·ngroups+4, u16 zone_idx, u16 groups[]}`) fill the 0x200 area
   stored after Zone_Group in block 63. **Proven** (code); exact group-table
   semantics at the ±1 level **Inferred**.
3. `FUN_0800d1ec` + `FUN_0800d300` per zone: FAKE → `FormatBlk@0x080103d4`
   (= NandMtd_Format: **erase every block in the span**, count bad;
   `"NandMtd_Format: the tatal bad block is %d in MTD!"`) then create the medium
   object (`FUN_08018f14`); non-FAKE → `Medium_CreatePartition@0x0800b0b8`
   (start/count in pages, 0x200-B sectors) inside the group's medium. **Proven.**

Note: with flash_ic `flag = 0x1` (no `0x10000000` wear bit) **all** resolvers are
the linear flavor (`Nand_CreatePartition`'s selector, nftl-layout §3) — the pen's
udisk is *not* wear-leveled, consistent with the "NFTL under FAT is linear" result in
[`nftl-layout.md`](nftl-layout.md). NFTL per-block 8-byte tags and info pages are written lazily by the MtdLib
write path (producer contains the same `NFTL_V1.2.2` MtdLib as PROG), *not* by the
format step: a freshly formatted FS area is simply all-0xFF. **Proven** (FormatBlk
only erases; nftl tag writing = `nftl_core_rw_sector`, already specced in
nftl-layout §5).

### 2.5 FAT filesystem / superblock

Two producer paths (**Proven**):

- **On-device mkfs** (cmd 10, `pr_m_driver_format@0x0800d73c`): calls the FsLib
  formatter (`FUN_080196d4`) and then writes an 11-byte **volume label** (attr
  0x08) into the root directory. The FsLib formatter is the same code family as
  PROG's `FUN_0803b310@0x0803b310` — a complete, self-contained FAT12/16/32 mkfs:
  picks cluster size from volume size, computes FAT size iteratively
  (`FUN_0803bccc`), writes BPB (+0x55AA, backup BPB at +6 for FAT32), FSInfo
  (`RRaA`/`rrAa`), zeroes both FATs, seeds `FAT[0..1] = fffffff8/ffffffff`,
  clears the root region.
- **Host-streamed image** (cmds 0xb,0x14..0x17, PR_I): `PR_I@0x0800d968` receives
  a 0x22-byte BPB-shaped descriptor from the PC tool (bytes/sector, sec/cluster,
  FAT size, offsets fat1/fat2/root, data size — all logged), and
  `FUN_0800dd78/0x0800dcb8/0x0800e348` write the host-built data/FAT regions
  through the medium (which adds the NFTL tags). I.e. at the factory the actual
  udisk *content* (`to_udisk` files from the `.upd`) is laid down by the PC tool
  as a ready-made FAT image.

At runtime PROG can also (re)create the FAT itself: `fs_storage_mount_init@
0x0803a484` (MT) → if `fs_partition_scan` fails on partition 0 →
`FUN_08039d00@0x08039d00` → `FUN_0803b310` formats it in place. **Proven.**

### 2.6 Boot-time consumers (who reads these structures)

- **SPL** finds `"PROG"` via the bin-info (+ ASA for bad blocks) — strings in
  `nandboot.bin`; loads PROG from the RB/UB map. **Proven strings / Inferred flow.**
- **PROG mount** (`fs_storage_mount_init@0x0803a484`, MT, Proven): device init
  (`FUN_0803a84c` → returns device obj; erase-status scan over all 4096 blocks);
  read **block `dev[+0x14]−1`** = zone table; take `fs_start = Information[+4]`
  and the fake-zone group table (copied from `buf + TotalLen + 6`, len =
  `first_group_rec.len − 4`); create the medium over `[fs_start, total)`
  (`FUN_0803be74`); look up Zone_Group by `Symbol` (`FUN_0803aa3c`, returns
  big-endian AddrCnt at +4) and create partition A at `[0, cntA)`, partition B at
  `[cntA, cntA+cntB)` (the "flash_erase_region"-named function is actually
  Medium_CreatePartition — misnamed); `fs_partition_scan` each (MBR optional: BPB
  accepted at sector 0); format A on failure, error-flag B on failure.
- **PROG self-update** rewrites bins via RB/UB exchange and refreshes blocks
  59..63 (`pr_p_write_maps`); `flash_program_region@0x0803cc80` (misnamed) saves
  blocks 61/62/63 (0x200 B each), performs an erase op, and rewrites them.

---

## 3. Function name + signature table

Producer.bin (base 0x08000000). "sig" uses `chip` = the 0x58-B object from
`s_ecctype` (fields: +4 flag, +8·+0x10 total blocks, +0xc·+0x10 blocks/die,
+0x14 pages/block, +0x1c pagesize).

| proposed name | addr | signature | purpose |
|---|---|---|---|
| `pr_cmd_media` | 0x08006738 | `int f(int *pkt, ...)` | command dispatcher: init/partition/format/image/boot |
| `pr_cmd_burn` | 0x08006408 | `int f(u32 *pkt, ...)` | command dispatcher: asa/bin/map/compare |
| `pr_t_set_resv_area` | 0x08006d5c | `int f(int *pkt)` | "ResvArea size/bErase" setter |
| `pr_p_burn_mode` (=PR_P) | 0x08007c98 | `int f(int mode)` | store burn mode 0/1/2/3/4 |
| `pr_p_set_gpio` | 0x08007d2c | `int f(int *pkt)` | GPIO config |
| `pr_p_read_chip_ids` | 0x08007e00 | `int f(u32,void*,void*,void*)` | probe CE0..3 chip IDs |
| `pr_p_read_partition_info` | 0x08007f88 | `int f(void)` | read block ppb−1 → Information/zones + fake map (modes 2/4) |
| `pr_p_init` | 0x08008080 | `int f(u8 flashic[0x40])` | copy nand param, `s_ecctype`, mode-3 erase-all, BinBurn init |
| `pr_p_detect_nand_param` | 0x080082a0 | `int f(u8 out[0x40])` | build flash_ic from chip ID |
| `pr_p_asa_mount_or_format` | 0x08008324 | `int f(int asa_type)` | cmd 7: asa_init; format (type1=EWR) or mount; total-BB count |
| `pr_p_get_disk_info` | 0x08008630 | `void f(int *pkt)` | report disk name/pagecnt |
| `pr_p_build_zone_groups` | 0x08008724 | `int f(rPInfo*,uint n,uint fs_start_blk,uint resv)` | build Information+Zone_Group[]+fake maps |
| `pr_p_expand_pinfo` | 0x08008be4 | `rPInfo* f(PInfo*,uint n,uint *out_n)` | group by isuserdisk, prepend FAKE zone per group |
| `pr_p_partition_all` (=PR_M) | 0x08008d9c | `int f(PInfo *p,uint n,uint resv)` | cmd 9: full zone build + medium/partition creation |
| `pr_p_write_maps` | 0x08009470 | `int f(void)` | cmd 0x22: write blocks 59..63 (maps/bininfo/zonetab) |
| `asa_lib_init` | 0x080096b4 | `void f(AsaCfg cfg[0x24])` | copy AsaLib config |
| `asa_sync_backups` | 0x080098e0 | `void f(uint blk,void *pagebuf)` | refresh stale ASA copies |
| `asa_repair_blk` | 0x08009a68 | `int f(uint blk,void *pagebuf)` | "repair asa blk %d times %d" |
| `asa_init` | 0x08009c50 | `int f(AsaCfg*)` | scan blocks step·1..50 for "ANYKAASA", pick max-times copy |
| `asa_restore` | 0x0800a09c | `int f(void)` | "asar::" restore path (mode 2 mount) |
| `asa_read` | 0x0800a33c | `int f(uint)` | "asas::" read bitmap |
| `asa_write` | 0x0800a530 | `int f(u32,int,int,int)` | "asaw::" update bitmap/times |
| `pr_fake_zone_span` | 0x0800ae60 | `int f(chip*,uint start_blk,int nblk,int resv,u16 *groups,u16 *ngroups)` | good-block walk → physical span + per-0x2000 group table |
| `Medium_CreatePartition` | 0x0800b0b8 | `Part* f(Medium*,uint start_pg,int cnt_pg,uint secsz,int type)` | logical partition on a medium |
| `pr_b_recover_bin_info_impl` | 0x0800b338 | `int f(void)` | mode-2/4 reload of bininfo+maps from flash |
| `pr_b_exchange_blocks` | 0x0800b574 | `int f(...)` | swap RB/UB roles after update ("after exchange") |
| `pr_b_geom_init` | 0x0800b670 | `void f(void)` | fill {pagesize,cap,ppb} @0x0802afa0 from flash_ic (512-B chips → 2048/64) |
| `pr_b_init` (BinBurn_Init) | 0x0800b6c4 | `int f(void)` | alloc 0x111c buffer: counts/bininfo[10]/maps; mode2/4 recover |
| `pr_b_set_start_block` | 0x0800b7b4 | `void f(int inc)` | advance bin start cursor |
| `pr_b_begin_bin` | 0x0800b934 | `void f(uint type)` | type0: next RB/UB pair, bump bin counters; type3: boot |
| `pr_b_file_info` | 0x0800b98c | `int f(struct{u32 len;u32 ld;char name[15]}*)` | cmd 0x10: create bin-info record + map count |
| `pr_b_end_bin` | 0x0800bb04 | `void f(void)` | finalize bin, log RB/UB |
| `pr_b_write_boot_stream` | 0x0800bb6c | `int f(void *data,int len)` | cmd 0xd: page-stream boot write |
| `pr_b_write_block_pair` | 0x0800bc1c | `uint f(u16 pair[2],uint pg0,uint npg,void *data)` | write pages to RB **and** UB; tags 0/0x11235813; bad-skip ≤0x31 |
| `pr_b_write_data` | 0x0800bfc0 | `int f(void *data,int len)` | cmd 0x11/0x29: split to blocks, dual-write, record map {RB,UB} |
| `pr_b_compare` | 0x0800c244 | `int f(void*,int)` | verify bin vs flash |
| `pr_b_read_bin` | 0x0800c444 | `int f(void*,int)` | read bin back |
| `pr_b_write_map_blocks` | 0x0800c5d4 | `int f(void *pagebuf)` | write per-bin maps → blocks ppb−3−nbins+i, tag 0x12121212 |
| `pr_b_write_bin_info` | 0x0800c760 | `int f(void *pagebuf)` | blocks ppb−3 (tag 0x34343434) + ppb−2 (tag 0x56565656) |
| `pr_b_select_boot` (BOOTBIN) | 0x0800c89c | `int f(char *name,u32 *out_len)` | match "BOOTBIN"/bin name, arm boot write |
| `pr_b_write_boot_data` | 0x0800ca48 | `int f(void *data,int len)` | boot pages via HAL boot op; overflow blocks tag 0x12345678 |
| `pr_b_read_boot` | 0x0800cd04 | `int f(void*,int)` | read boot back |
| `pr_b_map_offset` | 0x0800ceb4 | `short f(uint bin)` | Σ map_counts[0..bin)·4 |
| `pr_chip_factor` | 0x0800cf18 | `int f(void)` | block-number multiplier (1; 2 for upscaled dual-die) |
| `pr_write_page` | 0x0800cf54 | `int f(int blk,int pg,void *d,u32 *tag,int taglen)` | HAL write, blk·factor |
| `pr_read_page` | 0x0800cf9c | `int f(int blk,int pg,void *d,u32 *tag,int taglen)` | HAL read |
| `pr_erase_block` | 0x0800cfe4 | `int f(int blk)` | HAL erase |
| `pr_is_bad_block` | 0x0800d000 | `int f(int blk)` | HAL factory-bad check |
| `pr_m_fs_mtd_init` | 0x0800d0d0 | `void f(void)` | one-shot FsLib+MtdLib init ("fs and mtd init") |
| `pr_m_prepare_zones` | 0x0800d1ec | `int f(int,Information*)` | alloc medium/partition ptr arrays ("fs Bootblock: %d") |
| `pr_m_create_zone` | 0x0800d300 | `int f(int chip_idx,ZoneGroup *z,u16 resv,int do_format)` | FAKE→FormatBlk+medium; else Medium_CreatePartition |
| `pr_m_driver_format` | 0x0800d73c | `int f(int,int disk,char label[11])` | cmd 10: FsLib mkfs + volume label |
| `pr_i_set_img_info` (PR_I) | 0x0800d968 | `void f(u8 bpb[0x22],void *out0x20)` | host FAT geometry → offsets (fat1/fat2/root/data) |
| `pr_i_medium_info` | 0x0800dba8 | `int f(char disk)` | report medium geometry |
| `pr_i_write_mtd_data` | 0x0800dcb8 | `int f(...)` | raw medium write ("fail in write mtd data") |
| `pr_i_write_img` | 0x0800dd78 | `int f(int *pkt)` | write host FAT image (W1/W-fat/W2) |
| `pr_i_read_data` / `pr_i_read_fat` / `pr_i_write_fat` | 0x0800dfa0 / 0x0800e0f0 / 0x0800e348 | | image verify/patch helpers |
| `asa_set_scan_mode` | 0x0800e410 | `void f(u32 mode_pattern[2])` | select scan 0=read / 1=EWR + pattern |
| `asa_scan_ewr` | 0x0800e428 | `int f(u8 *bitmap,int bytes,uint pattern)` | destructive erase/write/read scan |
| `asa_check_block` | 0x0800e850 | `int f(int type,uint blk)` | is-ASA-block / param check |
| `asa_scan_initial_bad` | 0x0800eb4c | `int f(u8 *bitmap,int bytes)` | factory bad-marker scan (+ block-0 health) |
| `asa_format` | 0x0800ed98 | `int f(AsaCfg*,uint*)` | build bitmap+header, write ≤5+1 ASA blocks |
| `asa_copy_count` | 0x0800f160 | `int f(void)` | copies written |
| `nand_init_chip` (s_ecctype) | 0x08004ccc | `chip* f(flash_ic *p)` | chip object from param row; upscale check |
| `FormatBlk` | 0x080103d4 | `int f(chip*,int start_blk,uint nblk)` | NandMtd_Format: erase span, count bad |
| `Medium_Create` | 0x08018f14 | `Medium* f(chip*,int start_blk,int nblk,int type)` | medium over fake zone (from call site) |
| `fs_driver_format` | 0x080196d4 | `int f(Part*,int)` | FsLib mkfs (producer twin of MT 0x0803b310) |
| `udivmod32` | 0x08016598 | `u64 f(u32 div,u32 num)` | (quot,rem) — ceil-div idiom everywhere |

Key producer globals: `0x0802aedc` chip**; `0x0802aee0` `{u16 fake_map_len; void
*fake_map_0x200}`; `0x0802aee8` PR ctx `{+4 mode; +8 ok; +0x14 flashic copy;
+0x18 Information*}`; `0x0802af0c` OS/HAL vtable (malloc/free/memset/memcpy/printf
+ NAND ops: +0x34 erase&write-page0-with-tag, +0x38 read-page0, +0x20/+0x24/+0x28
erase/write/read page, +0x2c boot-page write, +0x38(chip) bad-check);
`0x0802af78` AsaCfg; `0x0802afa0` PR geometry; `0x0802afbc` buffer ptrs;
`0x0802afc8` burn ctx. Magic constants (all **Proven**, read from producer.bin):
`"ANYKAASA"` @0x0801b1ec; spare tags `0x11235813` (blank page), `0x12345678`
(boot data), `0x12121212` (map), `0x34343434` (bin-info), `0x56565656` (bin-info
header), `0x5a5a5a5a` (zone table); ASA copy-counter spare.

MT (PROG @0x08009000) — additions/corrections for names.csv:

| proposed name | addr | note |
|---|---|---|
| `upd_start_flash_mode2` | 0x080f27f0 | sets `pr_p_burn_mode(2)` before update |
| `pr_p_read_partition_info` | 0x08105344 | twin of producer 0x08007f88 |
| `pr_b_geom_init` | 0x08105c60 | twin of producer 0x0800b670 |
| `pr_b_write_bin_info` | 0x08106dc0 | twin of producer 0x0800c760 |
| `pr_b_page_step` | 0x08106fd4 | page-cursor advance, wraps at ppb |
| `pr_b_map_offset` | 0x081070a4 | twin of producer 0x0800ceb4 |
| `zonetab_lookup_size` | 0x0803aa3c | read block ppb−1, find Zone_Group by Symbol, return BE AddrCnt |
| `nand_device_init` | 0x0803a84c | returns the device obj (0x08008cc4) it seeds |
| `upd_rewrite_meta_blocks` | 0x0803cc80 | currently misnamed `flash_program_region`: save+erase+rewrite blocks 61/62/63 |
| `fat_partition_create_format` | 0x08039d00 | create partition obj + `fat_format_volume` |
| `fat_format_volume` | 0x0803b310 | full FAT12/16/32 mkfs (BPB/FSInfo/FATs/root) |
| `fat_calc_fatsize` | 0x0803bccc | iterative FAT-size solver |
| `udiv10` | 0x0803062c | div-by-10 (map +10% spare allowance) |
| `Medium_CreatePartition` | (the fn named `flash_erase_region` used in 0x0803a484) | misnamed; creates partitions A/B from zone sizes |

(Existing MT names `pr_p_write_maps@0x08105118`, `upd_build_log2phy_blockmap@
0x08105924`, `pr_b_*`, `upd_unpack_*` are confirmed correct; their tags match
producer's byte-for-byte.)

---

## 4. The on-flash factory layout, block by block

The structure-writers above collapse, for a healthy chip with **zero bad blocks**, to a
deterministic byte layout. Documented here as the concrete on-flash image the producer
lays down (a useful spec for reconstructing or validating a NAND image). Inputs (all read
from the `.upd`): the flash_ic row (2048/64/4096/2048/64/flag1 from `.upd`@0x200 row 48),
partition records (`.upd`@0xa4), the bins (`.upd` to_NAND: PROG, codepage + boot @0x20000),
a chosen bin start block (≥64), and the reserve size.

1. **ASA**: blocks 1..5(+1): page0 = `"ANYKAASA"` + directory (npages=1 ⇒ +0x14=3,
   +0x18=1,+0x1a=1,+0x20=2,+0x22=1), page1 = page2 = all-zero bitmap (no bad blocks),
   last page = header copy; spare u32 = copy index (1..5); times=1.
2. **Bins at S=64**: PROG blocks 64..119 as RB/UB pairs (even=RB copy, odd=UB copy of
   each 128 KiB chunk), codepage 120..133; page spare u32 = 0, or 0x11235813 for all-FF
   pages. Map for bin i at block `61−nbins+i` (59,60): `{u32 0x12121212}` + per-chunk
   `{u16 2j+S′, u16 2j+S′+1}`; spare 0x12121212.
3. **Block 61**: `{u32 0x34343434}` + 2×0x24 bin-info (`len, ld=0, mapblk, startblk, 0,
   name`); spare 0x34343434. Block 62: `{3,0,0,2}`; spare 0x56565656.
4. **Block 63**: Information `{0x40, fs_start=134, resv, 3, 0}` + 3 Zone_Group records
   (FAKE/A/B; A=30MB→240 blocks·64 pages, B=rest) + `u16` + 0x200 fake-map
   (`{len=6,zone=0,group0=good_count}` per zone with no bad blocks); spare 0x5a5a5a5a.
   *(Open: the exact byte order/unit of the Zone_Group `StartAddr`/`AddrCnt` field — and
   hence whether A: is 30 MB or 60 MB — is unresolved; see
   [`nftl-medium-translation.md`](nftl-medium-translation.md) §0.)*
5. **FS area (from block 134)**: erased (0xFF) — a valid fresh state, exactly what the
   factory `FormatBlk` leaves; `fs_storage_mount_init` mkfs's partition A on first boot
   (`FUN_08039d00`→`FUN_0803b310`) when the scan finds it unformatted. NFTL 8-byte tags
   appear only where FatLib actually writes (nftl-layout §5).

Caveats: (a) the bin start block and fs start are **host-chosen** — 64/134 are a safe
reconstruction, not read off a real pen (Inferred); (b) the device-struct unit question
(§0: sector vs page units) shifts only the zone-table block (63 vs 255) — verify against a physical
dump or SPL disasm; (c) ECC bytes in the spare are HAL-generated and out of scope here.
