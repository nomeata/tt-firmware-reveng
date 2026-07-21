# GME timer / counter / random commands — firmware handlers

Static RE of the 2N-MT (`fw/2N-update3202MT`, PROG base 0x08009000) GME-script interpreter,
focused on the **timer**, **counter (register-arithmetic)** and **random** commands.
Cross-checked against tttool (`tip-toi-reveng/src/{GMEParser,Types,Constants}.hs`, `GME-Format.md`).
Evidence tags: **[Proven]** = read directly from decomp/disasm/literals; **[Inferred]** = strongly
suggested but not fully chased.

Bottom line first: **all three command families are pure software**. There is **no hardware RNG
and no dedicated hardware timer** behind any GME command. Everything reduces to three things: the
system tick `*0x08008d24` (incremented once per timer IRQ by the resident-nandboot `timer_dispatch`),
the 6-slot software-timer table @0x0800895c, and plain memory (the GME register file @0x081da350 +
two free-running counters).

---

## 1. The interpreter

One interpreter in MT (the old build's second copy is not duplicated here: the debug strings
`"Rand(%d) = %d.\r\n"` @0x08035310 and `"Exit A Game"` occur exactly once in PROG.bin) [Proven].

- **`gme_exec_command` @0x08034da0** — the single opcode dispatcher.
  Signature: `gme_exec_command(u16 reg_idx, u16 opcode, u8 operand_is_const, u16 operand)`.
  Operands come from the per-line action tables filled by **`gme_parse_actions` @0x080354c8**
  (per action, read from the .gme: reg u16 → tbl@0x081da0ea, opcode u16 → 0x081da0fa,
  type u8 → 0x081da10a, value u16 → 0x081da112; max 8 actions/line, count @0x081da0a8) [Proven].
  Executed from **`gme_oid_dispatch` @0x0803629c** (and from many `game_*` handlers that
  synthesize commands).
- **Register file**: u16 array @**0x081da350** (`DAT_0803530c`); count + initial values loaded
  from the .gme header by `gme_reset_registers` @0x08035ca8 [Proven].
- `operand_is_const==1` → `operand` is a literal; else `operand` is a **register index** and the
  handler reads `*(u16*)(0x081da350 + operand*2)` — resolved *inside* each opcode case, **not**
  centrally (this matters for the quirk in §3/§4) [Proven].

Opcode numbering convention: 16-bit LE value of the two bytecode bytes (tttool `B.pack [lo,hi]`
= `0xHHLL`), matching the firmware's `switch(opcode)`.

### Command set (as dispatched by gme_exec_command) [Proven]

| opcode | tttool name | firmware behaviour |
|---|---|---|
| 0xFFF0 | Inc | `reg += v` |
| 0xFFF1 | Dec | `reg -= v` |
| 0xFFF2 | Mult | `reg *= v` |
| 0xFFF3 | Div | `reg = reg / v` (quotient, via nandboot `__rt_udiv`) |
| 0xFFF4 | Mod | `reg = reg % v` (remainder of same divide) |
| 0xFFF5 | And | `reg &= v` |
| 0xFFF6 | Or | `reg \|= v` |
| 0xFFF7 | XOr | `reg ^= v` |
| 0xFFF8 | Neg | logical-not: `reg = (reg <= 1) ? 1-reg : 0` |
| 0xFFF9 | Set | `reg = v` |
| 0xFF00 | "Timer" `T($r,m)` | **random-to-register**: `reg = tick % (m+1)`; prints `"Rand(%d) = %d."` |
| 0xFE00 | Timer (real) | arm GME software timer, delay `m*100` ms-units, **auto-reload** |
| 0xFEFF | — (not in tttool) | cancel the GME timer |
| 0xFC00 | Random a b | play random playlist entry in `[b..a]`: idx = `heartbeat_ctr % (a-b+1) + b` |
| 0xFFE0 | RandomVariant `P*` | **not random**: plays `playlist[dispatch_ctr % playlist_len]` (round-robin; per-dispatch counter gamectx+0x125, pick stored gamectx+0x141) |
| 0xFFE1 | PlayAllVariant | plays entry 0, sets play-all mode (gamectx+0x12c=1, ++0x12d) |
| 0xFFE8 | Play n | play `playlist[n]` |
| 0xFB00 | PlayAll | play from `hi`, mode 2 |
| 0xFAFF | Cancel | `"Exit A Game"`, posts event |
| 0xF8FF | Jump | deferred: gamectx+0xdd0=1, target → gamectx+0xdd2 (taken when audio stops) |
| 0xFD00 | Game | game select (reads .gme, posts event in state 0x0b) |
| 0xFEE0–0xFEE7 | — | `sm_set_state(0..7)` — direct statechart state jumps (0xFEE8 = nop) |
| 0xFFA1 | — | **coin-flip play** (resolved 2026-07-20, gme-format-completeness §1): iff `evt_dispatch_count` (gamectx+0x125) even → play `playlist[m]` immediately (FFE8 tail) + latch +0xde0=m&0xff/+0xddf=1 for the replay walks; odd → no-op |

