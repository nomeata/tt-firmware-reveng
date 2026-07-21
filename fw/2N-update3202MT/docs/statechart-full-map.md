# The complete QHsm statechart map — tiptoi 2N (MT) pen application

> The definitive "how the firmware works" artifact for the application state machine. Produced by a
> full static decode of all **70 descriptor-table entries** (@0x08121d44), the **68-entry event-action
> table** (@0x0800b8d4), the **transition-action table** (@0x0800b544), every state mapper, and the
> event-posting corpus. All addresses are the unified runtime base **0x08009000** (Ghidra = runtime =
> pen dump = `data/PROG.bin` flat load; file offset = addr − 0x08009000). Nandboot HAL @0x08000000
> (`data/nandboot.bin`). Evidence tags: **[P]** = read directly from decomp/disasm; **[I]** = inferred
> from structure. Companion deep-dives: `statechart-framework.md`, `state8-to-13-transition.md`,
> `autonomous-mount-state8.md`, `product-init-and-runtime-tables.md`.

---

## 0. Executive summary — the shape of the machine

The pen runs a **table-driven hierarchical state machine (QHsm/QP-style)** with **70 states**. It has
three architectural layers dispatched on *every* pumped event, in order:

1. **State 9 — global pre-handler.** Gets first crack at every event. Routes the six "system" events
   (heartbeat 0x1046, timer 0x30, OID phases 0x105e/0x105f/0x1060, 0x1054) into the
   **transition-action table**. `0x1060` (a decoded OID tap) → `FUN_08037cec`, the cover/product
   classifier that opens books. Unmatched events pass through to the leaf.
2. **The active leaf** — the real UI/game state (splash, standby, book, a mini-game, USB, charger…).
3. **State 10 — global post-handler.** Sees anything the leaf left unconsumed. `0x1062` here re-pushes
   the current state (refresh); everything else triggers the leaf's `desc+8` unhandled-fallback hook.

The **application shape** is a shallow flat set of system/idle states with **one deep content subtree**:

```
root(0) ─┬─ splash(1) ── standby(3) ─┬─ book_mount(12) ── book(13) ── [56 game/activity states 14..69]
         └─ fw_update(2)             ├─ usb_detect(8) ─┬─ usb_msc(5)
                                     │                 └─ charging(6)
                                     └─ poweroff_prep(7) ── [via state 10] ── system_off(4)
```

**Power-on → book, in one line:** `root(0) →0x1001→ splash(1) →0x1014→ standby(3)`; a **cover-OID tap**
decodes to event **0x1060**, which the global state-9 handler routes to `FUN_08037cec`; on a product
match it posts **0x1058 → book_mount(12)**, which unconditionally posts **0x1059 → book(13)**. From
book(13), `gme_oid_dispatch` reads each tapped OID's GME game-type/product and posts a **launch event
(0x1015–0x1045)** that pushes one of **56 game/activity leaf states (14–69)** on top of book. Every game
leaf exits with **0x100c/0x1049** (pop one level, back to book) or **0x104a** (pop-all, back to standby).

**Idle/power management:** standby(3) counts heartbeats and, after ~300 idle ticks, does an **inline
hardware power-off** (it does *not* route through states 7/4 for the idle timeout). The graceful/OID
power-off path is `standby(3) →0x1047→ poweroff_prep(7) →0x1062(via state 10)→ system_off(4)` which runs
the full peripheral shutdown. USB/charger handling is a side subtree under standby: `0x105d` enters
`usb_detect(8)`, which branches to `usb_msc(5)` (mass-storage) or `charging(6)` on hardware presence
bits, or pops back to standby on "detect out" (`0x100c`).

The game subtree (14–69) implements the built-in **"Chomp" study product** plus the GME script engine
that runs downloaded `.gme`/`.bnl` book content: study/drill modes, three game hubs (13, 30, 32, 50)
that push mini-games, and the generic **GME game-type dispatcher** (types 16–23 → states 58–68; 17–23 decoded in gme-subtype-parameters.md addendum).

---

## 1. The framework — dispatch, tables, and status semantics

### 1.1 The active object (AO) @0x08008874 [P]

| field | offset | meaning |
|---|---|---|
| `*AO` | +0x00 | scratch: id of the state currently being dispatched (written by `sm_dispatch_event`) |
| `AO+1` | +0x01 | "consumed" flag, cleared each hierarchy move; gates fallback re-run |
| `AO+2` | +0x02 | =1: enable the `desc+8` unhandled-fallback path (`func_0x080076ac`) [P] |
| `AO+4` | +0x04 | **stack DEPTH** (u16) — *not* the leaf id (live pen: depth 3) [P, corrects prior notes] |
| `AO+8` | +0x08 | cascade free-list head |
| `AO+0xc` | +0x0c | cascade pending-list head |
| `AO+0x14` | +0x14 | **state stack pointer** — `*(AO+0x14)` = current leaf id (live pen: 0x45 = state 69) [P] |
| `AO+0x18` | +0x18 | **descriptor table** = 0x08121d44 |
| `AO+0x1c` | +0x1c | **event-action table** = 0x0800b8d4 |
| `AO+0x20` | +0x20 | **transition-action table** = 0x0800b544 |
| `AO+0x24` | +0x24 | last event id |

