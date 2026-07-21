# The power-on sound — what actually plays at boot, from which state, and whether a tap is needed

> Resolves an apparent contradiction: *the real pen plays a power-on sound
> with no tap, yet a clean boot idles silently at standby until a cover-tap and the power-on
> jingle (voice 0x13) plays at book-state entry.* Pure static analysis of the named
> decompilation (unified base **0x08009000**) plus byte analysis of the `Chomp_Voice.bin` voice
> archive and a pen RAM dump (both firmware-derived content, captured from hardware, not included
> here). Tags: **[Proven]** = decomp/disasm/PROG bytes/file bytes cited; **[Inferred]** = inferred (reason
> given). Companions: `system-voice-feedback.md`, `statechart-full-map.md`,
> `cover-tap-first-product-load.md`, `pmu-power-management.md`.

---

## 0. Verdict (one paragraph)

**Voice 0x13 is the power-on jingle** — it and the power-off jingle 0x14 are the only two
*musical* clips in the boot-adjacent voice set (adjacent IDs, both mono 32 kHz, 1.62 s/1.66 s,
sustained-note pitch plateaus; everything else near the boot path is speech) **[P bytes +
pitch analysis §4]**. Its **only** call site is `book_state_entry` 0x080345cc, gated to **once
per power cycle** (bit 1 of `0x081da086` — set there, cleared nowhere) **[Proven]**. On a **clean
healthy boot this firmware is provably silent until book(13) is entered**, and the no-tap
routes into book(13) are the **`[0x24]==1` auto-descent** — reached EITHER via the
**power-button-held boot latch** (`app_init_main` 0x08038f5c: `[0x24]=(GPIO11==1)`, see the
§3) OR the **post-update resume** (`B:/FLAG.bin` → splash sets
`[0x24]=1`) — and the **low-battery** descent **[P §2–3]**. The live pen RAM dump shows
**`game_ctx[0x24]==1`** — consistent with a power-button boot (the normal case) and/or a
FLAG.bin resume; the FLAG.bin route plays voice **0x19** (update announcement, 5.5 s speech)
at splash before the descent, the plain GPIO11 route descends silently to book(13) and plays
**0x13, with no tap** **[P code; route attribution I]**. On a normal (no-FLAG) boot the "power-on jingle" actually plays **at the first cover
tap** (first book entry), not at power-on. "Idle silently at standby until a cover-tap" is
**correct for a clean boot**; the subtlety is only that the FLAG.bin (and power-button-held)
auto-descent is an authentic firmware behavior, not a spurious one. Either way, a system prompt
is audible only if `A:/VOIMG/Chomp_Voice.bin` is present — with the voice archive absent every
`fwl_play_voice_by_id` is a silent no-op.

---

## 1. Q1 — every voice reachable at boot with NO tap (exhaustive)

All system sounds go through exactly four sinks — `fwl_play_voice_by_id` 0x080ab9ac
(Chomp_Voice), `play_media` 0x080ab7b4 (GME, needs a mounted product), `lang_play_batlow`
0x080523b4 (updater WAVs), and raw `play_media_setup` 0x080ab6ac (prod-test whole-file plays).
Callers of `aud_player_set_source`/`aud_player_play` outside these: none (only the medialib
internals 0x080329b0/0x08032c94). **[P corpus grep]** So the boot-audio inventory is:

| trigger (state) | condition | sound | evidence |
|---|---|---|---|
| root(0) EA `root_action_boot_decide` 0x0804e570 | pending `B:/*.upd` **and** GPIO8==0 | `A:/Language/Update<LANG>.wav` (speech), then →fw_update(2) | [P 0x0804e570.c L9–19] |
| splash(1) ENTRY 0x0804c1d4 L40–47 | **`B:/FLAG.bin` exists** (opens → closes → **deletes** it via `fs_file_delete` 0x080ad564) | **voice 0x19** (5.54 s speech = update-complete/resume announcement) + `game_ctx[0x24]=1` | [Proven] |
| splash(1) ENTRY L53–102 | **GPIO11==1 && GPIO1==1, 4× over ~30 HAL ticks** | **voice 0x1B** (0.94 s **speech**, not a chime), then the prod-test cascade (§5) | [Proven] |
| splash(1) default EA 0x0804cecc L31–41 | `akoid_buf[0xb3]==0` (= retail path) | **nothing** — waits for audio idle, auth-chip check, posts **0x1014** → standby | [Proven] |
| standby(3) ENTRY 0x080511a0 | — | **nothing** (serial/log files + `udisk_gme_discovery`; zero audio calls) | [P full read] |
| standby(3) EA `standby_handler` 0x08051b0c | — | **nothing** (re-arm / idle count / auto-off / GPIO11-rescan; zero audio calls) | [P full read] |
| standby → book descent, **no tap** | `game_ctx[0x24]==1` (power-held boot latch OR FLAG.bin, §3) → post 0x1058 (L126–131) | book(13) entry → **voice 0x13** | [Proven] |
| standby → book descent, **no tap** | low battery: `battery_monitor_tick` 0x080afd78 warn/final + `akoid_buf[0x21]==0xff` → 0x104A+0x1058 | voices **0x17/0x1A**, then 0x14 + off | [Proven] |
| any first book(13) entry (normally = **first cover tap**) | bit 1 of 0x081da086 clear | **voice 0x13 — the jingle** | [P 0x080345cc.c L132–138] |

**There is no voice call in splash-entry (retail branch), standby-entry, or standby-handler.**
A clean, healthy, battery-only boot is **silent** from power-on to the first tap; after ~300
accepted heartbeats (~30 s) at idle standby it powers off, also silently (the inline auto-off
plays nothing — `0x08051b0c.c` L76–99). **[Proven]**

Exhaustive poster check for the book-opening event 0x1058 (PROG pool scan for the constant):
exactly three — `cover_oid_classifier` 0x08037cec (tap), `standby_handler` 0x08051b0c
(`[0x24]==1` resume), `battery_monitor_tick` 0x080afd78 (low-batt). 0x1057/0x105A: zero
posters (dead). 0x1059: only `state12_default_action` 0x0803440c. **[P scan]**

---

## 2. Q2 — is voice 0x13 really book-entry-only? YES, and it is the jingle

- **Sole call site**: `book_state_entry` 0x080345cc L137 (`fwl_play_voice_by_id(0x13)`). Corpus
  grep of all 9 `fwl_play_voice_by_id` callers (0x0800a9c8, 0x0803629c, 0x08034bc0, 0x0804c090,
  0x0804cecc, 0x080345cc, 0x0804bf84, 0x0804c1d4 + the fn itself): no other 0x13. Variable-id
  sites cover 0x09–0x18 (digits `n+9`), 0x15/0x16, 0x23/0x24, 0x26/0x27, 0x29/0x2A, 0x2B/0x2D —
  none reaches 0x13. **[Proven]**
- **Gate**: `if ((*0x081da086 & 2) == 0) { amp-on delay; *0x081da086 |= 2; delete
  "A:/African.fen" (vendor artifact, `FUN_08034588`); play 0x13; }` — bit 1 is set here and
  **cleared nowhere** in the corpus (all other users of 0x081da086 touch bits 4/5/6/7), so 0x13
  plays **exactly once per power cycle**, at the **first** book(13) entry. **[P pool scan of
  0x081da086 users: 0x0800a9c8 (&0x10), 0x0803629c (&0x20), 0x08034bc0 (&0x10), 0x08034300
  (|0x40), 0x08034130 (&0x7f,|0x80), 0x0804cecc (&0x10,&0xef), 0x080afd78 (|0x10)]**
