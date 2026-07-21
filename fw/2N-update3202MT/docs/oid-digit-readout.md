# OID digit read-out — the prod-test "tap a code, hear its number" feature

> Answers: is the OID-number readout (pen speaks the tapped OID as digits, in Chinese) a
> separate test mode or part of the factory prod-test mode, and the exact firmware path from the
> tap to the digit voices. Static analysis of the named decompilation (PROG base **0x08009000**;
> nandboot RAM copies at 0x07ffdxxx).
> Tags: **[P]** = read from decomp/bytes, **[I]** = inferred (reason given), **[W]** = wiki.
> Companions: `test-mode.md` (the prod-test mode this lives in), `system-voice-feedback.md`
> (Chomp_Voice + digit voices), `oid-sensor-read-protocol.md` (tap → 0x1060),
> `oid-classifier-logic.md` / `cover-tap-first-product-load.md` (retail routing it bypasses).

---

## 0. Verdict

**The OID digit read-out is NOT a separate mode — it is a built-in feature of the factory
PROD-TEST mode** (`test-mode.md`, entered by holding volume-down + power at power-on). There
is no distinct entry gesture, no dedicated state: the *only* code in the whole corpus that
routes a tapped OID into the number-speaker is the **0x1060 branch of the prod-test splash
handler `fwupdate_verify_image` 0x0804CECC** (splash(1) EA[0]), and the two number-speaker
functions **`FUN_0804bf84` (start) / `FUN_0804c090` (resume)** have no other callers. [P —
corpus grep: `FUN_0804bf84`/`FUN_0804c090` referenced only from `0x0804cecc.c`; the
single-digit `(n&0xf)+9` play site likewise]

**The readout is additionally gated on the prod-test stage byte > 5** — i.e. it only unlocks
**after the first '+' (volume-up) press has run the tip test** (stage 5→6; pass or fail both
end at stage 6). Before that (automatic program-test phase, stages 0x1F/0x20→1/2→3/4, busy
flag set) taps are ignored. [P — §3]

**The wiki does not know this feature.** The tip-toi-reveng wiki page
https://github.com/entropia/tip-toi-reveng/wiki/PEN-Firmware, section "Test mode"
(re-fetched verbatim 2026-07-06), describes the entry gesture, the Chinese phrases, the
'+'-stepped tip/encryption/tone tests and the '−' UDISK-file test — but contains **no mention
of OID codes being read out / spoken as numbers** in any mode. A PEN-Hardware wiki page does
not exist (404). This doc therefore *extends* the wiki from decomp evidence. [W/P]

---

## 1. Q1 — separate mode or prod-test? (entry)

Part of prod-test; entry = the prod-test entry, nothing else:

1. **Entry gesture** = hold **volume-down + power** at power-on (`splash_entry` 0x0804C1D4
   samples GPIO11==1 && GPIO1==1 four times in the first ~30 ms of splash) ⇒
   `akoid_buf[0xb3] = 1` (prod-test flag). Full mechanics: `test-mode.md` §2–3. [P]
2. `splash_entry` also (same branch): sets the busy flag `akoid_buf[0x180] = 1`
   (`0x0804c1d4.c` L87), arms the 100-unit heartbeat timer, and — crucially for taps —
   the OID poll timer is already started at splash entry (`func_0x080058b0()` =
   `hal_oid_timer_start`, `0x0804c1d4.c` L29), before the prod-test gate. [P]
3. The AO stays **parked in splash(1)** forever in prod-test (never posts 0x1014→standby), so
   the leaf event handler for every subsequent event *is* the prod-test stage machine
   `fwupdate_verify_image` 0x0804CECC. [P — `test-mode.md` §3, statechart map state 1]

Searched alternatives, all negative [P]: no other caller of the number-speakers; no OID value
in `cover_oid_classifier` or `gme_oid_dispatch` that triggers a readout; no button chord other
than the prod-test gate reaching this code; retail states never call voices 0x09–0x12 with a
tapped value.

---

## 2. Q2 — the number-speaker: `FUN_0804bf84` (start) / `FUN_0804c090` (resume)

Both operate on `akoid_buf` (= `*(game_ctx+0x20)`) fields [P — decomp verbatim]:

