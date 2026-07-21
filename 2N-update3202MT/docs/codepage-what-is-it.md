# What is "codepage" (codepage.bin and the codepage_* functions)?

**Verdict: the name is correct — and it is even the firmware's own name.**
`codepage.bin` is a **character-encoding conversion database** (a Win32-NLS-style
"codepage" bank): 61 codepage↔UTF-16 mapping tables plus shared Unicode
composition/decomposition tables. It is **not** a font/glyph store, **not** a
label/string store, and **not** GB2312-specific. The `codepage_*` functions
implement `MultiByteToWideChar`/`WideCharToMultiByte`-style string conversion,
consumed by the filesystem layer (Anyka `Fwl_osFS`, which handles file names as
UTF-16). The pen has no display; nothing here renders anything.

This is encoding data, not fonts or labels. The only GB connection is that
**cp936 (GBK, the GB2312 superset) is one of the 5 double-byte tables and is the
*default* codepage** (Anyka is a Chinese chipset vendor; the SDK defaults to
Chinese).

Evidence status: everything below marked *(Proven)* was verified either byte-by-byte
against reference encodings or read directly from the decompiled code;
*(Inferred)* items are interpretations consistent with all observations.

---

## 1. The firmware names it "codepage" itself *(Proven)*

`codepage_load` @0x08030e4c reads sectors of a NAND-resident file using the
literal strings:

- `W:\codepage.bin` (passed to the sector reader `FUN_08030da4`)
- `"codepage"` (used with `FUN_08009820` to locate a **backup copy** after a
  `"codepage recover data error"`; final failure prints
  `"codepage get backup error"`)

So the section name `codepage.bin` in our update-file extraction matches the
vendor's own file name.

## 2. File format of `data/codepage.bin` (879,820 = 0xD6CCC bytes)

Layout *(Proven by parsing; the regions tile the file exactly, last table ends
at 0xD6CCC = EOF)*:

| offset | size | content |
|---|---|---|
| 0x00000–0x0079F | 61 × 0x20 | codepage descriptor records (sel = 0..0x3C) |
| 0x007A0–0x0182B | 0x108C | shared Unicode **composition** table ("E") |
| 0x0182C–0x03BCB | 0x23A0 | shared Unicode **decomposition** trie ("F") |
| 0x03BCC–0xD6CCB | — | per-codepage tables A/B/C/D, packed in sel order |

### 2.1 Descriptor record (32 bytes, one per codepage) *(Proven)*

Example, record sel=0 (bytes at file offset 0):
`01 45 25 00 3F 00 3F 00  CC 3B 00 00 00 00 00 00  CC 3D 00 00 CC 4F 00 00  A0 07 00 00 2C 18 00 00`

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | u8 | bytes per char: 1 = SBCS, 2 = DBCS | returned by `codepage_get_widechar` (header+0x18); branches SBCS/DBCS in `FUN_0803136c` |
| +0x01 | u8 | =0x45 (69) — **entry count of the shared composition table E** | returned by `FUN_080310b8` (header+0x19) and used as the binary-search bound in `FUN_080fd968`; constant because E is shared |
| +0x02 | u16 | codepage ID, **truncated to 8 bits** (= Windows codepage number mod 256) | see identification table below; cp037 stores 0x25=37 exactly, cp424 stores 0xA8=424 mod 256, etc. |
| +0x04 | u16 | =0x003F `'?'` — default char (MB side) | `FUN_0803102c` (header+0x1C); compared against in the "invalid char" checks of `FUN_0803136c` |
| +0x06 | u16 | =0x003F `'?'` — default char (Unicode side) | `FUN_08031038` (header+0x1E); unmapped A-table entries hold 0x3F |
| +0x08 | u32 | **table A**: multibyte → UTF-16 | `FUN_08031044` reads u16 at `A + idx*2` |
| +0x0C | u32 | **table B**: DBCS lead-byte table (256 × u8); 0 for SBCS | `FUN_08031054` reads byte at `B + c` |
| +0x10 | u32 | **table C**: UTF-16 → multibyte best-fit (flat, U+0000..U+11FF) | `FUN_08031064` reads byte (SBCS) / u16 (DBCS) at `C + cp` |
| +0x14 | u32 | **table D**: 256 × u16 per-char flags (ctype-like) | `FUN_08031088`; e.g. cp1252: letters→0x1100, space→0x0700 *(Inferred: exact flag semantics)* |
| +0x18 | u32 | =0x07A0 — shared composition table E | `FUN_08031098` reads u16 at `E + idx*2` |
| +0x1C | u32 | =0x182C — shared decomposition trie F | `FUN_080310a8` reads u16 at `F + idx*2` |

