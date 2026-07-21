# SoC core control registers — 0x04000000–0x040000FF (Anyka AK1050 / ZC3202N, 2N MT firmware)

Authoritative, consolidated register map for the **SoC CORE peripheral window
`0x04000000`–`0x040000FF`**: GPIO, clock/PLL, ADC/analog-power, the interrupt
controller, the timer/wake block, boot-mode straps and the clock-gate. The AK1050 has
**no public datasheet**, so this RE (from the MT decompilation + the 2N mask ROM, which was
captured from hardware and is not included here) is the only reference.

Addresses: PROG runtime base = Ghidra base = `0x08009000`; app code page A at runtime =
static `A-0x9000`. Low-RAM HAL (`0x080000xx`–`0x08008xxx`) is `nandboot.bin` relocated
into RAM; its static twin is `0x07ffxxxx` (nandboot mapped at `0x07ff8000`), e.g. runtime
`0x08007740` = static `0x07ffe740`. Status tags: **[Proven]** = read from decomp/disasm;
**[Inferred]** = deduced from register wiring.

> ### Highlights (all Proven)
> 1. **`0x0400000c` is a per-module clock-gate, NOT a watchdog.** The mask ROM's
>    `*0x0400000c = 0x63` is the initial gate mask, not a WDT disable. **There is no
>    watchdog anywhere** in mask ROM, nandboot, or PROG — nothing to kick.
> 2. **`0x04000034` is the top-level interrupt-ENABLE register**; `irq_mask_push/pop`
>    save/zero/restore `0x34` as a nested IRQ critical section.
> 3. **`0x040000e0/e4/f0/f4` are GPIO-interrupt enable/polarity banks**, not an audio
>    interrupt controller. The **audio DMA-done IRQ is top-level line 0** (`0x34/0xcc`
>    bit0).
> 4. **The apparent "clock self-test" at boot is a battery-voltage ADC check**
>    (`power_battery_check`, runtime `0x08038da8`); HAL `0x07ffe740` (= runtime
>    `0x08007740`) is the **generic `gpio_get_in(pin)`**, not a timer probe.
> 5. **Boot mode is software-decoded from GPIO straps**, not a hardware-latched register.
>    Straps = `0x040000bc` bits 13/12/9; `0x04000054` is a plain software scratch tag.

---

## 1. Register map (offset → name → r/w → meaning)

