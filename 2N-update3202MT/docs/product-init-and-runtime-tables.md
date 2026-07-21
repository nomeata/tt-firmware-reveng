# Product-init & runtime dispatch tables

Static reverse-engineering of the tiptoi 2N (MT) pen firmware's runtime QHsm dispatch tables and the
cover-OID selectors, using the **static PROG.bin** for how they are built and cross-checked against a
**live-pen RAM capture** as ground truth. That RAM capture is a **hardware capture**, not included in
this repository; the analysis describes it.

Evidence tags: **[Proven]** = read directly from a capture / PROG.bin / disasm. **[Inferred]** = deduced
from structure, not byte-confirmed on hardware.

## 0. The dispatch tables are compile-time-constant .data

A live-pen RAM capture covers the low working RAM [0x08007000, 0x08009000) (the **AO struct** @0x08008874,
`game_ctx` @0x080089a4) and the high .data region [0x0811f000, 0x08127000) (the **state-descriptor table**
@0x08121d44). Cross-checked against the flat-loaded `PROG.bin` (base 0x08009000, file off = addr −
0x08009000):

- In `PROG.bin`, `descTbl[9]` is **already 0x0800b55c**, and `transAct[5]` is **already 0x08037cec**.
- The descriptor table read back from the live pen matches `PROG.bin` **70/70 entries, ~99.9%
  byte-identical** over [0x0811f000, 0x08127000) — only ~30 bytes differ, all genuine runtime-written
  handler/flag fields (§6). [Proven]

So the dispatch tables are **compile-time-constant .data, installed by the C-runtime data-init
(`__main`/scatter-load) at cold boot** — not "runtime-built from a product scan." This is the flat-load
model: Ghidra addr == runtime addr, and the constant tables are present in the image. The **cover-OID
selectors** are the exception — they are `.bss`, seeded at runtime by product code (§4).

---

## 1. Runtime AO struct & dispatch tables (Q1) [Proven]

The active object is at **0x08008874** (low working RAM). Live fields from the RAM capture:

| field | addr | value | meaning |
|---|---|---|---|
| AO+0x04 | 0x08008878 | 0x00000003 | current leaf state id = **3 (STANDBY)** |
| AO+0x08 | 0x0800887c | 0x08007f04 | current-state work ptr |
| AO+0x14 | 0x08008888 | 0x08007ea4 | state instance ptr |
| **AO+0x18** | 0x0800888c | **0x08121d44** | **descriptor table** ptr |
| **AO+0x1c** | 0x08008890 | **0x0800b8d4** | **event-action table** ptr |
| **AO+0x20** | 0x08008894 | **0x0800b544** | **transition-action table** ptr |
| AO+0x24 | 0x08008898 | 0x00000030 | last event (0x30 timer) |

All three table addresses are confirmed unchanged on the real pen. The tables:

- **descriptor table @0x08121d44** — high .data, **70 entries × 4 B**, [0x08121d44, 0x08121e5c]. Each
  entry → a 0x10-B descriptor struct in rodata 0x080b0378–0x080b07b8 (e.g. `descTbl[0]=0x080b0398`,
  `[3]=0x080b0418` standby, `[12]=0x080b0488` mount, `[13]=0x080b0498` book, **`[9]=0x0800b55c`**).
- **event-action table @0x0800b8d4** — loaded code/rodata. **19 entries**: `[6]=0x08050d28`
  usb_connect_handler, `[9]=0x08051b0c` standby_handler, `[16]=0x0803440c`, **`[17]=0x0803629c`
  gme_oid_dispatch**. [Proven — PROG.bin]
- **transition-action table @0x0800b544** — same region. **6 usable entries [0..5]**, then entries [6..9]
  physically *are* the state-9 descriptor struct 0x0800b55c (§2): `[0]=0x0800acfc, [1]=0x0800ad80,
  [2]=0x0804fdbc, [3]=0x0800b56c, [4]=0x0800a9c8, [5]=0x08037cec`. [Proven]

