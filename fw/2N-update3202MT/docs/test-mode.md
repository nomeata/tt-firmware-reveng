# Test mode (factory PROD-TEST) — wiki correspondence, entry gate, test sequence, and the volume buttons

> Answers: what is the tip-toi-reveng wiki's "Test mode", which code implements it in THIS
> firmware (2N update3202MT), how the volume buttons physically work, and how the factory
> prod-test is entered. Static analysis of the named decompilation (PROG base **0x08009000**) +
> the `nandboot` blob (flat at 0x08000000) + PROG pool bytes. Tags: **[P]** = read from
> decomp/disasm/bytes, **[I]** = inferred (reason given), **[W]** = quoted from the
> [tip-toi-reveng wiki](https://github.com/entropia/tip-toi-reveng/wiki).
> Companions: `power-on-sound.md` §5, `pmu-power-management.md`, `settings-config.md` §3,
> `system-voice-feedback.md`, `statechart-full-map.md`, `oid-sensor-read-protocol.md`.

---

## 0. Verdict (one paragraph)

**The wiki's "Test mode" IS this firmware's factory PROD-TEST splash branch — and the mystery
"GPIO11 && GPIO1 battery comparators" gate is actually the wiki's entry gesture read directly
from the buttons: GPIO11 = the POWER button, GPIO1 = the VOLUME-DOWN button, GPIO0 = the
VOLUME-UP button.** [P — nandboot key scanner `hal_key_read` 0x08006B2C reads exactly these
three pins and produces the 0x105F key codes 8/6/5]. Hold volume-down while pressing power-on
(the wiki gesture) ⇒ `battery_get_level` 0x0804BF18 (`GPIO11==1 && GPIO1==1`) passes 4× in
`splash_entry` ⇒ `akoid_buf[0xb3]=1` ⇒ the pen announces test mode (voice 0x1B), dumps + verifies
the NAND **PROG** image checksum ("start test of program" → "program test pass/fail", voices
0x1C/0x1E/0x1D), writes `B:/Prodtest.txt`, and parks at splash(1). There, **volume-up ('+')
presses step through the wiki's tests**: ① OID-tip communication test (0x1F→0x20/0x21),
② auth-chip "encryption" test (0x22→0x23/0x24), ③ play `B:/TestTone/Test1KHZ.wav`; volume-down
runs the `B:/TestFile/Test1..6.bin` checksum cascade (0x25→0x26/0x27); any OID tap is read out
in digits; long-press power exits via the normal power-off. Every element of the wiki
description is byte-matched below.

---

## 1. The wiki's Test mode [W]

Source: **https://github.com/entropia/tip-toi-reveng/wiki/PEN-Firmware**, section "Test mode"
(fetched 2026-07-06). Quotes:

> "**Hold the volume-down button while pressing the power-on button** to enter this mode (with
> the new Player pen you might want to hold the skip-backwards button while pressing the
> power-on button to do so). The pen must not be connected via USB!"

> "When entering that mode you first hear (in chinese) '**Testmode**' ('Toshe mo she'),
> '**Start test of program**' ('Cashe toshe ton tjio') followed by '**Program test pass**'
> ('ton tjio toshe dchin tao') or '**Program test fail**' ('ton tjio toshe she by')."

> "You can now run some tests by pressing '**+**'. Each press leads to the next test (in the
> order described here) being started"

The per-press tests, as the wiki lists them:

1. **Tip test** — "Start test of the tip of the pen" → "Test of the tip of the pen passed" /
   "Communication to the tip of the pen failed".
