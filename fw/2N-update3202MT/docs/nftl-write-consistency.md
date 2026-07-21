# NFTL write / copy-on-write / fold — the write side (2N / update3202MT)

Static decomp analysis of the **Anyka NFTL write path** for the FS-area (udisk) medium:
what happens when the firmware writes a FAT sector (e.g. `a:/oidfilelist.lst`), how
copy-on-write and chain folding relocate data, and the exact spare tags programmed. This
is the **write-side** companion to [`nftl-layout.md`](nftl-layout.md) (read side,
geometry, resolvers, tag format).

**Conventions.** Unified runtime base `0x08009000`. Claims are **Proven** (traced in the
decomp; constants read from `PROG.bin`) or **Inferred**. "part" = the per-group NFTL
partition object; "chip" = `part+0x94`; "dev" = `chip+0x54`; `w = part[+0x64]` = chain
width = **1** on the pen; "rel-block" = block relative to the group (`part[+0x6e]` =
StartBlock = fs_start = 134); "page" = one NFTL page = `chip[+0x18]·chip[+0x1c]` = 2 KiB
= 4 sectors; block = 64 pages = 256 sectors; "L" = logical rel-block; "H" = old physical
head block; "N" = freshly allocated block; "F" = fold target.

---

## 0. Roles of the core routines (Proven)

Several routines have names that suggest one role but do another:

1. **`nftl_core_rw_sector@0x08043a74` is the chain FOLD / relocate** (garbage-collect
   one chain), not a per-sector R/W core: `nftl_core_rw_sector(part, phys_head_block,
   ctx)`. `param_2` is a **physical rel-block** (a chain head); `ctx` is `0xffff` or an
   **endangered block number** to exclude from reuse — never a data buffer.
2. **`mtd_rw_sector_dispatch@0x08044110`** (and clone `0x08044218`) is "**fold the
   longest chain**": `FUN_08048308` finds the longest RLE run (length ≥ 3), reverse-looks
   up its logical block, folds it. Called only when the free pool is low.
3. **`nftl_rw_multi_sector@0x0800f5f4` is the multi-sector READ core** (calls read ops
   `+0x250`/`+0x258`, triggers a fold on danger codes).
4. The **actual write core** is `FUN_08049448@0x08049448` (NFTL page write), reached from
   the medium write op `FUN_080495f0@0x080495f0` (medium vtable `+0x14`).

---

## 1. The real write stack, top → bottom — Proven

```
FatLib fs_write (512-B logical sectors on partition A)
  ▼ partition write wrapper (A+0x14)  FUN_0804a060 @0x0804a060
      bounds vs A[+0xc]; buf-cache write-through FUN_08049dcc
  ▼ MEDIUM write op (medium+0x14)     FUN_080495f0 @0x080495f0
      unit conv: page = pagesize>>9 = 4 sectors; per-page READ-MODIFY-WRITE cache:
        - partial page: FUN_0800e6b0 reads the whole 2-KiB page into medium[+8],
          medium[+4]=page#, dirty medium[+2]=1; merge caller bytes in RAM
        - full aligned page: straight to FUN_08049448
        - dirty-page flush: FUN_080494ec (on page switch) / medium+0x1c flush op
          FUN_08042574 (FatLib sync) → FUN_08049448(medium, medium[+8], page)
      (medium READ op FUN_0800e760 (+0x10) serves the dirty cache page first —
       a just-written sector reads back from RAM until the page is flushed)
  ▼ NFTL page write                    FUN_08049448 @0x08049448
      L = addr/(chip[+0x14]·w), p = page-in-block, c = chain idx (=0)
      TRIM check → resolve → append-or-COW → program → map update  (§3)
  ▼ per-page program op (part+0x24c)   nftl_write_op FUN_0801027c @0x0801027c
      bounds (rel_block ≤ part[+0x70]); resolver part+0x26c (linear FUN_0800fef0:
      abs = part[+0x6e] + rel_block); write-pointer bookkeeping FUN_08010248
      (skipped for metadata tags [2:4]==0xfffa); die-split FUN_0800feac
  ▼ device program leaf (dev+0x20)     FUN_0800fcc4 @0x0800fcc4
  ▼ nandboot PAGE-PROGRAM              0x08002044   (row formula identical to the
                                        read leaf 0x0800fb00 / 0x0800260c)
```

