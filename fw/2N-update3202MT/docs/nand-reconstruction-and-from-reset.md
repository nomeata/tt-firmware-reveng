# NAND reconstruction + from-reset boot experiment (2N / update3202MT)

Whether the shipped artifacts alone form a complete, authentic cold-boot chain. Addresses cited;
Proven = observed by execution or byte-read, Inferred = deduction. The mask ROM referenced below is
a **hardware capture**, not included in this repository.

## Part A — reconstructed NAND image (Proven layout)

Parsed straight from `update3202MT.upd` (ANYKA106 container) header:

| .upd field | meaning | value |
|---|---|---|
| +0x20 / +0x24 | Boot (SPL) offset / size | 0x20000 / 0x7e80 |
| +0x38 / +0x3c | to_NAND count / records | 2 @ 0x3200 |
| +0x30 / +0x34 | to_udisk count / records | 7 @ 0x2800 (stride 0x110) |

to_NAND records (stride 0x24; {?, ?, size@+8, off@+0xc, name@+0x10}):
- **PROG**     .upd 0x28000, size 0x380000 → NFTL system partition (FLAT)
- **codepage** .upd 0x3a8000, size 0xd6ccc → NFTL system partition (FLAT)

to_udisk files (→ FAT partition): `voice\Chomp_Voice.bin`,
`Language\{BatLowUpdate,Update}{DUTCH,FRENCH,GERMAN}.wav`.

Geometry (Hynix HY27UF084G2B; seeded runtime struct @0x08008cc4): **page 2048 B + 64 B
spare, 64 pages/block, 4096 blocks (512 MiB)**.

A faithful NAND reconstruction lays out: boot area = the SPL **verbatim** (see below), system
partition = PROG+codepage flat, FAT = the 7 files. **Faithfulness caveat:** page DATA + a spare
stub are modelled; real Hamming/BCH ECC bytes and the exact NFTL log2phy wear-level map are not
reconstructed — the boot + FatLib mount consume the logical page content, which is the level that
matters.

**Producer does NOT re-stamp the boot magic (Proven).** The only `"ANYKANB2"` string in
`producer.bin` is at file 0x1b8fc, referenced from exactly one site (0x08000198 = the
chip-identity check that strcmps `maskrom[0x6224]`). There is no write of that constant
into any boot block. So the on-NAND boot image = the .upd SPL as-is: magic `"ANYKANB1"` at
**+0x20**, and a NAND geometry descriptor at +0x28.

## Part B — from-reset boot

### B.1 Mask-ROM magic outcome — **REJECT (Proven)**

Running the real captured mask ROM (Snowbird2 ANYKANB2) from its reset vector @0x20: straps →
auto storage boot; SPI probe reads zeros → fails → NAND boot (`try_nand_boot` @0xA68). Served the
SPL header page, the ROM copies 8 bytes from **image+4** and compares to `"ANYKANB2"` via `memcmp`
@0x140 (call site @0xB74).

Observed: the 8 bytes at image+4 are `40 00 00 ea 0e f0 a0 e1` (an ARM `b` instruction, not a
magic) vs `"ANYKANB2"` → **MISMATCH**. The ROM loops all 8 NAND cfgs (all mismatch), NAND boot
returns 0, and it falls through to **USB mass-storage recovery @0x4260** (where it idles waiting
for a USB host).

The mismatch is deeper than the version digit: the ANYKANB2 ROM reads the magic at **image+4**,
where the ANYKANB1 SPL has a `b` instruction (`40 00 00 ea`); the SPL's magic lives at **+0x20**
(the older ANYKANB1 header layout — magic@+0x20, descriptor@+0x28, i.e. +0x1C shifted from the
ANYKANB2 NAND layout magic@+4/descriptor@+0xC). So the pen's mask ROM cannot even find, let alone
match, the magic. **Generation mismatch confirmed empirically.**

### B.2 Does the SPL run? — **yes, HW init only; it does NOT load PROG (Proven)**

Loading the SPL @0x08000000 (as an accepting ROM would) and starting at the SPL reset vector, the
flow (all in the SPL image, verified) is: reset `b` @+0 → **reset handler @0x08000088**: MMU off
(`mcr p15 c1`), clear low RAM (loop @0x080000b0 zeros 0x08008000-0x08009000), set per-mode stacks,
then `bl init1` (0x08005a80, GPIO/clk), `bl init2` (0x080018d4, builds MMU section tables +
stacks), `bl init3` (0x08006c6c: clock/PLL `0x08002f6c(0x1c200,0x3938700)`, **chip-ID gate
`*0x04000000=="1090"` @0x08006c90**, region sanity), then finally
**`ldr pc,[0x0800021c]` = 0x08039100** — the PROG entry it hands off to.

The SPL then drives its own NFC (controller @0x0404A000; cmd-done poll bit31 @0x0404A158 via
0x08002db4) into **NFTL / NAND init** (page-read loop @0x08001fdc / 0x080027a4).

Two hard, decisive observations across the whole run:
- **PROG code region 0x08009000..0x08380000: ZERO writes.** All SPL writes land in
  0x08008000-0x08008ffc (the shared low-RAM globals/buffers, incl. geometry @0x08008cc4).
  → **The SPL does not stream PROG (3.5 MB) into RAM.**
- **Boot-SRAM window 0x07ff0000..0x08000000: ZERO writes.** → The SPL does not self-relocate its
  HAL there either.

### B.3 How far does PROG boot from the real SPL?

Even if the SPL reached the `ldr pc,[0x0800021c]=0x08039100` handoff, it would land on the same
PROG entry (0x08039100) that a from-entry boot starts from — so the real-SPL path adds nothing
PROG cannot already reach there. (PROG's entry boots cleanly from artifacts alone under a flat
load, with no dump seeding — reproduced by running the unmodified firmware under
[`tt-emu`](https://github.com/nomeata/tt-emu). **Proven.**)

## B.4 Verdict

**This .upd is NOT the pen's actual from-cold-boot image.** Its PROG matches the pen
(update3202MT, proven elsewhere by the sysapi correlation), but its boot SPL is the wrong
generation:
- The pen's mask ROM is ANYKANB2 (Snowbird2), loads ONE magic-checked image to 0x08000000
  and jumps there. It **rejects** the .upd's ANYKANB1 SPL (magic absent at +4).
- The ANYKANB1 SPL is **not a self-sufficient bootloader**: it does HW/clock/chip-ID/NFTL init
  and hands to PROG@0x08039100 assuming PROG is already resident at 0x08000000 and the HAL at
  0x07ff8000 — it loads neither. That fits an older boot model where the ROM (or a stage we don't
  have) places both images; it does not fit the pen's ANYKANB2 ROM.

So the pen's real boot SPL is an ANYKANB2 image not present in these artifacts (it would need
capturing from the pen's NAND boot blocks). PROG is authentic and matching, and the mask ROM is a
genuine hardware capture, but the boot SPL in this `.upd` cannot boot this pen — the from-reset chain
is **not** authentic end-to-end. The mask-ROM reject is the concrete proof.
