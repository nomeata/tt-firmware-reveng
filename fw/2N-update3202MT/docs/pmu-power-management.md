# Power management RE — battery, charger, USB-detect, auto-off, power-on/off (and why 0x04070000 is NOT a PMU)

Static analysis of the named decompilation (unified base **0x08009000**). Evidence tags:
**[Proven]** = read directly from the cited decomp/disasm/literal pools; **[Inferred]** = deduced
(reason given); **[Open]** = unresolved. Companions: `state8-to-13-transition.md` (state-8 deep
dive), `autonomous-mount-state8.md`, `statechart-framework.md`, `gme-timer-counter-random.md`
(sw-timer HAL), `soc-core-registers.md` (GPIO HAL + OID pins).

---

## 0. Headline findings

1. **There is no PMU block at 0x04070000.** That address is the **USB device controller — a
   Mentor-Graphics MUSB-compatible core** (Anyka SoCs use MUSB): `0x04070001` = MUSB **POWER**
   register, `0x0407000A` = MUSB **INTRUSB** interrupt-status, `0x04070348` = an Anyka
   **PHY-control extension** (bit0 = full-speed force). All firmware "PMU reads" in state 8 are
   USB-bus-activity queries used to tell a **PC host apart from a dumb charger**. [Proven — §2]
2. The pen's real power management is discrete: a **power-hold GPIO (pin 15)** that latches the
   pen's own supply, a **battery-voltage ADC** in the sys-ctrl block (`0x04000064`/`0x04000070`),
   two **battery comparator/status input pins (GPIO 11 & 1)**, the **VBUS-detect input GPIO 8**,
   and a **USB-power-path control GPIO 6**. Power-off = drive GPIO 15 low and spin. [Proven]
3. **Auto-off**: in idle standby (no product, no USB) the standby handler counts 0x1046
   heartbeats (~100 ms each) in `u16 @0x081da07c`; **count > 300 (~30 s) → power-off**. [Proven
   counter/threshold; ~100 ms per tick Inferred from the 20 ms timer-IRQ assumption]
4. **Low battery**: runtime monitor on every 0x1046 heartbeat; raw-scaled ADC **< 0x300 (≈2.47 V)
   → warn stage (voice 0x17)**; **< 0x2C0 (≈2.27 V) persistently (10 ticks) → final voice 0x1A,
   then power-off jingle 0x14 → event 0x1062 → GPIO15=0**. Firmware update refuses below
   **0x300** and plays the **`BatLowUpdate`** voice. [Proven thresholds; voltage conversion
   Inferred from the firmware's own log formula]
5. **Buttons**: the three physical buttons arrive as **event 0x105F with a code byte**: code
   **5 = volume-up, 6 = volume-down, 8 = power button**; power (code 8, sub 1) plays voice 0x14
   and powers off. [Proven routing/actions; the "physical buttons" identification Inferred]

---

## 1. The actual power-relevant hardware interface

### 1.1 GPIO pins (HAL: read `func_0x08007740(pin)` → `0x040000BC` bit; write `func_0x08007734(pin,v)` → `0x04000080`; dir `func_0x0800774c`)

| pin | dir | role | evidence |
|---|---|---|---|
| **15 (0xF)** | out | **POWER-HOLD latch.** Boot drives 1 (`app_init_main` 0x08038f5c L20-25, `clock_periph_init` 0x08039628); **power-off = drive 0** + spin (`sys_reset` 0x080508f8, `charge_usb_state_machine` 0x08050a8c, auto-off in `standby_handler` 0x08051b0c L89, mount-failure path 0x08038f5c L70, auth-fail kill 0x0804cecc L45-49) | [Proven] |
| **8** | in | **USB/charger VBUS detect** (1 = cable present). `FUN_0803ced8` = `read(8)==1`; gates state 8, charger-out, battery calibration abort, idle re-poll | [Proven fn; bit8 per state8 doc] |
| **11 (0xB)** | in | battery-status comparator A — see §3.1 and the **[Open]** note §3.5 | [Proven uses] |
| **1** | in | battery-status comparator B (only read in `battery_get_level`) | [Proven] |
| **0** | in | read + logged next to pin 11 ("`11 LEVEL=%d,0 level=%d.`") but not acted on | [Proven] |
| **6** | out | **USB-power-path switch**: `FUN_080abbc8` (on VBUS: pin6=1, wait, pin15=0 = run from USB, release battery latch); `FUN_080abc34` (pin15=1, pin6=0 = back on battery latch) | [Proven writes; power-path semantics Inferred] |
| 5 / 10 | i/o | anti-clone **auth-chip bit-bang** (challenge/response, `FUN_0804c47c`); failure sets `akoid_buf[0xb4]=1` → power-off kill in 0x0804cecc | [Proven] |
| 9 | in | OID sensor data (hw-io doc); also "input idle" gate for the USB re-poll (§4.4) | [Proven] |
| 12 (0xC), 13 (0xD) | out | audio-amp / codec power strobes (heartbeat amp toggle, standby/teardown) | [Proven writes] |
| 0xFF | — | **dummy pin**: nb GPIO read shifts by (0xFF-32)=223 → constant **0** (nb disasm 0x07fff6d8-0x7fff730). `while(read(0xff)!=0)` = no-wait; `if(read(0xff)==1)break` = never — the power-off spin loops are intentional "wait until the supply dies" | [Proven from nb disasm] |

### 1.2 Battery-voltage ADC (sys-ctrl block, NOT 0x0407xxxx)

`FUN_0800b3c8` = **battery_adc_read**: set `0x04000064 |= 0x200` (ADC enable/channel), read
**`0x04000070` bits [19:10]** (10-bit sample) 4×, average, disable, return **avg×2** (an
11-bit "scaled raw"). [Proven]

Conversion used by the firmware's own log (`fw_update_cleanup` 0x08052768 L117: "`Battery %d`" =
`scaled*0x21/0x2800` = `scaled*33/10240`): full scale 0x800 → 6.6, i.e. **volts ≈
scaled×3.3/1024** — consistent with the ADC pin seeing **Vbat/2** of a 2×AA pack (0x300 → 2.47 V,
0x2C0 → 2.27 V). [Inferred from the formula; divider assumption]