The block-copy path additionally uses **op `part+0x260` = `FUN_080444f4@0x080444f4` →
dev+0x34 = `FUN_0803d868@0x0803d868` → nandboot COPY-BACK op `0x080023bc`**
(`func(plane, dev[+0x14]·src_blk + page, dev[+0x14]·dst_blk + page)`) — an in-flash
data+spare copy that never passes through `0x08002044`/`0x0800260c` (§3.4).

### 1.1 The device program leaf `FUN_0800fcc4` (dev+0x20) — Proven

Byte-for-byte the same prologue as the read leaf `0x0800fb00`:

```
row = dev[+0x14]·phys_block + page                 ; dev[+0x14]=0x100 (256 sec/blk)
if (0x081226f8[0] != 0x081226f8[1] && 0x081226f8[0] <= block) block += 0x081226f8[1]  ; gap-skip
FUN_0800fe08(&secdesc, plane)                       ; sector size 512/1024
FUN_0800fde4(&tagdesc, tagbuf, taglen)              ; 8-B NFTL tag descriptor {tagbuf,taglen}
if (FUN_0800fe40(plane) == 0)  ECC-ON  path         ; else raw path
    ret = func_0x08002044(databuf, row, 0, &secdesc, &tagdesc)   ; PROGRAM
return (ret == 0)                                   ; leaf returns bool ok
```

Program op call convention (from the `0x0800fcc4` call site, Proven): `r0` = data
buffer, `r1` = `row` (512-B units) = `dev[+0x14]·block + page`, `r2` = 0, `r3` =
`&secdesc`, `sp[0]` = `&tagdesc` = `{tagbuf, taglen=8}`. Same argument shape as the
reader `0x0800260c` — the program op is its write twin. There is also a write-data+tag
leaf `FUN_0800f92c` (dev+0x28), a single-shot page+tag program used for metadata pages,
also calling `0x08002044`.

### 1.2 nandboot PAGE-PROGRAM `0x08002044` — Proven (disasm)

Copies the caller's data *into* the NFC staging SRAM `0x08006800`, then issues a program
and polls status:

1. ECC gate: `bic [0x0404A000+0x158], #0x4000`.
2. Command setup: internal opcode **`0x63`** (`mov r2,#0x63; orr r1,r2,r1,lsl#11`), then
   a per-sector program command `(page<<7)|(col<<21)|0x100000|0x15`.
3. Data staging (write direction): per 512-B sector, copy caller→SRAM (plane-dependent
   interleave), then the data. (Contrast the reader `0x0800260c`, which copies SRAM→caller
   with the same helpers in the opposite argument order.)
4. Program-confirm + completion: issue, spin on RB-ready (`0x08002db4`) until non-zero,
   then read status (`0x08002efc`, NFC cmd `0x70`, `[+0x150]&0xff` = NAND status): `tst
   #0x80` ready → `tst #0x40` pass → `tst #1` set = **program FAIL** (`"NF1:…"` →
   `0x08005b00`).

NFC MMIO: command/status block `0x0404A000` (`+0x100..+0x160`), ECC control `+0x158`
(bit `0x4000`), status read `+0x150` (`&0xff`), staging SRAM `0x08006800`, plane byte
`0x08008ca8+4`. See [`nfc-controller-registers.md`](nfc-controller-registers.md).

### 1.3 Erase leaf `FUN_0803d76c` (dev+0x3c) — Proven

`row = dev[+0xc]·dev[+0x10]·block + page`, `bl 0x080007d8` (nandboot block-erase op),
then sets the ASA/bad-block bitmap bit. NFTL erase/mark op is `nftl+0x25c`
(`0x08044628`) → `nand_EraseBlk@0x08042d34`, which erases and marks-bad-on-failure.
nandboot op summary: program `0x08002044`, read `0x0800260c`, OOB read `0x08002af8`,
status `0x08002efc`, RB-ready `0x08002db4`, erase `0x080007d8`.

