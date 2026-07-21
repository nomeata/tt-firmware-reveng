# OID-classifier value logic — what `cover_oid_classifier` tests vs what mounts a book

> A precise account of what `cover_oid_classifier` tests on the tapped OID value versus what
> actually mounts a book — the two are different decisions. Static decomp/disasm only; all
> addresses = unified runtime base **0x08009000** (`data/PROG.bin`, file off = addr − 0x08009000).
> **[Proven]** = read from decomp/disasm/PROG bytes (cited); **[Inferred]** = inferred.
> Companions: `cover-tap-first-product-load.md`, `book-discovery-and-load.md`,
> `statechart-full-map.md`.

---

## 0. Model (executive summary)

Two different decisions are involved, made in two different places:

| decision | where | what is tested |
|---|---|---|
| "open book **mode** (state 13)?" — first tap at fresh standby | `cover_oid_classifier` 0x08037cec first-load branch (0x080380e4–0x08038158) | **state bytes only** — no OID-value test. *Any* decoded OID (product, content, garbage) opens the product-less book(13). |
| "is this a **product** or **content** OID?" | `gme_oid_dispatch` 0x0803629c @ **0x080364b0–0x080364b8** | **`1 ≤ OID ≤ 0x3E7`** (999) = product band; `> 0x3E7` = content path |
| "**which** book to load?" | `gme_mount_check_product` 0x08034130 | linear probe of the booklist; accept the `.gme` whose **hdr@0x14 == the tapped OID** (latched at `akoid_buf+4` by the classifier) — **matching, never "first entry"** |
| "already the right book?" | `gme_oid_dispatch` @ 0x080364c4–cc | tapped == current product (`*0x081da08c`) → **no-op return** (short-circuit) |
| "content OID while a book is mounted?" | classifier gate-miss (returns 1 = propagate) → dispatch content path @0x080365fc | range check `first(+0x18) ≤ OID ≤ last(+0x1a)` → `gme_oid_to_playscript` |

So: **the classifier does test OID values** — extensively — but only for the built-in
cover/page families and for latching; it does **not** test the value in its first-load gate,
and it never chooses a book. The book choice is OID-**matched** in `gme_mount_check_product`.
"Any tap opens the first book" is wrong on both counts: any tap opens **book mode** (no book
loaded), and the load that follows picks the **matching** product.

---

## 1. Q1 — does the classifier compare the OID against a product/content threshold? **No** [Proven]

There is **no `0x3E7`/1000 compare anywhere in `cover_oid_classifier`** (0x08037cec). Its
value tests are (decomp `0x08037cec.c`):

1. **Frame-tag demux** (L25–89): raw arg `*(u32*)*param_2`; `(raw & 0x600000) == 0x400000` =
   normal decode. `code = raw & 0x3FFFF`: `code ≥ 0x10000` → blank/no-code handling (idle
   counter `akoid_buf[0xac]`, thresholds 0x12/0x36/0x3C, sensor cmd 0xA6) → return 1;
   `code == 0xFFFF` → return 0 (consumed); otherwise **latch `akoid_buf[4] = raw & 0xFFFF`**
   (L84) — the value every later consumer reads.
2. **Built-in cover matcher** (L90–146): with slot = current product `*0x081da08c`
   (pool `DAT_080381a4` [Proven]) it compares the latched OID against per-slot **cover pairs**:
   slot 6 {0xC21,0xC22}, slot 8 {0xEC4,0xEBA}, slot 0xF {0x16C0,0x16BF}, slot 0xE
   {0x162C,0x162B}, slot 7 {0xB7B,0xB7A}, slot 9 {0xA94,0xA93}, slot 0xB {0xA34,0xA35}, and —
   for a mounted GME — the **dynamic pair** `*0x081da716`/`*0x081da718` (enabled by
   `*0x081da714 == 1`; seeded by `gme_parse_header` from GME hdr@0x94). Current slot active ∧
   tap ∉ its pair → `akoid_buf[0x4a4] = 1` (LAB_08037ec4, "tapped off the current cover" —
   exit-behaviour flag, not a mount trigger).
3. **Page counters** (L147–240): specific first/second-page OIDs per slot (0xC1D/0xC1E @6,
   0xB7C/0xB7D..0xB7F @7, 0xA8D/0xA91 @9, 0x16BC/0x16BD @0xF, 0x1626/0x1629 @0xE, 0xA33/0xA3E
   @0xB, literal 8 @8) bump `akoid_buf[0x156]`/`[0x155]`, else both reset to 0.
