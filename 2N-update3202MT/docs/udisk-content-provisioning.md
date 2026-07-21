# Who provisions `A:/VOIMG/Chomp_Voice.bin` (and the `to_udisk` files) — producer vs firmware

Research question: *how does the `A:` system-partition content — specifically
`A:/VOIMG/Chomp_Voice.bin` and the other `.upd` `to_udisk` files — get written to the pen's
NAND? Is that the producer.bin factory-init's job, the firmware update path's job, or both?*

**Answer in one line:** **Both mechanisms exist, but only the firmware's own update path is
observed writing the FILES.** producer.bin does the low-level job (format + partition/zone +
bin burn + FAT mkfs) and *offers* a host-streamed FAT-image channel (`PR_I`) the PC tool
*could* use to lay down the `to_udisk` files at the factory; the pen's **firmware** contains
the self-contained `to_udisk` unpacker (`upd_unpack_udisk_files` 0x080f2760) that writes each
file onto a freshly-reformatted `A:` from the `.upd`, and that write is **byte-identical** to
the extracted file. So for our purposes: **the firmware update path is the authoritative,
reproducible provisioner; hand-placing the extracted file == what the firmware would write.**

Conventions: MT PROG base 0x08009000; `.upd` = `update3202MT.upd` (0xAC796C bytes).
Producer base 0x08000000. Claims tagged **Proven** (decomp/bytes cited) or **Inferred**.
Builds on [`upd-system-partition-layout.md`](upd-system-partition-layout.md) (the container
map), [`nand-init-from-producer.md`](nand-init-from-producer.md) (producer format flow), and
[`system-voice-feedback.md`](system-voice-feedback.md).

---

## 1. Q1 — the firmware update path, traced end-to-end (Proven)

### 1.1 Trigger — when does the unpack run?

**On (almost) every boot, gated by a newer `.upd` on B: AND running on battery.** The
statechart root state decides this (`statechart-full-map.md` §2):

```
state 0 root → EA[1] root_action_boot_decide  (FUN_0804e570 @0x0804e570)
   iVar = fw_update_cleanup()                  // scan B: for a newer .upd (§1.2)
   if (update pending) && (GPIO8 == 0)         // GPIO8==0 = on battery / NOT plugged to PC
        → post 0x1002 → enter state 2 (fw_update)
   else → post 0x1001 → splash → standby (normal boot)
```

- **`fw_update_cleanup` @0x08052768** (a.k.a. `fw_update_scan_b_for_upd`) — the detect/gate
  step (**Proven**):
  1. `battery_meas_enable(1)`, `lang_get_num()` parses `B:/LanguageInfoMT.txt`.
  2. Loops the language table; for each, builds `B:\<update-filename>` and `fs_open`s it.
     Seeks to `size − 0x38`, reads the trailer, requires the four bytes `R A V _`
     (`0x52 0x41 0x56 0x5f`), then compares the trailer's version digits against the
     **running firmware version at 0x08009000** (`DAT_08052b04 → "N0038MT\0\0\0201310…"`;
     the compared bytes are the digits after `N`). File **newer** → `"Can update."` sets
     `pending=1`, remembers the index; **older/equal** → `"Delete Update File."` deletes it.
     Verified: `DAT_08052b00 → "Update3202MT.upd"` (the default filename), `DAT_08052b04 →
     0x08009000` (running version). **Proven** (pointers resolved from `PROG.bin`).
  3. If pending: **battery gate** — reads the ADC up to 5× and requires `>= 0x300`; below
     that it plays `A:/Language/BatLowUpdate<LANG>.wav` (`lang_play_batlow`) and clears
     `pending`. Deletes all *other* update files, stores `B:\<name>` in the update ctx
     (`+0x28CC`). Returns `pending`.

  So the update fires when: a `.upd` on B: whose `RAV_` version is **strictly newer** than
  the pen's, **and** the pen is on battery (GPIO8==0), **and** battery ADC ≥ 0x300. This is
  neither "factory first-boot only" nor "user-flash only": it is **any boot that finds a
  newer `.upd` on B: while unplugged** — i.e. the ordinary tiptoi-Manager flow (drop `.upd`
  on the USB drive, then unplug; the pen self-updates on the next boot). **Proven.**

