# Partition A: superfloppy FAT vs MBR — what the firmware actually expects

Question: does the firmware read an NFTL FAT partition (carved by
`fs_storage_mount_init`) as a **bare FAT volume** (VBR/BPB at partition sector 0,
"superfloppy") or as an **MBR-partitioned disk** (partition table at sector 0 → FAT
sub-partition, with FAT at LBA 2048)? (The analysis is drive-agnostic; note the
PC-exposed drive is B:, not A: — see [`ab-drive-layout.md`](ab-drive-layout.md).)

**Verdict (Proven): it accepts EITHER, but the firmware's *native* format — what it
writes itself when it formats A — is a superfloppy.** `fs_partition_scan` first tries
to interpret sector 0 as a FAT VBR (unless a heuristic says "looks like an MBR"), and
falls back to parsing it as an MBR with one FAT sub-partition. The firmware's own
`Fat_Format` writes a bare FAT boot sector at partition-relative sector 0 with **no
MBR**, and even explicitly zeroes the 4 bytes at offset 0x1c6 so the scanner's
MBR heuristic can never fire on it. On mount failure the firmware **auto-formats A as
a superfloppy**. That is why the real pen shows up as `sda` (no partition table).

All addresses at unified base `0x08009000`. Decomp:
`out/decomp/0x08039a68.c` (fs_partition_scan), `0x0803b078.c` (BPB mount parse),
`0x0803b310.c` (Fat_Format), `0x08039d00.c` (format wrapper), `0x08039ddc.c` (volume
ctor), `0x0803a484.c` (fs_storage_mount_init). Branches verified against the raw ARM
disassembly of `data/PROG.bin` (offsets cited below). Everything here is **Proven**
unless marked Inferred.

---

## 1. `fs_partition_scan @0x08039a68` — the sector-0 parse, instruction level

After allocating a `1 << part[9]` = 0x200-byte buffer and reading **partition-relative
sector 0** via the partition read op `(**(part+0x10))(part, buf, 0, 1)`
(= `FUN_0800ea38`, the partition→medium read wrapper; see
[`nftl-layout.md`](nftl-layout.md) §4):

```
0x8039ae0  mov  r2, #4
0x8039ae4  add  r1, r4, #0x1c6        ; &buf[0x1c6]
0x8039af0  bl   0x8003118             ; unaligned LE copy: lba0 = *(u32*)&buf[0x1c6]
0x8039afc  cmp  r0, #0x100
0x8039b00  movhi r0, #0 / strhi       ; if lba0 > 0x100:  lba0 := 0, goto FAT_CHECK
0x8039b08  bhi  0x8039b14             ;
0x8039b0c  cmp  r0, #0
0x8039b10  bne  0x8039b44             ; if 1 <= lba0 <= 0x100: goto MBR_PATH
                                      ; if lba0 == 0: fall into FAT_CHECK
```

**Note on the field width:** the read at `buf+0x1c6` is a **4-byte** (u32) read, not u16
(the decompiler dropped the length arg of the first `func_0x08003118` call; the disasm
shows `r2=4`). `buf+0x1c6` is byte 8 of MBR partition entry 0 (`0x1be + 8`) = its 32-bit
**start LBA**.

### FAT_CHECK (superfloppy attempt) — `LAB_08039b14`

```
memcmp(buf+0x52, "FAT", 3) == 0   → MOUNT(base = lba0 = 0)     ; FAT32 sig
memcmp(buf+0x36, "FAT", 3) == 0   → MOUNT(base = lba0 = 0)     ; FAT12/16 sig
else                              → fall through to MBR_PATH
```

The `"FAT"` string is at `0x0811c6a0` (via `DAT_08039c74`). Note the superfloppy path
checks **only** the `"FAT"` type label at 0x36 (FAT12/16) or 0x52 (FAT32) of sector 0 —
it does **not** check 0x55AA, the 0xEB/0xE9 jump byte, or anything else at this stage.

### MBR_PATH — `LAB_08039b44`

```
buf[0x1fe] == 0x55 && buf[0x1ff] == 0xAA          ; boot signature
buf[0x1c2] != 0                                   ; entry-0 TYPE byte (any nonzero, not
                                                  ;   restricted to FAT types 0x06/0x0B/0x0E)
lba = *(u32*)&buf[0x1c6]                          ; entry-0 start LBA (4-byte LE read)
(**(part+0x10))(part, buf, lba, 1)                ; SECOND read: the VBR at that LBA
memcmp(buf+0x52,"FAT",3)==0 || memcmp(buf+0x36,"FAT",3)==0
                                                  → MOUNT(base = lba)
else                                              → FAIL (free, error exit)
```

Only **entry 0** of the partition table is examined (offsets 0x1c2/0x1c6 are fixed);
entries 1–3 are ignored. The VBR read goes through the *same* partition read op, i.e.
`lba` is **partition-A-relative** (for our synthetic image: A-relative sector 2048 =
NFTL logical block 8 at 256 sec/block).

### MOUNT — `LAB_08039bcc`