### 2.2 Table A — the actual conversion tables *(Proven byte-for-byte)*

- **SBCS** (widechar=1): 256 × u16, `A[byte] = UTF-16 code unit`. Verified
  **exact 256/256 matches** against Python codecs for cp037 (EBCDIC! 0xC1→'A',
  0x81→'a'), cp437, cp850, cp866, KOI8-R (both sel 0x15 and 0x2C), Latin-1
  (identity), ISO-8859-5, cp874 (0xA1→U+0E01), ISO-8859-15 (0xA4→U+20AC €).
  cp1250/cp1252 match on all 251 defined slots. Mac Roman matches the
  pre-1998 Apple variant (0xBD→U+2126 OHM SIGN, 0xDB→U+00A4, not €).
- **DBCS** (widechar=2): `A[ B[lead]*256 + trail ] = UTF-16`, where B is the
  lead-byte table (0 = not a lead byte, else row number). Verified:
  **cp936/GBK 21791/21791 exact**, **cp949 17048/17048 exact**,
  **EUC-JP (20932) 6879/6879 exact**; cp932 exact except Shift-JIS
  user-defined/PUA rows (mapped to 0x3F); Big5 matches the unicode.org
  BIG5.TXT variant (260 systematic diffs vs Microsoft cp950, e.g.
  0xA145→U+2027 not U+2022).

### 2.3 Table C — reverse (Unicode→local) best-fit *(Proven structure)*

Flat table indexed by codepoint, covering U+0000–U+11FF (SBCS: 0x1200 bytes,
1 byte/cp; DBCS: u16/cp). E.g. cp437's C table at cp range U+0100.. reads
`41 61 41 61 41 61 43 63 …` = Ā→'A', ā→'a', Ă→'A', ă→'a', Ć→'C' … i.e.
classic best-fit folding of Latin Extended-A onto ASCII.

### 2.4 Shared tables E and F *(Proven use, Inferred detail)*

- **E @0x7A0** (69 entries, count stored in record byte +0x01): sorted
  `(combining-char, subtable-offset)` pairs, binary-searched by `FUN_080fd968`
  to **compose** base char + combining mark → precomposed char (first keys:
  U+0300 grave, U+0301 acute, U+0302 circumflex, …). Windows analog:
  MB_PRECOMPOSED.
- **F @0x182C**: nibble-indexed trie walked recursively by `FUN_08031184` to
  **decompose** a char into base + combining marks (MB_COMPOSITE analog).

### 2.5 Codepage identification table *(Proven for the ~20 spot-checked tables; rest identified via the consistent id-mod-256 + ordering pattern)*

| sel | cp | | sel | cp | | sel | cp |
|---|---|---|---|---|---|---|---|
| 00 | 37 EBCDIC-US | | 15 | 878 KOI8-R | | 2A | 10081 MacTurkish |
| 01 | 424 EBCDIC-Heb | | **16** | **932 Shift-JIS (DB)** | | 2B | 20127 US-ASCII* |
| 02 | 437 OEM-US | | **17** | **936 GBK (DB, default)** | | 2C | 20866 KOI8-R |
| 03 | 500 EBCDIC | | **18** | **949 Korean (DB)** | | **2D** | **20932 EUC-JP (DB)** |
| 04 | 737 OEM-Greek | | **19** | **950 Big5 (DB)** | | 2E | 21866 KOI8-U |
| 05 | 775 Baltic | | 1A | 1006 Urdu | | 2F | 28591 ISO-8859-1 |
| 06 | 850 Latin-1 OEM | | 1B | 1026 EBCDIC-Tr | | 30 | 28592 ISO-8859-2 |
| 07 | 852 Latin-2 OEM | | 1C–24 | 1250–1258 | | 31–38 | 28593–28600 (8859-3..10) |
| 08 | 855 Cyrillic OEM | | 25 | 10000 MacRoman | | 39 | 28603 ISO-8859-13 |
| 09 | 856 Hebrew | | 26 | 10006 MacGreek | | 3A | 28604 ISO-8859-14 |
| 0A | 857 Turkish OEM | | 27 | 10007 MacCyrillic | | 3B | 28605 ISO-8859-15 |
| 0B | 860 Portuguese | | 28 | 10029 MacLatin2 | | 3C | 28606 ISO-8859-16 |
| 0C–12 | 861–866, 869 | | 29 | 10079 MacIceland | | **3D** | **UTF-8 (in code)** |
| 13 | 874 Thai | | | | | | |
| 14 | 875 EBCDIC-Gr | | | | | | |