| field | role |
|---|---|
| `+0x64` (u32) | remaining value being spoken (destructively decremented) |
| `+0x60` (u8) | readout-active flag (1 = in progress) |
| `+0x181` (u8) | resume position: 0=10⁴ place, 1=10³, 2=10², 3=10¹, 4=10⁰ |

Common gates in both functions: return unless `akoid_buf[0x60] != 0` **and**
`is_audio_playing() == 0` — one digit per audio-idle invocation. [P]

### 2.1 `FUN_0804bf84` — start (speak the most significant digit)

Pure decimal decomposition by repeated subtraction (the odd-looking
`val < 0x2000 || val-0x2000 < 0x710` guards are the compiler's split of the unsigned
`val < 10000` compare):

- picks the **highest magnitude bucket the value actually reaches** (≥10000 → ten-thousands;
  ≥1000 → thousands; ≥100; ≥10; else single digit) — so **no leading zeros**;
- subtracts that power of ten in a loop, counting into `digit`; remainder stays at `+0x64`;
- single-digit case clears `+0x60` (done); otherwise stores the *next* position (1..4) at
  `+0x181`;
- then `aud_player_construct(); fwl_play_voice_by_id(digit + 9)`. [P]

**Digit → voice id: `d + 9` ⇒ voices 0x09 ("0") … 0x12 ("9")** — the Chomp_Voice.bin digit
set (`system-voice-feedback.md` §4; 0.4–0.7 s 16 kHz mono WAVs). Wording is Chinese [I — the
adjacent prod-test clip set 0x1B–0x2A is Chinese per the wiki's phonetic transcriptions; the
digit clips sit in the same 16 kHz factory block].

### 2.2 `FUN_0804c090` — resume (one digit per call)

`switch (akoid_buf[0x181])`: case *k* subtracts the corresponding power of ten in a loop
(count may be **0 — interior zeros ARE spoken**), advances `+0x181`, plays `count+9`; case 4
(ones) plays the last digit and **clears `+0x60`**. Case 0 with value < 10000 plays voice
0x09 ("0") — only reachable if the start call was deferred (see §2.4 nuance). [P]

### 2.3 Who ticks the resume

`FUN_0804c090()` is called **unconditionally at the top of `fwupdate_verify_image` on every
event** once `[0xb3]==1` (`0x0804cecc.c` L51). The 100-unit prod-test heartbeat timer (cb
nandboot 0x08003994, armed by `splash_entry`) plus the regular 0x1046 heartbeats keep events
flowing, so the digits emerge back-to-back, each starting when the previous WAV finishes. [P]

### 2.4 Edge cases [P]

- **No decimal point / no padding** — plain decimal, MSD first, interior zeros spoken,
  leading zeros skipped (e.g. 4076 → "4","0","7","6").
- **No range guard beyond 5 digits**: a value ≥ 100000 would produce a ten-thousands count
  > 9 → voice id > 0x12 → out-of-table garbage (`fwl_play_voice_by_id` has no bounds check).
  Unreachable from taps: the classifier latches `raw & 0xFFFF` (≤ 65535, 5 digits max).
- **Tap while audio is playing**: the trigger still arms `+0x64/+0x60/+0x181=0` and calls
  the start fn, which early-returns (audio busy). The readout then begins via the *resume* at
  stage 0 — for values < 10000 that speaks a spurious **leading "0"** (case-0 fallthrough,
  §2.2). Tap when quiet for the clean sequence.
- The value at `+0x64` is also used by the prod-test flow itself; a new tap simply overwrites
  a readout in progress (`+0x181=0` restart), and the tip test cancels one (`+0x60=0`,
  `0x0804cecc.c` L692).

---

## 3. Q3 — the trigger: tap → 0x1060 → splash EA digit branch

### 3.1 Capture (why taps work while parked in splash)

