# Is the state-8 → state-13 advance gated on the A: mount? — VERDICT: NO (independent floors)

Question answered here: does leaving **state 8** (USB-charger pre-ready, mode handler
`usb_connect_handler` 0x08050d28) and reaching **state 13** (BOOK, `book_mode_handler`
0x0804eb08) require the A: (product) partition mount (`fs_storage_mount_init` 0x0803a484
→ the NFTL superfloppy) to complete?

All addresses = unified runtime base **0x08009000**. Evidence tags: **[Proven]** = read from
the decomp/disasm cited; **[Inferred]** = deduced. Companion deep-dive:
`state8-to-13-transition.md` (the full state-8 RE).

---

## 0. One-line verdict

**The state-8 floor and the mount floor are two different, independent floors.**
State 8's exit is gated **only on hardware presence bits** — GPIO input 0x040000BC **bit 8**
(USB/charger-detect) plus the PMU status bytes 0x0407000A/0x04070001 — and on nothing
filesystem-related. The A: mount **cannot** gate the 8→…→13 walk architecturally, because
`fs_storage_mount_init` runs **before the statechart exists** and posts **no event** on
completion; and the 3→12→13 walk is driven by an OID tap (event 0x1060), with the 12→13
edge **unconditional** even when the content scan finds nothing. Conversely, the mount gates
(a) **boot itself** (hard `while(true)` hang at state 0 if it fails) and (b) **content/play**
inside states 12/13 — but never the state-8 exit. Both floors were confirmed dynamically to
resolve separately (see [`tt-emu`](https://github.com/nomeata/tt-emu)).

---

## 1. Why the mount CANNOT gate state 8 — the architectural proof

### 1.1 The mount completes before the AO exists [Proven]

`app_init_main` 0x08038f5c runs strictly:

```c
iVar2 = fs_storage_mount_init();          // the A:/B: NFTL mount
if (iVar2 == 0) {
    ... akoid_init();
    sm_set_state(3,0);                    // INITIAL leaf := standby(3)
    ... wire AO tables (+0x18/+0x1c/+0x20), event_queue_init();
    (*descTbl[current].entry)();          // FIRST statechart code runs HERE
    return;
}
do { } while(true);                       // mount failed: hang forever (state 0)
```

Strict ordering: **mount → (only on success) statechart creation**. There is no
"mount-complete" statechart event because at mount time there is no statechart to post to —
`0x0803a484` and `0x08038f5c` contain **zero** `event_queue_post`/`func_0x08003644` calls and
zero 0x10xx event literals. The mount result is consumed solely as the `if (iVar2 == 0)`
return code. [Proven]

So a mount **failure** manifests as the AO frozen at **state 0** forever. A pen that got as
far as **state 8 has by definition already mounted** — the state-8 park is never a symptom of
a missing mount.

### 1.2 The state-8 code consults no filesystem state [Proven]

Full read of all three state-8 components:

- **`usb_connect_handler` 0x08050d28**: reads **only** `func_0x08007740(8)` (GPIO input bit 8),
  MMIO **0x0407000A** / **0x04070001** (PMU/charger status), MMIO 0x04000058/0x0400000C (power
  bits, write-side), the settle-counter struct `*0x081da078`, the timer-slot handle
  `*0x08121e64`, and `game_ctx+0x1e`. **No `fs_*` call, no booklist (`0x081da080`), no
  partition/medium object, no mount flag.**
- **`state8_entry` 0x08050c28**: zeroes the settle counter, arms the periodic 0x30 timer
  (`hal_timer_register(0,100,1,…)` → slot handle at `*0x08121e64`), GPIO 0xC direction.
  Nothing FS.
- **state-8 mapper `FUN_0804e7c0`** (entire fn — 30 lines): a pure event router with **no
  guards at all**: `0x1009→state 6`, `0x100c→status 6 (exit to parent)`, `0x105c→state 5`,
  default→status 0/result 6 (= `usb_connect_handler`).

The **only** exit conditions out of state 8 are therefore the three events its own handler
posts from **hardware reads**: `0x100C` ("detect out!!", GPIO8==0 twice 20 ms apart → back to
standby 3), `0x105C` ("usb connect pc!!" → state 5), `0x1009` ("go to charger" → state 6).
[Proven]

### 1.3 The 3→12→13 walk is tap-driven, and 12→13 is unconditional [Proven]

(Condensed from `state8-to-13-transition.md`.)

```
standby(3) ── OID tap → event 0x1060 → global STATE-9 pre-dispatch (mapper 0x0800a914:
              0x1060 → result 5/status 1) → trans-action[5] = FUN_08037cec (cover/first-load
              classifier) → posts 0x104a + 0x1058
        0x1058 → standby mapper FUN_0804e884 → ENTER state 12 (mount/booklist)
state 12 ── ANY event → event-action[16] = state12_default_action 0x0803440c:
              event_queue_post(0x1059) UNCONDITIONALLY  (one line, no guard)
        0x1059 → mapper FUN_0804eaac → ENTER state 13 (BOOK)
```

State-12's entry `booklist_scan_bnl` 0x08034300 does touch the FS (scans `B:/` for `*.bnl`)
— but **on an empty/failed scan it merely sets error flags** (`akoid_buf+0x38=0xff`, `+0x14=1`)
and the 0x1059 post still happens → **state 13 is entered regardless of content**. The
alternative standby trigger (`game_ctx+0x24==1` from `B:/FLAG.bin`, the update-resume path) is
likewise a file-*existence* check at splash, not a mount-completion event. So no edge on the
8→3→12→13 graph waits on a mount notification. [Proven]

### 1.4 Where the mount DOES matter on this path (the real couplings)

| coupling | effect | is it a state-8 gate? |
|---|---|---|
| `fs_storage_mount_init` != 0 | **hard hang at state 0** — statechart never starts | No — pre-statechart |
| B:/`*.bnl` booklist empty (state-12 entry) | book(13) still entered; product load/play has nothing to open | No — post-transition content |
| cover-OID selectors `*0x081da08c`/716/718 unseeded | `FUN_08037cec`'s *cover-family* match path can't fire; the **first-load branch** (gate bytes `akoid_buf[0]!=0`, `game_ctx[0x1d]==2`, `akoid_buf[0x21]==0xff`, `*0x08008c0d!=0xff`, `akoid_buf[0xb3]==0`) fires **without** them | No — and bypassed by first-load |
| discovery scan-root `*(ctx+8)` empty | codepage-mount gap; affects `.gme` discovery/play, not the state walk | No |

---

## 2. The settle counter — what "frozen at 2" actually was [Proven]

The "settle counter" is the byte `*0x081da078` (`DAT_08050f38`), zeroed by `state8_entry`,
incremented once per periodic **0x30 heartbeat** (nandboot timer slot → callback 0x08003994 →
coalesced post of `0x30{slot}`; the handler accepts only `args[0]==*0x08121e64`, which matches
by construction). It is **not a debounce that must reach a threshold to advance**; it selects
among the handler's branches:

- count==1 (`bVar1==1` after increment): one-time `usb_detect_2()` init, `game_ctx+0x1e=3`;
- count 2…0x27 with USB present + PMU bits (`0x0407000A` bit2/3, `0x04070001` bit 0x10) and
  `counter > 1`: post **0x105C → state 5 (USB-PC)**;
- count ≥ 0x28: post **0x1009 → state 6 (charger)** (or 0x100C on the error flag).

The upshot is a hardware-driven decision, not a stall: under a **USB-present** input
(0x040000BC bit8=1) the handler takes the USB branch and at the first tick where `counter > 1`
— i.e. count **2** — posts 0x105C and leaves state 8 deliberately, believing it is plugged
into a PC. Presenting the authentic battery-only condition (**GPIO 0x040000BC bit 8 = 0**)
makes the very first 0x30 tick take the `detect out!!` branch (0x100C) — or, on a quiet
battery boot, state 8 is **never entered at all** (entry needs event 0x105d, posted by content
leaves to re-poll the charger — not by the boot path). [Proven; full trace in
`state8-to-13-transition.md`.]

---

## 3. What the A: mount actually ungates

**Mount-gated vs independent: INDEPENDENT.** The A: mount will **not** ungate a state-8 park,
and a state-8 park was never evidence of a missing mount. What the authentic A: mount (the NFTL
superfloppy) ungates is:

1. **Boot past state 0** — `fs_storage_mount_init` returning 0 is the hard precondition for the
   statechart to start at all (§1.1). The A: mount ungates **state 0 → splash**, not state 8 →
   standby.
2. **Content for play** — codepage load, discovery scan-root, booklist, `a:/oidfilelist.lst`
   (§1.4) — the *post*-13 mount/play cascade, downstream of state 13.

---

## 4. Evidence index (this doc's claims)

| claim | status | evidence |
|---|---|---|
| mount runs before the statechart; failure = state-0 hang; no mount event posted | Proven | `app_init_main` 0x08038f5c; `0x0803a484`/`0x08038f5c` contain no event posts |
| state-8 handler reads only GPIO8 + PMU 0x0407000A/0x04070001 + counter; zero FS refs | Proven | `usb_connect_handler` 0x08050d28 (full read) |
| state-8 mapper is a guard-free event router (0x1009/0x100c/0x105c/default) | Proven | mapper `0x0804e7c0` (full read) |
| state8_entry = counter reset + 0x30 timer arm; no FS | Proven | `state8_entry` 0x08050c28 |
| 12→13 (0x1059) posted unconditionally; empty B: scan only sets error flags | Proven | `state12_default_action` 0x0803440c; `booklist_scan_bnl` 0x08034300 |
| "counter frozen at 2" = deliberate 0x105C exit under USB-present hardware, not a stall | Proven | `usb_connect_handler` 0x08050d28 (`1 < counter` → post 0x105C) |
| state 8 not entered on a quiet battery boot (entry event 0x105d from content leaves only) | Proven | standby mapper `0x0804e884`; no 0x105d poster in `0x08051b0c`/`0x080511a0` |
| tap→13 works with unseeded selectors (first-load branch), on the real NFTL mount | Proven (confirmed dynamically) | see `tt-emu` |
| selector seeding happens at product mount (gme_parse_header) | Inferred | measured 0 at standby; writer not byte-traced |