| off | name | r/w | meaning | key funcs |
|---|---|---|---|---|
| 0x00 | `REG_CHIP_ID` | R (ROM: W cfg) | Chip-ID gate. Reads ASCII **"1090"** = `0x30393031`. `app_init_main` boots only on match. | `app_init_main` 0x08038f5c; `anyka_chipid_check` 0x081150c4 / `_dbg` 0x080fd53c |
| 0x04 | `REG_CLK_DIV` | R/W | PLL + system-clock divider + power/sleep. bits[5:0]=PLL M (MHz=4·M+180); [8:6]=div A (÷2^A, latch bit14); [25:22]=div B (÷(B+1), latch bit21); bit15=VBAT-ADC mode; bits[14:12]|=0x7000 + poll bit13=sleep/clock-stop req (bit13 busy, self-clears on wake). | `clk_set_index` 0x08009134; `clk_get_pll_mhz` 0x080090dc; `enter_standby` 0x0800927c |
| 0x08 | `REG_CLK_AUDIO` | R/W | Audio/DAC clock. bit24=DAC-clk en; [20:13]=rate divider; bit21=latch; bit12+23=codec-type-3 path; [11:4]=ADC-path div; bit25=mic/ADC-in en; bit29 pulsed at standby. | `dac_set_rate` 0x08033258; `dac_bringup` 0x08033334 |
| 0x0c | `REG_CLK_GATE` | R/W | **Per-module clock-gate** (bit=1 → module OFF). bit2=NAND/NFC, bit4=USB-detect, bit5=DAC/audio, bit6=analog/ADC, bit20=USB-controller. Mask-ROM init `0x63`. **Not a watchdog.** | `hal_clkgate_set` 0x080077d0 |
| 0x10 | `REG_ANALOG_PD` | R/W | Analog power-down. bit9: 1=DAC powered down (cleared in `dac_bringup`). | `dac_bringup`/`FUN_080333fc` |
| 0x18 | `REG_TIMER1_CTRL` | R/W | Timer1: bits[25:0]=reload (**240000 = 20 ms @ 12 MHz**); bit26=load, bit27=enable, bit28=IRQ-ack (write per tick). | `hal_timer_register` 0x0800780c; `irq_aggregate` 0x07ffd534 |
| 0x34 | `REG_INT_ENABLE` | R/W | **Top-level IRQ enable.** bit0=audio-DMA-done, bit6=USB, bit10=timer block. Written 0 = mask all (reset stub, `irq_mask_push`, `soft_reboot`). | `irq_mask_push/pop` 0x0800339c/0x080033e4; `hal_audio_irq_enable` 0x08003430 |
| 0x38 | `REG_INT_ENABLE2`? | W | Only ever written 0 (with 0x34/0x4c/0xcc in reset paths). Possibly a 2nd enable bank. [Inferred] | `soft_reboot` 0x08051d60 |
| 0x3c | `REG_WAKE_POLARITY`? | R/W | Wake-source polarity/level. bit9 = USB-cable source (`usb_present_query` sets when cable absent; state handlers `|=0x200` after `gpio_read(8)==1`). Also per-PAD analog cfg bit9 on USB attach. [Inferred] | `usb_present_query`; `FUN_080f32cc` |
| 0x40 | `REG_WAKE_STATUS`? | R/W | Wake status; `=0xffffffff` then `0` = clear-all in wake-arm. | `wake_sources_arm` 0x080f32cc |
| 0x44 | `REG_WAKE_ENABLE` | R/W | Wake-source enable mask (compact pin ids). bit9 = USB-detect source. | `wake_sources_arm` 0x080f32cc |
| 0x4c | `REG_TIMER_STAT_CTRL` | R/W | Low bits = enables: bit1=sw-timer tick, bit4=GPIO-int scan enable. High = status (read by `irq_aggregate`): bit17 (0x20000)=timer1 fired → run sw-timer table; else bit20 (0x100000)=GPIO-int cause. | `timer_subsystem_init`; `irq_aggregate` 0x07ffd534 |
| 0x50 | analog/pad ctrl | R/W | `clock_periph_init` `|=0x280000`; pulsed bit3 with 0x58 bit17 (analog-block enable). | `analog_block_enable` 0x080f3c58 |
| 0x54 | `REG_BOOT_TAG` | R/W (ROM only) | **Boot-source scratch tag** written by mask ROM (`0x05/0x04/0x06/0x01/0x02` ×0x1000000 = SPI/NAND/Massboot/Usbboot/UART; `0xAA000000` = NAND read error). **Absent from all PROG/nandboot decomp.** Not hardware-latched. | mask ROM only |
| 0x58 | `REG_ANALOG_PWR` | R/W | Analog/power control (multi-field). bit2(+4)=USB-detect/PHY en; bit5=USB-PLL powerup; bits[13:11]=mic/analog input select; bit16=inv power-down; bit17=analog-block en; **bits[26:24]=OID sensor power** (`&0xF9FFFFFF|0x1000000`); bits[30:28]=ADC channel select. | `oid_sensor_enable` 0x080ef114; `usb_detect_2`; ADC/mic paths 0x080f3ba8/3cd4 |
| 0x5c | `REG_CODEC_BIAS` | R/W | Codec analog bias/ramp (bits[15:13], [22:21], composite `0xE1190C1F`); bit30 set in USB-PLL powerup. | 0x0800927c; codec init |
| 0x60 | `REG_ADC_CHSEL2` | R/W | Second ADC channel select (bits[30:28]). | `adc_chan_select_60` 0x080f3d2c |
| 0x64 | `REG_ADC_CTRL` | R/W | **bit9=battery-ADC enable**, bit10=aux-ADC enable, bit13=DAC enable, bits[16:14]=DAC OSR. | `adc_read_vbat` 0x0800b3c8; `dac_set_rate`; `adc_read_aux` 0x08030c40 |
| 0x68 | `REG_CODEC_VOL` | R/W | Codec volume (bits[9:6], [12:10]) + path enables (`0x600003`/`0x800000`/`0x1000000`). | codec paths 0x080f2f1c/3020 |
| 0x70 | `REG_ADC_DATA` | R | **ADC result (read-only).** bits[19:10]=battery channel (10-bit), bits[9:0]=aux channel. | `adc_read_vbat` 0x0800b3c8; `power_battery_check` 0x08038da8 |
| 0x74 | `REG_PIN_SHARE` | R/W | Pinmux/pin-share (RMW). bit0=UART0 console pins; NAND/SPI/strap pin routing. Values never read back → RAM-like. | `hal_pinshare_cfg` 0x08007768; `clock_periph_init` 0x08039628 |
| 0x7c | `REG_GPIO_DIR` (bank0) | R/W | GPIO direction, pins 0–31. **bit=1 → input**; HAL arg is inverted (arg 1 = output = bit clear). Bank1 = 0x84. | `hal_gpio_set_dir` 0x0800774c |
| 0x80 | `REG_GPIO_OUT` (bank0) | R/W | GPIO output latch, pins 0–31 (bank1 = 0x88). | `hal_gpio_write` 0x08007734 |
| 0x88 | GPIO_OUT bank1 | R/W | output latch pins 32+ | — |
| 0x9c | `REG_GPIO_PULL` (bank0) | R/W | GPIO pull enable, pins 0–31 (1=pull on; HAL arg inverted). Bank1 = 0xa0. | `gpio_pull_cfg` 0x080f3268 |
| 0xbc | `REG_GPIO_IN` (bank0) | R | GPIO input level, pins 0–31 (bank1 = 0xc0). **Boot straps + USB-detect (bit8) + OID data (bit9).** | `hal_gpio_read` 0x08007740 |
| 0xc0 | GPIO_IN bank1 | R | input level pins 32+ (scanned by `irq_aggregate`) | — |
| 0xcc | `REG_INT_PENDING` | R, RMW-clear | Top-level IRQ pending; same bit layout as 0x34. bit0=audio, bit6=USB, bit10=timer. FW clears by RMW / by acking the source. | `irq_vector_handler` 0x08000110; `irq_aggregate` 0x07ffd534 |
| 0xe0 | `REG_GPIO_INT_EN` (bank0) | R/W | GPIO per-pin interrupt enable, pins 0–31 (bank1 = 0xe4). | `hal_gpio_int_enable` 0x0800775c |
| 0xf0 | `REG_GPIO_INT_POL` (bank0) | R/W | GPIO interrupt polarity, pins 0–31 (bank1 = 0xf4). **bit set = trigger when pin reads LOW.** | `hal_gpio_int_arm` 0x08003c38 |