---

## 2. State-9 / 0x1060 dispatch reality (Q2) — CONFIRMED [Proven]

- **descTbl[9] = 0x0800b55c** — confirmed on the real pen. This is the state-9 (global pre-handler)
  descriptor. Its 0x10 bytes are `{entry=0, exit=0, super=0, mapper=0x0800a914}` — and those four words
  *overlay* transAct[6..9], so `transAct[9]==0x0800a914==descTbl[9].mapper`. [Proven]
- **State-9 mapper 0x0800a914** disassembled: it sets **status `r1 = 1`** unconditionally
  (`mov r1,#1` @0x0800a934), then routes the event (r5) to a result index (r0). The decisive instructions
  at 0x0800a980: `sub ip,r5,#0x1000 ; subs ip,ip,#0x60` (ip = event − 0x1060); if zero → `mov r0,#5`
  (@0x0800a98c). So:

  **event 0x1060 → result 5, status 1 → transition-action[5] = 0x08037cec = `FUN_08037cec`** (the
  cover/product-OID classifier). It does NOT propagate (status-0). Other routes: 0x30→2, 0x1046→0,
  0x1054→1, 0x105e→3, 0x105f→4. [Proven — disasm 0x0800a914]

The dispatch levels themselves (pump `FUN_0800b4a4` → `sm_dispatch_to_hierarchy` 0x080f2d70 runs state 9
first, then the leaf, then state 10; `mov r0,#9/#10/#11` in nandboot 0x08006bc8/bd0/bd8) are proven in
`state8-to-13-transition.md §7`; the live capture confirms the table those levels read (descTbl[9],
transAct[5], eventAct[17]) matches static.

---

## 3. Cover-OID selectors (Q3) — values + what they are

The literal pool (PROG static) resolves the DAT globals to **.bss** addresses (all **zero in PROG** = not
loaded, runtime-seeded):

| selector | ptr (PROG literal) | target .bss addr | width | role (from `FUN_08037cec`) |
|---|---|---|---|---|
| `*DAT_080381a4` | 0x080381a4 → | **0x081da08c** | word | **product-family MODE selector** (6/7/8/9/0xb/0xe/0xf) |
| `*DAT_080381a8` | 0x080381a8 → | **0x081da716** | u16 | specific **cover-OID value** #1 for the product |
| `*DAT_080381ac` | 0x080381ac → | **0x081da718** | u16 | specific **cover-OID value** #2 for the product |

`FUN_08037cec` reads the tapped OID `akoid_buf+4` and accepts a cover only when the **mode** matches the
family: mode **6**⇒0xc21/0xc00, **8**⇒0xec4/0xeba, **0xf**⇒0x16c0/0x1600, **0xe**⇒0x162c/0x1600,
**7**⇒0xb7b/0xb00, **9**⇒0xa94/0xa00, **0xb**⇒0xa34/0xa00; plus a direct compare of the tap to
`*DAT_080381a8`/`*DAT_080381ac`.

> **Note (see §8):** these family/selector compares do **not** gate the `0x1058` post. They only set the
> *"tap belongs to another product"* flag `akoid_buf[0x4a4]` and the repeat-tap counters `+0x155/+0x156`.
> The first-load branch that posts `0x104a`+`0x1058` is independent of the selectors — a fresh cover tap
> works with all three globals **zero**.

The selectors are `.bss` and seeded at runtime; the exact per-product live values depend on the mounted
product (recovered mechanism in §4). **[Proven: addresses/roles; Inferred: the exact per-product live
values]**

---

## 4. The product-init that seeds the selectors (Q4, KEY DELIVERABLE) [Proven]

**The seeder is `gme_parse_header` @0x08035d20.** It opens the mounted product's header file (path at
booklist 0x081da080 `+0x30`; `fs_open(booklist+0x30,0,0)`) and `fs_read`s it field-by-field into the
.bss config block, **directly writing the cover-OID selectors**:

- `*DAT_08035328 = local_10;` → **0x081da08c = MODE selector** (the 6th 4-byte header field).
- `*DAT_080361d8 = (u16)local_10;` → **0x081da716 = cover-OID #1** (from the sub-table at header offset
  read from off 0x94, `fs_seek(...,0x94)` then `fs_seek(*,Lword)`).
- `*DAT_080361e0 = (u16)local_10;` → **0x081da718 = cover-OID #2**.
- Same function also seeds the neighbouring config words 0x081da094/098/1ac/1b0/1b8/088 and the OID
  sub-table 0x081da714/728. [Proven]

**Call path (who runs the seeder):**

```
pump FUN_0800b4a4 → sm_dispatch_to_hierarchy(0x1060)
  state 9  mapper 0x0800a914 : 0x1060 → transAct[5]=FUN_08037cec  → (cover match) post 0x1058
  standby(3) mapper 0x0804e884 : 0x1058 → ENTER state 12 (mount)
state 12 entry FUN_08034300:
  FUN_080ef820("B:/", booklist+0x30) ; FUN_080ad7c0(booklist+0x30,"*.bnl",…)   // scan B: for .bnl into booklist 0x081da080
  default event-action[16] FUN_0803440c → post 0x1059 → ENTER state 13 (book)
state 13 book: each product OID → event-action[17] = gme_oid_dispatch 0x0803629c:
  iVar11 = gme_mount_check_product(1,0)   // 0x08034130: validate .bnl (magic 0x238b, language, product-id)
  if (ok) {
    gme_parse_header();          // <<< SEEDS 0x081da08c mode + 0x081da716/718 cover OIDs from the header file
    gme_reset_registers();       // 0x08035ca8
    gme_parse_start_end_oid();   // 0x08035c3c
  }
```

`gme_parse_header`'s **only caller is `gme_oid_dispatch` 0x0803629c** (reachable only as event-action[17],
i.e. only once the AO is in state 13 with a product mounted). [Proven — sole caller]

**NAND/udisk inputs consumed to produce mode+selectors:**
1. **`B:/*.bnl`** product archives — scanned by state-12 entry `FUN_08034300` into the booklist struct at
   **0x081da080**; the chosen archive's path lands at `booklist+0x30`.
2. **the product header file** (opened inside `gme_parse_header`): the raw fields at file offsets 0x00..,
   0x60, 0x94, 0xa4 supply the **mode** (6th dword) and the **cover-OID sub-table** (0x081da716/718).
   Magic `0x238b`@8, language, product-id@0x14 are pre-validated by `gme_mount_check_product` 0x08034130.
3. (for playback, not selectors) archive-internal **`a:/oidfilelist.lst`** via `FUN_080ae2c0` 0x080ae2c0.

**The dispatch TABLES are NOT product-init.** descTbl/eventAct/transAct are constant .data installed by
the C-runtime data-init at cold boot, before `app_init_main` (0x08038f5c). `app_init_main` only does
peripheral init, `fs_storage_mount_init`, `akoid_init`, `sm_set_state(3)` — it does **not** build the
tables. [Proven]

---

## 5. Cold-boot install ordering [Proven]

Two things must have happened by the time a cover tap can be classified, both at cold boot:

1. **The tables are installed by the C-runtime `__main`/scatter-load**, which copies the high .data
   (0x0811f000+) and zeroes .bss — populating descTbl/eventAct/transAct at 0x08121d44 / 0x0800b8d4 /
   0x0800b544. A boot that enters mid-`app_init_main` (at 0x08039100) would bypass this and leave the
   tables zero, so `sm_dispatch_event` would index null pointers. [Proven — `app_init_main` does not build
   the tables]
2. The **selectors** (0x081da08c/716/718) are `.bss`, written **only** by `gme_parse_header` ←
   `gme_oid_dispatch` ← state 13, which requires a **real product to actually mount** (valid NFTL/FAT, a
   matching `B:/*.bnl` passing `gme_mount_check_product`). [Proven]