- **It is the jingle, not speech** — frame-level pitch analysis of the WAVs extracted from the
  `Chomp_Voice.bin` archive (firmware content, not included here):

  | id | fmt | dur | F0 track character | verdict |
  |---|---|---|---|---|
  | **0x13** | 32 k | 1.62 s | sustained plateaus 533→131→1032/1067→131→780/800→1524 Hz (held notes) | **musical jingle** (power-on) |
  | **0x14** | 32 k | 1.66 s | sustained plateaus 1067→1524(long)→262 Hz | **musical jingle** (power-off — proven by its off-path call sites) |
  | 0x19 | 32 k | 5.54 s | wandering 105–195 Hz + unvoiced gaps | male speech (update announcement) |
  | 0x1B–0x1F | 16 k | 0.9–1.3 s | gliding 200–410 Hz contours + fricative frames | female speech (prod-test phrases) |
  | 0x2C | 32 k | 3.17 s | wandering 100–175 Hz + pauses | speech (power-off announcement) |

  0x13/0x14 are the **only two musical clips** among all boot/off-path voices — a matched
  on/off pair (same rate, ~same length, adjacent IDs). **[P bytes; "jingle vs speech"
  classification I from pitch structure of the extracted v13 clip]**

So the doc claim "0x13 = power-on jingle, played at book entry once per boot" is **correct**,
with one precision: *"once per boot" = once per power cycle at the FIRST book entry* — which on
a normal boot is the **first cover tap**, not power-on itself.

---

## 3. Q3 — does the pen auto-enter book(13) at boot? Only on the resume/battery paths — and the live pen did

Static routes into book_mount(12)/book(13) without a tap (from §1's exhaustive 0x1058 scan):

1. **`B:/FLAG.bin` resume** [Proven]: `fwupdate_finish_restart` 0x08052224 **creates** `B:/FLAG.bin`
   (UTF-16 string in .data @0x08121a68; fs_open(path,1,1), writes {0x0A,0x00}) at update end and
   soft-reboots. Next boot: `splash_entry` finds it → **deletes it** → `game_ctx[0x24]=1` → plays
   **0x19**; the splash EA **waits for the 5.5 s announcement to finish** (`is_audio_playing()`
   gate at 0x0804cecc L32) → posts 0x1014 → standby; `standby_handler`'s first accepted event:
   `[0x24]==1` → `akoid_poll_from_idle` + OID timer + **post 0x1058** → book_mount(12) →
   (unconditional 0x1059) → book(13) → **voice 0x13**. Sequence heard on real hardware:
   *power-on → 5.5 s speech → jingle → sits in (product-less) book mode.* **No tap.**
2. **Low-battery** [Proven]: `battery_monitor_tick` posts 0x104A+0x1058 when warn/final triggers with
   no product loaded — descends so the warn/final voices (0x17/0x1A) can be sequenced.
3. **Cover tap** [Proven]: the classifier first-load branch — needs a real 0x1060.

**Live-pen ground truth**: a RAM dump from a real 2N pen (hardware capture, not included) has
**`game_ctx[0x24] = 1`** (offset 0x19C8; also `[0x1d]=2`, `[0x1e]=0`, low-batt stage 0).
`game_ctx` is in `.bss` (zeroed by `__main` at cold boot). There are **TWO** `[0x24]` writers,
both byte-verified:
1. **`app_init_main` 0x08038f5c L20-27** (runs FIRST, every boot): latches
   `[0x24] = (hal_gpio_read(11) == 1)` — **GPIO11 = the POWER button at boot**. A pen powered
   on by a held power button latches **1**; the else branch writes **0**.
2. **splash's FLAG.bin branch** (0x0804c1d4 L43, later in the same boot): sets `[0x24]=1`
   iff `B:/FLAG.bin` exists (post-update resume) — conditional, set-only.
**⇒ the dumped pen's `[0x24]=1` is explained by EITHER route** (power button still held
during `app_init_main` — the normal way every pen is switched on — or a FLAG.bin resume);
the dump alone cannot distinguish which route the dumped pen took, so that attribution stays
[Inferred]. **[P dump byte + P decomp both-writers]**