4. **First-load branch** (L242–268, disasm 0x080380e4–0x08038158): the *only* mount-flow
   trigger — and it is **purely state-gated**:
   `akoid_buf[0xb3]==0 ∧ akoid_buf[0]!=0 ∧ game_ctx[0x1d]==2 ∧ akoid_buf[0x21]==0xff ∧
   *0x08008c0d != 0xff` → `event_queue_post(0x104A); event_queue_post(0x1058)` (pools
   0x080381bc/c0 = 0x104A/0x1058 [Proven]); latch `akoid_buf[0x74] = akoid_buf[4]`,
   `akoid_buf[0x58] = 1`; clear `akoid_buf[0]`; **return 0 = consume**.

**Where product-vs-content IS decided:** `gme_oid_dispatch` (book-13 default handler EA[17]),
disasm [Proven]:

```
80364a4: ldr  r0,[r3,#4]        ; tapped OID (akoid_buf+4, latched by the classifier)
80364a8: cmp  r0,#1 ; bcc 0x80365fc          ; OID 0 → content/error path
80364b0: subs ip,r0,#0x300
80364b4: subscs ip,ip,#0xe7                  ; 0x300+0xE7 = 0x3E7 = 999
80364b8: bhi  0x80365fc                      ; OID > 0x3E7 → CONTENT path
80364bc: ldr  r7,=0x081da08c                 ; OID 1..0x3E7 → PRODUCT block
```

So the **threshold compare lives at 0x080364b0–0x080364b8 in `gme_oid_dispatch`, constant
0x3E7**, not in the classifier. (Entire band 1..0x3E7 goes to the product block — the
0x300..0x3E7 idiom is just ARM immediate encoding, not a sub-band restriction.)

Classifier entry disable gate (for completeness, still not an OID test): low-battery stage
`*0x08008c0b ≥ 3` or low-batt-voice pending `*0x081da086 & 0x10` → consume everything
(L25; see `pmu-power-management.md`). [Proven]

---

## 2. Q2 — content pass-through to the mounted book [Proven]

**Mechanism = classifier state gate + return-value convention.** Proven semantics
(`sm_dispatch_to_hierarchy` 0x080f2d70 + `sm_dispatch_event` 0x080f2c78, param₄=0 for the
state-9 call: the TA's return value is returned verbatim, and the leaf is only dispatched
`if (ret != 0)`):

> **classifier returns 0 → event consumed at state-9; returns 1 → event continues to the leaf.**

(See `cover-tap-first-product-load.md` §0 for the same dispatch rule and the two-tap consequence.)

`book_state_entry` (0x080345cc) sets `akoid_buf[0x21] = 0` — so **in book(13) the first-load
gate can never fire** (`[0x21] != 0xff`), the classifier takes LAB_0803815c (substitute-event
hooks, no-ops in the GME flow) and **returns 1** → every 0x1060 propagates to
`gme_oid_dispatch`. It is the *state gate* that protects the mount flow, plus the *explicit
range branch* in dispatch that separates the bands. Both exist; they answer different
questions (open-book-mode vs product-vs-content).

### Trace: tap of OID value 0x1060 (4192, content), both states

**A. No book (fresh standby, gate bytes intact):**
1. classifier: latch `akoid_buf[4]=4192`; cover matchers idle (current product 0 → no slot
   active); **first-load gate PASSES** (state-only!) → posts 0x104A+0x1058, latches
   `+0x74=4192`, `+0x58=1`, returns 0 (consumed) → book_mount(12) → product-less book(13).
   **Yes: even a content OID opens book mode.** [Proven]
2. next tap of 4192 in-book: classifier gate-miss (`[0x21]=0`) → return 1 → dispatch:
   `4192 > 0x3E7` → 0x080365fc → `akoid_buf[0x4a8]==0` (nothing mounted) → **voice 0x2B**
   (0x08036608). A content OID **never** triggers a book-load. [Proven]

**B. Book mounted (say product 42, `[0x4a8]==1`):**
1. classifier: latch 4192; dynamic cover pair (0x081da716/718, seeded from GME hdr@0x94)
   compared — miss → `[0x4a4]=1` bookkeeping only; gate-miss → return 1 → propagate.
2. dispatch: `4192 > 0x3E7` → content path 0x080365fc → `[0x4a8]==1` → built-in special-OID
   chains miss → **range check `akoid_buf+0x18 ≤ 4192 ≤ akoid_buf+0x1a`** (decomp L789–795;
   range set by `gme_parse_start_end_oid` at mount) → in-range →
   **`gme_oid_to_playscript(4192)`** (L818; capture flag `[0x14]` toggled 0→1 around it).
   Out-of-range → voice 0x2B. [Proven]