---

## 2. The structures a write mutates (all firmware RAM, rebuilt each mount) — Proven

| where | what |
|---|---|
| `part+0x90` (0x280 B) | **chain/RLE region**: a packed array of u16 *runs*; run-length bytes stored from `+0x90+0x27f` downward (`0xff` = end). One run per *resolved* logical block = its chain of physical rel-blocks, newest → oldest, plus a trailing `0xfffd`. Empty after `mtd_init`; a run is inserted lazily on first resolve and edited on every write/fold. |
| `part+0x7c` | parallel byte array: per RLE entry the block's **write pointer** = highest programmed page (`0xff` = unknown). Maintained by `nftl_write_op` on every non-metadata program (`FUN_08010248`) and lazily discovered by a top-down blank-tag scan (`FUN_0804a5ac@0x0804a5ac`). |
| `part+0x8c` + windows `part+0x48+i·2` | **dense log2phy window** (4×16 u16): logical rel-block → physical **head** block. Fault-in/evict `FUN_0800ee5c@0x0800ee5c`. |
| metadata block `info[+4]`, dir `part+0xe` | **persisted map** (mode-3 region) + free-run pages (mode 0) — rebuilt and rewritten on every `mtd_init` (see [`nftl-layout.md`](nftl-layout.md) §6). |
| `part+0x98/0x9a`, `part+0x11a/0x11c` | **free pool**: scan-time free runs + runtime freed-list (blocks erased by folds/trim); free count `part+0x58`; allocator `FUN_0804a20c@0x0804a20c`. |
| `part+0x1a0` (× `part+0x74`) | **pending-TRIM runs** of logical blocks (`FUN_080498b8@0x080498b8`; consumed at write time by `FUN_0804af08`). |

**Persistent source of truth = the spare tags** (chain topology), not any of the above:
`mtd_init@0x08045e80` re-derives everything — a block is a **head** iff it is used and no
other block's tag`[2:4]` points at it, then `nftl_build_log2phy@0x08047510` sets
`map[tag[4:6] & 0x7fff] = block` for heads only; two heads claiming the same logical are
arbitrated by chain length (`FUN_08044b04@0x08044b04`, longer wins; the loser chain is
freed).

---

## 3. The write sequence for one FAT sector — Proven (`FUN_08049448`)

A 512-B sector write arrives as a **2-KiB page program** (after the medium RMW, §1) for
`(L, p)`. Ordered steps:

1. **TRIM consume** — `FUN_0804af08(part, L)`: if L lies in a pending-trim run, split the
   run, discard L's old chain (erase each block, free them, remove the RLE run) and set
   the window entry to `0xffff`. Normally a no-op.
2. **Resolve** — `nftl_read_resolve(part, L)` → old head `H` (faults the window in; on
   first touch chain-walks the spare tags and inserts the RLE run).
3. **Pre-fold** — `FUN_0804a8a8(part, H)`: if the RLE run length > **10**, fold the chain
   first (§3.4) and re-resolve.
