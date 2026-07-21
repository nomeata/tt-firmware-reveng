# FatLib volume ↔ medium layering — the "root-dir read" that is really a codepage read

Question: after A: mounts as a FAT superfloppy, an apparent *root-directory* read is
routed to a RAW physical NAND row (block ≈ 0/1, below `fs_start=134`), bypassing the NFTL
log2phy, while the VBR/FAT/data reads resolve correctly through the NFTL (logical 0→134,
logical 2→136). What is that raw read, and how does the root dir (logical 1 → physical 135)
actually enumerate?

**Verdict (Proven): the premise is a misattribution — there is no root-dir read that
bypasses the NFTL.** The FatLib volume owns no second medium: *every* FAT sector it
touches goes through the same partition object → `FUN_0800ea38` → NFTL submedium →
`FUN_0800f3ac` log2phy that `fs_partition_scan` used. The raw `row = 256·block + page` read
observed at "root-dir time" is **`codepage_load`** (`W:\codepage.bin`, the NLS conversion
tables) via **`FUN_08030da4`** — a **by-design raw physical read of the FHA system area
below `fs_start`** (FatLib paths are UTF-16; the fs layer converts every ANSI path through
the codepage *before any directory I/O*). It lands on block 0/1 when the codepage's
per-cluster **physical block map** is fed logical indices instead of physical blocks: the
resident `FHA_get_maplist` (`func_0x08000c40`) reads the maplist rows as
**`{u16 origin_physical, u16 backup_physical}`** pairs (§4), so a map row of the form
`{u16 logical, u16 physical}` makes the "origin block" the *logical index* 0,1,2,…. That
garbles the converted `"a:\…"` scan path, `File_Open`/opendir returns NULL, and the
root-dir sector (logical block 1 → physical 135) is then **never requested** (its only read
is the `mtd_init` spare scan — exactly the observed count of 1).

All addresses at unified runtime base `0x08009000`; nandboot-resident code at
`0x0800xxxx` (nandboot.bin offset = addr − 0x08000000). Claims are **Proven** (cited
decomp/disasm) unless marked **Inferred**.

---

## 1. Object model: ONE medium under the volume (Q1)

```
FatLib volume (FUN_08039ddc, 0x64 B, type 10)
   +0x08 ──▶ partition object A (flash_erase_region/Medium_CreatePartition 0x0803c784)
                +0x10 = read op  = FUN_0800ea38      (0x0803c9e0 constant)
                +0x14 = write op
                +0x20 ──▶ desc {medium, 0xffffffff clamp, base·f, count·f}
                             └─▶ FS-area medium (FUN_0803be74) — the NFTL submedium
                                   +0x10 read ──▶ FUN_0800f3ac dense-window/RLE log2phy
                                   +0x8c dense map (@0x08143380), +0x90 RLE, +0x6e start
```

The volume ctor `FUN_08039ddc@0x08039ddc` (called only from `fs_partition_scan`'s
MOUNT label and the format wrapper `FUN_08039d00`) stores the **same partition object**
it was scanned with — it never builds another medium. **Proven** (`0x08039ddc.c`,
`0x08039a68.c:53`).

### Volume struct (0x64 bytes, alloc'd `0x08039ddc.c:44`)

| off | field | set in |
|---|---|---|
| +0x00 | type byte = 10 | `:47` |
| +0x04 | dtor/vtable `DAT_0803a09c` | `:46` |
| +0x08 | **→ partition object A** (`param_1`) | `:51` |
| +0x0c | sector shift (copy of part+9, =9 → 512 B) | `:52` |
| +0x0e/+0x0f | cache-entry counts (data / FAT) | `:49,50` |
| +0x10/+0x2c | cache thresholds (`count<<7`) | `:61,62` |
| +0x14 | FAT-sector cache list (`FUN_080f4270` init) | `:87` |
| +0x30 | data-sector cache list | `:65` |
| +0x48 | `param_3` (=0) | `:54` |
| **+0x4c** | **base LBA** (`param_2`; **0 for superfloppy — correct**, MBR start-LBA otherwise) | `:48` |
| +0x50 | path separator `0x5c` `'\'` | `:55` |
| +0x54 | `param_5` (name-gen ctx) | `:58` |
| +0x5c | open-file list (`fs_list_init(…,0x20)` in `fs_partition_scan:58`) | |
| +0x60 | **→ FAT object** (BPB parse result) | `0x0803b078.c:76` |

