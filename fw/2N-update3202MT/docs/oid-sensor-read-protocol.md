# OID sensor read protocol — the two-wire link, the frame, and the raw↔OID boundary

How the 2N MT firmware physically reads the OID sensor and derives the internal OID number,
traced instruction-by-instruction from the two-wire GPIO boundary up to the GME script code the
games consume. Sources: the PROG decompilation (base 0x08009000), the `nandboot` blob
(loads flat at **0x08000000**; the reset vector/HAL live there), the 2N mask ROM (captured from
hardware, not included; checked to contain **no** OID/GPIO-OID references — the OID path begins in
nandboot), and the Sonix **SN9P701F v1.02** datasheet (a public vendor document, not redistributed
here).

Status tags: **[Proven]** = read from disasm/decomp; **[Inferred]** = deduced, not byte-traced.

---

## 0. Executive summary

- The sensor is a **Sonix OID decoder ASIC** (hardware fork: SN9P601FG-301 "OID 1.5" + SNM9S102
  optical module; the datasheet in refs is the sibling SN9P701F "OID 2"). The ASIC contains a
  16-bit DSP that does the **entire image-capture → dot-pattern → index decode** and outputs the
  finished index over a **proprietary bidirectional two-wire serial interface**. [Proven for the
  interface; part number from hardware side]
- Firmware side, the link is a pure **GPIO bit-bang**: **GPIO2 = clock (host out), GPIO9 = data
  (bidirectional, also the "code pending" attention line)**. There is no OID MMIO data register,
  no sensor IRQ, no DMA. [Proven]
- The sensor frame is (up to) **32 bits, MSB first**: a 23-bit code word
  `[type:2][res:3][index:18]` followed by a 9-bit trailer `[valid:1][check:8]`. The standby/game
  capture clocks only the first **23 bits**; the state-machine polls clock all **32**. [Proven]
- **The firmware performs no raw→OID translation whatsoever** — only framing checks (type bits,
  valid bit, check byte) and masking (`&0x3FFFF`, final store `&0xFFFF`). The value in the frame's
  index field **is** the GME script code the games consume. tttool's `KnownCodes.hs` raw↔code
  scramble therefore lives **inside the Sonix ASIC / printed-pattern domain**, not in firmware.
  **The working assumption is VERIFIED.** [Proven]
- Consequence: the decoded OID number *is* the frame's index field — exactly what the real ASIC
  emits over the two wires. The raw↔OID scramble lives entirely in the sensor/print domain (§6).

---

## 1. Hardware wiring (firmware view)

| element | value | evidence |
|---|---|---|
| clock line | **GPIO 2**, host output | `hal_gpio_write(2,·)` throughout the shifters |
| data line | **GPIO 9**, bidirectional | input for frames/attention, output for commands & ACK |
| GPIO direction reg | `0x0400007C` (bit=1 **input**, bit=0 **output**) | `0x0800774c(pin,out)` → op0, `r2=1-out`, bic/orr |
| GPIO output reg | `0x04000080` | op4 via dispatcher `0x080076d8`, reg table @`0x08007944` |
| GPIO input reg | `0x040000BC` | op3 |
| sensor power | `0x04000058` bits[25:24] := 01 | `oid_sensor_power_on` 0x080ef114 |
| sensor part | Sonix OID decoder (2-wire "index interface": ADO_SCK/ADO_SDIO in the datasheet) | SN9P701F datasheet pp.2,5,9 |
| I²C dev `0x94` | **dormant** on this pen | see below |

**The I²C-0x94 path is dead code here [Proven]:** `akoid_sensor_config` (0x080ef180) and
`FUN_080ef028` only program the I²C device when the **sensor-type byte @0x08008c18** (=capture
state +0x10) is 1 or 3. `oid_sensor_power_on` sets it to **0** on this pen, so no I²C config ever
runs. Types 1/3 are alternate (Anyka reference-design camera-style) sensors; the tiptoi uses the
Sonix two-wire part, which needs no register programming from the host.

`oid_sensor_init` (0x0803ad78) = register tick hook `0x0811c74c`, clear `0x08008c90`, pulse helper
`0x080077d0(5,·)`, power the sensor on, and (conditionally) trim clock regs `0x04000000/04/0c`.

---

## 2. The shared capture state @0x08008c08 [Proven]

