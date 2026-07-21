# USB mass-storage device stack — how the pen presents drive B: to a PC

Static decomp analysis (`out/decomp/`, unified base **0x08009000**; literal-pool constants
resolved from `data/PROG.bin`). Evidence tags: **[Proven]** = read directly from the cited
decomp/literal pools; **[Inferred]** = deduced (reason given); **[Open]** = unresolved.

Companion docs: `pmu-power-management.md` (the MUSB core as PMU-mimic, PC-vs-charger detection,
states 5/6/8), `statechart-full-map.md` (states), `partition-a-fat-vs-mbr.md` (B: on-media format),
`autonomous-mount-state8.md`, `nftl-layout.md`.

**Scope note:** this whole subsystem only runs when a *PC host* is plugged in (VBUS = GPIO8 = 1
**and** the USB bus enumerates); with GPIO8 = 0 the whole stack stays dormant and it is not
needed for book-play. This doc rounds out the USB subsystem: it is how GMEs and firmware updates
get onto the pen, and how the manufacturing line talks to it.

---

## 0. Headline findings

1. **The pen is a USB 2.0 High-Speed Bulk-Only Mass-Storage (BOT/SCSI) device.** Class 0x08 /
   subclass 0x06 (SCSI transparent) / protocol 0x50 (Bulk-Only). One configuration, one interface,
   **two bulk endpoints** (EP1 OUT, EP2 IN) plus EP0 control. [Proven — descriptor bytes §2]
2. **VID = 0x2546, PID = 0xE301, bcdDevice = 0x0001.** INQUIRY reports vendor `"MP4     "`,
   product `"MP4 Player      "`, rev `"V1.0"`; USB string descriptors are `"OID     "` /
   `"OID Player"` / `"USB 2.0"`. It deliberately looks like a generic MP4-player thumb-drive, not
   a "Ravensburger tiptoi". [Proven §2]
3. **The controller is the Mentor MUSB core at 0x04070000** (the same block `pmu-power-management.md`
   proved is *not* a PMU). Standard MUSB indexed register model: common regs at +0x00..+0x0E,
   an indexed per-endpoint CSR window at +0x10..+0x18, EP0 FIFO at +0x20, and an Anyka DMA/PHY
   extension at +0x330..+0x348. [Proven offsets §1]
4. **The PC sees drive B: (the u-disk) as a raw superfloppy.** READ10/WRITE10 LBAs map straight
   onto the B: medium's sectors through the same FatLib/NFTL "medium" object the pen uses
   internally — no MBR translation, no file-level indirection. The medium is registered as LUN 0.
   [Proven data path §4; on-media format cross-ref `partition-a-fat-vs-mbr.md`]
5. **Entry is statechart state 5 (USB-PC).** `usb_state_dbg` (0x08050fb8) arms it; `usb_state_handler`
   (0x08051090) holds the power latch and hands off to the blocking MSC service loop
   **`usb_power_switch` (0x0803d1d4)** which pumps the BOT protocol until unplug; **after** the
   session it re-scans (and if wiped, re-formats) the B: FAT, then 0x100C back to standby. [Proven §5]
6. **There is a full vendor command channel** (SCSI opcodes 0xCC–0xE2, gated by ASCII magics
   "ANYKA"/"FM"/"NO"/"AD") used by the Anyka production tooling to write serials, format, push
   config, and stream out `A:/Product log file.bin`. [Proven §4.4]

---

## 1. The MUSB endpoint model (0x04070000)

Base pointer `0x04070000` lives in pool `DAT_0804164c` and `DAT_0803dcec`/`DAT_08050c10`. All
endpoint access goes through the **INDEX register (+0x0E)** followed by the indexed CSR window —
the classic MUSB pattern. [Proven]

### 1.1 Register map (offsets confirmed from firmware access)

