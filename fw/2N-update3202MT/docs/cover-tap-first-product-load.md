# Cover-tap → first product load — from a standby tap to a mounted product in book(13)

> How a cover/product-OID tap at standby reaches book(13) and how the **first** product mounts.
> The full static trace of the state-9 classifier, the dispatch return-value semantics that govern
> it, the single `gme_mount_check_product` call site, and the game-runtime latch machinery that is
> *not* involved in the first load.
>
> The headline: the first product mounts on the **second physical tap** of its OID — tap 1 opens
> the book, tap 2 mounts the product. All addresses = unified runtime base **0x08009000**
> (`data/PROG.bin` flat load; file off = addr − 0x08009000). Nandboot HAL @0x08000000
> (`data/nandboot.bin`). **[P]** = read from decomp/disasm/PROG bytes (cited); **[I]** = inferred.
> Companions: `book-discovery-and-load.md` (the `.lst`/mount internals), `statechart-full-map.md`,
> `oid-classifier-logic.md`, `oid-sensor-read-protocol.md`.

---

## 0. The dispatch semantics that make it work

A tapped product OID at standby posts **event 0x1060**. The QHsm dispatch order is **state-9 (global
pre-handler) → the current leaf → state-10**, driven by `sm_dispatch_to_hierarchy`. The single fact
that governs the whole first-load story is **how a state-9 transition-action's return value gates the
leaf**:

```
sm_dispatch_to_hierarchy (0x080f2d70):
   iVar3 = sm_dispatch_event(state9, ev);              // the classifier level
   if (iVar3 != 0 && sm_dispatch_event(leaf, ev) != 0) // leaf ONLY runs if state-9 returned != 0
        sm_dispatch_event(state10, ev);
sm_dispatch_event (0x080f2c78) for state-9 (a TA match):
   uVar2 = TA[result](...);      // = the classifier's return value
   return uVar2;                 // returned VERBATIM to the hierarchy
```
[P — `0x080f2d70.c`, `0x080f2c78.c` L21-24.]

So **state-9 TA (classifier) return ≠ 0 ⟹ the leaf IS dispatched; return 0 ⟹ the leaf is skipped.**
(This matches `statechart-full-map.md`: "returns ≠0 on non-cover so book taps reach the leaf.")

That flips the intuitive reading. Combined with the classifier's own handling of `akoid_buf[0]` (the
pen-down / event-pending byte), it produces the two-tap behaviour:

- **Tap 1 (standby, product 42):** the classifier's first-load gate PASSES → it posts `0x104A`+`0x1058`,
  latches `[0x74]=42` and `[0x58]=1`, **clears `akoid_buf[0]=0` and returns 0** → the leaf is skipped
  (the tap is consumed to *open the book*). `0x1058` → `book_mount(12)` → `0x1059` → `book(13)`.
  **No product mounted yet.**
- **Tap 2 (in book(13), product 42):** the gate now FAILS (`[0x21]==0`) → the classifier falls to the
  substitute-event path → **returns 1, leaving `akoid_buf[0]=1`** → the leaf `gme_oid_dispatch` IS
  dispatched with `param_1=0x1060`, `akoid_buf[0]=1`, `akoid_buf[4]=42`. 42 is in the product band
  `[1,0x3E7]` and ≠ the current product (0) → it falls through to `gme_mount_check_product(1,0)` →
  **product 42 mounts.**

The `[0x58]=1`/`[0x74]=42` latch is **not used for the first mount at all** — it is game-runtime
switch-product machinery (§4).

---

## 1. The 0x1060 fork at standby — `cover_oid_classifier` (TA[5])

`sm_dispatch_to_hierarchy` (0x080f2d70) dispatches **state 9 → leaf → state 10** for every pumped
event. The state-9 mapper (0x0800a914) matches `0x1060` → status 1, result 5 → transition-action
**TA[5] = `cover_oid_classifier` 0x08037cec** (TA table 0x0800b544). It runs first. It:

1. Re-checks frame type "10" (`(uVar4 & 0x600000)==0x400000`). [P L29-30]
2. Handles the **system-code family 0xFF00..0xFFFF** (factory/status codes) with a per-tap `0xA6`
   ack-counter (thresholds 0x36/0x3C taps → mode flags) — returns 1, never opens a book. [P L31-79]
