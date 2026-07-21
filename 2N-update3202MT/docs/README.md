# Firmware findings — 2N-update3202MT

Reverse-engineering notes for the **tiptoi® pen firmware**, variant `2N-update3202MT`
(2nd-generation pen, ARM SoC ZC3202N, Micron NAND, build `20131009`). This is the
firmware that matches a real pen we verified against the silicon.

These documents describe **how the firmware works** — the GME file format and script
interpreter, the hardware interfaces, the storage stack, and the boot/runtime state
machine. They are our own commentary, addresses, struct layouts and short illustrative
snippets; they contain **no vendor firmware and nothing mechanically derived from it**
(see [CONTRIBUTING](../../CONTRIBUTING.md)). Findings that were confirmed by *running*
the firmware were checked under [`tt-emu`](https://github.com/nomeata/tt-emu), which lives
in its own repository.

Where a document refers to a binary that is firmware-derived or hardware-captured — the
mask ROM, live pen RAM/flash dumps, or extracted audio (`Chomp_Voice.bin`) — that binary
is **not** included here; you obtain it yourself.

**New here?** Start with **[FIRMWARE_OVERVIEW.md](FIRMWARE_OVERVIEW.md)** for the whole
system on one page, then dive into the flagship GME story below.

---

## Flagship: the GME format & script interpreter

How the pen reads a `.gme` file, runs its script bytecode, and drives the game types —
the black box the community reverse-engineered from the outside, seen from the inside.

| doc | what it covers |
|---|---|
| [community-open-questions-resolved.md](community-open-questions-resolved.md) | The community's open questions (from tttool's wiki & GME-Format) answered from the firmware: proven / inferred / still-open. **The "why this matters" lead.** |
| [gme-format-completeness.md](gme-format-completeness.md) | Completeness audit of the GME interpreter: every opcode, conditional, game type, media/codec path and header table — documented vs. undocumented. |
| [gme-subtype-parameters.md](gme-subtype-parameters.md) | The per-game-type parameter slots (Subgame u0–u9, Game c/i/q/x/w/v, special-symbol a–g) with their proven firmware meanings, including game types 17–23. |
| [gme-timer-counter-random.md](gme-timer-counter-random.md) | The timer / counter / random script commands — all pure software (system tick, 6-slot software timer table, LCG); no hardware RNG or timer. |
| [book-discovery-and-load.md](book-discovery-and-load.md) | How the pen discovers books on the drive (`oidfilelist.lst`) and mounts a product on tap; the `.lst` record format. |
| [cover-tap-first-product-load.md](cover-tap-first-product-load.md) | How a cover/product-OID tap at standby reaches book mode and loads the first product (two-tap sequence, cover-OID classifier semantics). |
| [oid-classifier-logic.md](oid-classifier-logic.md) | What the cover-OID classifier actually tests, and the product/content threshold that decides routing. |
| [gme-callgraph-inventory.md](gme-callgraph-inventory.md) · [.tsv](gme-callgraph-inventory.tsv) | Naming-database appendix: the ~294-function GME/script/engine call-graph (address → name / signature / docstring / coverage). Regenerable via `tools/gme_inventory.py`. |

## Hardware & I/O

The register-level boundaries between the firmware and the physical pen.

