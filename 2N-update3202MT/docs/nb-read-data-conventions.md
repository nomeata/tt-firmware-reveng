# NB_READ_DATA (0x0800260c) — call convention and per-call-site layout

Function: `nb_nand_read_page` @ nandboot `0x07ffa60c` = runtime **`0x0800260c`**
(nandboot base 0x07ff8000 ↔ runtime 0x08000000, so +0x260c). It is the READ twin of
the page-program op `nb_nand_program_page` @0x07ffa044 (rt `0x08002044`) — same helpers,
opposite streaming direction (SRAM→caller here vs caller→SRAM in program). See
[`nftl-write-consistency.md`](nftl-write-consistency.md) §1.

Decomp: `0x0800fb00` (data leaf), `0x0800f9cc` (tag leaf), `0x08030da4` (main-fw data
read), `0x0800fe08` / `0x0800fde4` (descriptor builders), `0x07ffa044` (program mirror,
confirms the descriptor consumption).

---

## 0. TL;DR

**Is NB_READ_DATA called with two incompatible conventions?**
**No — there is exactly ONE convention.** All three call sites use the same
`(r0,r1,r2,r3,sp[0])` signature. `r0` is a NAND **command/CE arg**, never a data buffer.
The data buffer is **always** in the **r3 descriptor** (`*(r3+0)`), or r3 is NULL for a
pure tag read. Any reading in which "the block-scan passes the data buffer in r0" is a
misread — `r0` holds a command value (often `0`, or a small value like `0x8600` which is in
fact the *row* passed in r1), and the receiving buffer is `*(r3+0)`.

---

## 1. THE signature (one convention) — `NB_READ_DATA(r0,r1,r2,r3,sp[0])`

From `nb_nand_read_page` (0x07ffa60c), args = `(param_1..param_4, param_5)`:

| reg    | param   | meaning | evidence |
|--------|---------|---------|----------|
| **r0** | param_1 | NAND **command / chip-enable** arg (`uStack_34`). Issued via `func_0x07ffadc8(param_1)`. **Not a buffer.** | lines 36,52,72,156 |
| **r1** | param_2 | **row address** (page/sector row) `uStack_30`. Column/size derived: `(param_2 & 0xff)*4`. | lines 37,42,49 |
| **r2** | param_3 | size/flags word `uStack_2c` (carries the `0x200` sector-size bit + column). | lines 38,42,49 |
| **r3** | param_4 | **DATA descriptor pointer** (or **NULL**). | see §2 |
| **sp[0]** | param_5 | **TAG/OOB descriptor pointer** (or **NULL**). | see §2 |

Return: bit31 set = fail (`& 0x80000000`); bit30 = corrected; low byte = bit-error count.
Callers treat `(ret & 0x80000000)==0` as success.

## 2. Descriptor layouts (the r3 and sp[0] structs)

**DATA descriptor** (r3 → built by `FUN_0800fe08` for the read leaf; `local_1c..` in
`FUN_08030da4`). Consumed in `nb_nand_read_page`: `unaff_r4 = param_4[2]*i + *param_4`
(line 74), DMA `SRAM 0x08006800 → unaff_r4` in 64-B chunks (line 88):

| offset | field | note |
|--------|-------|------|
| +0x00  | **data buffer base ptr** | `*param_4` — dest of the read |
| +0x04  | page/total bytes | `*(dev+0x1c)` (page size) |
| +0x08  | **per-sector stride** | `param_4[2]` (0x200, sometimes 0x400) |
| +0x0c  | ecc-mode byte | |

**TAG descriptor** (sp[0] → built by `FUN_0800fde4`). Consumed: `uStack_44 = param_5[1]`
(len), then `func_0x07ffb118(*param_5, OOBscratch, param_5[1])` (lines 63,95):

| offset | field |
|--------|-------|
| +0x00  | **tag/OOB buffer ptr** (`*param_5`) |
| +0x04  | **tag length** (`param_5[1]`) |
| +0x08  | tag length (copy) |
| +0x0c  | ecc-mode byte |

## 3. The three call sites — all ONE convention

