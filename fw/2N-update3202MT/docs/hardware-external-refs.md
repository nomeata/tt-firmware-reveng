# Tiptoi pen — external hardware references

External, web-sourced hardware documentation for the tiptoi pen, cross-referenced
against **our** target: the **2N generation** — Anyka "Snowbird2" SoC (marking
**ZC3202N**, ARMv5TE / ARM926EJ-S), NAND ≈ 512 MiB (2 KiB page, 64 pages/block,
4096 blocks), running `update3202MT` (`N0038MT`, ANYKANB2).

> **Big caveat.** The primary source (entropia tip-toi-reveng wiki, "PEN Hardware
> Details") is measured on **Revision B6 (2 GB variant)** and mixes several board
> revisions (A4, R5, B6, "Gen 4"). It documents multiple NAND parts and both the
> ZC3201 (1N) and ZC3202N (2N) SoCs. Judge each row on its own; the wiki is **not**
> a 1:1 description of our 2N pen. The "matches?" column is my assessment.

Sources:
- Wiki: https://github.com/entropia/tip-toi-reveng/wiki/PEN-Hardware-Details
  (raw: https://raw.githubusercontent.com/wiki/entropia/tip-toi-reveng/PEN-Hardware-Details.md)
- Schematic PDF (Rev B6): https://github.com/entropia/tip-toi-reveng/files/13058008/Tiptoi.B6.pdf
- snowbirdopter (UART-boot RE tool): https://github.com/maehw/snowbirdopter
- Mailing-list threads: https://listi.jpberlin.de/pipermail/tiptoi/2015/000226.html ,
  https://listi.jpberlin.de/pipermail/tiptoi/2015/000434.html

---

## 1. Component inventory

| Component | Part / marking | Key specs | Source | Matches our 2N pen? |
|---|---|---|---|---|
| **SoC / CPU** | Chomptech **ZC3202N** (ChipID "1090"), 64-pin LQFP; = Anyka **AK1050**, **ARM926EJ-S** (ARMv5TEJ). Predecessor: **ZC3201** (rev A4). | Boot ROM banner "SNOWBIRD2-BIOS" / "Snowbird2_Massboot". UART 115200 (std) / 38400 (mass/UART boot). BIOS 0x0000_0000–0000_FFFF; Registers 0x0400_0000–040A_FFFF; RAM 0x0800_0000–0802_FFFF. Integrates USB2.0 host+slave, MMC/SD, audio ADC/DAC, camera IF, DDR2 ctrl. | wiki; anyka.com/en/productInfo.aspx?id=77; jotrin/hkinventory | **YES (2N)**. ZC3202N = our pen; ZC3201 = older 1N. Snowbird2/ANYKANB2 confirmed in our firmware. |
| **NAND flash** (option A) | Hynix **HY27UF084G2B** | 4 Gbit = 512 MiB, ×8, TSOP-48. **2112-B page (2048+64), 64 pages/block, 4096 blocks, 128 KiB block.** 3.3 V. | wiki; alldatasheet 333911 | **YES (2N)** — this is the exact geometry our FINDINGS/nftl-layout.md pins as the 512 MiB target (2 KiB page, 64 pg/blk, 4096 blk). |
| **NAND flash** (option B) | Hynix **H27UAG8T2BTR** | 16 Gbit = 2 GiB, ×8, TSOP-48. 4 KiB page + 64B spare, 4096 blocks (MLC). | wiki; alldatasheet 521500 | **variant** — the "B6 2 GB" board. Larger page than our 2 KiB target; a different capacity SKU, not our unit. |
| **NAND flash** (option C) | Micron **29F32G08CBACA** (MT29F32G08CBACA) | 32 Gbit = 4 GiB, ×8. Note: Micron's *CBACA* die is 4 KiB page, 256 pg/blk, 24-bit/1080B ECC — larger than our 2 KiB target. | wiki; mouser micts04808 | **variant/unknown** — "MT29F32G08-class" is our *shorthand for a 512 MB-class ×8 part*; the literal CBACA is a bigger die. Our numeric geometry (2 KiB/64/4096) matches HY27UF084G2B, not CBACA. Flag: the task brief's "MT29F32G08 ≈512 MB" is imprecise — 512 MiB corresponds to the Hynix 4 Gbit part. |
| **OID sensor module** | Sonix **SNM9S102C2200A** (die = **SN9S102CE** CMOS image sensor); *marking not confirmed on our pen* | 3.0–3.6 V, 12 mA typ / 50 µA standby, up to 8 MHz, 12-pin module IF, programmable exposure/gain over serial bus, **2-bit nibble data out**, 8-bit ADC, built-in IR LEDs. | wiki; sonix.com.tw | **likely 2N**, unconfirmed by marking. Sensor is dumb pixels; decode is in the SN9P601 below. |
| **OID image decoder** | Sonix **SN9P601FG-301**, LQFP48 | "OID 1.5" decoder = lower-cost D.H.R.T. engine. Core 3.0–3.6 V, reg-in 3.6–5.0 V, ~5 mA typ, <10 µA shutdown, embedded **16-bit DSP**, built-in 16 MHz RC osc, LDO + low-battery detect, **bidirectional 2-wire serial ("two_wire_interface V2")**, outputs the Optical Index. **OID_Code_v1.5.** | wiki; datasheet4u SN9P601FG-301; sonix.com.tw/article-en-1684-7124 | **YES (2N pen family)** — matches our pen (decoder→SoC sends only OID/control codes). See §2. |
| — related 2nd-gen decoder | Sonix **SN9P701F-004** (LQFP48) | Full SN9P701 datasheet obtained (saved locally, see §2). OID_Code_v2: **65,536 indices (extendable to 262,144)**, 1.3×1.3 mm dots. Prior gen SN9P700 = OID_Code_v1.0, **0–4096 indices**, 32-pin. | datasheet PDF (saved) | **reference only** — the 601 (v1.5) is on our pen, but the 701 datasheet documents the same two_wire_interface family and the index model. |
| **EEPROM** | Shanghai Fudan **FM24C02B**, TSSOP-8 | 2 Kbit (256×8), I²C, connected to the **OID decoder** (SDA/SCL), 5 V-tolerant. First 48 bytes read ~2 s after power-on (OID config). **Not on the SoC bus** — SoC never sees it. | wiki; eng.fmsh.com | **YES (2N)** but **not visible to the SoC** (belongs to the OID subsystem). |
| **Crypto IC** | Chomptech **ZC90B** (2010 ver: ZC90), 8-pin | Undocumented. Pin1 VDD 3.3 V; pins 4 & 7 emit data bursts at power-on and while LED flashes; pin8 GND. Chomptech marketing lists it as "encrypt IC" alongside ZC3202N + SN9P701. | wiki; listi.jpberlin.de/2015/000434 | **YES (2N)** hardware, spec unknown. Talks to SoC as an anti-clone auth token. |
| **Audio amplifier** | **8891UL** (Lyontek LY8891), 1.0 W class-AB | Speaker amp; enabled/disabled by SoC GPIO (our HAL `audio_amp_enable` toggles GPIO bit 0x10). | wiki; alldatasheet | **YES (2N)** — matches our audio path (SoC internal DAC → analog → 8891UL → speaker). |
| **Speaker** | 8 Ω, 0.3 W (≈ Kingstate KDMG20008) | — | wiki | YES (generic). |
| **Crystal** | **12.000 MHz** quartz | SoC main clock reference. | wiki | YES (2N). |
| **Power button / LED** | separate PCB "1399A0" | LED + n.o. switch, 4 wires (red/blk/grn/blu). LED between #3/#4, switch #1/#2. LED blink 36 ms / 10.34 s period. | wiki | YES (housekeeping; low RE value). |
| **Fuse** | 0 Ω (repl. Bourns SF-0603F100, 1 A) | Battery-rail protection. | wiki | YES (irrelevant to emu). |
| **USB** | integrated in ZC3202N (USB2.0 HS host+slave) | Mass-storage boot / firmware update path. | wiki; AK1050 spec | YES (2N). |

---

## 2. Sonix OID — how OIDs are presented (most useful for RE)

The **decode from printed dots → numeric OID happens entirely inside the Sonix
decoder ASIC** (SN9P601FG on our pen). The SoC only ever receives the finished
index over a **2-wire serial ("two_wire_interface")** link. This matches our
firmware exactly: `oid-sensor-read-protocol.md` shows the SoC bit-bangs a command byte
out (`akoid_shift_out`, e.g. `0x56`) and shifts a raw word **in** on GPIO9,
then `>>9` / masks `&0x3FFFF` to recover the OID number — so the dot-pattern-recognition
math lives in the ASIC, and everything downstream sees only the decoded index over that
serial link.

**OID code family / index ranges (from the Sonix datasheets):**

| OID code version | Decoder part | Index range | Dot size | Package |
|---|---|---|---|---|
| OID_Code v1.0 | SN9P700(A) | **0 – 4096** | 0.5×0.42 mm | LQFP32 |
| **OID_Code v1.5** | **SN9P601FG** *(our pen)* | (v1.5 low-cost variant of the 2-wire family) | — | LQFP48 |
| OID_Code v2.0 | SN9P701F-004 | **0 – 65,535** (extendable to 262,144) | 1.3×1.3 mm | LQFP48 |

Our firmware masks OID to **18 bits (`&0x3FFFF` = 0–262,143)** in the nandboot
producer decoder (`hal_oid_decode` @0x08005764) and to the same field in the game
path — consistent with the Sonix "extendable to 262,144" index space.

**SN9P701F datasheet (Sonix, V1.02 Jul 2006)** — a public vendor document (not
redistributed here; obtain it from Sonix). Relevant contents: full 48-pin table
(SEN_CMD/SEN_CLK/SEN_D0/SEN_D1 sensor IF; ADIO0/1/2 = the ADO_CLK/ADO_SCK/ADO_SDIO
two-wire output pins; KEY0/1/2 button inputs; IRED0/1 IR-LED drives with voltage
feedback; BAT_DET/BAT_SW low-battery; built-in 3.25 V LDO; RC 16 MHz via 29 kΩ on
RC_BIAS or external crystal). It references a separate `two_wire_interface_v2.pdf`
for the **byte-level serial semantics**, which is not publicly available; the
community likewise has only the *physical* interface, not the byte semantics — but
those are not needed here, because our own firmware HAL already encodes the exact
shift-in/shift-out protocol the SoC uses (documented in `oid-sensor-read-protocol.md`).

**Note on decoder generation vs our sensor config:** the wiki calls the module
"OID 1.5 / SN9P601FG". However, our firmware also has an `akoid_sensor_config`
path that programs an **Anyka on-SoC sensor** over I²C at device `0x94` (type 1/3)
— i.e. some pen builds route the CMOS sensor straight to the SoC's camera/I²C
interface rather than through a standalone Sonix decoder. Treat the exact OID
front-end as **build-dependent**; for the 2N firmware, the authoritative model is
the GPIO2-clock / GPIO9-data shift path plus the `0x94` I²C sensor config, both
already in `oid-sensor-read-protocol.md`.

---

## 3. Cross-reference to our known facts

| Our fact (FINDINGS / docs) | External confirmation | Verdict |
|---|---|---|
| Anyka **Snowbird2** SoC, ANYKANB2 | ZC3202N = Anyka AK1050, ARM926EJ-S; boot banner "SNOWBIRD2-BIOS" | **Confirmed.** External marking ZC3202N ↔ our internal Snowbird2. |
| NAND 512 MiB, 2 KiB page, 64 pg/blk, 4096 blk (`nftl-layout.md` names **HY27UF084G2B**) | HY27UF084G2B datasheet = 4 Gbit, 2112-B page, 64 pg/blk, 4096 blk | **Confirmed exactly.** Our doc already names the right part; wiki lists it as option A. |
| "MT29F32G08-class" shorthand in task brief | Literal MT29F32G08CBACA is a 4 GiB / 4 KiB-page die (bigger) | **Mismatch flag.** The 512 MiB / 2 KiB-page target = **Hynix HY27UF084G2B**, not the literal Micron CBACA. Keep using HY27UF084G2B for geometry. |
| OID sensor read on **GPIO9** (data), GPIO2 (clock); OID = raw `>>9 & 0x3FFFF` | Sonix decoder emits index over 2-wire serial; SoC shifts it in | **Confirmed.** Decode is in the Sonix ASIC; the SoC only clocks in the number over this serial link. |
| DAC/DMA audio path (regs 0x04010000, 0x04080000, 0x04000008/64) → amp on GPIO 0x10 | External amp = **8891UL** 1 W; ZC3202N has integrated audio DAC | **Confirmed / enriched.** The analog chain is SoC-internal DAC → 8891UL → 8 Ω speaker. |
| NFC/NAND controller @ **0x0404a000**; PMU MMIO **0x04070000**; GPIO **0x040000BC** | Wiki gives only the coarse register window (0x0400_0000–040A_FFFF); no per-block map published | **Consistent** (addresses fall in the documented window) but **no external per-register datasheet exists** — Anyka AK1050 register map is not public. Our reverse-engineered map is the authority. |
| Boot pins GPIO9/12/13 (pins 37/36/39), RESET pin 29 | Wiki + snowbirdopter give identical pin map and UART-boot command set (download/setvalue/go/dump) | **Confirmed.** snowbirdopter is directly usable on our pen for RAM dump / code exec. |

---

## 4. What's useful for RE / running the firmware

1. **OID front-end (gold).** Confirmed: dot→index decode lives in the Sonix ASIC;
   the SoC only receives a numeric OID over a 2-wire serial link (GPIO2 clk /
   GPIO9 data in our firmware) — no image processing on the SoC side. Index space
   up to 18 bits (0x3FFFF), consistent with Sonix "extendable to 262,144". The
   byte-level `two_wire_interface_v2` semantics are **not published**, but our
   firmware HAL already fully specifies the shift protocol the SoC uses.
2. **Audio DAC.** The DAC is **inside the ZC3202N** (no external codec chip);
   register model is our own (`0x04080000` enable, `0x04000008/64` rate/OSR,
   `0x04010000` DMA). External amp 8891UL is downstream analog, gated by a GPIO
   enable bit.
3. **NAND geometry / ECC.** Use **HY27UF084G2B**: 2048+64-B pages, 64 pages/block,
   4096 blocks, 512 MiB, TSOP-48, ONFI-ish legacy command set. Datasheet gives
   timing and the ID bytes the boot ROM prints on GPIO13. (The 2 GB and 4 GB SKUs
   in the wiki are different pen builds — different page size; do not use for the
   2N target.)
4. **SoC peripherals / register map.** Anyka AK1050 has **no public register
   datasheet**; only the coarse address window (Registers 0x0400_0000–040A_FFFF,
   RAM 0x0800_0000+) is documented. Our RE'd map (NAND ctrl 0x0404a000, PMU
   0x04070000, GPIO 0x040000BC, DAC 0x04080000, DMA 0x04010000, IRQ 0x040000e0/f0)
   remains the sole authority. External sources add nothing here.
5. **UART boot / RAM access.** snowbirdopter (Python, 38400 8N1, GPIO9 low to
   enter BIOS) gives live `dump`/`load`/`go` — a practical channel to inspect real
   hardware and to fetch the ANYKANB2 SPL / RAM.
6. **ZC90B crypto.** Anti-clone auth token on 2 data lines; spec unknown; the 2N
   firmware queries it at boot (`anticlone-auth-check.md`). Pins 4/7 carry the
   traffic (needs a real-pen capture to characterise).
7. **FM24C02B EEPROM.** Belongs to the OID decoder, **not** the SoC bus.

---

## 5. Revision caveats & retrieval gaps

- **Wiki = mixed revisions.** Electrical measurements are on **Rev B6 (2 GB)**;
  it also documents A4 (ZC3201, 1N), R5, and "Gen 4" camera repairs. Only rows I
  marked **2N** describe our pen.
- **NAND SKU spread.** Three different NAND parts appear across pen builds. Our
  2N target = **HY27UF084G2B (512 MiB, 2 KiB page)**. The task brief's
  "MT29F32G08-class ≈512 MB" conflates two things: the *literal* Micron CBACA is
  a larger die; the 512 MiB geometry is the Hynix part. **Use HY27UF084G2B.**
- **Sonix protocol PDF unreachable.** `two_wire_interface_v2.pdf`,
  `Consumer_OID.pdf`, and the SNM9S102C2200A/SN9P601FG spec files on sonix.com.tw
  are behind a JS-gated download portal — byte-level protocol/register content
  could not be extracted from them. The **SN9P701F-004 datasheet** covers the same
  interface family, pinout, and index model. The community likewise has only the
  physical interface, not the byte semantics; our firmware HAL is the substitute source.
- **AK1050 register map not public.** No manufacturer datasheet exposes the
  peripheral register map; rely on our RE.
- **Sensor front-end is build-dependent.** Wiki says standalone Sonix decoder;
  our firmware also has an Anyka on-SoC sensor config (I²C dev 0x94). For the 2N
  firmware, the GPIO shift path + 0x94 config in `oid-sensor-read-protocol.md` is
  authoritative.