### FAT object (0x4c bytes, type 0xb, built by `FUN_0803b078@0x0803b078`)

| off | field | source (BPB) |
|---|---|---|
| +0x08 | → volume | `:29` |
| +0x0c | reserved sectors (**FAT region start**) | @0x0e |
| +0x10 | FAT size (sectors) | @0x16 / @0x24 |
| +0x14 | type: 2=FAT16, 3=FAT32 | cluster-count rule |
| +0x15 | #FATs | @0x10 |
| +0x16 / +0x17 | log2 sec/cluster / log2 bytes/sector | @0x0d / @0x0b |
| +0x18 | root-dir sectors (`(rootents·32 + secsize−1)>>shift`) | @0x11 |
| **+0x1c** | **first root-dir sector** = reserved + #FATs·FATsz | derived |
| +0x20 | root cluster (2 for FAT16; @0x2c for FAT32) | |
| +0x24 | cluster count + 1; +0x28 FAT[0] mask; +0x2c/+0x30/+0x34…+0x40 derived shifts | |
| +0x48 | cluster-chain cache object | |

### Every FatLib sector touch goes through the partition op — Proven

- FAT-region read `FUN_0811a064@0x0811a064:40,51`:
  `abs = vol[+0x4c] + fatobj[+0xc] + n;  (**(*(vol+8)+0x10))(part, buf, abs, 1)`.
  Used by the BPB parse (FAT[0] check, `0x0803b078.c:77`) and the cluster-chain
  walkers (`FUN_080f6a30`, `FUN_080f6b88`, `FUN_080f5268`, `FUN_080f7e7c`).
- Root-dir / data-region: `abs = vol[+0x4c] + fatobj[+0x1c] + ((cluster−2)<<secperclus)`
  through the same `part+0x10/+0x14` ops (`0x0803b9cc.c:25-30,55`; `Fat_Format`
  writes its boot sector at `vol[+0x4c]`, `0x0803b310.c:188`).
- `FUN_0800ea38@0x0800ea38` then resolves through the NFTL submedium
  (`desc=*(A+0x20)`, `(**(*desc+0x10))(medium, buf, desc[2]+sector, 1)` →
  `FUN_0800f3ac` dense window → physical `fs_start + rel`) — see
  `nftl-layout.md` §3.

**Answer Q1: the mounted volume reads through the SAME NFTL submedium as
`fs_partition_scan`. There is no raw/mis-based volume medium, and `vol+0x4c = 0` is the
correct superfloppy base.** With the resolved dense map (0:0 1:1 2:2 …) a root-dir read
at LBA 260 *would* resolve to logical block 1 → physical **135**. It just never gets
issued (§3).

---

## 2. The resolving reads (Q3): MBR/VBR, FAT, data

Pre-mount and post-mount reads use the identical path; nothing structural changes at
mount:

| read | issuer | chain |
|---|---|---|
| sector 0 (MBR/VBR) | `fs_partition_scan:27,41` | `(**(A+0x10))` = `FUN_0800ea38` → medium+0x10 → `FUN_0800f3ac` → phys 134 |
| FAT sectors | `FUN_0811a064` (cache) | same, `abs = 0 + reserved + n` (logical block 0) → phys 134 |
| dir/data clusters | cluster helpers via `fatobj[+0x1c]` | same → logical 0/2 → phys 134/136 |

This matches the dynamic counts (134: 32 reads, 136: 10). The 135 count of 1 is the
`mtd_init` spare-tag scan (`nftl-layout.md` §3), not FatLib.

---

## 3. The "bypassing" read (Q2): it is the codepage, and raw is by design

### 3.1 Who reads before the root dir — the ANSI→UTF-16 path conversion

FatLib handles all names as UTF-16. Every scan/open converts its ANSI path first:

- `discovery_ctx_build@0x080ae2c0:28-36` converts `"a:/oidfilelist.lst"`
  (@0x080ae394) and the scan-root string via `codepage_convert_devpath@0x08031aac`,
  then `discovery_scan_wrapper@0x080afa80` → `udisk_dir_scan_recurse@0x080af6dc`.
