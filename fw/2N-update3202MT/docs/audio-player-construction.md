# g_pAudPlayer construction — the statechart trigger, the caller chain, and its dependencies

All addresses are the unified runtime base **0x08009000** (Ghidra = runtime = `data/PROG.bin` flat,
file off = addr − 0x08009000). Evidence tags: **[Proven]** = read from decomp/disasm/literal scan;
**[Inferred]** = deduced, not byte-traced.

Primary sources (named decompilation, per-function `<addr>.c`):
`aud_player_construct` 0x080ab47c, `aud_player_alloc` 0x08032320, `play_media_setup` 0x080ab6ac,
`FUN_080345cc` 0x080345cc (state-13 entry hook), `sm_dispatch_event` 0x080f2c78,
`event_loop_dispatch` 0x080f2b38, `sm_state_entry` 0x080f2e0c, `audio_ring_setup` 0x080223a8,
`audio_ring_build` 0x08022aa0, `dac_bringup` 0x08033334, `FUN_08033004` 0x08033004,
`FUN_0804c1d4` 0x0804c1d4 (splash entry), `is_audio_playing` 0x0800b024.

---

## 0. One-line answer

**`g_pAudPlayer` (`*0x081db228`) is constructed by the ENTRY HOOK of statechart state 13 (book mode):
`FUN_080345cc` 0x080345cc calls `aud_player_construct()` unconditionally, near the top, the moment the
HSM performs the status-4 ENTER into state 13 (event `0x1059`).** It is *not* built by the first
`play_media` — `play_media_setup` asserts `"g_pAudPlayer is NULL"` and bails if the slot was never
written. The construction itself has **zero NAND/FS dependencies** (heap + static descriptors + DAC/amp
MMIO only); NAND matters only *upstream*, to reach state 13 at all. The corollary matters for anyone
running the firmware: a state machine that jumps *to* state 13's mapper without executing the genuine
`0x1058 → ENTER 12 → 0x1059 → ENTER 13` transitions (state8-to-13-transition.md §7) skips the entry
hook and never builds the player; drive the authentic ENTER and the firmware builds it by itself.

---

## 1. Q1 — the writer of `g_pAudPlayer` and its caller chain [Proven]

### 1.1 The sole writer

`aud_player_construct` 0x080ab47c, operating on the fwl-audio module struct at **`0x081db220`**
(`DAT_080ab3c0`, resolved from PROG): byte `[0]` is a flags field, word `[+8]` is `g_pAudPlayer`.
The body, in prose:

- Guard on flag bit `0x40` ("already constructed"); the whole thing runs at most once.
- `dac_bringup(...)` (0x08033334) — DAC + clocks MMIO.
- `FUN_08033004(1)` — amp/codec power path (MMIO + GPIO).
- `module[+8] = aud_player_alloc()` — **the sole store of `g_pAudPlayer` (`*0x081db228`)**.
- register the player-getter callback (`0x0800b5a8`) and a notify callback (`0x0800b5b8`).
- `aud_player_reset(module[+8], FUN_0800b6a4(), 0, game_ctx+0x11 /*volume*/)`.
- clear the last-voice cache (`*0x081226f0 = 0xffffffff`).
- set flag bit `0x40` (never cleared afterwards → construct-once).