Used by both nandboot HAL and PROG (same RAM):

```c
struct OidCaptureState {      // @ 0x08008c08
  u8  frame_ready;   // +0    set by hal_oid_shift_in after a full frame
  u8  bit_count;     // +1    bits to clock: 23 (standby decode) or 0x20=32 (PROG polls)
  u8  decode_valid;  // +2    set by nandboot validate: akoid_buf+8 holds 0x400000|index
  u8  unk3,unk4,unk5,unk6;    // cleared by akoid_init
  u8  done_latch;    // +7    1 = sensor sent to sleep; all polls skipped until akoid_rearm()
  u32 timer_handle;  // +8    standby poll soft-timer id (0xff = none)
  u32 raw_word;      // +0xC  last shifted frame, MSB-first (the "0x08008C14 raw word")
  u8  sensor_type;   // +0x10 0 = Sonix two-wire (this pen); 1/3 = alt I²C sensors (dormant)
};
```

`akoid_buf` (= `*(game_ctx+0x20)`, 0xdf0 bytes, alloc'd by `akoid_init` 0x080eeb20):
`+0` pen-down/event-pending flag · `+4` **current OID (≤0xFFFF, the value all `0x1060` handlers
read)** · `+8` standby raw word (`0x400000|index`) · `+0x14` standby-capture enable (init = 1)
· `+0x18/+0x1a` u16 first/last OID of the mounted GME · `+0x58` arm byte (100 = re-materialize
tap) · `+0x74` OID hand-off latch · `+0x4ac` last consumed OID · `+0xdd0/+0xdd2` deferred-jump
flag/OID.

Debug scratch @`0x081da084`: check-byte nibbles + result, written by the PROG polls.

---

## 3. The wire protocol [Proven from disasm]

All delays via `hal_delay_short(n)` (0x08002fa4): busy loop of `3·(n·(f_cpu>>20))/16`
iterations — ≈0.5–1 µs per unit at 96–166 MHz core [Inferred scale]. `0x08007938(n)` =
`hal_delay_short(n*1000)`.

### 3.1 Bus idle — `hal_oid_bus_idle` (nandboot 0x08005560)
GPIO9 → input (+write 1 = pull/latch high), GPIO2 → output, **clock low**. This is the rest
state; the sensor signals **"code pending" by pulling GPIO9 low**.

### 3.2 Host→sensor command — `akoid_shift_out(u8 cmd)` (PROG 0x080eed50)
```
dir9=out; clk=1; data=1; delay(10)
clk=0; delay(10)                      // start: data high, clock falls
for bit in cmd[7..0]:                 // MSB first
    clk=1; delay(4)
    data=bit                          // data set while clock high
    clk=0; delay(20)                  // sensor samples on the falling edge
hal_oid_bus_idle(); delay(0x40)
```
Command bytes used by this firmware:
| byte(s) | context | meaning [Inferred] |
|---|---|---|
| `0x56` | `akoid_rearm` (0x080eef68) + clear done-latch; on entering active states, USB, reset | wake / (re)enable reporting |
| `0xA0, 0xAC, 0xA6` (100-unit gaps) | `akoid_sensor_sleep` (0x080eedfc), sent when a **status frame** arrives; sets done-latch | sensor power-down / sleep handshake |
| `0xA6` alone | `cover_oid_classifier` system-code (0xFF00..0xFFFF) tap-counting loop | per-tap acknowledge/rearm |

### 3.3 Sensor→host frame — `hal_oid_shift_in` (nandboot 0x080055a4)
Runs with IRQs off (`0x0800339c`/`0x080033e4`). Bit count = `OidCaptureState.bit_count`.
```
frame_ready=0
if (gpio9 != 0) abort                  // attention must be LOW (sensor has a code)
clk=1; delay(15)
dir9=in; write9=1                      // release the data line
wait up to 100×delay(8) for gpio9==1   // sensor ACKs by releasing data HIGH
if still low → abort
dir9=out; write9=0; delay(10)          // host ACK: pull data low…
clk=0; delay(10); clk=1; delay(10)     // …and pulse the clock low→high
dir9=in; write9=1                      // release data again
raw=0
repeat bit_count times:                // 23 or 32
    clk=1; delay(10)
    raw <<= 1
    clk=0; delay(10)
    if (gpio9) raw |= 1                // sampled in the clock-LOW phase, MSB first
frame_ready=1; delay(0x40)
hal_oid_bus_idle()
```
Bit half-period = 10 units ≈ 5–10 µs → serial clock roughly 50–100 kHz; a 32-bit frame ≈
0.7–1.5 ms including handshake [Inferred scale].

### 3.4 Capture-trigger pulse (PROG polls only)
`akoid_poll_trigger` (0x080eef10): if done-latch clear → **GPIO2 high, `0x08007938(0x96)` ≈
75–150 ms, GPIO2 low**, IRQs off, run the 32-bit poll loop, IRQs on. This long clock-high pulse
precedes only the state-machine polls, not the timer path; the sensor needs no visible response
to it (likely its scan/holdoff window) [Inferred].

---

## 4. The frame format [Proven]

One format explains every check in both decoders. Full frame, MSB first:

```
bit 31..30  frame type      10 = decoded OID (data)   11 = status/special
bit 29..27  reserved        (firmware masks them out: bic 0x03800000 on the >>9'd word)
bit 26..9   index, 18 bit   the OID code (0..262143; in practice ≤0xFFFF is used)
bit 8       valid flag      must be 1 (PROG checks)
bit 7..0    check byte b    constraint: (b&0xF) + (b>>4) + 1 ≡ 0 (mod 16)
                            i.e. low nibble + high nibble = 15; NOT derived from the index
```

- The **23-bit captures** read only bits 31..9 (the code word `0x400000|index` for data frames).
- The **32-bit captures** read everything; firmware recovers the code word as `raw >> 9`.
- The check byte is self-contained (nibble + complement): the firmware never mixes the index into
  it, so **any** byte with nibble-sum 15 (e.g. `0xF0`) validates. Whether the real ASIC computes
  it from the index is unobservable to firmware.

Special values:
| value | where | behaviour |
|---|---|---|
| code word `0x60FFF8` / `0x60FFF1` (type 11, payload 0xFFF8/0xFFF1) | PROG polls (`DAT_080ef024`, and `−7`) | firmware sends `0xA0,0xAC,0xA6` + sets done-latch (sensor → sleep). Meaning of the two payloads = **hardware open question** (candidates: low-battery report — the Sonix part autonomously reports battery via the two-wire link every 10 s — or off-page/idle status) |
| index `0x3FFFC` (type 10) | nandboot decode (`0x0043FFFC` compare @0x080057c4) | frame silently dropped (sensor "invalid/filler" code) [Inferred meaning] |
| index `0xFF00..0xFFFF` | nandboot cb + `cover_oid_classifier` | system-code family: posted with a different post-mode; classifier runs a tap counter with `0xA6` per tap (thresholds 0x36/0x3C taps → mode flags) — factory/system codes |

---

## 5. The two decode paths

### 5.1 Standby/game timer path — 23-bit, **feeds gameplay** [Proven]
```
hal_oid_timer_start (0x080058b0):  hal_timer_create(0, period=40, mode=1, cb=0x080057cc)
hal_oid_timer_cb    (0x080057cc):  if akoid_buf[0x14] && gpio9==0:
  hal_oid_capture_decode23 (0x08005764):
      bit_count=23; hal_oid_shift_in()
      X = raw23; require (X & 0x600000) == 0x400000        // type == 10
      Y = X & ~0xFC000000 & ~0x03BC0000                    // keep bit22 + index[17:0]
      if (Y == 0x0043FFFC) drop
      akoid_buf[+8] = X;  then hal_oid_validate (0x08005720) sets decode_valid
  if decode_valid:
      akoid_buf[0] = 1                                     // pen-down / event pending
      arg = akoid_buf[+8]                                  // 0x400000|index
      index = X & 0x3FFFF
      hal_event_post(0x1060, &arg, index<=0xFFFF ? 1 : 0)  // id literal @0x080058AC = 0x1060
      → AO ring 0x08008898 → event_pump_ring (0x0800b4a4) → sm_dispatch_to_hierarchy
```
The period is 40 soft-timer ticks (timer IRQ line 10); tick length not pinned here — plausibly
1 ms → 40 ms cadence, consistent with the Sonix 50–100 ms response time [Inferred]. The timer is
restarted from the standby/idle flow (`standby_handler` calls `0x080058b0` after each
`akoid_rearm`); the callback kills its own handle each firing (one-shot style re-arm).

### 5.2 State-machine polls — 32-bit, standby/splash status watch [Proven]
`akoid_sensor_poll` (0x080eee40) and single-shot `FUN_080eef84`, reached via
`akoid_poll_trigger`/`FUN_080eef58`/thunk 0x080eef54, called from **standby_entry (0x080511a0),
standby_handler (0x08051b0c), splash_entry (0x0804c1d4), fwupdate_verify_image (0x0804cecc)** —
i.e. *not* from gameplay states:
```
loop (attention-wait ≤ ~0xEA5F reads, ≤ 20 frames):
  if gpio9==0:
      akoid_buf[+4]=0; bit_count=0x20; hal_oid_shift_in()
      akoid_buf[+4] = raw32 (provisional)
      if frame_ready:
          b = raw32 & 0xFF
          if (raw32 & 0x100) && ((b + (b>>4) + 1) & 0xF) == 0:  // valid + check byte
              w23 = raw32 >> 9;  akoid_buf[+4] = w23
              if w23 == 0x60FFF8 || w23 == 0x60FFF1:            // status frame
                  akoid_sensor_sleep()   // 0xA0,0xAC,0xA6 + done_latch=1
                  return
```
This path **never posts events**; it exists to catch the sensor's status frames (and keeps `+4`
provisionally updated). Note it stores the *unmasked* code word (incl. type bits) at `+4`; the
game classifiers overwrite it per tap (§5.3), so no conflict arises.

