# Book/product discovery and load — udisk scan → `a:/oidfilelist.lst` → product mount

> How the 2N (MT) pen firmware finds the products/books on the udisk and loads one when its
> product-OID is tapped. Full static trace: the `.lst` library, the discovery scan, the three
> index files, `gme_mount_check_product`, and the read-back dependency of the mount path.
> All addresses = unified runtime base **0x08009000** (`data/PROG.bin` flat load).
> Evidence tags: **[Proven]** = read from decomp/disasm or the binary (cited), **[Inferred]** = inferred.
> Companions: `statechart-full-map.md` (the state fabric), `autonomous-mount-state8.md`
> (why the mount doesn't gate state 8).

---

## 0. Executive summary

- **Discovery** runs at **standby entry** (state-3 entry hook, once per boot on the
  splash→standby transition): `udisk_gme_discovery` recursively enumerates **`B:` then `A:`**
  for files matching **`*.gme`** and writes one **0x214-byte record per file** into
  **`a:/oidfilelist.lst`** (header 0x424 bytes + records). The file is a **rebuilt-each-scan
  cache**, not persistent config: the scan always rewinds to offset 0 and rewrites everything,
  and the in-RAM count comes from the fresh scan, never from the old file.
- The `.lst` record is **just a path + three enumeration counters**. There is **no product-id,
  no OID range, no type field** in the record.
- **Product resolution is a linear probe at tap time**: a product-OID (**≤ 0x3E7 = 999**) makes
  `gme_oid_dispatch` call `gme_mount_check_product(1,0)`, which walks the `.lst` records,
  **opens every `.gme`**, and accepts the first whose header has **magic 0x238B @+0x08**,
  **matching language string @+0x59**, and **product-id @+0x14 == the tapped OID value**.
  There is no precomputed OID→file map anywhere.
- On success the open handle stays in **`p_filehandle_current_gme` @0x08121ed0**,
  `gme_parse_header` loads the script/media tables, sets **current-product @0x081da08c**
  (= GME hdr@0x14) and seeds the cover-OID selectors (@0x081da714/716/718…) from the
  hdr@0x94 block — closing the statechart-map open item on who seeds the selector.
- **Read-back dependency — verdict: REQUIRED.** The mount path consults *only* the iterator
  built by this boot's scan; skipping the scan (or an unwritable A:) leaves the count at 0 and
  every mount fails (voice 0x2D). A pre-existing `.lst` on disk does **not** suffice — the
  `.lst` write + read-back is genuinely on the critical path of tap-to-play.
- **Sequencing subtlety:** the *first* tap of a product OID (fresh standby) is consumed by the
  state-9 classifier's first-load branch (posts 0x104A+0x1058 → book(13)); the actual GME
  mount happens **inside book(13)** when `gme_oid_dispatch` processes a product-OID 0x1060 —
  i.e. on the **second** tap (or a game-side product-switch replay). So the sequence is: the
  first tap of a product OID takes the pen into book(13); tapping the same OID **again** there is
  what triggers the real `gme_mount_check_product` mount.

---

## 1. The `.lst` library ("Aud_list", `log_aud_musiclist.c`)

A small reusable module manages all three index files. Its context object (alloc size
`DAT_080afcc8` = **0x634** [P @0x080af924 + pool 0x080afcc8]):

| offset | field |
|---|---|
| +0x000 | u32 **magic = 0x12345678** (`DAT_080afcd0`) when valid; 0 = invalidated |
| +0x004 | u32 **record count** (set from the fresh scan counter) |
| +0x008 | wchar16 **scan-root path**[0x104] (e.g. `B:`) |
| +0x210 | u32 cursor: current record field₀ |
| +0x214 | u32 cursor: current record field₁ |
| +0x218 | u32 cursor: current record field₂ |
| +0x21c | wchar16 **current-record path buffer**[0x104] |
| +0x424 | wchar16 **filter pattern**[0x104] (e.g. `*.gme`) |
| +0x62c | int **file handle** of the open `a:/*.lst` |
| +0x630 | u8 flag (passed by `FUN_080ae23c` arg 1; 0 on the gme path) |

### 1.1 On-disk format of `a:/oidfilelist.lst` (and siblings) [Proven]

```
offset 0x000: header = the first 0x424 bytes of the ctx
              (magic, count, scan-root, cursor triple, last path buffer)
offset 0x424 + i*0x214: record i (0x214 bytes each):
    +0x00  u32  running record index at write time (global counter *0x081db9c8)
    +0x04  u32  1-based index of the file within its directory's enumeration
    +0x08  u32  first-entry index of the directory (opendir handle word 0)
    +0x0c  wchar16 path[0x104]  absolute path incl. drive, e.g. L"B:/taschenrechner.gme"
```

Header size constant `DAT_080afccc` = **0x424** [P pool]; record stride **0x214** [P
`FUN_080afb78` seek `0x424 + idx*0x214`, `udisk_dir_scan_recurse` write of `0x214`].
**The record carries no product-id, OID range, media offsets, or type** — only the path is
semantically used by consumers; the three u32s are enumeration bookkeeping. [Proven]

### 1.2 The helper functions [Proven]

| addr | proposed name | what it does |
|---|---|---|
| 0x080af924 | `lst_ctx_alloc` | malloc(0x634) + zero |
| 0x080af9b0 | `lst_file_open_or_create` | `fs_open(path, 2, 2)`; if exists → **reads the 0x424 header into the ctx** (magic/count/root persist across boots); else create (`fs_open(path,1,1)`) + zero header. Handle → ctx+0x62c. Return value ignored by the caller! |
| 0x080af960 | `discovery_ctx_set_root` | copy root path → ctx+8 (already named) |
| 0x080afa80 | `discovery_scan_wrapper` (≈ `lst_rebuild_scan_B_then_A`) | zero the record counter, seek 0, write header, `udisk_dir_scan_recurse` from ctx+8 root (**`B:`**), then **overwrite the drive letter with `'A'` (0x41) and scan again**, ctx+4 := fresh counter `*0x081db9c8`, zero cursor, magic := 0x12345678, rewrite header, `FUN_080f8a18` = **fs_flush (not close — the handle stays open)** |
| 0x080af6dc | `udisk_dir_scan_recurse` | see §2.2 (already named) |
| 0x080ad4e4 | **`fs_file_write`** (generic FSLib `File_WriteBlock`, *not* lst-specific) | generic buffered file write |
| 0x080afb78 | `lst_record_read` | seek `0x424 + idx*0x214`, read 0x214, unpack fields → ctx+0x210/0x214/0x218, copy path (0x104 wchars) to dest |
| 0x080ae430 | `lst_cursor_fetch` | fetch record by mode: −1 prev, 0 current, 1 next, 2 random (`rng_below`), 3 absolute index; calls `lst_record_read` into holder→pathbuf |
| 0x080ae23c | `lst_iter_holder_new` | malloc(0x10) holder `{ctx, ctx+0x21c, f₁, f₂}`; `lst_ctx_alloc`; copy **filter pattern** (arg 2) → ctx+0x424; flag → ctx+0x630 |
| 0x080ae2c0 | `discovery_ctx_build` (named) | list-id → `.lst` path (`1→a:/oidfilelist.lst`, `3→a:/voicelist.lst`, else `a:/musiclist.lst`, strings @0x080ae394/3a8/3bc), `lst_file_open_or_create`, root := `*(0x08122708 + id*4)`, `discovery_scan_wrapper`, export count/cursor to the holder |
| 0x080afc0c | `lst_invalidate_header` | magic := 0, rewrite header |
| 0x080afa50 | `lst_header_flush` | seek 0, rewrite header |
| 0x080afa30 | `lst_file_close` | `file_close(ctx+0x62c)` |
| 0x080af950 | `lst_ctx_free` | free |
| 0x080ae6c4 | `lst_invalidate_by_id` | open by list-id, invalidate, flush, close, free |
| 0x080ae65c | `aud_list_set_need_update` (named) | asserted wrapper around `lst_invalidate_header` |
| 0x080f0618 | `booklist_get_path_at_cursor` | `lst_cursor_fetch(mode 3, iter[2]+iter[3])`, returns pointer to ctx+0x21c (the record's path) |
| 0x080f0580 | `booklist_cursor_advance` | cmd byte: 0 = re-fetch current (returns path), 2 = move −1, 3 = move +1 (via `FUN_080f0408`), 4 → 0xFFFE; only acts when arg₂ == 0x105F |
| 0x080f0048 | `udisk_scan_iter_init` (named) | build the holder+ctx (filter = `0x0811c504 + list_id*0xC8`), run `discovery_ctx_build`, iter[0] := ctx+4 count, zero cursor |
| 0x080f013c | `udisk_gme_discovery` (named) | the entry point: `udisk_scan_iter_init(iter, 0x140000, 0x8c0080, 2, 0x10, 0x14, list_id)` |
| 0x080f07d0 | `voicelist_delete_and_invalidate` | deletes a file + `lst_invalidate_by_id(3,…)` twice — **no static caller** (dead) |

Static tables [P read from PROG.bin]:

- **scan-root table** @0x08122708: `[0] = "B:"` (music), `[1] = "B:"` (oid/gme),
  `[2]/[3] = 0xFFFFFFFF` (unused). Adjacent init strings @0x080ed9c6: `"A:/musiclist.lst"`,
  @0x080ed9d7 `"A:/oidfilelist.lst"` (referenced from 0x08122700/04 — an uppercase parallel
  path table, apparently unused by this flow).
- **filter-pattern table** @0x0811c504, stride 0xC8: `[0] = L"*.mp3/*.wma"`,
  `[1] = L"*.gme"`, `[2]/[3]` = garbage/unrelated rodata (never used as patterns).

---

## 2. The discovery scan → `.lst` write

### 2.1 When it runs [Proven]

Exactly two static call sites of `udisk_gme_discovery`, **both with list-id 1** (=
`a:/oidfilelist.lst`, filter `*.gme`):

1. **`standby_entry` 0x080511a0** (state-3 entry): after opening the two serial/log files, it
   runs `udisk_gme_discovery(*(0x081da07c+4), 1)` **unless `game_ctx[0x1e] == 2`** (the
   USB-session marker). The iterator object itself (0x240 bytes) is `malloc`'d + zeroed at
   every standby entry and its pointer stored at **`*0x081da080`** — the "discovery head
   object". Standby is entered once per boot (splash →0x1014→ standby is a sibling
   transition; pops back from book/USB do *not* re-run the entry hook per the QHsm entry
   semantics), so **the scan is a per-boot event**, plus:
2. **`standby_handler` 0x08051b0c**: in idle mode, if GPIO 0xB reads 1 (disc-change line) →
   re-run `udisk_gme_discovery(...,1)` and `soft_reboot()`.

No other list-id is ever built in this firmware: `udisk_scan_iter_init` has one caller
(`udisk_gme_discovery`), which has only the two callers above (grep of the full
`decomp_named` corpus). [Proven]

### 2.2 What the scan does [P `0x080af6dc.c`, `0x080afa80.c`]

`discovery_scan_wrapper` scans the root **`B:`** recursively, then rewrites the path's first
character to `'A'` and scans **`A:`** recursively — both passes append to the same
`oidfilelist.lst`. For each directory, `udisk_dir_scan_recurse`:

- `opendir` (`FUN_080fb8ac`) with an open-struct `{1, attr=3, 0, pattern, namebuf, path,
  pattern, filehandle}` — the **`*.gme` wildcard is applied by the FAT enumeration itself**;
  directories are still returned so recursion works. [P struct; I that FAT filters files by
  the pattern — consistent with the per-list patterns and consumers never re-filtering]
- for each **file** dirent (attr bit 0x10 clear): append the name to the path, build the
  record `{*0x081db9c8, index-in-dir, dir-first-entry}` + the full path (0x104 wchars via
  `FUN_08030824`), `fs_file_write(handle, rec, 0x214)`; on a short write log **"write music
  list failed"** and abort. Increment the global record counter `*0x081db9c8`.
- for each **subdirectory**, skipping `L"./"` and `L"../"` (@0x080edbe8/0x080edbf0 [Proven]):
  recurse. Recursion depth tracked at `*0x081db9c4`.
- **`handle == -1` probe mode**: if called with file-handle −1 the function stops after the
  first matching file and writes nothing (an "any file exists?" probe — and the silent
  behavior if the `.lst` could not be opened, since `discovery_ctx_build` ignores
  `lst_file_open_or_create`'s failure). [Proven]

After both passes the wrapper stores the fresh count into ctx+4, resets the cursor, rewrites
the header (magic 0x12345678) and **flushes** — the `.lst` handle at ctx+0x62c **remains
open** for all subsequent record reads. [P `FUN_080f8a18` = flush, not close]

### 2.3 Cache or persistent? — **Rebuilt each scan** [Proven]

`lst_file_open_or_create` does read an existing file's 0x424 header (the format is *designed*
to be reusable), but `discovery_ctx_build` **unconditionally** calls the scan wrapper, which
rewinds to 0 and rewrites header + all records, and sets the count from the fresh in-RAM
counter. Nothing on the `.gme` path ever consumes stale file content. (The
`lst_invalidate_header`/"need update" machinery is only reachable from the dead
voicelist/musiclist helpers.) The file persists on A: between boots merely as a side effect.

---

## 3. The three index files

| file | list-id | filter | scan roots | consumer | status in 3202MT |
|---|---|---|---|---|---|
| `a:/oidfilelist.lst` | 1 | `*.gme` | `B:` then `A:` | the **booklist iterator** `*0x081da080` → `gme_mount_check_product`, `gme_parse_header` | **the only list ever built** [Proven] |
| `a:/musiclist.lst` | 0 (default) | `*.mp3/*.wma` | `B:` then `A:` | the Aud_musiclist MP3-player module (`log_aud_musiclist.c`) | machinery present (strings, patterns, `aud_list_set_need_update`), **no static caller builds or reads it** — vestigial/fptr-only in this build [P corpus grep] |
| `a:/voicelist.lst` | 3 | (table entry −1/garbage) | (−1) | — | only touched by the **dead** `voicelist_delete_and_invalidate` (0x080f07d0, no callers). Vestigial. [Proven] |

So "oidfilelist" is one instance of a generic music-list library (hence the "write music list
failed" string on the gme path). The name notwithstanding, **`oidfilelist.lst` maps nothing
OID-specific** — it is simply "every `.gme` on both drives, by path".

---

## 4. Product tap → lookup → mount → book

### 4.1 The runtime objects

- **booklist iterator** @`*0x081da080` (alloc'd 0x240 in `standby_entry`): `+0` u16 total
  `.gme` count, `+4/+6` u16 cursor pair, `+0xC` holder ptr (→ lst ctx), `+0x16..` scan window
  params, `+0x30` wchar16 current-path buffer (0x103 wchars, terminator @+0x236), `+0x238`
  `.bnl`-found flag, `+0x23C` aux handle. [P `udisk_scan_iter_init`, `gme_mount_check_product`,
  `booklist_scan_bnl`]
- **`p_filehandle_current_gme`** @0x08121ed0 (static init −1 [Proven]): the open handle of the
  mounted product GME.
- **current product id** @**0x081da08c** (static 0): written by `gme_parse_header` from GME
  hdr@0x14; read by `gme_oid_dispatch` (`DAT_08036738`) and by the state-9 classifier as its
  per-slot "mode selector" (`DAT_080381a4`) — **the same global**, resolving the
  statechart-map open item. [P pools 0x08036738/0x080381a4/0x08035328 all = 0x081da08c]
- **akoid flag byte** @0x081da086: bit7 = "GME product mounted" (set by
  `gme_mount_check_product`), bit6 set at state-12 entry. [Proven]

### 4.2 Step-by-step trace (fresh boot, downloaded GME, product-OID 42)

1. **Boot → standby(3)**: `standby_entry` runs the discovery scan (§2) — `oidfilelist.lst`
   now lists every `.gme`; iterator count > 0. [Proven]
2. **First tap of OID 42** → nandboot decode → event **0x1060** → state-9
   `cover_oid_classifier` (0x08037cec): stores the OID value at **`game_subctx+4`**
   (`&0xFFFF`), bumps counters; the built-in cover-family checks miss (42 is no builtin
   cover); the **first-load branch** (`game_ctx[0x1d]==2` ∧ gate byte `subctx[0x21]==0xFF` ∧
   `*0x08008C0D != 0xFF`) fires: saves the OID also at `subctx+0x74`, sets `subctx+0x58=1`,
   posts **0x104A + 0x1058** (pools 0x080381bc/c0 [Proven]) and consumes the event. [Proven]
3. **0x1058 → book_mount(12)**: entry `booklist_scan_bnl` (0x08034300) resets the product
   context (`p_filehandle_current_gme := −1`, fresh-tap flag cleared), then scans **`B:/`**
   for **`*.bnl`** (retail-book format, findfirst `FUN_080ad7c0`) into iterator+0x30 —
   *independent of the `.gme` flow*; empty scan just sets error flags. Default action posts
   **0x1059 unconditionally** → **book(13)**. [Proven]
4. **book(13) entry** (0x080345cc): constructs the audio player, zeroes the whole GME script
   context **including the cover selectors** @0x081da714/716/718, sets
   `game_ctx[0x1d]=2`, clears the first-load gate `subctx[0x21]=0`, plays voice 0x13
   ("book open") on first-ever entry. **No mount happens here.** [Proven]
5. **Second tap of OID 42** → 0x1060 → classifier now takes the re-arm path (gate byte 0)
   and **passes the event through (returns 1)** → book's default EA
   **`gme_oid_dispatch`** (0x0803629c):
   - OID value ≤ **0x3E7 (999)** ⇒ the **product band** (0x300–0x3E7 = the *switch*-product
     sub-band checked against built-ins; everything ≤0x2FF falls through to the same path).
     [P range logic L1090–1094]
   - tapped ≠ current product (`*0x081da08c`, 0 on fresh boot) ⇒ stop audio, reset script
     state, call **`gme_mount_check_product(1, 0)`**. [P L1105–1117]
6. **`gme_mount_check_product` (0x08034130)** — the actual lookup. Expected product id
   `iVar8 = *(game_subctx+4)` = **the tapped OID value**. Loop over the booklist iterator
   (bound = iter u16 count):
   - `booklist_get_path_at_cursor` → **`.lst` record read (fs_seek/fs_read on the open
     handle)** → path → copy to iter+0x30;
   - `fs_open(path)`; **hdr@+0x08 == 0x238B** (magic); **`gme_check_language()`**
     (0x08033fb0: hdr bytes 0x59..0x5F vs the pen-language variable @0x08121ec0, default
     "GERMAN", all-zero field ⇒ GERMAN); with mode-arg 1: **hdr@+0x14 == expected
     product id**;
   - match ⇒ set bit7 of 0x081da086, **leave the handle in `p_filehandle_current_gme`**,
     return 1. No match ⇒ close, `booklist_cursor_advance(+1)`, next record; exhausted ⇒
     return 0. Mode-arg semantics: 0 = accept first valid GME (no product check),
     1 = require product-id match, 2 = open nothing (fail-through). Arg₂ selects the
     advance direction (0 ⇒ forward). [P full read]
7. **Mount failure** (no matching `.gme`): dispatch clears the mounted flag, zeroes current
   product, plays **voice 0x2D** ("book not found"). Tap of a product band OID with nothing
   mounted at all → **voice 0x2B**. [Proven]
8. **Mount success**: dispatch sets `subctx+0x4A8=1` (mounted), then
   - **`gme_parse_header` (0x08035d20)**: re-fetches the current record path (again via the
     `.lst`!), reopens if needed, reads GME hdr words 0..7 → play-script table
     (@0x081da094), media table (@0x081da098), additional-script, …, **hdr@0x14 → current
     product @0x081da08c**, language-voice index; hdr@0x94 → a 20×u16 block → the
     **cover-OID selector globals** (first word → 0x081da716, third → 0x081da718, 19th →
     0x081da714, …) — the classifier's dynamic cover selectors for *this* product. [Proven]
   - `gme_reset_registers`, **`gme_parse_start_end_oid` (0x08035c3c)**: reads the play-script
     table's first two u32s → **last OID → subctx+0x1A, first OID → subctx+0x18** (the GME
     content-OID range). [Proven]
   - hdr@0x71 (u32) → **`FUN_08077230`** = the **power-on-sound playlist parser+player**
     (the dedicated power-on playlist played at mount; state stored at subctx+0xDD8). See
     `gme-format-completeness.md` §3.2. [Proven]
9. **Subsequent content taps** (OID ≥ 0x3E8): dispatch checks `first ≤ OID ≤ last`
   (subctx+0x18/0x1A); in-range ⇒ `gme_oid_to_playscript(OID)` → conditions → media plays /
   game-launch events (see statechart map §2). Out-of-range ⇒ voice 0x2B. [Proven]

**"What does the tap consult to resolve OID 42 → taschenrechner.gme?"** — Nothing
precomputed: the `.lst` gives only the candidate *paths*; the mapping is established by
**opening each candidate and comparing GME hdr@0x14 with the tapped value**. First match wins
(iterator order = FAT enumeration order, B: before A:).

### 4.3 The other mount triggers

- **Game-side product switch**: game leaves set `subctx[0x58] = 100`; dispatch's preamble
  (`ctx[0x58]==100` ⇒ restore OID from `subctx+0x74`, fake a 0x1060) replays the saved
  product OID through the same path. [Proven]
- **Built-in products** (Chomp, the 0xC21/0xA94/… cover families): handled entirely by the
  classifier + game states; they never call `gme_mount_check_product`. [Proven]
- `FUN_08037b10` (a second header-parser reading hdr@0x1C→subctx+0x18/0x1A, @0xBC, @0xB8,
  @0x30 off the current `.lst` record) has **no static caller** — vtable/fptr or dead. [Proven]

---

## 5. Q4 — does product-load REQUIRE the `.lst` write + read-back this boot? **YES** [Proven]

1. **Only consumer**: `gme_mount_check_product` is the sole GME mount site (single static
   caller: `gme_oid_dispatch`), and it iterates **only** the booklist iterator @`*0x081da080`.
2. **Iterator count comes from the fresh scan**: `udisk_scan_iter_init` copies iter[0] from
   ctx+4, which `discovery_scan_wrapper` sets from the *in-RAM* record counter
   (`*0x081db9c8`) incremented per record **written this scan** — the header count read from
   a pre-existing file is overwritten before any consumer sees it.
3. **No scan ⇒ count 0 ⇒ mount fails**: the iterator object is malloc'd+zeroed at standby
   entry; if `udisk_gme_discovery` is skipped (`game_ctx[0x1e]==2`) or the scan writes fail
   (unopenable `a:/oidfilelist.lst` puts the recursion into the −1-handle probe mode, which
   writes nothing and never increments the counter), `gme_mount_check_product`'s loop bound
   is 0 → returns 0 → voice 0x2D. A stale `.lst` on disk is never consulted for the count.
4. **Every record fetch is a real file read**: `lst_record_read` = `fs_seek(0x424+i*0x214)` +
   `fs_read(0x214)` on the handle kept open since the scan (flushed, not closed). Both
   `gme_mount_check_product` (candidate paths) *and* the post-mount `gme_parse_header`
   (current path re-fetch) go through it.

So the discovery tail depends, in order, on: create/overwrite+write of `a:/oidfilelist.lst`
on the A: FAT, recursive FAT enumeration of B: (and A:) with wildcard matching, and later
seek+read of the same file through the same open handle. Any break in that chain silently
degrades to "no products" (0x2D), never a crash — there is no firmware path that mounts a
`.gme` without it.

### 5.1 The path field and the failure-swallow [Proven]

Two firmware details of the extraction path (`lst_record_read@0x080afb78` → the record's
wide path at rec+0xC):

- The path is copied verbatim by **`FUN_08030824@0x08030824`**, a plain `wcsncpy`-style bounded
  wchar16 copy (copy until NUL or 0x104 wchars) — **no codepage/ANSI transform** is applied to
  the stored path.
- **`fs_seek@0x0800ded8` returns the new absolute position** (so `0` means position-0 or a
  zero-length file, which the reader treats as a seek failure). `lst_record_read` zero-inits its
  cursor scratch and, on `fs_seek(...) == 0` or `fs_read(...) == 0`, **returns before copying the
  path** — leaving `ctx+0x21c` at whatever it held (zero on a fresh ctx). `booklist_get_path_at_cursor`
  returns that buffer regardless of the read result, so a failed record read silently yields an
  empty/stale path rather than an error — the same "degrade to no products, never crash" behaviour.

---

## 6. Proposed names / docstrings (delta to the current naming)

| addr | current | proposed | docstring |
|---|---|---|---|
| 0x080ad4e4 | `FUN_080ad4e4` | **`fs_file_write`** | Generic FSLib `File_WriteBlock` (asserts string @0x080fc0b4). The `.lst` write is just one user. |
| 0x080af924 | `FUN_080af924` | `lst_ctx_alloc` | alloc+zero the 0x634 list ctx (`DAT_080afcc8`). |
| 0x080af9b0 | `FUN_080af9b0` | `lst_file_open_or_create` | open `a:/*.lst` r/w; existing → read the 0x424 header into ctx; missing → create + zero header. Handle → ctx+0x62c. Caller ignores failure (→ −1-handle probe scan). |
| 0x080afa80 | `discovery_scan_wrapper` | keep; docstring: | zero counter, write header, recursive scan of ctx+8 root (`B:`), patch drive to `'A'`, scan again; ctx+4 := fresh count; rewrite header; flush (handle stays open). |
| 0x080af6dc | `udisk_dir_scan_recurse` | keep; docstring add: | pattern filter applied by the FAT opendir params; skips `./`,`../`; handle −1 = existence probe (stop at first match, write nothing); record = `{u32 seq, u32 idx-in-dir, u32 dir-first} + wchar16 path[0x104]` = 0x214. |
| 0x080afb78 | `FUN_080afb78` | `lst_record_read` | seek `0x424+i*0x214`, read record, unpack cursor triple → ctx+0x210.., path → dest. |
| 0x080ae430 | `FUN_080ae430` | `lst_cursor_fetch` | fetch by mode −1/0/1/2(random)/3(absolute). |
| 0x080ae23c | `FUN_080ae23c` | `lst_iter_holder_new` | alloc holder {ctx, pathbuf, f₁, f₂}; set filter pattern @ctx+0x424. |
| 0x080afc0c | `FUN_080afc0c` | `lst_invalidate_header` | magic := 0, rewrite header ("need update"). |
| 0x080afa50/0x080afa30/0x080af950 | — | `lst_header_flush` / `lst_file_close` / `lst_ctx_free` | — |
| 0x080ae6c4 | `FUN_080ae6c4` | `lst_invalidate_by_id` | id→path, open, invalidate, flush, close, free. |
| 0x080f0618 | `FUN_080f0618` | `booklist_get_path_at_cursor` | fetch record iter[2]+iter[3]; return ptr to path (ctx+0x21c). |
| 0x080f0580 | `FUN_080f0580` | `booklist_cursor_advance` | cmd 0=refetch, 2=−1, 3=+1, 4→0xFFFE; gated on arg₂==0x105F. |
| 0x080f0408 | `FUN_080f0408` | `booklist_cursor_move` | ±1 with window wrap. |
| 0x080f8a18 | `FUN_080f8a18` | `fs_file_flush` | flush dirty buffers + dir entry; **not** close. |
| 0x080ad514 / 0x080ad564 | — | `fs_file_close_checked` / `fs_file_delete` | — |
| 0x08034130 | `gme_mount_check_product` | keep; docstring: | walk the booklist; open each `.gme`; magic 0x238B@8 + language@0x59 + (mode 1) product-id@0x14 == `game_subctx+4`; match ⇒ handle stays in `p_filehandle_current_gme` @0x08121ed0, bit7 of 0x081da086, ret 1. Modes: 0 any-valid, 1 product-match, 2 skip-open. Sole caller: `gme_oid_dispatch` product band ≤0x3E7. |
| 0x08033fb0 | `gme_check_language` | keep; docstring: | GME hdr 0x59..0x5F language field vs pen language @0x08121ec0 ("GERMAN" default; zero field ⇒ GERMAN); numeric forms via `Utl_UAtoi`. |
| 0x08035d20 | `gme_parse_header` | keep; docstring add: | also **sets current-product @0x081da08c from hdr@0x14** and **seeds the cover-OID selectors 0x081da716/718/714… from the hdr@0x94 u16 block** — the classifier-selector seeding (closes statechart-map §5 open item). Re-fetches the path from the `.lst` cursor. |
| 0x08035c3c | `gme_parse_start_end_oid` | keep; docstring: | play-script table first two u32 → last OID (subctx+0x1A), first OID (subctx+0x18); script offsets base := table+8. |
| 0x08077230 | `FUN_08077230` | `gme_power_on_sound_playlist` | power-on-sound playlist parser+player, driven by GME hdr@0x71; state stored at subctx+0xDD8 (matches GME-Format's power-on playlist; see gme-format-completeness §3.2). [Proven] |
| 0x08037b10 | `FUN_08037b10` | `gme_parse_header_alt` | header parse variant off the `.lst` cursor (hdr@0x1C→OID range, @0xBC, @0xB8 volume?, @0x30); **no static caller**. |
| 0x080f07d0 | `FUN_080f07d0` | `voicelist_delete_and_invalidate` | dead. |
| globals | — | `g_p_booklist_iter` @0x081da080; `g_current_product_id` @0x081da08c; `g_akoid_status_flags` @0x081da086; `g_scan_depth` @0x081db9c4; `g_scan_record_count` @0x081db9c8; `g_pen_language` @0x08121ec0 | — |

---

## 7. Evidence index

| claim | status | evidence |
|---|---|---|
| record = 3×u32 + wchar16[0x104] path; no product/OID fields | **P** | `0x080af6dc.c` (write: `{*0x081db9c8, idx, dirfirst}` + `FUN_08030824(...,0x104)`), `0x080afb78.c` (read) |
| header 0x424, stride 0x214, magic 0x12345678 | **P** | pools 0x080afccc/0x080afcd0; `0x080afb78.c` seek |
| scan roots `B:` then `A:`; filter `*.gme` (list 1) / `*.mp3/*.wma` (list 0) | **P** | `0x080afa80.c` (`*puVar3=0x41`), tables @0x08122708 & 0x0811c504 read from PROG.bin |
| discovery runs at standby entry (per boot) + GPIO-0xB rescan; always list-id 1 | **P** | `0x080511a0.c` L376, `0x08051b0c.c` L108; corpus grep for callers |
| `.lst` rebuilt each scan; count from fresh counter; handle kept open (flush) | **P** | `0x080afa80.c`, `0x080f8a18.c` |
| product band = OID ≤ 0x3E7; mount on tapped ≠ current product | **P** | `0x0803629c.c` L1090–1117 |
| mount = linear probe: magic 0x238B@8, language@0x59, product-id@0x14 | **P** | `0x08034130.c`, `0x08033fb0.c` |
| handle → 0x08121ed0; mounted bit7 @0x081da086 | **P** | `0x08034130.c` + pool values |
| `gme_parse_header` sets 0x081da08c (=hdr@0x14) and the cover selectors (hdr@0x94 block) | **P** | `0x08035d20.c`; pools 0x08035328/0x080361d8/e0 + 0x08036220 = 0x081da08c/716/718/714 |
| first tap consumed by classifier (posts 0x104A+0x1058); mount needs a 2nd 0x1060 in book; no auto-replay of the first tap | **P** (`subctx[0x58]=1` is confirmed inert — no reader anywhere; dispatch's sole `[0x58]` reader is the `==100` replay, and 100 is written only by the game-state files, so the first-load latch never triggers a replay — see oid-classifier-logic.md §2/§5, cover-tap-first-product-load.md §4/§7) | `0x08037cec.c` L259–267, `0x0803629c.c` L48–55, corpus grep `0x58) = 100` |
| read-back REQUIRED (no scan ⇒ count 0 ⇒ 0x2D) | **P** | §5 chain: `0x080f0048.c`, `0x080afa80.c`, `0x08034130.c`, `0x080af6dc.c` (−1 mode) |
| musiclist/voicelist not built in this firmware | **P** (static reachability) | corpus greps; `0x080f07d0.c` no callers |
| fresh-tap flag `*game_subctx` set by the OID-decode pipeline before dispatch | **I** | consistent with classifier/dispatch gating; setter not traced |
| FAT opendir applies the wildcard to files but returns dirs | **I** | open-struct carries the pattern; consumers never re-filter |