**Uniqueness [Proven by full-binary scan]:** the literal `0x081db220` occurs at exactly four aligned
pool slots in PROG — `0x0800b088`/`0x0800b0bc` (pure *reads* in `is_audio_playing` 0x0800b024 and the
status predicate at 0x0800b08c: `ldr r1,[r2,#8]`, no store), `0x0800b5b4` (the 3-instruction *getter*
0x0800b5a8: `ldr r0,[pc,#4]; ldr r0,[r0,#8]; bx lr` — the callback registered by the constructor
itself), and `0x080ab3c0` (the constructor's own pool). `0x081db228` appears nowhere as a literal;
`data/nandboot.bin` references neither address. **`aud_player_construct` is the only writer.**
No code ever clears flag bit 0x40 (a decomp grep over all 15 `DAT_080ab3c0` users finds no `& 0xbf`),
so construction happens **exactly once per power-on** and re-entering state 13 is idempotent.

### 1.2 `aud_player_alloc` 0x08032320 — what gets built [Proven]

- `FUN_08038c7c(0xec)` — heap-alloc + zero the 0xec player object (returned = the g_pAudPlayer value);
- copies the **static DAC descriptor** from PROG pools `0x080323d0..f8` (fn ptrs: 0x08009890,
  0x0800e0b0/bc/c8/d4, heap fns 0x08038c7c/ba4/be0/ce4, 0x080ae7dc, byte ptr 0x08009c71) and calls
  `audio_ring_setup` 0x080223a8 → `audio_ring_build` 0x08022aa0: builds the **SDRAM audio-out ring
  singleton `*0x08008d2c`** (points at the static 0x4c-byte object **0x08008d30**; `DAT_08022b24 =
  0x08008d2c`, `DAT_08022b28 = 0x08008d30` resolved), state `ring[0x10]=0x3000`, chunk size
  `ring[0xb]=[0xc]=0x5dc`, PCM buffer heap-alloc'd via descriptor callback (0x08038ce4) into
  `ring[+0x44]` (alloc failure → ring torn down, returns 0);
- `player[0xc] = 0xffffffff`, then `FUN_080322a0()`.

### 1.3 All callers of `aud_player_construct` [Proven — decomp grep, complete]

| caller | role | condition |
|---|---|---|
| **`FUN_080345cc` 0x080345cc** | **state-13 (book) ENTRY hook** — descriptor `0x080b0498[+0]`; the *only* reference to 0x080345cc in all of PROG is that descriptor slot | **unconditional**, line 38, before anything else of substance (first FS access and the "book open" prompt voice 0x13 come after) |
| `play_media_setup` 0x080ab6ac | the play path | **cannot do first construction**: it first runs `ptr_nonnull_check(*(0x081db220+8))` — slot NULL → `dbg_assert("g_pAudPlayer is NULL")` + return. Only if the slot is already non-NULL *and* flag bit 0x40 is somehow clear does it re-construct (a stale-pointer repair path, never the bootstrap) |
| `FUN_0804c1d4` 0x0804c1d4 | splash (state 1) entry | only inside the `B:/FLAG.bin` **update-resume** branch, before `fwl_play_voice_by_id(0x19)` (state8 doc §6.3) |
| `FUN_0804e570` 0x0804e570 | **event-action[1]** (table 0x0800b8d4[1]) — fw-update finish | after `fw_update_cleanup`, only when GPIO8==0 (battery), to speak the battery-low voice |
| `fwupdate_verify_image` 0x0804cecc | event-action[0] — USB/update/product-test service | 3 sites, update path only |
| `fw_update_cleanup` 0x08052768 | update tail | update path only |
| `FUN_0804c090` / `FUN_0804bf84` | digit-speech helpers (speak a number) | BL-called voice utilities, gated on `is_audio_playing()==0` |

So outside the firmware-update/resume family, **the one constructor on the autonomous path is the
state-13 entry hook**: it fires at *book-mode entry*, before any play — not lazily inside an
"audio-playing sub-state".

### 1.4 How the ENTER transition reaches the hook [Proven]

`sm_dispatch_event` 0x080f2c78: a mapper returning **status 4** routes to `event_loop_dispatch`
0x080f2b38, whose status-4 branch (lines 42-53) does: depth check (`func_0x08006bd8()`=11 max) →
call the current frame's `+4` hook if set → **push a new 0xc-byte frame** on the state stack
(`obj+0x14`), store the new state id into frame[0] and `*obj` → **`sm_state_entry()` 0x080f2e0c**,
which tail-calls **`descTbl[newstate][+0]`** = the entry hook. For state 13 the descriptor is
0x080b0498 = `{entry 0x080345cc, exit 0x080348a8, +8 0x080348f8, mapper 0x0804eb08}` (words read from
PROG). Then the framework self-dispatches signal `0x1000` to the new state's mapper (the "initial"
event, `event_loop_dispatch` lines 56-59). The status-3 branch (lateral become-state) also runs
`sm_state_entry()`. So **any authentic ENTER of state 13 runs `FUN_080345cc` → `aud_player_construct`.**

---

## 2. Q2 — the autonomous sequence, book mode → playing player [Proven]

(Upstream 3→12→13 chain: state8-to-13-transition.md §7, all Proven. Repeated here compressed.)