4. **Placement decision** — `FUN_0804a68c(part, L, p, c, 1, …)`: walk the chain
   newest→oldest; a block qualifies for **append** iff its write pointer `< p`, or the
   pointer is unknown (`0xff`) and a top-down spare scan (`FUN_0804a5ac`) shows page `p`
   (and everything above the last programmed page) still blank. Then:

   **(a) APPEND (`ret ≥ 1`) — program page `p` into the existing chain block** `B`:
   - tag = `{[0]=0x00, [1]=0, [2:4]=chain-next of B, [4:6]=L, [6:8]=0x0001}`;
   - one program op; `nftl_write_op` bumps B's write pointer to `p`;
   - **no mapping change**.

   **(b) COPY-ON-WRITE (`ret == 0`) — every chain block already has page `p`:**
   1. **allocate** `N` = `FUN_0804877c(part, …, 1)` → (if free count `part[+0x58]` < 0x10
      → reclaim trimmed blocks, then if still short **fold the longest chain**, log
      `"FreeBlock %d"`) → `FUN_0804a20c` pops the next free-run block (assumed erased).
   2. read H's page-0 tag for the old seq.
   3. build the page tag: `[0] = (old_seq + 1) & 0x7f`, `[1] = 0`, `[2:4] = H` (the link
      that makes N the new head) or `0xfffd` if L had no head, `[4:6] = L`
      (`| 0x8000` only when there was no old head), `[6:8] = part[+0x68] = 0xffff`.
   4. **page-0 stamp**: if `p > 0`, first program page 0 of N with the same tag but
      `[0] |= 0x80` (obsolete marker) so a future scan can identify N.
   5. **program page `p` of N** with the real data + tag. On program failure: log `"NFTL
      it fails to WS B:%x P:%x"`, power-probe `FUN_08043190`, mark N bad, restart at (1).
   6. `FUN_0800ee5c(part, L)` — window fault-in, then **`nftl_update_log2phy(part, L,
      N)`** — dense window entry `L → N`, dirty flag.
   7. **`FUN_08042be8(part, N, H)`** — RLE update: insert N **before** H (or create a new
      run `[N, 0xfffd]` when L had no head).
   8. `FUN_08010248(part, N, p)` — write pointer of N = `p`.

   **NO erase happens on a COW write** — H keeps its data and stays in the chain as the
   source for all pages ≠ p. A logical block's content is therefore **split across the
   chain**; the read path (`FUN_0800f488`) walks newest→oldest and takes each page from
   the first block whose write pointer covers it with a non-obsolete tag (`tag[0]&0x80 ==
   0`; single-block tails accepted without a tag check via the `next==0xfffd` shortcut).

### 3.4 FOLD (chain merge) — `nftl_core_rw_sector(part, head, ctx)` — Proven

Triggered by: chain length > 10 (§3.3); free pool low; read-path danger codes; scan
repair. Sequence:

- **a.** allocate fresh `F`; if `F` equals the metadata block `info[+4]` → discard and
  restart.
- **b.** for each page `q` = 0..`chip[+0x14]`−1: chain-walk from the head; the source is
  the first block whose write pointer ≥ `q` (or unknown) and whose page-q tag reads OK
  with `seq&0x80 == 0`. Page 0 of the head supplies `L = tag[4:6]&0x7fff` and
  `base_seq = tag[0]&0x7f`.
- **c.** copy page q → F: if source and F are in the same zone and the source is not the
  chain tail (`FUN_08043700`), use the **HW copy-back** op `part+0x260` (`FUN_080444f4` →
  dev+0x34 `FUN_0803d868` → nandboot `0x080023bc`, copies data+spare in-flash). Otherwise
  read the page (`+0x250`) and program it into F with tag `{[0]=(base_seq+q_idx+6)&0x7f,
  [1]=idx, [2:4]=0xfffd, [4:6]=L|0x8000, [6:8]=part[+0x68]}`. **Blank pages** (no source):
  tag `[0]|=0x80` and — with `chip[+4]&0x40000` clear — the page is skipped except page 0
  (always programmed, obsolete tag). Program failure → mark F bad, restart at (a).
- **d.** `FUN_0800ee5c(part, L)`, then `nftl_update_log2phy(part, L, F)`.
- **e.** `nftl_rle_compact@0x080438a4`: for every old chain block **not** on the danger
  list: **ERASE NOW** (`FUN_080432d4` → `nand_EraseBlk` → dev+0x3c → `0x080007d8`),
  collect; danger-listed / erase-failed blocks are **marked bad** instead. Remove the run
  from `+0x90`/`+0x7c`; `FUN_08042ec0` merges the erased blocks into the runtime
  free-runs.
- **f.** `FUN_08042be8(part, F, 0xfffd)` — new single-entry run; `FUN_08010248(part, F,
  lastpage)`.

After a fold, L maps to the single fresh block F (tag `[2:4]=0xfffd`, `[4:6]=L|0x8000`)
and **all old chain blocks — including any originally pre-placed block — are erased**.