Each **descriptor** (0x10 bytes, rodata 0x080b0378–0x080b07b8) is
`{+0 entry, +4 exit, +8 unhandled-fallback, +0xc mapper}`. `AO+0x14` is initialised by `app_init_main`
to `func_0x08006ba4()` = **0x08007e80** (stack base in zero-init RAM) → **initial leaf = state 0**. [P mechanism / I zero]

> **Naming correction [P]:** `sm_set_state` (0x0800b65c) is **NOT** the statechart-state setter. It
> clamps its arg to [0,6], indexes u16 table @0x0800b9e4 `{0x18,0x60,0xA8,0x108,0x138,0x180,0}`, calls
> `FUN_0802265c` (rate math, clamp ≤0x400) and stores a mode/level byte at **0x081db904** — a 7-level
> **system rate/sound-profile** setter (driven by GME opcodes FEE0–FEE7), unrelated to the AO leaf.
> `app_init_main`'s `sm_set_state(3,0)` sets profile 3, not "initial leaf = standby". The real initial
> leaf is state 0 (above).

### 1.2 `sm_dispatch_event` (0x080f2c78) — one (state,event) dispatch [P]

The mapper returns `(result, status)`. `sm_dispatch_event` then acts on **status**:

| status | action |
|---|---|
| **0** | run **event-action[result]** (`AO+0x1c`); return its value; if 0, propagate to parent |
| **1** | run **transition-action[result]** (`AO+0x20`); same return/propagate convention |
| **2** | left untouched by the mapper → **return 1 = pass-through** (event not for this state) |
| **3** | **sibling transition**: replace the top-of-stack state with `result` |
| **4** | **push** child state `result` |
| **5** | **pop-all** — unwind the whole stack to the bottom |
| **≥6** | **pop** (status−5) levels (status 6 = pop 1) |

After any **3/4/5/6** hierarchy move, `event_loop_dispatch` (0x080f2b38) dispatches signal **0x1000** to
the landing state's mapper and runs the resulting event-action — the state's **init/entry action**. On
entry it calls `sm_state_entry` (`desc+0`); on exit `func_0x08007674` (`desc+4`). [P]

### 1.3 The pump and the three levels [P]

`FUN_0800b4a4` (app event pump) reads the input ring and calls `sm_dispatch_to_hierarchy` (0x080f2d70)
per event, which dispatches **state 9 (func_0x08006bc8=9) → current leaf → state 10 (=10)**, with
max-depth 11 (`func_0x08006bd8`). `func_0x08007620` pops the next cascade node each loop. `sm_set_state`'s
old "initial leaf" reading is wrong; the pump/levels are the truth. [P]

### 1.4 The action tables

**transition-action table @0x0800b544** (status-1 targets; only state 9 emits status 1) [P]:

| idx | fn | role |
|---|---|---|
| 0 | 0x0800acfc `heartbeat_1046_handler` | ++heartbeat @0x081da014, amp toggle, battery monitor |
| 1 | 0x0800ad80 | `mov r0,#1; bx lr` — no-op (swallows 0x1054) |
| 2 | 0x0804fdbc → `charger_out_handler` | 0x30 timer-slot demux |
| 3 | 0x0800b56c | 0x105e handler: reset USB-detect state, consume |
| 4 | 0x0800a9c8 | 0x105f partial-OID: audio feedback, posts 0x105b/0x1062 |
| 5 | 0x08037cec `FUN_08037cec` | **0x1060 cover/product classifier → posts 0x104a+0x1058** |
| 6–9 | (physically overlay the state-9 descriptor 0x0800b55c) | — |

**event-action table @0x0800b8d4** (68 entries; status-0 targets). It is essentially **one worker fn
per state** — EA[n] is the default handler for the state whose `desc+8` sits ~4 bytes below EA[n]. Key
entries: `[0]`=0x0804cecc `fwupdate_verify_image` (splash), `[1]`=0x0804e570 `root_action_boot_decide`,
`[6]`=0x08050d28 `usb_connect_handler`, `[9]`=0x08051b0c `standby_handler`, `[16]`=0x0803440c
`state12_default_action`, `[17]`=0x0803629c `gme_oid_dispatch`. Full state↔EA mapping in §2.

---

## 2. Per-state map (all 70 states)

