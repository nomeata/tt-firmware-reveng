# LED indicator — is the "blink on OID tap" in firmware? (2N MT)

Static decomp analysis (`out/decomp_named/`, base **0x08009000**) + `data/nandboot.bin` disasm
(loads flat @0x08000000). Evidence tags: **[Proven]** = read from disasm/decomp; **[Inferred]** =
deduced. Companions: `soc-core-registers.md` (GPIO map), `oid-sensor-read-protocol.md` (OID-detect
path), `pmu-power-management.md` (power/battery), `hardware-external-refs.md` (external LED/parts).

---

## 0. Headline answer

**The observed "LED briefly lights on an OID tap" is NOT driven by the SoC firmware.** [Proven]

- The OID-detect path performs **no GPIO write at all**. `hal_oid_timer_cb` (0x080057cc) only
  *reads* GPIO9, runs the decode, and posts event **0x1060** — there is no on/off pulse of any
  output pin at a successful decode. Nor does the classifier (`cover_oid_classifier` 0x08037cec)
  or any 0x1060 consumer toggle a GPIO for indication. [Proven]
- **There is no dedicated LED GPIO anywhere in the firmware.** Every output pin the firmware ever
  writes is already accounted for by a known role (OID clk, ZC90B/gauge, USB power, audio, power
  latch, amp). The unassigned output candidates (pins 3, 4, 14, 17+) are **never written**. [Proven]
