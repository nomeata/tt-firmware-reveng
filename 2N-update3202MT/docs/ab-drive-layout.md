# A: vs B: — the two NFTL FAT partitions and who holds the games

> Scope: the authentic drive/partition layout — which drive holds user games (`*.gme`)
> vs system content, the two-partition geometry, and how discovery/mount handle each.
>
> **Headline (Proven): B: is the user drive with the games; A: is the system drive.**
> The USB MSC exposes **only partition B** to the PC (LUN 0 = the B partition object),
> so every user-copied `.gme` lands on B: by construction. A: holds only system content
> (`VOIMG/`, `Language/`, `SYSTEM/profile.dat`, the discovery `.lst` indexes, the
> firmware log) and is never PC-visible in normal operation.
>
> All addresses = unified runtime base **0x08009000** (flat `PROG.bin` load).
> Tags: **[Proven]** = Proven (decomp/disasm/bytes cited), **[Inferred]** = Inferred.
> Companions: [`book-discovery-and-load.md`](book-discovery-and-load.md) (the scan/mount),
> [`partition-a-fat-vs-mbr.md`](partition-a-fat-vs-mbr.md) (sector-0 parse — drive-agnostic,
> applies to both A and B), [`usb-msc-device.md`](usb-msc-device.md),
> [`upd-system-partition-layout.md`](upd-system-partition-layout.md) (A:'s factory content),
> [`nftl-layout.md`](nftl-layout.md). Clarifications relative to those docs are in §6.

---

## 1. Partition → drive-letter mapping [Proven]

`fs_storage_mount_init @0x0803a484` (decomp `0x0803a484.c`), after building the ONE
FS-area medium (`nftl_build_fs_medium(dev, fs_start, total_blocks − fs_start, zonetab)`,
fs_start = Information[+4] of the zone-table page):

```
cntA = nftl_zonetab_addrcnt(0)                       // Zone_Group with Symbol byte == 0 ('A')
partA = flash_erase_region(medium, 0,    cntA, 0x200, 1)   -> *0x081db90c
cntB = nftl_zonetab_addrcnt(1)                       // Symbol byte == 1 ('B')
partB = flash_erase_region(medium, cntA, cntB, 0x200, 0)   -> *0x081db910
volA = fs_partition_scan(partA, 0x200, 0)            -> *0x081db914
   if 0: volA = fat_format_wrapper(partA, 0, …)      // A: AUTO-FORMATTED superfloppy; hang if that fails
FUN_0803a1c8(volA, 0)                                // register as DRIVE 0
volB = fs_partition_scan(partB, 0x200, 0)            -> *0x081db918
   if 0: FUN_080ef760(1)                             // B: error flag ONLY — never auto-formatted here
FUN_0803a1c8(volB, 1)                                // register as DRIVE 1 (no-op if volB == 0)
```

- `nftl_zonetab_addrcnt @0x0803aa3c` re-reads the zone-table page (block 0, page
  ppb−1) and walks the `Zone_Group` records matching **`[+0xb]` (Symbol) == param**,
  returning `[+4]` (AddrCnt, raw LE u32). Symbol 0 = 'A', 1 = 'B'. [P `0x0803aa3c.c`]
- **B's start is derived as A's AddrCnt** (`flash_erase_region(medium, cntA, cntB, …)`)
  — the record's own StartAddr word is not consulted; contiguity A-then-B is enforced
  by construction. [Proven]
- `FUN_0803a1c8` (**fs_drive_register**) stores the volume pointer into the drive array
  `*(*(0x08008c30+4)+0xc)[idx]` and bumps the drive count `*(…+8)`. `File_Open
  @0x080f8f20` resolves a path's drive letter as `toupper(w[0]) − 'A'` into that array
  (reject if ≥ drive count). **So drive slot 0 = 'a:'/'A:', slot 1 = 'b:'/'B:'** —
  zone A → a:, zone B → b:, exactly. [P `0x0803a1c8.c`; drive check per
  `fatvol-medium-layering.md` §3.4]
- Global quintet @**0x081db908**: `{fs_medium, partA, partB, volA, volB}`
  (pools `DAT_0803a78c/0x0803d764/0x0803d5b8/0x0803d768/0x0803d760` resolved from
  PROG.bin). [Proven]

**Asymmetry (intent evidence):** A: is force-formatted on scan failure (the pen cannot
run without its system FAT — the boot hangs if even the format fails); B: failure is a
soft flag (`FUN_080ef760(1)`) and drive 1 simply stays unregistered. B:'s FAT is created
at the factory (producer mkfs) / by the USB vendor "FM" command (`FUN_0803d0cc`:
`fat_format_wrapper(*0x081db910 = partB, 0, sizeMB·(0x100000>>9), …)`) — i.e. B: is the
*replaceable user medium*, A: the *mandatory system medium*. [Proven]

The same A-format-fallback + drive-0/1 re-registration sequence runs again after every
USB session in `usb_state_handler @0x08051090` (post-`usb_power_switch` remount: A via
`*0x0803d764`→partA with format fallback → drive 0; B via `*0x0803d5b8`→partB with
error-flag only → drive 1, registration inlined with idx 1). [P `0x08051090.c`]

## 2. Zone-table geometry — the exact numbers [Proven]

Producer-written zone table (block 0, page 63, tag `0x5a5a5a5a`; from a producer run,
64 MB image), raw bytes:

```
Information: 40 00 00 00 | 07 00 00 00 | 00 00 03 00 | …   TotalLen=0x40, fs_start=7*, nzones=3
Zone[0] FAKE: Start=0        AddrCnt=0x2F0 (LE)  Type5 Sym=0xBE   — 752 = A+B in PHYSICAL BLOCKS
Zone[1] A   : Start=0        AddrCnt=0x3C00 (LE) Type2 UNSTANDARD Sym=0 ('A') Part_NO=1
Zone[2] B   : Start=0x3C00   AddrCnt=0x8000 (LE) Type4 STANDARD   Sym=1 ('B') Part_NO=2
```

\* fs_start=7 is the no-bins-burned artifact; a real pen has the host-advanced value
(~64+); the working reconstruction uses **134**.

**Unit (current reading):** the fields read as plain **LE u32 in 2-KiB-page units**
(A: 0x3C00 = 15360 pages = **30 MB** — exactly the `.upd` partition record @0xA4;
B: 0x8000 = 32768 pages = 64 MB; FAKE: 0x2F0 = 752 *blocks* = (30+64) MB / 128 KiB).

> **Open:** the A: partition size is quoted as both **30 MB** (this doc's page-unit
> reading, matching the `.upd` record) and **60 MB** across the address-count derivations
> — [`nftl-medium-translation.md`](nftl-medium-translation.md) §0 decodes the same
> `AddrCnt` field in AU units; the exact unit is unresolved.

Consumption chain [Proven]:

- `nftl_zonetab_addrcnt` returns the raw LE u32 (pages).
- `flash_erase_region @0x0803c784` scales it: `f = udivmod(0x200,
  chip[+0x18]·chip[+0x1c]).quot` — `func_0x080031d0` disassembles as the classic ARM
  runtime **udivmod** (r1/r0), and chip[+0x18]·chip[+0x1c] is the page(+spare) buffer
  size of the 2-KiB-page chip, so **f = 4 sectors/page**. Partition desc gets
  `desc[2] = 4·start`, `desc[3] = 4·count` in **512-B medium sectors**, clamped to the
  medium capacity. `FUN_0800ea38` then reads `desc[2] + FAT_LBA`. [P disasm of
  nandboot 0x31d0 + `0x0803c784.c`; f=4 P-by-arithmetic (any sane 2-KiB-page
  chip[+0x18]·[+0x1c] ∈ {2048, 2112} gives quotient 4), pinned by `.upd` A=30 MB]

**Resulting layout on the FS-area medium** (logical block = 256-sector erase block,
identity log2phy under our spare tags; physical = fs_start + logical):

| partition | FS-medium sectors | FS-medium blocks | physical blocks (fs_start=134) | size |
|---|---|---|---|---|
| **A:** (drive 0) | 0 … 0xEFFF | 0 … 239 (240) | **134 … 373** | 30 MB |
| **B:** (drive 1) | 0xF000 … | 240 … 240+N−1 | **374 … 374+N−1** | real pen: fill-rest (≈ all remaining, ~460 MB); producer 64 MB run: N=512 → 374…885 |

Each partition is a **bare FAT16 superfloppy at its partition-relative sector 0**
(`fs_partition_scan`'s parse + the firmware's own `Fat_Format` — drive-agnostic; see
`partition-a-fat-vs-mbr.md` §1–3 for the VBR requirements: bytes/sector 512, cluster
count ≥ 4085, u32@0x1c6 ∈ {0} ∪ (>0x100)). [Proven]

## 3. Which drive holds WHAT [Proven]

**A: = system + index drive** (never PC-visible in normal operation):

| content | writer | evidence |
|---|---|---|
| `A:/VOIMG/Chomp_Voice.bin` (48 system voices) | `.upd` to_udisk unpack (fw update / factory) | dir map `voice → A:VOIMG` @.upd 0x2200; consumer `fwl_play_voice_by_id @0x080ab9ac` string `A:/VOIMG/…` @0x080ab990 |
| `A:/Language/*.wav` (update/bat-low prompts) | `.upd` to_udisk | dir map `Language → A:Language`; `lang_play_batlow @0x080523b4` |
| `A:/SYSTEM/profile.dat` | firmware on demand | `FUN_0804b8cc`; UTF-16 strings @0x080affd4 / 0x0803c9f0 |
| `a:/oidfilelist.lst` (+ vestigial music/voicelist) | discovery scan each boot | strings @0x080ae394/3a8/3bc |
| `A:/Firmware log file.bin`, `A:/Product log file.bin` | firmware / production | boot trace; `"USBSA:/Product log file.bin"` @0x0803f060 |

**B: = the user/udisk drive** (the one the PC sees):

- **USB MSC LUN 0 = partition B.** `FUN_0803ece4(1)` (**usb_msc_register_lun**) builds
  the LUN record from `*DAT_0803ee38 = 0x081db910` = **partB**, with the
  `"MP4     MP4 Player      V1.0"` inquiry blob @0x08041F4C, and hands it to
  `FUN_08040440` (LUN-array insert, `LUN+0x14` = the medium the READ10/WRITE10 pump
  addresses). Both static callers — `usb_power_switch @0x0803d1d4:36` (state-5 PC
  session) and `charge_main @0x080504a4:38` — pass **1**. The `param==2` variant
  (partition **A**, inquiry `"RES"` @0x08041F27) has **no static caller** — a
  production/reserved LUN, never exposed. So **the PC can only ever write to B:**;
  user-copied games land on B: by construction. [P pools 0x0803ee30/38 = 0x081db90c/10]
- The **`.upd` firmware update** is likewise dropped on B: by the tiptoi Manager
  (`fw_update_cleanup @0x08052768` scans `B:\` for `*.upd`; `B:/LanguageInfoMT.txt`
  @0x080ed016). The `.upd` volume-label slot for B = **`tiptoi`** — the label users
  see. [P `upd-system-partition-layout.md` §1/§3]
- The profile's default media scan path is UTF-16 `B:/` (profile+0xD4C,
  `FUN_080391e4`), and the vestigial IMAGE/VIDEO paths are `B:/IMAGE`, `B:/VIDEO`
  (@0x0803ca12). [Proven]

**Nothing in the boot path ever writes B:** — the log, profile, and `.lst` writes all
target A:. B: is read-only to the firmware outside the USB session / update flow. [P
corpus: all boot-path write/create sites above]

## 4. Discovery & mount vs the two drives [Proven]

- `udisk_gme_discovery(list 1)` at standby entry scans root **`"B:"`** (scan-root
  table @0x08122708 → string @0x080ed9ea, verified bytes `B:\0`) recursively for
  `*.gme`, **then patches the drive letter to 'A'** and scans A: — both passes append
  to `a:/oidfilelist.lst` (on A:). Records carry only the full path (drive letter
  included). [P `0x080afa80.c` `*puVar3 = 0x41`; `book-discovery-and-load.md` §2]
- `gme_mount_check_product @0x08034130` opens **whatever path the `.lst` record
  holds** — `B:/foo.gme` or `A:/foo.gme` — with no drive preference beyond the scan
  order (**B: records come first**). [Proven]
- So: **games are *expected* on B:** (that is where the USB session puts them, and B:
  is scanned first), but a `.gme` on A: *would* also be found (pass 2). On a real pen
  A: never holds one — nothing ever writes a `.gme` there (§3). The A: pass most
  plausibly exists to cover factory-preloaded content [Inferred].
- If B: failed to mount (drive 1 unregistered), the B: pass no-ops at `File_Open`'s
  drive check and only A: is scanned. [P mechanism]

## 5. Answers to the four questions

1. **Games live on B:** — the USB-exposed user FAT (LUN 0 = partB, label `tiptoi`).
   A: *can* hold games (scan pass 2) but authentically never does; it holds only
   system content + the discovery/`.lst` index. `gme_mount_check_product` opens the
   recorded path — on a real pen `B:/<name>.gme`. **[Proven]**
2. **Zone A (Type2 UNSTANDARD, Symbol 0) → drive 0 = 'a:' = SYSTEM; zone B (Type4
   STANDARD, Symbol 1) → drive 1 = 'b:' = USB user drive.** Cross-checked three ways:
   the mount order (§1), the USB LUN medium (partB = the MP4-inquiry LUN), and the
   content writers (§3). **[Proven]**
3. **Geometry:** A: = FS-medium blocks [0, 240) = 30 MB starting at fs_start
   (physical 134…373); B: = blocks [240, 240+N) starting at physical fs_start+240 = 374
   (N = 512 for the 64 MB producer ground truth; fill-rest on the real pen). AddrCnt is
   consumed ×4 into 512-B sectors, `B.start := A.AddrCnt`. **[Proven]** (§2 table) *(Open: the
   A: size is quoted as both 30 MB and 60 MB across the address-count derivations — the
   exact AddrCnt unit is unresolved; see
   [`nftl-medium-translation.md`](nftl-medium-translation.md) §0.)*
4. **A: holds SYSTEM/VOIMG/Language + `oidfilelist.lst` + logs — yes.** All path
   constants are `A:`/`a:`-rooted (§3). **[Proven]**

## 6. Clarifications relative to related docs

- **The USB-exposed superfloppy is B:, not A:.** The sector-0/format analysis in
  [`partition-a-fat-vs-mbr.md`](partition-a-fat-vs-mbr.md) is correct and drive-agnostic,
  but the drive a PC sees as `sda` is **B:** (A: is formatted identically, so those
  mechanics apply to both).
- **The FS area is linear, not wear-leveled.** The pen's `flash_ic.flag == 0x1` has no
  wear bit, so the single FS medium uses the linear resolver `FUN_0800fef0`; A: and B: are
  sector ranges on that one linear medium (§1). See [`nftl-layout.md`](nftl-layout.md) §3.
- **The format-on-fail fallback targets A:, not B:.** `fs_storage_mount_init`'s format
  fallback runs on **partition A** (`*0x0803d764 = 0x081db90c`); B: only gets an error
  flag. B: is (re)formatted only by the vendor "FM" command / factory mkfs
  ([`usb-msc-device.md`](usb-msc-device.md)).

## 7. Evidence index

| claim | status | source |
|---|---|---|
| zone Symbol 0/'A' → partA → drive 0; Symbol 1/'B' → partB → drive 1 | **P** | `0x0803a484.c:94–126`, `0x0803aa3c.c` (Symbol match @rec+0xb), `0x0803a1c8.c`; drive-letter check `File_Open` per fatvol §3.4 |
| B.start := A.AddrCnt (contiguous, StartAddr word unused by the mount) | **P** | `0x0803a484.c:99–103` |
| A: format-on-fail (hang if impossible); B: error-flag only | **P** | `0x0803a484.c:109–127`; post-USB remount `0x08051090.c` |
| AddrCnt consumed ×4 (units → 512-B sectors) by `flash_erase_region` | **P** | producer image block-0 page-63 bytes (§2); `.upd` A=30 MB record; `0x0803c784.c` + nandboot 0x31d0 disasm (udivmod). *(Open: A: size quoted as both 30 MB and 60 MB across derivations — exact AddrCnt unit unresolved; see [`nftl-medium-translation.md`](nftl-medium-translation.md) §0.)* |
| USB LUN 0 = partition B, "MP4" inquiry; A/"RES" variant dead | **P** | `0x0803ece4.c` + pools 0x0803ee30/38 → 0x081db90c/10, inquiry bytes @0x08041F27/F4C; callers `0x0803d1d4.c:36`, `0x080504a4.c:38` |
| vendor "FM" formats B | **P** | `0x0803d0cc.c` (`*DAT_0803d5b8` = 0x081db910) |
| A:-rooted system content; B:-rooted user/update content | **P** | strings §3 (verified in PROG.bin) |
| discovery roots "B:" then patched-'A'; `.lst` on a:; mount opens recorded path | **P** | scan-root table @0x08122708 → `B:\0` @0x080ed9ea (bytes verified); `0x080afa80.c`; `book-discovery-and-load.md` |
| boot path writes A: only, never B: | **P** | writer inventory §3 |
| A: scan pass exists for factory-preload; games authentically live on B: | **I** | §4 (mechanism P) |