Legend: **push N** = status-4 enter child N; **sib N** = status-3 sibling replace; **pop** = status-6
(one level); **pop-all** = status-5; **EA[n]** = event-action[n] default. Every game-leaf `desc+8`
fallback is a `bx lr` no-op unless noted.

### System / idle / power states (0–13)

| st | name | desc | entry(+0) | exit(+4) | mapper(+0xc) | default EA | parent ← entered by |
|---|---|---|---|---|---|---|---|
| **0** | `root` | 0x080b0398 | 0x0804e52c alloc 0x29d0 workbuf | 0x0804e54c clears `akoid[0x21]` (first-load gate) | 0x0804e678 | EA[1]=0x0804e570 | — (initial, depth 0) |
| **1** | `splash` | 0x080b0388 | 0x0804c1d4 battery gate + `B:/FLAG.bin`→`game_ctx+0x24`, voice 0x19 | 0x0804c3e4 kill timers/free/`game_ctx[0x1d]=2` | 0x0804e624 | EA[0]=0x0804cecc | 0 ←0x1001; 4 ←0x1003/0x1048 |
| **2** | `fw_update` | 0x080b0408 | 0x08052140 build update filename, `fs_open`, flag+0x208 | 0x080522f0 free, teardown | 0x0804e844 | EA[8]=0x08052320 | 0 ←0x1002 |
| **3** | `standby` | 0x080b0418 | 0x080511a0 re-arm OID poll, open log files | 0x08051ae4 stop stream, teardown | 0x0804e884 | EA[9]=0x08051b0c `standby_handler` | 1 ←0x1014 |
| **4** | `system_off` | 0x080b03d8 | 0x080508e0 `game_ctx[0x1d]=3` | no-op | 0x0804e76c | EA[5]=0x08050a24 shutdown | 10 ←0x1062 (push) |
| **5** | `usb_msc` | 0x080b03f8 | 0x08050fb8 stop player, usb-session flag | 0x0805102c restore vol, teardown | 0x0804e814 | EA[7]=0x08051138 | 8 ←0x105c (sib) |
| **6** | `charging` | 0x080b03a8 | 0x080504a4 "Enter Charge", draw anim | 0x080505ec kill timer/free | 0x0804e6b8 | EA[2]=0x08050670 `charge_count` | 8 ←0x1009 (sib) |
| **7** | `poweroff_prep` | 0x080b03c8 | 0x0805081c malloc(4) | 0x0805083c free, `game_ctx[0x1d]=2` | 0x0804e73c | EA[4]=0x08050864 posts 0x1062 | 3 ←0x1047 |
| **8** | `usb_detect` | 0x080b03e8 | 0x08050c28 arm 0x30 settle timer | 0x08050ca0 teardown, `akoid[0x58]=5` | 0x0804e7c0 | EA[6]=0x08050d28 `usb_connect_handler` | 3 ←0x105d |
| **9** | `global_pre` | 0x0800b55c | — | — | 0x0800a914 | (status-1 → TA table) | fixed level (never on stack) |
| **10** | `global_post` | 0x080b0378 | — | — | 0x0804e5fc | (0x1062→push cur) | fixed level (never on stack) |
| **11** | `orphan_overlay` | 0x080b03b8 | 0x08050738 | 0x0805075c | 0x0804e6f0 | EA[3]=0x08050788 | **dead** — no mapper pushes 11 |
| **12** | `book_mount` | 0x080b0488 | 0x08034300 scan `B:/*.bnl` | 0x080343d4 close handle | 0x0804eaac | EA[16]=0x0803440c posts 0x1059 | 3 ←0x1058/0x1057/0x105a |
| **13** | `book` | 0x080b0498 | 0x080345cc aud construct, voice 0x13 | 0x080348a8 `akoid[0x21]=0xff` re-arm gate | 0x0804eb08 `book_mode_handler` | EA[17]=0x0803629c `gme_oid_dispatch` | 12 ←0x1059 |

**State-0 `root` mapper 0x0804e678** [P]: `0x1001→sib 1 (splash)`; `0x1002→sib 2 (fw_update)`; default→EA[1].
EA[1] `root_action_boot_decide`: if pending update file exists **and** GPIO8==0 (on battery) → play
prompt, post 0x1002; else post 0x1001.

**State-1 `splash` mapper 0x0804e624** [P]: `0x1014→sib 3 (standby)`; `0x1048→pop`; `0x104a→pop-all`;
default→EA[0]=`fwupdate_verify_image`.

**State-3 `standby` mapper 0x0804e884** [P]: `0x1058/0x1057/0x105a→push 12`; `0x1047→push 7`;
`0x105d→push 8`; default→EA[9]=`standby_handler`. `standby_handler`: idle-mode (`game_ctx[0x1d]==8`)
heartbeat counter → **inline hardware power-off at >300 ticks** (`sm_set_state(0,0)`, amp off, halt
sequence — bypasses states 7/4); GPIO 0xB==1 → discovery + soft reboot; resume flag `game_ctx+0x24==1`
→ post 0x1058.