Since FLAG.bin is deleted at splash, this is a **once-after-each-update** behavior. A pen that
is being re-flashed frequently (ours, during RE work) shows it on (nearly) every observed boot;
a factory-fresh pen shows it on first boot after provisioning iff the producer/updater left a
FLAG.bin. **[P mechanism; frequency attribution I]**

Side note: the dump's `[0x24]=1` does not by itself prove splash ran — `app_init_main`'s GPIO11
latch writes 1 with no splash involvement.

---

## 4. Q4 — is the first tap needed? Separate the sound from the book

| what | needs a tap? | mechanism |
|---|---|---|
| power-on **jingle 0x13** | **normally YES** (first cover tap → first book entry); **NO** on a post-update boot (FLAG.bin descent) or low-batt descent | §2, §3 |
| update announcement 0x19 | NO (splash entry, FLAG.bin boots only) | §1 |
| opening a **product/book** (mount + content) | **YES, always** — the mount runs in-book off tapped OIDs (`gme_oid_dispatch`); even the FLAG.bin descent lands in a *product-less* book(13) | [P `cover-tap-first-product-load.md`] |

**Authentic clean-boot sequence** (retail pen, healthy battery, no FLAG.bin, no USB):
```
button → maskROM → SPL → PROG app_init_main (GPIO15=1, mount, statechart)
root(0) →0x1001→ splash(1)  [SILENT: no FLAG.bin, GPIO11/1=0 ⇒ akoid_buf[0xb3]=0]
  first event → EA[0]: auth-chip ok → post 0x1014
standby(3)  [SILENT: entry = logs + discovery; first heartbeat → idle mode 8]
  … idles SILENTLY; >300 heartbeats → silent hardware power-off …
  OR: first cover/product tap → 0x1060 → classifier first-load → 0x1058 → 12 → 13
      book(13) ENTRY → ♪ VOICE 0x13 (the "power-on" jingle, once per power cycle)
      → subsequent taps mount/play the product
```
**[P — every edge cited in §1–§3]**

**Post-update boot** (what the observed pen did): power-on → **0x19 speech (5.5 s, splash)** →
standby → auto 0x1058 → book(13) → **♪ 0x13** — all with no tap. **[Proven]**

---

## 5. The splash "healthy boot" branch is the FACTORY PROD-TEST mode, not retail

`splash_entry`'s GPIO11&&GPIO1 branch (4 consecutive 1-reads) does: `akoid_buf[0xb3]=1`,
checksum-verify **`B:/spotlight.bin`** (string @0x08121a9c) against `*0x08008cc4`, stage :=
0x1F/0x20, play **0x1B**. With `[0xb3]=1`, the splash EA becomes a stage machine that then
plays **0x1C** and **0x1D/0x1E** (checksum verdict), writes a checksum log, and parks at
splash waiting for jig input (0x105F codes 5/6/8 drive OID/auth/test-file stages, the
`B:/TestFile/Test1..6.bin` checks, digit read-outs). The .data string table
@0x08121a9c: `B:/spotlight.bin`, `B:/Prodtest.txt`, `B:/TestFile/Test1..6.bin`,
`C:/TestFile/Test*_sd.bin` — unambiguous production-test context; and 0x1B–0x1F are speech
clips (§2 table). A retail pen on this path would speak test phrases every power-on and never
reach standby — so **retail pens must read GPIO11/GPIO1 = 0 at boot** and take the quiet path.
The quiet path IS the normal retail boot; the GPIO11&&GPIO1 path is the factory test (0x1B is
a prod-test speech clip, not a power-on jingle).
(The physical meaning of GPIO11 stays [Open] — pmu §3.5.) **[P code/strings; "retail reads 0"
I by elimination]**

---

## 6. Q5 — implications

1. **A clean boot is silent at standby** (no autonomous descent) — there is no authentic boot
   jingle to suppress on a clean boot. **[Proven]**