| offset | MUSB reg | width | firmware use / evidence |
|---|---|---|---|
| **+0x00** | FAddr / device address | byte | `usb_bus_reset` sets `*base = 0` (address 0 on reset); `usb_set_address` 0x08040df8 writes the assigned address |
| **+0x01** | **POWER** | byte | HSEnab(0x20)/HSMode(0x10)/Reset(0x08)/Suspend. `0x21`=arm+HS-enable (`usb_phy_arm_detect`), `1`/`0x21`=FS/HS report (`usb_report_speed`), `0`=off (`usb_phy_off`) |
| **+0x02** | **INTRTX** | u16 | TX-endpoint interrupt flags; read (read-clear) in `FUN_08041b44` |
| **+0x04** | **INTRRX** | u16 | RX-endpoint interrupt flags; read in `FUN_08041b44` |
| **+0x06** | INTRTXE | — | TX interrupt enable; `usb_bus_reset` sets **5** (EP0 bit0 + EP2 bit2) |
| **+0x08** | INTRRXE | — | RX interrupt enable; `usb_bus_reset` sets **2** (EP1 bit1) |
| **+0x0A** | **INTRUSB** | byte | bus interrupts: **bit2=Reset**, **bit3=SOF** (used for PC-vs-charger, `usb_connect_handler`); read in `FUN_08041b44` |
| **+0x0B** | INTRUSBE | byte | `usb_bus_reset` sets **0xF7** (enable all but one) |
| **+0x0E** | **INDEX** | byte | selects the endpoint whose CSRs appear in the +0x10 window. Every CSR helper writes this first |
| **+0x10** | TXMAXP / RXMAXP | — | max-packet, programmed by `FUN_08041460` on reset |
| **+0x12** | **CSR0 / TXCSR** | byte/u16 | EP0 & TX (EP2-IN) control-status. Helpers read via `FUN_08041b30(2,0x04070012,…)`, write via `FUN_08041b20`, flush via `sysapi_free` |
| **+0x16** | **RXCSR** | byte/u16 | RX (EP1-OUT) control-status. `FUN_08041b30(1,0x04070016,…)` |
| **+0x18** | **RXCOUNT** | u16 | bytes received in the RX FIFO; read by `FUN_08041b64` and `FUN_08041684` |
| **+0x20** | **FIFO0** | byte | EP0 FIFO — byte writes flush/stage EP0 data (`FUN_080417f4`, `FUN_0803dc58`) |
| **+0x330,+0x338,+0x33c,+0x340** | Anyka **DMA/FIFO ext** | word | bulk-FIFO count/trigger latches used by `FUN_080417f4`/`FUN_08041684` |
| **+0x348** | Anyka **PHY control** | word | bit0 = force full-speed (set on FS, cleared on HS) |

Related sys-ctrl bits toggled around the PHY (0x04000034 bit6, 0x04000058 bits0-4, 0x0400000C
bit20, 0x0400004C bit4, 0x04000044 bit9) — see `pmu-power-management.md` §2. [Proven writes;
individual semantics Open]

### 1.2 Endpoints

| EP | index | dir | type | wMaxPacketSize | role |
|---|---|---|---|---|---|
| **EP0** | 0 | control | — | **64** (`bMaxPacketSize0=0x40`) | enumeration: SETUP + descriptor/status |
| **EP1** | 1 | **OUT** | bulk | **512** HS / 64 FS | receives **CBW** (31 B) and **WRITE10/12 data** |
| **EP2** | 2 | **IN** | bulk | **512** HS / 64 FS | sends **READ10/12 data** and **CSW** (13 B) |

`usb_bus_reset` (0x080414e4) configures both bulk EPs on every bus reset:
`FUN_08041460(1,0x200,0,0)` (EP1 OUT, 512-byte FIFO) and `FUN_08041460(2,0x200,1,0)` (EP2 IN),
and picks **0x200-byte FIFOs at HS** vs **0x40 at FS** by reading POWER bit4 (HSMode) — i.e. the
FIFO/max-packet sizes track the negotiated speed. Device address is reset to 0, INTRTXE/RXRXE/USBE
re-armed. [Proven]

### 1.3 FIFO access (`FUN_08041684` read, `FUN_080417f4` write)

- **EP0 (index 0):** PIO. Received setup/OUT bytes are staged in a RAM shadow at **0x08006A40**
  (`FUN_08041684` copies word-wise from there, length = RXCOUNT); TX bytes to EP0 are written to
  FIFO0 (+0x20) after staging in **0x08006400/0x08006600**. [Proven]
- **Bulk (index 1/2):** when the transfer length is a multiple of 64 the code uses the **Anyka DMA
  engine** (`func_0x0800344c` = DMA channel setup, `func_0x08003530` = DMA start,
  `func_0x08001990` = phys-addr translate); otherwise it falls back to word-wise PIO through the
  0x08006400/0x08006600 buffers, then sets the +0x33c/+0x340 trigger bits. [Proven mechanics;
  "DMA" label Inferred from the 64-byte-alignment fast path]

### 1.4 Interrupt flow

`usb_bus_reset` enables INTRTX/INTRRX/INTRUSB. The USB ISR body is split:

- **`FUN_0803db24`** (the endpoint dispatcher, called from the MUSB IRQ — reached by direct BL,
  not data-referenced): reads INTRUSB/INTRTX/INTRRX via `FUN_08041b44`, then calls
  **`usb_bulk_cbw_handler`** (0x0803dcfc) to service EP1/EP2 completions; if a full CBW arrived it
  calls **`usb_scsi_handler`** (0x0803df20). Return bits also drive CSW-send (`FUN_0804002c`/
  `FUN_08040068`). [Proven]
- **`usb_scsi_handler`** (0x0803df20) additionally handles the **EP0 control** path
  (`param&1` → read RXCOUNT; an 8-byte packet = a SETUP → `usb_setup_request_handler`), the
  **bus-reset** path (`param&4` → clear 0x04000044 bit9, `usb_bus_reset`), and stall/flush
  recovery. It also nudges PHY 0x348 bit0 (FS force) when the FIFO backs up. [Proven]