**State-4 `system_off` mapper 0x0804e76c** [P]: `0x1003/0x1048→sib 1 (wake→splash)`; `0x100c→pop`;
default→EA[5] full peripheral power-down (`sys_reset`, clock/power-gate MMIO). Entered **only** from
state 10 on 0x1062.

**State-8 `usb_detect` mapper 0x0804e7c0** [P]: `0x1009→sib 6 (charging)`; `0x105c→sib 5 (usb_msc)`;
`0x100c→pop (→3)`; default→EA[6]=`usb_connect_handler` (GPIO8/PMU settle logic). Fallback 0x08050ce8
plays a one-shot cue (voice 0x7e) before the first settle tick.

**State-9 `global_pre` mapper 0x0800a914** [P] — sets status **1** on match, else leaves 2 (pass to leaf):

| event | TA[result] | behavior |
|---|---|---|
| 0x1046 | TA[0]=0x0800acfc | heartbeat ++; returns 1 → leaf also sees it |
| 0x1054 | TA[1]=0x0800ad80 | no-op swallow |
| 0x30 | TA[2]=0x0804fdbc `charger_out_handler` | timer demux; returns 1 → leaf sees 0x30 |
| 0x105e | TA[3]=0x0800b56c | USB-detach: reset detect state, consume |
| 0x105f | TA[4]=0x0800a9c8 | partial-OID feedback (posts 0x105b/0x1062) |
| 0x1060 | TA[5]=0x08037cec `FUN_08037cec` | **cover classifier → post 0x104a+0x1058**; returns ≠0 on non-cover so book taps reach the leaf |

**State-10 `global_post` mapper 0x0804e5fc** [P]: `0x1062→push state 4 (off)`; everything else status 2
→ triggers the current leaf's `desc+8` fallback (auto-off hook + per-state fallback trigger).

**State-11 `orphan_overlay`** [P]: **unreachable** — no mapper in all 70 produces result 11 with
status 3/4. Vestigial USB/OID overlay. Mapper: `0x100c/0x105b→pop`, `0x104a→pop-all`, default→EA[3].

**State-12 `book_mount` mapper 0x0804eaac** [P]: `0x1059→push 13`; `0x100c/0x1049→pop (→3)`;
`0x104a→pop-all`; default→EA[16]=`state12_default_action` (posts 0x1059 **unconditionally** — empty
`B:/*.bnl` scan only sets an error flag; book(13) is entered regardless).

**State-13 `book` mapper `book_mode_handler` 0x0804eb08** [P]: `0x100c/0x1049→pop (→12)`; `0x104a→pop-all`;
default→EA[17]=`gme_oid_dispatch`; **plus 30 game-launch events, each `push`ing a sub-state** — the
gateway to the whole game subtree:

| ev→st | ev→st | ev→st | ev→st | ev→st |
|---|---|---|---|---|
| 0x1015→46 | 0x1016→47 | 0x1017→48 | 0x1018→49 | 0x1019→65 |
| 0x101a→30 | 0x101b→67 | 0x101c→50 | 0x101d→64 | 0x101e→42 |
| 0x101f→43 | 0x1020→44 | 0x1021→45 | 0x1022→66 | 0x1023→68 |
| 0x1024→59 | 0x1025→62 | 0x1026→18 | 0x1027→17 | 0x1028→60 |
| 0x1029→14 | 0x102a→58 | 0x102b→19 | 0x102c→61 | 0x102d→16 |
| 0x102e→63 | 0x102f→15 | 0x1038→32 | 0x1039→31 | 0x1045→69 |

### The book-content / game subtree (14–69)

`gme_oid_dispatch` (0x0803629c, EA[17]) reads the tapped OID's **GME game type** (`FUN_08034cbc`) and the
**current product id** (`*DAT_08036738`, set by product-switch OIDs 0x300–0x3E7; each product has a cover
OID — 0xC21↔6, 0x119A↔2, 0xF3F↔0xC, 0x713↔3, 0xB7B↔7, 0x89C↔10, 0xA34↔0xB, 0xCEA↔4, 0xA94↔9,
0x18A2↔0x12, 0x162C↔0xE, 0x16C0↔0xF, 0x1A32↔0x13, 0x1DB2↔0x15) and posts the launch event above. [P]

**Game-type → state (type-9 = product-specific; others = generic GME engine)** [P]:

| GME type | condition | event | state |
|---|---|---|---|
| 1,2,3,5,10 | — | 0x1029 | 14 |
| 4/40 | — | 0x102f | 15 |
| 6 | — | 0x102d | 16 |
| 7 | — | 0x1027 | 17 |
| 8 | — | 0x1026 | 18 |
| 9 | product 9, variant 0–3 | 0x1015–0x1018 | 46–49 |
| 9 | product 0xB, variant 0–3 | 0x101e–0x1021 | 42–45 |
| 9 | product 7 | 0x1038 | 32 |
| 9 | product 0xE | 0x101d | 64 |
| 9 | product 0xF | 0x1019 | 65 |
| 9 | else | 0x101a | 30 |
| 16 (0x10) | product 2 | 0x1028 | 60 |
| 16 | else | 0x102b | 19 |
| 17 | — | 0x102a | 58 |
| 18 | — | 0x1024 | 59 |
| 19 | — | 0x102c | 61 |
| 20 | — | 0x1025 | 62 |
| 21 | — | 0x102e | 63 |
| 22 | — | 0x1022 | 66 |
| 23 | — | 0x1023 | 68 |
| — | hdr@0x98≠0 (binary table) | 0x101b | 67 |
| — | "jump to separate" flag | 0x1045 | 69 |
| — | post-parse type 8 (product switch) | 0x101c | 50 |
| 253 | — | 0x1039 | 31 |

**Common game-leaf template** (states 14–29, 31, 33–49, 51–69 — mapper verified identical except the
EA index K) [P]:

| event | outcome |
|---|---|
| 0x100c / 0x1049 | pop 1 level → parent |
| 0x104a | pop-all → standby(3) |
| default (0x1000 init, 0x30, 0x105f, 0x1060, …) | EA[K] = the state's game engine tick |

Entry template: `akoid_buf[0x21]=0xB` (in-game mode byte), `physpool_alloc(ctx)`, init helpers, play
intro cue. Exit template: drain/stop audio (`is_audio_playing`+`fwl_voice_play_2`, some `FUN_080ab424`
amp-off + delay), `physpool_free`, clear mode byte. Every game defact calls `FUN_08034bc0`
(`game_lowbatt_notice_tick`, voice ids 0x2C/0x17/0x1A/0x14) and posts 0x100c on end, most also 0x104a/0x105d.

**The three game hubs** (non-template mappers that push children):

- **State 13 (book)** — the top hub; 30 launch events above.
- **State 30 `game_hub`** (mapper 0x0804f17c) [P]: `0x1030→26, 0x1031→24, 0x1032→23, 0x1033→20,
  0x1034→25, 0x1035→27, 0x1036→22, 0x1037→21, 0x103a→28, 0x103b→29`; pop/pop-all; default→EA[31]. The
  built-in "Chomp" minigame selector; children 20–29 are grandchildren of book(13).
- **State 32 `discovery_mode_hub`** (mapper 0x0804f950, product 7) [P]: `0x103c→38, 0x103d→40, 0x103e→36,
  0x103f→39, 0x1040→33, 0x1041→37, 0x1042→41, 0x1043→35, 0x1044→34`; pop/pop-all; default→EA[56]
  `discovery_mode` ("Enter Discovery Mode"). Children 33–41 are the nine `s_study_readinggame` leaves.
- **State 50 `selection_board_mode`** (mapper 0x0804ed44, product-type 8) [P]: `0x1005→54, 0x1006→51,
  0x1007→53, 0x1008→52, 0x1010→55, 0x1011→57, 0x1012→56`; pop/pop-all; default→EA[18]. Its defact drives
  `board_oid_selection_sm` (0x080610a4), a two-axis control-OID (0xe75–0xea6) selection FSM that posts
  the 0x1005–0x1012 sub-mode events. Children 51–57 are its grandchildren-of-book hub sub-games.

**Game/activity leaf states 14–69** (name / identity / EA index):

