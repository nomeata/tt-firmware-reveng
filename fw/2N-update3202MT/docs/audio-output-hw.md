# Audio OUTPUT hardware — internal DAC, audio DMA, PCM ring, amp/mute GPIOs, DMA-done IRQ (2N MT)

The **register-level model of the audio-out path** (decoded PCM → speaker). Cross-references
`media-pipeline.md`, `soc-core-registers.md`, `pmu-power-management.md`,
`system-voice-feedback.md`, `settings-config.md`. Static analysis of the decompilation
(unified base **0x08009000**; low-RAM HAL = the `nandboot` blob relocated, static twin
`0x07ffxxxx` ≈ runtime `0x0800xxxx` (+0x8000); pool constants resolved from the PROG/nandboot
bytes). Tags: **[Proven]** = read from decomp/disasm/bytes; **[Inferred]** = deduced (reason
given).

> ### Key facts (all Proven here)
> 1. **The audio DMA-done IRQ is top-level line 0** (`0x040000cc`/`0x04000034` bit0). `0x3e`
>    (62) is *not* an interrupt line: it is a software event-id constant that
>    `hal_audio_clk_enable` (rt 0x08003d18) memcpy's into `0x08006a80` and nothing in the
>    corpus ever reads back.
> 2. **DMA address encoding**: memory addresses are written **`phys & 0x3ffff`** (18-bit,
>    NO `|0x08080000`); the `|0x8080000` OR applies only to **peripheral-port codes** (§3).
> 3. **The refill is IRQ-chained in the HAL itself**: the DMA-done handler (rt 0x080039d4)
>    directly resubmits the next ring chunk from interrupt context; the PROG-side tick only
>    (re)kicks the chain after an underrun/stop (§5).
> 4. `0x08003d18` is the **audio-clock enable** (0x04036000 block), not the interrupt-enable;
>    the INT_ENABLE bit0 toggler is **rt 0x08003430** (static 0x07ffb430).
> 5. The ring is **fixed 0x3000 bytes (12 KB = 12 chunks)**, set at construction (§2).

---

## 0. Headline model

Decoded PCM (any codec) is **volume-scaled (Q10) and stereo-expanded** into a single
**12 KB ring buffer** (singleton ptr `0x08008d2c` → body `0x08008d30`). A small **peripheral
DMA engine at `0x04010000`** streams the ring **0x400 bytes (256 stereo frames) per
transaction** into the **SoC-internal DAC** (control block `0x04080000`, analog/clock fields
in the CORE window). Completion raises **top-level IRQ line 0**; the HAL ISR **chains the next
chunk itself** until the ring drains. Sample rate is set **per track** by retuning the DAC
clock divider/OSR (no software resampler). The speaker path is gated by three GPIOs:
**GPIO16 = external amp (8891UL) enable**, **GPIO13 = mute/pop strobe** paired with the amp,
**GPIO12 = periodic keep-alive pulse** during playback. There is **no external codec on the
2N** (`g_codec_type @0x08008c18 == 0`; the type-1/3 branches drive an AK-combo-chip codec over
I²C on other hardware variants and are dead here). **[Proven]**

```
play_media / fwl_play_voice_by_id                      (media-pipeline.md, system-voice-feedback.md)
  └ play_media_setup 0x080ab6ac
      ├ fwl_voice_stop_sync (stop current)
      ├ aud_player_construct 0x080ab47c   → dac_bringup + aud_player_alloc (ring built here)
      ├ audio_unmute_req    0x080ab5dc    → GPIO13=0, request amp-on (flag bit2)
      └ aud_player_play 0x08032c94 → medialib_play → FUN_08032c04 (kick: 0x08008c60 |= 5)
event loop: audio_wait_tick 0x0800b2ec  (headphone GPIO7 / amp GPIO16 arbitration)
  └ aud_task_tick 0x0800b71c
      ├ out-SM FUN_0800b9f8 → mp_pump_state1 0x0800be78 → decode → ao_mix_write_ring 0x08022c38
      └ if (0x08008c60 bit0) hal_ao_submit_tick (rt 0x08003a74): ring chunk → virt2phys → DMA
IRQ line 0 (0xcc bit0) → hal_audio_irq_handler (rt 0x080039d4): clear kick, resubmit next chunk
```

