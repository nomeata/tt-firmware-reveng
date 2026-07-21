# The ANYKANB1-vs-ANYKANB2 nandboot generation puzzle

Read-only investigation. Each claim is marked **Proven** (byte/disasm evidence cited inline)
or **Inferred** (reasoning from the evidence). The pen's mask ROM and the live-pen RAM/ROM
dumps referenced below are **hardware captures**, not included in this repository; the analysis
describes them.

## The puzzle (restated)

The pen's on-chip mask ROM guards the storage-boot path with an 8-byte magic and
accepts **only `"ANYKANB2"`**. The field-update copy of the nandboot/SPL
(`data/nandboot.bin`, extracted from `update3202MT.upd`) carries
**`"ANYKANB1"`** at +0x20. Yet a live pen RAM dump proves the pen is running the
*same nandboot code we have*. How can an ANYKANB1-stamped image run under a mask
ROM that demands ANYKANB2?

**Resolution (Proven in aggregate): the 8-byte magic is an isolated header/version
stamp, cleanly separable from the code. The pen's on-NAND boot image is
`ANYKANB2 header + shared SPL code`; the update file is `ANYKANB1 header + the same
code family`. The mask ROM is satisfied by the pen's ANYKANB2 header (which cannot
be read directly), then runs the shared code — which is why it matches ours.**

---

## 1. Structure of `nandboot.bin` — the magic is an isolated header field

File: `data/nandboot.bin`, 0x7e80 (32384) bytes.

- **Proven** — it is byte-identical to the boot/SPL section of `update3202MT.upd`
  (container `ANYKA106`, boot section at .upd offset 0x20000, size 0x7e80). Verified
  equal, 32384/32384 bytes.

Layout (Proven by hexdump):

| Offset | Contents |
|-------|----------|
| 0x00–0x1f | ARM exception vector table — 8 branch words (`2000 00ea` = `b 0x88`, `4000 00ea`, `0ef0 a0e1`, …). |
| **0x20–0x27** | **Magic: `41 4e 59 4b 41 4e 42 31` = "ANYKANB1".** |
| 0x28–0x37 | Fixed structural header bytes: `0419 0202 0300 3000 5124 0600 1313 0600`. |
| 0x38–0x52 | Per-flash-chip geometry/timing fields (`0500 0500 ecd3 5195 0008 4000 0020 0010 0008 4002 …`). |
| 0x53–0x72 | ASCII NAND part name: `"Samsung K9K8G08U0M "`. |
| **~0x7c →** | **Code begins** (`98f1 9fe5` = `ldr pc,[pc,#0x1f8]` region; `9801 9fe5`, `0ef0 a0e1`, …). |

- **Proven** — `"ANYKANB"` occurs exactly **once** in the file, at offset 0x20
  (single hit at byte 32). The magic is a self-contained 8-byte field, not embedded
  in any instruction stream.
- **Proven** — the byte-matched region cited in the puzzle (~0x7798) is code far
  past the header. `nandboot.bin@0x7798 = 04109fe5 000191e7 1eff2fe1 a0790008`
  (ARM: `ldr r1,[pc,#4]`, `ldr r0,[r1,r0]`, `bx lr`, then a data word). This is
  ≈0x7700 bytes *after* the magic — unambiguously code/data body, structurally
  independent of the 8-byte stamp at 0x20.

Conclusion: **the magic is trivially separable from the code** (Proven). Swapping
"ANYKANB1"→"ANYKANB2" is an 8-byte header edit that touches no instruction.

---

## 2. Cross-generation nandboot comparison — magic differs, header+code shared

Each `.upd` is an `ANYKA106` container; the boot/SPL section is at container offset
u32@0x20 (size u32@0x24). Extracted and compared:

| Update file | Boot off / size | Magic @ boot+0x20 | Fixed hdr 0x28–0x37 | NAND part name |
|---|---|---|---|---|
| `update.upd`        | 0x1d000 / 0x6fe4 | **ANYKANB0** | `0419 0202 0300 3000 …` | Samsung K9G8G08U0A |
| `Update3202.upd`    | 0x1e600 / 0x7020 | **ANYKANB1** | `0419 0202 0300 3000 …` | Samsung K9K8G08U0M |
| `Update3202_dl.upd` | 0x1e600 / 0x7020 | **ANYKANB1** | (same as above) | Samsung K9K8G08U0M |
| `update3202MT.upd`  | 0x20000 / 0x7e80 | **ANYKANB1** | `0419 0202 0300 3000 …` | Samsung K9K8G08U0M |
| `Update3203L.upd`   | 0x15400 / 0x1b08 | **ANYKANNL** | `0419 0202 0300 3000 …` | Samsung K9G8G08U0A |

**Proven observations:**

- **Same header skeleton across all four generations.** Every boot section has the
  identical vector-table + `magic@0x20` + fixed structural bytes
  `0419 0202 0300 3000` at 0x28. The only header differences are (a) the 8-byte
  magic (`…B0` / `…B1` / `…NL`) and (b) the per-flash-chip geometry/part-name
  fields — which track the physical NAND (K9K8G08U0M vs K9G8G08U0A), not the CPU
  generation. (A substring grep "misses" the NL image only because its magic is
  `ANYKANNL`, which does not contain `ANYKANB`; the header is otherwise the same
  shape — Proven by hexdump at .upd 0x15420.)

- **The code body is shared, just relocated between builds.** A 16-byte code
  signature taken from the matched region (`1eff2f11 740091e5 660fc0e3 440f80e3`,
  at MT boot 0x7780) is found verbatim in `Update3202.upd`'s boot at 0x3954 — the
  same routine, at a different offset (the MT build is larger, 0x7e80 vs 0x7020,
  and lays code out differently). Longest single common run: MT↔Update3202(B1) =
  0x1400 (5120 B); MT↔update.upd(B0) = 0xcc2 (3266 B). k-mer coverage (≥16-byte
  matched runs) of the MT image found in the others: **≈69% shared with the B1
  sibling, ≈43% with the B0 image**, confirming large identical code regions
  across the `…B0`/`…B1` generations. (`Update3203L.upd`/NL is a smaller, more
  divergent SPL — 0x1b08 bytes — but shares the same header skeleton.)

- **The update tooling is explicitly ANYKANB2-aware.** Both `Update3202.upd`
  (@0x1d984) and `update3202MT.upd` (@0x1ecfc) contain a second, distinct
  `"ANYKANB2"` occurrence inside the *producer/flashing* section, followed by a
  table of `0x040000xx` internal-SRAM addresses — i.e. the production tool carries
  an ANYKANB2 reference (a chip-ID/relocation descriptor), not just the ANYKANB1
  boot stamp. (Consistent with the producer's only `"ANYKANB2"` use being a
  chip-ID check, and its writing the SPL to the boot blocks verbatim without
  re-stamping the magic.) **Proven** the string exists; **Inferred** that it
  reflects the tooling being multi-generation-aware.

Conclusion: **the magic is a per-chip-generation version stamp; the surrounding
header structure and the bulk of the SPL code are shared across B0/B1 (and the NL
header skeleton matches too)** (Proven for header; Proven-large-fraction for code).

---

## 3. Mask ROM confirmation — it is the pen's, and the ANYKANB2 gate is real

The mask ROM analysed here is a **hardware capture** (65536 bytes, `maskrom.bin`),
not included in this repository; identical to the `2N-Update3202` capture (md5
`6f00bfe7…`).

- **Proven** — identity string at 0x6204: `"SNOWBIRD2-BIOS>#"`. This is the
  SNOWBIRD2 boot ROM.
- **Proven** — `"ANYKANB"` occurs exactly **once**, at 0x6224, and the 8 bytes
  are `41 4e 59 4b 41 4e 42 32` = **"ANYKANB2"**. It is immediately followed by the
  error strings `"wrong1\n"` (0x622c) and `"wrong\n"` (0x6234) — the magic-mismatch
  reject path. So `"ANYKANB2"` is the single accepted magic in this ROM. (The
  detailed PATH1 `image+0x20` / PATH2 `image+4` comparison logic was established by
  the prior disasm pass; this confirms the string it compares against.)