| st | name | identity (evidence) | EA[K] |
|---|---|---|---|
| 14 | `st14_gme_script_game` | GME play-script game mode | 32 |
| 15 | `st15_study_step` | study/drill w/ repeat-addr ("change sstep","Repeat Addr") | 38 |
| 16 | `st16_study_list_a` | study/lesson-list (2-level offset table) | 39 |
| 17 | `st17_study_list_b` | study-list variant (paired media ids) | 40 |
| 18 | `st18_study_return` | resume study at backed-up OID ("Returnstudy","Backupoid") | 41 |
| 19 | `st19_study_mode_x` | small study mode (no content-table read) — partial | 42 |
| 20 | `st20_game_step_confirm` | step-confirm game ("Confirm step") | 49 |
| 21 | `st21_minigame2` | "MiniGame Two Init" | 53 |
| 22 | `st22_minigame3_linecut` | "Exit MiniThree", line-cut game | 52 |
| 23 | `st23_game4_quiz` | "Game Four Init", Player/Prof.Mad win | 48 |
| 24 | `st24_game5` | "Game Five Init" | 47 |
| 25 | `st25_game7_findtarget` | "Game Seven Start","Target touched" | 50 |
| 26 | `st26_game_ask_question` | "Begin Ask Question!!!" quiz | 46 |
| 27 | `st27_minigame6` | "MiniGame Six Init" | 51 |
| 28 | `st28_special_game1` | "Spe one start" | 54 |
| 29 | `st29_special_game2` | "Spe Two Begin" | 55 |
| 30 | `game_hub` | **minigame selector hub** (20–29) | 31 |
| 31 | `st31_book_aux_mode` | OID-band-filtered aux mode (type 253) — partial | 66 |
| 32 | `discovery_mode_hub` | **product-7 discovery hub** (33–41) | 56 |
| 33–41 | `study_reading_game{1..9}` | the nine `s_study_readinggame` leaves (children of 32) | 57–65 |
| 42–45 | `prod0b_game_mode{0..3}` | product-0xB modes (43 = quiz w/ wrong-answer limit + score tiers) | 27–30 |
| 46–49 | `prod09_game_mode{0..3}` | product-9 GME-scripted modes (cover 0xA94) | 10–13 |
| 50 | `selection_board_mode` | **product-type-8 selection board hub** (51–57) | 18 |
| 51–57 | `hub_subgame_{A..G}` | state-50 hub sub-games (families 0xEBA/0xEC4) | 19–25 |
| 58 | `gme_gametype17` | GME type 17 sequential lesson steps (`game58_*`, entry 0x080894fc; hardwired 0x11EA/0x11EB control pair) | 43 |
| 59 | `gme_gametype18` | GME type 18 scored quiz, silent retries (`game59_*`, entry 0x0808adb0; 10-tier cascade) | 44 |
| 60 | `gme_gametype16_p2` | GME game type 16, product 2 | 45 |
| 61 | `gme_gametype19` | GME type 19 beat-the-clock quiz (`game61_*`, entry 0x0807a8e4; u5=time limit, PLL9=time-up; top win sets akoid+0xdcf) | 33 |
| 62 | `gme_gametype20` | GME type 20 find-N-targets single round (`game62_*`, entry 0x0807bd68; u5=taps to complete) | 34 |
| 63 | `gme_gametype21` | GME type 21 category ladder quiz (`game63_*`, entry 0x0807d440; 4 hardwired bands over 13 subgames, 3 rounds) | 35 |
| 64 | `game_reading_prod0e` | type-9 native reading game, product 0xE (`game_start`/`game_reading_sm`) | 26 |
| 65 | `game_prod0f` | type-9 game, product 0xF (RNG-cue) | 14 |
| 66 | `gme_gametype22` | GME type 22 scavenger hunt (`game66_*`, entry 0x0807ebe0; pre-drawn task list, ~20s inactivity timeout; top win sets akoid+0xdde) | 36 |
| 67 | `gme_binary_subgame` | GME embedded-binary sub-game (hdr@0x98 table) | 15 |
| 68 | `gme_gametype23` | GME type 23 find-everything percentage game (`game68_*`, entry 0x0807fcbc; 0x1B54-0x1B70 alias table, 75%/50% tiers) | 37 |
| 69 | `gme_separate_binary` | GME "separate" main-binary mode (hdr@0xA8, "exitstudy_separate"); +**0x101b→push 67** | 67 |

---

## 3. Event vocabulary

Posting primitives: `event_queue_post` (0x0800b35c, cascade queue) and `func_0x08003644` (nandboot,
input ring, dedup/coalesce). Hardware posters live in nandboot: timer cb 0x08003994 (heartbeat/0x30),
OID decoder 0x08005858 (0x1060), OID phase map 0x08003930 (0→0x1046, 1→0x105f, 2→0x1060), USB notifier
0x08005400 (0x105e / 0x104a+0x105d).

### Framework signals (not queued)

| id | meaning | source |
|---|---|---|
| 0x1000 | init-after-transition; dispatched to the new leaf after every hierarchy move; result's EA = the state's init action | `event_loop_dispatch` [P] |
| 0x1001 / 0x1002 | boot-select: normal→0x1001→splash(1); update-pending & on-battery→0x1002→fw_update(2) | `root_action_boot_decide` EA[1] [P] |

### Queued events