### 1.2 The update state → format A → unpack

- **state 2 `fw_update`** entry `FUN_08052140` @0x08052140: builds the update filename,
  `fs_open`s the `.upd`, sets ctx `+0x208 = 1`. Its default action EA[8]=0x08052320 →
  **`fwupdate_finish_restart` @0x08052224**, which calls **`fw_update`** and, on success,
  writes **`B:/FLAG.bin`** (`DAT_080522dc → UTF-16 "B:/FLAG.bin"`) and `soft_reboot()`s.
  (splash's entry reads `B:/FLAG.bin` — the post-update handshake.) **Proven.**

- **`fw_update` @0x08051fd4** (**Proven**):
  1. `fs_open(.upd)`.
  2. `"start format driver A"` (@0x080520d8) → `fat_format_wrapper(...)` = full FAT mkfs on
     partition A. **`A:` is wiped on every update.** (Chicken-and-egg: the `Update<LANG>.wav`
     "update running" prompt is played from the *old* A: before this.)
  3. `func_0x08000d84(0)` (disable something) → `"start update"` → **`fw_update_2`**.

- **`fw_update_2` @0x080f2838** — the `PR_S` unpack sequence, in order (**Proven**):
  `upd_unpack_headinfo` (verify `ANYKA106` + header checksum) → `pr_p_read_partition_info`
  (reload NAND metadata blocks) → `pr_b_recover_bin_info` → **`pr_u_file_count`** (re-burn
  the `to_NAND` bins PROG/codepage) → **`upd_unpack_udisk_files(fd, 1)`** → `upd_unpack_bootfile`
  (rewrite SPL + metadata). The udisk files are step 5 of 6.

### 1.3 The actual file writer — `upd_unpack_udisk_files` 0x080f2760

```c
undefined4 upd_unpack_udisk_files(fd, flag) {              // @0x080f2760
  ctx = *DAT_080f298c;                                     // the update ctx
  rec = *(int*)(ctx + 0x34);                               // hdr+0x34: to_udisk file records base
  for (i = 0; i < *(uint*)(ctx + 0x30); i++) {             // hdr+0x30: file count (=7)
    if (!upd_unpack_udisk_dirs(fd, rec, name128, target556)) return 0;   // resolve target path
    if (!part_udisk_write_verify(fd, src_off, src_size, target556, flag)) return 0;
    rec += 0x110;                                          // next file record (stride 0x110)
  }
  return 1;
}
```

(The decomp shows `local_20`/`local_24` = the record's `.upd` offset and size, filled by
`upd_unpack_udisk_dirs`.) **Proven.**

- **`upd_unpack_udisk_dirs` 0x08105524** (path resolver): reads the 0x110-byte file record;
  takes the component before `\` (`voice` / `Language`); scans the **to_udisk dir records**
  (hdr+0x28 count, +0x2c adr, stride 0x210), matches `pcpath`, and rewrites the target to
  `udiskpath + remainder` — so `voice\Chomp_Voice.bin` → **`A:VOIMG\Chomp_Voice.bin`**,
  `Language\*.wav` → `A:Language\*.wav`. **Proven.**

- **`part_udisk_write_verify` 0x080f2444** (the writer) — per file (**Proven**):
  1. malloc a 0x1000 scratch buffer.
  2. **mkdir every path component** (splits on `\`/`/`, creating each dir via the FatLib
     vtable — `puVar2[0x16]` copy-substring, `puVar3[1]` mkdir).
  3. `"PR_S udisk file path:%s"` → **create the file** on A: (FatLib open create-mode).
  4. **Seek the `.upd` handle** to the record's `adr`, then loop `ceil(size/0x1000)` chunks:
     read `<=0x1000` from the `.upd` (`puVar2[0x12]`), write the same bytes to the A: file
     (`puVar2[0x13]`). No transform, no wrapping — a raw copy.
  5. Close, **read back** and byte-compare (`part_udisk_read_verify`) →
     `"PR_S udisk file compare success"`.

So the payload streamed onto `A:/VOIMG/Chomp_Voice.bin` is exactly `upd[adr : adr+size]` from
the `.upd`, verified by read-back. The `.upd` is the authoritative source; the pen's own
firmware is the writer.

---

## 2. Q2 — producer.bin's role (does it also write the FILES?)

**producer.bin does the low-level provisioning, NOT (in the code we can see it doing) the
`to_udisk` file write.** It has no `to_udisk`/`VOIMG`/`Chomp` string and no `ANYKA106`
container parser (grep of `data/producer.bin`: 0 hits for each). Its command interpreter
(`nand-init-from-producer.md` §1) does (**Proven**):

- cmd 5 `pr_p_init`, cmd 6 detect NAND, cmd 7 ASA format, cmd 9 `PR_M` zone/partition build,
  cmd 0x10/0x11 bin burn (PROG/codepage/boot), cmd 0x22 `pr_p_write_maps` (blocks-0 metadata).
- cmd 10 `pr_m_driver_format` — **FAT mkfs** of the udisk (BPB/FSInfo/FATs/root + 11-byte
  volume label). This produces an **empty** formatted A:/B:, no files.
- **The file-content channel is `PR_I`** (cmds 0xb, 0x14..0x17): the PC tool streams a
  **ready-made FAT image** (`PR_I img info: bytes_per_sec/fat_size/offset_fat1/…`,
  `PR_I write mtd data`) which the medium writes through with NFTL tags. If the factory used
  this path, the FAT image the PC built would already contain `VOIMG/Chomp_Voice.bin` etc.
  **Proven** that the mechanism exists; **Inferred** whether the factory used PR_I-with-files
  vs. formatting empty and letting the firmware's own first-boot update populate A:.

**Producer's division of labour vs. the firmware:**

| step | producer.bin (factory) | firmware update path (field/first-boot) |
|---|---|---|
| ASA / bad-block table | **yes** (cmd 7 `asa_format`) | no (reused) |
| partition/zone table (block-0 meta) | **yes** (cmd 9 `PR_M`, cmd 0x22) | refreshes (`upd_unpack_bootfile`) |
| burn PROG/codepage bins | **yes** (cmd 0x10/0x11) | **yes** (`pr_u_file_count`, RB/UB exchange) |
| SPL/nandboot | **yes** (boot write) | **yes** (`upd_unpack_bootfile`) |
| FAT mkfs on A: | **yes** (cmd 10) | **yes** (`fw_update` "start format driver A") |
| **write `to_udisk` FILES to A:** | **only via PR_I host image (Inferred if used)** | **YES — `upd_unpack_udisk_files` (Proven, verified)** |

**Conclusion for Q2:** producer.bin's *own* code only formats/burns/partitions; the only
place a producer would write the *files* is the host-driven PR_I FAT-image path, which is
outside the pen and needs the PC tool's cooperation. The firmware, by contrast, provisions
the `to_udisk` files itself from the `.upd` — a self-contained, reproducible path.

**Why this matters:** even a factory that formatted A: empty would end up with populated
A:/VOIMG on the **very first update-bearing boot** (the pen ships with a `.upd` staged, or
the first tiptoi-Manager sync drops one). So in practice the firmware update path is the
provisioner we can rely on and reproduce.

---

## 3. Q3 — what gates the update-path provisioning (the firmware's own conditions)

The self-contained unpack (`upd_unpack_udisk_files`, §1.3) runs as part of `fw_update`, which
the boot-decide logic reaches only under a specific set of firmware gates (all Proven from the
traced path):

1. **A newer `.upd` on B:.** `fw_update_cleanup` opens `B:\Update3202MT.upd` (or a name from
   `B:/LanguageInfoMT.txt`), so B: must contain the `.upd` *as a FAT file* with the `RAV_`
   trailer at `size−0x38` and a **version strictly greater** than the running PROG's (`"N0038"`
   digits at 0x08009000); an equal-or-older `.upd` self-deletes without updating.
2. **Battery gate + on-battery.** Boot-decide requires **GPIO8 == 0** (on battery, not plugged
   to a PC) to post 0x1002, and `fw_update_cleanup` requires **battery ADC ≥ 0x300**; otherwise
   the update is skipped/aborted. (See [`pmu-power-management.md`](pmu-power-management.md).)
3. **A: reformatted then written.** `fw_update` reformats A: (`fat_format_wrapper`) and then
   does FatLib file creates/writes on partition A for each unpacked file.
4. **The bin re-burn runs first.** `fw_update_2` calls `pr_u_file_count` (re-burns PROG/codepage
   as RB/UB pairs) and `upd_unpack_bootfile` (rewrites SPL + metadata) around the udisk unpack —
   i.e. a full update also rewrites the boot chain and block-0 metadata, not just the files. The
   file-writing step alone is `upd_unpack_udisk_files(fd, 1)` with the update ctx seeded
   (`hdr+0x30`/`+0x34` counts/addr, the `.upd` fd).
5. **`.upd` reachability.** The unpack seeks the same `fd` all over the `.upd`, so the file must
   be fully present on B: (11.3 MB).

---

## 4. Q4 — is hand-placing the extracted file byte-faithful? (Proven — verified)

**Yes, byte-for-byte.** `part_udisk_write_verify` copies `upd[adr : adr+size]` verbatim (§1.3,
no transform), and the record's `(adr, size)` for file 0 are `0x47EE00 / 0x41EBE4`. Verified:

```
.upd to_udisk record 0: "voice\Chomp_Voice.bin"  size 0x41EBE4  adr 0x47EE00
  md5(upd[0x47EE00 : 0x47EE00+0x41EBE4]) = 074bffc51de57c79449e02e5ef04039e
  md5(vfs/voice/Chomp_Voice.bin)         = 074bffc51de57c79449e02e5ef04039e   → IDENTICAL
```

So the firmware writes `A:/VOIMG/Chomp_Voice.bin` **identical** to the file extracted from the
`.upd`; hand-placing the extracted file gives exactly what the authentic update would write.
(The extracted `Chomp_Voice.bin` is derived from the copyrighted `.upd` and is not part of this
repository — obtain it from your own `.upd`.) The same holds for the six `Language/*.wav`.
The only thing the firmware adds beyond the raw bytes is the FAT directory entry + NFTL tags
(structural, not content). **Proven.**

---

## 5. Bottom line (direct answer to the user's question)

- **Is populating A:/VOIMG the producer.bin init's job, the firmware update's job, or both?**
  **Effectively the firmware update's job.** producer.bin only *formats and burns the
  low-level layout* (ASA, zones/partitions, PROG/codepage bins, SPL, empty FAT); it can lay
  down udisk *content* only via the host-streamed `PR_I` FAT-image channel (Inferred if the
  factory used it). The pen's **firmware** carries the self-contained `to_udisk` unpacker
  (`upd_unpack_udisk_files` 0x080f2760 → `part_udisk_write_verify` 0x080f2444) that reformats
  A: and writes all 7 files from the `.upd` — verified byte-identical to our extracted files.

- **When:** any boot where a **newer** `.upd` sits on B: **and** the pen is on battery
  (GPIO8==0) with ADC ≥ 0x300 → `root_action_boot_decide` → state 2 `fw_update` → format A →
  `fw_update_2` → unpack. Not factory-only, not user-flash-only.

- **Fidelity:** hand-placing the extracted `Chomp_Voice.bin` (and the Language WAVs) into A: is
  **byte-faithful** to what the firmware writes — MD5-verified. Only the FAT/NFTL structure is
  added by the firmware, not content.

### Name-table additions (for names.csv)

| proposed name | addr | evidence |
|---|---|---|
| `root_action_boot_decide` | 0x0804e570 | root EA[1]: `fw_update_cleanup` + GPIO8 gate → post 0x1001/0x1002 (statechart-full-map §2) |
| `fw_update_state_entry` | 0x08052140 | state-2 entry: build filename, `fs_open(.upd)`, ctx+0x208=1 |
| `fwupdate_finish_restart` | 0x08052224 | state-2 EA[8] worker: `fw_update` → write `B:/FLAG.bin` → `soft_reboot` |

(All other addresses already named in `upd-system-partition-layout.md` §5.)