\* sel 0x2B matches ASCII except 0x27/0x60 → U+2019/U+2018 (typographic quotes).
Codepage id **0x3D is UTF-8**, implemented in code, not in the file: the
converters special-case `param_1 == 0x3d` and call a real UTF-8
decoder/encoder (`FUN_080fd714` / `FUN_080fd630` — visible `|0xC0`, `|0xE0`,
`^0x80 < 0x40` continuation-byte logic). Ids > 0x3E are rejected.

## 3. What the functions actually do

- **`codepage_load(fileOffset, buf)`** @0x08030e4c *(Proven)* — not a "load
  the codepage" initializer but a **cached sector read** from
  `W:\codepage.bin`: cluster = offset >> shift, per-cluster u16 NAND-block map
  at state+6, 0x200-byte sector reads via `FUN_08030da4`; on read failure it
  re-locates the cluster from a **backup copy** and patches the block map.
- **`codepage_get_header(sel)`** @0x08030fb4 *(Proven)* — copies the 32-byte
  descriptor at file offset `sel<<5` into the state struct at
  `DAT_08030f58+0x18` (Win32 `GetCPInfo` analog). All getters below read that
  cached header.
- **`codepage_get_widechar()`** @0x080310d0 *(Proven)* — returns header byte 0,
  i.e. **bytes-per-char (1 or 2), a DBCS flag — not a wide character**.
- **`map_gb0_to_codepage_sel(lang)`** @0x08031824 (identical twin
  `FUN_080318e0` reads the global itself) *(Proven code, Inferred semantics)* —
  maps a **language/region config byte** (0..0x13, from `DAT_080390c4[0]`) to a
  codepage sel: 3→Big5, 4→Shift-JIS, 5→cp949, 6–9→ISO-8859-1, 0xA→cp860,
  0xB→cp850, 0xC–0xF→ISO-8859-2, 0x10→cp866, 0x11→cp857, 0x12→cp1255,
  0x13→cp874, **anything else (incl. 0–2) → cp936 GBK**. The Chinese default
  is Anyka SDK heritage, not evidence the German pen uses GB2312.
- **`FUN_0803136c` / `FUN_08031630` / `FUN_08031210`** *(Proven)* —
  `MultiByteToWideChar` core: SBCS path maps each byte through table A; DBCS
  path checks table B for lead bytes and maps `A[B[lead]*256+trail]`; flag
  bit 3 = validate ("fail on undefined char", comparing against the 0x3F
  default chars), flag bit 1 = decomposed output via `FUN_08031184`.
- **`FUN_0803193c`** (MB→WC, id 0x3D→UTF-8) and **`FUN_080ac4a4`**
  (WC→MB, id 0x3D→UTF-8) *(Proven)* — the public string-conversion API.
- **`boot_read_serial_and_codepage(sel)`** @0x0803a3b0 — module init, called
  once from `app_init_main` @0x08038f5c right after `fs_storage_mount_init()`
  with `sel = map_gb0_to_codepage_sel(config_lang)`. It:
  1. computes the two cache shift values (log2 of NAND page/block geometry,
     capped at 0x1000) into state+2/+4 (`FUN_0803a9f0`) *(Proven)*;
  2. copies **8 bytes** to state+6 — which is exactly the per-cluster u16
     **NAND block map** used by `codepage_load`, i.e. 4 u16 block numbers
     (879 KB / 256 KB clusters ≈ 4 clusters) — **not a serial number**
     *(Inferred, strong)*;
  3. calls `codepage_get_header(sel)` and sets a ready flag.

## 4. Who consumes it → purpose *(Proven callers, Inferred purpose)*

The conversion API is used by the **filesystem/file-name layer**: callers sit
next to `Fwl_osFS.c` assert strings (`Fwl_GetRootDir…`, @0x080addb4) and the
media-list scanners for `a:\oidfilelist.lst`, `a:\voicelist.lst`,
`a:\musiclist.lst` (@0x080ae2c0, 0x080ae6c4). The Anyka SDK keeps file
names/paths as UTF-16 internally (FAT long file names are UTF-16 on disk) and
converts to/from the configured local 8-bit/DBCS encoding at the API surface.
No audio, OID, or UI code touches it. The pen has no display, and nothing in
the file is pixel data — record and table sizes are fully accounted for by
mapping tables (a CJK font at any resolution would dwarf 0xD6CCC bytes for
21k+ glyphs anyway... at 16×16 that alone is ~680 KB *per DBCS set* ×5).