Note the ordering subtlety: the **first** open of a product is driven by `gme_mount_check_product`
validating the `.bnl` (file-based), and `gme_parse_header` seeds the selectors *during* that open; the
mode-selector matching in `FUN_08037cec` governs *subsequent* fast cover recognition. [Proven flow;
first-tap-vs-seed ordering Inferred]

---

## 6. Static image vs live RAM — the tables are constant

Cross-checking the descriptor region [0x0811f000, 0x08127000) between the flat-loaded static image and
the live-pen RAM capture: everything matches except ~30 bytes of small handler/flag fields (live fn-ptrs
and status bytes written at runtime). The **dispatch/descriptor tables themselves are NOT among the
differences** — further proof that they are constant .data, not runtime-computed. (The specific
runtime-written bytes come from a hardware RAM capture not included in this repository.)

---

## 7. Proven vs Inferred

| claim | status | evidence |
|---|---|---|
| AO@0x08008874 in low working RAM; AO+0x18/1c/20 = 0x08121d44/0x0800b8d4/0x0800b544 | **Proven** | live-pen RAM capture |
| descriptor table @0x08121d44, 70 entries, `descTbl[9]=0x0800b55c`, matches PROG 70/70 | **Proven** | RAM capture vs PROG |
| RAM capture == PROG static ~99.9% (~30 bytes differ); tables are constant .data | **Proven** | byte-compare |
| static PROG already has descTbl[9]=0x0800b55c and transAct[5]=0x08037cec | **Proven** | PROG.bin off 0x118d68 / 0x2558 |
| state-9 mapper 0x0800a914: 0x1060→result 5, status 1 → transAct[5]=0x08037cec | **Proven** | disasm 0x0800a914 |
| eventAct[17]=0x0803629c gme_oid_dispatch; transAct[5]=0x08037cec | **Proven** | PROG tables |
| selectors: 0x081da08c(mode)/0x081da716/0x081da718; all .bss (zero in PROG) | **Proven** | PROG literals 0x080381a4/a8/ac; .bss=0 |
| **`gme_parse_header` 0x08035d20 seeds 0x081da08c/0x081da716/0x081da718 from the product header file** | **Proven** | 0x08035d20; PROG literal targets |
| seeder call path: state13 `gme_oid_dispatch` check → `gme_parse_header`; sole caller | **Proven** | 0x0803629c; grep |
| NAND inputs: `B:/*.bnl` (state-12 scan) + product header file + `gme_mount_check_product` magic 0x238b | **Proven** | 0x08034300, 0x08034130, 0x08035d20 |
| tables installed by C-runtime `__main`/scatter-load at cold boot | **Proven (divergence) / Inferred (exact loader step)** | `app_init_main` 0x08038f5c does not build them |
| real per-product live selector values | **Inferred** | not in capture range |

---

## 8. Cover-OID selector seeding + first-load lifecycle (chicken-and-egg RESOLVED)

**Net answer:** there is **no boot-time product pre-scan** and none is needed. The cover classifier
`FUN_08037cec` does **not** require the selectors to accept the first tap — the selector compares only
feed a *"belongs to another product"* flag. A first tap at standby takes a dedicated **first-load branch**
(gated on *"nothing loaded yet"* state, not on the OID value), which posts `0x104a`+`0x1058` → state 12
scans `B:/*.bnl` **on demand** → state 13 → `gme_oid_dispatch` mounts **by product-id compare**
(GME/BNL header `@0x14` == tapped OID) → `gme_parse_header` seeds the selectors. The selectors are a
*consequence* of the first load, not a precondition. All claims below are **[Proven]** from disasm/decomp
unless marked otherwise.

### 8.1 Every writer of the three selectors