| id | meaning | posted by | consumed by |
|---|---|---|---|
| **0x30** {slot} | periodic sw-timer tick (non-poll slots: state-8 settle, GME timers) | nandboot 0x08003994 | state-9 TA[2] `charger_out_handler`; state-8 default; `gme_oid_dispatch` |
| 0x60 {0/1/2} | audio/stream feed change | audio streamer `FUN_080490c4` | low-level pump (no C consumer) |
| 0x1003 | wake (state-4) | **dead** (no poster) | state-4 mapper |
| 0x1005–0x1008, 0x1010–0x1012 | state-50 hub sub-mode launches | `board_oid_selection_sm` 0x080610a4 | state-50 mapper → 51–57 |
| **0x1009** | "go to charger" (USB settle count ≥0x28) | `usb_connect_handler` | state-8 → charging(6) |
| 0x100a | charger-related | `standby_handler` | state-6 mapper |
| **0x100c** | universal back/exit/detect-out/cancel (pop one level) | 72 fns: `usb_connect_handler` (GPIO8==0), `gme_exec_command`, every game leaf | 64 mappers (default exit route) |
| **0x1014** | splash done → standby | `fwupdate_verify_image` | splash mapper → standby(3) |
| **0x1015–0x102f, 0x1038/0x1039, 0x1045** | **game-launch events** (book→game state; see §2 table) | `gme_oid_dispatch` (per GME game-type/product) | `book_mode_handler` (13) → push game state |
| 0x1030–0x103b | minigame-select (hub 30); 0x103a from 0x08074bb8 | GME/game chaining + 0x08074bb8 | state-30 mapper |
| 0x103c–0x1044 | discovery sub-game select (hub 32) | `discovery_subgame_select_post` 0x0809aa20 | state-32 mapper |
| **0x1046** | OID-poll heartbeat (current poll slot tick) | nandboot 0x08003994 (`hal_event_id_for(0)`) | state-9 TA[0] `heartbeat_1046_handler` (++counter, amp toggle, battery monitor `FUN_080afd78`) |
| 0x1047 | standby → poweroff_prep(7) | **no static poster** (dead / dynamic game-config only) | standby mapper → state 7 |
| 0x1048 | wake / product-enable (a `standby_handler` gate enabler) | dynamic (game-config) | splash/state-4/standby mappers |
| **0x1049** | close current level (sibling of 0x100c); `FUN_08050788` rewrites to 0x105f | dynamic | game/book/mount mappers (pop) |
| **0x104a** | abort — stop audio & unwind (pop-all); arg 0xfe on USB-plug | 46 fns: `FUN_08037cec`, state-9 0x0800a9c8, USB notifier, battery monitor, game leaves | 61 mappers (pop-all route) |
| 0x1054 | reserved — swallowed by state-9 TA[1] no-op | **dead** (no poster) | state-9 only |
| 0x1057 | standby alt-mount route | **dead** (no poster) | standby mapper |
| **0x1058** | open product → enter book_mount(12) | `FUN_08037cec` (cover tap), `standby_handler` (resume flag), `FUN_080afd78` (battery path) | standby mapper → push 12 |
| **0x1059** | mount done → enter book(13) | `state12_default_action` 0x0803440c (unconditional) | state-12 mapper → push 13 |
| 0x105a | standby alt-mount route | **dead** (no poster) | standby mapper |
| 0x105b | OID-feedback follow-up (with 0x1062) | state-9 0x0800a9c8 | state-11 mapper |
| **0x105c** | "usb connect pc!!" → usb_msc(5) | `usb_connect_handler` (USB present + PMU, counter>1) | state-8 → state 5 |
| **0x105d** | USB/charger attached → enter usb_detect(8) | nandboot 0x08005400 (GPIO8==1); helper `FUN_0803d094` (posts 0x104a+0x105d from 43 content/game leaves) | standby mapper → push 8 |
| **0x105e** | USB detached notification | nandboot 0x08005400 (GPIO8==0) | state-9 TA[3]: reset detect state, consume |
| **0x105f** {phase} | OID partial/present (decode phase 1) | nandboot `hal_event_id_for(1)`; `FUN_08050788` | state-9 TA[4]=0x0800a9c8; ~60 content handlers |
| **0x1060** {code} | OID decoded / tap (decode phase 2) | nandboot 0x08005858 (`hal_event_id_for(2)`) | **state-9 TA[5]=`FUN_08037cec`** → cover match posts 0x104a+0x1058; else propagates to leaf `gme_oid_dispatch` |
| **0x1062** | re-enter/refresh current state (re-push, rerun entry) | state-9 0x0800a9c8, `FUN_08050864` (state-7 EA), `fwupdate_verify_image`, GME | state-10 mapper (re-push); also enters state 4 |
| 0x101b | binary sub-game launch | `gme_oid_dispatch` + state-69 action | book/state-69 mapper → state 67 |
| 0x101c | selection-board / product-switch | `gme_oid_dispatch` | book mapper → state 50 |

**Notes.** The earlier "0x30xx GME software timers" phrasing is imprecise — GME timers post **0x30 with
the slot handle as the arg**, not a 0x30xx id family (corpus scan found no 0x30xx posts). Dead
vocabulary (mapper route but no static poster anywhere): **0x1003, 0x1047, 0x1054, 0x1057, 0x105a** —
legacy transitions retained in the routers; dynamic script-fed posting of 0x1047/0x1048 from per-game
config fields cannot be fully excluded.

---