Note the inert latch: the classifier's `akoid_buf[0x58] = 1` has **no reader** — corpus grep
[Proven]: the only reader of `[0x58]` is dispatch's `== 100` replay preamble (L48–55), and the
only writers of 100 (`'d'`) are 14 game-state files (0x0807bdc8 … 0x0808ae10). So the
first tap's latched `+0x74` is only consumed via the game-side 100-replay; the standby→book
flow needs a **second physical tap** to mount. The `[0x58]=1` latch is proven inert (no
reader), which settles the open question in `book-discovery-and-load.md` §7.

---

## 3. Q3 — matching book, not the first entry [Proven]

The classifier **routes** (posts 0x1058 → book mode); the **OID match happens in
`gme_mount_check_product`** (0x08034130), sole caller `gme_oid_dispatch` @0x08036508 with
mode **(1,0)** [P corpus grep — no other caller]:

- expected id `iVar8 = *(akoid_buf+4)` = **the tapped OID** (decomp L18–19);
- loop over the booklist iterator `*0x081da080` (u16 count = this boot's `.lst` scan):
  fetch record path (`booklist_get_path_at_cursor` 0x080f0618) → `fs_open` → **hdr@0x08 ==
  0x238B** → `gme_check_language()` → mode 1: **`fs_seek(0x14); fs_read(4)`; accept iff
  `hdr@0x14 == tapped OID`** (L57–60);
- mismatch → close handle, `booklist_cursor_advance(+1)` (cursor walk with window wrap —
  the probe visits **every** record regardless of start position), next;
- match → handle stays in `p_filehandle_current_gme` @0x08121ed0, return 1; exhausted →
  return 0.

**With multiple products on the udisk a product tap loads the one whose GME hdr@0x14 equals
the tapped value — not index 0.** "First" enters only as a tie-break: if *several* `.gme`
files carry the *same* product id, the first in cursor/FAT-enumeration order wins.
(Side effect: bit7 of `0x081da086` is set as soon as *any* valid-magic+language GME is seen
during the probe, L56 — before the product-id check.)

**"Already the right book" short-circuit — yes** [P disasm]:

```
80364c4: ldr r1,[r7]      ; current product *0x081da08c
80364c8: cmp r0,r1        ; tapped == current?
80364cc: beq 0x80365b0    ; → 80365b0: cmp r1,#20 ; cmpeq r0,#20 ; bne 0x8036528 (return)
```

Tapping the mounted product's OID again is a **no-op** (no re-probe, no re-parse). Sole
exception: product id **20** (0x14) re-reads hdr@0x71 and re-derives the media XOR key with
alternating parity (0x080365bc–0x080365f8). Also: after a *successful* mount of product id
**8**, dispatch posts event 0x101c (0x08036598–0x080365ac). [Proven]

---

## 4. Q4 — switching books while one is mounted [Proven]

Handled by the **same dispatch product block** — no unmount state, no transition:

1. In-book tap of a different product OID `p ≤ 0x3E7`, `p != *0x081da08c`: classifier
   gate-miss → return 1 → dispatch product block (0x080364d0–):
   stop audio if playing (`is_audio_playing` → 0x080ab620), `FUN_080ab424`, 5-tick delay,
   clear the script-sequencer flags (+0x12a..+0x12e), then **`gme_mount_check_product(1,0)`**.
2. The probe loop itself closes the previously mounted handle (`if (*0x08121ed0 != -1) →
   fs_close` on the first iteration, decomp L42–45) — the old book is implicitly unmounted.
3. Match → `[0x4a8]=1`, `gme_parse_header` (new `*0x081da08c` = new hdr@0x14, re-seeds the
   dynamic cover pair), `gme_reset_registers`, `gme_parse_start_end_oid` (new content range
   +0x18/+0x1a), XOR-key re-derivation → **book switched in place**.
4. No match → `[0x4a8]=0`, `*0x081da08c=0`, **voice 0x2D** — the pen degrades to the
   product-less book(13) state (the old book stays unmounted).

---

## 5. Q5 — reconciliation: exact division of labour

**`cover_oid_classifier` (0x08037cec) tests:**
- frame tag / code validity (`&0x600000`, `code ≥ 0xFFFF` = non-OID) [Proven]
- the latched OID vs the **built-in + GME-seeded cover/page families** (§1.2/§1.3) — for the
  `[0x4a4]`/`[0x155]`/`[0x156]` bookkeeping flags only [Proven]
- **five state bytes** for the first-load branch — **never the OID value** [Proven]

**`gme_oid_dispatch` (0x0803629c) tests:**
- `akoid_buf[0]` fresh-tap flag; `*0x08008c0d != 0xff` usable-stage [Proven]
- **product band `1..0x3E7` @0x080364b0** — THE product-vs-content decision [Proven]
- tapped vs current product (`*0x081da08c`) — short-circuit / switch [Proven]
- content range `+0x18 ≤ OID ≤ +0x1a` → playscript, else voice 0x2B [Proven]

**`gme_mount_check_product` (0x08034130) tests:**
- per booklist record: GME magic 0x238B@8, language@0x59, **product id hdr@0x14 == tapped
  OID** (`akoid_buf+4`) [Proven]

**Precise distinctions worth stating explicitly:**

| point | the precise reading |
|---|---|
| the first-load fork is gated on state, not on the tapped OID | correct — but it applies only to *opening book mode*, not to *loading a book* |
| "any product tap opens the first book" | **any tap of any decoded OID** (product, content, garbage) opens the **product-less book(13)** — **no book is loaded** by that tap. The load happens on the next product-band tap and is **hdr@0x14-matched**, not positional. A content-OID first tap ends in voice 0x2B, never a load. |
| the state-9 return convention | `sm_dispatch_to_hierarchy` dispatches the leaf only `if (state9_ret != 0)`. Classifier: return 0 = consume (first-load, 0xFFFF code, disable gate), return 1 = propagate (gate-miss → in-book taps reach `gme_oid_dispatch`). |
| the "system-code family 0xFF00..0xFFFF" | the blank/ack branch fires for `code ≥ 0x10000` (and `== 0xFFFF` returns 0); 0xFF00..0xFFFE latch as ordinary OID values |
| `book-discovery-and-load.md` §7 "no auto-replay of the first tap" | confirmed **[Proven]**: `[0x58]` readers = dispatch `==100` only; the classifier's `=1` is inert |

A precision point on the dispatch pools: `*DAT_08036734 == -1` (dispatch decomp L135) is
**`*0x08008c0d == 0xff`** (battery-final stage → error voices only), *not* the GME file-handle
test — pool `DAT_08036734 = 0x08008c0d` [P PROG bytes], distinct from `DAT_08036730 =
0x08121ed0` (the handle used for `fs_seek`).

---

## 6. Evidence index

| claim | status | evidence |
|---|---|---|
| classifier has no ~1000 threshold; tests cover/page constants + latches `akoid_buf[4]` | **P** | `0x08037cec.c` L29–241; no 0x3E7/0x3E8 constant in the fn |
| first-load branch = state gate only; posts 0x104A+0x1058; latches +0x74; ret 0 | **P** | `0x08037cec.c` L242–268; disasm 0x080380e4–0x08038158; pools 0x080381bc/c0 = 0x104A/0x1058 |
| state-9 TA ret 0 = consume, ret 1 = leaf sees the event | **P** | `0x080f2d70.c` (`if (iVar3 != 0)` → leaf), `0x080f2c78.c` (param₄==0 → `return uVar2`) |
| product band = `1..0x3E7` decided in dispatch | **P** | disasm 0x080364a8–0x080364b8 (`subs #0x300; subscs #0xe7; bhi`) |
| same-product tap = no-op (exceptions: id 20 XOR re-derive; id 8 posts 0x101c) | **P** | disasm 0x080364c4–cc, 0x080365b0–f8, 0x08036594–ac; pool 0x08036744 = 0x101c |
| mount = booklist probe matching hdr@0x14 == `akoid_buf+4`; closes old handle; full cursor walk | **P** | `0x08034130.c` L18–19, L42–45, L55–63; pools 0x0803452c/0x0803411c = 0x081da080/0x08121ed0 |
| `gme_mount_check_product` mode (1,0), sole caller dispatch | **P** | corpus grep; `0x0803629c.c` L1117 / disasm 0x08036504–08 |
| in-book product switch via the same block; fail → product 0 + voice 0x2D | **P** | disasm 0x080364d0–0x08036528 |
| content in-book: range `+0x18..+0x1a` → playscript; else 0x2B; unmounted → 0x2B | **P** | `0x0803629c.c` L789–856, L655–658; disasm 0x080365fc–0x08036610 |
| `[0x58]==1` has no reader (only ==100 replay); 2nd tap required to mount | **P** | corpus grep: writers `'d'` in 14 game files; sole reader `0x0803629c.c` L48–55 |
| classifier disable gate = low-batt stage/flag (`*0x08008c0b ≥ 3` ∨ `*0x081da086 & 0x10`) | **P** | `0x08037cec.c` L25; `pmu-power-management.md` |
| cover-pair table incl. dynamic GME pair @0x081da716/718 (enable 0x081da714) | **P** | `0x08037cec.c` L90–146; pools 0x080381a8/ac/b0; `gme_parse_header` seeding per `book-discovery-and-load.md` §4 |