- `Fwl_GetRootDir` = `FUN_080addb4:20` converts the static `":\"` (@0x080adeb4) the
  same way. Same pattern in `FUN_08031fe4`, `0x080ae6c4`, `0x080afcf0`, `0x080ef8b8`.

`codepage_convert_devpath` → `FUN_0803193c@0x0803193c` (MultiByteToWideChar analog):

- `FUN_080310c4()` returns the codepage **ready flag** = byte 0 of the NLS state
  struct **`0x081db730`** (`DAT_08030f58`). Flag **0** ⇒ trivial byte→u16 zero-extend
  copy (`:29-43`) — no NAND touched. Flag **1** ⇒ real conversion
  `FUN_0803136c@0x0803136c` → per-char table lookups `FUN_08031044/54/64`, each of
  which calls **`codepage_load`**. **Proven.**

### 3.2 `codepage_load@0x08030e4c` → `FUN_08030da4` — the raw read

```
cluster = offset >> state[+4];                    // state = 0x081db730
block   = *(u16*)(state + 6 + cluster*2);         // per-cluster PHYSICAL block map
page    = (offset & (1<<state[+4])-1) >> state[+2];
FUN_08030da4(block, page, n, buf, "W:/codepage.bin")
   row = *(0x08008cc4+0x14)·block + page          // = 256·block + page  (0x08030da4.c:14)
   func_0x0800260c(0, row, size, &desc, 0)        // NB_READ_DATA, raw physical row
```

`FUN_08030da4` is called **only** by `codepage_load` (grep: sole xref). It reads by
**physical row**, no NFTL resolver — **correct by design**: `codepage.bin` is an FHA
"BIN" in the linear system area below `fs_start` (blocks 36..42), where
`phys = start + logical` (nftl-layout §3a). The
sector cache is 512 B @0x080089e0 (`FUN_08031044:19` masks the offset with 0x1ff).
**Proven.**

### 3.3 Where `block ≈ 0` comes from — the maplist format mismatch

The per-cluster block map at `state+6` is seeded once, in
`boot_read_serial_and_codepage@0x0803a3b0` (called from `app_init_main` right after
`fs_storage_mount_init`):

```
FUN_0803a9f0(state+2, state+4, 1)                 // shifts from dev 0x08008cc4 geometry
ok = func_0x08000c40("codepage", state+6, 8)      // resident FHA_get_maplist
ok &&= codepage_get_header(sel)                   // codepage_load(sel<<5) — first raw read
if both ok: state[0] = 1                          // READY flag
```

(`"codepage"` @0x08031ae8; errors `"get codepage maplist errior"`,
`"code page init error"`. **Proven**, literals verified in PROG.bin.)

**`func_0x08000c40` (nandboot 0xc40) disassembled — this is `FHA_get_maplist`:**

1. `FUN_08000b20(0x1000)` scratch; **`nftl_load_info@0x080009c8(name, buf)`**: reads
   row `dev[0x14]−2` (=254, header) and row `−3` (=253, entry table) via NB_READ_CP
   `0x08002a38`, strcmp-matches the entry name @entry+0x14 (`0x8003044`), then reads
   **row = entry word[8]** (the maplist row, = our `nftl_info_row` rows **200+i**)
   into buf and returns entry word[0] = size-in-sectors. (disasm 0x80009c8–0x8000a98)
2. `nclusters = ((size−1) >> log2((1<<G0[0x8d1])·dev[0x14])) + 1`; bails (return 0) if
   `> max` (=8). `G0 = 0x08007f8c` (literal @0x8000c20), `dev = 0x08008cc4`.
3. `FUN_080002b8(0, bmp, 0x200)` — ASA bad-block bitmap; failure ⇒ fatal
   `FUN_080009ac(2)`.
4. **Copy loop (0x8000cf8-0x8000d6c): `dst[i] = *(u16*)(buf + i*4)` — the FIRST u16
   of each 4-byte entry is taken as the cluster's ORIGIN PHYSICAL BLOCK**; if its
   bad-block bit is set, `dst[i] = *(u16*)(buf + i*4 + 2)` — the SECOND u16 is the
   **BACKUP block** (both bad ⇒ fatal spin).

