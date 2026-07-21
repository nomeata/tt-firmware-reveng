# NFC (Anyka NAND-flash controller) register model — 2N / update3202MT

Static RE of the **hardware boundary** the firmware's own NAND routines drive: the NFC
register block at **`0x0404A000`**, the **ECC engine at `0x0405B000`**, the **L2 buffer
controller at `0x04010000`** with its **staging SRAM at `0x08006000..0x08006AFF`**
(NAND uses buffer 4 = **`0x08006800`**), plus clock/pin plumbing. This is the register/IRQ
boundary at which the firmware's `nb_nand_read_page` / `nb_nand_program_page` /
`nb_nand_erase_block` — and the main firmware's *own* raw page ops
(`FUN_080ee224`/`FUN_080ee468`) — talk to the flash: a full description of the controller
below the leaf functions.

**Conventions.** nandboot image (32384 B) analyzed at file base `0x07ff8000` ↔ runtime
`0x08000000` (rt = file + 0x8000). PROG at `0x08009000`. Claims are **Proven** from disasm
+ decomp + the literal pools resolved from the binaries, unless marked **Inferred**.
Mask-ROM cross-checks in §7 (the mask ROM is a separate hardware capture, not part of this
repository).

---

## 0. Architecture: three MMIO blocks + one SRAM, no DMA engine

```
CPU  ──memcpy──►  L2 SRAM buffer 4 @0x08006800 (512-B circular window)
                        ▲ │
                        │ ▼   (streamed by the controller)
   L2 buffer ctrl @0x04010000  ◄──►  ECC engine @0x0405B000  ◄──►  NFC @0x0404A000  ◄──► NAND bus
                                     (BCH encode/decode,           (command-list
                                      correction FIFO,              sequencer, R/B,
                                      randomizer NOT here)          CE, timing)
```

- **Proven:** there is **no DMA** in the page path. The CPU `memcpy`s
  (`0x08003118` = plain word-optimized memcpy) between the caller buffer and the L2
  SRAM window; the controller moves bytes NAND↔ECC↔L2 by itself. The "DMA" wording in
  older docs (`nftl-write-consistency.md`, `nb-read-data-conventions.md`) means this internal
  streaming.
- The NFC executes a **command list** (micro-ops) staged at `0x0404A100..`, started by a
  GO write to `0x0404A158`. Data-phase micro-ops route bytes through the ECC engine into
  L2 buffer 4; ECC parity is generated/consumed inside the engine and never appears in
  the SRAM.
- **L2 SRAM map (Proven** from the buffer-address jumptable `0x080034b4`): buffers
  0–4 = 512 B each at `0x08006000/0x6200/0x6400/0x6600/0x6800`; small 64-B buffers at
  `0x08006A00/0x6A40/0x6A80/0x6AC0`. NAND always uses **buffer 4 (`0x08006800`)**;
  buffers 2 and 3 are used by other peripherals (`FUN_0803ef30`, `FUN_080417f4`:
  `func_0x0800344c(2,…)/(3,…)` — SD/USB side). The nandboot image is zero-filled over
  file offsets `0x5f00..0x6aff`, i.e. the L2 SRAM window is carved out of the boot-SRAM
  address space (**Proven**: image bytes all-0, code resumes at `0x6b00`).

---

## 1. NFC register block `0x0404A000` (Q1)