| doc | what it covers |
|---|---|
| [soc-core-registers.md](soc-core-registers.md) | Consolidated SoC register map (GPIO, clock/PLL, IRQ, timer, ADC, boot straps) — the reference the other hardware docs cross-link. |
| [oid-sensor-read-protocol.md](oid-sensor-read-protocol.md) | The OID optical sensor link: the two-wire serial protocol, frame format, and the raw↔OID-code boundary. |
| [audio-output-hw.md](audio-output-hw.md) | Audio output: the internal DAC, audio DMA, the PCM ring buffer, the refill IRQ, and amp/mute/keep-alive GPIO sequencing. |
| [media-pipeline.md](media-pipeline.md) | Short overview: tap → `play_media` → codec detection → decode → DAC, and the `0xAD` media XOR. |
| [audio-player-construction.md](audio-player-construction.md) | How the audio-player object is constructed on entry to book mode, and its dependencies. |
| [pmu-power-management.md](pmu-power-management.md) | Battery / charger / USB-detect / auto-off / power-on-off; and why `0x04070000` is the MUSB controller, not a PMU. |
| [led-indicator.md](led-indicator.md) | Whether the tap "blink" is in firmware — a negative result: no LED GPIO exists. |
| [power-on-sound.md](power-on-sound.md) | What actually sounds at power-on (silent until the first cover tap) and the resume path. |
| [system-voice-feedback.md](system-voice-feedback.md) | The system-prompt voices: the voice-ID map and the `Chomp_Voice.bin` offset-table format. |
| [anticlone-auth-check.md](anticlone-auth-check.md) | The boot-gated anti-clone challenge/response over GPIO (protocol and pins; substitution-table values deliberately not reproduced). |
| [zc90b-model.md](zc90b-model.md) | The ZC90B anti-clone chip's full challenge/response algorithm, the recovered device response function, and the GPIO wire protocol (S-box table *contents* not reproduced — extract from your own image). |
| [test-mode.md](test-mode.md) | The factory PROD-TEST mode: entry buttons and full test sequence (matches the tip-toi-reveng wiki's "Test mode"). |
| [oid-digit-readout.md](oid-digit-readout.md) | The prod-test "tap a code, hear its digits" feature. |
| [hardware-external-refs.md](hardware-external-refs.md) | External datasheet cross-references for the SoC, NAND, OID sensor, amplifier and EEPROM. |

## Storage stack

From raw NAND up through the flash-translation layer and FAT to the two logical drives.

| doc | what it covers |
|---|---|
| [nftl-layout.md](nftl-layout.md) | Canonical NFTL **read-side** reference: device/chip/partition objects, geometry, the log2phy resolver (dense-window + RLE), spare-tag format, bad-block codes. |
| [nftl-write-consistency.md](nftl-write-consistency.md) | Canonical NFTL **write-side** reference: the write/program stack, append-vs-COW, the fold/garbage-collect sequence, and the tag-field contract. |
| [nftl-medium-translation.md](nftl-medium-translation.md) | The AU-scaled medium→row address translation and zone-table decode. |
| [fatvol-medium-layering.md](fatvol-medium-layering.md) | The FatLib volume↔medium object model and the FHA maplist format. |
| [partition-a-fat-vs-mbr.md](partition-a-fat-vs-mbr.md) | The drives' native on-disk format (bare FAT superfloppy, no MBR), the sector-0 dispatch heuristic, and auto-format behavior. |
| [ab-drive-layout.md](ab-drive-layout.md) | The A: (system/index) vs B: (USB user drive) split and the zone-table geometry. |
| [nfc-controller-registers.md](nfc-controller-registers.md) | The NAND-flash controller hardware: register maps (command sequencer, ECC engine, L2 buffer) and per-op micro-op sequences. |
| [nb-read-data-conventions.md](nb-read-data-conventions.md) | The `NB_READ_DATA` low-level read call convention and its descriptor layouts. |
| [usb-msc-device.md](usb-msc-device.md) | The USB mass-storage (BOT/SCSI) device stack, MUSB registers, enumeration, and the vendor channel. |
| [upd-system-partition-layout.md](upd-system-partition-layout.md) | The `.upd` update-container format: header fields, verified checksum, the `to_udisk` payload, and how A: gets populated. |
| [nand-init-from-producer.md](nand-init-from-producer.md) | Static RE of the factory `producer` image: NAND format/burn flow, metadata pages, zone table, FAT. |
| [udisk-content-provisioning.md](udisk-content-provisioning.md) | Who writes `A:/VOIMG` and the `to_udisk` files — the firmware's own `.upd` unpacker is the verified writer. |
| [codepage-what-is-it.md](codepage-what-is-it.md) | What `codepage.bin` is (a Win32-NLS-style 61-codepage conversion DB, not fonts) and the language/codepage selection (always GBK / ASCII-identity). |
| [nftl-library-identification.md](nftl-library-identification.md) | Identifying the flash stack as the Anyka SDK (FHA + MtdLib + NFTL_V1.2.11 + FatLib) via reproducible external evidence. |

## Boot & runtime

The boot chain, the QHsm state machine, and how the app initializes.

| doc | what it covers |
|---|---|
| [FIRMWARE_OVERVIEW.md](FIRMWARE_OVERVIEW.md) | The single-reference map: boot chain, flat-load memory model, statechart, media, storage, and open items. **Start here.** |
| [statechart-framework.md](statechart-framework.md) | The QHsm framework primer: the active-object struct, the dispatch functions, and the state tables. |
| [statechart-full-map.md](statechart-full-map.md) | The complete ~70-state map: descriptor / event-action / transition-action tables, event vocabulary, and the state graph. |
| [autonomous-mount-state8.md](autonomous-mount-state8.md) | The one-page verdict on the state-8 gate and the A: mount as independent floors. |
| [state8-to-13-transition.md](state8-to-13-transition.md) | The deep dive on the state-8→13 path: the USB gate, the state-9 pre-dispatch, and the cover-tap descent. |
| [product-init-and-runtime-tables.md](product-init-and-runtime-tables.md) | The flat-load memory model, the QHsm dispatch tables in `.data`, and the `gme_parse_header` first-load seeder. |
| [settings-config.md](settings-config.md) | What the pen persists: volume (RAM-only), language, auto-off, and the FAT-file persistence model (no settings menu, no EEPROM). |
| [maskrom-boot-vs-recovery.md](maskrom-boot-vs-recovery.md) | The mask-ROM boot: ANYKANB2 loaders are the normal boot; the strap-selected UART/USB consoles are the recovery surfaces. |
| [nandboot-generation-puzzle.md](nandboot-generation-puzzle.md) | The ANYKANB1-vs-ANYKANB2 puzzle: the magic is an isolated per-generation header stamp; the SPL code is shared. |
| [nand-reconstruction-and-from-reset.md](nand-reconstruction-and-from-reset.md) | The from-reset experiment: the `.upd`'s SPL does hardware init only and is not a cold-boot image. |