```
standby (state 3)
  cover-OID tap → sensor posts 0x1060 → global state-9 mapper 0x0800a914: result 5 status 1
    → trans-action[5] = FUN_08037cec: cover match → post 0x104a, 0x1058
  0x1058 → standby mapper 0x0804e884: result 12, STATUS 4  →  ENTER state 12
      entry FUN_08034300: scan/mount B:/*.bnl                        (NAND: content)
      default action FUN_0803440c: post 0x1059
  0x1059 → state-12 mapper 0x0804eaac: result 13, STATUS 4  →  ENTER state 13
      ── event_loop_dispatch status-4 → sm_state_entry → descTbl[13][+0] = FUN_080345cc ──
      FUN_080345cc line 38:  aud_player_construct()
          dac_bringup → FUN_08033004(1) → aud_player_alloc → *0x081db228 = player
          (ring *0x08008d2c built, callbacks registered, aud_player_reset(volume))
      first-ever entry ((*0x081da086 & 2)==0): FUN_08034588 + fwl_play_voice_by_id(0x13)
          = the "book open" prompt — ALREADY plays through the just-built player
  in-book content tap → 0x1060 → state-9 FUN_08037cec (non-cover → propagate)
    → leaf mapper book_mode_handler 0x0804eb08: result 17, status 0
    → event-action[17] = gme_oid_dispatch 0x0803629c: resolve OID via a:/oidfilelist.lst
    → play_media 0x080ab7b4 → play_media_setup 0x080ab6ac:
         ptr_nonnull_check(player) OK; flag&0x40 set → NO reconstruct
         aud_player_reset → aud_player_set_source (codec id → player+0xe9) → medialib_open
         → aud_player_play 0x08032c94 → decode loop → DAC DMA
```

**Answer:** yes — the statechart reaches the constructor *naturally and before any play*: it is the
book-mode entry action, triggered by event `0x1059` (itself posted mechanically by state 12's default
action). By the time the first `play_media` runs, the player exists. There is **no separate
"audio-playing sub-state"** doing the construction; the first sound (prompt voice 0x13) already uses it.

### 2.1 The one subtlety: dispatching *to* state 13 is not entering it [Proven]

Only a genuine **status-4 ENTER** through `event_loop_dispatch` (frame push + `sm_state_entry`) runs
`FUN_080345cc` and hence the constructor. Merely dispatching an event *to state 13's mapper* (e.g. a
direct `sm_dispatch_event(13, …)`) leaves `*0x081db228 == 0`, producing exactly the
`"g_pAudPlayer is NULL"` assert in `play_media_setup`. Any run that genuinely transitions into state 13
through the HSM (via the real `0x1058/0x1059` dispatches) gets the player for free.

---

## 3. Q3 — what the construction depends on [Proven]

**Everything is firmware-internal (SDRAM heap + static PROG data + MMIO). No fs_open/fs_read/NAND
access anywhere in the `aud_player_construct → dac_bringup / FUN_08033004 / aud_player_alloc /
audio_ring_build` chain.**