Conditionals (in **`gme_check_condition` @0x08035624**, called per line by
`gme_parse_check_conditions` @0x08035888): 0xFFF9 Eq, 0xFFFA Gt, 0xFFFB Lt, 0xFFFD GEq,
0xFFFE LEq, 0xFFFF NEq — pure u16 compares against the register file [Proven].
Signature: `gme_check_condition(u8 lhs_is_const, u16 lhs, u16 op, u8 rhs_is_const, u16 rhs)`.

---

## 2. Counter (register arithmetic + conditionals) — pure software [Proven]

All of 0xFFF0–0xFFF9 and Neg operate on the u16 register file @0x081da350 in `gme_exec_command`
itself. The only external code is Div/Mod's call to `func_0x080031d0` = resident-nandboot
**`__rt_udiv`** (nb 0x07ffb1d0 → rt 0x080031d0; ADS convention: r0=divisor, r1=dividend, returns
quotient r0 / remainder r1) — ordinary code.

Only edge: Div/Mod with divisor 0 enters the ADS div-0 handler — a content bug, not a HW gap.

---

## 3. Randomness — no HW RNG anywhere [Proven]

Three distinct "random" sources, all software:

### 3a. Opcode 0xFF00 `T($r,m)` → system tick modulo
`gme_exec_command` case 0xFF00:
`reg = FUN_08030c88() % (m+1)` where **`FUN_08030c88` @0x08030c88 returns `*(0x08008d20 + 4)` =
the system tick `*0x08008d24`** [Proven — literal DAT_08030c94=0x08008d20]. The tick is
incremented once per timer IRQ by the resident-nandboot `timer_dispatch` (nb 0x07ffdc8c →
rt 0x08005c8c: `piRam07ffdd50[1]++` with pool word = 0x08008d20) [Proven].
Debug print `"Rand(%d) = %d.\r\n"` — the firmware itself calls this command *Rand*; tttool's
`Timer` name for 0xFF00 is historical (GME-Format.md's description "writes an internal counter
to $r, range 0..m" is exactly right).
- Quirk: `m` is used **raw** — the `operand_is_const` flag is not checked, so a register-typed
  operand would use the register *index*, not its value [Proven].
- `FUN_08030c88` is the firmware-wide "get tick" API (~90 callers incl. all game handlers, and
  it is placed as the first field of the system_api struct handed to embedded GME binaries in
  `gme_launch_binary_build_sysapi` @0x080aa934 / `FUN_0805c0f0`).

### 3b. Opcode 0xFC00 `Random a b` → heartbeat counter modulo
`gme_exec_command` case 0xFC00 calls **`gme_rand_in_range` @0x08034d74**
(`(FUN_0804fda8() % (hi-lo+1)) + lo`, result &0xff; pick stored at gamectx+0x142; guarded by
`a < playlist_len` @0x081da122). **`FUN_0804fda8` @0x0804fda8 returns `*(0x081da010 + 4)` =
a free-running heartbeat counter @0x081da014** [Proven — literal DAT_0804fde4=0x081da010].

Who advances it: **`FUN_0800acfc` @0x0800acfc** does `*(0x081da010+4) += 1` on every invocation
(plus audio-amp keepalive). It is **transition-action[0] @0x0800b544** of the statechart's
state-9 pre-handler, bound to **event 0x1046** [Proven — docs/product-init-and-runtime-tables.md
§1, state8-to-13-transition.md]. Event 0x1046 is posted by the resident-nandboot software-timer
callback `oid_change_notifier` (nb 0x07ffb994) every firing of the *current* poll slot: the
event-id mapper nb 0x07ffb930 maps arg 0 → `0x1000+0x46` [Proven — disasm]. I.e. **the counter
increments once per OID-poll timer period** (reload 100 ms-units → every ~5 timer IRQs).

### 3c. Games: a real LCG (`rng_below`)
**`rng_below` @0x080ac5f8** (already named; ~10 game-handler callers, *not* used by the GME
interpreter): lazily seeds once (`FUN_080ac5d8` @0x080ac5d8: `state@0x081db900 = tick ×
heartbeat_ctr`; seed-once flag @0x081226f4, static init = 1), then calls nb **0x07ff81fc**
(rt 0x080001fc), a classic ARM-ADS **LCG**: `state = state*0x91E6D6A5 + 0x91E6D6A5;
return (state*n) >> 32` — i.e. uniform `[0,n)` [Proven — disasm + literal].
Also `game_random_id` @0x0806f058: picks a random *available* entry via `tick % count` then a
.gme table lookup — same tick source.

No MMIO/HW RNG is read by any path. Entropy = IRQ count (tick) and poll-tick count (heartbeat),
both advanced by the timer IRQ. Note that until the OID poll starts running, the heartbeat counter
@0x081da014 stays 0, so 0xFC00 deterministically picks `b`.

---

## 4. Timer (opcode 0xFE00 / cancel 0xFEFF) — software-timer slot, no HW gap [Proven]

Arm (case 0xFE00 in `gme_exec_command`):
1. If the GME timer handle `*0x08121ecc` (static init 0xff = none; literal DAT_08035304) is
   live, cancel it first via `func_0x08005be8` = nb **0x07ffdbe8 = `hal_timer_unregister(slot)`**
   (clears the slot, returns 0xff) [Proven — disasm].
2. `handle = hal_timer_register(0, m*100 /*& 0xff80ffff*/, 1, 0x08003994)` — resident-nandboot
   nb 0x07fff80c: allocates one of the **6 software-timer slots @0x0800895c** (stride 0xc),
   reload u16 = `period / 20` (via `__rt_udiv(0x14, period)`), stores the callback at slot+8.
   `param_3=1` sets **bit7 = auto-reload → the GME timer is PERIODIC**, re-firing every
   `m*100` ms-units (~`m` × 0.1 s if the timer IRQ is the 20 ms tick [Inferred]) until 0xFEFF /
   re-arm / slot exhaustion (register returns 0xff on a full table — silently no timer).
   Quirks: the register field `r` of `Timer r v` is ignored, and `m` is used raw (same
   const-flag quirk as 0xFF00) [Proven].

Expiry: timer IRQ → `timer_dispatch` (nb 0x07ffdc8c) increments the tick, walks the slots, and
calls `callback(slot_idx, reload)`. The callback **0x08003994** (nb 0x07ffb994,
`oid_change_notifier` — the name reflects its *other* job as the OID-poll callback) posts
**event `0x30 {slot_idx}`** into the AO input ring for any slot that is not the current poll
slot (via `hal_event_dedup`) [Proven].

Consumption: **`gme_oid_dispatch` @0x0803629c** — on event 0x30 it compares `args[0]` with the
GME handle `*0x08121ecc`; on match (and engine idle: gamectx flags +0x12a/+0x12c/+0x128 clear)
it runs `gme_clear_script_state` → **`gme_parse_additional_script` @0x08036228** → evaluates
each line of the **additional script table (GME header offset 0x000C — "purpose unknown" in
GME-Format.md)** with `gme_parse_check_conditions`, and executes the **first line whose
conditions hold** [Proven]. So: *the 0xFE00 timer periodically re-enters the engine through the
GME's additional-script table* — that is the delayed/countdown-action mechanism (registers count
the periods; conditions on them select the action).