3. Otherwise stores **`akoid_buf[4] = uVar4 & 0xFFFF`** (the proven arg→OID masking site), bumps
   counters, and runs the **built-in cover-family matcher** (0xc21/0xc00, 0xa94/0xa00, 0x16c0/0x1600,
   0xec4/0xeba, 0xb7b, 0xa34…) keyed on the per-slot mode selector **`*DAT_080381a4` = `0x081da08c`**
   (the current-product global). A built-in cover match sets `akoid_buf[0x4a4]=1`. [P L83-241; pools
   `DAT_080381a4=0x081da08c`, `080381a8/ac/b0 = 0x081da716/718/714`.]
4. Then reaches the **first-load branch**. [P L242-268]

### 1.1 The first-load gate

Disasm 0x080380e4-0x08038118 [P]:

```
80380e4: ldrb r1,[r0,#0xb3]  ; akoid_buf[0xb3]
80380ec: cmp  r1,#0          ; != 0  → skip (return 1 via 0x803818c) : in fw-update auth playback
80380f4: ldrb r1,[r0]        ; akoid_buf[0]  (event-pending / pen-down)
80380f8: cmp  r1,#0
80380fc: beq  0x803815c      ; == 0 → the per-mode substitute-event path (NOT first-load)
8038100: ldrb r1,[r4,#0x1d]  ; game_ctx[0x1d]
8038104: cmp  r1,#2          ; != 2 → 0x803815c  (standby marker must be 2)
8038108: ldrbeq r0,[r0,#0x21]; akoid_buf[0x21]
803810c: cmpeq  r0,#0xff     ; != 0xff → 0x803815c  (a product is already loaded)
8038110: bne  0x803815c
8038114: ldrb r0,[<0x08008c0d>]
8038118: cmp  r0,#0xff       ; == 0xff → skip
       ; ---- FIRST-LOAD: post 0x104A + 0x1058, latch +0x74, +0x58=1 ----
```

**The gate is NOT the OID value** — the first-load branch never tests what was tapped. It is gated
purely on four *state* bytes:

```
if (akoid_buf[0xb3] == 0                 // not in fw-update auth mode
    && akoid_buf[0]   != 0               // pen-down / event-pending (set by the OID decode pipeline)
    && game_ctx[0x1d] == 2               // "fresh standby" marker
    && akoid_buf[0x21] == 0xff           // "no product loaded"
    && *0x08008c0d    != 0xff) {          // content/battery-stage byte usable
    FUN_080ab424(); delay;
    func_0x08003644(0x104A, 0);          // queue-append
    func_0x08003644(0x1058, 0);          // queue-append → standby mapper → book_mount(12) → book(13)
    akoid_buf[0x74] = akoid_buf[4];      // latch the tapped OID (for the game-runtime replay)
    akoid_buf[0x58] = 1;
}
// then fall through → akoid_buf[0]=0; return 0  (consume at state-9; leaf skipped)
```
[P — disasm 0x080380cc-0x08038194, verified against decomp `0x08037cec.c` L242-279 and
`product-init-and-runtime-tables.md` §8.3.]

The state bytes' provenance:

- **`akoid_buf[0]` (event-pending)** is set to 1 by the nandboot OID timer cb `0x080057cc`
  immediately before it posts 0x1060 (`oid-sensor-read-protocol.md` §5.1). A tap delivered through
  the authentic sensor protocol always has it set. [P]
- **`game_ctx[0x1d]==2`** = the fresh-standby marker, set by `standby_entry` (0x080511a0 L31) and
  splash entry/exit; flipped to **8** by `standby_handler`'s idle path (0x08051b0c L123). [P]
- **`akoid_buf[0x21]==0xff`** = "no product loaded", set by `akoid_init` (0x080eeb20 L75:
  `pcVar7[0x21]=-1`); set back to 0xff only in **content states** — book exit `FUN_080348a8`,
  the per-game OID-index loaders `FUN_0808063c`/`FUN_08078474`/`0x08084e64` — never at standby.
  Cleared to 0 by book entry `book_state_entry` (L43). [P]
- **`*0x08008c0d`** = the battery/content-stage byte (`akoid_init` sets 0; battery-final sets 0xff).

### 1.2 The two `akoid_buf[0]` exits — why the return value is everything

`gme_oid_dispatch`'s entire `0x1060` product-handling block is itself gated on **`akoid_buf[0] != 0`**:

```
8036444: sub ip,r8,#0x1000 ; subs ip,ip,#0x60   ; ip = param_1 - 0x1060
8036458: bne 0x8036f54                           ; not 0x1060 → skip block
8036464: ldrb r0,[r3] ; cmp r0,#0 ; beq 0x8036f54; akoid_buf[0]==0 → SKIP entire block (incl. mount)
8036474: strb r4,[r3]                            ; akoid_buf[0] = 0  (consume the pen-down)
```
[P — disasm 0x08036444-0x08036478.]

So whether the leaf can do real work depends entirely on which classifier exit ran:

| classifier exit | addr | when | `akoid_buf[0]` after | returns | leaf dispatched? |
|---|---|---|---|---|---|
| consume-and-drop | 0x0803818c | `[0xb3]!=0` (auth) **or** `game_ctx[0x36]==0` (normal in-book) | **preserved (=1)** | **1** | **YES** |
| gate-PASS (first-load open) | 0x08037f5c via 0x08038180 | standby gate pass: `[0x1d]==2 ∧ [0x21]==0xff` | cleared (=0) | 0 | no |
| propagate-clear | 0x08037f5c via 0x08038180 | `[0x36]!=0`, or `*0x08008c0d==0xff` | cleared (=0) | 0 | no |

[P — classifier tail disasm 0x080380e4-0x08038190; `mov r0,#0` @0x08037f5c, `mov r0,#1` @0x0803818c;
`strb r7(=0),[akoid_buf]` @0x08038184.]

**The counter-intuitive fact:** the classifier's *return-1 exit at 0x0803818c does NOT touch
`akoid_buf[0]`*. Only the *return-0 exits* clear it. So a tap the classifier "returns 1" on reaches the
leaf **with the pen-down flag intact**, and the leaf can mount; a tap it "returns 0" on is dropped
(leaf skipped) — the standby first-load open, which posts the book events itself.

For an **in-book product tap** the classifier reaches 0x0803815c (gate fails because `book_state_entry`
set `[0x21]=0`), runs two no-op `FUN_080afd30` substitute-event calls (slots `game_ctx+0x18/0x1a` =
0x081da01c/1e, **0 in the GME flow**), then tests `game_ctx[0x36]`. `[0x36]` is forced to 0 at
classifier entry (0x08037cfc) and nothing in the GME flow sets it → **`[0x36]==0` → branch to
0x0803818c → return 1, `akoid_buf[0]` untouched** → leaf `gme_oid_dispatch` runs with `akoid_buf[0]=1`.
[P]

---

## 2. Tap 1 — the book opens (classifier returns 0)

When the first-load gate passes, the classifier posts `0x104A` + `0x1058` and consumes the event.
book(13) entry is **unconditional of any product**:

```
0x1058 → standby mapper → push book_mount(12)
   book_mount entry FUN_08034300: scan B:/*.bnl into the booklist iterator (empty scan → error flag only)
   book_mount default EA[16] FUN_0803440c: post 0x1059  UNCONDITIONALLY
0x1059 → book_mount mapper → push book(13)
   book(13) entry FUN_080345cc: build audio player, zero the cover selectors 0x081da714/716/718,
        akoid_buf[0x21]=0, game_ctx[0x1d]=2, akoid_buf[0x14]=1, first-ever entry → voice 0x13.
        NO product mount happens here.  [P 0x080345cc.c]
```

Until an in-book tap mounts a product, `*0x081da08c` (current product / mode selector) stays **0** — a
product-less book(13) is a *real, reachable* firmware state.

### 2.1 The other way into book(13): the FLAG.bin resume

Besides the classifier first-load branch, two other sites legitimately post `0x1058`:

1. `standby_handler` resume path — **`game_ctx[0x24]==1`**, set by splash entry when **`B:/FLAG.bin`**
   exists (post-update resume). [P 0x0804c1d4 L43, 0x08051b0c L129]
2. `battery_monitor_tick` (0x080afd78) low-battery path (`DAT_080affd0=0x1058`). [P]

The FLAG.bin resume is an **authentic real-pen behaviour**, not a corner case: a live pen RAM dump
(obtained separately, hardware capture) has **`game_ctx[0x24]==1`** (offset 0x19C8), and splash's
FLAG.bin branch is the only writer of 1. So a recently-updated pen **auto-descends** to book(13) with
no tap — playing voice 0x19 at splash and the power-on jingle 0x13 at book entry. This is why a
recently-updated pen jingles at power-on without a tap; on a **clean** boot (no FLAG.bin,
`game_ctx[0x24]==0`) the jingle instead plays at the FIRST cover tap, and the pen otherwise idles at
standby until then. (`game_ctx[0x24]==1` also proves `splash_entry` ran on the real pen.) See
`power-on-sound.md`.