Helpers: `FUN_0800b174` pushes samples into a 4-entry ring `@0x081db928`; `FUN_0800b15c` averages
the ring (drop min/max) and scales to **millivolts** via calibration bytes; `FUN_0800ada4` bins
the mV value into **battery level 1..8** (breakpoints 0xCE4/0xD00+0xDE/0xE10/0xE74/0xED8/0xF50/
0x1004 ≈ 3300/3550/3600/3700/3800/3920/4100 mV — Li-ion-style vendor code, used by the charge
screen animation `FUN_0803d10c`). [Proven code; mV interpretation Inferred]

`FUN_0800b464(on)` toggles `0x04000004` bit 15 around ADC bursts and heavy FS phases —
measurement/load enable of some kind. [Proven writes; semantics Open]

### 1.3 Boot-time ADC calibration — `power_battery_check` 0x08038da8 (called `(1)` from `app_init_main`)

Aborts immediately if **GPIO8==1** (on cable — measurement would see charge voltage). Otherwise:
10 quick samples (`FUN_0800b3c8`) + 1000 samples of the second ADC reader (`func_0x0800b428`)
under `FUN_0800b464(1)`; sanity-compares the two averages against reference **0x155**±2% and each
other (±0x1E..0x20 window, complaint hook `func_0x08038d24(...,0x7fff)`), then stores
`avg_a+avg_b` into **`game_ctx+0x30` / `+0x32`** — the runtime monitor's baseline (it
self-corrects afterwards, §3.3). Returns a fresh scaled reading. [Proven mechanics; "calibration"
label Inferred]

---

## 2. The 0x04070000 block = MUSB USB device controller [Proven]

Every firmware access, with the standard MUSB register map:

| addr | MUSB reg | firmware use |
|---|---|---|
| **0x04070001** (byte) | **POWER** (bit0 EnSuspendM, bit3 Reset, bit4 **HSMode**, bit5 **HSEnab**) | `usb_detect_2` 0x08050bc4 writes **0x21** (HSEnab\|SuspM) to arm detection (base ptr `*0x08050c10` = **0x04070000** [Proven pool]); `usb_report_speed` 0x08041324 writes **1** (full-speed) / **0x21** (high-speed); `FUN_08041ce4` writes **0x20**; disconnect paths write **0** (`usb_connect_handler` L74/96, `FUN_080413fc`); `usb_connect_handler` L62 reads **bit4 HSMode** ("host negotiated HS") |
| **0x0407000A** (byte) | **INTRUSB** interrupt status (bit0 Suspend, bit1 Resume, **bit2 Reset**, **bit3 SOF**, …; read-clears) | `usb_connect_handler` L50-70: **bit2 (0x04) = bus RESET seen → a real host is enumerating** (sets `game_ctx[0x1e]=4`); **bit3 (0x08) = SOF arriving** — either (with HSMode) confirms **PC** → post 0x105C; logged as "`stat  = %d`". `FUN_08041ce4` L36-44 busy-waits for SOF (≈0xC3500 loop timeout) and returns 1 = host alive |
| **0x04070348** (word) | Anyka **PHY control** extension | bit0 set = force full-speed (`usb_report_speed` full branch, cleared in HS branch, cleared by `usb_detect_2`, `FUN_08041ce4`, `0x080414e4` "reset to full speed!") |

Related sys-ctrl bits toggled with the USB PHY: `0x04000058` bits 0..2/4 (values `|4`, `&~0x14`,
`|6`), `0x0400000C` bit 20, `0x04000034` bits 6/10, `0x0400004C` bit 4, `0x0400003C` bit 9
(pull for the detect pin, `usb_present_query` 0x0803cf7c). [Proven writes; individual bit
semantics Open]

**PC vs charger discrimination** (the whole point of state 8): VBUS present (GPIO8=1) → enable
PHY (`usb_detect_2`) → wait up to **0x28 settle ticks**; if INTRUSB shows **Reset/SOF** (a host
talks) → "`usb connect pc!!`" → event **0x105C** → state 5 (mass storage); if the bus stays dead
→ "`go to charger`" → event **0x1009** → state 6. GPIO8 dropping (2 reads 20 ms apart) at any
point → "`detect out!!`" → **0x100C** → back to standby. [Proven — `usb_connect_handler`
0x08050d28; see state8-to-13-transition.md §2]

**Mask ROM**: no PMU/USB-power init found there either; it only reads the boot-mode strap from
`0x040000BC`. Power-on latching is pure board hardware: the button powers the
SoC, firmware then holds GPIO15=1. [Proven for the firmware side; board circuit Inferred]

---

## 3. Battery model

### 3.1 Boot gate — `battery_get_level` 0x0804bf18 [Proven]

`return read(GPIO 11)==1 && read(GPIO 1)==1;` (logs pins 11 and 0 as "`11 LEVEL=%d,0 level=%d.`").
`splash_entry` 0x0804c1d4 requires **four successive 1-results** (delays 5/5/20 HAL units apart)
to take the **FACTORY PROD-TEST** path (this is the prod-test path, *not* a "healthy boot"):
`akoid_buf[0xb3]=1`, arm the 100-unit poll timer, `sm_set_sound_profile(2,0)` (a volume/profile
setter, NOT a state transition), checksum-verify `B:/spotlight.bin`, play voice **0x1B — a
prod-test SPEECH announcement, not a jingle** — then the 0x0804cecc stage machine speaks
0x1C/0x1D/0x1E and parks at splash awaiting jig input (`B:/Prodtest.txt`, `B:/TestFile/Test*.bin`
context — see `power-on-sound.md` §5). Any 0 → the **quiet path = the NORMAL RETAIL boot**
(battery health is judged by the ADC monitor, not these pins): `akoid_buf[0xb3]` stays 0; the
splash EA 0x0804cecc posts **0x1014** (splash mapper 0x0804e624: 0x1014 → state 3) with **no
sound**. Retail pens must read 11/1 = 0 at boot — a static 1 would speak test phrases every
power-on and never reach standby. GPIO bits 11/1 = 0 is therefore the **authentic retail boot**,
and it is also what makes the first-load gate (`akoid_buf[0xb3]==0`) fire.
[Proven code; "retail reads 0" Inferred by elimination]

### 3.2 Runtime low-battery monitor — `FUN_080afd78`, driven by every **0x1046** heartbeat [Proven]

Caller: `heartbeat_1046_handler` 0x0800acfc (state-9 transition-action[0]). Skips unless
`game_ctx[0x1d] ∈ {2,4,8,9,…}` (not <2, not 3) and low-batt stage `*0x08008c0b ≤ 3`.
Reads `FUN_0800b3c8()` (scaled raw, §1.2) and compares against the baseline
`game_ctx+0x30/+0x32`:

| condition | action |
|---|---|
| value ≥ 0x2C0 and steady | **mode OK**: `game_ctx+0x10=3`, baseline `+0x30 := value` (self-recalibrates); sustained sag (>0x1F under baseline, 10 ticks, audio-dependent window 0x1F/0x46) refreshes `+0x32` |
| **value < 0x300** for 3 consecutive ticks | `game_ctx+0x10=6`, `akoid_buf[0xb9]=1` → **warn** |
| **value < 0x2C0** for 10 consecutive ticks (or erratic jumps) | `game_ctx+0x10=7`, `akoid_buf[0xb9]=2` → **final** |

On either trigger, if **no USB** (`game_ctx[0x1e]==0` && GPIO8==0) and stage<3: plays the pending
voice (`fwl_voice_play_2`), sets stage `*0x08008c0b=1`, flag `*0x081da086 |= 0x10`; if no product
is loaded (`akoid_buf[0x21]==0xff`) also posts 0x104A+0x1058. [Proven]

### 3.3 The announce/shutdown continuation (0x0804cecc, LAB_0804e27c) [Proven]

On the next events, stage 1 + flag bit4: `akoid_buf[0xb9]==1` → play **voice 0x17** (warn
announce), then continue running (stage→0, b9=3); `b9==2` → play **voice 0x1A** (battery empty),
wait, stage 3 → play **voice 0x14** (good-bye/off jingle), wait, stage 5, post **0x1062** →
power-off path (§6.2). [Proven; which German phrase is 0x17 vs 0x1A: Open — Chomp_Voice indices]

### 3.4 Firmware-update battery gate — `fw_update_cleanup` 0x08052768 [Proven]

Before applying a `B:/*.upd`: 5 × `FUN_0800b3c8`; logs "`Battery %d`" (=volts, §1.2); any sample
**< 0x300 → abort update**, play **`A:/Language/<lang>/BatLowUpdate/<name>.wav`**
(`lang_play_batlow(1,lang)` 0x080523b4 — param 0 selects the normal `/Update/` voice). This is
what the `.upd`'s BatLowUpdate WAVs are for: *"battery too low to update"*, not general
low-battery. General low-battery uses the Chomp_Voice indexed voices (0x17/0x1A/0x14).

### 3.5 [Open] The physical meaning of GPIO 11

Three proven uses conflict with a single static "battery-OK comparator" reading:
(a) `battery_get_level` needs pin11==1 for the healthy boot; (b) idle standby (mode 8) treats
pin11==1 (2 reads, 20 ms apart) as a trigger to **re-scan `a:` into the booklist
(`udisk_gme_discovery`) and `soft_reboot`** (0x08051b0c L100-117 — a healthy static 1 would
reboot-loop the pen), which was confirmed dynamically. So pin 11 is either a transient/latched
"content changed / power event" line or shares the pad with a second function. The consistent
retail reading is **bit11=0, bit1=0**.

---

## 4. Charger / USB power states

### 4.1 `game_ctx[0x1e]` — the cable-state field (game_ctx = 0x080089A4) [Proven]

| value | meaning | setter |
|---|---|---|
| 0 | battery only (no cable) | boot; `usb_connect_handler` "detect out"; splash via `usb_present_query`==0 |
| 1 | dumb charger confirmed | `usb_connect_handler` charger branch (L94) |
| 2 | USB-PC confirmed | `usb_connect_handler` PC branch (L72) |
| 3 | state-8 detection armed | first settle tick (L45) |
| 4 | host bus-reset seen (mid-classification) | INTRUSB bit2 (L60) |

### 4.2 State 8 (classification) — see §2 and state8-to-13-transition.md. Entry only via event
**0x105D**, posted by: content leaves re-polling, the idle re-poll (§4.4), `usb_power_switch`
cable-out, and the low-batt splash helper `FUN_0803d094` (posts 0x104A+0x105D when
`game_ctx[0x1e]` ∈ {1,2}). [Proven]

### 4.3 State 6 (charging) — `charge_main` 0x080504a4 + `charge_count` 0x08050670 [Proven]