The nandboot **23-bit poll path** delivers taps here — not the 32-bit status polls:
`hal_oid_timer_start` (armed at splash entry, self-re-arming every 40 sw-timer ticks via
`hal_oid_timer_cb` 0x07FFD7CC L53) → gated on `akoid_buf[0x14]!=0` (init 1) && **GPIO9 == 0**
(sensor attention) → `hal_oid_capture_decode23` (23-bit shift-in; require type
`(raw & 0x600000) == 0x400000`; drop filler 0x43FFFC) → `akoid_buf[0] = 1`,
`akoid_buf+8 = 0x400000|index`, **`hal_event_post(0x1060, &raw)`**. [P —
`0x07ffd7cc.c`/`0x07ffd764.c`; `oid-sensor-read-protocol.md` §5.1]

**The tip-test sleep does not block taps**: passing the tip test ends with
`akoid_sensor_sleep` + `done_latch` (0x08008C0F) = 1, but that latch is only checked by the
32-bit poll path (`akoid_poll_from_idle`/`akoid_poll_status32`); neither
`hal_oid_timer_cb` nor `hal_oid_capture_decode23` reads it. [P — no 0x08008C0F reference in
either fn] (Physically the sensor was told to sleep, but a real sensor wakes on pen-down, so
taps keep arriving. [I])

### 3.2 Routing — how "speak digits" wins over "load a product"

Event 0x1060 first runs the root transition action **`cover_oid_classifier` 0x08037CEC**:

- **Normal code** (`(raw & 0x600000)==0x400000`, index not in 0xFF00–0xFFFE): latches
  **`akoid_buf+4 = raw & 0xFFFF`** (the value every consumer reads), sets `[0xe2]=1`; then at
  L242: **`if (akoid_buf[0xb3] != 0) return 1;`** — in prod-test the classifier bails out
  *before* the entire retail cover-tap/book-open machinery (no 0x1058 substitution, no
  first-load branch), and return 1 = propagate to the leaf. [P — `0x08037cec.c` L83–88, L242]
- **System family 0xFF00–0xFFFE**: `akoid_buf[0] = 2` (instead of 1) + consecutive-tap
  counter `[0xac]`; in prod-test < 0x12 consecutive → return 1; **≥ 0x12 (18) consecutive
  system taps → `*0x08008c0b = 3` = the power-off path** (an alternate exit). [P — L39–77]

The leaf is splash(1)'s EA `fwupdate_verify_image`; its 0x1060 branch
(`0x0804cecc.c` L607–635) does, in prose [P]:

- A **system-family tap** (`akoid_buf[0]==2`) only speaks voice 0x2F, and only once `stage`
  passes 0x29 and no voice is playing.
- A **normal tap** (`akoid_buf[0]!=0`) is gated on `stage > 5 && akoid_buf[0x180] != 1` (i.e. past
  the fixed self-tests and not busy). When the gate opens it reads the tapped OID from
  `akoid_buf+4`: a **single digit** (`oid < 10`) plays voice `(oid & 0xF) + 9` directly; a
  **multi-digit** value is stashed at `akoid_buf+0x64` (with `+0x60=1`, `+0x181=0`) and
  `FUN_0804bf84()` starts the multi-digit read-out.

**The gate** (`stage` = prod-test stage byte @ **0x081DA004+2**, `+0x180` = busy flag):

| phase | stage | +0x180 | tap speaks? |
|---|---|---|---|
| splash entry → program test → verdict 0x1D/0x1E | 0x1F/0x20 → 1/2 → 3/4 | 1 (entry L87) until the verdict voice (clear @ L679) | **no** (busy, then stage ≤ 4) |
| after '+' press 1 (tip test, stage 5→**6**, pass *or* fail) | 6 | 0 | **YES** |
| after '+' press 2 (auth, 7→8) / '−' cascade (9…0x17) | > 5 | transient 1 during auth/file work | yes (when not busy) |

So: **one volume-up press after entry is the minimal unlock**. In retail mode none of this is
reachable — `[0xb3]==0` sends 0x1060 through the cover-tap/product path instead
(`cover-tap-first-product-load.md`). [P]

---

## 4. Q4 — how the readout is reached and what it does

The readout lives inside the factory prod-test mode (`test-mode.md` §5): after the entry gesture
(power + vol-down at boot) and the automatic phase (voices 0x1B → 0x1C → 0x1E/0x1D), a **vol-up
press** runs the tip test (voice 0x1F, then 0x20 pass / 0x21 fail) which sets the prod-test stage
to 6 — and stage > 5 is exactly the gate that **unlocks the readout**. From then on, tapping any
OID while no voice is playing speaks the code as digits.