- **Proven — this is genuinely the pen's silicon ROM, not a mismatched dump.** The
  live pen ROM dump (65532 bytes) equals `maskrom.bin[4:]` with **0 differing bytes**
  over all 0xfffc overlapping bytes. The dump simply starts 4 bytes into the ROM
  (65536−4 = 65532; the identity string sits at dump 0x6200 vs ROM 0x6204). So the
  captured `maskrom.bin` = the pen's actual mask ROM, and its ANYKANB2 gate is the
  real gate on this pen.

- **Corroborating** — a one-byte-patched variant of the ROM differs in exactly
  **one byte, at 0x622b**: `'2'`(0x32)→`'1'`(0x31), i.e. its gate reads `"ANYKANB1"`.
  This pinpoints the entire generation gate to that single magic byte, and shows a
  prior experiment already needed a 1-byte ROM patch to make an ANYKANB1 image boot.
  (**Proven** the 1-byte delta.)

---

## 4. Pen is running the shared code (grounding the byte-match)

- **Proven** — a dump of runtime 0x08007000…0x08007fff (32768 B; a hardware capture)
  compares to `nandboot.bin[0x7000:]` (runtime 0x08007000..0x08007e80): **only 16
  differing bytes over 0xe80**, all in the tail data region (scattered at runtime
  0x080079d4, 0x08007e28, 0x08007e74, …) — i.e. runtime-mutated variables, not code.
  The cited code word at runtime 0x08007798 matches exactly
  (`04109fe5 000191e7 1eff2fe1 a0790008` on both).

The pen's on-NAND boot *header* itself (runtime 0x08000000..0x08007000) was
unmapped/absent in the RAM dump, so the pen's stored magic cannot be read directly.
But the code identity at 0x08007798 (and across the whole 0x7000..0x7e80 window) is
solid.

---

## 5. Resolution

**Shared code + differing header magic** (Proven in aggregate):

1. The 8-byte magic at boot+0x20 is an **isolated header/version field**, separable
   from the code (§1: single occurrence at 0x20; matched code is ~0x7700 bytes
   later). *Proven.*
2. Across generations the **header skeleton is identical and the SPL code body is
   largely shared**; only the magic (`…B0/…B1/…NL`) and per-NAND geometry fields
   change (§2: identical `0419 0202 0300 3000` header, 69%/43% code coverage,
   same routine found relocated). *Proven for header; large-fraction Proven for
   code.*
3. The pen's mask ROM is exactly the captured `maskrom.bin` (§3: `mrom dump ==
   maskrom.bin[4:]`, 0 diffs) — a SNOWBIRD2 BIOS whose sole accepted magic is
   `"ANYKANB2"`. It is not the wrong ROM, and there is **no** alternate accepted
   magic (the only `ANYKANB` string in the ROM is `ANYKANB2`). *Proven.*
4. The pen is running the shared SPL code (§4: 0x08007798 and the whole tail window
   match our nandboot). *Proven.*

Therefore: the physical pen is an **ANYKANB2** part (SNOWBIRD2). Its factory NAND
was flashed with an **ANYKANB2 boot image = ANYKANB2 header + the shared SPL code**.
`update3202MT.upd` is the **ANYKANB1** (sibling-generation) release of the same
firmware family; its boot image = **ANYKANB1 header + the same code**. The mask ROM
checks the header magic on the pen's on-NAND image → sees `ANYKANB2` → passes →
runs the code → which is byte-for-byte the code we hold, because the two only differ
in the 8-byte stamp (and per-chip geometry) at the very top of the header.

**Where the pen's ANYKANB2 header came from (Inferred):** an ANYKANB2 factory/OTP
boot image for the SNOWBIRD2 silicon — the generation-correct sibling of the
ANYKANB1 image in the field-update file. Consistent with the field producer writing
the SPL to the boot blocks *without re-stamping the magic*: the generation stamp is
set at production time to match the silicon, and the shared code is what actually
executes. (The pen's stored magic byte cannot be directly confirmed because that
NAND/RAM region was not captured; hence this last step is Inferred rather than Proven.)

### Not the explanation
- *Wrong mask ROM?* No — pen ROM == `maskrom.bin[4:]` exactly (§3).
- *A second accepted magic / bypass?* No — `ANYKANB2` is the only magic string in
  the ROM (§3); the 1-byte patch shows the gate is that single byte and had to be
  patched to accept `ANYKANB1` (§3).