### 5.3 Event → `akoid_buf+4` → consumers [Proven]
- `cover_oid_classifier` (0x08037cec), a statechart transition action, receives the posted arg
  (`*(uint*)*param_2` = `0x400000|index`), re-checks type 10, handles the 0xFF00..0xFFFF system
  family, else stores **`akoid_buf[+4] = raw & 0xFFFF`** — the proven masking site.
- ~40 state handlers consume `param_1==0x1060 && akoid_buf[0]!=0`, read `+4`, range-check against
  the GME bounds `+0x18/+0x1a` (u16, from `gme_parse_start_end_oid`), then act. Dozens compare
  `+4 == (gme_table_value & 0xFFFF)` directly (e.g. 0x080535d0, 0x08055520, …).
- `gme_oid_dispatch` (0x0803629c) passes `+4` **directly** to `gme_oid_to_playscript()` and
  compares it against literal product codes (0xC21/0xC22, 0x119A, 0xF3F, 0x713, 0xB7B, …).
- Hand-off: game handlers (e.g. 0x080549ec, 0x08056388) set `+0x58=100` and copy `+4 → +0x74`;
  `gme_oid_dispatch`/`FUN_080aaf70` then re-latch `+0x74 → +4` and re-materialize `0x1060`.
  `+0x74` is an intra-statechart relay, still sourced from the sensor value.