---

## 2. GPIO pin map

All GPIO ops go through one dispatcher **`hal_gpio_op(pin, op, val)`** (static
`0x08007944` op-table): dir=`0x7c/0x84`, int-en=`0xe0/0xe4`, int-pol=`0xf0/0xf4`,
data-in=`0xbc/0xc0`, data-out=`0x80/0x88`, pull=`0x9c/0xa0` (bank0 for pins 0–31, bank1
for 32+). `gpio_pin_lookup` (0x0800af74/0x080f3278) maps logical channels → physical pins;
channels 0xb/0xc/0xd/0xf/0x10 in the (disconnected) I²C block resolve to `0xff` = no-pin
no-op.

Boot direction/values are authoritative from `clock_periph_init` (0x08039628).

| pin | dir@boot | val | meaning | evidence | status |
|---|---|---|---|---|---|
| 0 | in | — | battery-sense aux (logged) | `battery_get_level` 0x0804bf18 | Proven read / Inferred role |
| 1 | in | — | **battery-OK comparator B (must=1)** | `battery_get_level`: OK iff pin11 && pin1 | Proven read / Inferred role |
| **2** | out | — | **OID sensor CLOCK** (`0x80` bit2) | `akoid_poll_wrapper` 0x080eef54 | Proven |
| 5 | out | 1 | gauge/charger-IC data (bidir; ACK read) | 0x080abd14, `event_pump_ring` idles it | Proven ops / Inferred device |
| 6 | out(1s) | — | shutdown/mute strobe (high 1 s before power-off) | `power_off_request` 0x080abbc8 | Proven ops / Inferred role |
| 7 | in | — | **headphone detect (1 = plugged → amp off)** | `audio_wait_tick` 0x0800b2ec | Proven |
| **8** | in,pull | — | **USB/charger detect (1 = present)** | `usb_connect_handler` 0x08050d28; ~40 handlers gate OID on ==0 | Proven |
| **9** | in | 1 | **OID sensor DATA** (`0xbc` bit9; idle 1) | shifter + `hal_gpio_read(9)` | Proven |
| 10 | out | 0 | gauge/charger-IC clock | 0x080abcac bit-bang | Proven ops / Inferred device |
| 11 | in | — | **battery-OK comparator A** → `ctx[0x24]=1` | `app_init_main`, `battery_get_level` | Proven read |
| 12 | out | 0 | audio mute / pop-suppression (pulsed 0→1 in USB reinit) | 0x08050898, 0x0803cc80 | Proven ops / Inferred role |
| 13 | out | 1 | unknown enable (Hi-Z during flash program) | 0x08039628, 0x0803cc80 | Proven ops |
| **15** | out | **1** | **POWER LATCH (1=stay on, 0=power off)** | boot sets 1; `sys_reset` writes 0 | Proven |
| **16** | out | 0 | **speaker AMP enable (1=on)** = "GPIO 0x10" | `audio_amp_enable`/`_disable` 0x080abb0c/0x080ab3ec | Proven |
| 0xff | — | — | power-button candidate (`sys_reset` polls `gpio_read(0xff)` for supply death) | `sys_reset` 0x080508f8 | Inferred |

