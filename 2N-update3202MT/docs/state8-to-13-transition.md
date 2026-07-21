# State 8 (pre-ready / USB-charger) → State 13 (book-listening): the transition and its trigger

All addresses are the unified runtime base **0x08009000**. Evidence tags: **[Proven]** = read directly
from decomp / disasm; **[Inferred]** = deduced from register wiring / table structure, not byte-traced
on hardware.

Primary sources: `usb_connect_handler` 0x08050d28, `state8_entry` 0x08050c28, `FUN_08050ca0`
0x08050ca0 (state-8 exit hook); state mappers `FUN_0804e7c0` (state 8), `FUN_0804e884` (state 3
standby), `FUN_0804eaac` (state 12), `book_mode_handler` 0x0804eb08 (state 13), `FUN_0804e624`
(state 1 splash), `FUN_0804e678` (state 0 root); action fns `standby_handler` 0x08051b0c,
`FUN_08034300` (state-12 entry / mount), `FUN_0803440c` (posts 0x1059), `gme_oid_dispatch` 0x0803629c,
`fwupdate_verify_image` 0x0804cecc; `FUN_0804c1d4` 0x0804c1d4 (state-1 splash entry: battery gate),
`battery_get_level` 0x0804bf18; HAL (nandboot): GPIO read `func_0x08007740`, timer register
`hal_timer_register` 0x0800780c, 0x30 poster 0x08003994.

---

## 0. One-line answer

**State 8's exit is a hardware presence gate, not a battery/voltage gate — it is a USB/charger-PRESENCE
gate.** It is decided by **GPIO input pin 8** (`func_0x08007740(8)` → GPIO input register **0x040000BC
bit 8**) plus the **PMU/charger status registers 0x0407000A and 0x04070001**. When the pen is **on
battery only (GPIO8 == 0)** the state-8 handler posts event **0x100C ("detect out!!")** on the first
settle tick and pops out of the USB-charger sub-state back to **standby (state 3)**, where an OID tap
routes product → **0x1058 → state 12 (mount) → 0x1059 → state 13 (book)**. When it reads **USB present
(GPIO8 == 1)** it instead settles and posts **0x105C ("usb connect pc!!") → state 5 (USB-PC /
mass-storage)** or, at settle count 0x28, **0x1009 ("go to charger") → state 6**. The only battery gate
in the boot path is separate and earlier — a GPIO-bit read in the splash entry (§2), not a
voltage/ADC threshold.

---

## 1. The state / event map (8 → 13)

State handlers come from the descriptor table **0x08121d44** (`obj+0x18`): `desc = *(0x08121d44 +
state*4)`, mapper fn = `desc[+0xc]`, entry/exit hooks = `desc[+0]/[+4]/[+8]`. The mapper returns a
**result index** and a **status byte**; `sm_dispatch_event` 0x080f2c78 then: status 2 = handled; status
1 = run **transition-action** table `obj+0x20 = 0x0800b544`[result]; status 0 = run **event-action**
table `obj+0x1c = 0x0800b8d4`[result] then propagate to parent; other status = `event_loop_dispatch`
0x080f2b38 (entry/exit/super hierarchy move). [Proven — see `statechart-framework.md` + the two table
dumps]

Static descriptor table (from PROG.bin, Proven):

| state | desc | mapper (+0xc) | entry (+0) | role |
|---|---|---|---|---|
| 0 | 0x080b0398 | 0x0804e678 | 0x0804e52c | root/boot |
| 1 | 0x080b0388 | 0x0804e624 | **0x0804c1d4** | splash (battery gate) |
| 2 | 0x080b0408 | 0x0804e844 | 0x08052140 | post-splash init |
| 3 | 0x080b0418 | **0x0804e884** | 0x080511a0 | **standby** (event-action[9] = `standby_handler` 0x08051b0c) |
| 5 | 0x080b03f8 | 0x0804e814 | 0x08050fb8 | **USB-PC / mass-storage** |
| 6 | 0x080b03a8 | 0x0804e6b8 | 0x080504a4 | charger screen |
| 8 | 0x080b03e8 | **0x0804e7c0** | **0x08050c28** | **USB-charger pre-ready** (event-action[6] = `usb_connect_handler` 0x08050d28) |
| 9 | 0x0800b55c | 0x0800a914 | — | global pre-dispatch level |
| 12 | 0x080b0488 | **0x0804eaac** | **0x08034300** | **product mount / load** (event-action[16] = `FUN_0803440c`) |
| 13 | 0x080b0498 | **0x0804eb08** | 0x080345cc | **book / OID-listening** (event-action[17] = `gme_oid_dispatch` 0x0803629c) |