| dependency | where | note / constraint |
|---|---|---|
| SDRAM heap (`FUN_08038c7c` alloc: 0xec player + 0x5dc PCM buffer) | initialized in `app_init_main` | already satisfied once init has run |
| static DAC descriptor (pools 0x080323d0..f8) + ring singleton slot `*0x08008d2c` / object 0x08008d30 | PROG static data | nothing to do (ring must NOT be pre-seeded non-zero — `audio_ring_build` no-ops if `*0x08008d2c != 0`, and `medialib_open`'s first gate `medialib_open_ring_check` 0x08022f0c requires the ring to exist) |
| `dac_bringup` 0x08033334: sets codec-on byte `*0x08008c90 = 1`; board-variant byte `*0x08008c18` selects `FUN_080ef028` (clock enables MMIO 0x04000008 / 0x04000064 / 0x04090000) vs the internal-DAC path (MMIO 0x0400005c, 0x04000008, **0x04080000 \|= 1**, 0x04000064, 0x04000010); GPIO `func_0x080077d0(5,1)`; `dac_set_rate(8000)` | MMIO writes only | benign under the existing MMIO model; no read-back gates |
| `FUN_08033004(1)`: amp power — reads `*0x08008c90` (just set to 1 → takes the "turn amp on" arm, sets it to 2), GPIO via `func_0x08003110` (descriptors 0x0811c75c/76c/778), MMIO 0x0400005c/0x04000068, HAL delay `func_0x08007938(2)` | MMIO/GPIO writes + delay | the `halt_baddata` arm is only on the param==0 (power-*off*) path with state!=2 — not reached from the constructor |
| volume byte `game_ctx+0x11`, mode from `FUN_0800b6a4()` | RAM | already seeded by init |

**NAND/FS prerequisites are strictly upstream/downstream, not in the constructor:** upstream, whatever
it takes to *reach* state 13 (valid NFTL so `fs_storage_mount_init` doesn't hang; a matching `B:/*.bnl`
so `FUN_08037cec`/mount fire — state8 doc §7.6); downstream, `a:/oidfilelist.lst` + media inside the
mounted archive for a tap to have something to play. Also note the state-13 first-entry prompt
`fwl_play_voice_by_id(0x13)` will *use* the fresh player immediately — with an empty/absent voice list
it takes the error path but does not undo the construction (the 0x40 flag stays set). [Inferred: the
prompt's failure mode; the flag persistence is Proven.]

---

## 4. Summary — the condition for the firmware to build the player itself

The firmware constructs `g_pAudPlayer` entirely on its own **iff the state machine performs a genuine
status-4 ENTER into state 13**:

1. the authentic transitions must run (`0x1058`/`0x1059` through the real tables) so
   `event_loop_dispatch`'s status-4 branch executes the entry hook — a direct
   `sm_dispatch_event(13, …)` or `sm_set_state(13)` does **not**;
2. no extra hardware state is needed beyond the ordinary MMIO defaults (§3) — the constructor is
   write-only to HW;
3. after ENTER-13, `*0x081db228 != 0` and flag `*0x081db220 & 0x40`; the play chain
   (`gme_oid_dispatch → play_media → medialib_open → …`) then proceeds normally.

---

## 5. Evidence index

| claim | status | evidence |
|---|---|---|
| `aud_player_construct` is the sole writer of `*0x081db228` | **Proven** | literal scan of PROG for 0x081db220 → pools 0x0800b088/0bc/5b4/0x080ab3c0; the first three disasm to loads only; no 0x081db228 literal; no nandboot refs |
| `DAT_080ab3c0 = 0x081db220`; store is `*(base+8)` | **Proven** | PROG word @0x080ab3c0; `0x080ab47c.c` L20-21 |
| state-13 descriptor 0x080b0498 = {entry 0x080345cc, exit 0x080348a8, …, mapper 0x0804eb08} | **Proven** | PROG words @0x080b0498 |
| `FUN_080345cc` referenced only by that descriptor; calls the constructor unconditionally | **Proven** | literal scan for 0x080345cc; `0x080345cc.c` L38 |
| status-4 ENTER runs `sm_state_entry` → `descTbl[new][+0]` | **Proven** | `0x080f2b38.c` L42-53; `0x080f2e0c.c` |
| `play_media_setup` asserts on NULL slot; only re-constructs on stale non-NULL + flag clear | **Proven** | `0x080ab6ac.c` L21-30 |
| flag bit 0x40 never cleared (construct-once) | **Proven** | grep over all 15 `DAT_080ab3c0` users: no `&0xbf` write |
| ring singleton `*0x08008d2c`, object 0x08008d30, PCM 0x5dc via heap callback | **Proven** | `0x08022aa0.c`; PROG words @0x08022b24/28/2c |
| constructor chain touches no filesystem | **Proven** | `0x080ab47c.c`, `0x08032320.c`, `0x08033334.c`, `0x08033004.c`, `0x080223a8.c`, `0x08022aa0.c` — zero fs_* calls |
| other constructor callers are update/resume/voice-utility paths | **Proven** | event-action table 0x0800b8d4[0]/[1]; `0x0804c1d4.c`, `0x0804e570.c`, `0x0804cecc.c`, `0x08052768.c`, `0x0804c090.c`, `0x0804bf84.c` |
| dispatching directly to state 13 (`sm_dispatch_event(13,…)`) bypasses the ENTER → hook skipped | **Proven (mechanism)** | `0x080f2c78.c` (the status-4 path is the only route to the entry hook) |
| first-entry prompt failure doesn't undo construction | Proven (flag) / **Inferred** (prompt error path) | flag persistence above; voice error path not byte-traced |