Entry: "`Enter Charge`", buffers, **1000-unit periodic timer** (`hal_timer_register(0,1000,1,
0x08003994)`, handle `*0x08121e60`, ≈1 s ticks), `game_ctx[0x1d]=9`. Each tick (`charge_count`):
`ChargerCount++` ("`ChargerCount =%d.`"), at **>1000 ticks (≈17 min)** call `FUN_080abbc8(2)` —
**switch the power path to USB and release the battery latch** (pin6=1, pin15=0, §1.1); GPIO8==0
for **20 consecutive ticks** → post **0x100C** (cable out → exit to standby). Flag byte
`*0x08008c01` bits 1/2 sequence the pin-6/15 handover (10-tick debounce), `FUN_080abc34`
reverses it. `charger_out_handler` 0x0803cff8 (posts around **0x100A**) handles the
out-transition bookkeeping + DAC re-setup. [Proven code; timing units Inferred]

### 4.4 Idle USB re-poll (how a cable is noticed outside state 8) [Proven]

The global handler 0x0804cecc (tail, L779-819): when quiet (no audio, stage<3, akoid idle,
GPIO9==1, GPIO8==0) and ≥0x32 system ticks since last check: `FUN_08050898()` (amp/codec
re-init) and if **GPIO8==1** → post **0x104A + 0x105D** → state 8 classification.

### 4.5 State 5 (USB-PC): entry `usb_state_dbg` 0x08050fb8 (`*0x08121e68=1`, amp off, akoid
re-armed); `usb_state_handler` 0x08051090 keeps **GPIO15=1** during the session, mounts the
FAT for the host, `usb_power_switch` 0x0803d1d4 after ~1.2M polls logs "`switch to usb power.`"
and releases the battery latch (pin15=0 — the pen then runs from VBUS); on unplug ("`usb out`")
posts 0x104A/0x105D; `FUN_08051138` posts **0x100C** when the session byte `*0x08121e68` drops.
[Proven]

---

## 5. Auto-off / sleep

### 5.1 The idle auto-off (standby, nothing mounted) — `standby_handler` 0x08051b0c [Proven]

Accepted "tick" events: {0x100C, 0x104A, 0x100A, **0x1014**, **0x1048**, **0x1046**} (pools
0x08051d30/34). Flow on battery (`game_ctx[0x1e]==0`):

1. First event after standby entry: **re-arm** — `akoid_rearm()` (OID → wake mode),
   `game_ctx[0x1d]=8` (idle-armed submode), counter `u16 *0x081da07c = 0`. (If
   `game_ctx[0x24]==1` — latched by `app_init_main` 0x08038f5c from **GPIO11-at-boot**
   (power button still held), or set by splash's `B:/FLAG.bin` update-resume branch;
   see `power-on-sound.md` §3 — post 0x1058 instead. On a clean retail boot GPIO11=0
   (power not pressed) + no FLAG.bin ⇒ `[0x24]=0` ⇒ this clean idle path.)
2. Every subsequent accepted event (in practice the **0x1046 OID-poll heartbeat, ~100 ms**):
   cancel the aux timer handle `*0x08008c10` (once), **counter++** ("`Count %d.`");
3. **counter > 300 (~30 s)** → inlined power-off: `sm_set_state(0)`, amp off, timers reset,
   **GPIO15=0**, spin → pen dies. (Identical sequence factored as `sys_reset` 0x080508f8.)
4. Below the threshold: the GPIO-11 branch (`a:` rescan + `soft_reboot`, §3.5).

Reset condition: any activity that leaves standby (OID tap → 0x1060 → states 9/12/13; USB →
0x105D → 8) stops the count; re-entering standby re-runs step 1 (counter=0). With a product
mounted the pen sits in state 13, where this counter never runs. [Proven]

*Note the tick-rate caveat: 100 ms/0x1046 assumes the 20 ms timer IRQ (`hal_timer_register`
reload = period/20). If the IRQ is 10 ms, auto-off = 15 s. [Inferred]*

### 5.2 Book-mode (state 13) idleness

No firmware-global inactivity power-off was found in `book_mode_handler`/`gme_oid_dispatch`;
long-idle behavior in a book is product-driven (the GME **additional script** timer, e.g. the
game handler `FUN_08090718` counts its own `+0xa0` ticks to 300 for a reminder voice). The pen's
"goes quiet then off" in a book is therefore: GME reminder script(s) → eventually the product or
the user (power button) triggers off. [Proven for the absence in the two handlers; overall
Inferred/Open]

### 5.3 Wake