`FUN_08039ddc(part, base, 0, secsize, …)` builds the volume object and stores
**`base` at vol+0x4c** (0 for superfloppy, the MBR start LBA otherwise); all further
FatLib sector I/O adds this base (e.g. `Fat_Format` writes its boot sector at
`vol+0x4c`, `0x0803b310.c:188`). Then `FUN_0803b078(vol, buf)` parses the BPB **from
the last-read sector** — sector 0 in the superfloppy case, the VBR in the MBR case.

### Dispatch summary (what discriminates MBR vs FAT)

| u32 @ buf[0x1c6] | order tried |
|---|---|
| `0` | FAT-at-sector-0 first → MBR fallback |
| `1 … 0x100` | **MBR only** (no superfloppy attempt; if MBR checks fail → scan fails) |
| `> 0x100` | FAT-at-sector-0 first → MBR fallback |

So the discriminator is a **heuristic on the would-be start-LBA field**, not the type
byte or the signature: a value in 1..256 is taken as "plausible small MBR start LBA ⇒
must be an MBR". In a genuine FAT VBR those bytes are boot-code/message bytes; the
firmware's own format zeroes them (§3), and zero-filled boot areas (mkfs.fat etc.) also
read 0 — landing in the FAT-first row.

## 2. What the BPB parse validates — `FUN_0803b078 @0x0803b078`

On the mounted boot sector (superfloppy sector 0 or MBR sub-partition VBR):

- `*(u16*)(buf+0x0b)` (bytes/sector) **must equal `1 << part[0xc]` = 0x200** — hard
  reject otherwise (`0x0803b078.c:30`).
- Reads sec/cluster @0x0d, reserved @0x0e, nFATs @0x10, root entries @0x11,
  totsec16 @0x13 (or totsec32 @0x20 if zero), FATsz16 @0x16 (or FATsz32 @0x24 if zero),
  root cluster @0x2c (FAT32).
- Cluster count = data sectors >> log2(sec/cluster):
  **`< 0xff5` (4085) ⇒ reject** (no FAT12 mount, `:59`);
  `0xff5 … 0xfff4` ⇒ FAT16; `≥ 0xfff5` ⇒ FAT32.
- Then reads FAT sector 0 (via `FUN_0811a064`) for the FAT[0] media entry.
- It does **not** re-check 0x55AA, the label string, or the jump byte here.

## 3. The firmware's own format writes a superfloppy — `Fat_Format` (`FUN_0803b310`)

`FUN_08039d00(part, start=0, count=part[0xc], 0x200, 0)` → volume with **base 0** →
`FUN_0803b310` builds the boot sector in a buffer and writes it at
**`vol+0x4c` = partition sector 0** (`0x0803b310.c:188`). What it writes:

- bzero whole sector (`FUN_08009cac(buf, 0x200)` — verified bzero, `0x08009cac.c`),
  then copies a **VBR template**: `EB 3C 90 "MSDOS5.0"` @0x0803cc42 for FAT16 or
  `EB 58 90 "MSWIN4.1"` @0x0803ca42 for FAT32 (literal pool 0x0803b9a4/9a8) — i.e. a
  **FAT boot sector, not an MBR**.
- Fills the BPB (0x0b/0x0d/0x0e/0x10/0x11/0x13 or 0x20, 0x16 or 0x24, FAT32 extras
  0x2c/0x30/0x32), drive# 0x80, boot sig 0x29, volume ID, `"NO NAME    "`, and
  `"FAT16   "`/`"FAT12   "` @0x36 or `"FAT32   "` @0x52 (strings @0x0811c8b4…),
  0x55AA @0x1fe (plus at secsize−2 for >512B sectors).
- **`FUN_08009cac(buf+0x1c6, 4)` (`:187`): explicitly zeroes the 4 bytes at 0x1c6** —
  precisely the field `fs_partition_scan` dispatches on, guaranteeing the next scan
  takes the FAT-first row of the table in §1. This is the firmware defending its own
  superfloppy against its own MBR heuristic — strong intent evidence.
- Then writes FSInfo (FAT32), zero-fills both FATs, writes FAT[0]/FAT[1] seeds
  (0xfffffff8/…). **No partition table is ever written anywhere in the firmware** (no
  writer of entries at 0x1be with type/start-LBA exists; `Fat_Format` is the only
  boot-sector writer — checked the `0x55AA`/format xrefs).

## 4. Mount flow auto-formats A as superfloppy on failure

`fs_storage_mount_init @0x0803a484` (`0x0803a484.c:107-127`):

```
A = flash_erase_region(medium, 0, cntA, 0x200, 1)
volA = fs_partition_scan(A, 0x200, 0)
if (volA == 0)
    volA = FUN_08039d00(A, 0, A->count, 0x200, 0)   ; ← FORMAT A, base 0 = superfloppy
    if still 0: infinite loop (hang)
register volA as drive 0 ('A')
volB = fs_partition_scan(B, …)                      ; B: no format fallback, error-flag only
```

Same format call from `fw_update @0x08051fd4` ("start format driver A") and from
`0x08051090` / `0x0803d0cc`. So on real hardware partition A's content *is* the output
of §3 — a superfloppy — which matches the real-pen observation (USB drive is `sda`
with FAT directly, no partition table).