Event-action table `0x0800b8d4` (Proven): idx6 = 0x08050d28 (`usb_connect_handler`), idx9 = 0x08051b0c
(`standby_handler`), idx16 = 0x0803440c, **idx17 = 0x0803629c (`gme_oid_dispatch`)**.

### The natural chain

```
state 0 (root) --0x1001--> state 1 (splash)
     FUN_0804e678: 0x1001 -> result 1                     [Proven]

state 1 (splash) entry FUN_0804c1d4:
     battery_get_level() ok -> sm_set_state(2) + own 0x30 timer; else low-batt path   [Proven]
     mapper FUN_0804e624: 0x1014 -> state 3               [Proven]

state 3 (standby) mapper FUN_0804e884:
     0x105d -> result 8, status 4  -> ENTER state 8 (run USB/charger detect)  [Proven]
     0x1058 -> result 12, status 4 -> ENTER state 12 (product mount)          [Proven]
     0x1047 -> result 7  ; default (0x10xx) -> result 9, status 0 = standby_handler

state 8 (USB-charger) mapper FUN_0804e7c0:
     0x30 (default) -> result 6, status 0 = usb_connect_handler  <-- the settle counter lives here
     0x100c        -> status 6  = EXIT sub-state (pop back to standby)   [Proven]
     0x105c        -> state 5 (USB-PC)   ;  0x1009 -> state 6 (charger)  [Proven]

state 12 (mount) entry FUN_08034300: mounts the product (FUN_080ad7c0)      [Proven]
     mapper FUN_0804eaac: 0x1059 -> result 13, status 4 -> ENTER state 13    [Proven]
     default -> result 16, status 0 = FUN_0803440c which POSTS 0x1059        [Proven]

state 13 (book) mapper book_mode_handler 0x0804eb08:
     default (content/product event) -> result 0x11 = 17, status 0
       = event-action[17] = gme_oid_dispatch 0x0803629c  (mount+play the OID)  [Proven]
```

**Trigger that drives 3 → 12 → 13** (i.e. the actual "open a book"): a **product/book OID tap** at
standby. State 8 is a *side sub-state* for USB/charger detection; the book path is `standby(3) →
mount(12) → book(13)` driven by a product OID. State 8 only has to **get out of the way** (exit back to
standby) for taps to be processed. The complete tap path is resolved in §5–7.

---

## 2. The exact exit condition of state 8 — `usb_connect_handler` 0x08050d28 [Proven]

Literal pool (resolved from PROG.bin): `DAT_08050f38 = 0x081da078` (settle-counter struct: `[0]`=count,
`[1]`=err/flag), `DAT_08050f44 = 0x08121e64` (timer-slot handle), `DAT_08050f4c = game_ctx 0x080089a4`,
0x100C / 0x1009 / 0x105C event ids, MMIO 0x0407000A / 0x04070001 (PMU/charger status).

The handler acts only on **event 0x30 whose `args[0] == *(0x08121e64)`** (the settle-timer slot handle —
it matches by construction, §3). It then reads hardware, in annotated pseudocode:

```
gpio8 = GPIO_in(8)                         // 0x040000BC bit 8 = USB/charger-detect pin
if gpio8 == 0:                             // NOT on USB
    debounce 20 ms; if still 0:
        post 0x100C ("detect out!!")       // EXIT state 8 -> standby ; game_ctx[0x1e]=0 ; return
// ---- USB present (gpio8 == 1): settle / classify ----
count++
if count < 0x28 and <usb/charge status bits at 0x0407000A/0x04070001> and count > 1:
    clear "on USB" power bits (0x04000058, 0x04070001); settle delay
    post 0x105C ("usb connect pc!!")       // -> state 5 (USB-PC) ; game_ctx[0x1e]=2
elif count >= 0x28:
    post (err==0 ? 0x1009 "go to charger" -> state 6 : 0x100C)  // game_ctx[0x1e]=1
```