Notes: **there are no volume or cover GPIOs** — volume/cover/power-off are all delivered as
**OID codes**, not pins. Battery level = the pin 0/1/11 comparators + the ADC (`0x64` bit9
enable, `0x70` bits[19:10] data). "Amp enable = GPIO bit 0x10" from the HW inventory = GPIO
**pin 16** (0x10), register `0x80` bit16.

### USB-detect (state 8)
GPIO **pin 8** (`0xbc` bit8): **1 = USB/PC present, 0 = standalone**. With bit8=1,
`usb_connect_handler` (state 8) leaves for the USB-PC sub-machine (state 5) and never
reaches the book path; with bit8=0 it detect-outs (event `0x100C`) to STANDBY (state 3) →
the real book path.

---

## 3. Clock / PLL / battery-check details

**Chip-ID gate.** `app_init_main` requires `*0x04000000 == 0x30393031` ("1090") before it
installs the statechart tables / starts the event pump. The MtdLib/FatLib self-ID checks
(`anyka_chipid_check` 0x081150c4 / `_dbg` 0x080fd53c) use a case table keyed on a
`get_chip_type` callback; **case 5 = "1090" @0x04000000** is this chip (snowbird2). On
mismatch they zero the storage/FatLib ops tables ("your ic isn't anyka ic!").