- **The MUSB IRQ is top-level line 6** — `0x040000cc bit6 (0x40)`. Besides the vectored path,
  there is one *polling* consumer: **`usb_wait_host_sof` (0x08041ce4)** writes POWER=0x20, then
  polls `0x040000cc & 0x40` (clearing it when seen) for ≤ ~0xC3500 iterations before reading
  INTRUSB — returns 1 ("host alive") iff **SOF (bit3)** is set, or (SOF clear) **POWER bit4
  HSMode** is set. On exit it drops POWER=0, clears 0x04000058 bits0-2 and 0x0400000C bit20.
  No direct BL to it was found in the decomp set (likely a leftover/diagnostic probe). [Proven
  body; "no caller" from decomp grep — Open whether reached via a pointer]

### 1.5 CSR bit semantics the firmware actually uses (host-model contract)

These are the exact bits a virtual-host model must set/honour (standard MUSB peripheral-mode
meanings; every one below is read or written by the cited code): [Proven]

**CSR0 (INDEX=0, +0x12)** — `usb_scsi_handler` EP0 path:
| bit | MUSB name | firmware behaviour |
|---|---|---|
| 0 (0x01) | RxPktRdy | tested; set by *host model* when SETUP/OUT data staged → firmware reads RXCOUNT, 8 bytes ⇒ SETUP |
| 2 (0x04) | SentStall | tested → `sysapi_free(0)` (EP0 flush/clear) |
| 4 (0x10) | SetupEnd | tested → firmware writes **0x80** (ServicedSetupEnd) |
| 1 (0x02) | TxPktRdy | set by `usb_fifo_write` EP0: **`|= 0x02`** (full 64-B packet) or **`|= 0x0A`** (TxPktRdy\|DataEnd, last/short packet); a zero-length status stage writes **0x0A** with +0x330=0. Host model consumes staged bytes and clears it |

**RXCSR (INDEX=1, +0x16)** — `usb_bulk_cbw_handler` EP1-OUT path:
| bit | name | firmware behaviour |
|---|---|---|
| 0 (0x01) | RxPktRdy | tested (data available → dispatch flag 2); **cleared by `usb_fifo_read`** after draining (`&= 0xfe`) |
| 6 (0x40) | SentStall | tested → log "stall1", write-back `& ~0x40`, flush + error-CSW |

**TXCSR (INDEX=2, +0x12)** — `usb_bulk_cbw_handler` EP2-IN path:
| bit | name | firmware behaviour |
|---|---|---|
| 0 (0x01) | TxPktRdy | **written = 1** by `usb_fifo_write` (bulk) to arm a packet; tested clear = "TX done" → dispatch flag 4 (continue stream / CSW sent) |
| 1 (0x02) | FIFONotEmpty | tested set → `musb_ep_stall(2)` |
| 2 (0x04) | UnderRun | tested → write-back `& ~0x04`; if the read-stream ended, send the CSW |
| 5 (0x20) | SentStall | tested → log "stall2", write-back `& ~0x20`, flush + error-CSW |

**Interrupt regs** (all read together by `musb_read_intr` 0x08041b44, read-clear semantics
expected): INTRUSB bit0 Suspend / bit1 Resume / bit2 Reset / bit3 SOF (bits0-2 make
`usb_bulk_cbw_handler` return 1 = "bus event"); INTRTX bit0 = EP0, bit2 = EP2-IN completion;
INTRRX bit1 = EP1-OUT arrival. [Proven]

**Bulk data staging (Anyka L2 + DMA)** — `usb_fifo_write`/`usb_fifo_read`:
- EP0: word-staged in the RAM shadows (RX @0x08006A40, TX via FIFO0 +0x20 with byte writes),
  then **+0x330 = byte count**, **+0x340 |= 1** (EP0 trigger), CSR0 armed as above.
- Bulk PIO (len % 64 ≠ 0): word-staged in the 0x08006400/0x08006600 L2 windows;
  **+0x33c |= 1 (RX) / |= 4 (TX)** direction latch, **+0x338 = byte count**, **+0x340 = 4**
  (TX trigger), then TXCSR=1.
- Bulk DMA (len % 64 == 0): the **same hal-DMA engine the DAC uses** (`func_0x0800344c` =
  channel config, `func_0x08003530` = submit with a *physical* address from `func_0x08001990`)
  with USB channel codes **2 = EP1-RX** and **3 = EP2-TX**. A host model must service these
  submits by copying between the DRAM buffer and the virtual endpoint. [Proven code paths;
  channel-code naming Inferred]

---

## 2. Enumeration / descriptors

All descriptors are a static blob at **0x08041E7C–0x08041F28** in PROG. Served by
`FUN_0803d9fc` (string descriptors by index) and `FUN_0803d8e0` (the assembled config), with the
descriptor part-pointers cached in the `usb_dev_open` object by `FUN_0803da4c`. [Proven]

### 2.1 Device descriptor (@0x08041E95, 18 bytes)

```
12 01 00 02  00 00 00 40  46 25 01 E3  00 01 02 03  01
```
bcdUSB **2.00**, class/subclass/proto 0/0/0 (defined at interface), **bMaxPacketSize0 = 0x40**,
**idVendor = 0x2546**, **idProduct = 0xE301**, bcdDevice 0x0001, iManufacturer 1, iProduct 2,
iSerial 3, **1 configuration**. [Proven]