## 5. Answers

1. **Sector-0 parse:** both, ordered by the u32 at buf+0x1c6 (table in §1). It parses
   the FAT BPB directly at offset 0 (superfloppy) first when that field is 0 or
   > 0x100; it parses a DOS MBR (entry 0 only: type @0x1c2, start-LBA @0x1c6,
   sig @0x1fe) when the field is 1..0x100, or as fallback when sector 0 lacks a
   `"FAT"` label.
2. **Validation:** superfloppy path = `"FAT"` @0x36 or @0x52 only; MBR path =
   0x55AA @0x1fe + nonzero type @0x1c2 + `"FAT"` @0x36/0x52 of the sub-partition VBR.
   The actual FS validation (bytes/sector == 512, cluster count ≥ 4085) happens in
   `FUN_0803b078` on whichever sector won. The MBR-vs-FAT discriminator is the
   start-LBA-field heuristic, nothing else.
3. **Verdict:** accepts either. If MBR, yes — it reads the VBR at the entry's start
   LBA (LBA 2048 in our image) through the same partition-relative read path. But the
   firmware's native, self-generated layout for A is a **superfloppy**, and a failed
   scan is "repaired" by superfloppy-formatting A.
4. **On-disk consequence:** see §6.

## 6. The on-disk A: layout (bare FAT16 superfloppy @ flash block 134)

The native layout is a **bare FAT16 superfloppy**: the FAT VBR/BPB at partition-A sector 0
(= NFTL logical sector 0 = physical block 134), **no MBR, no LBA-2048 offset** — this is
what the firmware itself formats. The mount requires of the boot sector:

- bytes/sector @0x0b = **0x200** (hard-checked).
- `"FAT16   "` @0x36 (or a FAT32 layout with `"FAT32   "` @0x52) — this is the *only*
  thing the scanner checks on the superfloppy path.
- **cluster count ≥ 4085** (FAT12-sized volumes are rejected at mount): pick
  sec/cluster so `datasectors/secperclus ≥ 0xff5`. (64 MiB @ 4 sec/cluster ≈ 32k
  clusters — fine; beware tiny images with large clusters.)
- **u32 @0x1c6 must be 0 or > 0x100** — zero those 4 bytes explicitly (as the firmware
  does) so the scan can't mis-dispatch into the MBR-only row. mkfs.fat images are
  zero there anyway, but our tooling should enforce it.
- Keep 0x55AA @0x1fe (not needed by the scanner's superfloppy path, but needed by the
  MBR fallback, by USB hosts, and by convention).

Consequences of the superfloppy layout vs an MBR+LBA-2048 image:
- No second read at partition-relative sector 2048, so no dependence on the partition size
  merely to reach the VBR; FAT metadata sits at logical block 0-2, comfortably inside the
  64-entry dense log2phy window ([`nftl-layout.md`](nftl-layout.md) §3).
- No ~1 MiB of pre-partition padding; FAT sector N maps directly to partition-relative
  sector N.
- The partition size (`AddrCnt`) must still cover the whole FAT volume (every sector FatLib
  touches must be in range — the `FUN_0800ea38` clamp still applies).

The MBR@0 + FAT@2048 image is *not* rejected by the firmware — it mounts via the MBR
path provided `AddrCnt_A` covers LBA 2048+ and the touched blocks are spare-tagged —
but it is the non-native layout, wastes dense-window blocks, and diverges from real-pen
state. Use the superfloppy.

## Key code citations

| function | addr | evidence |
|---|---|---|
| `fs_partition_scan` | 0x08039a68 | u32 @0x1c6 dispatch (disasm 0x8039ae0-0x8039b10); FAT check @0x36/0x52 (0x8039b14); MBR check 0x55AA/0x1c2/0x1c6 + VBR read (0x8039b44-0x8039b90) |
| `FUN_08039ddc` | 0x08039ddc | volume ctor; base LBA stored @vol+0x4c |
| `FUN_0803b078` | 0x0803b078 | BPB parse: 512 B/sector check, cluster-count ≥0xff5, FAT16/32 select |
| `FUN_08039d00` | 0x08039d00 | format wrapper (base 0) → `Fat_Format` |
| `Fat_Format` = `FUN_0803b310` | 0x0803b310 | writes FAT VBR at partition sector 0; templates `EB 3C 90 MSDOS5.0` / `EB 58 90 MSWIN4.1`; **zeroes buf+0x1c6..0x1c9** (`:187`) |
| `fs_storage_mount_init` | 0x0803a484 | scan(A) fail → format(A) superfloppy → hang if that fails (`:107-118`) |
| `fw_update` | 0x08051fd4 | "start format driver A" → `FUN_08039d00(A, 0, …)` |
| `FUN_08009cac` | 0x08009cac | bzero |
| strings | 0x0811c6a0 `"FAT"`; 0x0811c8c0/8cc/8d8 `"FAT16/12/32   "`; 0x0811c88c `"Fat_Format: offset=%d, SecPerClus=%d"` |
