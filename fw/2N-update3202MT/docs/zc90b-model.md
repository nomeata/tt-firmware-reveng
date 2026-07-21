# ZC90B anti-clone auth chip — challenge/response model

The Chomptech **ZC90B** is an external anti-clone chip the pen authenticates over two GPIO
lines before it will operate. This documents the bit-banged challenge/response protocol and
the (fully recovered) response function, so the exchange is understood end to end. The
`tt-emu` emulator implements this model to answer the pen's challenge with a correct chip;
see `anticlone-auth-check.md` for where the check sits on the boot path and its power-off
consequence.

Static evidence only. Unified base **0x08009000**; file offset = addr − 0x08009000; image =
`data/PROG.bin` (obtained via `tools/fetch_firmware.py`). **[Proven]** = read directly from
the decompilation; **[Derived]** = algebraically follows from it.

---

## 0. Headline result

**The response is fully deterministic in the 3 challenge bytes the firmware clocks out — the
device needs NO RNG.** The firmware's nonce only randomizes *which* challenge is sent; a
correct chip just transforms the challenge through 3 fixed S-boxes and echoes it back. All
three tables are 256-byte **bijective S-boxes** (each a permutation of 0x00..0xFF).
Simulating 1000 random nonces, a device that recovers the answer from the challenge alone
passes **every** time. **[Proven + Derived]**

---

## 1. The literal pool (table pointers), decoded

From the literal pool at 0x0804ce90 (file off 0x43e90):

| symbol (suggested name) | addr | value | meaning |
|---|---|---|---|
| DAT_0804ce90 | 0x0804ce90 | 0x080089a4 | game_ctx base ptr |
| DAT_0804ce94 | 0x0804ce94 | 0x081da004 | else-branch enable flag ptr |
| DAT_0804cebc = `zc90b_tableB` | 0x0804cebc | **0x080b0178** | S-box B (256 B) |
| DAT_0804cec0 = `zc90b_tableA` | 0x0804cec0 | **0x080b0078** | S-box A (256 B) |
| DAT_0804cec4 = `zc90b_tableC` | 0x0804cec4 | **0x080b0278** | S-box C (256 B) |
| DAT_0804cec8 = `zc90b_rng_bound_C` | 0x0804cec8 | **0x00001770** = 6000 | rng bound for 3rd nonce byte |

The three tables are contiguous 256-byte blocks at 0x080b0078 / 0178 / 0278 (file off
0xa7078 / 0xa7178 / 0xa7278). **[Proven]**

---

## 2. The algorithm (Q1)

### 2a. What the firmware computes (expected responses), main `b3==0` branch

```c
seed  = game_ctx[0x20][0xcc];            // rolling counter
n13   = rng_below(1000) + seed;          // nonce for A
n10   = rng_below(2000) + seed;          // nonce for B
n14   = (rng_below(6000) + seed) & 0xff; // nonce/index for C

expA = tableA[n13 & 0xd7];   // expected response byte A
expB = tableB[n10 & 0xbe];   // expected response byte B
expC = tableC[n14];          // expected response byte C
```

### 2b. The 3 challenge bytes clocked OUT on GPIO5 (MSB-first)

```c
c1 = expB ^ n14;   // loop 1: mixes expected-B with the C index
c2 = n10 & 0xff;   // loop 2
c3 = n13 & 0xff;   // loop 3
```

### 2c. The 3 response bytes read BACK on GPIO5 (MSB-first): R1, R2, R3

Compare (0x0804c47c) — **PASS ⟺ R3==expA && R2==expB && R1==expC**, else
`akoid_buf[0xb4]=1`. **[Proven]**

### 2d. The ZC90B response function (what a genuine chip does) — derived, self-contained

The chip receives only `(c1,c2,c3)` and must return `(R1,R2,R3)`. Because the masks fit in a
byte (`c2 & 0xbe == n10 & 0xbe`, `c3 & 0xd7 == n13 & 0xd7`) and B is recoverable, the answer
is a closed form in the challenge — **no nonce, no seed, no RNG needed on the device side**:

```c
// ZC90B: given challenge bytes c1,c2,c3 → response R1,R2,R3
B    = tableB[c2 & 0xbe];      // == expB   (recovers B first)
idxC = (c1 ^ B) & 0xff;        // == n14    (un-mixes the C index from c1)
C    = tableC[idxC];           // == expC
A    = tableA[c3 & 0xd7];      // == expA
R1 = C;   R2 = B;   R3 = A;    // firmware checks R3==A, R2==B, R1==C
```

Why it works: `c1 ^ B = (expB ^ n14) ^ expB = n14`, so the chip un-mixes the C index using B,
which it derived from `c2`. `c3 & 0xd7` reproduces the A index; `c2 & 0xbe` the B index.
**[Derived, verified over 1000 random nonces — all pass.]**

**Note on ordering:** the firmware sends `c1,c2,c3` in that order but the byte carrying the A
index is the *last* one sent (`c3`) and expected back *last* (`R3`); B is middle; C is first.

---

## 3. The three S-box tables (Q2)

All three are **256-byte bijective S-boxes** (each a permutation of 0x00..0xFF), stored
contiguously in the firmware image:

| table | address | file offset | index used |
|---|---|---|---|
| `tableA` | 0x080b0078 | 0xa7078 | `challenge3 & 0xd7` |
| `tableB` | 0x080b0178 | 0xa7178 | `challenge2 & 0xbe` |
| `tableC` | 0x080b0278 | 0xa7278 | `(challenge1 ^ B) & 0xff` |

The table *contents* are firmware data and are not reproduced here; extract them directly from
your verified `PROG.bin`:

```python
data = open("fw/2N-update3202MT/data/PROG.bin", "rb").read()
BASE = 0x08009000
tableA = data[0x080b0078 - BASE : 0x080b0078 - BASE + 256]
tableB = data[0x080b0178 - BASE : 0x080b0178 - BASE + 256]
tableC = data[0x080b0278 - BASE : 0x080b0278 - BASE + 256]
assert all(sorted(t) == list(range(256)) for t in (tableA, tableB, tableC))  # bijective
```

---

## 4. GPIO wire model (Q3)

Lines: **GPIO10 = clock** (always driven output, toggled 1→0 per bit); **GPIO5 = data**
(bidirectional: driven while sending challenge, switched to input to read response). HAL:
`func_0x0800774c(pin,dir)` = dir reg 0x0400007C; `func_0x08007734(pin,val)` = out reg
0x04000080; `func_0x08007740(pin)` = in reg 0x040000BC; `func_0x08002fa4(n)` = short busy
delay; `func_0x0800339c`/`func_0x080033e4` = IRQ mask/unmask around the exchange. **[Proven]**

**Bit order:** MSB-first — bit = `value >> (7 - i)` for `i = 0..7`. 8 clocks per byte, 3 bytes
each direction. **[Proven]**

**Challenge (send) phase — per bit:**
1. `GPIO10 = 1` (clock high)
2. `GPIO5 = bit`  (put data bit while clock high)
3. delay (2× `func_0x08002fa4(200)`)
4. `GPIO10 = 0` (clock low — **falling edge is the latch**; chip samples data here)
5. delay (2×200)

Order sent: byte `c1` (`expB ^ n14`), then `c2` (`n10&0xff`), then `c3` (`n13&0xff`).

**Ready handshake:** after the challenge + a short trailer, the firmware polls `GPIO5` input up
to **0x30 (48)** times, breaking when it reads non-zero. The chip signals "response ready" by
pulling GPIO5 high. **[Proven]**

**Response (read) phase — per bit:**
1. `GPIO10 = 1`; delay (2×200)
2. `GPIO10 = 0`; delay `func_0x08002fa4(0x78)`
3. sample `GPIO5` input → if non-zero, set bit `(7 - i)`
4. delay `func_0x08002fa4(0xfa)`