2. **Encryption test** — "Start test of encryption procedure"; the pen is unresponsive for a
   while; → "Encryption procedure test passed/failed". (Wiki guesses it "possibly uses the
   `TestFile` directory" — wrong for this firmware; it is the anti-clone **auth chip**, §3.3.)
3. **Audio test** — plays `TestTone/Test1KHz.wav` (wiki: three times).
4. **End** — "Nothing happens any more".

The wiki also says the mode produces a **`Prodtest.txt`** results file with checksums for the
firmware and six test binaries, and references a `TestFile/` directory.

**Generation caveat**: the wiki page covers several pen generations and does not pin this
section to one; the Chinese phrases and `Prodtest.txt` details match the Anyka-lineage pens.
Everything below shows this update3202MT firmware implements the same mode with only small
deltas (tone played once per press, not 3×; results file starts with a text line, not byte
0x01; the volume-DOWN button additionally triggers the TestFile cascade). [P/I]

---

## 2. THE key discovery: the three buttons are GPIO 11 / 0 / 1

### 2.1 The nandboot key scanner [P — disasm]

`hal_key_read` **0x08006B2C** (nandboot, ARM):

```
if (akoid_buf[0x57] != 0) {          // boot latch: power button still held since power-on
    if (gpio_read(11) == 0) akoid_buf[0x57] = 0;   // wait for power release
    return 0xFF;                     // all keys ignored until then
}
if (gpio_read(11) == 1) return 8;    // POWER button      (active HIGH)
if (gpio_read(0)  == 0) return 5;    // VOLUME-UP  ('+')  (active LOW)
if (gpio_read(1)  == 1) return 6;    // VOLUME-DOWN ('−') (active HIGH)
return 0xFF;                         // no key
```

- **Idle levels** (nothing pressed): GPIO11=0, GPIO0=1, GPIO1=0. Note volume-up is
  **active-low**. [P]
- The boot latch `akoid_buf[0x57]` is set to **1** by the boot task **0x08039100** immediately
  after `app_init_main` returns (`strb` @0x08039154) — so the press that powered the pen on
  can never fire a key event; scanning begins after the power button is first released. [P]

### 2.2 Scan → debounce → event chain [P — disasm]

Armed by `gpio_config_unpack` 0x08039834 (← `app_init_main` 0x08038F5C):
`hal_timer_register(0, 0x78, periodic, 0x080058F0)` (pool 0x08039938) — a **~120-unit
(~120 ms) periodic key scan**. Chain (all nandboot):

| fn | role |
|---|---|
| `0x080058F0` | scan: `hal_key_read`; key ≠ 0xFF → store last-key @0x08007E70, one-shot 20-unit debounce timer → `0x08005954` |
| `0x08005954` | debounce: re-read; same key → 120-unit hold-tracker timer `0x08005998`, else back to scan |
| `0x08005998` | hold tracker, count @0x08008C1C (u16), state @0x08008C1A: key still down at count==5 (~600 ms) → **post {code, sub=1}** (HOLD); count>5 and code∈{5,6} every 2nd tick (codes 2/3/5/6) → **post {code, sub=3}** (REPEAT); key released → **post {last_code, sub=state}** where state is **0 if no hold event fired** (⇒ a short press posts **sub=0 on release**) else 2; then reset + re-arm scan |
| `0x08006BFC` | the poster: if hook `*0x08008C5C` set → callback, else **`hal_event_post(hal_event_id_for(1) = 0x105F, {code, sub}, dedup=(sub∈{1,2}))`** |

So the three buttons arrive in the statechart as **event 0x105F with payload bytes
{code, sub}**: code 5 = vol+, 6 = vol−, 8 = power; sub 0 = short press (on release),
1 = held ~600 ms, 3 = auto-repeat while held (volume keys only, ~every 240 ms), 2 = release
after a hold. Timing values assume the 20 ms timer IRQ (gme-timer doc) — [I] for absolute ms,
[P] for the tick counts. **0x105F is NOT only "OID partial"** (statechart map's label): phase 1
of `hal_event_id_for` is the shared button/aux event id.

### 2.3 Corrections to earlier docs [P]

- `pmu-power-management.md` §1.1/§3.1: **GPIO 11 and 1 are not "battery comparators"** — they
  are the power and volume-down buttons. `battery_get_level` 0x0804BF18 (logs
  "`11 LEVEL=%d,0 level=%d.`" — i.e. power and vol-up pins) = "**power AND vol-down both
  pressed**". The pmu §3.5 [Open] "GPIO11 triple role" resolves cleanly: (a) boot gate = the
  test-mode chord; (b) standby GPIO11==1 → `a:` rescan + `soft_reboot` = **pressing the power
  button while idling in standby** re-scans content and reboots. "Retail pens read 11/1 = 0 at
  boot" = "nobody holds the buttons".
- `power-on-sound.md` §5 called the branch "gated on battery comparators sampled ×4" — same
  correction; the gate is the wiki's entry gesture.
- The "physical buttons identification [Inferred]" in pmu §0.5 is now **[Proven]** (scanner
  reads the pins; codes match the dispatch).

---

## 3. Firmware correspondence — entry gate and the full test sequence

Runs entirely in **splash(1)**; its event-action EA[0] = **0x0804CECC**
(`fwupdate_verify_image` — historic name; it is the splash/prod-test/low-batt/off multiplexer).
The prod-test state lives at **0x081DA004**: `+1` = pass counter, `+2` = **stage byte**;
per-file results in `akoid_buf` `+0x15A..0x15F` (exists flags), `+0x160..0x165` (wrong flags),
`+0x168..0x17C` (checksum values), `+0x180` busy flag. [P]

### 3.1 Entry — `splash_entry` 0x0804C1D4 L53–102 [P]

`battery_get_level()` (GPIO11==1 && GPIO1==1) sampled **4×** with delays 5/5/20 ms between
samples (`0x08007938(5/5/0x14)`) — i.e. both buttons must be held through the first ~30 ms of
splash entry (which is well under a second after power-on). All four 1 ⇒

1. `akoid_buf[0xb3] = 1` (prod-test mode flag — also disables the retail button handler, §4.3);
2. arm a 100-unit periodic timer (cb 0x08003994) so heartbeats keep driving the stage machine;
3. `sm_set_sound_profile(2,0)` (volume level 2);
4. open-or-create **`B:/spotlight.bin`** (UTF-16 @0x08121A9C) and call
   `file_checksum_verify(handle, nand_dev@0x08008CC4)` 0x080EE6E4: locates the FHA entry named
   **"PROG"** (string @0x08003394 in nandboot RAM), streams the whole PROG image out of NAND
   **writing it into spotlight.bin**, byte-sums all but the trailing 4 bytes and compares with
   the trailing LE u32; the running sum is left in `*0x081DA000`. Then spotlight.bin is closed
   and **deleted** (transient). Result ⇒ stage := **0x20** (pass) / **0x1F** (fail);
5. play **voice 0x1B** ("Testmode" announcement). [P]

If any sample reads 0 → quiet retail boot (`[0xb3]=0`, →standby). [P]

### 3.2 Automatic phase (no input needed) — stage machine in 0x0804CECC (LAB_0804D158) [P]

On subsequent events (heartbeats), gated on `is_audio_playing()==0` each step:

| stage | action | voice |
|---|---|---|
| 0x1F / 0x20 | announce sequence start | **0x1C** ("start test of program") → stage 1 / 2 |
| 1 (fail) | delete + recreate **`B:/Prodtest.txt`** (@0x08121ABE), write "`The Program File's checksum:0X%X`" (value `*0x081DA000`) | **0x1D** ("program test FAIL") → stage 3 |
| 2 (pass) | same Prodtest.txt write (len 0x28) | **0x1E** ("program test PASS") → stage 4 |

This exactly reproduces the wiki's automatic audio sequence: *Testmode → Start test of program
→ Program test pass/fail*. [P code / W wording]

### 3.3 Button-driven tests — the 0x105F branch of 0x0804CECC [P]

Payloads with **sub ≠ 0** are ignored except `{8, sub=1}` (power long-press) →
`*0x08008C0B = 3` → the shared off-path (voice **0x14**, GPIO15=0) — **the exit**.
Short presses (sub=0):

**code 5 = volume-up = the wiki's '+', press counter `akoid_buf[0x158]`:**

| press | action | voices |
|---|---|---|
| 1 | **tip test**: stage 5→6; clear `OidCaptureState.done_latch` (@0x08008C0F), then ≤6 rounds of `akoid_poll_from_idle()` / `akoid_rearm()` (3 ms gaps) waiting for the latch — it is set only when the sensor answers a 32-bit poll with a **status frame 0x60FFF8/0x60FFF1** (`akoid_poll_status32` → `akoid_sensor_sleep`) | **0x1F** ("start tip test") then **0x20** pass / **0x21** fail ("communication to the tip failed") |
| 2 | **"encryption" test**: stage 7→8; runs `authchip_challenge_gpio5_10` 0x0804C47C (the anti-clone GPIO5/10 bit-bang challenge — this is the wiki's "unresponsive for a while") | **0x22** ("start encryption test") then **0x23** pass / **0x24** fail (soft — no power-off, unlike the retail path) |
| 3 | **audio test**: open **`B:/TestTone/Test1KHZ.wav`** (@0x08121D12), `play_media_setup(h,0,size,3)` = play the whole file once, busy-wait until done | the tone itself |
| ≥4 | nothing (matches the wiki's "Nothing happens any more") | — |

**code 6 = volume-down, press counter `akoid_buf[0x159]`:**

| press | action | voices |
|---|---|---|
| 1 | reset per-file flags, pass-count=0, stage 9 → the **`B:/TestFile/Test1..6.bin`** cascade (stages 9→0xB→0xD→0xF→0x11→0x13→0x15, one file per event): each file byte-summed over size−4 bytes vs trailing LE u32; then stage 0x16 appends 6 result lines to `B:/Prodtest.txt` — "`The TestN.bin File's checksum:0X%X checksum right!/checksum wrong!`" or "`.bin File is not exist!`" → stage 0x17 | **0x25** (stage announce), then **0x26** all-6-pass / **0x27** fail |
| ≥2 | stage := 0x29 → at audio-idle stage 0x2A | **0x2F** (final/summary prompt) |

**code 8 = power, short press:** only counts `akoid_buf[0x157]++` (no consumer found — [P
write / I dead]). Long press (sub=1): exit as above.

**OID taps (event 0x1060) while in test mode:** value < 10 → speak single digit (voice
`n+9`); else store at `akoid_buf+0x64` and run the multi-digit read-out `FUN_0804BF84`
(voices 9..0x12) — i.e. **the pen speaks the number of any code you tap**: a combined
OID+audio check. Repeated system-family taps (≥0x12 within the classifier
`cover_oid_classifier` [0xb3]==1 branch) also force the off-path. [P]

**Dead code**: stages 0x18–0x28 would run a second cascade over
**`C:/TestFile/Test1_sd.bin..Test6_sd.bin`** (SD card) with overall verdict voices
**0x29/0x2A** — but nothing in this build ever sets stage 0x18 (single pool ref to 0x081DA004
is shared by splash_entry/0x0804CECC; neither writes 0x18). The SD-variant of the jig flow is
compiled in but unreachable. [P — pool scan]

### 3.4 Deltas vs the wiki [P/W]

| wiki | this firmware |
|---|---|
| tone played three times | played **once per press** of the 3rd '+' |
| Prodtest.txt "begins with byte 0x01, then checksums" | begins with the text line "`The Program File's checksum:0X…`", then per-file text lines |
| "must not be connected via USB" | no USB gate on the splash branch itself (USB classification only starts from standby, which is never reached) — the caveat is for other generations / the MSC boot path [I] |
| encryption test "possibly uses TestFile" | it is the **auth chip**; TestFile is the separate vol-down cascade |

---

## 4. Volume buttons — complete documentation (doc gap filled)

### 4.1 Physical → event (NEW: §2)

GPIO0 (vol+, active-low), GPIO1 (vol−, active-high), GPIO11 (power, active-high) → nandboot
key scanner (~120 ms period, 20 ms debounce, boot latch `akoid_buf[0x57]`) → **event 0x105F
{code 5/6/8, sub 0/1/2/3}** posted by 0x08006BFC (hookable via `*0x08008C5C`). The buttons do
**not** go through the OID decoder, and there is no keypad MMIO — plain GPIO polling. [P]

### 4.2 Retail dispatch — `button_dispatch_105f` 0x0800A9C8 (state-9 global TA[4]) [P]

Only when **`akoid_buf[0xb3]==0`** (retail mode; in prod-test it returns without consuming so
the splash EA sees the event):

- code 5, sub 0 → `volume_up` 0x0800B6B4: `if (idx<5) idx++`;
- code 6, sub 0 → `volume_down` 0x0800B6E0: `if (idx>0) idx--`;
- feedback (when idle enough): stop audio, **voice 0x15** (blip) or **0x16** (limit hit:
  idx==5 after up / ==0 after down); `subctx+0x54=1` keeps repeats beeping;
- code 8, sub **1** (held ~600 ms), `game_ctx[0x1d]`∈{2,4} → **voice 0x14** → 0x1062 →
  power-off. A short power tap does nothing in retail mode.

### 4.3 Level storage / gain [P — settings-config.md §3, confirmed]

Index u8 @**0x081DB904** (0..5 user, 6 = GME-only mute), **RAM-only, boot default 3**
(prod-test splash sets 2). Commit via `sm_set_sound_profile(level,mode)` 0x0800B65C → gain
table @0x0800B9E4 `{0x18,0x60,0xA8,0x108,0x138,0x180,0}` → `FUN_0802265C` → audio-ring Q10
multiplier (ring obj 0x08008D30+0x14, 0x400=unity). GME opcodes 0xFEE0–0xFEE7 set profiles
0..7 from scripts. No persistence.

### 4.4 Is a volume combo the test-mode entry? **Yes.**

Power + volume-down held at power-on (read as raw GPIO levels by `splash_entry`, not via the
key-event path) = the wiki gesture = the prod-test gate. Volume-up alone / volume-down alone
at boot do nothing special (the gate needs GPIO11 **and** GPIO1). [P]

---

## 5. The test sequence — how the mode runs

All of test mode is driven by authentic surfaces: the button GPIOs (input register 0x040000BC),
the OID two-wire lines, and B: FAT content. [P interface; sequence composed from §2–§3]

### 5.1 Entry gesture

Holding **power + vol-down** at boot (0x040000BC bit 11 = 1 and bit 1 = 1) takes the prod-test
branch. While GPIO11 stays high the boot latch `akoid_buf[0x57]` (set at 0x08039154) mutes the key
scanner; scanning begins once power is released. Releasing power and vol-down promptly together
(within one ~120 ms scan period) avoids the release itself registering as a stray vol-down press.
The four gate samples happen in the first ~30 ms of splash entry, after which the pen enters the
prod-test stage machine and announces voice 0x1B.

### 5.2 What each button does

A button press = the corresponding GPIO level asserted for ~2 scan periods + debounce (< the
~600 ms hold threshold), posted on release:

- **vol+ press #1 → tip (OID sensor) test.** The firmware runs the 32-bit OID polls
  (`akoid_poll_trigger` → 100 ms GPIO2-high pulse → attention/handshake) and passes when it reads
  a **status frame** (code word 0x60FFF8, serial frame `(0x60FFF8<<9)|0x100|0xF0`;
  oid-sensor-read-protocol.md §7) and the sleep commands 0xA0,0xAC,0xA6 are honoured. No response
  ×6 → voice 0x21 (fail); the mode continues either way.
- **vol+ press #2 → auth-chip challenge** on GPIO5/10 (anticlone-auth-check.md): pass ⇒ 0x23,
  mismatch ⇒ 0x24 (soft fail, keeps running).
- **vol+ press #3 → whole-file playback** of `B:/TestTone/Test1KHZ.wav` (a DAC/ring exercise).
- **vol− press #1 → TestFile cascade**: checksum-verifies `B:/TestFile/Test1..6.bin` (trailing
  LE u32 = byte-sum of the rest), writes result lines to `B:/Prodtest.txt`, voices 0x26/0x27.
- **Tap any OID** → digit read-out of the code number (oid-digit-readout.md).
- **power held ≥ ~700 ms** → voice 0x14 → GPIO15=0 (power off).

### 5.3 Observable behaviour

| observable | behaviour |
|---|---|
| voice IDs requested (fwl_play_voice_by_id) | **0x1B, 0x1C, then 0x1E** (0x1D if PROG has no valid trailing sum — see note), and per test: 0x1F+0x20/0x21, 0x22+0x23/0x24, 0x25+0x26/0x27, 0x2F, digits 0x09–0x12, exit 0x14 |
| `B:/Prodtest.txt` | line 1 "`The Program File's checksum:0X%X`"; after vol−: 6 "`The TestN.bin File's checksum:0X… checksum right!`" lines |
| `B:/spotlight.bin` | created, filled with the PROG NAND image, then deleted |
| state | parked in splash(1), `akoid_buf[0xb3]==1`, never reaches standby |
| UART debug | "`Test file2 ok`" during the cascade |

The program-checksum verdict tests the **NAND "PROG" FHA area** trailing byte-sum — on an
authentically-provisioned NAND this is the shipped image's own checksum; verdict 0x1E is emitted
iff the image carries a valid trailing sum (`sum(PROG[:-4]) == LE32(PROG[-4:])`). [P mechanism]

System prompts are only audible if `A:/VOIMG/Chomp_Voice.bin` is present (system-voice-feedback.md);
absent it, the verdict voices are silent no-ops but the sequence is unchanged.

---

## 6. Proposed names / docstrings

| addr | proposed name | docstring sketch |
|---|---|---|
| nb 0x08006B2C | `hal_key_read` | GPIO11=1→8 (power), GPIO0=0→5 (vol+), GPIO1=1→6 (vol−); boot latch akoid_buf[0x57] mutes keys until power released |
| nb 0x080058F0 / 0x08005954 / 0x08005998 | `key_scan_start` / `key_debounce_cb` / `key_hold_cb` | 120-unit scan, 20-unit debounce; sub=0 press(release)/1 hold(5 ticks)/3 repeat(vol)/2 release-after-hold; state @0x08008C1A, last @0x08007E70 |
| nb 0x08006BFC | `key_event_post` | post 0x105F {code,sub}; hook ptr *0x08008C5C |
| 0x0804BF18 | `testmode_chord_gate` (was battery_get_level / battery_comparators_ok) | power && vol-down both pressed (raw GPIO) |
| 0x0804CECC | `splash_prodtest_handler` (keep fwupdate_verify_image alias) | splash EA: retail auth+0x1014 / prod-test stage machine / low-batt continuation / idle USB re-poll |
| 0x080EE6E4 | `prog_area_dump_checksum` (was file_checksum_verify) | stream FHA "PROG" → handle (spotlight.bin), byte-sum vs trailing LE u32; sum → *0x081DA000 |
| 0x0804BF84 | `testmode_speak_digits` | read out akoid_buf+0x64 in digit voices 9..0x12 |
| 0x08039100 | `boot_task_main` | prints serial, calls app_init_main, sets key boot-latch akoid_buf[0x57]=1 |
| globals | | `0x081DA004+2` prod-test stage; `+1` pass count; `0x081DA000` PROG byte-sum; `0x08008C0F` OidCaptureState.done_latch (tip-test flag); `akoid_buf+0x157/158/159` power/vol+/vol− press counters; `akoid_buf+0x57` key boot latch; `0x08008C1A/1C` key state/hold-count; `0x08007E70` last key |

---

## 7. Evidence index

| claim | status | source |
|---|---|---|
| wiki test mode = vol-down + power-on; program test voices; '+' steps tip/encryption/tone; Prodtest.txt | W | github wiki PEN-Firmware "Test mode" (2026-07-06) |
| GPIO11/0/1 = power/vol+/vol− buttons, polarities, codes 8/5/6 | **P** | nandboot disasm 0x08006B2C |
| key scan/debounce/hold chain, sub semantics, 0x105F poster | **P** | nandboot 0x080058F0/0x08005954/0x08005998/0x08006BFC; timer arm: pool 0x08039938 → 0x080058F0 ← `gpio_config_unpack` ← app_init_main |
| boot latch akoid_buf[0x57]=1 after app_init_main | **P** | PROG disasm 0x08039100–0x08039154 |
| gate = 4× (GPIO11 && GPIO1) with 5/5/20 delays; spotlight dump; stage 0x1F/0x20; voice 0x1B | **P** | `0x0804c1d4.c` L53–102 |
| PROG-area dump + trailing-sum verify; "PROG" name; sum @0x081DA000 | **P** | `0x080ee6e4.c`; pools 0x080EE9D4→0x08003394 ("PROG"), 0x080EE9D8→0x081DA000; nandboot bytes @0x3394 |
| stage machine: 0x1C→0x1D/0x1E + Prodtest.txt header | **P** | `0x0804cecc.c` L640–760 (LAB_0804D158) |
| '+' presses 1/2/3 = tip test (done_latch, 6 polls) / auth chip / Test1KHZ.wav; ≥4 nothing | **P** | `0x0804cecc.c` L102–147, L690–715; pools 0x0804D508 → L"B:/TestTone/Test1KHZ.wav", 0x0804D538→0x08008C0F |
| vol− = TestFile/Test1..6.bin cascade + result lines + 0x25/0x26/0x27; 2nd press → 0x2F | **P** | `0x0804cecc.c` L79–99, L150–370; pools 0x0804D53C..0x0804D91C (B:/TestFile/TestN.bin), strings "checksum right!/wrong!/is not exist!" |
| power sub-1 exit via 0x08008C0B=3 → 0x14 → off; short press only counts +0x157 | **P** | `0x0804cecc.c` L64–77, LAB_0804E27C |
| OID tap digit read-out in test mode | **P** | `0x0804cecc.c` L605–637; `0x0804bf84.c` |
| C:/TestFile *_sd.bin stages 0x18–0x28 + voices 0x29/0x2A unreachable | **P** | single pool ref to 0x081DA004 (PROG scan 2026-07-06); no stage-0x18 writer |
| retail dispatch gated on [0xb3]==0; vol acts on sub 0; power needs sub 1 | **P** | `0x0800a9c8.c` L36–155 |
| volume idx/gain/persistence | **P** | settings-config.md §3 (re-verified) |