2. **The observed-hardware behavior (sound with no tap) is the `[0x24]==1` auto-descent** —
   on a real pen most simply the power button still held through `app_init_main` (GPIO11
   latch, §3), and/or the FLAG.bin resume. The FLAG.bin flavor is driven purely by content:
   with `B:/FLAG.bin` (2 bytes, {0x0A,0x00}) on the B: partition the firmware itself announces
   (0x19), descends 3→12→13 with **no tap**, plays 0x13, and deletes the flag (so the next boot
   is clean). **[Proven]**
3. A system prompt is audible only if the A: FAT carries `VOIMG/Chomp_Voice.bin` (plain, NO
   XOR); otherwise `fwl_play_voice_by_id` silently no-ops (`fs_open==-1` return).
   **[P `system-voice-feedback.md` §1.1/§5]**
4. Expected authentic audio timeline for a clean boot with the voice archive present:
   silence → tap → **0x13** at book entry → (mount/product voices per the cover-tap doc). For a
   FLAG.bin boot: **0x19 (full 5.5 s — the splash EA waits on `is_audio_playing`)** → **0x13**
   preceded by no tap. **[Proven]**
5. GPIO `0x040000BC` bits 11/1 = 0 is the retail boot; 11/1 = 1 enters the factory prod-test
   splash (§5), which parks at splash. **[Proven]**

### Discriminator for the remaining [Inferred]

The extracted clips are **v13** (candidate power-on jingle), **v14** (power-off jingle), **v19**
(update announcement), **v1b** (prod-test phrase). If a pen at power-on-without-tap plays **v19
then v13**, the FLAG.bin explanation holds. If it plays **v13 alone, immediately, on EVERY boot
of a non-updated pen**, this 3202MT image cannot produce that statically — that would point at a
firmware version difference (the observed pen is the newer ANYKANB2 generation; this
decompilation is update3202MT) and re-opens the question for that image. **[discriminator]**

---

## 7. Evidence index

| claim | status | source |
|---|---|---|
| retail splash branch (`[0xb3]==0`) plays nothing, posts 0x1014 after auth | P | 0x0804cecc.c L31–41; DAT_0804e528=0x1014 (PROG bytes) |
| splash/standby entries + standby_handler contain zero audio calls (except FLAG/GPIO branches) | P | 0x0804c1d4.c, 0x080511a0.c, 0x08051b0c.c full reads |
| 0x13 sole site = book entry, once per power cycle (bit1 0x081da086 set-only) | P | 0x080345cc.c L132–138; pool scan of 0x081da086 users |
| 0x13/0x14 = the only musical clips (jingle pair); 0x19/0x1B–0x1F/0x2C = speech | P bytes / I classification | frame pitch analysis of the WAVs extracted from Chomp_Voice.bin |
| 0x1058 posters = classifier / standby-resume / battery only; 0x1057/0x105A dead | P | PROG pool scan + decomp grep |
| FLAG.bin: created by fwupdate_finish_restart, one-shot consumed+deleted by splash; sets [0x24]=1 → auto-descent → 0x13, no tap | P | 0x08052224.c L29–36; 0x0804c1d4.c L39–47; 0x08051b0c.c L126–131; .data string @0x08121a68 |
| live pen booted via FLAG.bin path (`game_ctx[0x24]==1`) | P byte / I narrative | pen RAM dump @0x19C8 (hardware capture, not included); writer scan (only splash writes 1; .bss zero at cold boot) |
| GPIO11&&GPIO1 splash branch = factory prod-test (spotlight.bin/Prodtest.txt/TestFile) | P context / I "retail reads 0" | 0x0804c1d4.c L53–102; .data strings @0x08121a9c–…; 0x0804cecc stage machine |
| splash EA waits for 0x19 to finish before 0x1014 | P | 0x0804cecc.c L32–34 (`is_audio_playing` early return) |
| clean-boot auto-off (~300 heartbeats) is silent | P | 0x08051b0c.c L69–99 |