---

## 1. The PCM ring singleton — `*0x08008d2c` → `0x08008d30` [Proven]

Built once by **`audio_ring_build` (0x08022aa0)** (via `audio_ring_setup` 0x080223a8, from the
`aud_player_alloc` descriptor): memset 0x4c, copy the 5-word descriptor (stop-cb, free-cb,
**alloc-cb**, …), then `+0x44 = alloc_cb()` — the PCM buffer, from the physical pool
(`physpool_alloc` 0x08038ce4). Field map (byte offsets from `0x08008d30`):

| off | field | writer / reader |
|---|---|---|
| +0x00..+0x13 | descriptor copy (callbacks: +0x04 free, +0x08 alloc, +0x0c used by teardown `thunk_FUN_08022ba8`) | `audio_ring_build` |
| **+0x14** | **volume multiplier, Q10** (`0x400` = unity, clamp `0x400`) | `sm_set_sound_profile` 0x0800b65c → `audio_gain_commit` 0x0802265c; read per sample by `ao_mix_write_ring` |
| +0x18 | volume shadow (skipped when fade flag +0x27 set) | `audio_gain_commit` |
| +0x20 | consumed-chunk counter | `hal_ao_get_chunk` ++; cleared by `medialib_open_ring_check(0/5)` |
| +0x24/+0x25/+0x26 | ring state bytes (open=1 / pause / eof-mark) | `medialib_open_ring_check` 0x08022f0c cmds 0–4 |
| +0x27 | fade flag | volume path |
| +0x2c/+0x30 | timeout-ish defaults, both init **0x5dc** (1500) | `audio_ring_build` |
| **+0x34** | underrun **zero-fill flag** (cmd 6) | set by `medialib_open_ring_check(6)`; consumed by `hal_ao_get_chunk` |
| **+0x38** | ring **read** ptr (byte offset; +0x400/chunk, wraps at +0x40) | `hal_ao_get_chunk` |
| **+0x3c** | ring **write** ptr | `ao_mix_write_ring` |
| **+0x40** | ring size = **0x3000** (12 KB ≈ 139 ms @22050 stereo S16) | `audio_ring_build` (`puVar2[0x10]=0x3000`) |
| **+0x44** | ring **base** (physpool PCM buffer) | `audio_ring_build` |
| +0x48 (u16) | source channels | `ao_set_fmt` 0x08022c14 ← `aud_set_format` |
| +0x4a (u16) | source bits | ditto |

**Mixer `ao_mix_write_ring` (0x08022c38)** [Proven, decomp verbatim]: for every 16-bit input
sample `s`: `out = (s * (ring[+0x14] & 0xffff) + round) >> 10` (arith-rounded toward 0);
**mono sources (`+0x48 != 2`) are written twice (L=R)** → the ring is always
**stereo-interleaved S16LE**. Handles the wrap split; advances +0x3c. So **the ring content is
post-volume** — a capture at the DMA sink already includes the user volume.

**Volume plumbing** (cross-ref `settings-config.md`): user volume index 0..5 @`0x081db904` →
`sm_set_sound_profile` gains `{0x18,0x60,0xA8,0x108,0x138,0x180,0}` → `audio_gain_commit`
0x0802265c (optional rate-dependent rescale via the `0x08122710` pair, clamp 0x400) →
ring `+0x14`. Boot default idx 3 = **0x108/0x400 ≈ 25.8 %**. A second, coarser volume
(`aud_player_set_volume` 0x080abb60, 0..0x10, default 8) feeds the **medialib decoder-level**
gain via LUT @`0x08031e66` (`{0,0,1,1,2,2,3,4,5,6,7,8,9,a,b,c,c}` for the internal-DAC row) —
only for non-0x13 codecs. **[Proven]**

