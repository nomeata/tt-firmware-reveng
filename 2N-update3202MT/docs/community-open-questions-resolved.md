# Community open questions resolved by the 2N-MT firmware decomp

> **Purpose.** The public tip-toi-reveng (tttool) reverse engineering was done almost entirely
> **black-box** — staring at `.gme` hex, sniffing wires, poking pens — without the decompiled pen
> firmware. This project has the firmware **decompiled and named**
> (`out/decomp/`, unified base **0x08009000**; resident nandboot at 0x08000000). This doc
> walks the tip-toi-reveng **wiki** and **`GME-Format.md`** and, for every item the community marked
> *unknown / unclear / TODO / "not sure" / "probably" / "?"*, records whether our decomp resolves it,
> with the firmware address/evidence and a **Proven / Inferred / Still-open** verdict.
>
> Evidence tags: **[Proven]** = read directly from decomp/disasm/bytes; **[Inferred]** = deduced
> with the reason given; **[Open]** = our asset does not settle it (hardware we lack, or a
> different pen generation than the 2N-MT).
>
> The community questions are drawn from the tip-toi-reveng wiki and `GME-Format.md`
> (https://github.com/entropia/tip-toi-reveng). Companion docs cited inline live in this `docs/` dir.

---

## 0. Scorecard

Community-flagged unknowns catalogued: **38**.

| verdict | count | |
|---|---|---|
| **Proven** (firmware settles it) | **25** | |
| **Inferred** (strongly, from the code) | **6** | |
| **Still-open** | **7** | other pen generations, the Sonix ASIC scramble, and a few content-internal residuals |

**Bonus:** the decomp also surfaced **5 format facts tttool does not have at all** (opcodes 0xFFA1 /
0xFFFC / 0xFEFF / 0xFEE0–0xFEE7, and the game-type-12 dual playlist) — potential upstream additions.

### The five most valuable resolutions
1. **The raw→real XOR "magic" is a 256-byte firmware LUT @0x080381da** — tttool's single biggest
   GME mystery (they empirically collected ~55 raw/real pairs). `real = LUT[raw]`, extractable
   whole. **[Proven]**
2. **The raw→OID transform lives entirely in the Sonix ASIC — the firmware does NO descramble.**
   The 18-bit index field on the two-wire link *is* the GME code. Settles where tttool's
   `KnownCodes.hs` scramble physically lives. **[Proven]**
3. **The GME "additional script table" (header 0x0C, "purpose unknown / TODO when executed") is the
   GME software-timer script**, re-entered on opcode-0xFE00 timer expiry. **[Proven]**
4. **The "encryption procedure" test = the ZC90B anti-clone auth chip** (not the `TestFile` dir the
   wiki guessed) — full challenge/response cracked, three S-boxes extracted. **[Proven]**
5. **Script opcodes FE00/FF00 decoded:** FF00 = Rand-to-register (`tick % (m+1)`), FE00 = arm
   periodic GME timer, FEFF = cancel. **[Proven]**

---

## 1. GME format

| # | Community unknown | Source (wiki) | Our resolution (decomp addr / evidence) | Verdict |
|---|---|---|---|---|
| 1 | Header **0x1C** "raw XOR value … might use some algorithm or a lookup table" to derive the real media XOR | GME-Media-file-table; GME-Header | **256-byte firmware ROM LUT @0x080381da**: `*0x08121a80 = *(u8*)(0x080381da + rawXor)`, set in `gme_parse_header` @0x08035d20 & `gme_oid_dispatch`. Verified `LUT[0x39]=0xAD`. File-offset = `0x080381da − 0x08009000`. See gme-format-completeness.md §4.1. | **[Proven]** |
| 2 | Header **0x0C** additional-script table "Purpose unknown"; GME-Additional-script-table "**TODO: When is this executed?**" | GME-Header; GME-Additional-script-table | It is the **GME software-timer script**. Opcode **0xFE00** arms a periodic sw-timer (`gme_exec_command` @0x08034da0; `hal_timer_register`), whose expiry posts event **0x30**; `gme_oid_dispatch` @0x0803629c → `gme_parse_additional_script` @0x08036228 evaluates the 0x0C table and runs the first line whose conditions hold. gme-timer-counter-random.md §4. | **[Proven]** |
| 3 | Script-line: "**Currently unknown commands are FE00 and FF00** … FE00 seems to have something to do with timers and random numbers" | GME-Script-line | **FF00 = Rand-to-register**: `reg = tick@0x08008d24 % (m+1)`; firmware's own debug string is `"Rand(%d) = %d."`. **FE00 = arm periodic GME timer** (delay `m*100`, auto-reload). Both in `gme_exec_command` @0x08034da0. gme-timer §3a/§4. | **[Proven]** |
| 4 | Script-line "**Missing commands**: random number generation, timer programming, loops, play-from-register, sleep" | GME-Script-line | **random = FF00 (tick%) + FC00 (`gme_rand_in_range` @0x08034d74, heartbeat%)**; **timer = FE00/FEFF**. No dedicated *loop*/*sleep* opcode exists — those are built from the timer + register conditionals on the additional-script table. "play-from-register" is the register-operand mode of every play opcode. gme-timer §3–5. | **[Proven]** (random/timer) / **[Inferred]** (no loop/sleep opcode) |
| 5 | Header **0x8C** "offset to a table of 32-bit values, looks like a special media flag table?" — GME-Media-flag-table "**Purpose unknown**" | GME-Header; GME-Media-flag-table | **Per-media "VoiceNumberNeedRep" repeat-suppression list.** `play_media` seeks header 0x8C (`fs_seek(handle,0x8c,0)`) and consults the flag per media index to suppress immediate repeats. media-pipeline.md §; gme-format-completeness.md §4. | **[Proven]** (mechanism); exact per-title semantics **[Inferred]** |
| 6 | Header **0x60** "offset to an additional media file table"; GME-Media-file-table "**purpose of the additional media file table is unknown**"; GME-Additional-media-file-table "Suspicion this is for older pens" | GME-Header; GME-Media-file-table | **Additional media table @0x081da1b8**, consumed by the **native (built-in game-type) handlers** on *this* pen (e.g. 0x0807ba04) — not an "older pens" leftover. gme-format-completeness.md §4 (hdr 0x60 row). | **[Proven]** it is used here / **[Inferred]** exact division of labour vs main table |
| 7 | Header language string "**it is unclear where it is checked**" | GME-Header | Checked by **`gme_check_language` @0x08033fb0** (compares hdr 0x59 field to pen language @0x08121ec0) inside **`gme_mount_check_product` @0x08034130** at mount time. book-discovery-and-load.md §; matches wiki "not the .tiptoi.log". | **[Proven]** |
| 8 | Binary-table slots 0x90/0x98/0xA0/0xA8/0xC8/0xCC "**probably** the games/main binary for ZC3201 / ZC3202N / ZC3203L" | GME-Header | Confirmed mapping for the MT (ZC3202N) pen: it reads **0x98 (embedded game binaries) + 0xA8 (main binary)** via `gme_read_main_binary_table` @0x080aadd8 → `gme_launch_binary_build_sysapi` @0x080aa934 (builds a ~90-entry system_api vtable, jumps to the ARM blob). 0xA0/0xC8/0xCC are the ZC3201/ZC3203L slots — *not* the MT pen's. gme-format-completeness.md §2/§4. | **[Proven]** (which slots this SoC uses) |
| 9 | Header **0x08 = 0x238B** "Maybe this is the Ravensburger customer number at Chomptech" | GME-Header | It is the **GME validity magic**, checked at mount: `hdr[+8]==0x238B` in `gme_mount_check_product` @0x08034130 (all mount/first-load paths). Whether the *number itself* is a Chomptech customer id is not observable in firmware. | **[Proven]** (role = mount magic) / **[Open]** (customer-number semantics) |
| 10 | GME **checksum** "simple additive check-sum … which is **not checked by the pen**" | GME-Checksum | Confirmed: the mount path validates only the **0x238B magic + language + product id** — no trailing-checksum verification anywhere in `gme_mount_check_product`/`gme_parse_header`. (Contrast: the `.upd` container *is* checksummed, §3.) | **[Inferred]** (consistent; absence of a check) |
| 11 | Special-symbols **a/b/c/d/e/f/g** (Restart / Stop / Skip / unknown…) | GME-Special-symbols | Header **0x94 = special-OID list, 20×u16** → cover-OID selectors @0x081da714/716/718… (`gme_parse_header`). Of the 20 words the native firmware consumes exactly three: **a** and **c** are the two control symbols exempted from content-tap recording, and **f** = replay-recording enable (settles the wiki's "0 or 1, always 1 when g≠0"). **b/d/e/g** and the 13 "padding" words are not read natively — they are exported to the embedded game binaries via the system_api as a per-title parameter block. gme-subtype-parameters.md §9; gme-format-completeness.md §4 (hdr 0x94). | **[Proven]** (a/c/f native use) / **[Open]** (b/d/e/g per-title semantics live in the shipped binaries) |
| 12 | Subgames **u0–u9** (ten 16-bit "unknown" fields); Games **i/q/x/w/v** unknown 16-bit fields | GME-Subgames; GME-Games | **Decoded** — field-annotated in gme-subtype-parameters.md. Games: **c** = multi-target "find-all" round flag, **i/q** = type-6's find-all flag + bonus-stage earlyRounds, **x/w/v** = read-and-discarded authoring-tool metadata. Subgames: **u0–u5** carry proven gameplay roles (correct/wrong feedback index modes, wrong-tap limits, in-order match gate; u5 also live as time-limit/target-count in types 19/20), **u6–u9** read-and-discarded. Game-type dispatch (types 1–23, 253) fully mapped to statechart leaves (`FUN_08034cbc` + `gme_oid_dispatch`, statechart-full-map.md §2). Residual [Open] confined to the embedded-binary per-title parameter block and type-9's per-slot native cue-pool catalogue (§5). | **[Proven]** (static decomp) |
| 13 | Header **0x9C** "purpose unknown, always 0"; **0x1D** "three bytes with unknown meaning, 0 for all products" | GME-Header | `gme_parse_header` does not consume 0x9C or 0x1D as live inputs on the MT pen (0x9C aligns with the ZC3201 binary-table region; 0x1D is padding after the raw-XOR byte). No firmware read observed. | **[Inferred]** (unused here) |

### Bonus — format facts tttool does not have (upstream candidates)
| item | evidence |
|---|---|
| Opcode **0xFFA1** = coin-flip play: at its own slot, iff the free-running book-event counter (ctx+0x125) is even, play `playlist[m]` immediately (same path as `P(m)`); the latch (ctx+0xddf/+0xde0) is only a replay record for replay-after-interruption walks; odd = no-op | `gme_exec_command` @0x08034da0 (handler @0x080352cc); gme-format-completeness §1 |
| Opcode **0xFEFF** = cancel GME timer; **0xFEE0–0xFEE7** = `sm_set_sound_profile(0..7)` | same |
| Conditional **0xFFFC** = Eq alias (`case 0xfffc` shares the Eq compare) | `gme_check_condition` @0x08035624 |
| **Game-type 12 carries a *second* playlist** (`gme_parse_media_offsets2` @0x08035b8c, `gme_parse_playlist` second half) | gme-format-completeness §3.1 |
| Actions are **clamped to 8 per script line** (count @0x081da0a8) | `gme_parse_actions` @0x080354c8 |

---

## 2. OID encoding / decoding

| # | Community unknown | Source (wiki) | Our resolution (decomp addr / evidence) | Verdict |
|---|---|---|---|---|
| 14 | "The raw value of the integer is **obfuscated by using a mapping table / an offset**" (the raw→OID transform) | PEN-Optical-ID-and-codes | **The firmware performs NO raw→OID transform.** The Sonix OID-decoder ASIC does the entire image→pattern→index decode and outputs the finished index over the two-wire link; firmware only frames/masks it (`>>9`, `&0x3FFFF`, store `&0xFFFF`) and compares it verbatim to GME codes. So tttool's `KnownCodes.hs` scramble lives **inside the Sonix DSP / printed-pattern domain**, not in firmware. oid-sensor-read-protocol.md §0/§6. | **[Proven]** (firmware side); ASIC internal map **[Open]** (hardware we don't have) |
| 15 | OID "top-left pixel works as a **checksum/parity bit**" — how the check is formed | PEN-Optical-ID-and-codes | Firmware's frame **check byte** rule: `(b & 0xF) + (b>>4) + 1 ≡ 0 (mod 16)` i.e. low nibble + high nibble = 15, **self-contained, NOT derived from the index** (`akoid_poll_status32`; `hal_oid_validate_word` @0x08005720). Any nibble-sum-15 byte validates. Whether the *printed* parity pixel maps to this is an ASIC-domain question. oid-sensor-read-protocol.md §4. | **[Proven]** (firmware check) / **[Open]** (printed-pixel↔byte mapping) |
| 16 | OID code bands "The codes seem to be used in the following way (**please correct if wrong**)": products 0…, language 999↓, objects 1000–14999, raw 65535 = ignored | PEN-Optical-ID-and-codes | Firmware confirms the **product band**: `gme_oid_dispatch` @0x0803629c treats OID **≤ 0x3E7 (999)** as the product/mount band and higher codes as in-book object codes; the **system/factory family is 0xFF00–0xFFFF**; the sensor "filler/invalid" code is index **0x3FFFC** (dropped in `hal_oid_capture_decode23`). The ≤999-product / object-code split matches the wiki. oid-sensor-read-protocol.md §4/§5. | **[Proven]** (product/system/filler bands) |
| 17 | The physical OID **read protocol** (sensor↔SoC link) was never fully known black-box | PEN-Hardware-Details (OID sensor/decoder sections) | Full wire protocol reversed: **GPIO2 = clock, GPIO9 = data (bidirectional, also "code-pending" attention)**; bit-banged, no MMIO/IRQ/DMA. Frame = up to 32 bits MSB-first `[type:2][res:3][index:18][valid:1][check:8]`; 23-bit gameplay capture vs 32-bit status polls. `hal_oid_shift_in` @0x080055a4, `hal_oid_capture_decode23` @0x08005764. oid-sensor-read-protocol.md §3–5. | **[Proven]** |

---

## 3. Firmware behaviour

| # | Community unknown | Source (wiki) | Our resolution (decomp addr / evidence) | Verdict |
|---|---|---|---|---|
| 18 | **Test mode** entry + full test sequence (Chinese prompts, per-'+'/'−' tests) — inferred black-box | PEN-Firmware "Test mode" | Fully reversed. Entry = **power (GPIO11) + volume-down (GPIO1)** held at power-on (`splash_entry` @0x0804c1d4 samples both 4×) → `akoid_buf[0xb3]=1`; the stage machine in `fwupdate_verify_image` @0x0804cecc runs program-checksum, '+' → tip test / auth test / Test1KHZ.wav, '−' → TestFile cascade, writes `B:/Prodtest.txt`. Every wiki element byte-matched. test-mode.md §2–3. | **[Proven]** |
| 19 | Test mode "**Start test of encryption procedure** … **possibly uses the `TestFile` directory**" | PEN-Firmware "Test mode" | **Wrong guess — it is the anti-clone auth chip.** '+' press #2 runs `anticlone_zc90b_verify` @0x0804c47c (the ZC90B GPIO5/10 challenge). `TestFile` is a *separate* test triggered by volume-down. test-mode.md §3.3. | **[Proven]** (correction) |
| 20 | `TestFile/Test1..6.bin` "format looks like random data … **checksums-32 of the testfiles are contained in the firmware**" | PEN-Firmware "Test mode" | **Also wrong** — the expected checksum is **each file's own trailing LE u32**, compared against a byte-sum over `size−4` bytes (prod-test cascade in `0x0804cecc`), not a constant baked into firmware. Hence editing a file flips it to "checksum wrong!". test-mode.md §3.3. | **[Proven]** (correction) |
| 21 | The `.upd` **firmware-update format** / how A: is populated | PEN-Firmware (update flow) | Full ANYKA106 container map + updater path reversed: header @0..0xA4, `to_udisk`/`to_NAND`/producer/nandboot sections, checksum `upd_checksum_xor` @0x0810543c (`Σ b[i]^b[i+2]`, verified). Updater `fw_update` @0x08051fd4 **reformats A:** then unpacks the 7 to_udisk files (`upd_unpack_udisk_files` @0x080f2760). upd-system-partition-layout.md §1–3. | **[Proven]** |
| 22 | "There a string that looks like `ANYKA_IDRAV…` … **RAV probably stands for Ravensburger**" + version check | PEN-Firmware | The updater reads the trailer at `size−0x38`, requires **`RAV_`** and compares the `N….` version digits against the running firmware before applying (`fw_update_scan_b_for_upd` @0x08052768; `"Can update."`/`"Delete Update File."`). "RAV" being *Ravensburger* is a naming guess firmware can't confirm, but the trailer's role is proven. upd-system-partition-layout.md §3. | **[Proven]** (trailer role) / **[Open]** ("RAV" etymology) |
| 23 | Pen **modes** (normal / test / USB-MSC / USB storage-boot) — enumerated black-box | PEN-Firmware "Modes" | Test mode + normal operation fully mapped (statechart-full-map.md); USB-MSC = state 5 device stack; **USB storage-boot ("Snowbird2_Massboot") is the mask-ROM path** (captured separately, maskrom.bin) — not in PROG. Consistent with the wiki's four modes. | **[Proven]** (normal/test/MSC) / **[Inferred]** (mask-boot = maskrom) |
| 24 | **"MT" in filenames** "**currently unknown** … suspicion is Micron Technology flash for the 4 GB version" | PEN-Firmware | Supporting evidence, not proof: this firmware's `.upd` NAND geometry table and A: partitioning drive Micron-class NAND, and the hardware list includes Micron 29F32G08. Consistent with "MT = Micron Technology". **But** our maskrom capture shows a pen/firmware *generation* mismatch (pen ANYKANB2 vs our ANYKANB1 `.upd`s), so we cannot flatly certify the naming convention. | **[Inferred]** (Micron support consistent) / **[Open]** (naming certainty) |
| 25 | `.tiptoi.log` — described but its exact writer/format inferred | PEN-Firmware | Firmware maintains the pen-state/version files (version string updated each boot; `A:/SYSTEM/profile.dat` created on demand by `profile_write_record` @0x0804b8cc; runtime log `A:/Firmware log file.bin`). settings-config.md; upd-system-partition-layout.md §3. The precise 4-line `.tiptoi.log` layout is consistent with these writers. | **[Inferred]** |
| 26 | RAV/REC files "audio dongled to the pens … likely encrypted with a generic key which the pen gets by decrypting a keyfile" | RAV-file-format; REC-file-format | **Out of scope for the 2N-MT.** RAV/REC (audio-player/recorder) belong to the ZC3203L generation; this firmware has no RAV keyfile path. REC = WAV XOR 0x6A is a fixed constant, consistent, but not exercised here. | **[Open]** (different generation) |

---

## 4. Hardware

| # | Community unknown | Source (wiki) | Our resolution (decomp addr / evidence) | Verdict |
|---|---|---|---|---|
| 27 | **Crypto IC Chomptech ZC90B — "unknown spec"** (2 data lines emit bursts at power-on; role unknown) | PEN-Hardware-Details "Crypto IC" | It is the **anti-clone auth chip** the firmware gates boot on. Protocol: `anticlone_zc90b_verify` @0x0804c47c bit-bangs **GPIO10 = clock, GPIO5 = data** (the two ZC90B data lines), sends a 3-byte nonce challenge, reads 3 response bytes, compares to table-derived expected values; mismatch on the quiet-boot path → GPIO15=0 → power-off. Three **256-byte bijective S-boxes** live @0x080b0078/0178/0278 (`zc90b_tableA/B/C`); the algorithm was verified over 1000 nonces. anticlone-auth-check.md. | **[Proven]** (protocol + S-boxes) |
| 28 | ZC90B: does the firmware **gate boot** on it (block clones)? | (community had no firmware view) | **Yes.** On the low-battery/first-load "quiet" boot the auth failure is a **hard power-off**; on the healthy fw-update path it is a soft voice-only fail. anticlone-auth-check.md §1/§3. | **[Proven]** |
| 29 | EEPROM FM24C02B: "first 48 bytes read ~2 s after power-on, **probably** for OID config … EEPROM **seems not** used for SOC-relevant info" | PEN-Hardware-Details "EEPROM" | Confirmed from the SoC side: the EEPROM sits on the **OID decoder's** I²C bus; the firmware's I²C-device-0x94 path (`akoid_sensor_config_i2c` @0x080ef180) is **dead code** on this pen (sensor-type byte = 0 = Sonix two-wire). The SoC never reads the EEPROM. oid-sensor-read-protocol.md §1. | **[Proven]** (SoC doesn't use it) / **[Open]** (what the OID decoder stores there) |
| 30 | Main controller "Boots from external flash … **paging by MMU?**" | PEN-Hardware-Details "Standard mode" | Not separately proven here. The mask-ROM/nandboot SPL loads PROG from NAND to RAM (nandboot; NFTL docs); we did not audit MMU paging of PROG specifically. | **[Open]** |
| 31 | OID sensor/decoder link (SN9P601FG two-wire "index interface") — signals unknown black-box | PEN-Hardware-Details "OID sensor/decoder" | Reversed: two-wire serial, **GPIO2 clk / GPIO9 data**, frame format in §2/§17 above. Sensor power via `0x04000058` bits[25:24]. oid-sensor-read-protocol.md §1–5. | **[Proven]** (firmware view of the link) |
| 32 | Buttons / GPIO map (which GPIO = power / volume) — inferred black-box | PEN-Hardware-Details; PEN-Firmware | **GPIO11 = power (active-high), GPIO0 = volume-up (active-low), GPIO1 = volume-down (active-high)**; scanned by `hal_key_read` @0x08006B2C → event 0x105F {code,sub}. Corrects an earlier internal "battery-comparator" guess too. test-mode.md §2/§4. | **[Proven]** |
| 33 | USB storage boot mode / "Snowbird2_Massboot" details | PEN-Hardware-Details "Mass Storage Boot" | This lives in the **mask ROM** (separately dumped, maskrom.bin; maskrom-boot-vs-recovery.md), not in PROG — consistent with the wiki's GPIO13-strap description; we have the ROM but the massboot USB protocol itself is only partially audited. | **[Inferred]** (location) / **[Open]** (full massboot protocol) |

---

## 5. What remains genuinely open (and why)

- **Game-subtype parameter words** (Subgames u0–u9; Games i/q/x/w/v; Special-symbols c–g) — **now
  resolved** in gme-subtype-parameters.md: all six Games words and Subgames u0–u5 are given firmware
  meaning (u6–u9 dead), and the native firmware's use of special-symbols a/c/f is decoded. What stays
  open is narrower and content-internal: the embedded-binary per-title parameter block (special-symbols
  b/d/e/g + the 13 "padding" words, which live in the shipped ARM game binaries) and a full per-slot
  catalogue of type-9's native cue pool.
- **The Sonix ASIC's internal pattern→index scramble** (tttool's `KnownCodes.hs`). Firmware proves
  it is *not* in firmware; resolving the actual mapping needs the Sonix DSP / printed-pattern domain,
  which we don't have.
- **"Customer number" / "RAV" etymology, MT-naming certainty.** These are naming conventions outside
  the code; firmware confirms the *roles* (mount magic, version trailer, Micron support) but not the
  human labels.
- **RAV/REC audio dongling** and **ZC3203L-specific behaviour** — a *different pen generation*; our
  decomp is the 2N (ZC3202N) MT firmware. Compounded by the captured-maskrom generation mismatch
  (ANYKANB2 pen vs our ANYKANB1 `.upd`s).
- **MMU paging of PROG** and the **full massboot USB protocol** — auditable from the mask ROM we hold,
  but not yet done.

---

## 6. Upstream contribution note

Items **1, 2, 3, 5, 18–21, 27–29, 32** are concrete, wire-/byte-level answers to questions the
tip-toi-reveng wiki explicitly leaves open, plus the five **bonus** GME opcodes/fields tttool lacks
(§1 bonus box). These are directly submittable as wiki/`GME-Format.md` corrections and additions,
each backed by a firmware address in this decomp.