**Pen-lift heuristic:** active handlers read `gpio9 == 1` (attention idle = no pending code) as
"pen not on paper" (e.g. `gme_oid_dispatch` tail). The attention line therefore doubles as a
touch indicator: it is deasserted whenever no tap is pending.

---

## 6. Verdict: where the raw→OID boundary sits

**Entirely in the sensor ASIC.** Evidence chain [Proven]:
1. The only "decode" firmware performs is framing: type-bit check, valid bit, self-contained
   check byte, bit extraction (`>>9`, `&0x3FFFF`, `&0xFFFF`). No lookup table, no descrambler, no
   arithmetic on the index anywhere between `hal_oid_shift_in` and `gme_oid_to_playscript`.
2. The index is compared *verbatim* against GME script codes and header OID bounds.
3. tttool's `KnownCodes.hs` is a bare empirical table (no formula) mapping GME codes to the "raw
   codes" tttool embeds in the printed dot geometry. Since firmware adds no mapping, that
   scramble is realized inside the Sonix DSP (its pattern-number → output-index function).
   Equivalently: **the number on the two-wire link IS the internal OID / GME code** (16 bits used;
   18 carried).

Consequence for tap injection: supplying the decoded OID number is not a shortcut
past hardware behaviour — it is exactly the ASIC's output. Authenticity is a question of the
*transport*, not the value.

---

## 7. The sensor's observable contract (at the two GPIO wires)