| writer | what it writes | when it runs |
|---|---|---|
| C-runtime .bss zeroing | all → 0 | cold boot |
| **`gme_parse_header` 0x08035d20** | `0x081da08c` = header dword `@0x14` (= **product id**, see 8.4); `0x081da716/718/714/728…748` = the 20×u16 **special-OID list** at file offset `[hdr dword @0x94]` (entries #1/#3 → 716/718 = special cover OIDs; entry #19 → byte `0x081da714` = enable flag, classifier requires ==1) | **on mount only** — sole caller `gme_oid_dispatch`, after `gme_mount_check_product(1,0)` succeeds |
| **`FUN_080345cc` 0x080345cc = descTbl[13].entry (book state)** | `0x081da716`=0, `0x081da718`=0, `0x081da714`=0 — plus a big akoid/ctx reset | on **every entry to state 13**, i.e. *before* the first mount → selectors are guaranteed **0 during the first `gme_oid_dispatch`** |
| (indirect) `gme_launch_binary_build_sysapi` 0x080aa934 | passes the **addresses** 0x081da716/718/714 into the `system_api` struct handed to a GME-embedded main binary (header `@0xA8`) | only if a product ships an embedded binary |

Nothing in `app_init_main`, the standby entry (0x080511a0) or any boot path writes them. The mode
selector `0x081da08c` is additionally **cleared to 0** by `gme_oid_dispatch` when a product-id tap
matches no file.

### 8.2 There is NO boot/standby product pre-scan

- The udisk book enumeration exists in exactly **one** place: state-12 entry **`FUN_08034300`**
  (= `descTbl[12].entry`): `FUN_080ef820(L"B:/", booklist+0x30)` + `FUN_080ad7c0(booklist+0x30, L"*.bnl",
  booklist+0x30)` filling the booklist object at `*0x081da080`. Both strings are UTF-16. `FUN_080ad7c0`'s
  **only caller is `FUN_08034300`**. So the scan runs **after** the first tap (standby → state 12), never
  at boot.
- No cover-OID→product map is ever built at boot. Matching is **lazy**: `gme_mount_check_product(1,0)`
  (0x08034130) walks the booklist per tap — `fs_open`, magic `0x238b @8`, `gme_check_language`, then
  **`fs_read(@0x14) == akoid_buf+4`** (the tapped OID). On match the file is left open.
- `app_init_main` contributes only the *state* the first-load branch tests: `akoid_init` (0x080eeb20)
  sets `akoid_buf[0x21] = 0xff` ("no product loaded") and `*0x08008c0d = 0` (content-handle byte, ≠0xff =
  usable).

### 8.3 What `FUN_08037cec` REALLY does (instruction-verified, disasm 0x08037cec–0x08038190)

Args: `(param_1=&event, param_2=&data)`; `*(uint*)*param_2` = a decoded-OID word with flag bits
(`&0x600000`; `0x400000` = fresh valid decode). For a fresh decode: code = `word & 0x3ffff`; codes
`0xff00–0xffff` and `>0xffff` are special (repeat/power-off counting); otherwise **`akoid_buf+4 = code &
0xffff`** is stored — *for any code, 42 included; there is no family gate on storing/accepting*.

Then three independent bookkeeping steps:
1. **Family/selector compares:** pairs (0xc21/0xc22↔mode 6), (0xec4/0xeba↔8), (0x16c0/0x16bf↔15),
   (0x162c/0x162b↔14), (0xb7b/0xb7a↔7), (0xa94/0xa93↔9), (0xa34/0xa35↔11), plus the per-book pair
   `*0x081da716`/`*0x081da718` (if flag `*0x081da714`==1). Result: sets **`akoid_buf[0x4a4] = 1`** = "tap
   is not the current product's cover". It does **not** control any event post.
2. **Repeat counters:** cover-adjacent codes (0xc1d/0xc1e, 0xb7c/0xb7d, 0xa8d, …) increment
   `akoid_buf+0x155/+0x156` when the matching mode is active.
3. **THE FIRST-LOAD BRANCH:**
   ```
   if (akoid_buf[0xb3]) return 1;
   if (akoid_buf[0] != 0                      // pen-down flag (set by the OID poll/gate)
       && game_ctx[0x1d] == 2                 // standby/book marker (standby entry 0x080511a0 sets 2)
       && akoid_buf[0x21] == 0xff             // "no product loaded" (akoid_init)
       && *0x08008c0d != 0xff) {              // content-handle byte usable (akoid_init sets 0)
       func_0x08003644(0x104a, 0);            // queue-append
       func_0x08003644(0x1058, 0);            // queue-append  → standby mapper → STATE 12
       akoid_buf+0x74 = akoid_buf+4;          // latch tapped OID
       akoid_buf[0x58] = 1;
   }
   akoid_buf[0] = 0; return 0;                // consume the 0x1060 at state-9 level
   ```
   **The tapped VALUE is irrelevant here** — the branch is gated purely on "standby + nothing loaded".

### 8.4 The two OID number spaces + what is compared where (raw vs internal)

- **The firmware contains NO raw↔code translation table.** Byte-scans for the `knownRawCodes` prefix
  over PROG.bin, nandboot.bin, producer.bin, codepage.bin: **no hit**. The raw→code mapping of tttool's
  `KnownCodes.hs` lives **upstream of the firmware — in the OID sensor ASIC** (I²C dev 0x94 is used only
  to configure it; the GPIO serial frame already carries the final code, `code = frame >> 9`).
  **[Proven: no table in any blob; Inferred: mapping in sensor silicon]**
- Consequently **everything from `akoid_buf+4` onward is in the INTERNAL OID-number space** (the space GME
  scripts/product-ids use): `gme_oid_dispatch` compares `akoid_buf+4` against the **script-table
  first/last used OID** (`gme_parse_start_end_oid` 0x08035c3c → `akoid_buf+0x18/+0x1a`) to index play
  scripts. The classifier families (0xa00–0x16c0) and `gme_mount_check_product`'s `@0x14` compare are the
  **same space**.
- **Product id == the load OID you tap.** `gme_oid_dispatch` treats every tapped code **`< 0x3e8` (1000)
  as a product id** (codes ≥1000 go to in-book/cover dispatch, codes ≤999 go to product handling) —
  exactly tttool's `firstObjectCode = 1000` boundary. Inside the window: tap == `*0x081da08c` (currently
  mounted pid) → replay power-on jingle (hdr `@0x71`); else → `gme_mount_check_product(1,0)` = **mount the
  .bnl whose header `@0x14` == tapped code**. taschenrechner (`product-id: 42`) is loaded by tapping OID
  **42**; WWW Bauernhof (pid 1) by tapping OID 1.