## 4. State graph

```
                          ┌─────────────────────────────────────────────┐
   (initial, depth 0)     │                                             │
        root(0)           │  GLOBAL LEVELS (dispatched every event):     │
        │  │              │   state 9  = pre-handler  (0x1046,0x30,      │
   0x1001│  │0x1002       │              0x105e/f, 0x1060→FUN_08037cec)  │
        ▼  ▼              │   leaf     = the boxes below                 │
   splash(1) fw_update(2) │   state 10 = post-handler (0x1062→re-push,   │
        │                 │              →state 4 off; else fallback)    │
   0x1014│                └─────────────────────────────────────────────┘
        ▼
   ┌─────────────────────────── standby(3) ───────────────────────────┐
   │  idle >300 ticks ─► INLINE hardware power-off (not via 7/4)       │
   │  GPIO 0xB ─► discovery + soft reboot                              │
   └───┬───────────────┬────────────────────────┬─────────────────────┘
       │0x1047         │0x105d                   │0x1058 (cover tap via state-9 FUN_08037cec,
       ▼               ▼                         │        or resume flag game_ctx+0x24)
  poweroff_prep(7)  usb_detect(8) ──0x105c──► usb_msc(5)      ▼
       │            │       └──────0x1009──► charging(6)   book_mount(12)
       │0x1062      │ 0x100c (GPIO8==0)                        │0x1059 (unconditional)
       ▼ (state 10) │ ─► pop back to standby                  ▼
  system_off(4) ◄───┘                                       book(13) ◄──── pop (0x100c/0x1049)
   full shutdown                                              │  ▲
   0x1003/0x1048 ─► splash (wake)                             │  │ pop-all (0x104a) ─► standby
                                                              │  │
              ┌───────────── gme_oid_dispatch posts ──────────┘  │
              │  0x1015..0x1045 (GME game-type/product)          │
              ▼                                                  │
   56 game/activity leaves (14..69):                             │
     • direct children of book(13): 14–19,31,42–49,58–69         │
     • game_hub(30) ─0x1030..0x103b─► minigames 20–29            │
     • discovery_hub(32) ─0x103c..0x1044─► study games 33–41     │
     • selection_board(50) ─0x1005..0x1012─► hub games 51–57     │
     every leaf: 0x100c/0x1049 ─► pop to parent; 0x104a ─► pop-all to standby ─┘
```

**Where book mode sits / how entered & exited:** book(13) is a child of book_mount(12), which is a child
of standby(3). Entered by a cover-OID tap (0x1060 → state-9 `FUN_08037cec` → 0x1058 → 12 → 0x1059 → 13)
or the update-resume flag. Exited by 0x100c/0x1049 (pop → mount → standby) or 0x104a (pop-all → standby).
In-book OID taps route through `gme_oid_dispatch` (EA[17]) to play media, or push a game leaf.

**Role of state 9 / state 12:** state 9 is the always-first global pre-handler — the single most
important node: it owns the OID-tap classifier that turns taps into book-opens, plus the heartbeat and
timer routing. State 12 (book_mount) is a transient "mount the product then immediately advance"
state — its only job is to scan `B:/*.bnl` and post 0x1059 to reach book(13).

**Sleep/off triggers:** idle timeout (300 heartbeats at standby → inline power-off); explicit power-off
OID (standby → 0x1047 → poweroff_prep(7) → 0x1062 → system_off(4)). Wake from system_off = 0x1003/0x1048
→ splash.

---

## 5. Proven vs Inferred, and open items

**Proven** (read from decomp/disasm): the entire descriptor/event-action/transition-action tables; all
70 mappers' event→(result,status) routes; the three-level dispatch; states 0–13 hooks; the game-launch
event fabric (0x1015–0x1045) and its GME game-type/product keying; the hub mappers (13/30/32/50); the
event vocabulary posters/consumers; `sm_set_state` is a rate/mode setter, not the AO state; AO+4 =
depth, AO+0x14 = leaf.

**Inferred / partial:**
- Exact game identities for states 19, 31 (marked "partial") and the precise gameplay of several
  GME-type leaves (58–69) beyond "runs the GME script engine of type N".
- The cover-OID **mode selector `0x081da08c`** must match for `FUN_08037cec` to fire; who seeds it at
  product mount is not byte-traced (see `autonomous-mount-state8.md` §7.6).
- Dynamic posters of 0x1047/0x1048 (likely from per-game content config fields; no static poster).
- `sm_set_state`'s 7-level table semantics (rate/sound-profile) — bytes proven, meaning inferred.

**Cross-references:** state-8 hardware gating & the cover-tap path — `state8-to-13-transition.md`,
`autonomous-mount-state8.md`. Runtime table ground-truth vs pen dump — `product-init-and-runtime-tables.md`.
Framework primitives — `statechart-framework.md`.