**(A) `nand_read_page_tag_leaf` @0x0800f9cc — "block-scan tag op" (mtd_init scan):**
```
func_0x0800260c(param_2,  row,  count,  0,      TAGdesc)   ; r3 = 0 (NO data buffer)
                 r0        r1    r2      r3      sp[0]
```
Pure spare/tag read. `r3 == 0` → no data DMA. Tag goes to `*(sp[0]+0)`, len `*(sp[0]+4)`.
This is the site sometimes misread as "data buffer = r0"; it actually has **no data
buffer** and `r0 = param_2` (a command value).

**(B) `FUN_0800fb00` @0x0800fb90 and @0x0800fbd8 — the data/metadata read leaf:**
```
func_0x0800260c(param_2, param_4, 0, local_30, auStack_40)  ; data via r3, tag via sp[0]
                 r0       r1(row) r2  r3=DATA   sp[0]=TAG
```
- `r1 = param_4 = *(dev+0x14)*block + off` (the row; `*(dev+0x14)` = 256 sectors/block).
- `r3 = local_30` (DATA desc). At **0x0800fb90** (unaligned path, `FUN_0800fe40`==0),
  `local_30[0]` is first overwritten (line 34) to a **bounce buffer**
  `*(DAT_0800fcb8+8)`; after the call the firmware itself copies bounce→real
  (`func_0x08003118`, line 37). At **0x0800fbd8** (aligned) `local_30[0]` = the real
  buffer `param_5`. **In both cases the intercept just writes to `*(r3+0)`** and the
  firmware handles any bounce copy — no special-casing needed.

**(C) `FUN_08030da4` @0x08030da4 (main firmware) — data read, no tag:**
```
func_0x0800260c(0, row, count, &local_1c, 0)   ; r0 = 0, data via r3, sp[0] = 0
                r0  r1   r2     r3=DATA    sp[0]
```
`local_1c` = the caller buffer (`param_4`). Confirms `r0` is not a buffer (it's 0 here).

Across (A)/(B)/(C): `r0 ∈ {param_2, param_2, 0}` — **never a data buffer**. Data is
**always** `*(r3+0)` (when r3≠0); tag is **always** `*(sp[0]+0)` (when sp[0]≠0).

## 4. Resolving the destination buffer

There is no caller-LR test and no "which convention" test to make — the one convention is
driven entirely by the descriptor pointers:

- `r1` = row (sector row; block = `r1 // 256`).
- `r3` = DATA descriptor ptr (0 = no data). When non-null, the buffer base is `*(r3+0)`,
  total bytes `*(r3+4)`, per-sector stride `*(r3+8)` (= sector size 512). Sectors this call
  = `total/stride` (== 1 for a 512-B MBR read).
- `sp[0]` = TAG descriptor ptr (0 = no tag). When non-null, tag buffer = `*(sp0+0)`,
  length `*(sp0+4)` (≤ 64).
- `r0` (command) and `r2` (size/flags) are **not** destinations.

So all three call sites resolve identically: data lands at `*(r3+0)` (row `r1`), the spare
tag at `*(sp0+0)`; the pure-tag block-scan site simply has `r3 == 0` (skip data, write tag).

## 5. The same op at the NFC-controller boundary

`nb_nand_read_page` talks to the Anyka NFC exactly like its program twin: read opcode
**`0x64`** to the NFC cmd block at `0x0404A000+0x100..` (row/column latched via
`func_0x07ffadfc`), the page staged through SRAM `0x08006800` (streamed SRAM→caller in
64-B chunks), completion signalled by the RB-ready poll `0x08002db4` and the status read
`0x08002efc` (`[0x0404A000+0x150]&0xff`, ready+pass = `0xC0`). The full register-level
sequence is documented in [`nfc-controller-registers.md`](nfc-controller-registers.md) §4.1.

---

### Aside — an easily-confused address
`0x08002044` is **not** a read: it is `nb_nand_program_page` (a WRITE / page-program), the
write twin of `0x0800260c` (see [`nftl-write-consistency.md`](nftl-write-consistency.md)
§1.2). Treating it as a read would overwrite the caller buffer on a program call.