**Cross-check `func_0x08000d90` (nandboot 0xd90) — the codepage RECOVERY lookup**
(called by `codepage_load` on a failed read): same `nftl_load_info`, then scans
entries: `if (*(u16*)(buf+i*4) == failed_block) → take *(u16*)(buf+i*4+2)` (origin →
backup); if the *second* u16 matches it fatals (a backup block failed). It then
re-reads/repairs. So both consumers agree: **maplist entry =
`{u16 origin_physical, u16 backup_physical}` per cluster** — the FHA RB/UB
"written-twice" pairing (`nftl-library-identification.md`: `FHA_get_maplist(...,
T_U16 *map_data,..., bBackup)`). **Proven.**

**The failure mode:** if rows 200+i are populated as `{logical:u16, physical:u16}`
identity pairs `{j, phys_start+j}` rather than the true `{origin_physical,
backup_physical}` (§4), `FHA_get_maplist` returns `dst = [0, 1, 2, …]` — the *logical
indices* — as the codepage's physical blocks. The first `codepage_load` (header, offset
`sel<<5` → cluster 0) then does `FUN_08030da4(block=0, page≈0)` → **raw rows 0..~511 =
physical blocks 0/1**, well below the system-area content, which read back `0xff`. That is
the observed "root-dir read to raw block 0/1". **Proven mechanism** (the exact observed row
was confirmed dynamically).

### 3.4 Why the dir object is NULL and block 135 is never read

The 0xff "success" reads leave the cached codepage header all-0xff but set the READY
flag, so conversions take the real (non-fallback) path with garbage tables
(bytes-per-char 0xff ⇒ DBCS branch; table pointers 0xffffffff ⇒ garbage cluster
indices ⇒ more raw garbage reads ⇒ garbage/empty UTF-16 output). Then, in
`udisk_dir_scan_recurse:45` → opendir `FUN_080fb8ac@0x080fb8ac`:

- empty converted string ⇒ return NULL at `:19`;
- else `File_Open@0x080f8f20`: with `w[1]==':'` the drive check
  `toupper(w[0]) − 0x41 ≥ drive_count` (`0x080f9024-0x080f9073`) rejects a garbled
  letter ⇒ **return 0**; a garbled `':'` falls into the component walk
  `FUN_080f61b0` with garbage names ⇒ 0xff ⇒ return 0.

opendir NULL ⇒ `udisk_dir_scan_recurse` returns without one `readdir`
(`FUN_080f3fb8`) ⇒ **no root-dir sector is ever requested from the volume** ⇒
physical 135's only read stays the `mtd_init` scan. (The scan wrapper's second pass
force-writes `w[0]='A'` (`0x080afa80.c:25`) but the rest of the string is still
garbled.) **Proven** (paths); which exact reject fires is run-dependent (**Inferred**).

**Answer Q2/Q3: nothing in the FatLib picks a different vtable slot or medium — the
"bypass" read is a different subsystem (NLS/FHA), raw by design, keyed by a wrong
physical-block map.**

---

## 4. The FHA maplist row format (Q4)

The system-area bins (PROG, codepage) are addressed through the **FHA maplist**
(`nftl_info_row`, rows 200+i), not through the NFTL dense window. Each 4-byte entry is
**`{u16 origin_physical_block, u16 backup_physical_block}`** for cluster j of the bin —
not `{logical, physical}`. For the two named bins (`PROG` @blk 8, `codepage` @blk 36) with
1-block clusters those are `{8+j, backup}` and `{36+j, backup}`.

- *Backup value:* the producer writes every bin **twice** (RB/UB pairing), so the backup
  is a real second copy placed elsewhere in the system area. `FHA_get_maplist` only
  consults it when the origin's bad-block bit is set, and `func_0x08000d90` checks origin
  before backup within an entry, so a healthy read never dereferences the backup. (A `0`
  backup is unsafe: a recovery would then raw-read block 0.)