### 2.2 Device-qualifier (@0x08041EB9) & other-speed-config (@0x08041EC3)

`0A 06 00 02 00 00 06 40 01 00` (qualifier: HS-capable, other-speed MaxPacket 0x40) and a
9-byte **OTHER_SPEED_CONFIGURATION** header `09 07 20 00 01 01 00 C0 C8` — the device answers HS
enumeration correctly. [Proven]

### 2.3 Configuration (@0x08041EA7, wTotalLength = 0x20 = 32)

```
config    09 02 20 00 01 01 00 C0 C8   1 iface, self-powered(0xC0), MaxPower 0xC8 (400 mA)
interface 09 04 00 00 02 08 06 50 01   bInterfaceClass=08 MSC, SubClass=06 SCSI, Proto=50 BOT, 2 EPs
endpoint  07 05 01 02 00 02 00         EP1 OUT, bulk, wMaxPacketSize=0x0200 (512)
endpoint  07 05 82 02 00 02 00         EP2 IN,  bulk, wMaxPacketSize=0x0200 (512)
```
(A third `07 05 03 02 00 02 00` EP3-OUT descriptor also sits in the blob at 0x08041E8E and is
copied by the config assembler slot but the interface declares only 2 EPs — likely an unused/FS
spare. [Proven bytes; role Inferred/Open]) `FUN_0803d8e0` patches the EP `wMaxPacketSize` bytes to
**0x40** when the negotiated speed is full-speed (`*DAT_0803dc9c != 0x100`) and **0x0200** at HS.
[Proven]

### 2.4 String descriptors (`FUN_0803d9fc`)

| index | len | content |
|---|---|---|
| 0 | 4 | LANGID `04 09` (US-English) |
| 1 (iManufacturer) | 0x16 | `"OID     "` |
| 2 (iProduct) | 0x16 | `"OID Player"` |
| 3 (iSerial) | 0x10 | `"USB 2.0"` |

A separate ASCII serial `"A55A55A55A55A55"` sits at 0x08041F10 (used by the vendor channel, not
the USB serial string). [Proven]

### 2.5 SETUP / control request handling

`usb_setup_request_handler` (0x08040b48) reads the 8-byte SETUP from the EP0 shadow, splits on
`bmRequestType` direction/type bits (`&0x60`: 0=standard, 0x20=class, 0x40=vendor) and dispatches
through a **16-entry request table at 0x08041FA4** indexed by `bRequest & 0x0F`:

| bRequest | handler | meaning |
|---|---|---|
| 0 | 0x08040C6C | GET_STATUS |
| 1 | 0x08040CD8 | CLEAR_FEATURE (clears EP halt) |
| 3 | 0x08040D44 | SET_FEATURE |
| 5 | `usb_set_address` 0x08040DF8 | SET_ADDRESS (writes FAddr) |
| 6 | 0x08040E7C | **GET_DESCRIPTOR** (device/config/string/qualifier via `FUN_0803d9fc`/`FUN_0803d8e0`) |
| 7 | 0x080411AC | GET_CONFIGURATION |
| 8 | 0x080411B4 | SET_CONFIGURATION |
| 9 | 0x080411FC | (GET/SET_INTERFACE) |
| 10/11/12 | 0x0804125C/98/0x080412FC | interface/endpoint requests + class MSC (Get-Max-LUN / BOT reset) |
| 2/4/13/14/15 | 0x08040B38 | stub/ignore |

The class vtable (`*DAT_08040c50`, at RAM `0x081DB978`) provides `+0x34/+0x38/+0x3c` entry points
for standard/class/vendor SETUP tails. [Proven table; individual GET_MAX_LUN slot Inferred]

---

## 3. USB device open / init (`usb_power_switch` prologue)

The device is brought up lazily when state 5 runs, inside `usb_power_switch` (0x0803d1d4):

1. `physpool_alloc(0x8000)` — a **32 KB shared transfer buffer**; `physpool_alloc(0x2000)` — 8 KB
   DMA scratch. [Proven]
2. **`FUN_0803db9c`** → **`FUN_080402f8`** = `usb_dev_open(buffer, 0x8000)`: allocates the MSC
   context sub-objects — a 0x114-byte **LUN/device array** (`msc_ctx+8`), a 0x13-byte **CBW parse
   object** (`msc_ctx+0x24`), a **phase/CSW object** (`msc_ctx+0x28`), and two 0x24-byte **stream
   descriptors** for read (`+0x30`) and write (`+0x2c`). It records the FIFO buffer and its size
   (must be ≥ 0x200). [Proven]
3. **`FUN_0803dbd8`** → **`FUN_0803da4c`** = `usb_phy_start`: builds the descriptor-pointer table
   into the dev object, allocates a 0x1000 EP0 buffer, wires the callbacks
   (`FUN_0803dc58`=EP0 data helper, `FUN_08040108`=bulk data-phase pump, `0x0803f31c`, stubs
   0x08040b38/3c), then `usb_report_speed()` to advertise on the bus. [Proven]