| offset | name | R/W | semantics (all Proven unless noted) |
|---|---|---|---|
| `+0x100..+0x14F` | **CMD_LIST[0..19]** | W | Command-list FIFO. Firmware writes 32-bit micro-op words at `+0x100, +0x104, …`; execution starts on GO and stops at the first word with **bit0 (LAST)** set. Re-used phase-by-phase (a data phase writes a single word at `+0x100` and re-triggers). |
| `+0x150` | **DATA_RD0** | R | Read-back bytes 0–3 (LE) captured by a *read-to-register* micro-op (`0x59`-type): NAND status byte (`&0xff` after cmd 0x70), ID bytes 1–4 after cmd 0x90. |
| `+0x154` | **DATA_RD1** | R | Read-back bytes 4–7 (ID bytes 5–8 after cmd 0x90). |
| `+0x158` | **CTRL/STATUS** | R/W | *Write* `0x40000200 \| 1<<(10+ce)` = **GO** (execute staged list; bit30 = go/enable, bits[13:10] = chip-enable mask, bit9 always set — maskrom uses the identical value). *Read*: **bit31 = ready/done** (`nb_nand_ready` `0x08002db4` polls `[+0x158]&0x80000000`). **bit14 (0x4000)** is a sticky status bit the firmware clears (RMW `&~0x4000`) before every data phase; it is never read back — safe to model as don't-care. (Older docs called this bic "arming HW-ECC"; the real ECC config is the engine word, §2. Correction.) |
| `+0x15c` | **TIMING0** | W | NAND timing, written once at probe from the flash-config table (`cfg[+0x18]` = `0xF5AD1`). |
| `+0x160` | **TIMING1** | W | Timing 2 (`cfg[+0x1c]` = `0x40203`). |

### Micro-op word encoding (Proven from all sequences below)

```
bits[18:11] = payload: the byte driven on the NAND bus (cmd/addr ops),
              or count-1 (data ops; field extends above bit18 for counts >256)
bit0  = LAST (end of command list)
low bits    = type:
  0x64  command cycle  (CLE)          e.g. 0x64        = cmd 0x00
  0x62  address cycle  (ALE)          0x62|byte<<11 ; last addr cycle uses 0x63
  0x118 data transfer NAND->L2/ECC    word = (nbytes-1)<<11 | 0x119   (always LAST)
  0x128 data transfer L2/ECC->NAND    word = (nbytes-1)<<11 | 0x129   (always LAST)
  0x58  data read NAND->DATA_RD regs  word = (nbytes-1)<<11 | 0x59  (1..8 bytes)
  0x200/0x201 wait R/B                (0x201 = wait + LAST terminator)
  0x400 flag = wait-R/B attached to a cmd cycle (e.g. 0x18464 = cmd 0x30 + wait)
  0xc00 short delay (tWHR-style)      0x27401 = timed wait, count 0x4E, + LAST
```

Observed literal words: `0x64`=cmd 0x00 · `0x18464`=cmd 0x30+wait · `0x40064`=cmd 0x80 ·
`0x8065`=cmd 0x10+LAST · `0x30064`=cmd 0x60 · `0x68064`=cmd 0xD0 · `0x38064`=cmd 0x70 ·
`0x48064`=cmd 0x90 · `0x7f864`=cmd 0xFF · `0x1b865`=cmd 0x37+LAST · `0x1b065`=cmd 0x36+LAST ·
`0xb065`=cmd 0x16+LAST · `0x1a864`=cmd 0x35 · `0x42864`=cmd 0x85 · `0x3859`=read 8 ID bytes.
So the "internal opcode 0x63/0x64" wording in `nftl-write-consistency.md` is a mis-read: 0x64 is
the *command-cycle micro-op type* (the NAND opcode sits at bits[18:11]); `(row>>…)<<11|0x63`
is the **last row-address cycle**, not a command. (Correction.)