---

## 2. Producer side — decode pump into the ring [Proven]

`aud_player_play` → `FUN_08032c04` (kick: `*0x08008c60 |= 1|2`, then one `aud_task_tick`).
Every `audio_wait_tick` (0x0800b2ec, the event-loop pacer) calls through the callback pair at
`0x081db93c` into **`aud_task_tick` (0x0800b71c)**:

1. run the **output state machine `FUN_0800b9f8`** (state byte obj+0x30: 1=decode → calls
   **`mp_pump_state1` 0x0800be78**, 2/3=flush, 4/5=stop path, 6=error);
2. `mp_pump_state1`: `ao_pull_decoder` (0x0802220c, decoder cb obj+0x74 → PCM scratch at
   player+0x88) → optional in-line DSP stages (obj+0x190/+0x194/+0x198, generic stage objects
   built by `FUN_0801ed14`, run by `FUN_0801edc0` — EQ/effect hooks, **not** a resampler; unused
   in the tiptoi flow) → **`ao_mix_write_ring`**;
3. if `*0x08008c60 bit0` (chain idle): **`hal_ao_submit_tick`** (rt 0x08003a74) — submit the
   next chunk; on success, if bit2 was set, clear it and run the **codec-bias ramp**
   `FUN_0800b284(1)` (analog un-pop, §4), then `hal_audio_clk_enable(0x3e)` (rt 0x08003d18).

`is_audio_playing` (0x0800b024) = player exists ∧ status bit7 set ∧ bit4 clear.

---

## 3. The audio DMA engine — MMIO `0x04010000` [Proven]

All from **`hal_dma_submit`** (static **0x07ffb530** = rt **0x08003530**; wrapper rt
0x08003c2c = `hal_dma_submit(phys, 1, len)`), plus the HAL init write.

| reg | role |
|---|---|
| `0x04010000` | control. HAL init writes **`0x00620024`** (rt-init, static `0x07ffb420` thunk). **bit16 = kick/GO** for peripheral-port transfers (`\|=0x10000` after programming; cleared by the DMA-done ISR). |
| `0x04010004` | **source**. Memory: `phys & 0x3ffff` (18-bit). Port code <10: `port_addr \| 0x8080000`. |
| `0x04010008` | **destination**, same encoding. **Port 1 = DAC** (`0x8080000\|0x6200`). |
| `0x0401000c` | **word-count + START/BUSY**: `((len<<19)>>21) \| 0x2000` = (len/4, 11 bits) with **bit13 = START/BUSY**. Poll `while (reg & 0x2000)` before programming (slot free) and, for mem-to-mem, after (completion). |
| `0x04010010` | status word read by the poll helper `0x07ffb484` (per-port bits). |

**Port-code table** (mapper static `0x07ffb4b4` = rt 0x080034b4): 0→`0x6000`, **1→`0x6200`
(DAC out)**, 2→`0x6400`, 3→`0x6600`, 4/9/default→`0x6800`, 5→`0x6a00`, 6→`0x6a40`, 7→`0x6a80`,
8→`0x6ac0`. Port transfers are **asynchronous** (program regs, set ctrl bit16, return);
memory-to-memory transfers spin on bit13 + do a cache op. Before launching, `hal_dma_submit`
records a **transaction tag at `0x08007e78`**: `0x75` = source-port-0 transfer (ADC/mic
capture), `0x76` = **dest-port-1 = DAC playback** — the DMA-done ISR dispatches on this tag.

**Audio submission** — `hal_ao_submit_tick` (static 0x07ffba74 = rt 0x08003a74):
`hal_ao_get_chunk` (trampoline static 0x07ffec58 → **0x07ffbdbc** = rt 0x08003dbc) →
`hal_virt_to_phys` (rt 0x08001990, page tables 0x08004000/0x08004400) → `hal_dma_submit(phys,
1, len)` → flag-set `func(0)` (rt 0x080035f8: **clear `0x08008c60` bit0, set bit2** =
"chain active").

