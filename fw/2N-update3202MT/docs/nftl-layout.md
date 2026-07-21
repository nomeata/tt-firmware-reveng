# NFTL layout — the read side (2N / update3202MT)

Storage-stack reference for the tiptoi pen's NAND: device geometry, the NFTL logical
block device (log2phy), the partition table, the spare/OOB + ECC format, and the two
log2phy resolvers. This is the **read-side** reference; the write / copy-on-write /
fold path is documented in [`nftl-write-consistency.md`](nftl-write-consistency.md).

**Conventions.** All addresses are the unified runtime address (Ghidra base = runtime =
`0x08009000`). Data globals (e.g. the device object `0x08008cc4`, the partition-boundary
global `0x081226f8`) live in low RAM / bss at their own fixed addresses. Each claim is
tagged **Proven** (traced in code/disasm) or **Inferred**.

---

## 1. Object model (who points at whom)

Three nested objects carry the storage state. Traced through the read path
`FUN_08010078` / `FUN_08010144` and the install site `FUN_0803a84c`:

```
NFTL/MTD partition object  ──+0x94──▶  CHIP object  ──+0x54──▶  DEVICE object (= 0x08008cc4)
   (per partition)                       (nftl+0x94)              (physical die + HAL leaves)
```

- **partition +0x94 → chip** — used everywhere as `iVar2 = *(nftl+0x94)`
  (`nftl_core_rw_sector@0x08043a74` line `iVar2 = *(int*)(nftl+0x94)`;
  `nftl_load_info@0x0804855c`). **Proven.**
- **chip +0x54 → device** — the page ops deref `*(chip+0x54)` then call `+0x20/+0x24/
  +0x28/+0x2c` (`FUN_08010078@0x08010078`, `FUN_0801027c@0x0801027c`,
  `FUN_08010144@0x08010144`, `FUN_080103ac@0x080103ac`). **Proven.**