### 3.5 Hot vs cold blocks: why the root dir relocates most — Proven mechanics

On the superfloppy FAT16 udisk (4 reserved, 2 FATs × 128 sectors, 512 root entries), the
FAT structures map onto NFTL logical blocks as (Proven from the geometry + observed row
reads):

| FAT structure | LBA | NFTL logical block | page-in-block |
|---|---|---|---|
| VBR | 0 | **0** | 0 |
| FAT1 | 4–131 | **0** | 1–32 |
| FAT2 | 132–259 | **0** / **1** | 33–63 / 0 |
| **root directory** | **260–291** | **1** | **1–8** |
| data clusters (cluster 2 …) | 292… | 1, 2, … | 9–63, … |
| a subdirectory (e.g. `VOIMG/`) dir cluster | one 4-sector cluster ≥ 292 | some data block | one page |

The root dir shares **logical block 1** with the FAT2 tail and the first data clusters;
logical block 0 holds VBR+FATs. Every file create/close bounces the medium's single
dirty-page cache between the dir page (L1 p1), FAT pages (L0), and data pages — each
switch flushes a full 2-KiB page (`FUN_080495f0`). Because page 1 of L1 is already
programmed in every chain member, each dir-page flush is a fresh **COW relocate**. A boot
doing several create+write+close cycles flushes the dir page 10+ times → chain length >
10 → **fold** (`FUN_0804a8a8`). A cold, never-rewritten cluster (e.g. a subdir enumerated
but not written) has a short chain that never folds and reads end at the placed block via
the tail shortcut (`FUN_0800f488`, successor == `0xfffd` accepted without a tag/wp
check). So the meaningful asymmetry is **HOT (rewritten, folded) vs COLD (short chain,
placed block still the chain tail)** — not "root vs subdir" per se.

### 3.6 The read-modify-write read, and multi-COW tracking — Proven

`FUN_080495f0` does a read-modify-write for any partial-page write: `FUN_0800e6b0` reads
the whole current 2-KiB page through the NFTL read core (`FUN_0800f488`'s newest→oldest
chain walk) before merging. The walk (per chain member): tail shortcut (`next==0xfffd` ⇒
accept), else wp gate (accept if wp unknown or `page ≤ wp`) then tag gate (`tag[0]&0x80`
⇒ obsolete ⇒ fall through to the older member). The whole COW design leans on one flash
fact: **a never-programmed page reads an all-`0xff` tag, and `0xff` has bit 7 set ⇒
"obsolete" ⇒ the walk falls through.** The wp-probe (`FUN_0804a5ac`) uses the same fact
from the other side (scan tags top-down while `tag[4:6]==0xffff`).

The firmware's log2phy tracking is **sound**: `FUN_08049448` updates the dense window on
**every** relocate and re-resolves before returning:

```
if (COW, not append) {
    FUN_0800ee5c(nftl, L);              // window fault-in (evict+reload if needed)
    nftl_update_log2phy(nftl, L, N);    // dense window entry L -> NEW head, dirty flag
    FUN_08042be8(nftl, N, oldhead);     // RLE: prepend N before H
    FUN_08010248(nftl, N, page);        // write pointer of N = p
}
nftl_read_resolve(nftl, L);             // re-resolve before returning
```

So a second/nth rewrite of L always resolves through the just-updated window; the
evicted-window persist/reload is directory-directed (`part+0xe`) and cannot go stale.

---

## 4. Does a "linear" udisk write relocate?

**Yes — every write that cannot append into still-erased pages relocates.** The linear
`+0x26c` resolver (`FUN_0800fef0`) is only the *last* translation step (partition-relative
block → absolute die block, `abs = part[+0x6e] + rel`); it does **not** make data writes
in-place, and there is **no erase-then-reprogram-same-block path in the data write code**.
The identity `logical L ↔ physical rel-block L` holds only until the first COW write to L;
from then on L's head is a block from the free pool (typically far beyond the placed FAT
image), the dense window is updated, and the spare-tag chain records the relocation for
the next scan. Allocation is a simple run cursor (`FUN_0804a20c`) — no write-pointer wear
rotation beyond run order.