---

## 3. Tap 2 — the product mounts (classifier returns 1)

The mount call is reached through the leaf `gme_oid_dispatch`. There is exactly **one** direct `bl` to
`gme_mount_check_product` in the whole image: **0x08036508** (full ARM BL scan of PROG.bin → 1 hit).
It is additionally exposed as a **syscall to loaded game binaries** (pointer table @0x080aad6c consumed
by `gme_launch_binary_build_sysapi` 0x080aa934) — the game-runtime switch path, not the first load.
[P — BL scan; pointer scan.]

The 0x08036508 site is reached by this straight-line path inside `gme_oid_dispatch`:

```
8036478: ldrb r0,[0x08008c0d] ; cmp r0,#0xff ; bne 0x80364a4   ; *0x08008c0d != 0xff → OID branch
80364a4: ldr r0,[r3,#4]        ; r0 = akoid_buf[4]  (tapped OID)
80364a8: cmp r0,#1 ; bcc 0x80365fc                              ; OID==0 → cover/system region (no mount)
80364b0: subs ip,r0,#0x300 ; subscs ip,ip,#0xe7 ; bhi 0x80365fc ; OID > 0x3E7 → cover/content (no mount)
80364c4: ldr r1,[0x081da08c]   ; r1 = current product / mode selector
80364c8: cmp r0,r1 ; beq 0x80365b0                              ; tapped == current → special (no mount)
80364d0: ...is_audio_playing; FUN_080ab424; func(5); clear 299/12a/12c/12d/12e...
8036504: mov r0,#1
8036508: bl 0x08034130          ; gme_mount_check_product(1,0)   ← THE mount
```
[P — disasm 0x08036478-0x08036508.]

**The mount is reached whenever** `param_1==0x1060` ∧ `akoid_buf[0]!=0` ∧ `*0x08008c0d!=0xff` ∧
`akoid_buf[4] ∈ [1,0x3E7]` (product band) ∧ `akoid_buf[4] != *0x081da08c` (current product). **There is
no `[0x132]` and no `[0x58]` test on this path** — the mount is a plain product-band fall-through, not
gated on any game-runtime latch. (The `[0x132]==0x11`/`==3` tests live in a *different* branch of
`gme_oid_dispatch`, the `OID>0x3E7` cover/system region at 0x080365fc, a sibling of the product-band
fall-through.)

From there `gme_mount_check_product(1,0)` walks the booklist iterator, opens each `.gme`, and accepts
the first with magic 0x238B@8, matching language@0x59, and product-id@0x14 == the tapped OID — see
`book-discovery-and-load.md` §4. On success the handle lands in `p_filehandle_current_gme`,
`gme_parse_header` sets `*0x081da08c` = 42 and seeds the cover selectors, and `gme_parse_start_end_oid`
sets the content-OID range.

---

## 4. The `[0x58]` / `[0x74]` / `[0x132]` fields — game-runtime switch machinery

These three are **game-runtime** switch-product machinery, *unrelated to the first load*:

- **`akoid_buf[0x58]`** is a multiplexed mode/pending byte. Observed codes: `5` = USB-attached, `0xAA` =
  power/idle marker, **`100` = "replay the latched product OID"** (game-runtime switch), `1` = the
  classifier's first-load latch. **`[0x58]==1` has no reader anywhere** — it is inert. [P — grep of all
  `[0x58]` readers: only `==100`, `==5`, `==0xAA`, `!='d'(100)` exist; value 1 is never tested.]
- **The `[0x58]==100` replay** at the very top of `gme_oid_dispatch` (0x080362c4):
  ```
  80362c4: ldrb r2,[r0,#0x58] ; cmp r2,#100
  80362cc: strbeq r4(0),[r0,#0x58]   ; [0x58]=0
  80362d0: strbeq r9(1),[r0]         ; akoid_buf[0]=1   ← re-arms the pen-down flag
  80362d4: ldreq r2,[r0,#0x74]       ; r2 = [0x74] (latched OID)
  80362dc: streq r2,[r0,#4]          ; akoid_buf[4] = latched OID
  80362d8: moveq r8,r7(0x1060)       ; param_1 = 0x1060
  ```
  This re-presents the latched product OID **with `akoid_buf[0]=1`** — the *self-injection* a loaded
  game uses to switch products without a physical tap. It is armed only by **`[0x58]=100`**, written
  exclusively by loaded-game OID handlers (11 sites in 0x0805xxxx-0x0807xxxx, each setting
  `[0x58]=100; [0x74]=[4]`). **Nothing promotes `[0x58]` 1→100** — the first load never uses the replay.
  [P]
