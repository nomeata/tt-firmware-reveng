# Mask ROM (2N / SNOWBIRD2): is the ANYKANB2 code NORMAL boot or RECOVERY?

Static RE of the 2N mask ROM (ARM LE, base 0x0, 64 KiB). The mask ROM is a **hardware capture**,
not included in this repository; this is a description of its behaviour. **Proven** = read from the
disassembly/bytes cited; **Inferred** = deduction. Read-only analysis.

## Verdict (short)

- The **ANYKANB2-accepting code IS the normal boot path** — a 2nd-stage *loader*
  (read image to 0x08000000, then the reset code jumps there). It is **not** an
  erase/reflash/factory routine. Proven: no NAND program/erase in the loaders; on
  success they `return 1` and the reset dispatcher jumps to 0x08000000.
- The **normal power-on path** (nothing held down) is the boot-mode-detect return
  `r0==0` → storage boot: try SPI loader (PATH1 @0xc5c), then NAND loader
  (PATH2 @0xa68). That's it — the same ANYKANB2 loaders.
- The **recovery / re-flash / console** paths are the *strap-selected* modes
  `r0==1/2/3`: UART "SNOWBIRD2-BIOS>#" console (download/go/dump/setvalue) and USB
  Usbboot/Massboot. These do NOT magic-check anything; they take host commands.
- **No path accepts `ANYKANB1`.** The *only* boot magic anywhere in the ROM is the
  string `"ANYKANB2"` @0x6224, compared in exactly two places (PATH1 @0xd58, PATH2
  @0xb74). So the `.upd`'s ANYKANB1 SPL cannot auto-boot this pen — a structural fact,
  not a missing boot path.

## 1. Reset dispatch (@0x20) — Proven

The reset code calls the boot-mode-detect routine at 0xec0 (§2), which returns a mode in `r0`, then
dispatches:

- **`r0==3`** → jump 0x4260 (USB Massboot console);
- **`r0==1`** → jump 0x2840 (USB Usbboot console);
- **`r0==2`** → jump via 0xa4 → 0xa50 → 0x830 (UART BIOS console);
- **`r0==0` (DEFAULT, storage boot):** set the storage-select mux `[0x04000054]=0x5000000` and call
  PATH1 @0xc5c (SPI); on success (`r0==1`) jump 0x08000000. Otherwise set `[0x04000054]=0x4000000`
  and call PATH2 @0xa68 (NAND); on success jump 0x08000000. If both fail, set
  `[0x04000054]=0x6000000` and jump 0x4260 (USB Massboot recovery fallback).

Dispatch literals resolved: 0x4260 (Massboot), 0x2840 (Usbboot), 0xa50 (UART BIOS), 0x04000054
(storage-select mux reg). The exception vectors @0x0..0x1c forward to the *loaded image*
(0x08000004..0x0800001c) — i.e. the ROM expects a real image resident at 0x08000000 to own the
vectors, consistent with "load + jump".

## 2. Boot-mode detect @0xec0 — Proven (GPIO straps)

- Configures GPIO: `[0x04000074] &= ~1`, `[0x0400007c] |= 0x3000` (pull-ups).
- Samples input reg **`0x040000bc`** 5× (delay `bl 0xe2c` between).
  - If any sample has `(v & 0x3200)==0x3200` (all three strap pins high/idle)
    → **return r0=0 immediately** (0xfa8). This is the no-button power-on case.
  - Else counts, per pin, samples-held-LOW across all 5:
    - bit `0x2000` low ×5 → `[0x04000054]=0x6000000`, **r0=3** (USB Massboot)
    - bit `0x1000` low ×5 → `[0x04000054]=0x1000000`, **r0=1** (USB Usbboot)
    - bit `0x200`  low ×5 → `[0x04000054]=0x2000000`, **r0=2** (UART BIOS)
    - else **r0=0** (storage boot)

**So r0==0 = normal power-on** (pull-ups make idle = high = normal). r0==1/2/3
require physically holding a strap pin low → these are the recovery/console modes.

## 3. PATH1 @0xc5c (SPI, mode 0x5000000) — Proven: LOAD + JUMP

- `bl 0x4080` (storage init), `bl 0x4118(r0=0,r1=blk=1,r2=0x08000000,r3=0x40)`
  = read 0x40 header pages to 0x08000000.