(The state-9 pre-handler also routes 0x30 → `FUN_0804fdbc` → `charger_out_handler` @0x0803cff8,
but that one self-filters on *its own* handle @0x080079cc — unrelated to the GME timer [Proven].)

The handle `*0x08121ecc` is also placed in the system_api struct for embedded GME binaries
(`FUN_0805c0f0`, `gme_launch_binary_build_sysapi` @0x080aa934) [Proven].

End to end, the delivery path is: timer IRQ → nandboot `timer_dispatch` → slot reload/callback →
event 0x30 in the AO input ring → dispatch → `gme_oid_dispatch`. The `.gme` must actually carry an
additional-script table for the expiry to do anything.

---

## 5. Assessment summary (the deliverable)

| feature | mechanism | HW dependency |
|---|---|---|
| **Counter** (0xFFF0–0xFFF9, Neg, conditionals) | u16 register file @0x081da350, in-line C + nandboot `__rt_udiv` | none |
| **Random** 0xFF00 `T($r,m)` | `tick@0x08008d24 % (m+1)` | none (tick = timer-IRQ count) |
| **Random** 0xFC00 `P(b-a)` | `heartbeat_ctr@0x081da014 % range + b` | none (ctr = 0x1046 events from the OID-poll sw-timer) |
| **"Random"** 0xFFE0 `P*` | `evt_dispatch_count % len` (gamectx+0x125: ++ per event delivered to book(13) — heartbeats/taps/timers — NOT per tap, so only round-robin when nothing else fires) | none |
| **Random** 0xFFA1 `P½(m)` | coin-flip play: `evt_dispatch_count & 1` == 0 → play `playlist[m]` + latch for replay (gme-format-completeness §1) | none |
| **Game RNG** `rng_below` | LCG ×0x91E6D6A5 in nandboot, seeded `tick×ctr` once | none |
| **Timer** 0xFE00/0xFEFF | 6-slot sw-timer @0x0800895c, periodic, expiry → event 0x30 → additional-script evaluation | none beyond the timer IRQ |