**`hal_ao_get_chunk`** [Proven, disasm]: returns `(base+readptr, 0x400)` and advances
`+0x38` (wrap at `+0x40`), `+0x20`++. If fewer than 0x400 bytes available: with the zero-fill
flag `+0x34` set → **zero-fills the partial chunk to 0x400** (pads with silence, sets
`+0x3c = +0x38`, clears the flag); without it → **submits the partial length** (`avail`) if
nonzero, else returns 0 (chain stops). Each 0x400 chunk = **256 stereo frames ≈ 11.6 ms @
22050 Hz**.

**[Inferred]** The 18-bit source-address field + the `0x8080000` port alias imply the DMA
addresses a 256 KB physical window (internal SRAM view); the ring's physpool block must
therefore land inside it. The exact window base is not byte-proven — it can be verified at
runtime by reading `0x04010004` after a submit (open item).

---

## 4. The internal DAC + clocks [Proven]

No external codec: `g_codec_type` **@0x08008c18** (read by `FUN_08031f6c`) is 0 on the 2N
(`oid_sensor_enable` 0x080ef114 writes 0); values 1/3 select the AK-combo-chip codec paths
(`FUN_080ef028/09c/2f4/3c8`, I²C dev 0x94) — dead code here.

| register | field(s) | function |
|---|---|---|
| `0x04080000` | **bit0 = DAC enable** | `dac_bringup` sets; teardown clears; `dac_set_rate` pulses 0→1 around a retune |
| `0x04000008` (`REG_CLK_AUDIO`) | bit24 = DAC-clk enable; **bits[20:13] = rate divider code** (+ latch bit21); bit25/bit12/23 = ADC/mic & codec-type-3 paths | `dac_set_rate`, `dac_bringup` |
| `0x04000064` (`REG_ADC_CTRL`) | **bit13 = DAC enable (analog)**; **bits[16:14] = OSR index** | `dac_bringup` (`\|0x2000`), `dac_set_rate` |
| `0x04000010` (`REG_ANALOG_PD`) | **bit9 = DAC power-down** (cleared on bring-up, set on teardown) | `dac_bringup` / `FUN_080333fc` |
| `0x0400005c` (`REG_CODEC_BIAS`) | bits[15:13] ramp step, [22:21], composite `0xE1190C1F` | `FUN_0800b284` bias ramp, `FUN_08033004` |
| `0x04000068` (`REG_CODEC_VOL`) | bits[16:15] et al. path enables | bias ramp / `FUN_08050898` |
| `0x04036000/+4` | audio/DAC clock block: `[0]\|=0x10000000; [4]\|=0x10010; wait [4]&0x80000` | `hal_audio_clk_enable` (rt 0x08003d18; guarded by `*0x08008d1c==0`) |
| `0x0400000c` bit5 | DAC/audio module clock-gate | `hal_clkgate_set(5,1/0)` in bring-up/teardown |

**Bring-up** `dac_bringup` (0x08033334): `*0x08008c90 = 1` (DAC-powered flag) → clkgate(5,on)
→ bias fields off → `0x04080000 |= 1` → `0x04000064 |= 0x2000` → `0x04000008 |= 0x1200000` →
`0x04000010 &= ~0x200` → `dac_set_rate(8000)` (default; every track retunes). Callers:
`aud_player_construct`, `FUN_080322a0` (session open, also `*0x08008c61=1`), `FUN_08050898`
(USB-side codec reinit). **Teardown** `FUN_080333fc` / `FUN_08032d38`: inverse writes +
clkgate(5,off), `*0x08008c90 = 0`.