- Copies 8 bytes from **image+0x20** to stack, `bl 0x140` memcmp vs `"ANYKANB2"`
  (@0xe28→0x6224). Mismatch → block-counter++ , retry blocks 1..4 (0xcc0), then
  `return 0` (0xe20).
- Match (0xd68): `bl 0x1ec` reads a **2-word descriptor at image+0x28**;
  requires `desc[0] > 0x40` (0xd8c) and rounds `desc[0]` up to /4 (0xda8).
  Then `bl 0x4080` (re-init w/ desc[1]) and
  **`bl 0x4118(r0=0, r1=blk, r2=0x08000000, r3=desc[0])`** — streams `desc[0]`
  pages of the *full image* into 0x08000000. Returns that read's result (1 = ok).
- Reset code then jumps 0x08000000. **No write/erase** (0x4118/0x4080 scanned:
  no 0x60/0xD0/program-confirm). Pure loader.

Note: PATH1 checks the magic at **image+0x20** — exactly where the `.upd`'s
ANYKANB1 SPL keeps its `"ANYKANB1"`. So on the SPI path the offset matches and the
rejection is purely the version digit `1` vs `2`.

## 4. PATH2 @0xa68 (NAND, mode 0x4000000) — Proven: LOAD + JUMP

- Iterates 8 NAND chip configs (table @0x5fd0). Per config: copy 6-word cfg to
  stack, `bl 0x1be8` (apply), `bl 0x20c4(r0=0x08000000,r1=0xa00000,r2=0x1d8)` read.
- Copies 8 bytes from **image+4** to stack, `bl 0x140` memcmp vs `"ANYKANB2"`
  (@0xc58→0x6224). Mismatch → next config; all 8 fail → `return 0` (0xc4c).
- Match (0xb84): `bl 0x1ec` reads a **6-word descriptor at image+0xC**; parses
  geometry (desc[2]/desc[3] via `bl 0x1a48`, desc[4]&0xffff via `bl 0x1aa4`) then
  **`bl 0x1ee8(r0=0x08000000, r1=descriptor, r2=1)`** — streams the full image to
  0x08000000. Returns result. Reset code then jumps 0x08000000.
  **No write/erase** in 0x20c4/0x1ee8. Pure loader.

## 5. Recovery / console modes (r0==1/2/3) — Proven

- `0x830` (r0==2, mode 0x2000000): prints `"SNOWBIRD2-BIOS>#"` (@0xa48→0x6204),
  dispatches a UART command table @0x5f6c (stride 0x14): **"download" (h=0x2d0),
  "go" (0x274), "dump" (0x55c), "setvalue" (0x6b0)**. (The 3rd memcmp@0x140 caller,
  0x7d0, is this command-name match — NOT a boot magic.)
- `0x2840` (r0==1, mode 0x1000000): `"\nSnowbird2_Usbboot>#"` (@0x2884).
- `0x4260` (r0==3, mode 0x6000000, also storage-fallback): `"\nSnowbird2_Massboot>#"`.
  This is the USB mass-storage programming/recovery endpoint (idles for a host).

These are the true recovery / re-flash / factory-programming surfaces. They are
host-command-driven (download to addr, go addr, poke memory, mass-storage) and are
**not** magic-gated auto-boot. The BurnTool/producer talk to these.

## 6. Does anything accept ANYKANB1? — Proven: NO

`"ANYKANB2"` @0x6224 is the sole boot-magic string; the only two pointers to it are
@0xc58 (PATH2) and @0xe28 (PATH1). memcmp@0x140 has three callers: PATH1, PATH2, and
the console command-parser (which compares command names, not a boot magic). There
is no ANYKANB1/ANYKANB0 string, no looser/shorter compare, no alternate auto-boot.
The real pen has no SPI-NOR (PATH1 reads zeros → fails), so it boots via PATH2/NAND,
whose magic is at +4 with a NAND descriptor at +0xC — a layout the ANYKANB1 SPL
(magic@+0x20, descriptor@+0x28) doesn't even match structurally.

**Conclusion:** the `.upd`'s ANYKANB1 SPL cannot auto-boot this pen, and this is
**structural**, not a missing normal path — the ANYKANB2 loaders ARE the normal path,
and they load+jump an ANYKANB2 image the `.upd` does not contain. Cold-booting this pen
requires an ANYKANB2 (Snowbird2) NAND boot image (magic@+4, descriptor@+0xC), obtainable
only from the pen's NAND boot blocks (a hardware capture, not included here).