- Additionally `gme_oid_dispatch` carries a **hardcoded cover-OID→pid map for known retail/built-in
  products**: (0x59d↔1), (0x119a/0x1199↔2), (0x713/0x712↔3), (0xcea/0xce9↔4), (0x3f1,0x40a,0x509…↔5),
  (0xc21/0xc22↔6), (0xb7b/0xb7a↔7), (0xa94/0xa93↔9), (0x89c/0x89b↔10), (0xa34/0xa35↔11),
  (0xf3f/0xf3e/0xf3d↔12), (0x162c/0x162b↔14), (0x16c0/0x16bf↔15), (0x18a2/0x18a1↔0x12),
  (0x1a32/0x1a31↔0x13), (0x1db2/0x1db1↔0x15) — plus the mounted book's own special-OID list values. These
  are the ≥1000 cover symbols of the known product families and only steer replay/exit behaviour once
  something is mounted.

### 8.5 First-load lifecycle, cold boot → first cover tap opens the book

```
cold boot: __main/scatter-load → app_init_main 0x08038f5c
  akoid_init:  akoid_buf[0x21]=0xff, *0x08008c0d=0        // "nothing loaded"
  sm_set_state(3) → standby entry 0x080511a0: game_ctx[0x1d]=2, arm slot *0x081da01c|=0x8000
TAP product OID 42 (sensor streams frames while touching):
  poll/decode → akoid_buf+4=42, akoid_buf[0]=1, event 0x1060
  0x1060 #1: state-9 mapper → transAct[5]=FUN_08037cec → FIRST-LOAD branch (8.3):
             post 0x104a + 0x1058, latch +0x74=42, return 0 (leaf never sees it)
  0x104a:    standby: no-op
  0x1058:    standby mapper 0x0804e884: status 4, result 0xc → ENTER STATE 12
             entry FUN_08034300: scan B:/*.bnl → booklist *0x081da080 (no match ⇒ error flag +0x238)
  next event (0x30 tick): state-12 mapper → eventAct[16]=FUN_0803440c → post 0x1059
  0x1059:    state-12 mapper: status 4 → ENTER STATE 13
             entry FUN_080345cc: zero selectors, akoid_buf[0x21]=0, first-time voice 0x13, arm poll timer
  0x1060 #2 (pen still on the symbol; sensor repeats):
    state-9 classifier: [0x21]==0 ⇒ no re-post; returns 1 ⇒ event continues to leaf
    state-13 book_mode_handler: default → result 0x11 → eventAct[17]=gme_oid_dispatch:
      42 < 0x3e8 ⇒ product window; 42 != *0x081da08c(0)
      gme_mount_check_product(1,0): booklist walk, hdr@0x14==42 → taschenrechner.bnl, fd stays open
      akoid_buf[0x4a8]=1; gme_parse_header()  ← SEEDS 0x081da08c=42 + special-OID list → 716/718/714
      gme_reset_registers; gme_parse_start_end_oid → akoid_buf+0x18/1a = script first/last OID
      hdr@0x71 → power-on jingle plays.  BOOK IS LIVE.
subsequent taps: ≥1000 & in [first,last] → gme_oid_to_playscript; other pid ≤999 → remount (switch book);
                 same-pid cover → replay jingle; classifier now also flags foreign covers via selectors.
```