**PLL / sysclk.** `clk_get_pll_mhz` = `(0x04000004 & 0x3f)*4 + 180`. Live pen M=13 →
**232 MHz**. sysclk = PLL / 2^A / (B+1), A=bits[8:6] (latch bit14), B=bits[25:22] (latch
bit21); both latches self-clear and are polled. The runtime table (`0x08007974`) packs
{M,B,A} for indices 0..10 → 116/58/38.7/29/19.3/14.5/9.67/7.25/3.625/1.8125/96 MHz; the pen
runs index 0. Runtime M-change is refused (`hang_forever`).

**"Clock self-test" = battery check.** `power_battery_check` (0x08038da8):
early-returns if `gpio_get_in(8)!=0` (USB present); else runs three
ADC phases against threshold `0x155` (341): phase 1 @14.5 MHz (10 doubled reads), phase 2
@58 MHz with `0x04000004` bit15 set (1000 raw reads), then a cross-mode consistency test.
Any failure → `fatal_low_battery_blink` (0x08038d24, blink LED 6× and halt). A raw battery
reading of `0x200` (512 ≥ 341) at `0x04000070` passes all three phases.

**Standby handshake.** `enter_standby` (0x0800927c): `irq_mask_push` → arm wake sources
(`0x3c/0x40/0x44`) → `0x58 |=0x20`, `0x5c |=bit30` → **`0x04000004 |= 0x7000`; spin until
`0x04000004 & 0x2000` (bit13) clears** (bit13 auto-clears on wake).

---

## 4. Interrupt controller / timer / boot & reset

**Interrupt controller.** `irq_vector_handler` (static 0x08000110):
`r9 = INT_PENDING(0xcc) & INT_ENABLE(0x34)`, then dispatch by bit:
- **bit10 (0x400)** → `irq_aggregate` (0x07ffd534) = **timer block**. Reads `0x4c` status:
  bit17 (0x20000)=timer1 fired → run the 6-slot sw-timer table @`0x0800895c` (system tick
  `*0x08008d24`, OID poll cadence, GME timers); else bit20 (0x100000)=GPIO-int scan cause.
- **bit0 (0x001)** → audio **DMA-done** handler (0x080039d4): clears `0x04010000` bit16
  (the DMA kick/busy bit) and posts the recorded audio event (id @0x08007e78). The audio
  interrupt is this top-level line 0, not a GPIO-int bank line.
- **bit6 (0x040)** → USB (PROG 0x08035b24).

**GPIO interrupts are timer-polled, not edge hardware.** While `0x4c` bit4 is set,
`irq_aggregate` scans each enabled pin (`0xe0/e4`): if `data-in(0xbc/c0) != polarity
(0xf0/f4)` expectation it dispatches `gpio_int_dispatch` (0x08005500). Polarity bit set =
trigger-when-LOW. Armed via `hal_gpio_int_arm` (0x08003c38, writes inverted level), e.g.
`usb_present_query` arms pin 8 = wake on cable change.

**Timer1 (`0x18`).** `hal_timer_register` (0x0800780c) on first use writes
`240000 | 0xc000000` = reload 240000 (20 ms @ 12 MHz) + load + enable; each tick the ISR
writes `|= 0x10000000` (bit28 ack). Registrations divide their ms period by 20.

**Boot mode (mask ROM).** Straps = GPIO input `0x040000bc` bits 13/12/9 (pull-ups via
`0x0400007c |= 0x3000`, pin cfg `0x04000074 &= ~1`), sampled 5×. If any sample has
`(v & 0x3200) == 0x3200` (bits 13,12,9 all high) → **normal storage boot** (return
immediately). Else 5×-low per pin selects: bit13→Massboot (tag 0x06), bit12→Usbboot (0x01),
bit9→UART (0x02). The mode is stored as a software tag in `0x04000054[31:24]`
(SPI 0x05 → NAND 0x04 → Massboot 0x06 fallback; NAND read error 0xAA). **The ROM never
touches `0x04000000` and performs no PLL writes in this window on the NAND path**;
`0x04036000` in this window is the UART, not a clock register.