**No GME command touches dedicated timing/entropy hardware.** No MMIO register is read for entropy;
no dedicated HW timer is programmed by any GME command (the only MMIO touched is the interrupt
controller enable/ack 0x04000018 inside the nandboot timer HAL).

---

## 6. Proposed names / signatures / docstrings (for input/names.csv later)

New names:

```
0x08030c88,sys_get_tick
    u32 sys_get_tick(void) — returns the system tick *(0x08008d20+4)=*0x08008d24
    (incremented once per timer IRQ by nandboot timer_dispatch). Firmware-wide time API
    (~90 callers); entropy source for GME opcode 0xFF00 (reg = tick % (m+1)); field 0 of the
    embedded-binary system_api struct.
0x0804fda8,sys_get_heartbeat_count
    u32 sys_get_heartbeat_count(void) — returns *(0x081da010+4)=*0x081da014, the free-running
    heartbeat counter (one ++ per event 0x1046 = per OID-poll timer period, in
    heartbeat_1046_handler). Entropy source for gme_rand_in_range (opcode 0xFC00).
0x0800acfc,heartbeat_1046_handler
    int heartbeat_1046_handler(void) — statechart transition-action[0] (table @0x0800b544),
    bound to event 0x1046 by the state-9 pre-handler 0x0800a914. Increments the heartbeat
    counter *0x081da014, toggles the audio amp while playing (is_audio_playing), returns 1.
0x080ac5d8,rng_seed_once
    void rng_seed_once(void) — LCG seed: *0x081db900 = sys_get_tick() *
    sys_get_heartbeat_count(). Called lazily from rng_below when the seed-once flag
    *0x081226f4 (static init 1) is set; clears the flag.
0x0805c0f0,game_binary_launch
    void game_binary_launch(void) — builds a system_api struct on the stack (tick value first
    field, GME timer handle 0x08121ecc included), gme_alloc_binary_region + fs_read of the
    game binary, then jumps to it.
```

Nandboot (bootrom-side names; resident copies live at nb−0x07ff8000+0x08000000):

```
0x07ffdbe8,hal_timer_unregister
    u8 hal_timer_unregister(u8 slot) — frees sw-timer slot (table @ rt 0x0800895c), disables
    the timer IRQ source if no slot of that class remains, returns 0xff (callers store it back
    into their handle). rt 0x08005be8.
0x07ff81fc,lcg_rand_below
    u32 lcg_rand_below(u32 *state, u32 n) — ARM-ADS LCG: *state = *state*0x91E6D6A5
    + 0x91E6D6A5; returns (*state * n) >> 32, uniform in [0,n). rt 0x080001fc. Game-side RNG
    (rng_below wrapper @0x080ac5f8); NOT used by the GME interpreter.
0x07ffb1d0,rt_udiv
    (u32 q, u32 r) rt_udiv(u32 divisor, u32 dividend) — ADS __rt_udiv, quotient r0 /
    remainder r1. rt 0x080031d0. Used by Div/Mod (0xFFF3/0xFFF4), 0xFF00, gme_rand_in_range,
    hal_timer_register period scaling.
0x07ffb930,hal_event_id_for
    u16 hal_event_id_for(u8 kind) — 0→0x1046 (poll tick), 1→0x105f (OID partial),
    2→0x1060 (OID decoded). rt 0x08003930-family.
```