---

## 5. log2phy update details — Proven

- `nftl_update_log2phy@0x08042c58` is **RAM-only and immediate**: find the window
  containing L (else LRU-bump / fatal `"it fails at update logtophy"` past 4 windows),
  set dirty flag `part+6+i`, write the new physical into
  `part[+0x8c][(L − win_base) + i·0x10]`.
- The **persisted** map (metadata mode-3 region) is written only on window eviction
  (`FUN_0800ee5c` → `nand_mtd_3(part, base·2, 0x20, window, 3)`), at scan end
  (`nftl_build_log2phy` → `nand_mtd_3(part, 0, span·2, map, 3)`), and by the TRIM path.
  Metadata pages carry tag `[2:4]=0xfffa` and are erased+rebuilt on every mount — a cache,
  not the truth.
- The RLE region `+0x90` is pure RAM (never persisted); after a reboot, chains are
  re-walked from tags on first resolve.
- Reads resolve **only** through the dense window (+ RLE); the `+0x26c` resolver is below
  that.

---

## 6. Exact spare tags programmed — Proven

8-byte tag per page program (`taglen 8` at every op). `part[+0x68] = 0xffff` (seeded from
`DAT_08044fdc` in `mtd_init`).

| write type | `[0]` seq | `[1]` | `[2:4]` | `[4:6]` | `[6:8]` |
|---|---|---|---|---|---|
| COW new head N, data page | `(old_seq+1)&0x7f` | 0 | **old head H**, or `0xfffd` if none | `L`, `\|0x8000` iff no old head | `0xffff` |
| COW new head N, page-0 stamp (p>0) | same `\| 0x80` | 0 | same | same | `0xffff` |
| APPEND into chain block B | `0x00` | 0 | B's chain successor | `L` | `0x0001` |
| FOLD target F, sourced page | `(base_seq+idx+6)&0x7f` | idx | `0xfffd` | `L \| 0x8000` | `0xffff` |
| FOLD target F, blank page (only page 0 programmed) | same `\| 0x80` | idx | `0xfffd` | `L \| 0x8000` | `0xffff` |
| metadata region page (`nand_mtd_3@0x08046ed8`) | region-specific | — | **`0xfffa`** | dir index info | `part[+0x68]+1 = 0` |

Scan-side consumption (what must round-trip): `[2:4]` `0xffff`=free, `0xfffa`=metadata
(erased at scan), `< 0x8000` = points at an older chain member (target loses head
status), own-block-number = corrupt (erased at scan); `[4:6]&0x7fff` = logical (head
inversion); `[0]&0x80` = page copy obsolete (fold skips it as a source); `[0]&0x7f` = seq
(display/ordering only — head selection is topological, §2).

---

## 7. Consistency invariant

The whole write/read model reduces to one physical-address invariant:

> *For every physical (block, page): a read returns the last programmed data+tag since
> the most recent erase; else erased (`0xff`) if the block was ever erased; else the
> factory content/tag; else blank — addressed by the row number the firmware's own leaves
> compute (`row = dev[+0x14]·block + page`, identical in the program op `0x08002044` and
> the read op `0x0800260c`).*

With that, the `.lst` write sequence (RMW page reads → COW program of N (+page-0 stamp) →
window update inside the firmware) reads back exactly, the old block H keeps serving the
untouched pages of L as the chain tail, folds erase-and-merge correctly, and a re-scan
reconstructs `map[L]=N` from the tags the firmware itself wrote. Two subtleties follow
directly and are worth stating because they are easy to get wrong when reasoning about a
NAND image:

- **Relocation** — a COW write programs a *different* physical block N (from the free
  pool) than the placed layout; anything reasoning by physical row (not by logical block)
  must key on the leaf-computed row, or a read of N sees content/tags for a *different*
  logical block and the chain walk resolves L to garbage.
- **Resurrection** — folds/TRIM **erase** pre-placed blocks (§3.4e). An erased block must
  read blank tags afterwards; if a stale non-blank tag survives for an erased block, the
  next `mtd_init` rescan sees **two heads for L** and arbitration corrupts the map. The
  erase must win over the factory layer.