Address cycles are emitted by `nb_nfc_addr_cycles` (`0x08002dfc` → writer `0x08002f2c`):
**2 column cycles** (column = the op's `param_3`) then **N row cycles** (LSB first),
N = `[0x080079e0]` (= 3 for this cfg, from `cfg[0x11]`). Erase passes 0 column cycles.

---

## 2. ECC engine `0x0405B000` (Q1)

| offset | name | R/W | semantics |
|---|---|---|---|
| `+0x00` | **ECC_CTRL/STATUS** | R/W | *Write:* engine config word, then `word\|8` (**bit3 = start**). *Read (status bits):* **bit6 (0x40)** = op complete (W1C: firmware writes back `\|0x40`); **bit24 (0x1000000)** = encode/write-path done; **bit25 (0x2000000)** = decode/read-path done; **bit26 (0x4000000)** = decode pass (no errors; W1C); **bit27 (0x8000000)** = uncorrectable (W1C); neither 26 nor 27 = correctable errors pending in the correction FIFO. |
| `+0x04..` | **ECC_CORRECT[i]** | R | Correction FIFO: one 32-bit entry per correction register, count = `mode<4 ? mode*4+4 : mode*8-8`. Entry: bits[9:0] = bit position, bits[21:10] = error flags; applied by `nb_ecc_apply_corrections` (`0x08002518`) as XOR bit-flips in the caller buffer. |

**Engine config word** (Proven from call sites of `nb_data_phase_issue` `0x08002eb8`):

```
bits[18:7]  = payload length in bytes (512 for a data sector, 8 for the NFTL tag,
              0x202=514 for the producer raw read data+2 spare, 0x200 for info pages)
bits[24:21] = ECC mode/strength (parity bytes per sector = mode<4 ? 7*mode+7 : 14*mode-14,
              from nb_ecc_parity_bytes 0x08002ee0)
base flags  = read:  0xc100012  (bit1+bit4 dir/en, bit20, bits26+27 = W1C old status)
              write: 0x0100015  (bit0 = ECC encode en, bit2 write dir, bit4, bit20)
              raw single-byte write (read-retry): 0x100094 = len 1, no ECC bit
```

The NFC-side data micro-op count = payload + parity (`nbytes = len + parity(mode)`);
the parity is generated/checked by the engine and **never enters the L2 SRAM**.

`nb_ecc_wait_ack` (`0x08002e8c(dirbit)`): poll `+0` until bit6, then until `dirbit`
(0x2000000 read / 0x1000000 write), write back `|0x40`, return the value.
`nb_ecc_classify` (`0x080025d4(val)`): bit26 → 1 (clean, W1C), bit27 → 3 (uncorrectable
→ caller does all-0xFF erased-page check, else Hynix read-retry), else → 2 (apply
correction FIFO).

---

## 3. L2 buffer controller `0x04010000` (Q1, Q2)

| offset | name | R/W | semantics |
|---|---|---|---|
| `+0x00` | **BUF_CTRL** | R/W | `nb_l2_buf_ctrl(buf,op)` `0x0800344c`: RMW `(old & ~0xf700) \| (buf&7)<<8 \| (op&15)<<12 \| 0x800`, then clear bit11 — bit11 is a **strobe**. `op 0` = attach/reset buffer, `op 1` = **flush/commit** partial buffer to the peripheral (used after staging program data+tag). Init value `0x620024` (`nb_l2_init` `0x08003420`). |
| `+0x10` | **BUF_STATUS** | R | `nb_l2_buf_level(4)` `0x08003484` returns bits[19:16] = buffer-4 **fill level in 64-B chunks** (0..8). Read path waits `!=0` per 64-B chunk (and `FUN_080ee468` waits `==8` for a full sector); program path waits `==0` (drained). Buffers >4 use single bits (`(val>>(buf+15))&1`). |

**Buffer 4 = `0x08006800`, 512 bytes, circular** (Q2 answer): the read loop copies 64-B
chunks from `0x08006800 + ((i*64) & 0x1ff)` — mask literal `0x1ff` at `0x07ffaa24`. It is
**not** a full-page buffer: a 2048-B page moves as 4 × 512-B sector transactions (+ one
short tag transaction). Transfers >512 B wrap: the producer raw read (514 B/sector)
leaves its last 2 spare bytes at window offsets 0–1, which `FUN_080ee468` fetches
separately after ready — i.e. the controller writes the payload **circularly** into the
window. The NFTL tag (8 B) is likewise staged/served at window offset 0.

---

## 4. Register-level op sequences (Q3)

All ops are bracketed by `nb_nfc_acquire`/`release` (`0x08002e24`/`0x08002e64`): clock
gate #2 on/off (`0x08003798`-area helpers on sysctrl `0x04000074`) + pin-share push/pop
(`[0x04000034]=0` = NAND pins, save/restore stack at `0x08008c94`). One-time HW init
(nandboot entry, `0x07ffec70`): `nb_l2_init` (`[0x04010000]=0x620024`) +
`nb_nfc_clock_init` `0x08002f6c` (`[0x04000074]|=1`, clock divider
`[0x04036000]=(div-1)|0x30000000|0x600000`) + probe (§5.1).

### 4.1 READ page (+tag) — `nb_nand_read_page` rt `0x0800260c` (file `0x07ffa60c`)

```
1  [+0x158] &= ~0x4000
2  CMD_LIST: 0x64 (cmd 0x00) · 2 col cycles (col=param_3) · N row cycles (row=param_2)
             · 0x18464 (cmd 0x30 + wait R/B) · 0x201 (wait + LAST)
3  GO:  [+0x158] = 0x40000200 | CE          (nb_nfc_go(param_1), param_1 = chip idx)
4  poll [+0x158] bit31 (nb_nand_ready)
5  per sector s = 0 .. nsec-1  (nsec = pagesize/512 + 1; the LAST "sector" is the TAG,
                                len = taglen (8), ecc mode = tagdesc[+0xc]):
   a  [+0x158] &= ~0x4000
   b  [0x0405B000] = len<<7 | mode<<21 | 0xc100012 ;  then |8   (engine start)
   c  [+0x100] = (len+parity-1)<<11 | 0x119 ;  L2 ctrl (4,0)
   d  GO (re-trigger, single-word list)
   e  data sectors: 8× { poll L2 level != 0 ; memcpy 64 B from 0x08006800+((i*64)&0x1ff) }
      (timeout ~0xf000 spins → "NF:9" → retry)
   f  poll [+0x158] bit31
   g  tag sector only: memcpy taglen B from 0x08006800 → *tagdesc
   h  wait ECC: poll [0x0405B000] bit6, then bit25; W1C bit6   (nb_ecc_wait_ack(0x2000000))
   i  classify: bit26 → OK · none → apply ECC_CORRECT[i] entries · bit27 → all-0xFF
      erased check (≤3 zero bits → force buffer to 0xFF) else Hynix read-retry
      (§5.2) and restart, ≤0x20 rounds then return bit31 (fail) / bit30 (corrected)
   j  if randomizer enabled (state[+4]==1) and not the tag: descramble buffer in place
      (XOR table 0x080079f4, column cursor state[+8], nb_scramble_copy 0x08001a60)
```

Tag-only reads (block scan, `0x0800f9cc` → r3=0) are the same with **one** short
data transaction (taglen+parity) — the column cycles then address the spare area.

### 4.2 PROGRAM page (+tag) — `nb_nand_program_page` rt `0x08002044` (file `0x07ffa044`)

```
1  bounds check (0x08001744), [+0x158] &= ~0x4000
2  CMD_LIST: 0x40064 (cmd 0x80) · 2 col · (N-1) row cycles (0x62) ·
             last row cycle (rowbyte<<11)|0x63 (LAST)
3  GO · poll ready
4  per sector s = 0 .. nsec-1 (last = TAG, len 8):
   a  [0x0405B000] = len<<7 | mode<<21 | 0x100015 ; |8
   b  [+0x100] = (len+parity-1)<<11 | 0x129 ;  GO
   c  per 512-B chunk: memcpy caller→0x08006800 (scrambled via 0x08001a60 if state[+4])
      then poll L2 level == 0 (drained)
   d  tag sector: memcpy tag → 0x08006800
   e  L2 ctrl (4,1)  = flush strobe · poll [+0x158] bit31 · L2 ctrl (4,0)
   f  wait ECC: bit6 + bit24, W1C          (nb_ecc_wait_ack(0x1000000))
5  CMD_LIST[0] = 0x8065 (cmd 0x10 + LAST) · GO · poll ready
6  status loop (§4.4): need bit7 (ready); bit6 set → check bit0: 1 = PROGRAM FAIL
   ("NF:3", error handler), return bit0
```

### 4.3 ERASE block — `nb_nand_erase_block` rt `0x08002af8` (file `0x07ffaaf8`)

> ⚠ **Identity correction:** rt `0x08002af8` is easy to mistake for a "READ OOB/spare"
> primitive; it is not. It issues NAND **0x60/0xD0 = BLOCK ERASE** (Proven: literals
> `0x30064`/`0x68064`,
> payloads 0x60/0xD0). Callers: device erase leaf `FUN_0800faa4` (`0x08002af8(cmd,
> block*pages_per_block)`), `flash_program_region` `0x0803cc80`, `FUN_080edc44`, and the
> nandboot erase-region wrapper. rt `0x080007d8` (file `0x07ff87d8`) is a REGION-erase
> wrapper (erase + bad-block bitmap bookkeeping) that calls it per block. There is no
> separate OOB-read primitive — spare/tag reads go through `0x0800260c` with r3=0.

```
1  [+0x158] &= ~0x4000
2  CMD_LIST: 0x30064 (cmd 0x60) · N ROW cycles only (0 col cycles; row = page address
             of the block, callers pass block*pages_per_block) · 0x68064 (cmd 0xD0) ·
             0x201 (wait R/B + LAST)
3  GO · poll ready
4  status loop: bit7 else fatal "NF:12"; bit6 → bit0 set = erase FAIL "NF:13"
   → retry whole sequence ≤16×, return 1 on give-up, else 0
```

### 4.4 STATUS — `nb_nand_status` rt `0x08002efc`

```
CMD_LIST: 0x38064 (cmd 0x70) · 0xc00 (tWHR delay) · 0x59 (read 1 byte → DATA_RD0, LAST)
GO+wait (0x08002de4) ;  return [+0x150] & 0xff
```
NAND status byte: bit7 = not-write-protected/ready, bit6 = ready, bit0 = fail.
Firmware requires **bit7 set** (else fatal), **bit6 set** to proceed, **bit0 clear** for
pass — i.e. a passing status byte is `0xC0`.

### 4.5 Other ops (all Proven)

- **RESET** `nb_nand_reset` rt `0x08001de8`: 4× { clear bit14 · `0x7f864` (cmd 0xFF) ·
  `0x27401` (timed wait + LAST) · GO+wait }.
- **READ-ID** `nb_nand_read_id` rt `0x08001e34`: `0x48064` (cmd 0x90) · `0x62` (addr 0x00)
  · `0xc00` · `0x3859` (read 8 bytes → `+0x150`/`+0x154`, LAST) · GO+wait; returns
  `[+0x150]`, stores `[+0x154]` → state`+0x14`.
- **COPYBACK** `nb_nand_copyback` rt `0x080023bc` (caller: dev leaf `FUN_0803d868`):
  `0x64` (cmd 0x00) · col+row(src) · `0x1a864` (cmd 0x35) · `0x200` (wait) · `0x42864`
  (cmd 0x85) · col+row(dst) · `0x8064` (cmd 0x10) · `0x201` — one GO, then status loop
  (bit0 fail → retry ≤50, "NF:c").
- **Hynix read-retry** (only when state`+3`==1, i.e. Hynix ID matched):
  get `0x08001ae4` (cmd 0x37 + per-register addr/`0xc00`/`0x59` reads);
  set-next `0x08001cac` (cmd 0x36, per register: addr cycle `0x63`, then raw 1-byte
  data-write `0x100094/0x10009c` engine + `0x129` micro-op with the byte staged at
  `0x08006800`, L2 (4,0), poll engine bit6; final cmd 0x16 `0xb065`). Retry value tables
  at `0x08007df4`/`0x08007e0c`, register-address tables `0x080079ec`/`0x080079f0`.

---

## 5. Probe, config, randomizer, state

### 5.1 Probe/init — `nb_nand_probe_init` rt `0x08002c18` (file `0x07ffac18`)

Called only from the nandboot boot path (`0x07ffed48`). Sequence: RESET; 4× READ-ID and
compare `[+0x150]` with the static flash-config id at **`0x0800003c`** (nandboot image
`+0x3c`); requires 4/4 matches else fatal `"…"`+hang. Then programs **TIMING0/1**
(`+0x15c/+0x160` ← cfg`+0x18/+0x1c`), fills the device struct, sets
`[0x080079e0]` = row-address-cycle count ← cfg`[0x11]` (= 3), and sets the
**randomizer flag** state`+4` = 1 iff **bit17 of cfg`[+0x14]`** is set
("Nand Randomizer function Enabled." string at `0x07ffad80`).

**Static config @`0x0800003c` (this .upd, header tag `ANYKANB1`):** id `0x9551D3EC`
(bytes EC D3 51 95 = **Samsung K9GAG08U0M**), pagesize `0x800`, pages/block `0x40`,
blocks `0x800`, `+0x11`=3 row cycles, flags `0xB0000001` (**bit17 = 0 → randomizer
OFF**), timing `0xF5AD1`/`0x40203`. Inside READ-ID there is a separate Hynix detect
(`0xAD DE 94 D2 04 43…` = H27UCG8T2A, `0xAD D7 94 DA 74 C3…` = H27UBG8T2B) that arms
read-retry (state`+3`=1) and swaps the retry tables — not taken for the Samsung id.

### 5.2 nandboot state @`0x08008CA8` (BSS — beyond the 0x7E80 image!) and dev @`0x08008CC4`

| addr | field | value after probe |
|---|---|---|
| state+0 | col-cycle nibble (cfg`[0x14]`>>4 &0xf) | 0 |
| state+1 | acquire refcount | 0 |
| state+2 | per-op bit-error count | scratch |
| state+3 | read-retry available (Hynix) | 0 |
| state+4 | **randomizer enabled** | **0** |
| state+5/+6 | retry index / retry counter | 0 |
| state+8 | scrambler column cursor (set per op: `(flags&0x200)+(row&0xff)*4`) | scratch |
| state+0xc/+0x10 | retry value/reg-addr table ptrs | unused (state+3=0) |
| state+0x14 | ID word 2 | — |
| dev+0x4 | cfg flags word | 0xB0000001 |
| dev+0x14 | pages per block | 0x40 |
| dev+0x1c | **page bytes** | **0x800** |
| dev+0x10 / +0xc / +0x18 / +0x8 | blocks / planes-derived / 1 / capacity | 0x800 / … |

**Note (Proven):** the state/dev structs live *past* the end of the nandboot image, so a
static image load leaves them **zero** (page size 0!). They are populated at runtime by the
nandboot probe/init (`0x08002c18`), which answers RESET/READ-ID with `0x9551D3EC` — PROG
itself never calls the probe (no PROG callers of `0x08002c18`); it runs once during boot.

### 5.3 Randomizer / "0x080016c8"

`0x08001a60` XORs data with the 1-KB table at `0x080079f4` (column-cursor keyed) — only
when state`+4`==1, which is **OFF** for this cfg, so on this build the data at the SRAM
boundary is plain. (If a future image enables bit17, sector bytes are XORed with
`table[(col++) & 0x3ff]`, col₀ = `(flags&0x200)+(row&0xff)*4`.)
Also: rt `0x080016c8` is easy to mistake for a "toggle ECC" routine; it is actually a
**cache/MMU page-attribute toggle** over the buffer range (per-4K bitmap at
`0x08008C20+0x8a4`) — unrelated to the NFC.

---
## 6. Mask-ROM cross-check (Proven)

`data/maskrom.bin` (captured pen ROM) drives the **same NFC IP one register bank lower**:
command list at `+0x00..`, CTRL/STATUS at **`+0x58`** (ready = bit31, GO =
`0x40000600` = `0x40000200|CE0<<10`), timing at **`+0x5c/+0x60`** (init `0xF5BD1`),
same micro-op encoding (`0x7f864` = cmd 0xFF, `arg<<11|0x401` timed wait, `0x5401`
default). ROM routines: `0x1960` ready-poll, `0x19a4` clock/pin/timing init, `0x1a48`
timing-set, `0x1aa4` reset-op. So either the ANYKANB2 controller decodes both banks or the
register bank moved +0x100 between generations — the mask-ROM `+0x00/+0x58/+0x5c/+0x60`
correspond to the PROG-era `+0x100/+0x158/+0x15c/+0x160`.

---

## 7. Proposed names / docstrings (Q5)

nandboot (rt = file+0x8000; apply via `tools/ghidra_rename.py` naming):

| rt | file | name | docstring |
|---|---|---|---|
| 0x08001744 | 0x07ff9744 | `nb_row_bounds_check` | Fatal "E11:" if row ≥ device capacity. |
| 0x08001a60 | 0x07ff9a60 | `nb_scramble_copy` | Randomizer XOR copy (table 0x080079f4, column cursor state+8). Inert here (state+4==0). |
| 0x08001ae4 | 0x07ff9ae4 | `nb_readretry_get` | Hynix cmd 0x37: read 4 retry registers → 0x08008cc0. |
| 0x08001c14 | 0x07ff9c14 | `nb_readretry_value` | Look up retry value[reg] for current retry index. |
| 0x08001cac | 0x07ff9cac | `nb_readretry_set` | Hynix cmd 0x36 (+raw byte writes) … cmd 0x16: advance read-retry set. |
| 0x08001de8 | 0x07ff9de8 | `nb_nand_reset` | NAND cmd 0xFF ×4 CE, timed wait. |
| 0x08001e34 | 0x07ff9e34 | `nb_nand_read_id` | Cmd 0x90, 8 ID bytes → [+0x150/+0x154]; Hynix detect arms read-retry. |
| 0x08001fdc | 0x07ff9fdc | `nb_unpack_descs` | (data,tag) descriptors → (nsec incl. tag, seclen, eccmode). |
| 0x080023bc | 0x07ffa3bc | `nb_nand_copyback` | Cmd 0x00/0x35 src → 0x85/0x10 dst, status check, ≤50 retries ("NF:c"). |
| 0x08002518 | 0x07ffa518 | `nb_ecc_apply_corrections` | Apply one ECC_CORRECT entry (bit flips) to the sector buffer. |
| 0x080025d4 | 0x07ffa5d4 | `nb_ecc_classify` | ECC status → 1 clean / 2 corrected (FIFO pending) / 3 uncorrectable. |
| 0x0800260c | 0x07ffa60c | `nb_nand_read_page` | (keep; update docstring: per-sector L2 window drain, ECC classify, read-retry.) |
| 0x08002044 | 0x07ffa044 | `nb_nand_program_page` | (keep; cmd 0x80/…/0x10, per-sector L2 stage + flush.) |
| **0x08002af8** | 0x07ffaaf8 | **`nb_nand_erase_block`** | **RENAME (was nb_nand_read_oob): NAND 0x60/0xD0 block erase, status fail-bit retry ≤16 ("NF:12/13").** |
| 0x08002c18 | 0x07ffac18 | `nb_nand_probe_init` | Reset + 4×read-ID vs cfg@0x0800003c, timing +0x15c/160, dev/state fill, randomizer flag. |
| 0x08002db4 | 0x07ffadb4 | `nb_nfc_ready` | [+0x158] bit31. (rename from nb_nand_ready: it is NFC-sequencer done, not NAND R/B.) |
| 0x08002dc8 | 0x07ffadc8 | `nb_nfc_go` | [+0x158] = 0x40000200 \| 1<<(10+ce): execute staged command list. |
| 0x08002de4 | 0x07ffade4 | `nb_nfc_go_wait` | go + poll ready. |
| 0x08002dfc | 0x07ffadfc | `nb_nfc_addr_cycles` | Emit 2 column + N row address micro-ops. |
| 0x08002e24/64 | 0x07ffae24/64 | `nb_nfc_acquire`/`release` | clk gate 2 + pin-share push/pop + refcount. |
| 0x08002e8c | 0x07ffae8c | `nb_ecc_wait_ack` | Poll ECC bit6 + dir bit, W1C bit6, return status. |
| 0x08002eb8 | 0x07ffaeb8 | `nb_data_phase_issue` | ECC engine word (+start) + NFC data micro-op (count-1)<<11\|0x119/0x129. |
| 0x08002ee0 | 0x07ffaee0 | `nb_ecc_parity_bytes` | mode<4 ? 7m+7 : 14m−14 parity bytes / sector. (was misdescribed as "cmd issue".) |
| 0x08002efc | 0x07ffaefc | `nb_nand_status` | (keep.) |
| 0x08002f2c | 0x07ffaf2c | `nb_nfc_put_addr` | words 0x62\|byte<<11. |
| 0x08002f6c | 0x07ffaf6c | `nb_nfc_clock_init` | sysctrl 0x74 bit0 + divider @0x04036000. |
| 0x0800339c/3e4 | 0x07ffb39c/3e4 | `nb_pinshare_push`/`pop` | [0x04000034] save/0/restore, stack @0x08008c94. |
| 0x08003420 | 0x07ffb420 | `nb_l2_init` | [0x04010000] = 0x620024. |
| 0x0800344c | 0x07ffb44c | `nb_l2_buf_ctrl` | (buf,op): bits[10:8]=buf, [15:12]=op(1=flush), bit11 strobe. |
| 0x08003484 | 0x07ffb484 | `nb_l2_buf_level` | [+0x10] bits[19:16] = buf-4 fill, 64-B units. |
| 0x080034b4 | 0x07ffb4b4 | `nb_l2_buf_addr` | buf idx → 0x6000/0x6200/…/0x6800/0x6a00… |
| 0x080007d8 | 0x07ff87d8 | `nb_erase_region` | Erase wrapper: per-block nb_nand_erase_block + bad-block bitmap. |

PROG: `FUN_080ee224` → `fw_nand_program_page_direct` (producer info/page writer, same
registers), `FUN_080ee468` → `fw_nand_read_page_direct` (raw 512+2 reads, needs L2
level==8), `FUN_080edc44` → `fw_nand_erase_blocks_direct`, `FUN_0800faa4` →
`dev_erase_block_leaf`, `FUN_0803d868` → `dev_copyback_leaf`.

### Clarifications on easily-confused ops
1. **0x08002af8 is BLOCK ERASE**, not an OOB/spare read (§4.3) — spare/tag reads go through
   `0x0800260c` with r3=0.
2. "DMA" in the NAND paths = CPU memcpy + controller streaming; there is no DMA unit (§0).
3. `bic [+0x158] 0x4000` is a status-clear, not an HW-ECC arm (§1); ECC config is the
   0x0405B000 engine word (§2).
4. "internal opcode 0x63/0x64" = micro-op types (addr/cmd cycle), not NAND opcodes (§1).
5. `0x080016c8` "ECC toggle" = cache/MMU page-attribute toggle (§5.3).
6. `0x08002ee0` computes ECC parity size; the per-sector issue is `0x08002eb8` (§2).