Everything above reduces to a small, edge-driven contract the sensor presents at input-register
bit 9 and output-register bit 2 / direction bit 9 — with `hal_oid_shift_in` and both decoders
running unmodified:

1. **Idle:** GPIO9 input bit = 1 (attention deasserted) whenever no tap is pending; active
   handlers read this as pen-up.
2. **A queued tap** is the 32-bit frame
   `F = ((0x400000 | (oid & 0x3FFFF)) << 9) | 0x100 | 0xF0`
   (`0xF0` = any check byte with nibble-sum 15). Attention is asserted by pulling GPIO9 = 0.
3. **Handshake** (per §3.3, driven by the firmware): when the firmware raises GPIO2 and switches
   GPIO9 to input, the sensor releases GPIO9 to 1 (the "ready" ACK), then tolerates the host-ACK
   (data driven low, clock low→high pulse).
4. **Bit serving:** after the k-th falling edge of GPIO2, the sensor presents frame bit `31−k` on
   GPIO9 (MSB first). If the firmware stops after 23 clocks (timer path) it has read
   `0x400000|oid`; if it clocks 32 (state polls) the trailer validates — the **same frame serves
   both decoders**, no mode tracking needed.
5. **After the frame:** attention is deasserted (GPIO9 = 1) until the next tap (re-asserted per
   tap, or per ~40 ms while the pen is held down, mimicking repeat reporting).
6. **Commands:** `akoid_cmd_write` writes are sampled on the GPIO2 falling edge while GPIO9 is an
   output: `0xA0,0xAC,0xA6` put the sensor to sleep (stop asserting attention); `0x56` wakes it.
   The ~100 ms GPIO2-high trigger pulse needs no response.
7. **Status frames:** a sleeping/battery-low sensor sends `(0x60FFF8<<9)|0x100|csum`.