- **`akoid_buf[0x132]`** is the game-runtime "additional-script / media-sequence" phase (2→3 at
  0x08036f7c; `==0x11` gates a media-loop branch; `==3` gates another). It is 0 in a product-less
  book(13) and is only written by loaded-game code. Irrelevant to the first mount. [P]

**Why the classifier writes the `[0x58]=1`/`[0x74]=42` latch at all** (0x08038148-0x08038154, gated on
`*0x08008c0d != -1`): it seeds the *game-runtime* switch latch so the first mounted game, if it wants
to hand off to another product, has `[0x74]` pre-populated. It is a courtesy seed, not the first-load
trigger. [P]

---

## 5. OID-timer arming at book(13)

**The OID poll timer is self-re-arming and state-independent** — not a one-shot the standby flow must
re-arm. Disasm of the callback `0x080057cc` [P nandboot]:

- On entry it kills its own handle (`capture_state+8` @0x08008c10) and, if `akoid_buf[0x14]` (the
  capture-enable) is set and GPIO9 is low, decodes a 23-bit frame and posts **0x1060**.
- **Every exit path unconditionally falls to `0x800589c: bl 0x080058b0`** = `hal_oid_timer_start`,
  which recreates a fresh 40-tick timer with the same callback. So once started it re-arms itself
  **forever, in any state**, gated only by `akoid_buf[0x14]`. [P — all branch targets converge on
  0x800589c before the `bl`.]

`0x080058b0` callers: **`splash_entry` (0x0804c1d4 L29)** and **`standby_entry` (0x080511a0 L39)**,
plus the `standby_handler` resume path — enough to bootstrap the self-perpetuating loop before book(13)
is ever entered. `book_state_entry` (0x080345cc) sets **`akoid_buf[0x14]=1`** (L148) and arms a 100 ms
*generic* timer with cb 0x08003994 (the heartbeat/0x30 poster), **not** the OID timer — it does not
need to; the OID timer is already self-perpetuating and `[0x14]=1` re-enables decoding. So content taps
can decode at book(13). [P]

**One robustness quirk:** a **failed** in-book product tap disables capture. `gme_oid_dispatch` executes
**`akoid_buf[0x14]=0`** right before `gme_oid_to_playscript` (0x0803629c L817). If the mount fails (e.g.
no matching `.gme` on the media), `[0x14]=0` leaves the self-re-arming timer firing but **skipping the
decode** → no further 0x1060 posted until a product mounts. Once a product mounts successfully,
decoding stays enabled for in-range content OIDs and the loop is stable. [P]

---

## 6. The full authentic first-product-load sequence

```
STANDBY, gate intact: akoid_buf[0x21]=0xff, game_ctx[0x1d]=2, *0x08008c0d=0, OID timer self-arming
 TAP 1 — product OID 42:
  nandboot 0x080057cc: akoid_buf[0]=1, akoid_buf[8]=0x40002a, POST 0x1060(&arg)
  0x1060 @ state-9 TA[5] cover_oid_classifier:
     akoid_buf[4]=42 ; built-in cover matchers miss ; gate PASS ([0x1d]==2 ∧ [0x21]==0xff)
     → func_0x08003644(0x104A) ; func_0x08003644(0x1058) ; akoid_buf[0x74]=42 ; akoid_buf[0x58]=1
     → akoid_buf[0]=0 ; RETURN 0  → sm_dispatch_event=0 → LEAF SKIPPED (tap consumed to open the book)
  0x1058 @ standby mapper → push book_mount(12): booklist_scan_bnl scans B:/*.bnl → EA[16] POST 0x1059
  0x1059 @ book_mount → push book(13): book_state_entry — akoid_buf[0x14]=1, akoid_buf[0x21]=0,
     game_ctx[0x1d]=2, voice 0x13. current product *0x081da08c still 0. NO product mounted.
 --- product-less book(13), OID decode armed ([0x14]=1), OID timer self-perpetuating ---
 TAP 2 — product OID 42 (in book):
  nandboot 0x080057cc: akoid_buf[0]=1, POST 0x1060
  0x1060 @ state-9 TA[5] cover_oid_classifier:
     akoid_buf[4]=42 ; gate FAILS ([0x21]==0 now, ≠0xff) → 0x0803815c → [0x36]==0 → RETURN 1
     → akoid_buf[0] LEFT = 1  → sm_dispatch_event=1 → LEAF gme_oid_dispatch IS DISPATCHED
  gme_oid_dispatch(0x1060):
     [0x58]==1 (≠100) → replay skipped
     param_1==0x1060 ∧ akoid_buf[0]==1 ∧ *0x08008c0d!=0xff ∧ akoid_buf[4]=42∈[1,0x3E7] ∧ 42≠current(0)
     → gme_mount_check_product(1,0): walk booklist iterator → fs_open each .gme →
        magic 0x238B@8 ∧ gme_check_language ∧ product-id@0x14 == 42 → MATCH → PRODUCT 42 MOUNTED
        (returns 1) → [0x4a8]=1, gme_parse_header, gme_reset_registers, gme_parse_start_end_oid …
 subsequent content taps (OID ≥ 0x3E8, or in-range for the game) → gme_oid_dispatch → play scripts
```
[P — synthesis of `0x08037cec.c`, `0x0803629c.c`, `0x08034130.c`, `0x080345cc.c`, `0x080f2c78.c`,
`0x080f2d70.c`, `booklist_scan_bnl 0x08034300`, `state12_default_action 0x0803440c`.]