Each tapped value `N` (delivered as a normal 0x1060 tap; avoid the filler 0x3FFC and the system
family 0xFF00–0xFFFE) is read out digit by digit, `d → voice d + 9`:

| tapped OID | spoken | voice-id sequence |
|---|---|---|
| **4716** | "4","7","1","6" | **0x0D, 0x10, 0x0A, 0x0F** |
| **42** | "4","2" | **0x0D, 0x0B** |
| 7 | "7" | 0x10 (immediate, preceded by a voice-stop) |
| 4076 | "4","0","7","6" | 0x0D, 0x09, 0x10, 0x0F |

Each id resolves to a Chomp_Voice.bin WAV (16 kHz mono, 0.4–0.7 s). A new tap during a readout
restarts with the new value. Internally: `akoid_buf+4` = N after the classifier; `+0x64` counts
down to the last digit; `+0x60` goes 1→0 across the readout; `+0x181` steps 0→…→4; the pen stays
parked in splash(1) with stage byte `0x081DA006 == 6`.

(As always, the digit voices are only audible if `A:/VOIMG/Chomp_Voice.bin` is present on A:;
without it each digit is a silent no-op.)

---

## 5. Names / docstrings (names.csv candidates)

| addr | name | docstring |
|---|---|---|
| 0x0804BF84 | `prodtest_speak_number_start` | Begin decimal readout of akoid_buf+0x64 (gate: +0x60 && audio idle): speak MSD (no leading zeros), remainder → +0x64, next place → +0x181, voice = digit+9 (0x09–0x12). Only caller: 0x0804CECC. |
| 0x0804C090 | `prodtest_speak_number_resume` | One digit per call (place from +0x181, interior zeros spoken); ones place clears +0x60. Ticked from 0x0804CECC L51 on every prod-test event. |
| 0x0804BEE8 | `prodtest_voice_stop_helper` | Stop current voice before single-digit play (decomp truncated: halt_baddata after FUN_080ab424). |
| fields | | `akoid_buf+0x64` readout value; `+0x60` readout-active; `+0x181` readout place 0..4; `+0x180` prod-test busy (blocks buttons AND readout); `0x081DA004+2` prod-test stage (readout needs > 5). |

---

## 6. Evidence index

| claim | status | source |
|---|---|---|
| wiki Test mode has NO OID-readout mention; '+'/'−' tests only | **W** | PEN-Firmware wiki, "Test mode" section fetched verbatim 2026-07-06; PEN-Hardware page = 404 |
| number-speakers called only from 0x0804CECC; no separate mode | **P** | corpus grep `FUN_0804bf84|FUN_0804c090` → only `0x0804cecc.c` L51/L631 |
| decimal decomposition, digit+9, places via +0x181, zeros semantics | **P** | `0x0804bf84.c`, `0x0804c090.c` (full listings) |
| trigger branch: akoid_buf[0]==1/2 split, gate stage>5 && +0x180!=1, <10 fast path, ≥10 start | **P** | `0x0804cecc.c` L607–635 |
| stage=6 after tip test pass AND fail; busy set@entry/L689, clear@L679; auth busy L136/L145 | **P** | `0x0804cecc.c` L685–711, L129–146; `0x0804c1d4.c` L87 |
| classifier: latch raw&0xFFFF → akoid_buf+4, early `return 1` when [0xb3]!=0; system taps → [0]=2, 18× → off | **P** | `0x08037cec.c` L28–88, L242–244 |
| OID poll timer armed at splash entry; 23-bit cb posts 0x1060, ignores done_latch | **P** | `0x0804c1d4.c` L29; `0x07ffd7cc.c`, `0x07ffd764.c` |
| digit voices 0x09–0x12 in Chomp_Voice.bin, plain WAV, fwl_play_voice_by_id path | **P** | `system-voice-feedback.md` §1–4 (re-used) |
| digit clips are Chinese | **I** | wiki gives Chinese phonetics for the surrounding factory clip set; same 16 kHz block |
| real sensor wakes from sleep on new pen-down | **I** | firmware ignores done_latch on the 23-bit path, so HW must re-signal; Sonix behaviour not captured |