Docstring updates for already-named functions:

```
0x08034da0,gme_exec_command
    void gme_exec_command(u16 reg_idx, u16 opcode, u8 is_const, u16 operand) — GME action
    dispatcher (see §1 table). Registers @0x081da350. Timer: 0xFE00 arm periodic
    (hal_timer_register(0, m*100, autoreload=1, cb=0x08003994), handle @0x08121ecc),
    0xFEFF cancel. Random: 0xFF00 reg=tick%(m+1) ("Rand(%d)=%d"), 0xFC00 via
    gme_rand_in_range. 0xFEE0..7 = sm_set_state(0..7). Quirk: 0xFE00/0xFF00 use the raw
    operand (is_const not checked).
0x08034d74,gme_rand_in_range
    u8 gme_rand_in_range(u8 lo, u8 hi) — (heartbeat_ctr % (hi-lo+1)) + lo; playlist pick for
    opcode 0xFC00 (Random a b → [b..a]).
0x08035624,gme_check_condition
    int gme_check_condition(u8 lhs_is_const, u16 lhs, u16 op, u8 rhs_is_const, u16 rhs) —
    conditional ops 0xFFF9 Eq / 0xFFFA Gt / 0xFFFB Lt / 0xFFFD GEq / 0xFFFE LEq / 0xFFFF NEq
    over regfile 0x081da350; returns 0/1.
0x08036228,gme_parse_additional_script
    Loads the line-offset table of the ADDITIONAL SCRIPT (GME header +0x0C — the timer script):
    count → *0x081da0a0-area, offsets → tbl. Evaluated by gme_oid_dispatch on each GME-timer
    firing (event 0x30 with args[0]==*0x08121ecc): first line whose conditions hold executes.
0x0803629c,gme_oid_dispatch
    ... (existing docstring) + : consumes event 0x30 for the GME timer (handle @0x08121ecc):
    when idle, re-evaluates the additional-script table (delayed/countdown actions). Also
    executes deferred Jump (gamectx+0xdd0/+0xdd2) once audio stops.
0x07fff80c,hal_timer_register
    u8 hal_timer_register(u8 class, u32 period_ms_units, u8 autoreload, void (*cb)(u8 slot,
    u16 reload)) — allocate one of 6 sw-timer slots @rt 0x0800895c (stride 0xc: [0] state|
    bit7=autoreload, [1] class, [2] u16 reload = period/20, [4] u16 elapsed, [8] cb);
    enables the timer IRQ source; returns slot idx or 0xff.
0x07ffdc8c,timer_dispatch
    Timer-IRQ handler (resident rt 0x08005c8c): ++*0x08008d24 (system tick), ack IRQ
    (0x04000018), walk the 6 slots, on elapsed>=reload call cb(slot, reload); autoreload
    (bit7) resets elapsed, else slot → state 2.
0x07ffb994,oid_change_notifier
    Shared sw-timer slot callback (rt 0x08003994): if slot == current poll slot (*(0x080089a4
    +4)) posts hal_event_id_for(0)=0x1046 (heartbeat), else coalesced-posts 0x30{slot}.
    Registered both by the OID poll and by the GME Timer opcode 0xFE00.
```

Key globals:

```
0x08008d24  u32 system tick (timer-IRQ count)                      [nandboot RAM]
0x0800895c  6 × 0xc software-timer slots                           [nandboot RAM]
0x08121ecc  u8/u32 GME timer slot handle (0xff = none, static)     [.data]
0x081da014  u32 heartbeat counter (event-0x1046 count)             [high data/bss]
0x081da350  u16[] GME register file                                [bss]
0x081db900  u32 LCG state (rng_below)                              [bss]
0x081226f4  u8 rng seed-once flag (static 1)                       [.data]
0x081da0ea/0fa/10a/112  parsed action reg/opcode/flag/value tables (8 entries)
0x081da4e4/0x081da564  playlist media offset/size tables; count @0x081da122
```