The re-fire via a second 0x1060 is the physically normal path (the sensor emits many frames per touch).
**[Proven: every step's code; Inferred: that the 2nd 0x1060 (vs a latch replay) is what fires in state 13
on real hardware timing]**

### 8.6 Proven vs Inferred (this section)

| claim | status | evidence |
|---|---|---|
| selector writers = gme_parse_header (seed) + FUN_080345cc (zero) only; +sysapi address export | **Proven** | full PROG pool scan + decomp grep |
| booklist scan only in descTbl[12].entry FUN_08034300; strings L"B:/" / L"*.bnl"; FUN_080ad7c0 single caller | **Proven** | 0x08034300; pools 0x08034534/38; grep |
| classifier first-load branch posts 0x104a+0x1058 independent of selectors/OID value | **Proven** | disasm 0x80380f4–0x8038158 |
| standby mapper: 0x1058/0x105a → state 12; state-12 default posts 0x1059 → state 13 | **Proven** | 0x0804e884; FUN_0803440c; 0x0804eaac |
| gme_oid_dispatch: tapped code ≤0x3e7 = product-id window; sole mount site `gme_mount_check_product(1,0)` = hdr@0x14 == tap; then gme_parse_header seeds | **Proven** | 0x0803629c |
| mode selector 0x081da08c = header@0x14 = product id | **Proven** | gme_parse_header (6th dword) |
| special-OID list @[hdr@0x94] → 0x081da714/716/718/728…; = GME "special OID list" | **Proven** | gme_parse_header; GME-Format.md @0x94 |
| hardcoded cover-OID↔pid map for known products (0x59d↔1 … 0x1db2↔0x15) | **Proven** | 0x0803629c |
| no raw→code table anywhere; akoid_buf+4 is internal OID-number space | **Proven** | byte-scans; 0x08035c3c + dispatch range use |
| raw→code mapping happens in the OID sensor ASIC | **Inferred** | elimination + I²C-config-only wiring |
| voice ids: 0x13 first book-entry, 0x2b unknown tap, 0x2d product not found | Inferred (ids proven, meanings not) | FUN_080345cc; 0x0803629c |