- **device object is `0x08008cc4`** — `FUN_0803a84c@0x0803a84c` seeds
  `*(0x08008cc4+0x20..+0x3c)` with the physical read/write/erase leaves
  (`0x0800fcc4` write, **`0x0800fb00` read** = task's page-read leaf, etc.).
  Literal pool at `0x0803a940` = `{0x08008cc4, 0x0800fcc4, 0x0800fb00, 0x0803d868,
  0x0800faa4, 0x0800f9cc, 0x0800f92c, 0x0803d7b8, 0x0803d76c}`. **Proven.**

The device object lives in low RAM (`0x08008xxx`, bss) — it is *seeded at runtime by
the SPL*, so its numeric geometry fields are not present in the static PROG.bin image;
their values come from prior runs/dumps (marked Inferred where so).

### Struct field cheat-sheet

DEVICE object (`0x08008cc4`) — Proven offsets from disasm of the leaves:

| off | meaning | evidence |
|---|---|---|
| +0x14 | **row stride = sectors per erase block** (`= 0x100 = 256`) | read leaf `0x0800fb44`: `mla r6, [dev+0x14], block, page` → `row = dev[+0x14]·block + page`. Value 256 from the seeded struct. |
| +0x1c | I/O sector-size factor | `FUN_0800fe08@0x0800fe08` (see §4) |
| +0x20 | write-page leaf | `FUN_0803a84c` seed = `0x0800fcc4` |
| +0x24 | read-page (data) leaf | seed = **`0x0800fb00`** |
| +0x28 | **write/PROGRAM data+tag leaf** = `0x0800f92c` | `FUN_0803a84c` disasm |
| +0x2c | **read data+spare-tag leaf** = `0x0800f9cc` (→ NB_READ_DATA `0x0800260c`) | `nb-read-data-conventions.md` |
| +0x30 | erase-check leaf | seed = `0x0800faa4`… (`nand_EraseBlk` calls `dev+0x30`) |
| +0x3c | erase leaf | seed = `0x0803d76c` |
| +0x54 | (unused here; device is leaf) | |

CHIP object (`nftl+0x94`) — Proven offsets:

| off | meaning | evidence |
|---|---|---|
| +0x04 | flags: `0x10000000`=wear-level, `0x40000`=skip danger-check, `0x800`=double | `Nand_CreatePartition`, `nftl_core_rw_sector` (`&0x40000`), `FUN_0803a84c` (`&0x800`,`&0x10000000`) |
| +0x0c · +0x10 | **sectors per block** (product) used by the linear resolver | `FUN_0800fef0@0x0800fef0`: `iVar2 = chip[+0x10]·chip[+0xc]` |
| +0x14 | die pages/block (compared to device+0x14 for die split) | `FUN_0800feac@0x0800feac` |
| +0x18 · +0x1c | **page + spare buffer size** (product) | `mtd_init@0x08045e80` `local_44 = chip[+0x18]·chip[+0x1c]`; `nftl_core` `local_48`; `nftl_load_info` `uVar1` |
| +0x54 | → device | above |

NFTL/MTD partition object — Proven offsets (from `Nand_CreatePartition@0x080443d0`,
`mtd_init`, `nftl_core_rw_sector`, `nftl_load_info`):

| off | meaning |
|---|---|
| +0x02 | plane/mode byte (`FUN_0801027c`: compared to 2) |
| +0x64 (100) | sectors-per-page group / NFTL chain width (=1, set by `Nand_CreatePartition`) |
| +0x6e | **partition start** (block/sector base), added to logical sector by both resolvers |
| +0x70 | partition end / max logical sector (bounds check: `param_2 > [+0x70]` → error) |
| +0x72 | `[+0x70] − size` |
| +0x7c | ptr to per-block danger/version byte array (`FUN_08010248`) |
| +0x8c | ptr to **log2phy page map** buffer (size `nsectors·4`) |
| +0x90 | `[+0x8c]+0x80` scratch / RLE region (see §3, §6) |
| +0x94 | → chip |
| +0x19c | NFTL info descriptor `{fmt, block, page, ...}`; `+4` = the metadata-block number (see §6) |
| +0x1cc + i·8 | per-plane bad-count + min-index tables (built in `mtd_init`) |
| +0x1d2 + i·8 | zone-size table consulted by the **wear-leveled** resolver (`FUN_0800ff74`) |
| +0x24c | write-sector(+tag) op = `0x0801027c` (calls dev+0x20) |
| +0x250 | read-data op = `0x08010078` (dev+0x24) |
| +0x254 | read-OOB op = `0x080103ac` (dev+0x28) |
| +0x258 (600) | read data+tag op = `0x08010144` (dev+0x2c) |
| +0x25c | erase/mark op = `0x08044628` |
| +0x260 | (block-copy/relocate) = `0x080444f4` |
| +0x264 | factory-bad / danger check = `0x080446c0` |
| +0x268 | mark-bad = `0x08044754` |
| +0x26c | **log2phy resolver** (linear `0x0800fef0` or wear `0x0800ff74`) |
| +0x270 | size/geometry helper (`0x0800ff6c` / `0x08010030`) |
| +0x274 | partition init fn (`0x08044824` / `0x08044948`), called by `mtd_init` |

---

## 2. Device geometry

The chip is **Hynix HY27UF084G2B** (4 Gbit / 512 MiB, ×8, 2 KiB page). The firmware
addresses it in **512-byte sectors** (the HW-ECC unit), giving two consistent views of
the same 512 MiB:

| quantity | firmware/NFTL view (512-B sector unit) | datasheet/physical view |
|---|---|---|
| I/O sector | **512 B** (Proven, §4) | column within page |
| erase block | **256 sectors = 128 KiB** (`dev[+0x14]=0x100`) | 64 pages × 2 KiB = 128 KiB |
| page | (= 4 sectors) | **2048 B data + 64 B spare** |
| blocks/device | **4096** | 4096 |
| total | **512 MiB** | 512 MiB |

- **Row address (Proven).** `row = device[+0x14]·block + page`, from the read leaf
  `0x0800fb00`: `mla r6, [r9+0x14], r1, sl` at `0x0800fb44`
  (`r9`=device, `r1`=block, `sl`=page). `device[+0x14] = 0x100 = 256` (seeded struct).
- **512-B sector unit (Proven).** `FUN_0800fe08@0x0800fe08` builds the read descriptor
  and sets the sector size to `0x200` (512) when the plane byte `[0x08008ca8] ≤ 2`,
  else `0x400` (1024). Dual-plane ⇒ 512. This is the HW-ECC chunk size.
- **Reconciliation (Inferred).** 256 sectors/block ÷ 4 sectors/page = **64 pages/block**,
  matching the HY27UF084G2B datasheet (page 2048 B + 64 B spare, 64 pages/block, 4096
  blocks). Both views give 4096 × 128 KiB = **512 MiB**; "64 pages/block" and the
  firmware's `dev[+0x14]=256` are the *same* geometry in different units.
- **Physical–logical remap in the leaf (Proven).** Before the `mla`, `0x0800fb28`
  loads `{*0x081226f8, *(0x081226f8+4)}` and, if the two differ **and** `block ≥
  first`, does `block += second` — a gap-skip. Global `0x081226f8 = {0x800, 0x800}`
  (§4): equal ⇒ no gap currently. Populated by `FUN_0803a84c@0x0803a830` via literal
  `0x0803a97c = 0x081226f8`.

---

## 3. Logical → physical sector mapping (log2phy)

The NFTL presents a **linear logical block device** to the FAT layer. A logical sector
N is resolved to a physical `(block, page-in-block)` by the resolver at
`partition+0x26c`, then handed to the device leaf. The read path (Proven,
`FUN_08010144@0x08010144`, mirrored in `FUN_08010078`/`FUN_0801027c`):

```
phys_page_in_block = (**(part+0x26c))(part, logical_sector, &phys_block);   // resolver
FUN_0800feac(chip, &phys_page_in_block, &phys_block);                        // die split
(**(chip+0x54 + 0x2c))(device, phys_block, phys_page_in_block, ..., tagbuf, 8);
```

- Bounds: `logical_sector > partition[+0x70]` ⇒ error ("… out of range").
- `FUN_0800feac@0x0800feac` (die split, Proven): if `chip[+0x14] ≠ device[+0x14]`
  then `block <<= 1`; if `page ≥ device[+0x14]` then `page -= device[+0x14]; block++`.
  (Splits a logical block that spans two physical dies/planes.)

There are **two resolver flavors**, selected in `Nand_CreatePartition@0x080443d0` by
`chip[+0x04] & 0x10000000`:

### (a) Linear / flat — system partition (Proven)

`FUN_0800fef0@0x0800fef0` (installed when `chip[+4]&0x10000000 == 0`):

```
sectors_per_block = chip[+0x10] · chip[+0x0c];               // = 256
addr              = partition[+0x6e] + logical_sector;       // start offset + N
phys_block        = addr / sectors_per_block;   // udivmod quotient (r0)
page_in_block     = addr % sectors_per_block;   // udivmod remainder (r1)
```

`func_0x07ffa1d0(D,N)` is the unsigned div/mod HAL returning `(N/D, N%D)` (semantics
Proven from every call site, e.g. `upd_build_log2phy_blockmap` ceil-div and both
resolvers). Because `sectors_per_block == device[+0x14]`, the read leaf's
`row = dev[+0x14]·block + page` collapses to **`row = partition[+0x6e] + logical_sector`
— a pure identity/linear map, no wear-level indirection.** This is why "the NFTL under
FAT is linear" holds, and why PROG + codepage can be laid down flat and read directly by
the SPL/codepage loader.

**On the pen, the FS-area (udisk) group also uses the linear resolver** — its
`chip[+4]` has the `0x10000000` wear bit *clear*. So `+0x26c` is the linear
`FUN_0800fef0` for both the system and the udisk group; the *last* translation step is
identity (`abs = part[+0x6e] + rel_block`). This does **not** mean data stays in place
across writes — relocation happens above this layer, through the dense window + spare
tags (see [`nftl-write-consistency.md`](nftl-write-consistency.md) §4).

### (b) Wear-leveled — the alternate flavor (Proven)

`FUN_0800ff74@0x0800ff74` (installed when `chip[+4]&0x10000000 ≠ 0`): walks the
per-zone size table `partition[+0x1d2 + i·8]`, subtracting `2·zonesize` per zone until
the sector lands in a zone, then indexes with a `·2 (+1)` fold — a two-plane interleave
over wear-leveled zones — before the same `udivmod` to `(block,page)`. Not selected on
this pen (wear bit clear), documented for completeness.

### The read resolver `FUN_0800f3ac` (dense window + RLE) — Proven

The op at `+0x26c` (above) is only the final block→row step. The logical→physical head
lookup that precedes it lives in the resident resolver `FUN_0800f3ac@0x0800f3ac`,
reached through the medium read op:

```
FUN_0800f3ac(part, logical):
  find window i with win[i] <= logical < win[i]+0x10    (part+0x48+i·2)
  uVar9 = *(u16*)(part[0x8c] + ((logical − win[i]) + i·0x10)·2)   // dense-window head
      if uVar9 == 0xffff -> free, return
      if no window matches (logical >= 0x40) -> uVar9 stays 0      // out-of-window
  rle_total = FUN_0800efc0(part)          // sum of RLE run-length bytes at +0x90
  p = FUN_0800f020(part, uVar9, &base)    // walk RLE for the run whose base == uVar9
  if run == 0xff:   // not in RLE (first touch): chain-walk the spare-tag `next`
                    //   ([2:4]) until 0xfffd, then insert a run (small safe memcpy)
  else:             // already in RLE: compact via memmove
                    //   dst=part[0x90]+base·2, src=+run·2, len=((rle_total−base)−run)·2
```

- **Dense-window geometry (Proven):** `part+0x8c` is a **64-entry sliding window**
  (4 windows × 16 logical blocks, bases `part+0x48+i·2` = `{0, 0x10, 0x20, 0x30}`),
  covering logical blocks 0..63. Logical blocks ≥ 64 are served by faulting a window in
  from the persisted map region (mode 3) via `FUN_0800ee5c@0x0800ee5c` (evict LRU
  window, rebase to `logical & ~0xf`, reload).
- **RLE region (Proven):** `part+0x90` (0x280 B) holds a packed `u16`-short run array
  (run-length bytes stored top-down from `+0x90+0x27f`, `0xff` = end). A fresh
  `mtd_init` leaves it **all-0xff (empty)**; runs are inserted lazily on first resolve
  and edited on every write/fold. `FUN_080438a4` (RLE compaction, called from
  `nftl_core_rw_sector`) shares the same length arithmetic — the `·2` is because entries
  are `u16` shorts, not UTF-16 characters (an earlier reading mistook it for a
  wide-string routine).

### The log2phy table itself (Proven)

- **In RAM:** `partition[+0x8c]` points at a `nsectors·4`-byte map allocated/cleared in
  `mtd_init@0x08045e80` (`*(param_2+0x8c) = iVar7 + uVar13·4`, then `memset 0`). It is
  populated during the format/scan by reading each block's spare tag: `mtd_init` reads
  each block's tag, stores its `[4:6]` field, then `FUN_08047510@0x08047510` inverts it
  into `map[(iVar5[b] & 0x7fff)] = b` (logical→physical, heads only). Two heads claiming
  the same logical are arbitrated by chain length (`FUN_08044b04`, longer wins).
- **Update on write:** `nftl_update_log2phy@0x08042c58` — RAM-only, immediate; see
  [`nftl-write-consistency.md`](nftl-write-consistency.md) §5.
- **Rebuild from spare (Proven):** `upd_build_log2phy_blockmap@0x08105924` reconstructs
  a block map by reading two metadata pages and, per logical unit, computes
  `ceil(size/blocksize)` and copies the map. Here `piVar1[2]` is **pages-per-block
  (64)**, not total blocks, and the two indices `ppb−2`/`ppb−3` are **pages of the info
  block 0**, not block numbers — the metadata (bin maps, bin-info, zone table) all lives
  in block 0 (see [`nand-init-from-producer.md`](nand-init-from-producer.md)).

---

## 4. Partition table

| partition | physical blocks | size | layer | resolver |
|---|---|---|---|---|
| **boot / SPL** | ~0–4 | SPL 0x7e80 B | **raw** (mask-ROM/SPL read directly, no NFTL) | none |
| **system (PROG + codepage)** | flat, from a fixed base block | PROG 0x380000 + codepage 0xd6ccc | **NFTL linear** (no wear map) | `FUN_0800fef0` (linear); §3(a) |
| **udisk** | remainder, up to block boundary `0x800` | rest of 512 MiB | **NFTL linear (dense-window head map) → FAT16 on top** | `FUN_0800fef0`; §3 |

- **Partition boundary global `0x081226f8 = {0x800, 0x800}` (Proven).** Two 32-bit
  words = block 2048. Consulted in the physical read leaf (`0x0800fb28`) as a gap-skip:
  when the two words differ and `block ≥ word0`, blocks are shifted by `word1`. Equal
  today ⇒ no gap. Written by `FUN_0803a84c`.
- **`Nand_CreatePartition@0x080443d0`** installs the full op vtable (`+0x24c..+0x274`)
  and picks linear-vs-wear via `chip[+4]&0x10000000` (§3). It sets `partition[+0x64]=1`
  (single NFTL chain width).
- **The FS-area medium is one NFTL object.** `fs_storage_mount_init@0x0803a484` builds
  **one** NFTL partition per `0x2000`-block group (`FUN_0803be74`; the pen's FS area =
  4096−134 = 3962 blocks ⇒ 1 group, 1 object), then carves A: and B: as plain sector
  ranges with `Medium_CreatePartition@0x0803c784` using the Zone_Group `AddrCnt` values
  from the zone-table block. See [`ab-drive-layout.md`](ab-drive-layout.md).
- **FAT geometry (Proven).** `fs_partition_scan@0x08039a68` reads logical sector 0 —
  natively a bare FAT **VBR/BPB (superfloppy, no MBR)**; an MBR with one FAT
  sub-partition is also accepted via a fallback heuristic
  ([`partition-a-fat-vs-mbr.md`](partition-a-fat-vs-mbr.md)) — and
  `medium_format@0x080421c8` parses the FAT16/EXFAT BPB (bytes/sector, reserved, #FATs,
  root entries, cluster shift) and stores cluster/FAT geometry into the volume object
  (`iVar7+0x14..+0x30`). FatLib runs **on the NFTL linear logical device** — no
  NFTL/ECC metadata is visible at the FAT layer.

The **system partition size** and **start block** come from `partition[+0x6e]` /
`[+0x70]`, set in `mtd_init@0x08045e80` (`*(param_2+0x6e)=StartBlock`, `+0x70=EndBlock`,
`+0x72=size`) from the args logged as `"MtdLib::Init::StartBlock=%d Block=%d"`.
`nftl_load_info@0x0804855c` + `nftl_init@0x08044d28` then load the NFTL info descriptor
from near the top of the partition (reads block `param_2[1]`, page `param_2[2]`;
`0x540`-byte info area, `0x280`-byte header). Exact numeric block ranges are runtime args
(not literals in PROG).

### The FS-area A/B carve (Proven)

`fs_storage_mount_init@0x0803a484`:

1. `dev = FUN_0803a84c()` (device obj `0x08008cc4`); erase-status scan of all blocks.
2. Read block **`dev[0x14]−1`** (the **zone/partition table**, spare tag `0x5a5a5a5a`).
   From it: `fs_start = Information[+4]`, and the fake-zone group table.
3. `medium = FUN_0803be74(dev, fs_start, total − fs_start, fakemap)` — builds the FS-area
   medium over `[fs_start, total)`, split into `0x2000`-block groups; per group calls
   `mtd_init` with `reserved = fakemap[g]·ngroups`.
4. `cntA = FUN_0803aa3c(0)` (zonetab AddrCnt for Symbol 0 = 'A'), `cntB = FUN_0803aa3c(1)`.
   `partA = Medium_CreatePartition(medium, 0, cntA, 0x200, 1)`;
   `partB = Medium_CreatePartition(medium, cntA, cntB, 0x200, 0)`.
5. `fs_partition_scan(partA, …)`, then B.

`FUN_0800ea38` is the partition→medium read wrapper (`A+0x10`, seeded by
`Medium_CreatePartition` to `DAT_0803c9e0 = FUN_0800ea38`). Its descriptor
`desc = *(A+0x20)` is `memset 0xff` then filled `desc[0]=medium`,
`desc[2]=sectorfactor·base`, `desc[3]=sectorfactor·count`; a read whose logical index
exceeds the partition bound `A[0xc]` is **clamped to `desc[1]=0xffffffff`** (relevant to
sizing A: correctly — see [`nftl-resolver`] note below and
[`nftl-medium-translation.md`](nftl-medium-translation.md)).

---

## 5. Spare / OOB & ECC format

Physical page spare = **64 B** (16 B per 512-B sector). Two things live there: the
8-byte **NFTL tag** and the **HW-ECC** bytes; bad blocks are tracked via status codes.

### NFTL tag (8 bytes) — Proven from `nftl_core_rw_sector@0x08043a74`

Every page op passes a trailing `8` = tag length (e.g. `(**(chip+0x54+0x2c))(dev,
block, page, ..., tagbuf, 8)`). The **field split is settled for the pen (chain width
w=1)**:

| bytes | field | meaning (w=1, the pen) |
|---|---|---|
| [0] | **seq / version** | `seq & 0x7f`; **bit 0x80 = obsolete** (skipped/blank/copied-away page). Head selection is topological, not by seq; seq is ordering/display. |
| [1] | **sector index in page** | 0 (chain width 1) |
| [2:4] | **chain-next / status** | `0xfffd` = valid head / chain terminator; `0xffff` = free; `0xfffc`/`0xfffa` = bad/metadata; `< 0x8000` = points at an older chain member (that target loses head status) |
| [4:6] | **LOGICAL block + head flag** | `logical & 0x7fff`, `| 0x8000` when the chain terminates at this block. `mtd_init`/`FUN_08047510` invert this into `map[logical]=phys`. |
| [6:8] | `part[+0x68]` (= `0xffff`) | seeded from `DAT_08044fdc` in `mtd_init`; metadata pages use `part[+0x68]+1`. Not consulted on the read path. |

> **Note on an earlier "[4:6]=physical" reading.** A `[4:6]=physical` label appeared in
> earlier drafts; it came from the `sector_idx != 0` fold tag (`F & 0x7fff`), which only
> exists for chain width **w>1** — never on this pen. For w=1, `[4:6]` is the **logical**
> block, as above. Confirmed across the four tag consumers and the live scan.

The reader (`nftl_core`) walks the block chain following `[2:4]` until `0xfffd`,
skipping pages whose tag `[0]&0x80` is set (obsolete), taking each page from the first
non-obsolete chain member that covers it (single-block tails via the `next==0xfffd`
shortcut, no tag check). `FUN_08010248@0x08010248` stamps the per-block version byte
into `partition[+0x7c][...]`.

### Bad-block / danger status — Proven from `mtd_init` + `nftl_check_block_danger@0x08043780`

Block status is kept as 16-bit codes in the RAM block table (built during
`mtd_init@0x08045e80` init scan) and in the read tag's status field:

| code | meaning |
|---|---|
| `0xffff` | free / erased |
| `0xfffc` | factory or scanned **bad** block (`"InitPlane: factory bad"` / read-flag fail) |
| `0xfffa` | reserved / metadata marker (erased and rebuilt each mount) |
| `0xfff7` | **strong danger** (`"it is strong dange"`) — triggers relocation |
| (else) | `"it is weak dange"` (correctable, watched) |

`nftl_check_block_danger` maintains a small list (≤15) of endangered blocks with a
weak/strong byte flag, driving refresh/relocation. `nandmtd_format@0x080433bc` does the
initial factory bad-block scan (`"NandMtd_Format: the tatal bad block is %d in MTD!"`),
and `nand_EraseBlk@0x08042d34` erases + marks-bad-on-failure via `dev+0x30`/`dev+0x3c`.

### ECC scheme

- **Hardware ECC in the NFC (Proven/Inferred).** The physical leaves call the HAL
  ECC engine `func_0x07ff9044` (ECC on) vs `func_0x07ff960c` (raw), selected by
  `FUN_0800fe40@0x0800fe40`; the NFC is at MMIO `0x0404A000`
  ([`nfc-controller-registers.md`](nfc-controller-registers.md)). The read leaf checks
  the HAL return: bit `0x80000000` = hard fail, bit `0x40000000` = ECC path, mapping to
  error codes. ECC is computed/checked in the controller, per **512-B sector**.
  **Proven** that ECC is HW-per-512B; the **exact code** (Hamming vs BCH strength) lives
  in the NFC and is **Inferred** BCH (typical for an Anyka 2 KiB-page controller) — not
  decodable from PROG alone.

---

## 6. Metadata regions and the `nftl_init` reader

The persisted NFTL bookkeeping (log2phy map, free-run table, info header) lives in a
dedicated **metadata block** on flash, addressed through a small RAM page directory.

### The metadata page directory `part+0xe` (Proven)

`part+0xe` is an array of **29 u16 entries** (`0x3a` bytes, memset `0xff` by `mtd_init`).
Each entry = a **page number inside the current metadata block**. `nftl_init(part,
byte_offset, length, dest, mode)` reads a region; the `mode` selects a base index:

| mode | dir base | `part+` offset | slots | region (writer → reader) |
|---|---|---|---|---|
| 0 | 0 | `+0x0e..+0x1c` | 8 | free-run ("blank block") table continuation pages |
| 1 | 8 | `+0x1e` | 1 | previous-session snapshot page (best-effort salvage) |
| 2 | 9 | `+0x20` | 2 | NFTL info header `0x280` + tail (`nftl_load_info` — system path) |
| **3** | **0xb** | `+0x24..+0x42` | **16** | **the persisted log2phy map** (`span·2` bytes) |
| 4 | 0x1d | `+0x48` | — | past the directory — dead/unused (Inferred) |
| 5 | 0x1b | `+0x44` | 2 | secondary table (`nftl_init_2@0x080451a4`) |

The metadata block itself is `*(u16*)(info+4)` where `info = *(part+0x19c)` — on the
udisk medium `info[+4]` is the **current metadata block number** (partition-relative,
runtime-chosen). Read call (`nftl_init` lines 94-96):

```
entry = *(u16*)(part + 0xe + 2*(dir_base + byte_offset/pagesize));   // page number
iVar6 = (**(part+0x250))(part, *(u16*)(info+4) /*block*/, entry /*page*/, dest, tag);
```

`pagesize = chip[+0x18]·chip[+0x1c]`. A dir entry of `0xffff` ⇒ the region was never
persisted this session ⇒ `nftl_init` synthesizes erased content (`memset 0xff`) and
succeeds. **The metadata map is by design rebuilt and rewritten on every mount:**
`mtd_init`'s epilogue re-picks a metadata block (`FUN_08045b04` → `info[+4]`) and
rewrites all regions (`FUN_08047510` → `nand_mtd_3(part, 0, span·2, map, 3)`), tagging
the pages `[2:4]=0xfffa` — which is exactly why the scan **erases** any `0xfffa`-tagged
block and rebuilds.

### Leaf return conventions (Proven)

The op layer returns the device leaf's value unchanged, and **0 = failure, 1 = success**
at the PROG/NFTL level. The DATA leaf `0x0800fb00` returns 0 on HAL hard-fail (bit31),
1 on success (correctable count below threshold), else the `0xfff7` danger family.
Consumers (`nftl_init`, `nand_mtd_3`, `nand_mtd_2`) all treat 0 as failure and retry via
`mtd_reload@0x080466e8` (fresh object + full rescan + rewrite). The nandboot ops below
the leaves use the inner convention (0 = ok for `0x0800260c`); a leaf's epilogue
converts to the `1`/danger/`0` PROG codes.

---

## 7. What a NAND-dump model needs (summary)

To read a **real** NAND dump (not just a logical image):

1. Geometry: 512-B sectors, 256 sectors/block (128 KiB), 4096 blocks, 512 MiB; 64 B
   spare/page (16 B/sector). (§2)
2. Per-sector spare = 8-B NFTL tag (layout §5) + HW-ECC bytes; honor the `0x8000`
   head-valid flag; skip `tag[0]&0x80` obsolete pages; a chain tail (`[2:4]==0xfffd`) is
   accepted without a tag check. (§5)
3. **System partition:** linear map `phys = start + logical` (§3a) — read directly, no
   wear indirection; PROG + codepage sit here. (§4)
4. **udisk:** the linear resolver + dense-window head map (§3) under a FAT16 volume (§4);
   back reads at the logical-sector level and the real FatLib mounts a plain FAT16 image.
5. Partition boundary/gap: `0x081226f8={0x800,0x800}` → block 2048 split, no gap. (§3–4)
6. Metadata regions (§6) are rebuilt from spare tags on every mount — the truth is the
   tags, not any persisted table.

## Key code citations

| function | addr | role |
|---|---|---|
| device read leaf | `0x0800fb00` | `row = dev[+0x14]·block+page`; gap-skip via `0x081226f8`; returns 1=ok/0=fail |
| `FUN_0800fe08` | `0x0800fe08` | sector size 512/1024 selector |
| `FUN_0803a84c` | `0x0803a84c` | installs device leaves; seeds `0x081226f8` |
| linear resolver `FUN_0800fef0` | `0x0800fef0` | log2phy (identity `abs = start + rel`) |
| wear resolver `FUN_0800ff74` | `0x0800ff74` | alternate (zone table +0x1d2); not selected on pen |
| read resolver `FUN_0800f3ac` | `0x0800f3ac` | dense-window + RLE head lookup |
| `FUN_0800f020` / `FUN_0800efc0` | `0x0800f020` / `0x0800efc0` | RLE walk / RLE total |
| `FUN_0800ee5c` | `0x0800ee5c` | map-window fault-in / evict (mode 3) |
| `FUN_0800feac` | `0x0800feac` | die split |
| `Nand_CreatePartition` | `0x080443d0` | op vtable + resolver select (`chip[+4]&0x10000000`) |
| `mtd_init` | `0x08045e80` | geometry, log2phy alloc `+0x8c`, dense windows `+0x48`, bad-block scan, metadata rewrite |
| `mtd_reload` | `0x080466e8` | fail handler: fresh object + full rescan + rewrite + window reload |
| `nftl_core_rw_sector` | `0x08043a74` | chain walk + 8-B tag pack/parse (fold; see write-consistency doc) |
| `nftl_init` (loop `LAB_08044db4`) | `0x08044d28` | metadata-region reader + retry loop |
| `nftl_check_block_danger` | `0x08043780` | weak/strong danger list |
| `nftl_load_info` | `0x0804855c` | NFTL info descriptor load |
| `FUN_08047510` | `0x08047510` | inverts per-block `[4:6]&0x7fff` → `map[logical]=phys` |
| `nandmtd_format` / `nand_EraseBlk` | `0x080433bc` / `0x08042d34` | format scan / erase+mark-bad |
| `upd_build_log2phy_blockmap` | `0x08105924` | updater's spare-tag map rebuild |
| `fs_storage_mount_init` | `0x0803a484` | zone-table read, FS-area medium, A/B carve |
| `FUN_0803be74` / `Medium_CreatePartition` | `0x0803be74` / `0x0803c784` | one NFTL object per group; A/B as sector ranges |
| `FUN_0803aa3c` | `0x0803aa3c` | zonetab AddrCnt lookup by Symbol (0='A', 1='B') |
| `FUN_0800ea38` | `0x0800ea38` | partition→medium read wrapper; out-of-range clamp |
| `fs_partition_scan` / `medium_format` | `0x08039a68` / `0x080421c8` | FAT MBR/BPB on the linear NFTL device |