Sleep never powers the core down short of GPIO15=0 (which is full off — RAM lost, next start is
a cold boot through the mask ROM). "Waking" from idle standby is just the OID pen-down IRQ path
(`akoid_rearm`). There is no suspend-to-RAM. [Proven within the analyzed code]

---

## 6. Power on / off

### 6.1 Power-on [Proven]

Button (hardware) powers the SoC → mask ROM → SPL → PROG `app_init_main` 0x08038f5c:
**GPIO15 dir out, =1** (self-hold) at L20-25, logs "`syspoweron.`" (string 0x08039180),
`game_ctx[0x1d]=1`, `game_ctx[0x1e]=0`, `power_battery_check(1)` (§1.3), mount → statechart.
Mount failure → **GPIO15=0** + spin (the pen turns itself off rather than hanging visibly).

### 6.2 Power-off paths (all end at GPIO15=0 + spin) [Proven]

| trigger | path |
|---|---|
| **power button** (event **0x105F**, code byte **8**, sub 1; `game_ctx[0x1d]` ∈ {2,4}) | global handler `FUN_0800a9c8`: stop audio, flags reset, **voice 0x14**, wait, stage 5, post 0x104A/0x105B/**0x1062**, teardown → `charge_usb_state_machine` 0x08050a8c: ~20s of GPIO15=0 retries → dead (on VBUS the loop keeps the firmware parked — the "off but charging" state) |
| **battery final-low** (§3.3) | voice 0x1A → 0x14 → post **0x1062** (`FUN_08050864` re-posts 0x1062 for stray events) → same off path |
| **auto-off** (§5.1) | inlined `sys_reset` in `standby_handler` |
| **auth-chip failure** | 0x0804cecc L42-49: GPIO15=0 + `while(true)` |
| **mount failure** | `app_init_main` L70 |
| NAND write failure during update | `FUN_08043190` "`system is low power, please change power`" + hang (vendor misnomer — it is the write-fail handler) |

Volume buttons for completeness: 0x105F code **5** = `FUN_0800b6b4` volume-up, code **6** =
`FUN_0800b6e0` volume-down (index `*0x081db904` 0..6, DAC gain table 0x0800b9e4 via
`FUN_0802265c`), feedback voices 0x15/0x16. [Proven]

### 6.3 Power state machine (condensed)

```
            [button] → maskROM boot → app_init (GPIO15:=1) → splash(1)
                                                             ├─ GPIO11&&GPIO1 4×ok → FACTORY PROD-TEST (speech 0x1B/0x1C/…, parks at splash)
                                                             └─ normal retail quiet path (silent) ──0x1014→ standby(3)
                                                                (B:/FLAG.bin present → voice 0x19 at splash, then standby auto-posts
                                                                 0x1058 → book(13) → jingle 0x13 with NO tap — power-on-sound.md)
 standby(3) ── first tick, no USB → mode 8 (idle-armed, counter=0)
    │  ├─ 0x1046 ticks: counter>300 → OFF (GPIO15=0)                      [auto-off]
    │  ├─ OID tap → 0x1060 → 9 → 12 → book(13)                            [activity resets]
    │  └─ GPIO8=1 (via idle re-poll 0x105D) → state 8
 state 8 ── INTRUSB reset/SOF → 0x105C → USB-PC(5) ── unplug → 0x100C → standby
    │       no bus activity in 0x28 ticks → 0x1009 → charging(6) ── unplug ×20s → 0x100C → standby
    └       GPIO8 drops → 0x100C → standby
 any run state ── battery <0x300 warn (voice 0x17) ── <0x2C0 final → 0x1A + 0x14 → 0x1062 → OFF
 any run state ── power button (0x105F code 8) → 0x14 → 0x1062 → OFF/charge-park
```

---

## 7. The power-relevant inputs for a healthy, battery-powered book session

Consolidating the findings above, the power-domain inputs that define a clean autonomous boot are:

1. **GPIO `0x040000BC` bit 8 = 0** — no VBUS: state 8 never entered / exits instantly; no spurious
   USB-PC; the idle re-poll stays quiet; `power_battery_check` runs its full calibration.
2. **Bits 11 = 0 and 1 = 0** — the quiet low-batt splash path; keeps the standby `a:`-rescan/reboot
   branch (§3.5) and the state-2 prod-test detour disabled, and preserves the first-load gate
   (`akoid_buf[0xb3]==0`). *(A "fully authentic healthy pen with power held" is 11=1 & 1=1 plus the
   state-2 / `0xb3=1` prod-test branch — a different classifier path.)*
3. **Timer IRQs** (the 0x1046 heartbeat) also drive the battery monitor.
4. **ADC `0x04000070` bits[19:10] ≥ 0x180** whenever `0x04000064` bit9 is set (scaled ≥ 0x300;
   a steady **0x1E0 → scaled 0x3C0 ≈ 3.1 V** is representative) — otherwise, 3 heartbeats after
   boot, the monitor starts the low-battery voice/shutdown cascade (and `fw_update_cleanup` would
   refuse updates). A constant value keeps the monitor in mode 3 (OK); the boot baseline
   self-corrects.
5. **`0x0407000A` (INTRUSB) = 0** and `0x04070001` (POWER) scratch — only touched if state 8 is
   entered; INTRUSB=0 = "dead bus" = charger-not-PC, and GPIO8=0 exits first anyway.
6. **Auto-off**: after standby entry with nothing mounted, the firmware powers off in ~300
   heartbeats (~30 s), ending at **GPIO15=0** (power latch dropped) + spin.

(These were confirmed dynamically in [tt-emu](https://github.com/nomeata/tt-emu).)

---

## 8. Proposed names / docstrings (for names.csv)

| addr | proposed name | docstring sketch |
|---|---|---|
| 0x0804bf18 | `prodtest_gpio_gate` (now battery_comparators_ok) | GPIO11 && GPIO1 both 1 = factory prod-test splash branch (retail pens read 0); logs "11 LEVEL/0 level" |
| 0x08038da8 | `battery_adc_calibrate` (now power_battery_check) | boot ADC cross-calibration → baseline game_ctx+0x30/+0x32; aborts on VBUS (GPIO8) |
| 0x0800b3c8 | `battery_adc_read` | 0x04000064 bit9 on, 4×avg of 0x04000070[19:10], ×2 |
| 0x0800b428 | `battery_adc_read2` | second channel/loaded variant used by the calibration |
| 0x0800b464 | `battery_meas_enable` | 0x04000004 bit15 on/off around ADC/FS bursts [semantics Open] |
| 0x0800b174 / 0x0800b15c | `battery_ring_push` / `battery_voltage_mv` | 4-ring @0x081db928; drop-min/max avg → mV via cal bytes |
| 0x0800ada4 | `battery_level_bin8` | mV → level 1..8 (charge-screen animation) |
| 0x080afd78 | `battery_monitor_tick` | per-0x1046; <0x300→warn(b9=1), <0x2C0×10→final(b9=2); stage @0x08008c0b, flag 0x081da086 bit4 |
| 0x0800acfc | (extend heartbeat_1046_handler) | + "runs battery_monitor_tick every heartbeat; amp keep-alive toggle GPIO12" |
| 0x0800a9c8 | `button_dispatch_105f` | 0x105F codes: 5=vol+, 6=vol− (idx @0x081db904, gains 0x0800b9e4), 8/1=power button → voice 0x14 → 0x1062 → off |
| 0x0800b6b4 / 0x0800b6e0 / 0x0800b6a4 | `volume_up` / `volume_down` / `volume_get` | |
| 0x080508f8 | `power_off` (now sys_reset) | teardown + GPIO15=0 + spin-until-dead |
| 0x08050a8c | `power_off_or_charge_park` (now charge_usb_state_machine) | repeated GPIO15=0; survives only on VBUS |
| 0x08050864 | `repost_off_event_1062` | |
| 0x08041ce4 | `usb_wait_host_sof` | POWER=0x20, poll INTRUSB SOF ≤ ~0xC3500 iters; 1 = host alive |
| 0x080413fc | `usb_phy_off` | POWER=0, sysctrl teardown |
| 0x08050bc4 | `usb_phy_arm_detect` (now usb_detect_2) | POWER=0x21, 0x348&=~1, 0x04000058\|=4 |
| 0x0803ced8 / 0x0803cf7c | `usb_vbus_present` / (keep usb_present_query) | GPIO8==1 |
| 0x0803d1d4 | (keep usb_power_switch) | + battery-latch release after 1.2M polls in USB-PC |
| 0x080abc34 / 0x080abbc8 | `pwr_path_battery` / `pwr_path_usb` | pin15/pin6 handover, flags @0x08008c01 |
| 0x0804c47c | `authchip_challenge_gpio5_10` | anti-clone check; fail → akoid_buf[0xb4]=1 → kill |
| 0x08051b0c | (extend standby_handler) | + idle auto-off: mode-8 counter @0x081da07c >300 → power_off; GPIO11 rescan branch |
| 0x0804cecc | `state2_fwupdate_prodtest_handler` (now fwupdate_verify_image) | + low-batt announce continuation + idle USB re-poll tail + auth kill |
| 0x080523b4 | (extend lang_play_batlow) | plays A:/Language/<lang>/(Update\|BatLowUpdate)/<voice>.wav |
| 0x08043190 | `nand_update_write_fail_hang` | the "system is low power" string is a write-failure message |
| globals | | `0x081da07c` u16 idle-off counter; `0x08008c0b` low-batt stage; `0x08008c01` power-path flags; `0x081da086` bit4 low-batt-voice pending; `game_ctx+0x10` batt mode (3 ok/6 warn/7 final); `game_ctx+0x30/+0x32` ADC baseline; `game_ctx+0x1d` run submode (1 boot, 2 fresh-standby, 8 idle-armed, 9 charging); `game_ctx+0x1e` cable state (§4.1); `0x08121e60` charge-timer handle; `0x08121e68` usb-session byte; `0x081db904` volume idx |

Voice IDs (Chomp_Voice): 0x14 power-off jingle, 0x15/0x16 volume feedback, 0x17 battery-warn,
0x1A battery-empty, 0x1B prod-test announcement (speech — NOT a power-on jingle), 0x19
update-resume; the **power-on jingle is 0x13** (first book-entry per power cycle —
`power-on-sound.md`). [Proven call sites; 0x13/0x14 jingle vs speech proven by pitch analysis of
the extracted WAVs, which are firmware content obtained separately and not included here]

---

## 9. Evidence index

| claim | status | source |
|---|---|---|
| 0x04070000 = MUSB core (POWER@+1, INTRUSB@+0xA), base ptr in pool | Proven | `0x08050c10`→0x04070000; reg semantics match MUSB layout (Inferred mapping, Proven offsets/bits used) |
| PC-vs-charger = INTRUSB reset/SOF within 0x28 ticks | Proven | 0x08050d28.c L50-99 |
| GPIO15 = power-hold; all off-paths drive it 0 | Proven | 0x08038f5c.c, 0x080508f8.c, 0x08051b0c.c L89, 0x08050a8c.c, 0x0804cecc.c L45-49, 0x08039628.c |
| pin 0xFF read ≡ 0 (spin loops) | Proven | nandboot disasm 0x07fff6d8-0x7fff730 |
| battery ADC = 0x04000064 bit9 / 0x04000070[19:10], ×2 scale | Proven | 0x0800b3c8.c |
| volts = scaled×33/10240 (firmware's own log) | Proven formula | 0x08052768.c L117 |
| low-batt thresholds 0x300 warn / 0x2C0 final; stages/voices/0x1062 | Proven | 0x080afd78.c, 0x0804cecc.c LAB_0804e27c, 0x0800a9c8.c |
| BatLowUpdate = update-refused voice, gate <0x300 | Proven | 0x08052768.c L121-128, 0x080523b4.c |
| auto-off = mode-8 counter @0x081da07c > 300 | Proven | 0x08051b0c.c L69-99 (pool 0x08051ac8→0x081da07c) |
| tick = 0x1046 ≈ 100 ms | Inferred | hal_timer_register(0,100,1) reload=period/20 + 20 ms IRQ assumption (gme-timer doc) |
| buttons = 0x105F codes 5/6/8 | Proven routing / Inferred physical | 0x0800a9c8.c; vol-idx+gain table Proven |
| charger state ticks 1 s; >1000 → USB power path; out ×20 → 0x100C | Proven code / Inferred units | 0x080504a4.c, 0x08050670.c, 0x080abbc8.c |
| auth-chip GPIO5/10 challenge; fail → power-off | Proven | 0x0804c47c.c, 0x0804cecc.c |
| GPIO11 triple role conflict | Proven uses / Open semantics | 0x0804bf18.c, 0x08051b0c.c L100-117 |
| no firmware-global book-mode auto-off | Proven absence in 0x0804eb08/0x0803629c; overall Open | grep + read |
