# Statechart framework (MT firmware) — the UI/game core

The pen's application logic is a **table-driven hierarchical state machine (QHsm-style)**. Real timer
IRQs drive the event pump, which dispatches through this framework to the real state handlers
(splash→standby→mount→book). All addresses are the unified runtime address (Ghidra base = runtime =
0x08009000). The concrete MT state map is in `statechart-full-map.md`.

## The statechart object / active object (AO)
Pointed to by `DAT_080f2e28`. Fields observed:
- `+0x00`  current-state byte (the active leaf state id)
- `+0x01`  a "handled/consumed" flag (cleared at the top of each hierarchy dispatch)
- `+0x14`  parent/super-state pointer (for the hierarchy walk)
- `+0x18`  **state-descriptor table** base: `desc = *(*(obj+0x18) + state*4)`; the handler fn ptr is `desc[+0xc]`
- `+0x20`  **transition-action table** base: `action = *(obj+0x20)[result*4]`

On the pen this AO = 0x08008874; the descriptor table = 0x08121d44 (runtime, in m08120000.bin).

## Dispatch (the two core functions)
- **`sm_dispatch_to_hierarchy` (0x080f2d70)** — the run loop. Per iteration: dequeue an event via
  the HAL (`func_0x07ffdbc8`), `sm_dispatch_event(current_state,…,0)`; if it propagates, dispatch to
  the parent (`*(obj+0x14)`) then grandparent (`func_0x07ffdbd0`); `func_0x07ffe620` advances. Loops
  until the event queue drains. → classic HSM current→parent→grandparent propagation.
- **`sm_dispatch_event` (0x080f2c78)** — one (state,event) dispatch:
  1. store the state id in `*obj`;
  2. look up the handler: `pcVar4 = *(*(*(obj+0x18) + state*4) + 0xc)`;
  3. call it: `handler(event, &status, event, handler, data)`;
  4. `status==2` → handled (return 1); `status==1` → run transition action
     `(*(obj+0x20)[result*4])(…)`; `status==0` → propagate (caller tries the parent).

## State handlers
A handler (e.g. `0x0804e678` — the root/state-0 mapper; per-state list in `statechart-full-map.md`)
takes `(event_id, out_status_ptr)`, and returns the next/super state, setting the status byte.
Framework signals seen: `0x1001`, `0x1002` (entry/exit-like). Real handlers chain into the app
services — `play_media`, `play_media_setup`, `fwl_voice_play_2`, `aud_player_*`,
`codepage_get_header/widechar` (media + voice + on-screen text).

## Table accessors
`sm_get_state_descriptor_table` (0x0804fd94), `sm_get_event_action_table` (0x0804fd84),
`sm_get_transition_action_table` (0x0804fd8c) — these return the runtime tables (populated by init).
Related: `sm_set_state` (0x0800b65c), `sm_state_entry` (0x080f2e0c), `sm_current_state_byte`
(0x080f2df0), the AO event ring (`sm_ao_ring_clear`/`sm_ao_event_in_ring`/`sm_ao_event_coalesce`
@0x0800b86c/b884/b8cc), `event_loop_dispatch` (0x080f2b38).

## Why the 0x07ffxxxx HAL matters
The framework's **event-queue primitives are in the 0x07ffxxxx HAL** (the nandboot/BIOS layer):
`func_0x07ffdbc8` (dequeue), `func_0x07ffdbd0`, `func_0x07ffe620`, `func_0x07ffe6ac`. So the HSM
runtime is split — dispatch/handlers in PROG, the event-queue/scheduler primitives in the HAL — which
is a large part of why MT calls 0x07ffxxxx so heavily.

## Status
Confirmed dynamically: the real pump self-sustains over the authentic NFTL-mounted NAND, running
`sm_dispatch_to_hierarchy` → `sm_dispatch_event` and walking 0→splash→standby and (on a tap)
→mount(12)→book(13) autonomously. See `statechart-full-map.md` and `state8-to-13-transition.md`.