- The only firmware function named for an LED — `fatal_low_battery_blink` (0x08038d24) — toggles
  the **dummy pin 0xFF** (the firmware's "no-pin" sentinel), i.e. it drives **nothing** on this
  board, then drops the power latch. It is Anyka reference-code vestige, not a working indicator on
  the tiptoi. [Proven]

**Conclusion:** the visible flash on tap is produced **outside the SoC** — by the Sonix OID decoder
module (its capture/illumination LED strobes when it images a dot pattern), independent of the
firmware. The SoC only clocks in the already-decoded index over GPIO2/GPIO9. [Inferred — see §4]

---

## 1. Q1 — Is there LED logic on the OID-detect path? No. [Proven]

`hal_oid_timer_cb` (nandboot 0x080057cc, the 40-tick periodic OID poll) disassembled in full:

```
push {...}
r0 = akoid_buf[+0x14]  (standby-capture enable); if 0 → exit
r0 = hal_gpio_read(9)        ; <-- the ONLY GPIO op: a READ of the attention line
if gpio9 != 0 → exit          ; no pending code
bl hal_oid_capture_decode23 (0x8005764)
if decode_valid == 0 → exit
akoid_buf[0] = 1              ; pen-down / event pending
arg = akoid_buf[+8]           ; 0x400000|index
bl hal_event_post(0x1060, &arg, ...)   ; (0x8003768) — posts the tap event
bl hal_oid_timer_start (0x80058b0)     ; re-arm
pop {...}
```

No `hal_gpio_write` (0x08007734), no `hal_gpio_set_dir`, no direct `0x04000080` write occurs between
frame decode and event post. The event is delivered to the statechart; the handlers that consume
`0x1060` (`cover_oid_classifier` and ~40 state handlers) only read/mask `akoid_buf+4` — none pulses
an output for indication. [Proven]

`event_pump_ring` (0x0800b4a4) does bracket **every** dispatched event with
`gpio_set_dir(5,1); gpio_write(5,1); … gpio_write(5,0)` — but pin 5 is the **ZC90B/gauge data
line**, and this happens for *all* events (not OID-specific), so it is bus idling, not an LED. [Proven]

So there is no "turn an output on at decode, off shortly after" pulse in the firmware.

---

## 2. Q2 — Which GPIO is the LED? None. [Proven]

Complete set of pins the firmware **ever writes** (via `hal_gpio_write` 0x08007734, across all of
`decomp_named/`):

| pin | role (from soc-core-registers.md / pmu doc) |
|---|---|
| 2 | OID sensor clock |
| 5 | ZC90B auth data / gauge data (also event_pump idle) |
| 6 | USB power-path switch |
| 7 | (set at boot in clock_periph_init; headphone-detect pin, cfg only) |
| 8 | (set-dir in battery check; USB/VBUS-detect input) |
| 9 | OID data (driven during handshake) |
| 10 | ZC90B auth clock / gauge clock |
| 0xB (11) | battery comparator (bidir cfg in clock_periph_init) |
| 0xC (12) | audio mute / amp keep-alive |
| 0xD (13) | codec/enable strobe |
| 0xF (15) | **power-hold latch** |
| 0x10 (16) | speaker amp enable |
| 0xFF | **dummy no-pin sentinel** (writes lost; reads ≡ 0) |

The **unassigned output candidates (pins 3, 4, 14, and 17+) are never written**
by any code path. Direct `_DAT_04000080` RMW writes exist only in the **dormant I²C block**
(`reg04000080_set_bit/clr_bit` 0x080f39ac/0x080f39c4, `i2c_gpio_init` 0x080f355c), whose channels
resolve through `gpio_pin_lookup` (0x0800af74) to **0xFF = no-pin** (dead code for the type-1/3
sensor that this pen does not use). No GPIO bank1 (`0x04000088`) output is ever written. [Proven]

Therefore **no pin can be confirmed as an LED via the DIR register** — because no pin is dedicated
to an LED at all.

---

## 3. Q3 — Pulse timing. N/A (no firmware pulse). [Proven]

There is no LED pulse to time. The nearest firmware "blink" is `fatal_low_battery_blink`
(0x08038d24), and it does not drive a real pin:

```
gpio_write(15, 1)              ; hold power latch ON
delay(10)
for i in 0..5:                 ; 6 iterations
    gpio_write(0xFF, i&1?0:1)  ; <-- pin 0xFF = DUMMY, no physical effect
    delay(100)                 ; ~100 delay-units per half (hal_delay_1k)
gpio_write(15, 0)              ; drop power latch → power off
b .                            ; spin forever
```

Cadence would be ~6 toggles at ~100 delay-units each **if** pin 0xFF were wired, but on this board
0xFF is the "no-pin" sentinel (`pmu-power-management.md` §1.1; nb disasm 0x07fff6d8-0x7fff730), so
the loop is a visible-effect no-op that just burns time before the pen powers itself off. It runs on
a **fatal battery-calibration mismatch** in `power_battery_check` (0x08038da8), i.e. a boot-time
error halt — not a per-tap indicator and not in the IRQ/timer OID path. [Proven]

---

## 4. Q4 — Other uses / where the visible flash actually comes from

**Firmware LED writers: none functional.** No boot, charging, low-battery, error, or power-state
code drives a real indicator GPIO:
- Boot / power-on: `app_init_main` (0x08038f5c) sets GPIO15=1 (power latch) — not an LED. [Proven]
- Charging: state 6 (`charge_main`/`charge_count`) animates the **screen/voice**, no LED GPIO. [Proven]
- Low battery: warning is **voice** (0x17/0x1A/0x14); the only "blink" is the dummy-pin
  `fatal_low_battery_blink` above. [Proven]
- Error / power-off: all end at GPIO15=0 + spin — no LED. [Proven]

**So what flashes on a tap?** [Inferred, hardware-side]
1. **Most likely — the Sonix OID decoder module's capture/illumination LED.** The SN9P601FG
   decoder + SNM9S102 optical module has built-in IR/illumination LED drives (the SN9P701F
   datasheet shows `IRED0/1` LED-drive pins). The ASIC strobes its illumination
   LED to image the paper and does the entire dot→index decode autonomously; the SoC only clocks in
   the finished index over GPIO2/GPIO9. A brief flash "when an OID is detected" is exactly the
   decoder's per-capture strobe — driven by the sensor ASIC, **not** the firmware. This matches the
   observed behaviour and the total absence of any decode-time GPIO write in firmware.
2. **Possible secondary correlate — the ZC90B.** `hardware-external-refs.md` notes the ZC90B crypto
   IC "emits data bursts at power-on and **while LED flashes**", and `event_pump_ring` toggles
   GPIO5 (a ZC90B line) on every dispatched event (including 0x1060 taps). This is a weak,
   non-authoritative correlation, not a firmware LED driver.
3. The **power-button PCB "1399A0"** has an LED (blink 36 ms / 10.34 s per the wiki) — a
   housekeeping/power indicator with a fixed hardware cadence, **not** SoC-firmware-driven and not
   tied to taps.

If a *dedicated SoC-driven* LED existed it would be on one of pins 3/4/14 — but those pins are never
touched, so this can be excluded. [Proven]

---

## 5. Q5 — Consequences

- **There is no LED GPIO.** The firmware writes no real indicator pin, so any tooling that runs
  the firmware has nothing to observe or drive for an LED. [Proven]
- Pin **0xFF** (used by `fatal_low_battery_blink`) is the dummy no-pin: writes are harmless no-ops
  and reads ≡ 0 (consistent with the power-off spin loops in `pmu-power-management.md`). [Proven]
- Any per-tap illumination is a property of the Sonix two-wire OID front-end (the sensor's own
  capture LED), not of a SoC GPIO. Purely cosmetic; irrelevant to boot or a book session. [Inferred]

### Proposed names / docstrings

| addr | name | docstring |
|---|---|---|
| 0x080057cc | `hal_oid_timer_cb` | Periodic OID poll: reads GPIO9, decodes, posts event 0x1060. **No GPIO write / no LED pulse on detect.** |
| 0x08038d24 | `fatal_low_battery_blink` | Boot battery-cal fatal: GPIO15=1, 6× toggle **dummy pin 0xFF** (no physical effect on this board), GPIO15=0, spin. Anyka reference LED-blink vestige; drives no real indicator. |
| 0x0800af74 | `gpio_pin_lookup` | Logical-channel→physical-pin map; returns **0xFF (no-pin)** for the dormant I²C channels (0xB/0xC/0xF/0x10) — the `reg04000080_*` I²C bit-bang is dead code. |
| 0x080f39ac / 0x080f39c4 | `reg04000080_set_bit` / `_clr_bit` | Direct `0x04000080` RMW for the **dormant** I²C block only; called with a 0xFF pin id → no real GPIO. |

---

## 6. Bottom line

- **Blink-in-firmware?** No. The OID-decode path (`hal_oid_timer_cb` → decode23 → post 0x1060) does
  no GPIO write; no firmware code pulses an indicator pin on a tap.
- **Which GPIO?** None. No dedicated LED pin exists; all written pins have known non-LED roles, and
  the unassigned outputs (3/4/14/17+) are never driven.
- **How pulsed?** It isn't, by the SoC. The visible tap flash is the **Sonix OID decoder module's
  capture/illumination LED**, strobed by the sensor ASIC outside the SoC (closest firmware
  correlate is only the non-specific GPIO5/ZC90B event-pump toggle). The lone firmware "blink"
  (`fatal_low_battery_blink`) is a dummy-pin no-op boot-error halt.