- *Cluster granularity:* nandboot uses `(1 << G0[0x8d1])·256` sectors/cluster
  (`G0=0x08007f8c`, SPL-seeded); `codepage_load` uses `2^state[+4]` bytes with
  `state[+4] = log2(min(dev[0x18]·dev[0x1c],0x1000)·256)` (`FUN_0803a9f0`). The 512-B
  sector cache and the row math (`row = 256·block + page`, page in `2^state[+2]`-byte
  units) are consistent with **unit 512 B ⇒ cluster = 1 block (128 KiB) ⇒ codepage = 7
  clusters** (**Inferred, strong**; larger seeded shifts would give 2- or 4-block clusters,
  i.e. one maplist entry per cluster starting at the cluster's first block).

The system-area reads (`FUN_08030da4` / `func_0x0800260c`, and the recovery path for PROG
rows) hit the **raw NB_READ_DATA seam** with `codepage.bin`/PROG content at blocks 8..42 —
they do **not** pass through the NFTL/FS-area (≥134) path. The ASA bad-block bitmap read
`FUN_080002b8(0,·,0x200)` supplies the all-good bitmap.

The full boot chain: `boot_read_serial_and_codepage` caches the codepage header →
`codepage_convert_devpath` converts `"a:/…"` identically (ASCII is identity in every table
incl. default cp936) → `File_Open` matches the drive letter → opendir/readdir issue
root-dir sector reads at LBA 260 through `vol(+0x4c=0)` → `FUN_0800ea38` → NFTL dense map
`[1]=1` → **physical block 135 sector 4 → "TIPTOI" label + `.gme` enumerated.**

---

## 5. Answers in one table

| Q | answer | status |
|---|---|---|
| 1 volume vs medium | Same NFTL submedium; volume adds only base `+0x4c` (=0, correct) and caches; struct in §1 | Proven |
| 2 root-dir read path | Never issued; the raw read is `codepage_load`→`FUN_08030da4` (NLS/FHA, raw-by-design), block from the mis-formatted maplist ⇒ 0/1 | Proven (mechanism) |
| 3 resolving reads | All FatLib I/O: `vol[+0x4c]+region+n` → `(**(part+0x10))` = `FUN_0800ea38` → `FUN_0800f3ac` → phys 134/136 | Proven |
| 4 authentic state | Maplist rows as `{origin_phys, backup_phys}` pairs + serve system-area raw rows (§4) | Proven format; cluster size Inferred |

## Key code citations

| function | addr | role |
|---|---|---|
| `FUN_08039ddc` | 0x08039ddc | volume ctor; struct §1; base @+0x4c |
| `FUN_0803b078` | 0x0803b078 | BPB parse → FAT object §1 |
| `FUN_0811a064` / `FUN_0803b9cc` | 0x0811a064 / 0x0803b9cc | FAT-region / cluster-region sector I/O via `(**(vol[8]+0x10))` |
| `FUN_0800ea38` / `FUN_0800f3ac` | 0x0800ea38 / 0x0800f3ac | partition read op → NFTL log2phy (resolving path) |
| `codepage_convert_devpath` / `FUN_0803193c` / `FUN_0803136c` | 0x08031aac / 0x0803193c / 0x0803136c | ANSI→UTF-16; ready-flag gate `FUN_080310c4` (byte 0 @0x081db730) |
| `codepage_load` / `FUN_08030da4` | 0x08030e4c / 0x08030da4 | cached 512-B sector read of `W:\codepage.bin`; **raw row = 256·block+page**, dev 0x08008cc4 |
| `boot_read_serial_and_codepage` | 0x0803a3b0 | seeds shifts + maplist (state+6) + header; sets READY |
| `func_0x08000c40` | 0x08000c40 (nandboot 0xc40) | **FHA_get_maplist**: maplist row → per-cluster `{origin,backup}` u16 pairs; ASA bad-bit fallback |
| `func_0x08000d90` | 0x08000d90 (nandboot 0xd90) | recovery: origin→backup lookup (same pair format) |
| `nftl_load_info` (nandboot) | 0x080009c8 | rows 254/253 → name match → maplist row (entry word[8]) |
| `discovery_ctx_build` / `discovery_scan_wrapper` / `udisk_dir_scan_recurse` | 0x080ae2c0 / 0x080afa80 / 0x080af6dc | scan chain; opendir `FUN_080fb8ac` → `File_Open@0x080f8f20` drive check 0x080f9024 |
| strings | 0x08031ae8 `"codepage"`; 0x08031af4 `"get codepage maplist errior"`; 0x080ae394 `"a:/oidfilelist.lst"`; 0x080adeb4 `":\"` |