`msc_ctx` = `0x081DBABC` (pools `DAT_0803f050`, `DAT_080407b8`, `DAT_0803dcb4`). Key fields:
`+8` LUN array, `+0x24` CBW obj, `+0x28` phase/CSW obj, `+0xC/+0x10/+0x1C` residue trackers,
`+0x2c` write-stream, `+0x30` read-stream. Each **LUN entry (0x5C bytes)** holds: `+0xC` block
size, `+0x10` medium-type (3 = removable/NFTL-backed), `+0x14` **medium device handle** (the
FatLib/NFTL object for B:), `+0x18` inquiry-length, `+0x19` unit-attention flag, `+0x1B` inquiry
data. `msc_ctx+0x24 (+0xD)` = current **bCBWLUN**. [Proven field uses; struct labels Inferred]

---

## 4. Bulk-Only Transport + SCSI

### 4.1 The BOT phase machine (`*msc_phase` = byte @0x08122718)

`FUN_0803dcf0` reads it, `FUN_0803df14` writes it. Also mirrored per-transfer in the phase/CSW
object `msc_ctx+0x28`[0]:

| value | phase |
|---|---|
| 0 | **idle** — waiting for a CBW on EP1 |
| 1 | **command / data-out active** (WRITE streaming in) |
| 2 | **data-in active** (READ streaming out on EP2) |
| 3 | **status** — send the 13-byte CSW |
| 5 / 6 | read-busy / write-busy (drives the on-screen animation in the service loop) |
| 7 | vendor "exit"/done (from the ANYKA magic command) |

### 4.2 CBW (Command Block Wrapper) — `FUN_08040068`

A completed EP1-OUT transfer of **0x1F (31) bytes** is treated as a CBW: it is copied into the
0x13-byte CBW object and its `dCBWSignature` is checked against **`DAT_080407bc` = "USBC"**
(0x43425355). Valid → phase 1, dispatch to the SCSI decoder; invalid → `FUN_08040b0c` (stall).
`dCBWDataTransferLength` (`+0xC`) and `bCBWLUN` (`+0xD`) are extracted. [Proven]

### 4.3 CSW (Command Status Wrapper) — `FUN_0803ef94`

The 13-byte CSW is built from **`"USBS"`** (0x53425355, the first 4 bytes of the ROM string
`0x0803f060 "USBSA:/Product log file.bin"` — the signature and an unrelated filename happen to be
adjacent), the CBW tag, the computed `dCSWDataResidue` (expected − actually transferred), and a
status byte (0 = good, 1 = failed, from the per-LUN sense state `DAT_0803f054`). Sent on EP2 IN
(`FUN_080417f4(2,…,0xD)`), then phase → 0 (idle). [Proven]

### 4.4 SCSI command decoder — `FUN_0803fb48`

Dispatched on the opcode (CDB byte 0). Standard MSC set:

| opcode | command | handling |
|---|---|---|
| **0x00** | TEST UNIT READY | medium-ready check (`FUN_0803eea4`); good or NOT-READY sense |
| **0x03** | REQUEST SENSE | returns up to 0x12 bytes of fixed sense (clears the pending sense) |
| **0x12** | **INQUIRY** | returns the LUN's inquiry buffer (`LUN+0x1B`); vendor/product/rev = `"MP4 "`/`"MP4 Player "`/`"V1.0"` (§2 blob @0x08041F28/F52) |
| **0x1A** | MODE SENSE(6) | 4- or 12-byte mode page (page 0x3F = all) from `DAT_08040b30/34` |
| **0x1B** | START STOP UNIT | eject/load: CDB[4]&1 sets removal flag |
| **0x1E** | PREVENT/ALLOW MEDIUM REMOVAL | toggles the removable-lock flag |
| **0x23** | READ FORMAT CAPACITIES | reads block 0 into a 0x200 scratch, returns a 12-byte capacity list |
| **0x25** | **READ CAPACITY(10)** | reads block 0 to spin up, returns 8 bytes (last-LBA, block-size) |
| **0x28** | **READ(10)** | sets up the **read stream** (LBA=CDB[2..5], count=CDB[7..8]); phase → data-in; pumped by `FUN_08040108` |
| **0x2A** | **WRITE(10)** | sets up the **write stream**; phase → data-out; data drained to the medium |
| **0x2F** | VERIFY(10) | ready-check only, good status |
| **0x5A** | MODE SENSE(10) | 8-byte zero mode header |
| **0xA8** | **READ(12)** | 32-bit block count variant of READ(10) |
| **0xAA** | **WRITE(12)** | 32-bit block count variant of WRITE(10) |
| default | — | `FUN_08040620` → 0x20-byte default response / unsupported-command sense |