## 5. Naming assessment

| current name | verdict |
|---|---|
| `codepage.bin` (section) | **Correct** — vendor's own name (`W:\codepage.bin`). Description: "codepage conversion tables (61 encodings + Unicode (de)composition)". |
| `codepage_load` | OK but it's a cached **sector read with backup recovery**; `codepage_read_cached` would be clearer. |
| `codepage_get_header` | **Accurate** (GetCPInfo analog). |
| `codepage_get_widechar` | **Misleading** — returns the 1/2 bytes-per-char flag. Rename → `codepage_bytes_per_char` / `codepage_is_dbcs`. |
| `map_gb0_to_codepage_sel` | Mechanism right, "gb0" opaque. It maps the **language config byte** → codepage index. Rename → `map_lang_to_codepage_sel`. |
| `boot_read_serial_and_codepage` | **Half wrong** — the "serial" is (very likely) the 4-entry NAND block map + geometry shifts for the codepage cache, not a serial number. Rename → `codepage_fs_init` / `nls_init`. |

## 6. The language selector G0 — the pen always runs GBK, and GBK is ASCII-identity *(Proven)*

Codepage selection happens exactly once, at boot, from a single language byte:

```
app_init_main @0x08038f5c  (after fs_storage_mount_init()==0):
   sel = map_lang_to_codepage_sel(*(u8*)0x080089a4)          // G0 = game_ctx[0]
   boot_read_serial_and_codepage(sel) @0x0803a3b0            // caches the descriptor
```

**G0 = the byte at `0x080089a4` (game_ctx[0])**, an Anyka-SDK locale code 0..0x13. Two
facts settle what encoding the pen actually uses:

1. **No G0 value maps to the ASCII/cp437 record.** `map_lang_to_codepage_sel@0x08031824`
   (disassembly-verified) sends `0/1/2` and any value `≥0x14` to **sel 0x17 = cp936 GBK**;
   the other values pick Big5/SJIS/cp949/ISO-8859-x/85x/866/1255/874 (all ASCII-transparent
   in 0x00–0x7F). cp437 (sel 2, the "sector 52" table) is unreachable by the selector.
2. **G0 is never written.** An exhaustive scan of all 690 `ldr rX,[pc,#imm]` sites loading
   `0x080089a4` in `PROG.bin` plus every nandboot literal site finds **zero** stores to
   `[rX,#0]` — the only offset-0-ish writes resolve to `*(game_ctx+0x20)[…]` (akoid_buf
   fields), not `game_ctx[0]`. `0x080089a4` is in nandboot RAM, zero at reset. So **G0 = 0
   on every boot, on real hardware too** — there is no settings file, FLAG.bin field, GME
   language, or menu that writes it. It is vestigial Anyka SDK config; the pen permanently
   runs the **GBK** record.

**GBK converts ASCII device paths correctly** *(Proven from `codepage.bin` bytes)*. GBK
record (sel 0x17): `w=2, A=0x362CC, B=0x460CC, C=0x461CC`. For every ASCII byte `c`,
`B[c] = 0` (not a lead byte; checked for the whole path alphabet incl. `a A : /`), so the
DBCS core falls to the single-byte lookup `A[c] = c` (`0x61→0x0061`, `0x41→0x0041`,
`0x3A→0x003A`, `0x2F→0x002F`). Identity. So a device path like `"a:/oidfilelist.lst"`
converts to the identical UTF-16 string under the authentic record. (There is also a
ready-flag fallback: if the NLS state's ready byte at `0x081db730` is 0 — e.g. because
`boot_read_serial_and_codepage` failed — the converter does a plain byte→u16
zero-extension, also correct for ASCII.)

## 7. Open ends (minor)

- Exact semantics of table D's flag bits (0x1100 letters / 0x0700 space) —
  ctype-style classification *(Inferred)*.
- The 8 bytes copied in `boot_read_serial_and_codepage` are the **FHA maplist** for the
  "codepage" bin (per-cluster `{origin_phys, backup_phys}` u16 pairs, 7 clusters =
  blocks 36–42); see [`fatvol-medium-layering.md`](fatvol-medium-layering.md) §3.3.
- Why sel 0x2B "ASCII" maps 0x27/0x60 to curly quotes (vendor quirk).