The firmware busy-waits with fixed delays and never checks pulse widths, so the protocol is purely
edge-driven — there are no real-time constraints in the read path. [Proven]. This contract is what
[tt-emu](https://github.com/nomeata/tt-emu) presents to run the unmodified read path.

---

## 8. Proposed names / signatures / docstrings (names.csv harvest)

nandboot HAL (addresses are runtime = file offset + 0x08000000):
| addr | name | signature | docstring |
|---|---|---|---|
| 0x08005560 | `hal_oid_bus_idle` | `void(void)` | Release the OID two-wire bus: GPIO9→input (pulled high), GPIO2→output low. Sensor pulls GPIO9 low to signal a pending code. |
| 0x080055a4 | `hal_oid_shift_in` | `void(void)` | IRQs-off capture: attention check (GPIO9 low), ready-ACK wait, host-ACK pulse, then clock `state.bit_count` bits (23 or 32) MSB-first from GPIO9 into `state.raw_word` (@0x08008C14); sets `frame_ready`. |
| 0x08005720 | `hal_oid_validate_word` | `void(void)` | Validate `akoid_buf+8`: type bits must be "10"; normalize to `0x400000\|index`; set `state.decode_valid`. |
| 0x08005764 | `hal_oid_capture_decode23` | `void(void)` | Standby/game capture: 23-bit shift-in, require type "10", drop index 0x3FFFC, store code word → `akoid_buf+8`, validate. (was: `hal_oid_decode`) |
| 0x080057cc | `hal_oid_timer_cb` | `void(void)` | Periodic OID poll: capture+decode; on success set `akoid_buf[0]=1` (pen-down) and post event **0x1060** with `&(0x400000\|index)` into the AO ring. |
| 0x080058b0 | `hal_oid_timer_start` | `void(void)` | (Re)arm the OID poll soft-timer: `hal_timer_create(0, 40, 1, hal_oid_timer_cb)`; handle → `state.timer_handle`. |
| 0x0800780c | `hal_timer_create` | `int(u8 type, u32 period, u32 mode, void* cb)` | Soft-timer allocator (6 slots); returns handle or 0xFF. |
| 0x08002fa4 | `hal_delay_short` | `void(u32 n)` | CPU-clock-scaled busy wait, ≈0.5–1 µs per unit. |
| 0x08007938 | `hal_delay_1k` | `void(u32 n)` | `hal_delay_short(n*1000)`. |
| 0x080076d8 | `hal_gpio_op` | `int(u32 pin, u32 op, u32 val)` | GPIO reg dispatcher; table @0x08007944: [0]=dir 0x0400007C, [3]=in 0x040000BC, [4]=out 0x04000080 (+0x18 for pins≥32). |
| 0x08007734 / 0x08007740 / 0x0800774c | `hal_gpio_write` / `hal_gpio_read` / `hal_gpio_set_dir` | `(pin,val)` / `(pin)` / `(pin,out)` | dir reg bit: 1=input, 0=output. |

PROG:
| addr | name | signature | docstring |
|---|---|---|---|
| 0x080eed50 | `akoid_cmd_write` | `void(u8 cmd)` | Bit-bang an 8-bit command to the sensor, MSB first, data valid on GPIO2 falling edge; ends with bus idle. (was: `akoid_shift_out`) |
| 0x080eedfc | `akoid_sensor_sleep` | `void(void)` | Send `0xA0,0xAC,0xA6` (100-unit gaps) and set the done-latch — sensor power-down handshake. |
| 0x080eee40 | `akoid_poll_status32` | `void(void)` | 32-bit poll loop (≤20 frames): verify valid bit + check byte, store `raw>>9` → `akoid_buf+4`; on status word 0x60FFF8/0x60FFF1 → `akoid_sensor_sleep`. Standby/splash only. (was: `akoid_sensor_poll`) |
| 0x080eef84 | `akoid_poll_status32_once` | `void(void)` | Single-frame variant of the above. |
| 0x080eef10 | `akoid_poll_trigger` | `void(void)` | If done-latch clear: ~100 ms GPIO2-high trigger pulse, then IRQs-off `akoid_poll_status32`. |
| 0x080eef54 | `akoid_poll_trigger_thunk` | `void(void)` | Thunk of 0x080eef10. |
| 0x080eef58 | `akoid_poll_from_idle` | `void(void)` | `hal_oid_bus_idle()` then `akoid_poll_trigger()`. |
| 0x080eef68 | `akoid_rearm` | `void(void)` | Wake the sensor: `akoid_cmd_write(0x56)`; clear done-latch. |
| 0x080eeb20 | `akoid_init` | `char*(void)` | Alloc + zero the 0xdf0 `akoid_buf` at `game_ctx+0x20`; init capture state; bus idle. |
| 0x080ef114 | `oid_sensor_power_on` | `void(void)` | `0x04000058` bits[25:24]:=01; sensor_type:=0 (Sonix two-wire). (was: `oid_sensor_enable`) |
| 0x080ef180 | `akoid_sensor_config_i2c` | `void(void)` | I²C(dev 0x94) register setup for alternate sensor types 1/3 — **dormant** on this pen (type==0). |
| 0x080ef684 / 0x080ef6a4 | `akoid_reg_write` / `akoid_reg_read` | `(u8 reg, u8 val)` / `u8(u8 reg)` | I²C dev 0x94 accessors (dormant). |
| 0x08037cec | `cover_oid_classifier` | (keep) | add: *also the proven arg→`akoid_buf+4` masking site: stores `raw & 0xFFFF` after type/system-range checks.* |
| **0x080ee6e4** | `file_checksum_verify` | `int(int,int)` | **MISNAMED** as `akoid_decode_frame` in names.csv — it reads sectors by filename and verifies a trailing 32-bit byte-sum; nothing OID. Rename. |

Struct: adopt `OidCaptureState` (§2) at 0x08008c08 in `ghidra_types.h`.

---

## 9. Open questions (hardware side)

1. Semantics of command bytes `0x56`, `0xA0/0xAC/0xA6`, lone `0xA6` — need the Sonix
   *two_wire_interface_v2* spec (referenced by the datasheet, not in refs) or a logic-analyzer
   capture from a real pen.
2. Meaning of status payloads `0xFFF8` vs `0xFFF1` (type-11 frames) — battery-low vs sleep-ack?
3. Exact serial clock rate / delay-unit calibration (needs the CPU clock value fed to
   `hal_delay_short`'s `freq>>20` table @0x08007798).
4. Soft-timer tick length (poll cadence: 40 ticks = ? ms).
5. Part-number reconciliation: the SN9P701F datasheet (OID 2, 65536+ indexes) vs the hardware
   fork's SN9P601FG-301 (OID 1.5). The wire protocol per this firmware is identical in shape;
   the 18-bit index field fits either.