**Data path (the B: mapping).** READ/WRITE set up a stream descriptor with the byte offset
`LBA × block_size` and total `count × block_size`, then **`FUN_08040108`** (the data-phase pump)
repeatedly calls the LUN medium's read/write method — `(*(LUN+0x14 vtable))(handle, buf, offset,
len)` — moving `block_size`-sized chunks between the 32 KB USB buffer and the **B: medium object**,
until the residue reaches 0, then emits the CSW. The LBAs therefore address **B:'s sectors
directly** — the PC sees a raw superfloppy over the FatLib/NFTL medium, no partition-table
remapping. `FUN_0803eea4` gates every access on medium-ready (medium-type 3 + present). [Proven
data flow; the medium object identity cross-refs `partition-a-fat-vs-mbr.md`/`nftl-layout.md`]

### 4.5 Vendor command channel (opcodes 0xCC–0xE2)

A production/manufacturing back-channel, each guarded by an ASCII magic in the CDB:

| opcode | magic (CDB bytes) | action |
|---|---|---|
| 0xCC | `"ANYKA"` (41 4E 59 4B 41) | enter special mode → phase 7 (`FUN_0803df14(7)`) |
| 0xCD | `"FM"` (46 4D) | `FUN_0803d0cc(CDB[3..4])` — format/media op, then re-init |
| 0xCE | `"NO"` (4E 4F) | `FUN_080ee1d8(CDB+3)` — write config/params |
| 0xCF | `"AD"` (41 44) | status/ack |
| 0xD0 | — | `serial_is_valid()` — read/validate the pen serial |
| 0xE0 | — | open `A:/Product log file.bin` for reading (`func_0x08007938(10)`) |
| 0xE1 | — | stream out the log in 0x1C0-byte chunks (`fs_read`) |
| 0xE2 | — | close the log file (`FUN_080ad514`) |

These confirm the `"USBSA:/Product log file.bin"` string and are how the Anyka line writes serials
and dumps diagnostics. Not part of normal user-drive operation. [Proven]

---

## 5. State 5 (USB-PC) behaviour

Entered from state 8 when `usb_connect_handler` sees INTRUSB **Reset/SOF** within 0x28 settle
ticks → "`usb connect pc!!`" → event **0x105C** (see `pmu-power-management.md` §2/§4). The
statechart runs three functions:

1. **`usb_state_dbg` (0x08050fb8)** = state-5 entry action: `func_0x08007938(5)` (mode marker),
   **`audio_amp_disable`** (gameplay/audio suspended), `akoid_rearm`, GPIO12 amp strobe, sets the
   session byte **`*0x08051018 = 1`**, logs `"usbstate %d"`. [Proven]
2. **`usb_state_handler` (0x08051090)** = state-5 tick: consumes the session flag; **drives the
   power-hold GPIO15 = 1** (keeps the pen alive while plugged), then runs, in decomp order,
   *`FUN_080ae75c` update-list → the blocking `usb_power_switch()` session (which builds the
   exported LUN itself via `FUN_0803ece4(1)`) → **after unplug**: `fs_partition_scan` on B:
   (→ `fat_format_wrapper` if the scan fails, i.e. the PC wiped it — a `while(true)` hard-hang
   if even the format fails), `FUN_0803a1c8` re-register, `fs_partition_scan` on A:, and hook
   A: back into the mount-table slot*. So the B: FAT rescan/format is the **post-session
   re-mount** of whatever the PC left, not session prep, and the LUN is registered inside the
   service loop rather than before it. Logs `"paint usb state %d"`, `"update list"`. [Proven]
3. **`usb_power_switch` (0x0803d1d4)** = the **MSC service loop** (misnamed — it does far more than
   the power path): opens the USB device (§3), then spins in `while(true)` polling the BOT phase
   (`FUN_0803dcf0`) and driving the enumeration/transfers:
   - phase 5/6 → play the read/write **activity animation** (`FUN_0803d16c`, `FUN_0803d10c`);
   - after **1,200,000** idle iterations → `pwr_path_usb(1)` = "`switch to usb power.`" (release the
     battery latch, run from VBUS — `pmu` §4.5);
   - **GPIO8 == 0** (cable gone, re-checked after a debounce delay) → break;
   - phase **7 or 3** with the "usb out" condition → `pwr_path_battery`, log "`usb out`", break;
   - every 0x2BC0 iterations → periodic **flush** of the medium (log "`flush`").
   On exit: flush, `FUN_0803dbec` (phase 3, free the dev object, **`usb_phy_off`**), free the 32 KB/
   8 KB buffers. **Except phase 7** (the vendor "ANYKA" command): that path runs `FUN_0803dbec` then
   **`flash_program_region()` and spins in a deliberate `while(true)`** — the pen parks forever,
   waiting for the production tool to reflash/power-cycle it. Never returns to the statechart.
   [Proven — 0x0803d1d4.c tail]
4. **`FUN_08051138`** = state-5 event filter: on the tick/`0x1046` heartbeat or a `0x105F` button
   while the session byte is clear, posts **`DAT_08051198`** → event **0x100C** → **standby (state 3)**.
   `pwr_path_battery` re-latches GPIO15 so the pen keeps running on battery after unplug. [Proven]

So: **plug → state 8 classify → state 5**: suspend audio, latch power, run the BOT loop serving
the PC; **unplug → re-scan/format B:, 0x100C → standby**, back on battery. [Proven]

### 5.1 Live-pen RAM dump cross-check (window 0x081D8000–0x081DFFFF)

*(Cross-checked against a RAM dump captured from a real pen; the dump itself is obtained
separately from hardware and is not part of this repository.)*

The hardware RAM dump confirms the static analysis of the MSC context: [Proven bytes; history Inferred]

| addr | dump value | meaning |
|---|---|---|
| msc_ctx+0xC (0x081DBAC8) | **0x0814C000** | the USB transfer buffer pointer — `usb_dev_open(buf, 0x8000)` field |
| msc_ctx+0x10 (0x081DBACC) | **0x00004000** | `= (0x8000 & ~0x3F) >> 1` — exactly the `usb_dev_open` size formula |
| msc_ctx+0x1C (0x081DBAD8) | **0x0814C000** | buffer mirror field (residue base) — same formula source |
| msc_ctx+8 / +0x24 / +0x28 / +0x2C / +0x30 | **0** | LUN array / CBW / phase / stream sub-objects all NULL |
| partB slot 0x081DB910 | **0x08145C00** | live partition-B object pointer (cf. `ab-drive-layout.md`) |
| 0x081DB984…0x081DB99C | 0x08038CF8, 0x08038D04, 0x08009C71, 0x08009C85, 0x081117C9, 0x08009C19 | populated function-pointer slots of the class-vtable area @0x081DB978 (§2.5) |

Reading: the buffer-geometry fields match `usb_dev_open(0x0814C000, 0x8000)` **bit-exactly**
(strong live confirmation that the pen ran a USB-PC session with the 32 KB buffer at
0x0814C000), while the heap sub-objects are NULL — consistent with `FUN_0803dbec`'s teardown
(frees each sub-object and writes back the NULL), i.e. the dump was taken **after** an
unplug/cleanup, with the stale buffer fields never zeroed. [Inferred from the field pattern]

---
## 6. Proposed names / docstrings (for names.csv)

| addr | name | docstring |
|---|---|---|
| 0x0803d1d4 | (keep `usb_power_switch`) | **MSC service loop**: usb_dev_open → poll BOT phase, serve transfers, animation, switch to USB power after 1.2M idle iters, exit on unplug/phase7 → usb_phy_off |
| 0x08051090 | (keep `usb_state_handler`) | state-5 tick: GPIO15=1, rescan/format B: FAT, register LUN, run MSC loop |
| 0x08050fb8 | (keep `usb_state_dbg`) | state-5 entry: audio off, akoid rearm, session byte @0x08051018=1 |
| 0x08051138 | `usb_state_exit_filter` | on tick/button with session clear → post 0x100C → standby |
| 0x080402f8 | `usb_dev_open` | alloc MSC ctx sub-objects (LUN array, CBW/CSW, R/W streams); FIFO buf ≥0x200 |
| 0x0803db9c | `usb_dev_open_wrap` | alloc 0x4c dev obj + usb_dev_open |
| 0x0803da4c | `usb_phy_start` | build descriptor table, EP0 buf, callbacks, usb_report_speed |
| 0x0803db24 | `usb_irq_dispatch` | read INTRUSB/TX/RX; usb_bulk_cbw_handler → usb_scsi_handler; drive CSW |
| 0x0803dcfc | (keep `usb_bulk_cbw_handler`) | EP1/EP2 completion service; returns dispatch flags |
| 0x0803df20 | (keep `usb_scsi_handler`) | EP0 SETUP (8B→setup handler), bus-reset, stall recovery, kick SCSI |
| 0x0803fb48 | `usb_scsi_decode` | SCSI opcode decoder (INQUIRY/READ CAPACITY/READ10/12/WRITE10/12 + vendor 0xCC-0xE2) |
| 0x08040068 | `usb_cbw_parse` | 31B CBW, check "USBC" @0x080407bc, phase→1 |
| 0x0803ef94 | `usb_csw_send` | build 13B CSW "USBS"+tag+residue+status, send EP2, phase→0 |
| 0x08040108 | `usb_data_phase_pump` | stream READ/WRITE chunks to/from the B: medium object (LUN+0x14) |
| 0x0803eea4 | `usb_medium_ready` | medium-type-3 present check + unit-attention |
| 0x0803dcf0 / 0x0803df14 | `usb_phase_get` / `usb_phase_set` | BOT phase byte @0x08122718 |
| 0x08040b48 | (keep `usb_setup_request_handler`) | EP0 SETUP → 16-entry req table @0x08041FA4 by bRequest&0xF |
| 0x08040e7c | `usb_get_descriptor` | GET_DESCRIPTOR via FUN_0803d9fc/FUN_0803d8e0 |
| 0x0803d9fc / 0x0803d8e0 | `usb_string_descriptor` / `usb_build_config_desc` | string table; assemble+patch config for FS/HS |
| 0x080414e4 | (keep `usb_bus_reset`) | reset: addr=0, EP1/EP2 FIFO 0x200(HS)/0x40(FS), INTRTXE=5/RXE=2/USBE=0xF7 |
| 0x08041460 | `usb_ep_config` | per-EP max-packet/FIFO setup |
| 0x08041684 / 0x080417f4 | `usb_fifo_read` / `usb_fifo_write` | EP FIFO PIO/DMA (DMA fast-path when len%64==0) |
| 0x08041b30 / 0x08041b20 / 0x08041b64 / 0x08041b44 | `musb_csr_read` / `musb_csr_write` / `musb_rxcount` / `musb_read_intr` | INDEX(+0xE)+CSR(+0x12/+0x16), RXCOUNT(+0x18), INTRUSB/TX/RX(+0xA/2/4) |
| 0x08041ab4 | (keep `sysapi_free`) | **misnomer**: MUSB per-EP CSR flush/clear (idx0/1/2) |
| 0x08041bb4 | `musb_ep_stall` | set stall bit on EP1/EP2 CSR |
| 0x08040df8 | (keep `usb_set_address`) | write MUSB FAddr (+0x00) |
| 0x08041ce4 | (keep `usb_wait_host_sof`) | POWER=0x20, poll 0x040000cc bit6 (line 6) then INTRUSB SOF / POWER HSMode ≤ ~0xC3500 iters; 1 = host alive; no decomp caller found |
| globals | | `0x081DBABC` msc_ctx; `0x08122718` BOT phase; `0x08051018` state-5 session byte; `0x080407BC` "USBC" CBW sig; `0x0803F060` "USBS…" CSW sig+logname; `0x08041E7C` descriptor blob; `0x08041FA4` EP0 request table; `0x04070000` MUSB base |

---

## 7. Evidence index

| claim | status | source |
|---|---|---|
| MSC device: class 08/06/50, 2 bulk EPs, VID 0x2546/PID 0xE301 | Proven | descriptor blob 0x08041E7C-F28 (PROG) |
| MUSB indexed reg model, offsets +0x0E INDEX/+0x12 CSR/+0x16 RXCSR/+0x18 RXCOUNT/+0x20 FIFO0 | Proven | `FUN_08041b30/20/64/44`, `usb_bus_reset`, `sysapi_free` (base 0x04070000) |
| bus reset EP1/EP2 512/64B, INTRTXE=5/RXE=2/USBE=0xF7 | Proven | 0x080414e4.c |
| CBW "USBC" 31B check; CSW "USBS" 13B | Proven | 0x08040068.c (`DAT_080407bc`), 0x0803ef94.c (`0x0803f060`) |
| SCSI opcode set incl READ10/WRITE10/12 + vendor 0xCC-0xE2 | Proven | 0x0803fb48.c |
| READ/WRITE map to B: medium sectors via LUN+0x14 vtable | Proven data flow | 0x08040108.c, 0x0803fb48.c (0x28/0x2A branches) |
| INQUIRY "MP4/MP4 Player/V1.0"; strings OID/OID Player/USB 2.0 | Proven | 0x08041F28/F52, 0x08041ECC-EF8 (via `FUN_0803d9fc`) |
| state-5 entry/tick/loop/exit (0x08050fb8/0x08051090/0x0803d1d4/0x08051138) | Proven | those decomp files |
| B: reformatted if scan empty during state 5 | Proven | 0x08051090.c (`fat_format_wrapper`) |
| GPIO8=0 keeps the whole stack dormant | Proven | `usb_connect_handler` gate; see `pmu-power-management.md` |
| USB service loop reached by direct BL (no data-ref to 0x0803db24) | Proven | binary xref scan (no pool reference) |
| "DMA" fast-path label, EP3-OUT spare descriptor role | Inferred | 64-byte alignment branch; iface declares 2 EPs |
| CSR bit contract: CSR0 0x01/0x04/0x10/0x80/0x02/0x0A; RXCSR 0x01/0x40; TXCSR 0x01/0x02/0x04/0x20 | Proven | 0x0803df20.c (EP0 path), 0x0803dcfc.c (bulk path), 0x080417f4.c/0x08041684.c (arm/clear) |
| L2 trigger regs: +0x330 EP0 count, +0x338 bulk count, +0x33c dir latch (1=RX/4=TX), +0x340 trigger; DMA ch 2=RX/3=TX via hal-DMA | Proven mechanics / ch-naming Inferred | 0x080417f4.c, 0x08041684.c |
| state-5 tick order: update-list → blocking `usb_power_switch()` → post-unplug B: scan/format/re-register | Proven | 0x08051090.c (scan calls after the loop call) |
| phase 7 (vendor "ANYKA") exit → `flash_program_region()` + deliberate infinite loop (production reflash park) | Proven | 0x0803d1d4.c tail (`unaff_r4 == 7` branch) |
| periodic medium flush every 0x2BC0 loop iterations ("flush") | Proven | 0x0803d1d4.c (`uVar9 == 0x2bc0`) |
| `usb_wait_host_sof` polls 0x040000cc bit6 first, then INTRUSB SOF / POWER HSMode; no decomp caller | Proven body / caller Open | 0x08041ce4.c; grep over decomp |
| live dump: msc_ctx buffer fields = usb_dev_open(0x0814C000, 0x8000) bit-exact; sub-objects NULLed (post-teardown); partB ptr 0x08145C00 | Proven bytes / history Inferred | live-pen RAM dump window 0x081D8000 (§5.1) |