The mount happens on **tap 2**, exactly as `book-discovery-and-load.md` §4 states. The two-tap is real:
tap 1 opens the book (classifier returns 0, leaf skipped), tap 2 mounts (classifier returns 1, leaf
runs). No self-posted event, timer replay, or `[0x58]`/`[0x132]` promotion is involved in the first
load.

---

## 7. Proven vs Inferred

| claim | status | evidence |
|---|---|---|
| leaf dispatched iff state-9 TA returns ≠0; classifier return returned verbatim for state-9 | **P** | `0x080f2d70.c`, `0x080f2c78.c` L18-24 |
| classifier return-1 exit (0x0803818c) does NOT clear `akoid_buf[0]`; return-0 exits (via 0x08038180) do | **P** | disasm 0x08038180-0x08038190, 0x08037f5c |
| first-load gate = `[0xb3]==0 ∧ [0]!=0 ∧ game_ctx[0x1d]==2 ∧ [0x21]==0xff ∧ *0x08008c0d!=0xff`; posts 0x104A+0x1058; **no OID-value test** | **P** | disasm 0x080380e4-0x08038158 |
| in-book product tap → classifier gate fails ([0x21]==0) → 0x0803815c → [0x36]==0 → return 1, [0] intact | **P** | disasm 0x08038100-0x0803817c; `[0x36]` zeroed at 0x08037cfc |
| gate miss at standby → 0x1060 to standby_handler → idle re-arm (`akoid_rearm`, `[0x1d]=8`) or count | **P** | `0x0804e884.c`, `0x08051b0c.c` |
| book(13) entry is unconditional of any product; mount is a later in-book step | **P** | `0x08034300`/`0x0803440c`/`0x080345cc.c`; book-discovery §4 |
| 0x1058 posters = classifier / FLAG.bin resume (`game_ctx[0x24]==1`) / battery-low | **P** | `0x08037cec.c`, `0x08051b0c.c` L129, `0x080afd78.c` |
| `gme_oid_dispatch` mount block gated on `param_1==0x1060 ∧ akoid_buf[0]!=0` | **P** | disasm 0x08036444-0x08036468 |
| mount (0x08036508) reached via product-band fall-through, no `[0x132]`/`[0x58]` test | **P** | disasm 0x08036478-0x08036508 |
| exactly one direct BL to `gme_mount_check_product`; also a loaded-binary syscall (table 0x080aad6c) | **P** | full BL scan; pointer scan; `0x080aa934.c` |
| `[0x58]=1` (classifier) has no reader; `[0x58]=100` written only by loaded-game handlers | **P** | grep of all `[0x58]` readers/writers |
| OID timer 0x080057cc self-re-arms unconditionally (bl 0x080058b0 on every path); gated by `akoid_buf[0x14]` | **P** | nandboot disasm 0x080057cc/0x080058b0 |
| `book_state_entry` sets `akoid_buf[0x14]=1`; arms generic cb 0x08003994, not the OID timer | **P** | `0x080345cc.c` L148; `DAT_08034b54=0x08003994` |
| failed mount clears `akoid_buf[0x14]=0` (0x0803629c) → decode skipped until a product mounts | **P** | `0x0803629c.c` L817 |