The persistent truth is always the spare tags; every RAM structure (window, RLE, write
pointers, free pool, metadata regions) is rebuilt from them on the next mount. There is
no firmware state to reconstruct independently — it all flows through the same
program/read/erase seams, including the persisted map (written by `nand_mtd_3` as
ordinary page programs with tag `[2:4]=0xfffa`, read back via `nftl_init` mode 3).

---

## Key code citations (write side)

| function | addr | role |
|---|---|---|
| partition write wrapper (A+0x14) | `0x0804a060` | bounds, → medium+0x14 |
| medium write op (+0x14) | `0x080495f0` | page RMW cache; → `FUN_08049448` |
| medium read op (+0x10) | `0x0800e760` | serves dirty cache page first |
| dirty-page flush / medium flush (+0x1c) | `0x080494ec` / `0x08042574` | flush cache page |
| **NFTL page write** | `0x08049448` | trim→resolve→append/COW→program→map update |
| placement decision (append vs COW) | `0x0804a68c` | chain walk, write-pointer test; builds append tag |
| appendability probe (blank scan) | `0x0804a5ac` | top-down tag scan → write pointer |
| chain-length fold trigger (>10) | `0x0804a8a8` | → `nftl_core_rw_sector` |
| fresh-block front-end / GC gate | `0x0804877c` / `0x08043604` | low-free: reclaim trim, fold longest chain |
| free-run allocator | `0x0804a20c` | pops next free block (assumed erased) |
| free-list add/merge | `0x08042ec0` | runtime free runs `+0x11a/+0x11c` |
| **chain FOLD** | `0x08043a74` | merge chain → fresh block, erase old |
| fold-longest-chain GC | `0x08044110`/`0x08044218` | free-pool GC |
| multi-sector READ core | `0x0800f5f4` | read + danger-triggered fold |
| page-source pick on read | `0x0800f488` | newest-first, `seq&0x80` skip, `0xfffd` shortcut |
| RMW whole-page read | `0x0800e6b0` | read the 2-KiB page before merge |
| chain successor (RLE) | `0x0800f3cc` | `run[idx+1]` |
| RLE insert (prepend/new run) | `0x08042be8` → `0x08042a44` | `[N,H,…]` / `[N,0xfffd]` |
| RLE remove + erase old chain | `0x080438a4` (`nftl_rle_compact`) | erase via `0x080432d4`, free via `0x08042ec0` |
| erase one rel-block | `0x080432d4` | resolver → `nand_EraseBlk@0x08042d34` → `0x080007d8` |
| device program leaf (dev+0x20) | `0x0800fcc4` | `row=dev[+0x14]·blk+page`; → nandboot program `0x08002044` |
| device program+tag leaf (dev+0x28) | `0x0800f92c` | single-shot page+tag program → `0x08002044` |
| nandboot PAGE-PROGRAM | `0x08002044` | data→SRAM 0x08006800, cmd 0x63, program-confirm, status poll |
| block copy op (+0x260) | `0x080444f4` → dev+0x34 `0x0803d868` | **nandboot copy-back `0x080023bc`** (`r0=plane, r1=src_row, r2=dst_row`) |
| copy-back eligibility | `0x08043700` | same zone && src not tail |
| write-pointer set / get | `0x08010248` / `0x0800ee28` | `part+0x7c[rle_idx]` |
| dense-window update / fault | `0x08042c58` / `0x0800ee5c` | RAM map; evict persists mode 3 |
| TRIM: discard op / write-time consume | `0x080498b8` / `0x0804af08` | logical-range discard |
| scan: head inversion / conflict arbiter | `0x08047510` / `0x08044b04` | `map[tag46&0x7fff]=blk`; longer chain wins |
| erase leaf (dev+0x3c) | `0x0803d76c` | `row=dev[+0xc]·dev[+0x10]·blk+page`; → nandboot erase `0x080007d8` |
| power probe on program-fail | `0x08043190` | program+erase a scratch block |