**Watchdog `0x0400000c`.** Mask ROM writes `0x63` once at reset — the **clock-gate init
mask**, not a WDT. No kick loop exists. `hal_clkgate_set(module,on)`: on=1 clears the bit
(module ON), on=0 sets it; module 9 = whole-value `0x77`/`0x7f`.

**Reset / power-off.** No software-reset register. `sys_reset` (0x080508f8) = **power-off**:
teardown, then `gpio_write(15, 0)` (drop the power latch), then poll `gpio_read(0xff)` for
supply death. `soft_reboot` (0x08051d60) zeroes `0x4c/0xcc/0x38/0x34`, switches CPU clock,
halts.

---

## 5. Register behaviours that matter for a faithful boot

The relevant dynamic behaviours in this window, all derived from the findings above:

- `0x04000000` reads the constant chip-id `0x30393031` ("1090"); `app_init_main` boots only
  on match.
- `0x04000004` self-clearing latch/busy bits (13/14/21) read back 0; the standby handshake
  spins on bit13 until a wake source clears it.
- `0x04000070` is the ADC data register; a raw battery reading ≥ 341 (0x155) passes all three
  phases of `power_battery_check`.
- `0x040000bc`: **bit8 = USB present** (1 = USB/PC → USB sub-machine; 0 = book path), **bit9 =
  OID data** (driven by the OID shifter, idle 1), **bits 0/1/11 = battery-OK comparators**,
  bit7 = headphone detect, straps 13/12/9 = boot mode.
- `0x04000034/cc` are the top-level IRQ enable/pending pair; a CPU IRQ is asserted when
  `(0xcc & 0x34) != 0`.
- Timer1 (`0x18`) raises `0xcc` bit10 + `0x4c` bit17 every 20 ms once bits 26/27 are set, and
  is acked by writing `0x18` bit28. This drives the sw-timer table, system tick
  `*0x08008d24`, OID poll cadence and GME timers.
- The audio DMA-done IRQ is top-level line 0 (`0xcc` bit0, gated by `0x34` bit0); its handler
  clears `0x04010000` bit16.
- `0x040000e0/e4/f0/f4` (GPIO-int enable/polarity) power-on to 0 — a non-zero default would
  make `irq_aggregate`'s GPIO scan treat every pin as an enabled active-low interrupt.

