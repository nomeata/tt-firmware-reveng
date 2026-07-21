# Anti-clone auth-chip verification (Chomptech ZC90B) — does the firmware gate boot on it?

Static analysis of the named decompilation (unified base **0x08009000**). Evidence tags:
**[Proven]** = read directly from the cited decomp/HAL/statechart; **[Inferred]** = deduced (reason
given); **[Open]** = unresolved. Companions: `zc90b-model.md` (the full challenge/response
algorithm and the recovered device response function), `pmu-power-management.md` (§1.1 GPIO map, the
GPIO15=0 off-paths), `hardware-external-refs.md` (ZC90B token), `statechart-full-map.md` (state 1 =
splash), `soc-core-registers.md` (GPIO HAL).

> **Scope note.** This documents *that* and *how* the firmware gates boot on the auth chip — the
> protocol, the pins, and the boot consequence. The **values** of the substitution tables (the
> shared secret) and any complete clone/response implementation are **deliberately not reproduced
> here**; the tables are referred to only by their firmware addresses.

---

## 0. Headline answer

**YES — the firmware performs a hardware challenge/response against the ZC90B and powers the pen
off on failure**, and it sits on the **autonomous boot path**.

- The verifier is **`FUN_0804c47c` @0x0804c47c** — a GPIO **bit-banged challenge/response** on
  **GPIO 10 (clock) + GPIO 5 (data)** (the ZC90B's two data lines). [Proven]
- It is invoked from **`fwupdate_verify_image` @0x0804cecc** (the **state-1 `splash` default event
  handler**, statechart EA[0]). On mismatch it sets `akoid_buf[0xb4]=1`; the handler then drives
  **GPIO15=0 and spins forever** (`0x0804cecc` L45-49) → **pen powers off**. This is the
  "auth-chip-fail" GPIO15→0 off-path (`pmu-power-management.md`). [Proven]
- **Consequence for running the firmware:** the quiet/first-load boot path (GPIO 11/1 = 0 →
  `akoid_buf[0xb3]=0`) reaches this check, so a genuine boot requires a device on GPIO 5/10 that
  answers the challenge correctly — with no such device present the all-zero readback mismatches
  → power-off. [Proven]

---

## 1. Where it lives on the boot path (Q1)

`splash` is statechart **state 1**; its **default event-action EA[0] = `fwupdate_verify_image`
@0x0804cecc** (statechart-full-map.md §2, state-1 row and EA table). [Proven]

`splash_entry` (0x0804c1d4) sets `akoid_buf[0xb3]=0` (L49), then runs the **battery gate**: four
successive `battery_get_level()!=0` (GPIO 11==1 && GPIO 1==1).
- **All four pass → "healthy" boot:** `akoid_buf[0xb3]=1`, `akoid_buf[0xb4]=0`, `sm_set_state(2)`
  (fw_update/prod-test), voice 0x1B. [Proven]
- **Any fail → "quiet"/low-battery/first-load boot:** `b3` stays **0**, stays in state 1. [Proven]

Then, on the next default event in state 1, `fwupdate_verify_image` runs. Its **top block is gated
on `akoid_buf[0xb3]==0`** (0x0804cecc L31):

```c
if (akoid_buf[0xb3] == 0) {              // the quiet/first-load path
    if (is_audio_playing()) return 0;    // defer while a voice is playing
    FUN_0804c47c();                      // <-- ZC90B challenge/response
    if (akoid_buf[0xb4] == 0) {          // PASS
        func_0x08003644(DAT_0804e528,0); // post continuation event (=0x1014 → standby) [Inferred]
        FUN_0803d094();                  // (no-op when on battery: game_ctx[0x1e]==0)
        return 0;
    }
    // FAIL (akoid_buf[0xb4]==1):
    FUN_080ab424();                      // amp off
    func_0x08007938(5);
    akoid_rearm();
    func_0x0800774c(0xf,1);              // GPIO15 dir=out
    func_0x08007734(0xf,0);              // GPIO15 = 0  → RELEASE POWER-HOLD LATCH
    do { } while (true);                 // spin until the supply dies
}
```

**Conditionality:** the **hard power-off** auth gate is specifically the **`b3==0` path** — i.e.
the low-battery / first-load / "quiet" boot (pmu-power-management.md §3.1: GPIO 11/1 = 0 → quiet
path, `b3` stays 0). On the *healthy* boot (`b3==1`) this top block is skipped; `FUN_0804c47c`
is instead called later in the fw-update
apply flow (0x0804cecc L137, `LAB_0804d3d0`) where a mismatch only plays a "fail" voice (0x24) and
does **not** power off. So: **quiet boot = hard auth gate (power-off); healthy boot = soft auth
(voice only).** [Proven]

`FUN_0804c47c` is called from **nowhere else** in the decomp (only 0x0804cecc, two sites). [Proven]

---

## 2. The protocol (Q2) — GPIO 10 = clock, GPIO 5 = data

HAL (pmu §1.1 / soc-core-registers.md): `func_0x0800774c(pin,dir)` → dir reg `0x0400007C`;
`func_0x08007734(pin,val)` → output reg `0x04000080`; `func_0x08007740(pin)` → input reg
`0x040000BC`; `func_0x08002fa4(n)` = short busy-delay; `func_0x08007938(n)` = longer delay;
`func_0x0800339c`/`func_0x080033e4` = IRQ mask/unmask around the timing-critical exchange;
`FUN_080f3268(5,..)` = pin-5 pull/mode config. [Proven HAL; the two ZC90B lines = **GPIO 5 & GPIO
10** Proven from the bit-bang]

**Lines:** **GPIO 10** is always driven as the **clock** (toggled 1→0 per bit); **GPIO 5** is the
bidirectional **data** line (driven while sending the challenge, switched to input to read the
response). These are the ZC90B's two data pads (hardware-external-refs.md: ZC90B pins 4 & 7 "emit
data bursts at power-on"). [Proven bit-bang; pad mapping Inferred from the ZC90B 2-line note]

**It is a table-based challenge/response with a random nonce** (main branch, `b3==0`):

1. **Nonce + expected responses** (firmware holds copies of the secret tables):
   - `seed = game_ctx[+0x20][+0xcc]` (a rolling counter). [Proven]
   - `uVar13 = rng_below(1000) + seed`; expected `A = tableA[uVar13 & 0xd7]`  (tableA = DAT_0804cec0)
   - `uVar10 = rng_below(2000) + seed`; expected `B = tableB[uVar10 & 0xbe]`  (tableB = DAT_0804cebc)
   - `uVar14 = (rng_below(DAT_0804cec8) + seed) & 0xff`; expected `C = tableC[uVar14]` (tableC = DAT_0804cec4)
2. **Send 3 challenge bytes**, MSB-first, one bit per clock (GPIO10 high → put bit on GPIO5 → GPIO10
   low), 8 bits each:
   - byte1 = `B ^ uVar14`  (obfuscated: mixes an expected-response byte with a nonce byte)
   - byte2 = `uVar10 & 0xff`
   - byte3 = `uVar13 & 0xff`
3. **Wait for the chip**: poll GPIO5 up to 0x30 times until it goes non-zero (chip-ready). [Proven]
4. **Read 3 response bytes**, MSB-first, clocked by GPIO10, sampled on GPIO5: `R1, R2, R3`.
5. **Compare** (0x0804c47c L184-199):
   - pass ⟺ `R3 == A` **and** `R2 == B` **and** `R1 == C`
   - pass → `akoid_buf[0xb4] = 0`; any mismatch → `akoid_buf[0xb4] = 1`.

So the genuine ZC90B is expected to apply the **same table transform** the firmware pre-computed
(the lookup tables in ROM are the shared secret) and echo it back. The nonce (`rng_below`) makes it
a live challenge, not a replayable fixed handshake. [Proven mechanics; the tables are the shared
secret, and the chip's response transform is derived and verified in `zc90b-model.md` §2d]

**Else branch** (`b3!=0` && `*DAT_0804ce94!=0`): a nested **brute-force scan** (uVar13 0..99 ×
uVar10 0..2) that always clears `akoid_buf[0xb4]=0` and breaks on first pass — a
provisioning/diagnostic sweep, **not** the normal boot check. [Proven; purpose Inferred]

---

## 3. Pass/fail + consequence (Q3)

- **Pass:** all three readback bytes equal the table-derived expected values → `akoid_buf[0xb4]=0`.
  0x0804cecc posts the continuation event (`DAT_0804e528`, → 0x1014 → **standby**, then book) and
  boot proceeds normally. [Proven; event id 0x1014 Inferred from splash mapper + pmu §3.1]
- **Fail:** `akoid_buf[0xb4]=1` → 0x0804cecc drives **GPIO15=0** (releases the power-hold latch) and
  enters an infinite `do{}while(true)` spin → the pen loses power. **No retry, no degrade** on the
  quiet-boot gate: a single exchange, one shot. [Proven]
- (Soft variant on the healthy/fw-update path: mismatch → voice 0x24 instead of 0x23, no power-off.)
  [Proven]

This is exactly the pmu fork's **"auth-fail kill" GPIO15→0 off-path** (pmu-power-management.md §1.1,
pin-15 row: "auth-fail kill 0x0804cecc L45-49"). [Proven]

---

## 4. What satisfies the gate (Q4)

The quiet boot path (GPIO 11/1 = 0 → `akoid_buf[0xb3]=0`) runs the `b3==0` top block, so the check
is reached on any genuine autonomous boot. With GPIO5 (data) left idle the readback is all-zero,
which cannot equal the table-derived expected bytes → mismatch → `akoid_buf[0xb4]=1` → GPIO15=0 →
power-off. [Proven]

To pass, the responder on GPIO 5/10 must implement the same clock/data state machine: read the
three challenge bytes the firmware clocks out on GPIO5 (`byte1 = B^n, byte2, byte3`), and drive
back on GPIO5, MSB-first under the GPIO10 clock, the three bytes that satisfy `R1==C, R2==B, R3==A`
— i.e. the values the firmware pre-computes from its own copies of the tables
(`DAT_0804cec0/cebc/cec4`) and the rng bound (`DAT_0804cec8`). Those table values are the shared
secret and are **not reproduced here** (see the scope note at the top). On the real pen the genuine
ZC90B is the responder; running the firmware outside an authentic pen requires an equivalent
device model.

**Bottom line:** the firmware **does gate boot on the auth chip** on the autonomous path, and
without a correct responder on GPIO 5/10 the pen powers itself off.

---

## 5. Names / docstrings (Q5)

| addr | suggested name | role |
|---|---|---|
| **0x0804c47c** | `anticlone_zc90b_verify` | ZC90B GPIO10-clk/GPIO5-data bit-banged challenge/response; nonce+table expected bytes; sets `akoid_buf[0xb4]` 0=pass/1=fail. Else-branch = brute-force provisioning scan. |
| **0x0804cecc** | `fwupdate_verify_image` (existing) — really the **splash default handler**; top block (`b3==0`) is the **anti-clone gate**: run verify, on fail GPIO15=0 + spin (power-off); on pass post 0x1014→standby. Lower body = fw-update apply flow (soft auth at L137). |
| **0x0803d094** | `lowbatt_splash_post` | posts continuation events only when on cable (`game_ctx[0x1e]∈{1,2}`); no-op on battery. |
| `akoid_buf[0xb4]` | `anticlone_verify_failed` | 1 = ZC90B mismatch (→ power-off on quiet boot); 0 = ok. |
| `akoid_buf[0xb3]` | `boot_healthy_flag` | 1 = battery-gate-healthy boot (→ state 2, soft auth); 0 = quiet/first-load boot (→ hard auth gate). |
| DAT_0804cec0 / cebc / cec4 | `zc90b_tableA/B/C` | response lookup tables (shared secret). |
| DAT_0804cec8 | `zc90b_rng_bound_C` | rng bound for the 3rd nonce byte. |

---

## 6. Open items

- **[Open]** The value of the posted continuation event `DAT_0804e528` (asserted 0x1014) — a
  literal-pool lookup. (The table *contents* are intentionally out of scope for this repository.)
- **[Open]** Physical ZC90B protocol timing vs. the firmware's `func_0x08002fa4(200)` / `(0x78)` /
  `(0xfa)` delays — only matters for a cycle-faithful pad model; the firmware does not check pulse
  widths, so a functional (untimed) responder that returns the right bytes suffices.
- **[Open]** Whether a *real* healthy pen (GPIO 11/1 = 1) ever exercises the hard gate — on hardware
  the quiet path is entered on low battery / first load; the soft (voice-only) auth covers the
  healthy fw-update path.