**Deciding inputs and their "advance vs wait" values:**

| input | address / HAL | "not on USB → leave to standby" | "on USB → USB-PC/charger" |
|---|---|---|---|
| USB/charger-detect pin | `func_0x08007740(8)` → **GPIO 0x040000BC bit 8** [Proven fn; bit# Inferred from the pin-9 wiring in `soc-core-registers.md`] | **0** | 1 |
| PMU/charger status | **MMIO 0x0407000A** (bits 2, 3) | (not reached) | bit2/bit3 select connect-mode |
| charger detail | **MMIO 0x04070001** (bit 4, bit 0x10) | (not reached) | gates the 0x105C post |
| "on-USB" power bits | **MMIO 0x04000058** bits 0x14 | cleared on the way out | set while charging |

**There is NO battery-voltage / power-good / sensor-calibration gate inside state 8.** The only battery
gate in the boot path is earlier, in the **splash entry `FUN_0804c1d4`**, which calls
`battery_get_level()` (0x0804bf18 = `func_0x08007740(0xB) && func_0x08007740(1)`, GPIO bits 11 & 1) up
to four times and only takes the full "battery good" init (`sm_set_state(2)`, font mount, own 0x30
timer) when it returns 1. So the battery gate is a **GPIO-bit read in splash (state 1)**, not a
voltage/ADC threshold, and it is distinct from state 8's USB-presence gate.

State-8 **entry** `state8_entry` 0x08050c28: resets `*0x081da078 = 0`, arms the periodic 0x30 timer via
`hal_timer_register(0, 100, 1, &0x08003994)` and stores the slot handle at `*0x08121e64`, then
`func_0x0800774c(0xC,1)` (sets GPIO 0xC direction). The **exit hook** `FUN_08050ca0` 0x08050ca0 tears
the timer down and sets `game_ctx[0x20][0x58] = 5`. [Proven]

---

## 3. The 0x30 settle event — real source & cadence [Proven]

`state8_entry` calls the **resident-nandboot** `hal_timer_register` 0x0800780c. It installs callback
**0x08003994** into a software-timer slot (`[+1]`=arg=slot index, `[+2]`=reload period, `[+8]`=callback)
and returns the **slot index** as the handle, stored at `*0x08121e64`. On each tick the timer dispatch
0x08005c8c calls the slot with `r0 = slot_index`; 0x08003994 then coalesced-posts `0x30{slot_index}` via
0x0800371c/0x08003768. So `0x30.args[0] == slot_index == *(0x08121e64)` — the gate in
`usb_connect_handler` **matches by construction**. The 0x30 is a **free-running periodic heartbeat**
(period from the `hal_timer_register(…,100,…)` reload); it keeps firing regardless of USB/battery.

**Cadence on hardware:** on a pen genuinely plugged into USB, GPIO8 == 1, so each 0x30 increments the
counter; at count 0x28 the "go to charger" path fires (a fraction of a second at a 100-unit reload). But
on **battery power GPIO8 == 0**, so the **very first** 0x30 tick takes the `detect out` branch and posts
0x100C — the counter never climbs at all; state 8 exits immediately. (State 9, referenced as the
post-exit dispatch continuation, is the global pre-dispatch level `0x0800a914` = transition-action[9];
see §7.)

---

## 4. Evidence index (state-8 exit)

| claim | evidence |
|---|---|
| state-8 exit reads GPIO8, posts 0x100C/0x105C/0x1009 | `usb_connect_handler` 0x08050d28 [Proven] |
| GPIO8/9 = 0x040000BC bits 8/9 via `func_0x08007740` | nandboot 0x08007740 → table @0x08007944[3]=0x040000BC; `soc-core-registers.md` [Proven fn, Inferred bit8] |
| 0x30 = periodic timer 0x0800780c → 0x08003994 posts 0x30{slot}=*(0x08121e64) | nandboot disasm 0x0800780c / 0x08003994; `state8_entry` 0x08050c28 [Proven] |
| standby posts 0x1058 on the resume flag / cover tap | `standby_handler` 0x08051b0c; §6/§7 [Proven] |
| 0x1058→12, 0x1059→13; state-12 posts 0x1059 | mappers 0x0804e884 / 0x0804eaac; `FUN_0803440c` [Proven] |
| state 13 default → event-action[17] = gme_oid_dispatch | `book_mode_handler` 0x0804eb08; table 0x0800b8d4[17]=0x0803629c [Proven] |
| battery gate is in splash, is a GPIO read not a voltage ADC | `FUN_0804c1d4` 0x0804c1d4, `battery_get_level` 0x0804bf18 [Proven] |

---

## 5. The two routes into book mode

There are **two** distinct ways standby(3) advances to mount(12)→book(13), both posting event **0x1058**:

- **(A) the auto-resume route** — the persistent flag `game_ctx+0x24 == 1`, set at splash when
  `B:/FLAG.bin` exists (the update-finish path). §6.
- **(B) the fresh cover-tap route** — a decoded cover OID (event 0x1060) handled by the **global
  state-9 pre-dispatch level**, which runs `FUN_08037cec` (the cover/product classifier) before any
  leaf. §7. **No `B:/FLAG.bin` is required for this route.**

---

## 6. The auto-resume route — `game_ctx+0x24` and `B:/FLAG.bin` [Proven]

Two distinct structs each carry a `+0x24` field; the standby gate reads the game-context one:

- `game_ctx = 0x080089a4` (the static game-context struct).
- `akoid_buf = *(game_ctx+0x20) = *(0x080089c4)` (a `malloc(0xdf0)` buffer, allocated in `akoid_init`
  0x080eeb20).

`standby_handler` 0x08051b0c reads `game_ctx+0x24` **directly** (no deref) — a byte at **0x080089c8** —
distinct from the `akoid_buf+0x24` that `akoid_init` zeroes and that `FUN_08037b10` sets to a book-header
version. The exact post-0x1058 condition:

```c
iVar5 = game_ctx;                                        // 0x080089a4
bVar7 = *(char*)(*(int*)(iVar5+0x20)+0x58) == 5;         // akoid_buf+0x58 == 5 ("came back from state 8")
if (bVar7) akoid_buf[0x58] = 0;                          // consume the flag
if (event == 0x1048)           bVar7 = true;             // 0x1048 also enables
if (game_ctx[0x1e] == 0)       bVar7 = true;             // on battery, game_ctx+0x1e == 0 enables
...
if (bVar7 && game_ctx[0x1e] != 2) {                      // gate
    if (game_ctx[0x24] == 0) { scan/idle; game_ctx[0x1d]=8; }        // idle sub-state (300-tick power-off)
    if (game_ctx[0x24] == 1) { event_queue_post(0x1058); return 1; } // -> state 12 mount -> 13
}
```

So the **only standby route to 0x1058 via this handler is `game_ctx+0x24 == 1`.** The `akoid_buf+0x58 ==
5` clause is one of three enablers (not required — on battery `game_ctx+0x1e == 0` makes `bVar7` true).

**Who writes `game_ctx+0x24`:** exactly one writer — the state-1 splash entry `FUN_0804c1d4`:

```c
iVar4 = fs_open("B:/FLAG.bin", 0, 0);       // UTF-16 path at 0x08121a68
if (iVar4 != -1) {
    *(undefined1*)(game_ctx + 0x24) = 1;    // the product/resume flag
    aud_player_construct(); fwl_play_voice_by_id(0x19);
}
```

`game_ctx+0x24 = 1` iff **`B:/FLAG.bin` exists** on the `B:` udisk. `B:/FLAG.bin` is created by
`fwupdate_finish_restart` 0x08052224 (the tail of the firmware/content **update** path), gated on a
`+0x208` "update-pending" byte. So `game_ctx+0x24` is an **auto-resume / finish-the-update flag**: an
update reboots the pen, splash finds `FLAG.bin`, sets `+0x24=1`, and standby auto-advances `0x1058 → 12
→ 13`. It is **not** a fresh cover-tap classifier.

**Does a battery boot transit state 8?** No, not on a quiet boot. The chain is `root → splash(1) → state
2 → standby(3)`. **State 8 is entered only on event `0x105d`** (standby mapper: `0x105d → result 8,
status 4`), which is posted by the **content/game leaves** during playback to re-poll the charger — not
by splash or standby entry on an idle battery boot. So the state-8 exit hook never runs and
`akoid_buf+0x58` stays 0; this does **not** block recognition, because `game_ctx+0x1e == 0` on battery
already sets `bVar7`. [Proven]

---

## 7. The fresh cover-tap route — the global state-9 pre-dispatch level [Proven]

The key architectural fact: **the pump dispatches to a fixed "state 9" level FIRST, on every event,
before the current leaf.** A fresh cover tap at standby therefore reaches the classifier and opens a
book, with **no `B:/FLAG.bin` required.**

### 7.1 The pump dispatches state 9 first, every event

The application event pump is **`FUN_0800b4a4`**: an infinite loop that reads the hardware/OID input
event ring (`DAT_0800b520+0xc0`=head/`+0xc2`=tail, 0xc-byte entries) and calls
**`sm_dispatch_to_hierarchy`** (0x080f2d70) per event. That function does **not** dispatch to the leaf
first:

```c
sm_dispatch_event(9,  &ev, &data, 0);            // (1) STATE 9 first, unconditionally
if (result != 0 && sm_dispatch_event(leaf, &ev, &data, 1) != 0)  // (2) then the current leaf
    sm_dispatch_event(10, &ev, &data, 1);        // (3) then state 10
func_0x08007620(&ev, &data);                     // dequeue next cascade event
```

`func_0x08006bc8/bd0/bd8` are three constants in nandboot (`mov r0,#9/#10/#11; bx lr` — Proven by
disasm). So the HSM has three **fixed dispatch levels**: **state 9 (global pre-handler) → current leaf →
state 10**. State 9 gets first crack at *every* event regardless of which leaf is active.

### 7.2 State-9 mapper 0x0800a914 — the event → transition-action map

State 9's mapper is a pure event router that **sets status = 1 (`mov r1,#1`) and returns a result
index**, driving the **transition-action table `0x0800b544`** (a full disasm scan of all 70 mappers
found `mov r1,#1` **only** here):

| event | result | → transition-action `0x0800b544[result]` | meaning |
|---|---|---|---|
| **0x1060** | **5** | **`0x08037cec` = `FUN_08037cec`** (cover/product OID classifier) | **decoded-OID tap** |
| 0x105f | 4 | `0x0800a9c8` | OID present/partial |
| 0x105e | 3 | `0x0800b56c` | USB detach |
| 0x1054 | 1 | `0x0800ad80` | no-op |
| 0x1046 | 0 | `0x0800acfc` | heartbeat |
| 0x30 | 2 | `0x0804fdbc` | periodic timer |

So **every `0x1060` OID-tap event → state 9 → transition-action[5] → `FUN_08037cec`.** `0x1060` is the
"decoded OID ready" event (nandboot 0x08003930: `r1==2 → 0x1060`). The OID sensor poll (`FUN_080eef84`,
armed by splash and re-armed by standby entry) posts it into the input ring irrespective of the active
leaf.

`FUN_08037cec` reads the decoded OID at **`akoid_buf+4`**. It classifies the tap against the
cover/product-OID families (keyed by the mode selector `*DAT_080381a4` = `0x081da08c`) for bookkeeping,
but the event post is driven by the **first-load** condition, independent of the selector
(`akoid_buf[0]!=0 && game_ctx+0x1d==2 && akoid_buf+0x21==0xff && *0x08008c0d!=-1`):

```c
func_0x08003644(0x104a, 0);          // 0x104a
func_0x08003644(0x1058, 0);          // 0x1058  -> mount(12) -> book(13)
akoid_buf[0x74] = akoid_buf[4];      // remember the OID
akoid_buf[0x58] = 1;
```

It **returns 0** for a non-cover OID (the event then propagates to the leaf — e.g. `book_mode_handler` /
`gme_oid_dispatch` for in-book play). [Proven]

### 7.3 Standby on a raw tap

Standby's leaf mapper `FUN_0804e884` routes a generic `0x10xx` event → result 9, status 0 →
`standby_handler`. For `0x1060`, `standby_handler` matches none of its cases and **returns 0
(propagate)** — standby does not itself open anything on a raw tap; it self-loops and powers the pen off
after 300 idle ticks. Standby's only mapper route out to content is `0x1058 → state 12`. That 0x1058 is
posted by **either** the resume flag (§6) **or** the global state-9 classifier `FUN_08037cec` on a fresh
cover tap. So a cover tap opens a book at standby **through state 9, not through the standby leaf.**
[Proven]

### 7.4 The complete authentic cover-tap path

```
cold boot (maskROM → nandboot SPL → PROG app_init_main 0x08038f5c)
  ├─ fs_storage_mount_init 0x0803a484   : mount NFTL, build the flat system + FAT/udisk partitions
  ├─ akoid_init 0x080eeb20              : malloc akoid_buf (0xdf0), zero fields
  ├─ sm_set_state(3)                    : initial leaf = STANDBY
  ├─ boot_read_serial_and_codepage 0x0803a3b0 : read serial + codepage header
  └─ wire AO tables, start pump FUN_0800b4a4
state 1 (splash) entry FUN_0804c1d4    : battery gate; fs_open("B:/FLAG.bin") — absent on a normal pen → game_ctx+0x24=0
state 3 (STANDBY) entry FUN_080511a0   : re-arm OID poll (FUN_080eef84); open log files
  ── physical cover tap ──> OID sensor decodes ──> input ring gets event 0x1060 (arg = decoded OID @ akoid_buf+4)
pump FUN_0800b4a4 → sm_dispatch_to_hierarchy(0x1060):
  (1) STATE 9  mapper 0x0800a914: 0x1060 → result 5, status 1 → trans-action[5] = FUN_08037cec
        FUN_08037cec: akoid_buf+4 matches cover family → post 0x104a, post 0x1058 ; return
  (2) leaf STANDBY: 0x1060 → standby_handler → return 0 (propagate, no-op)
pump: 0x1058 → STATE 9 (pass-through) → leaf STANDBY mapper 0x0804e884: 0x1058 → ENTER state 12
state 12 (MOUNT) entry FUN_08034300    : scan B: for *.bnl into 0x081da080
        mapper 0x0804eaac default → event-action[16] FUN_0803440c → post 0x1059
pump: 0x1059 → leaf state 12 mapper 0x0804eaac: 0x1059 → ENTER state 13
state 13 (BOOK) entry 0x080345cc, then each in-book OID tap:
        0x1060 → STATE 9 FUN_08037cec (non-cover → return 0, propagate) → leaf book_mode_handler 0x0804eb08
        → event-action[17] = gme_oid_dispatch 0x0803629c → resolve OID via a:/oidfilelist.lst → play media
```

This walk — power-on to standby, silent until the first tap, then a decoded cover tap into book mode
(state 13) with the product mounted — has been reproduced by running the unmodified firmware under
[`tt-emu`](https://github.com/nomeata/tt-emu). [Proven]

### 7.5 NAND-FS dependencies of the cover-tap path

Drive-letter map (Proven from the path strings + `nftl-layout.md`): **`W:`** = flat/linear *system*
partition (PROG + fonts); **`B:`** = wear-leveled *FAT/udisk* content partition (book archives, flags,
logs); **`A:`** = a log/aux FAT volume; **`a:`** = the *virtual* FS **inside the currently-mounted book
archive** (its internal `oidfilelist.lst`/`voicelist.lst`/`musiclist.lst`). What the
`standby → FUN_08037cec → mount(12) → book(13) → play` path touches:

| # | resource | read by | purpose | if missing / empty |
|---|---|---|---|---|
| 1 | **NFTL system + FAT partitions** | `fs_storage_mount_init` 0x0803a484; `nftl_check_anyka_ic`, `fs_partition_scan` | mounts the flat + wear-leveled partitions | **HARD FAULT** — `app_init_main` loops forever if the mount returns nonzero; `fs_storage_mount_init` has two `do{}while(true)` traps if the device/FTL cannot be built. A valid FTL is mandatory. |
| 2 | **`W:/codepage.bin`** (NLS conversion tables; `codepage-what-is-it.md`) | `boot_read_serial_and_codepage` 0x0803a3b0 → `codepage_get_header` 0x08030fb4 → `codepage_load` 0x08030e4c | text/token glyphs; codepage/lang selection | **Soft-degrade** — retries a backup then returns −1; boot continues (return ignored). Required for correct text/lang, not to open a book. |
| 3 | **pen serial** (8 bytes) | `boot_read_serial_and_codepage` 0x0803a3b0 | product/lang gating; log headers | If unreadable → boot flag stays 0 (soft); does not block the cover-tap path. |
| 4 | **cover-OID mode selector** `*DAT_080381a4` = RAM `0x081da08c` | `FUN_08037cec` 0x08037cec | fast-classifies a tap as the current product's cover family (bookkeeping only) | `.bss`, seeded by `gme_parse_header` at product mount (`product-init-and-runtime-tables.md` §4). If 0, only the "belongs to another product" bookkeeping is skipped — the **first-load** branch still posts 0x1058 (§7.2). |
| 5 | **`B:/` book archives** — `*.bnl` files | state-12 entry `FUN_08034300` → `FUN_080ad7c0` ("B:/","*.bnl") into 0x081da080 | the book/product content to mount | If none found → mount-error flag set; state 13 still entered but with no content → error/again audio. |
| 6 | **book header** — magic `0x238b` @ off 8, language, product-id @ off 0x14 | `gme_mount_check_product` 0x08034130 | validate each `.bnl`/`.gme`, match language/product | A file failing magic/language is skipped; if all fail, no product mounts. |
| 7 | **`a:/oidfilelist.lst`** (+ `voicelist.lst`, `musiclist.lst`) | `FUN_080ae2c0` 0x080ae2c0, via `gme_oid_dispatch` (event-action[17]) | maps a tapped in-book OID to the file to play | If absent/empty the OID→media lookup fails → tap produces no/erroneous audio; opening the book still succeeds. |
| 8 | **`B:/FLAG.bin`** (auto-resume flag, §6) | splash `FUN_0804c1d4` | **only** the update-RESUME path; NOT needed for a fresh cover tap | Absent on a normal pen. |

### 7.6 Proven vs Inferred (this section)

| claim | status | evidence |
|---|---|---|
| the app event pump is `FUN_0800b4a4`, reads input ring, calls `sm_dispatch_to_hierarchy` per event | **Proven** | `FUN_0800b4a4` |
| pump dispatches **state 9 first**, then leaf, then state 10 (fixed levels 9/10/11) | **Proven** | `sm_dispatch_to_hierarchy` 0x080f2d70; nandboot `mov r0,#9/#10/#11` |
| state-9 mapper 0x0800a914: **0x1060 → result 5, status 1** → trans-action[5] = `FUN_08037cec` | **Proven** | 0x0800a914 disasm; table 0x0800b544[5]=0x08037cec |
| status 1 comes **only** from 0x0800a914 | **Proven** | full disasm scan of all 70 mappers: sole `mov r1,#1` |
| `FUN_08037cec` reads `akoid_buf+4`, cover match → post 0x104a + 0x1058 | **Proven** | `FUN_08037cec` 0x08037cec |
| **fresh cover tap opens a book at standby (no FLAG.bin)** | **Proven** | composition of the above + §7.3 |
| `fs_storage_mount_init` hangs on a bad FTL; `app_init_main` hangs on mount!=0 | **Proven** | 0x0803a484; `app_init_main` 0x08038f5c |
| `game_ctx+0x24=1` set only by splash entry, from `B:/FLAG.bin` | **Proven** | `FUN_0804c1d4`; `fwupdate_finish_restart` 0x08052224 |
| cover-OID **mode selector `0x081da08c`** is seeded by `gme_parse_header` at product mount; it is *not* required for the first-load 0x1058 post | **Proven** | `FUN_08037cec`; `gme_parse_header` 0x08035d20 (`product-init-and-runtime-tables.md` §4/§8) |