So the chip must **present each response bit while the clock is low** (drive GPIO5 on the 1→0
edge, hold it stable through the sample). Order read: `R1` then `R2` then `R3` (each MSB-first).
Firmware maps R1→C-slot, R2→B-slot, R3→A-slot.

**Framing summary:** IRQ-off → send c1,c2,c3 (24 clocks, data on GPIO5, latch on GPIO10
falling) → trailer clocks → poll GPIO5 for ready (≤48 tries) → read R1,R2,R3 (24 clocks, sample
GPIO5 while clock low) → release lines. Timing (200/0x78/0xfa delays) only matters for a
cycle-faithful pad; a functional model that returns the right bits under the clock passes.
**[Proven / Open: exact HW ns]**

---

## 5. Nonce independence (Q4) — CONFIRMED

The compare is against `f(nonce)` where `f` is the S-box transform, and the **same** nonce
bytes are embedded in the challenge (`c2=n10&0xff`, `c3=n13&0xff`, `c1=expB^n14`). The device
recovers every index it needs from `(c1,c2,c3)` alone (see §2d). The response is deterministic
in the challenge; verified: 1000 random `(seed, rng)` draws all pass. **[Proven + Derived]**

---

## 6. Reference device model (Q5)

A correct chip can be modelled as a state machine driven purely by watching GPIO10 edges and
GPIO5:

```
state IDLE:
  on the exchange start (IRQ-off / first clock), enter RECV, shift_count=0, byte_buf=0

state RECV (capturing challenge, GPIO5 = firmware output):
  on GPIO10 falling edge (1->0):
      byte_buf = (byte_buf << 1) | GPIO5_out_level     # MSB-first
      if ++bit == 8: store c[k++]; byte_buf=0; bit=0
      if k == 3:  # have c1,c2,c3
          B = tableB[c2 & 0xbe]
          C = tableC[(c1 ^ B) & 0xff]
          A = tableA[c3 & 0xd7]
          resp = [C, B, A]      # R1,R2,R3
          enter READY

state READY:
  drive GPIO5_in = 1  (ready handshake; firmware polls up to 48x)
  on first response read-clock, enter SEND

state SEND (driving response, GPIO5 = device output = firmware input):
  maintain a bit pointer over resp[0..2], MSB-first (24 bits)
  when firmware pulls GPIO10 low to sample, present resp_bit on GPIO5_in and hold
  after 24 bits: release GPIO5, return to IDLE
```

Key points:
- During RECV, GPIO5 is a firmware **output** — read the level the firmware drives on the
  clock falling edge.
- During SEND, GPIO5 is a device **output** — drive the value that `func_0x08007740(5)`
  returns, presenting each bit while GPIO10 is low.
- The ready handshake: hold GPIO5 high after the 3rd challenge byte so the poll loop breaks.
- No RNG, no seed, no timing sensitivity required for correctness — only bit order (MSB-first)
  and the C/B/A response order.

With this model, `anticlone_zc90b_verify` (0x0804c47c) reads back exactly `R3==expA,
R2==expB, R1==expC` and sets `akoid_buf[0xb4]=0` (PASS).

---

## 7. Names / docstrings

| symbol | name | note |
|---|---|---|
| 0x080b0078 | `zc90b_tableA` | 256-B bijective S-box; A = tableA[challenge3 & 0xd7] |
| 0x080b0178 | `zc90b_tableB` | 256-B bijective S-box; B = tableB[challenge2 & 0xbe] |
| 0x080b0278 | `zc90b_tableC` | 256-B bijective S-box; C = tableC[(challenge1 ^ B) & 0xff] |
| 0x0804cec8 | `zc90b_rng_bound_C` | = 6000; rng bound for 3rd nonce byte (firmware side only) |
| 0x0804c47c | `anticlone_zc90b_verify` | GPIO10-clk/GPIO5-data challenge/response; PASS→akoid_buf[0xb4]=0 |

Device response function (one line): `R = ( tableC[(c1 ^ tableB[c2&0xbe]) & 0xff],  tableB[c2&0xbe],  tableA[c3&0xd7] )`.