(These behaviours were confirmed dynamically; the emulator that does so lives separately in
[tt-emu](https://github.com/nomeata/tt-emu).)

---

## 6. Proposed names / docstrings (for names.csv & ghidra_types.h)

| addr (runtime) | name | docstring |
|---|---|---|
| 0x08000110 | `irq_vector_handler` | IRQ entry: `pending(0xcc) & enable(0x34)`; line10→timer, line0→audio-DMA, line6→USB. |
| 0x08007944 | `hal_gpio_op` | `(pin, op, val)`; op-table dir/int-en/int-pol/data-in/data-out/pull, 2 banks. |
| 0x08007734 | `hal_gpio_write` | write output latch `0x80/0x88` pin bit. |
| 0x08007740 | `hal_gpio_read` | read input level `0xbc/0xc0` pin bit (== HAL 0x07ffe740). |
| 0x0800774c | `hal_gpio_set_dir` | `(pin, dir)`; dir arg inverted (1=output=bit clear). |
| 0x0800775c | `hal_gpio_int_enable` | per-pin GPIO int enable `0xe0/0xe4`. |
| 0x08007768 | `hal_pinshare_cfg` | pinmux `0x04000074` (NAND `&~0xBA`, USB `&~0x198|0x110`). |
| 0x080077d0 | `hal_clkgate_set` | `(module, on)` on `0x0400000c`; on=1 clears bit (ON); module9 = whole 0x77/0x7f. |
| 0x0800339c / 0x080033e4 | `irq_mask_push` / `irq_mask_pop` | save/zero / restore `INT_ENABLE 0x34`; 4-deep stack @0x08008c94. |
| 0x08003430 | `hal_audio_irq_enable` | set/clear `INT_ENABLE` bit0 (audio DMA-done, line 0). |
| 0x080039d4 | `hal_audio_irq_handler` | clear DMA kick `0x04010000` bit16; post audio event (id @0x08007e78). |
| 0x08003c38 | `hal_gpio_int_arm` | `(pin, level)` → inverted level into polarity `0xf0/0xf4`. |
| 0x08005500 | `gpio_int_dispatch` | per-pin GPIO-int callback dispatch (from `irq_aggregate`). |
| 0x07ffd534 | `irq_aggregate` | timer/GPIO 2nd-level: `0x4c` status → sw-timer table / GPIO scan. |
| 0x0800780c | `hal_timer_register` | `(class, ms, periodic, cb)`; ms/20 ticks; arms TIMER1 `0x18 = 240000\|0xc000000`. |
| 0x080090dc | `clk_get_pll_mhz` | `(0x04000004 & 0x3f)*4 + 180`. |
| 0x08009134 | `clk_set_index` | table-driven sysclk switch; latches bits 14/21; hangs on PLL-M change. |
| 0x0800b0f8 / 0x0800b138 | `clk_boost_max` / `clk_boost_release` | refcounted clock boost around decode. |
| 0x0800b464 / 0x0800b490 | `adc_vbat_mode_set` / `_get` | `0x04000004` bit15. |
| 0x0800b3c8 | `adc_read_vbat` | avg-of-4 ×2; `0x64` bit9 + `0x70[19:10]`. |
| 0x08030c40 | `adc_read_aux` | `0x64` bit10 + `0x70[9:0]`. |
| 0x08038da8 | `power_battery_check` | 3-phase ADC vs 0x155; USB early-out via `gpio_read(8)`; fail → `fatal_low_battery_blink`. |
| 0x08038d24 | `fatal_low_battery_blink` | blink LED 6×, halt. |
| 0x080fd53c / 0x081150c4 | `anyka_chipid_check_dbg` / `anyka_chipid_check` | case 5 = "1090" @0x04000000. |
| 0x08033258 / 0x08033334 | `dac_set_rate` / `dac_bringup` | `0x08`/`0x64` divider+enable. |
| 0x080ef114 | `oid_sensor_enable` | `0x58 &= 0xF9FFFFFF; |= 0x1000000` (bits[26:24] OID power). |
| 0x0800927c (+thunk 0x080f3de8) | `enter_standby` | arm wake, power down analog, `0x04 |= 0x7000`, spin on bit13, restore. |
| 0x080f32cc / 0x080f33ac | `wake_sources_arm` / `wake_sources_disarm` | program `0x3c/0x40/0x44` around standby. |
| 0x080508f8 | `sys_reset` | power-off: teardown, `gpio_write(15,0)`, poll `gpio_read(0xff)`. |
| 0x08051d60 | `soft_reboot` | zero `0x4c/0xcc/0x38/0x34`, switch CPU clock, halt. |
| 0x080413fc / 0x08041ce4 | `usb_detect_disable` / `usb_cable_probe` | IRQ6/audio-irq off, USB power off / cable classify via PHY `0x0407000a`. |
| 0x080abb0c / 0x080ab3ec | `audio_amp_enable` / `_disable` | GPIO pin 16 (`0x80` bit16). |
| 0x080abbc8 | `power_off_request` | GPIO6 high 1 s, then `gpio_write(15,0)`. |

---

## Open items
- `0x04000038` (write-0 only) and the exact `0x3c` wake-polarity semantics (W1C latch vs
  level) are [Inferred]; confirm at runtime.
- GPIO pins 5/10/13 device attributions (gauge/charger IC) are Proven-ops / Inferred-device.
- Pin `0xff` as the power button is Inferred from `sys_reset`'s poll.