**Sample rate — retuned per track, no software resampler.** `aud_set_format` (0x080324d0):
`medialib_get_info` → `{rate u32, ch u16, bits u16}` → `ao_set_fmt(ch,bits)` (ring +0x48/+0x4a)
→ **`dac_set_rate(&rate)`** (0x08033258): `FUN_08032f34` searches OSR index 0..7 ×
divider code 0..0xff for the closest `rate ≈ PLL/(div+1)/OSR[i]` with the master clock capped
at **14 MHz** (`0xd59f80`), **OSR table @0x08033628 = {256,272,264,248,240,136,128,120}**;
then pulses `0x04080000` bit0, writes `0x04000064[16:14] = osr_idx` and
`0x04000008[20:13] = div | latch 0x200000`, waits, sets bit24. Observed: **22050 Hz → divider
code 0x46** (prior doc, Proven). Mixed-rate system voices (16 k/32 k/44.1 k WAVs,
`system-voice-feedback.md`) work the same way — the DAC clock follows each file's header.

**Bias/pop ramp `FUN_0800b284(on)`**: state byte `0x08008c62`; ramps `0x0400005c` bits[15:13]
(step codes 1..4) with `0x04000068` bit16, 1 ms delay — run on the **first chunk after a kick**
(from `aud_task_tick`) and inversely at stop. **Silence flush `FUN_08032eb0(val)`**: fills a
0x800-byte physpool buffer with a constant sample, sets **`*0x08008c91 = 1`** ("swallow next
DAC done — do not chain"), submits it directly via `hal_dma_submit(phys,1,0x800)`, and spins
until the ISR clears the flag — the DAC is fed defined silence before analog power-down
(pop suppression). Called from `FUN_08033004(0)` teardown path.

---

## 5. The DMA-done IRQ (top-level line 0) and the refill [Proven]

Wiring (`soc-core-registers.md` §4): `irq_vector_handler` (static 0x08000110) computes
`PENDING(0x040000cc) & ENABLE(0x04000034)`; **bit0 → `hal_audio_irq_handler`** (static
**0x07ffb9d4** = rt **0x080039d4**). INT_ENABLE bit0 is toggled by rt **0x08003430** (only
PROG caller: `usb_detect_disable` 0x080413fc turns it *off*; the enable happens during HAL
init **[Inferred]**).

Handler body (disasm, byte-proven):

```
if (check_0x07ff90a8() != 0) return;          // spurious / not ours
[0x04010000] &= ~0x10000;                     // clear the DMA kick/GO bit
tag = *0x08007e78;                            // 0x75 = ADC-capture, 0x76 = DAC-playback
if (tag == 0x75) {                            // mic path: clear 0x08008c60 bit2,
    ... ; call PROG 0x080a757c; return; }     //  notify the recorder
if (tag != 0x76) return;
if (*0x08008c91) { *0x08008c91 = 0; return; } // silence-flush handshake: swallow, no chain
if (*(void**)0x08008c64) tailcall it;         // optional override callback
if (hal_ao_submit_tick() == 0)                // ring has more data? -> next chunk is already
    audio_flag(1);                            //   in flight. Drained -> set 0x08008c60 bit0
                                              //   (chain idle; PROG tick must re-kick),
                                              //   clear bit2 (chain active)
```

So the steady state is **ISR-chained**: DAC-done → dequeue+submit the next 0x400 chunk in
interrupt context, ~86 IRQs/s at 22050 stereo. The PROG-side `aud_task_tick` only restarts the
chain (bit0) after the ring ran dry or at play-start (`FUN_08032c04` sets bit0|bit2 manually).

**Audio flag byte `0x08008c60`** (aka `g_voice_busy_flags`): **bit0** = chain idle/needs kick;
**bit2** = chain active (stop paths spin on it: `FUN_080abbb0`/`fwl_voice_stop_sync` do
`while (*0x08008c60 & 4) audio_wait_tick();` and the DAC teardown `FUN_08032cb4` busy-waits it
raw). `0x08008c61` = DAC session open; `0x08008c62` = bias ramped; `0x08008c64` = ISR override
callback ptr; `0x08008c90` = DAC powered; `0x08008c91` = swallow-one-done flag. The `0x3e`
event-id record at `0x08006a80` (+ `0x08006abc=0`) is written by `hal_audio_clk_enable` and
**never consumed** anywhere in the corpus — vestigial. **[Proven, pool-resolved]**

---

## 6. Amp / mute GPIO sequencing [Proven ops; roles partly Inferred]

Three output pins (all via `hal_gpio_write` 0x08007734 / `hal_gpio_set_dir` 0x0800774c,
registers `0x04000080`/`0x0400007c`):

| pin | boot | role |
|---|---|---|
| **GPIO16** | out, **0** (`clock_periph_init` 0x08039628) | **speaker amp enable (8891UL), 1 = on** [Proven] |
| **GPIO13** | out, **1** | **mute / pop-suppression strobe, 1 = muted** — always toggled in lock-step with the amp [Proven ops; "mute" role Inferred from ordering] |
| **GPIO12** | out, **0** (`app_init_main`, `splash_entry`) | **keep-alive pulse**: while `is_audio_playing`, every 2nd heartbeat (event 0x1046) drive **0 for 0x28 ms then 1** (`heartbeat_1046_handler` 0x0800acfc); same 0→40 ms→1 pulse in the USB codec-reinit `FUN_08050898`; driven 1 in the USB state (`usb_state_dbg` 0x08050fb8). [Proven ops; role Inferred — external charge-pump/watchdog refresh, *not* a simple mute] |
| GPIO7 | in | headphone detect, 1 = plugged → amp forced off each `audio_wait_tick` [Proven] |

**Amp state machine** — flag byte **`0x081db221`** (`DAT_080ab3c0+1`): bit0 = muted (GPIO13
high) latched, bit1 = amp currently on, bit2 = amp-on request pending.

- **Start of playback** (`play_media_setup`, also `splash_entry`, `FUN_080aba54` resume):
  **`audio_unmute_req` = `FUN_080ab5dc`**: if muted → **GPIO13 := 0** (unmute), set bit2
  (request). The amp itself turns on in the next **`audio_wait_tick`**:
  `if (gpio_read(7)==1) audio_amp_disable(); else if (audio_amp_pending()) audio_amp_enable()`
  — **`audio_amp_enable` (0x080abb0c): GPIO16 := 1** (idempotent via bit1). So the order is
  *unmute → data flowing → amp on*, and headphones always win.
- **Stop / teardown** (~50 call sites: `fwl_voice_stop_sync` 0x080ab620, `fwl_voice_stop_close`
  0x080ab500, `FUN_080abbb0`, power-off, USB entry, standby): wait for the DMA chain to drain
  (`0x08008c60` bit2), then **`audio_mute` = `FUN_080ab424`**: **GPIO13 := 1**, delay **1 ms**,
  **`audio_amp_disable` (0x080ab3ec): GPIO16 := 0**, set bit0/clear bit2 — i.e.
  *mute first, 1 ms, then amp off* (pop-free).
- Deeper teardown additionally plays the **silence flush** (§4) and drops the DAC/bias
  (`FUN_08032d38`/`FUN_08032cb4`): spin bit2 → bias ramp down → `0x04080000 &= ~1` →
  analog PD bit9.

---

## 7. The audio-out boundary in one place

Pulling §§1–6 together, the observable hardware boundary a decoded track crosses is:

- **The bytes on the wire** are the ring contents at the DMA dequeue: **post-volume, S16LE,
  stereo-interleaved** (mono sources are duplicated L=R; the Q10 gain at ring `+0x14` is already
  applied by `ao_mix_write_ring`). A tap at the DMA sink is exactly what the DAC receives.
- **The transfer** is the `0x04010000` engine: `0x04010004` source = `phys & 0x3ffff`,
  `0x04010008` dest = port 1 (`0x8080000|0x6200` = DAC), `0x0401000c` = `(len/4)|0x2000` with
  bit13 START/BUSY, control bit16 = GO. Each transaction is 0x400 bytes = 256 stereo frames.
- **Completion** raises top-level IRQ line 0 (`0x040000cc` bit0, gated by `0x04000034` bit0);
  the firmware's own `hal_audio_irq_handler` clears `0x04010000` bit16 and self-chains the next
  chunk.
- **Sample rate** is not fixed: it follows each track's header via `dac_set_rate`, encoded as
  `0x04000008` bits[20:13] (divider) + `0x04000064` bits[16:14] (OSR index into
  `{256,272,264,248,240,136,128,120}`, master clock ≤ 14 MHz). System voices are 16 k/32 k/44.1 k.
- **Audibility** is gated by GPIO16 (amp enable, 1 = on) and GPIO13 (mute, 0 = unmuted), with
  GPIO7 (headphone detect) forcing the amp off; the audio-clock block `0x04036000[4]` reads
  bit19 set once enabled.

(These behaviours were confirmed dynamically against a DAC capture in
[tt-emu](https://github.com/nomeata/tt-emu).)

---

## 8. Names / docstrings (names.csv candidates)

| addr (runtime) | name | docstring |
|---|---|---|
| 0x08003530 (st 0x07ffb530) | `hal_dma_submit` (keep) | `(src, dst, len)`: <10 = port code (1=DAC@0x6200, tag 0x76→0x08007e78; 0=capture, tag 0x75), else `addr&0x3ffff`; regs 0x04010004/08/0c(len/4\|0x2000); port xfer async via ctrl bit16, mem xfer spins bit13. |
| 0x080034b4 (st 0x07ffb4b4) | `hal_dma_port_addr` | port code → engine address: 0..9 → 0x6000/0x6200(DAC)/0x6400/0x6600/0x6800/0x6a00/0x6a40/0x6a80/0x6ac0. |
| 0x08003a74 (st 0x07ffba74) | `hal_ao_submit_tick` (keep) | get ring chunk → virt2phys → `hal_dma_submit(·,1,·)` → flags(0): 0x08008c60 bit0←0, bit2←1. Ret 1 = submitted. |
| 0x08003dbc (st 0x07ffbdbc) | `hal_ao_get_chunk` (keep) | dequeue ≤0x400 B from ring `*0x08008d2c`: +0x38 read ptr, +0x40 wrap, +0x20 count; +0x34 set → zero-pad partial chunk (silence). |
| 0x080039d4 (st 0x07ffb9d4) | `hal_audio_irq_handler` (keep) | IRQ line 0: clear 0x04010000 bit16; tag 0x76 → (0x08008c91 swallow) / (cb @0x08008c64) / resubmit next chunk, on drain set 0x08008c60 bit0; tag 0x75 → mic-capture notify. |
| 0x08003430 (st 0x07ffb430) | `hal_audio_irq_gate` | set/clear INT_ENABLE `0x04000034` bit0 (audio DMA-done line 0). |
| 0x08003d18 (st 0x07ffbd18) | `hal_audio_clk_enable` (was hal_audio_irq_enable) | guard `*0x08008d1c`; record event-id byte (0x3e, never consumed) @0x08006a80; `0x04036000\|=0x10000000, +4\|=0x10010`, wait bit19. |
| 0x080035f8 (st 0x07ffb5f8) | `hal_ao_flags` | arg0: 0=submit (bit0←0,bit2←1), 1=drained (bit0←1,bit2←0) on `0x08008c60`. |
| 0x0800b71c | `aud_task_tick` (keep) | per-tick: out-SM (or 0x0800ca14 for codec 0x13) + re-kick submit when 0x08008c60 bit0; first-chunk bias ramp. |
| 0x0800b284 | `codec_bias_ramp` | (on): 0x0400005c bias-step ramp + 0x04000068 paths; state @0x08008c62; anti-pop. |
| 0x08032eb0 | `dac_flush_silence` | fill 0x800 B with const sample, `*0x08008c91=1`, direct DMA to DAC, spin until ISR clears — pre-teardown pop kill. |
| 0x08032cb4 | `aud_player_prestop` | mark repeat/stop state; codec!=0x13 path: drain (spin 0x08008c60 bit2) + DAC/bias teardown + `*0x08008c61=0`. |
| 0x080322a0 | `dac_session_open` | clk boost + `dac_bringup` + `*0x08008c61=1` (refcounted clock). |
| 0x08022aa0 | `audio_ring_build` (keep) | build singleton *0x08008d2c: size 0x3000, +0x44 = alloc-cb PCM buf, defaults 0x5dc. |
| 0x08022c38 | `ao_mix_write_ring` (keep) | Q10 volume (+0x14) scale, mono→stereo dup, S16 into ring, advance +0x3c. |
| 0x0802265c | `audio_gain_commit` (keep) | rate-rescale (pair @0x08122710), clamp 0x400 → ring +0x14/+0x18. |
| 0x08033258 / 0x08033334 / 0x080333fc | `dac_set_rate` / `dac_bringup` / `dac_teardown` | divider(0x04000008[20:13]) + OSR(0x04000064[16:14], table @0x08033628 {256,272,264,248,240,136,128,120}, mclk ≤ 14 MHz) / enable / disable of 0x04080000+clocks+PD bit9. |
| 0x08032f34 | `dac_rate_search` | closest-match (osr_idx, div_code) for a requested rate. |
| 0x080324d0 | `aud_set_format` (keep) | header → `ao_set_fmt(ch,bits)` + `dac_set_rate(rate)` — per-track HW retune, no SW resampler. |
| 0x080ab5dc | `audio_unmute_req` | GPIO13←0 (unmute) + amp-on request (0x081db221 bit2). |
| 0x080ab424 | `audio_mute` | GPIO13←1, 1 ms, `audio_amp_disable` (mute-before-amp-off). |
| 0x080abb0c / 0x080ab3ec / 0x080abb50 | `audio_amp_enable/_disable/_pending` (keep) | GPIO16 amp; state/request bits in 0x081db221. |
| 0x0800acfc | `heartbeat_1046_handler` (keep) | + GPIO12 keep-alive: 40 ms low pulse every 2nd heartbeat while playing. |
| globals | | `0x08008c60` audio flags (bit0 idle-kick, bit2 chain-active); `0x08008c61` DAC session; `0x08008c62` bias state; `0x08008c64` ISR override cb; `0x08008c90` DAC powered; `0x08008c91` swallow-one-done; `0x08007e78` DMA tag (0x75/0x76); `0x08006a80` dead event-id record; `0x08008c18` g_codec_type (0=internal DAC); `0x081db221` amp/mute flags; `0x08122710` gain-rescale pair; `0x081db93c` tick-callback pair; `0x08008d2c/0x08008d30` ring singleton. |

---

## 9. Open items

- **[Inferred]** The DMA 18-bit address window base (how `0x04010004 & 0x3ffff` maps back to a
  CPU address) — read the register after a live submit.
- **[Inferred]** Who sets INT_ENABLE bit0 initially (rt 0x08003430 callers in the relocated
  HAL are not statically enumerable); confirm at runtime.
- **[Inferred]** GPIO12's electrical role (keep-alive pulse cadence is proven; the consumer —
  charge pump vs. external watchdog on the amp board — is not identifiable statically).
- The exact divider arithmetic of `FUN_08032f34` vs. the proven 22050→0x46 data point (the
  PLL-source constant `FUN_080090f4` returns wasn't chased); the rate is recoverable by the
  inverse table lookup.
- `FUN_08033588` (rate → double math with 0.5/750.0 constants, called inside `dac_set_rate`)
  — settle-delay/level helper [Inferred].
