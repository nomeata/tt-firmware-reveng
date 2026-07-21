# GME per-game-type parameter words — the community's unnamed slots, resolved

> READ-ONLY reverse-engineering pass over the 2N (MT) decomp
> (`out/decomp_named/`, base 0x08009000). Resolves the game-type-specific
> config fields that tip-toi-reveng documents as unknown placeholders:
> Subgames `u0`–`u9`, Games `c`/`i`/`q`/`x`/`w`/`v`, Special-symbols `c`–`g`.
> Flagged "still-open but reversible" in `community-open-questions-resolved.md`
> (§1 #12 and the Still-open list); this doc closes that item.
>
> Evidence tags: **[P] Proven** = the decomp shows the read *and* the use;
> **[I] Inferred** = structure/dataflow implies it; **[O] Still-open**.
> Community field names are tttool's (`GME-Format.md` / `src/GMEParser.hs`)
> and the wiki's GME-Games/GME-Subgames/Special-OID tables.

---

## 0. How the firmware consumes the game table (recap)

There is **no RAM copy of a parsed game record**. Every game-state module
re-reads the record from the GME file with `fs_seek`/`fs_read` (header 0x10
→ game-table → offset[gamectx+0x131] → record). `FUN_08034cbc` @0x08034cbc
reads only the type word for dispatch; each game *state* then has its own
record loader, subgame parser and engine. Game-type → handler state mapping
is in `statechart-full-map.md` §2; the addresses used below:

| GME type | state | entry | record loader | subgame parser | engine (EA) |
|---|---|---|---|---|---|
| 1,2,3,5,10 | 14 | 0x08078e58 | `FUN_080774e0` @0x080774e0 | `FUN_08077a8c` @0x08077a8c | 0x08078ec4 + phase engine `FUN_080785cc` @0x080785cc |
| 4 | 15 | 0x08080780 | (see §3) | (see §3) | 0x080810d0 |
| 6 | 16 | 0x0808339c | (see §4) | (see §4) | 0x080833f8 |
| 7 | 17 | 0x08085370 | (see §5) | (see §5) | 0x080853c8 |
| 8 | 18 | 0x080866a8 | (see §5) | (see §5) | 0x08086764 |
| 16 | 19 / 60 (product 2) | 0x08088044 / 0x0808c610 | (see §6) | (see §6) | 0x080880a0 / 0x0808c678 |
| 9 | native product states (30/32/42–49/64/65) | — | (see §7) | — | — |
| 253 | 31 | 0x080aa208 | n/a (empty record) | n/a | 0x080aa498 |

Support helpers used by all game modules [P]:
- `FUN_08078014(pllOffset)` — parse a playlist-list, leaves its **count** in
  gamectx+0x490 (each module has its own twin).
- `FUN_08078050(pllOffset, index)` — play playlist `index` of a playlist-list.
- `func_0x080031d0(n, seed)` — HAL random, returns value < n (used for
  "pick a random playlist of the list").

The state-14 game context (physpool block at `*(DAT_08078000+4)`) is the
reference layout; other modules mirror it.

**Addendum 2026-07-19 — dispatch completeness (for tttool PR review):** the
type dispatch in `gme_oid_dispatch` @0x0803629c is an explicit if-chain over
the type *byte* (read via `gme_read_game_type` @0x08034cbc): 1/2/3/5/10,
4/40, 6, 7, 8, 9 (per-product), 16, **17–23 (dedicated states 58/59/61/62/
63/66/68)** and 253 each set a launch event; **any other value (e.g. 0,
11–15, 24–39, 41+) falls through to `LAB_08036528` and returns the raw type
with no launch event — the tap is simply unhandled** (no error voice). Note
the whole chain is gated: if hdr@0x98 (additional game-binaries table) is
non-zero/-1, the tap posts the embedded-binary event (state 67) *instead* of
any type dispatch. Corpus cross-check (all 205 published GMEs): the only
type numbers occurring outside the dispatched set are 0 (285 records, empty
dummy entries), 11–15 (Englisch-Detektive) and 30 (Leserabe Pony) — all in
binary-carrying products, consistent with the 0x98 gate. [P, decomp]

---

## 1. Common game header (types ≠ 6) — the 9 × u16 words

File layout (record-relative): word *n* at byte `+2n`.

| off | community name | firmware storage (state 14) | firmware meaning | evidence |
|---|---|---|---|---|
| +0x00 | `t` game type | gctx+0x00 (u32) | dispatch selector | [P] loader 0x080774e0 |
| +0x02 | `b` subgame count | gctx+4 (byte) | # subgame offsets read; rounds pick a random *unplayed* subgame from a 128-bit played-mask (gctx+0xc..0x18, picker `FUN_080779ac`) | [P] |
| +0x04 | `r` rounds | gctx+5 (byte) | total rounds; compared against the round counter gctx+0x1d at round end (engine @0x080785cc phase 3, `if (roundCtr < rounds)`) | [P] |
| +0x06 | `c` (tttool `gFlagC`, "0/1, effect not pinned down") | gctx+6 (byte) | **multi-target round flag.** 0 → the round is complete after the first correct tap. 1 → the player must find **all** entries of subgame OID-list 1: each hit clears that OID's bit in the availability masks gctx+0x24c..0x258; while bits remain, a fresh hit plays subgame-PLL 2 ("right, keep going") and a repeat-tap on an already-found target plays subgame-PLL 5; only when all masks are empty does the score increment and the round end (engine lines: mask test over gctx+0x24c..0x258, `+0x21==2` → PLL5) | **[P]** — closes tttool's `gFlagC` TODO |
| +0x08 | `gEarlyRounds` | gctx+7 (byte) | **round number at which the round-start playlist switches**: at the end of each round, if roundCtr == earlyRounds the *later-round-start* PLL (+0x38) is played, else the *round-start* PLL (+0x34); 0 → no round-start announcement at all (phase-3 code @0x080785cc) | [P] |
| +0x0A | `gRepeatOID` | gctx+8 (u16) | **replay-prompt control OID**: a 0x1060 tap equal to this word replays the current question — subgame-PLL 1 index 0 while a question is open, the last-played hint (PLL 6, index tries−1) after a wrong answer, else the last game-level announcement (saved at gctx+0xc8/+0xb7 by `FUN_08077230`) (handler 0x08078ec4 line `uVar15 == *(ushort*)(iVar17+8)` → LAB_08079084) | [P] |
| +0x0C | `x` (`gTuningX`, always 0) | — | **read and discarded** (three bare `fs_read`s in the loader) | [P] dead field |
| +0x0E | `w` (`gTuningW`, always 111) | — | read and discarded | [P] dead field |
| +0x10 | `v` (`gTuningV`, always 222) | — | read and discarded | [P] dead field |

Game-level playlist-list offsets (5 × u32 following the header) land at
gctx+0x28 (start), +0x30 (round-end), +0x2c (finish), +0x34 (round-start),
+0x38 (later-round-start) — i.e. tttool's names are confirmed; the start
PLL is played immediately at state entry (`FUN_08078050(gctx+0x28, 0)`),
the finish PLL (+0x2c) in the game-over phase 4. [P]

**Target scores / finish playlists** (10 × u16 @gctx+0x494.., 10 × u32
@gctx+0x4a8..): at game end the score (gctx+0x1c, +1 per completed round)
is cascaded against the ten thresholds; score ≥ ts[0] → finish-PLL 1,
between ts[k] and ts[k−1] → finish-PLL k+1 (engine phase 4/3 cascade,
0x080785cc lines 398–451). tttool's names confirmed, with the exact
threshold-to-playlist mapping now pinned. [P]

---

## 2. Subgame header `u0`–`u9` — state 14 (types 1, 2, 3, 5, 10)

Subgame record: 20-byte header (`u0..u9`, ten u16), then three OID lists
(u16 count + entries each), then 9 × u32 playlist-list offsets.
Parser `FUN_08077a8c` @0x08077a8c: u0–u5 → gctx+0x23c..0x241 (bytes),
**u6–u9 read and discarded**; OID-list-1 count → gctx+0x25c, list positions
computed to gctx+0x464/0x468/0x470; PLL offsets 1..8 → gctx+0x244, 0x474,
0x47c, 0x484, 0x478, 0x480, 0x488, 0x48c (**PLL 9 is never read**). [P]

The engine's use of the three OID lists [P]:
- **list 1** (tttool `sgTargetOids`) = the **answer/target set** — matcher
  `FUN_08078318` @0x08078318, hit → correct-answer path.
- **list 2** (`sgOids2`) = **known-wrong answers with dedicated feedback**
  (the decoys) — matcher `FUN_08078474`, hit → subgame-PLL 3, retry-limited
  by `u3`.
- **list 3** (`sgAllOids`) = **the rest of the subgame's active OIDs** —
  matcher `FUN_08078524`, hit → subgame-PLL 6 (hints), retry-limited by `u4`.
- A tap in *none* of the lists → subgame-PLL 4 ("not part of this game").

| slot | storage | firmware meaning | evidence |
|---|---|---|---|
| `u0` | gctx+0x23c | **correct-feedback indexing mode for PLL 2**: 0 = play a random playlist of PLL 2; 1 = index by the matched target's position in OID-list 1; 2 = index by the number of targets found so far (gctx+0x24a − 1) (engine @0x080785cc lines 294–300) | **[P]** |
| `u1` | gctx+0x23d | consumed only inside the list-1 matcher `FUN_08078318`: when set, a hit can be demoted to a miss depending on the availability mask of the previously-hit slot — a re-tap/ordering gate (decomp is register-mangled here; see §8 disasm check) | see §8 |
| `u2` | gctx+0x23e | **wrong-feedback indexing mode for PLL 3 / PLL 8**: 0 = random pick; ≠0 = index by the matched OID's position in list 2 (engine lines 217/231/245) | **[P]** |
| `u3` | gctx+0x23f | **max wrong taps on OID-list 2** (per question, counter gctx+0x1e): while tries < u3 → play PLL 3 and retry; tries ≥ u3 → play PLL 8 (give-up/solution) and end the round scoreless; u3 = 0 → unlimited retries (engine lines 206–241) | **[P]** |
| `u4` | gctx+0x240 | **max wrong taps on OID-list 3** (counter gctx+0x1f): while tries < u4 → play PLL 6 (game type 2 plays PLL 6 *sequentially* by try count = escalating hints, other types random); tries ≥ u4 → PLL 8 give-up, round ends; u4 = 0 → PLL 6 forever (engine lines 146–201) | **[P]** |
| `u5` | gctx+0x241 | stored by the parser but **never read back** anywhere in the module | [P] dead (this pen) |
| `u6`–`u9` | — | **read and discarded** by the parser | [P] dead (this pen) |

Subgame playlist-list roles (state-14 engine) [P]:

| PLL # | gctx | role |
|---|---|---|
| 1 | +0x244 | question / round announcement (played at round start, index 0; replayed by `gRepeatOID`) |
| 2 | +0x474 | correct-answer feedback (index mode = `u0`) |
| 3 | +0x47c | wrong-answer feedback for OID-list-2 (decoy) taps (index mode = `u2`) |
| 4 | +0x484 | "that OID is not part of this game" feedback (random) |
| 5 | +0x478 | multi-target mode (`c`=1): "you already found that one" repeat-tap feedback (random) |
| 6 | +0x480 | wrong-answer feedback for OID-list-3 taps — hint chain (type 2: sequential by try #) |
| 7 | +0x488 | round-complete jingle (random; type 10 uses it as its per-answer feedback) |
| 8 | +0x48c | give-up / solution after `u3`/`u4` exhausted (index mode = `u2`) |
| 9 | — | never read by this firmware |

---

## 3. Game type 4 (state 15) — the memory-sequence game

Type 4 (and its no-repeat variant **type 40**) is a memory-sequence /
cumulative-step study game. It **shares state 14's game-record loader**
(`FUN_080773c8`/`FUN_080774e0`), with entry `FUN_08080780` @0x08080780,
engine `FUN_08080988` @0x08080988 (EA/wrapper `study_step_handler`
@0x080810d0), subgame parser `FUN_08080264` @0x08080264. Because the loader
is shared, the header mapping matches §1; the type-4-specific reads:

| off | community name | ctx | firmware meaning for type 4 | evidence |
|---|---|---|---|---|
| +0x00 | type | gctx+0 | **type 40 vs 4**: `type != 0x28` selects draw-with-repeats (4) vs no-repeat draw (40) in `FUN_08080178` | [P] |
| +0x02 | subgameCount | gctx+4 | random-draw bound | [P] |
| +0x04 | rounds | gctx+5 | **number of steps**: entry pre-draws `rounds` subgames into a sequence; each round activates one more; end when step > rounds | [P] |
| +0x06 | `c`/flagC | gctx+6 | **NOT read by type 4** | [P absence] |
| +0x08 | earlyRounds | gctx+7 | **NOT read by type 4** | [P absence] |
| +0x0A | repeatOID | gctx+8 | replay current prompt chain | [P] |
| +0x0C/E/10 | x/w/v | — | discarded | [P] |

PLL usage differs from state 14: start (+0x28) at entry; **round-end (+0x30)
indexed by current step count** (per-step feedback, not the state-14 role);
finish (+0x2c) at end (with a product-2 special: `rand(5)+1` and **game
chaining** — re-enters `FUN_08080780` with record index 0xC). Target scores
and finish-PLLs are **not read** (type 4 has no scoring). [P]

Subgame `u0`–`u9` (parser `FUN_08080264`, stores u0–u5, discards u6–u9):
**only u2/u3/u4 are read** — u2 = wrong-feedback index mode, u3 = list-2
wrong-tap limit (→PLL8 give-up + game over), u4 = OID-list-3 wrong-tap limit
(same). **u0, u1, u5 stored but never read** (the type-4 matchers have no
flagC/u1 logic). PLL3 has a dual-use quirk (matched as an OID list *and*
playable) that is defensive-dead in the one real type-4 GME (WWW Bauernhof,
count 0). [P]

The type-4 handler keeps the book's GME play-script layer live during play
(non-game taps run `gme_oid_to_playscript`). [P]

---

## 4. Game type 6 (state 16) — the bonus-stage game, fields `i` and `q`

Loader `FUN_08081d18` @0x08081d18, engine `FUN_08082bf4` @0x08082bf4,
subgame parser `FUN_080823a0` @0x080823a0, subgame matcher `FUN_08082950`.
ctx = `*DAT_080822c0`. Type-6 has a distinct file layout (13 header words +
2 extra PLL offsets + a bonus stage); the two words tttool leaves unnamed
(`i` and `q`) are the headline result.

Header words (file order):

| off | community name | ctx | firmware meaning | evidence |
|---|---|---|---|---|
| +0x00 | type | +0x00 | dispatch | [P] |
| +0x02 | subgameCount | +4 | main-stage subgame count | [P] |
| +0x04 | rounds | +5 | main-stage round limit | [P] |
| +0x06 | bonusSubgameCount | +6 | bonus-stage subgame count | [P] |
| +0x08 | bonusRounds | +7 | bonus-stage round limit | [P] |
| +0x0A | bonusTarget | +8 | **min main-stage score to unlock the bonus stage** (phase 3: score < bonusTarget → play finish-PLL 2 and end; else finish-PLL 1 → bonus stage) — refines tttool's name | [P] |
| **+0x0C** | **`i`** (tttool `gBonusTuningI`, unknown) | **+0xA** | **= the type-6 slot of the common `c` flag: the "find ALL targets in the round" flag.** i≠0 → each correct tap clears a target bit (masks ctx+0x454..0x460); round ends only when all are cleared; i=0 → first correct tap ends the round. Byte-identical logic to state-14 `c`. | **[P]** — closes `gBonusTuningI` |
| +0x0E | earlyRounds | +0xB | main-stage round-start-PLL switch round | [P] |
| **+0x10** | **`q`** (tttool `gBonusTuningQ`, unknown) | **+0xC** | **= earlyRounds for the BONUS stage**: phase 7 `bonusRoundCtr == q` → play laterRoundStartPlaylist2 (ctx+0x40) else roundStartPlaylist2 (ctx+0x3c). Symmetric to earlyRounds in the main stage. | **[P]** — closes `gBonusTuningQ` |
| +0x12 | repeatOID | +0xE | replay-prompt control OID (as §1) | [P] |
| +0x14/16/18 | x, w, v | — | read and discarded | [P] |

PLL offsets (7 × u32): start→+0x2c, **round-end DISCARDED** (type-6 never
plays a round-end PLL), finish→+0x30, roundStart→+0x34, laterRoundStart→
+0x38, roundStart2→+0x3c, laterRoundStart2→+0x40. [P]

Subgames: `subgameCount` u32 offsets → ctx+0x44[], then `bonusSubgameCount`
u32 offsets → ctx+0x244[] — the firmware splits tttool's single subgame
array **positionally** (tail entries = bonus subgames). The
`gBonusSubgameIds` list offset is **never read** (loader stops after the 10
finish-PLL words). [P]

Scores / finish PLLs (10 u16 → ctx+0x69a.., 10 u32 → ctx+0x6b0..): the
firmware treats these as tttool's `2 targetScores + 8 bonusTargetScores` and
`2 finishPLLs + 8 bonusFinishPLLs`. **Dead data on this pen**: the 2 main
targetScores, bonusTargetScores[4..7], bonusFinishPLLs 5–8 — stored, never
read. The 2 "finish playlists" are repurposed: FPL1 = bonus-unlocked cue,
FPL2 = missed-bonus ending. Bonus ending cascades score vs
bonusTargetScores[0..3] → bonusFinishPLL 1..4. [P]

Bonus machinery: ctx+9 = stage flag (0 main / 1 bonus); phase-4 entry
(after score ≥ bonusTarget) zeroes score/round/masks; phase-5 picks a random
unplayed **bonus** subgame via `FUN_080822e0(bonusSubgameCount)` reading the
ctx+0x244 array. [P]

Subgame `u0`–`u9` (parser `FUN_080823a0`, layout identical to state-14):
u0→+0x444 (correct-feedback index mode), u1→+0x445 (matcher gate, see §8),
u2→+0x446 (wrong-feedback index mode), u3→+0x447 (OID-list-2 wrong-tap
limit), u4→+0x448 (OID-list-3 wrong-tap limit), u5→+0x449 **stored never
read**, u6–u9 **read and discarded**. Same meanings as §2, verified in this
module's engine. [P]

---

## 5. Game types 7 (state 17, subgame groups) and 8 (state 18, game select)

### Type 7 (state 17)

Loader `FUN_08083f94` @0x08083f94, engine `FUN_08084e64` @0x08084e64,
subgame parser `FUN_08084624`, group-pick `FUN_080844bc`, group-id-list load
`FUN_0808459c`.

Header words: type discarded; subgameCount = loop bound (subgame offsets →
gctx+0x238[]); rounds→gctx+6 (round limit); `c`/flagC→gctx+8 (multi-target
mode, as §1); **earlyRounds→gctx+9 loaded but never read (unused in type
7)**; repeatOID→gctx+0xA; **x/w/v read and discarded**. PLL offsets:
start→+0x28, **round-end discarded**, finish→+0x2c, roundStart→+0x30,
**laterRoundStart→+0x34 loaded but never played**. **The 10 target scores
and 10 finish-PLLs are loaded but never read** — unlike state 14, type 7 has
no score cascade; game end plays the single finish PLL (+0x2c) at a *random*
index. [P]

Tail = subgame-groups table (gctx+0x718; count→gctx+4; per-group id-list
offsets→gctx+0x38[]). Semantics [P]: **one group = one round.**
`FUN_080844bc` picks a random unplayed group; `FUN_0808459c` loads its
game-id list (count→gctx+7, ids→gctx+0x446[], **1-based** → offset table
index id−1, matching tttool's `subtract 1`). The subgames of a group play
**sequentially in listed order** (cursor gctx+0x486). Group exhausted →
finish or next round.

Subgame `u0`–`u9` (parser `FUN_08084624`): identical scheme to §2, offsets
u0→+0x438 (correct-feedback index mode), u1→+0x439 (matcher gate, §8),
u2→+0x43a (wrong-feedback index mode), u3→+0x43b (OID-list-2 wrong-tap
limit), u4→+0x43c (OID-list-3 wrong-tap limit), u5→+0x43d **loaded never
read**, u6–u9 **discarded**. [P]

### Type 8 (state 18) — the game-select menu

Loader `FUN_08085ee4` @0x08085ee4, select logic `FUN_080864dc` @0x080864dc,
selectOID matcher `FUN_08086410`. ctx = physpool_alloc(0x40) (a small menu
context, no phase machine).

Header words: type→g8+0; subgameCount = loop bound; **rounds, `c`/flagC,
earlyRounds all discarded**; repeatOID→g8+0x20; **x/w/v discarded**. PLL
offsets: start→g8+0xc; **round-end/finish/roundStart/laterRoundStart all
discarded**. Subgame offsets: the loop overwrites g8+0x24 each iteration, so
**only the LAST subgame record is kept**. **The 10 target scores and 10
finish-PLLs are read and discarded.** [P]

Tail (all Proven): `gameSelectOIDs`→g8+4, `gameSelect` id-list→g8+0x18,
`gameSelectErrors1`→g8+0x14, `gameSelectErrors2`→g8+0x10.

`gameSelect` semantics [P]: a menu tap is matched against `gameSelectOIDs`
(`FUN_08086410`, match index → g8+10); the firmware then reads the id at that
position in the `gameSelect` list and writes **id − 1 into gamectx+0x131 —
the current game-record index** — then re-dispatches so the selected game
launches (same mechanism as script opcode 0xFD00). So `gameSelect` = "menu
OID *k* launches game record `gameSelect[k]`". [P]

- `gameSelectErrors1` (g8+0x14) = **"that game is locked"** feedback — played
  when the selected id is gated by a product-specific unlock flag (product
  0xC id 0x18, product 0x12 id 0xA, product 0x13 ids 0xB/0xC). [P] mechanism.
- `gameSelectErrors2` (g8+0x10) = **"valid game OID but not a menu choice"**
  — played when the tap is a known game OID (matched in the last subgame's
  OID list) that isn't one of the menu entries. [P] mechanism / [I] labels.

**Subgame `u0`–`u9` in type 8: the 10 header words are skipped entirely**
(`FUN_0808617c`) — the menu uses only the last subgame's OID list, not its
tuning words. [P]

Both types 7 and 8 are **script-hybrid** modes: non-control taps also route
through the GME play-script engine, and both treat OID 0xF3D as a deferred
play trigger (ctx+0xdd8).

---

## 6. Game type 16 (states 19 / 60) — extra OIDs + 3 extra playlist-lists

Type 16's defining mechanic: `gExtraOIDs` is the **subgame-selector OID list**
(1:1 with subgames) — the player *chooses* which subgame to play by tapping
its selector OID, instead of state 14's random draw. Two handlers:

### State 19 (generic) — loader `FUN_08087110` @0x08087110, engine `FUN_08087bf4` (EA 0x080880a0)

Header: type→ctx+0; subgameCount→ctx+4 (loop bound); rounds→ctx+6 (**# of
subgames to complete before the game ends**); `c`/flagC→ctx+8 (multi-target
"find all list-1 OIDs" flag, as §1); **earlyRounds→ctx+0xA stored but never
read**; repeatOID→ctx+0xC; **x/w/v read and discarded**. PLL offsets:
start→+0x14, finish→+0x1c, **roundEnd/roundStart/laterRoundStart stored but
unused**. **The 10 target scores and 10 finish-PLLs are read and discarded**
(state 19 has no scoring). [P]

Tail [P]:
- **`gExtraOIDs`** (→ctx+0x110, matcher `FUN_080873f8`) = subgame-selector
  OIDs; tapping selector *i* opens subgame *i*.
- **extra PLL #2** (→ctx+0x114) = **"subgame already completed"** feedback,
  played when a finished subgame's selector is re-tapped (done-flag ctx+0xf0).
- **extra PLLs #1 and #3 = read and discarded** (dead on this firmware).

Subgame `u0`–`u9` (parser `FUN_08087528`, stores u0–u4 → ctx+0xa8..0xac,
discards u5–u9): u0 = correct-feedback index mode; **u1 = ordered-targets
flag** (a list-1 hit is voided while any earlier-listed target is still
unfound — targets must be tapped in list order; matcher `FUN_08087858`,
mask ctx+0x118 — this is the clean, *working* form of the state-14 u1 gate,
see §8); u2 = wrong-feedback index mode; u3 = OID-list-2 wrong-tap limit;
u4 = OID-list-3 wrong-tap limit; **u5–u9 never read**. [P]

### State 60 (product 2) — loader `FUN_0808b7ac` @0x0808b7ac, engine `FUN_0808c3ac` (EA 0x0808c678)

Same record, **different game**: a fixed **8-question quiz** (constants 7/8
hardcoded). Header: type→ctx+1; subgameCount→ctx+2; **rounds→ctx+3 unused**;
**flagC & earlyRounds discarded**; repeatOID→ctx+0x138; **x/w/v discarded**.
Unlike state 19 it **uses** target scores + finish PLLs as score tiers:
correct-of-8 ≥ ts[0]→finishPLL 1, ≥ ts[1]→2, ≥ ts[2]→3, below→silent exit
(only ts[0..2]/FPL 1..3 used). roundEnd-PLL (ctx+0xac) = **right/wrong jingle
pair** (index 0 correct, 1 wrong). gExtraOIDs→ctx+0x13c = the 8 question
selector OIDs. Subgame `u0`–`u5` stored (ctx+0xe..0x13) but **no code reads
them**; u6–u9 discarded — each subgame is a single answer-tap against list 1,
with a per-subgame answered bit. [P]

---

## 7. Game types 9 (native product games) and 10 (extra playlist)

### Type 9 — the 75 extra playlist-lists

Type 9 dispatches to product-specific **native** game states (30, 32, 42–49,
64, 65), not the generic script engine. Each native state has its own
game-table walker (the `FUN_08034cbc` pattern: `fs_seek(h,0x10,0)` → offset →
skip to record index gctx+0x131). Proven by full read for product 9,
states 46–49 (`FUN_08054f54` @0x08054f54 and 0x08056890 / 0x080582ac /
0x0805a6c4); attributed by identical pattern for the rest. [P]

Header words for type 9: **only `subgameCount` survives** (→ctx+1); type is
discarded and `rounds`/`c`/`earlyRounds`/`repeatOID`/`x`/`w`/`v` are all
**read and discarded**. The 10 target scores and 10 finish-PLLs are
discarded. [P]

The **75 extra playlist-lists**: each native mode reads its own *fixed
consecutive slice* of the tail into named ctx slots (statically partitioned,
not a computed index) — e.g. state 46 takes the first 10 offsets →
ctx+0x38..0x5c; state 48 a longer run; state 49 four. At runtime each slot is
a **fixed-role cue bank**; the engine plays `FUN_080556b8(slotOffset, k)`
where the playlist index `k` is chosen by game logic (counter, score,
random). So the 75 PLLs are a **per-product-mode partitioned cue pool with
firmware-hardcoded slot roles** — the individual roles are product-specific
game internals, not a general format field. [P] (structure) / [O] (a full
per-slot catalogue across all ~30 native modes).

### Type 10 — the single extra playlist-list is NOT consumed

Type 10 runs in state 14. The loader `FUN_080774e0` stops after the 10
finish-PLL offsets, and **no function in the state-14 module, nor
`gme_oid_dispatch`/`gme_exec_command`, ever reads past that for type 10** —
the tttool `gExtraPlayLists` (1 entry) is **dead on this pen**. [P by
absence]. Type 10's actual distinction inside the shared engine `FUN_080785cc`
is behavioural: phase 0 skips the subgame-PLL1 announce, and phase 3 loops
forever instead of ending rounds (**endless mode**), with the round-complete
cue coming from subgame PLL 7. [P]

---

## 8. u1 disassembly check (subgame word +1)

Disassembly of the state-14 list-1 matcher `FUN_08078318` (0x08078318–
0x08078470, ARM) resolves the mangled Ghidra output. For each list-1 entry
that does **not** match the tapped OID, when `u1 != 0`:
- `p = gctx+0x249` (the *last-match index* left by the previous tap's u0
  feedback logic);
- `word = p >> 5`; `mask = gctx->avail[word]` (gctx+0x24c+4·word);
- tests `mask & (1 << shift)` where `shift` comes from a **stack slot that is
  only written *after* the loop** — i.e. it reads the residue of the previous
  invocation.
- If the test passes, a genuine hit is rewritten to a miss (gctx+0x21=0), so
  the tap falls through to the wrong-answer paths.

**Intended meaning (Inferred):** an **ordered / one-target-at-a-time**
answering mode — "reject a new list-1 hit while the previously matched
target's availability bit is still pending." The **clean, correct
implementation of exactly this idea is type 16's u1** (`FUN_08087858`, §6),
which uses a properly-maintained mask (ctx+0x118) — strong cross-confirmation
of the intent. In state 14 the state-14 code as compiled reads a stale stack
slot, so with `u1 != 0` the behaviour is residue-dependent (effectively a
firmware bug). **Practical takeaway for tttool:** u1 selects a strict
in-order matching mode; the safe/observed value is 0. [P] (mechanics) /
[I] (intent, corroborated by type 16).

---

## 9. Special-OID list ("Special symbols" `a`–`g`)

Header 0x94 → 20 × u16, read by `gme_parse_header` @0x08035d20 into fixed
globals (pointer table @0x080361d8..0x08036224 in the literal pool):

| word | community name | global | firmware use | evidence |
|---|---|---|---|---|
| 0 | `a` = Replay symbol | 0x081da716 | exempted (with `c`) from the tap classifier's content-tap marking; exported to embedded binaries via the system_api (0x080aa934) | [P] use / [I] "replay" role is content-side |
| 1 | `b` = Stop symbol | 0x081da728 | **only exported to embedded binaries** (system_api @`gme_launch_binary_build_sysapi` 0x080aa934); no native firmware read | [P] |
| 2 | `c` (wiki: "Skip symbol?") | 0x081da718 | treated **exactly like `a`**: `cover_oid_classifier` @0x08037cec — `if (OID != a && OID != c && f == 1) ctx[0x4a4] = 1`, i.e. `a` and `c` are the two control symbols that must **not** count as content taps (and hence are excluded from the replay recording, §below); also exported to binaries | **[P]** (mechanism; the "skip" label stays content-side [I]) |
| 3 | `d` | 0x081da72a | only exported to embedded binaries | [P] |
| 4 | `e` (wiki: "game mode OID?") | 0x081da72c | only exported to embedded binaries | [P] |
| 5–17 | `p` "padding?" | 0x081da72e..0x081da746 | **not padding**: all 13 words are part of the 20-word block whose *addresses* are handed to the embedded game binary in the system_api — a per-title parameter block for binary games | **[P]** (exported) / [O] (per-title semantics live in the shipped binaries) |
| 18 | `f` "0 or 1" | 0x081da714 | **replay-recording enable**: in `play_media` @0x080ab7b4, when in-game (mode byte 0xb) and the product is not one of the hardwired set {6,7,8,9,0xB,0xE,0xF}, media are pushed onto the replay ring (ctx+0x184/+0x314, count +0x182) only if `f == 1`. Also gates the `a`/`c` exemption above. Explains the wiki's "always 1 when g ≠ 0" (books with game/replay symbols need it) | **[P]** |
| 19 | `g` (wiki: "discover mode?") | 0x081da748 | only exported to embedded binaries | [P] |

So on the 2N-MT pen the native firmware consumes exactly three of the
twenty words (`a`, `c`, `f`); everything else exists for the embedded ARM
binaries, which receive pointers to the whole block through the system_api.

---

## 10. Scorecard

The community left **three families** of per-game unknowns
(`community-open-questions-resolved.md` §1 #12 + Still-open list). Result of
this pass:

**Games header words `c`/`i`/`q`/`x`/`w`/`v`** (per tttool `Types.hs`):

| slot | verdict | firmware meaning |
|---|---|---|
| `c` (gFlagC) | **Proven** | multi-target "find ALL list-1 targets" round flag (types 1/2/3/5/7/10/16) |
| `i` (gBonusTuningI, type 6) | **Proven** | type-6's slot of the `c` flag (find-all-targets) |
| `q` (gBonusTuningQ, type 6) | **Proven** | earlyRounds for the bonus stage (roundStart2 ↔ laterRoundStart2 switch) |
| `x` / `w` / `v` (gTuningX/W/V) | **Proven** | read-then-discarded authoring-tool metadata (dead in every handler) |

Plus, as a bonus, the previously "effect not pinned down" fields are now
firm: `gEarlyRounds` = round-start-PLL switch round; `gRepeatOID` =
replay-prompt control OID; and every type-specific tail
(`gSubgameGroups` type 7, `gGameSelect*` type 8, `gExtraOIDs` type 16,
`gBonus*` type 6) is decoded.

**Subgame words `u0`–`u9`** (ten per subgame): the community named **zero**.
We now name **six of ten** (u0–u5) with proven gameplay roles, and prove
u6–u9 are read-and-discarded on this pen:

| slot | verdict | meaning |
|---|---|---|
| `u0` | **Proven** | correct-feedback (PLL2) index mode (random / by-position / by-count) |
| `u1` | Proven (mechanics) / Inferred (intent) | strict in-order target-matching gate (clean form = type 16) |
| `u2` | **Proven** | wrong-feedback (PLL3/PLL8) index mode |
| `u3` | **Proven** | max wrong taps on OID-list 2 (decoys) → give-up |
| `u4` | **Proven** | max wrong taps on OID-list 3 (other-game OIDs) → give-up |
| `u5` | **Proven** | stored, never read on this pen (dead) |
| `u6`–`u9` | **Proven** | read-and-discarded (dead) |

Also newly named: the three subgame OID lists (target / decoy / other) and
all 8 used subgame playlist-list slots (PLL9 unused).

**Special-symbols `c`–`g`** (§9): of the 20-word list, the native firmware
consumes exactly `a`, `c`, `f`. **Proven**: `c` = second control-symbol
exemption (like `a`), `f` = replay-recording enable (settles the wiki's
"0 or 1, always 1 when g≠0"). `b`/`d`/`e`/`g` and the 13 "padding" words are
**Proven exported** to embedded game binaries via the system_api — not
padding, but a per-title parameter block whose semantics live in the shipped
ARM blobs (**Still-open**, needs the binaries).

**Genuinely Still-open [O]:**
- Type-9's 75 extra playlist-lists: structure proven (statically-partitioned
  per-product-mode cue pool), but a full per-slot role catalogue across all
  ~30 native modes is product-internal, not a format field.
- Special-symbols `b`/`d`/`e`/`g`/`p*` per-title meaning (lives in embedded
  binaries we don't run).
- `u1`'s exact intended behaviour in state 14 is inferred (the state-14
  compilation reads a stale stack slot — a firmware bug — but type 16's clean
  twin corroborates "in-order matching").

Net: of the community's unnamed per-game parameter slots we now give firmware
meaning to **`c`, `i`, `q`, `x`, `w`, `v`** (all six Games words) and **u0–u5
plus the dead-status of u6–u9** (all ten Subgame words accounted for) and
**`a`/`c`/`f`** of the Special-symbols — closing
`community-open-questions-resolved.md` item #12 from **[O]** to **Proven**,
with only the embedded-binary and native-type-9-cue-pool internals remaining
genuinely open.

---

## Appendix — per-type handler address quick-reference

| type | state | loader | engine | subgame parser |
|---|---|---|---|---|
| 1,2,3,5,10 | 14 | `FUN_080774e0` | `FUN_080785cc` | `FUN_08077a8c` |
| 4/40 | 15 | `FUN_080774e0` (shared) | `FUN_08080988` | `FUN_08080264` |
| 6 | 16 | `FUN_08081d18` | `FUN_08082bf4` | `FUN_080823a0` |
| 7 | 17 | `FUN_08083f94` | `FUN_08084e64` | `FUN_08084624` |
| 8 | 18 | `FUN_08085ee4` | `FUN_080864dc` | (last subgame only, `FUN_0808617c`) |
| 16 | 19 | `FUN_08087110` | `FUN_08087bf4` | `FUN_08087528` |
| 16 (prod 2) | 60 | `FUN_0808b7ac` | `FUN_0808c3ac` | `FUN_0808bc80` |
| 9 (prod 9) | 46–49 | `FUN_08054f54` (+3) | 0x08055864 | — |
| special-OIDs | — | `gme_parse_header` @0x08035d20 | — | — |


---

## Addendum (2026-07-20, naming-campaign session 2 — interior helper clusters named)

New facts surfaced while naming the per-state helper functions (all decomp-read, base
0x08009000; names now live in the decomp):

- **Types 7/8 use the additional media table (hdr 0x60) for phase cues [P].** The state-17/18
  cue players `game17_play_phase_cue` @0x08083c14 / `game18_play_phase_cue` @0x08085c64 resolve
  *every* playlist entry against **both** media tables — main `hdr+0x04` and additional
  `hdr+0x60` — into two parallel (offset,size) arrays, then play the main-table entry. No other
  script game engine touches the hdr+0x60 bank in its cue path (game16/19/60/wronglimit/quiz/
  findtarget cue players are single-table).
- **Type 4/40 (state 15) chains into game record 12 [P].** `game15_engine` phase 0xBB runs
  `game15_teardown` @0x08080910, then sets `akoidpara->game_index := 0x0C` and re-enters
  `game15_entry` — a **hardwired record number**; the engine also special-cases records 3/0x0B/
  0x0C (`akoidpara+0x131` compares). The memory-sequence game is welded to specific game-table
  slots of its product.
- **Reading game OID aliasing [P, decomp-only].** `game_oid_alias_equal` @0x0806a54c treats a
  tapped OID as matching iff equal after translating between two parallel OID ranges based at
  **0x15EB** and **0x1609** (delta 0x1E) — two printings/areas of the same 30 buttons.
- **game60 (prod-2 quiz) per-question answer bitmasks [P].** `game60_match_answer_oids`
  @0x0808c0fc keeps a u32 bitmask per question (`ctx+0x20[q]`): repeat answers to the same
  question yield result 2 (ignored for score).
- **Common helper shape per state.** Every game state cluster repeats the same four helpers,
  now uniformly named: `gameNN_ctx_reset` (clear ctx + roll the game seed
  `rng_below(akoidpara+0xcc)`), `gameNN_match_{target,decoy,other}_oids` (u16 OID-list scans
  vs the tapped OID `akoidpara+4`), `gameNN_play_phase_cue` (playlist-list cue player, entry 0
  immediately + rest at audio boundaries), plus per-state pickers (`*_pick_unplayed_*` =
  seed-modulo + played-bitmask walk).
- **Type 253 (state 31) is NOT empty [P].** `game31_engine` @0x080aa498 drives the
  `altbook_*` cluster below - the "empty record" note only covers its game-table record.
- **Alt book mode ("altbook") control OIDs [P, semantics partly open].** The
  `gme_oid_dispatch_alt` cluster is now named `altbook_*` and is exercised by
  `game31_engine` (GME type 253, state 31): control OIDs **0x899/0x89A**
  (double-tap counted → reload playlist offsets / teardown + fresh 0x240 game ctx),
  **0x89B** (hard audio stop), **0x89C** (load the pending 32-entry OID list at ctx+0x56),
  **0x89D** (reset/replay via the default list). An OID list is played one entry per tap
  (`altbook_play_next_in_list`), each entry's cue resolved from a u16 media-index table at
  `(hdr@0x10)+0x0A` — see OPEN-QUESTIONS for what product/mode this serves.

---

## Addendum (2026-07-20, naming-campaign session 3) — game types 17–23 decoded

The seven remaining dispatched types were previously completely unstudied ("their behaviour
has not been studied yet" in GME-Format.md). Their handler states 58/59/61/62/63/66/68 are now
fully named (`game58_*` … `game68_*`, one cluster per state — the campaign names by STATE
number, matching `game14`…`game60`). All facts below are **[P] decomp** unless tagged; none
are yet empirically verified on hardware/tt-emu (no type-17–23 GME on disk).

**Shared mechanics** (all seven): same record framing as §1 (9 header words, 5 game PLLs,
subgame offsets; subgames = 10 u16 words + 3 OID lists + 8/9 PLL offsets). All are
**script-hybrid** like types 7/8: a content tap first runs its normal play script, THEN the
game logic; all honour `gRepeatOID` (replay the current phase cue) and post 0x100c (pop back
to book mode) when done. One global cue machinery is shared by every game state: the cue
players (per-state twins of `game_play_phase_cue`) resolve the WHOLE selected playlist into
(offset,size)[32] arrays @0x081dac84/0x081dad04, play entry 0 now and the rest one-per-audio-
boundary (pending count @0x081dac7c); states 66/68 use the shared `game_play_phase_cue`
itself. States 59/61/62/63 resolve every entry against
**both** media tables (hdr+0x04 and hdr+0x60); the additional-table copy is played on the
**second consecutive repeat-OID tap** — an alternate (presumably slower/simpler) recording
[P mechanism / I intent].

### Type 17 (state 58) — sequential lesson steps
Subgames play **in listed order** (no random draw, no scoring, no score cascade — targets
scores/finish PLLs not even loaded). Each subgame: play PLL1 (whole playlist = the lesson),
then wait; the FIRST subgame accepts only the HARDWIRED two-stage control sequence **OID
0x11EA (twice), then 0x11EB** (each hit → PLL2 idx 0; the third → random PLL7 → next subgame);
later subgames accept the subgame's list-1 targets (hit → random PLL7 → next). Wrong taps:
list-3 vs `u4` → PLL6 hints then PLL8 give-up (advances anyway); unknown OIDs → PLL4. After
the last subgame: random finish PLL, exit. Only `u4` of the subgame words is consumed; list-2
positions are computed but never matched. The 0x11EA/0x11EB weld pins this type to one
specific book [P weld / O which product].

### Type 18 (state 59) — scored quiz, silent retries
The straightforward one: `rounds` rounds, each a random unplayed subgame (32 max), PLL1
question, one list-1 hit = +1 score → random PLL7. The twist: **wrong taps give NO feedback
cue** — a tap in list-2 (decoys) is silently counted vs `u3` and only the give-up (random
PLL8, round lost) is voiced; a tap in list-3 but not list-2 is silently ignored (uncounted);
unknown OIDs → random PLL4. earlyRounds round announcements as §1. Ends with
the full **10-tier score cascade** (score ≥ ts[k] → random pick of finish PLL k+1). Only game
type that uses all ten tiers with random finish-cue picks.

### Type 19 (state 61) — beat-the-clock quiz (uses subgame PLL 9!)
Per-subgame countdown: subgame word 6 (`u5`) × 10 heartbeat ticks (≈ u5 seconds; 0 → ~5 s).
While the clock runs the engine plays the **roundEnd game-PLL as a once-per-second tick cue**.
Correct in time → random PLL2, +1 score; list-3 wrong → PLL6 hint and the **clock resets**;
unknown → PLL4 **index 1** (clock keeps running); time up → next tap plays **subgame PLL 9**
("time's up" — the only reader of PLL9 in the firmware) or PLL4, round lost. Ten-tier score
cascade at the end. **WELD:** a top-tier win when product == 0xC and game record == 0x16 sets
`akoidpara->menu_unlock_dcf` — which `game18_select_handler` checks to un-lock a locked
type-8 menu entry. So product 0xC ("cover 0xF3F") ships type 19, and winning it unlocks a
menu game [P mechanism].

### Type 20 (state 62) — find N targets (single round)
One random subgame per session ( `rounds` is loaded but never compared). List-1 is a find-all
set (availability bitmask, repeat taps detected); **`u5` = the number of successful target
taps that completes the game** (fresh AND repeat finds both count) → random FINISH game-PLL →
exit. Below u5: random PLL2 per find. Wrong: list-3 vs `u4` → PLL6/PLL8; unknown → PLL4. No
scoring. This is the second live use of `u5` (after none in types 1–16 — u5 was "stored,
never read" everywhere else).

### Type 21 (state 63) — category ladder quiz (hardwired 4×{3,4,3,3} bands)
Exactly **3 rounds**; each round randomly picks one of **four hardwired subgame bands** —
table slices 0–2, 3–6, 7–9, 10–12 (so the record MUST have 13 subgames) — and announces it by
playing the **roundEnd game-PLL at playlist index = band #** (a per-category intro bank). The
band's subgames then play in random order, one list-1 hit each (random PLL2); ≥3 correct in
the band → random "round success" cue, else "round fail" — these two cues are the **first two
finish-PLL slots**, the ten target scores and the other eight finish PLLs are discarded.
Wrong taps: list-3 vs `u4` → PLL6 retry / random PLL8 skips the subgame; unknown → PLL4.
After round 3: random finish game-PLL, exit. A content tap while the question is playing cuts
it short and is evaluated immediately.

### Type 22 (state 66) — scavenger hunt (pre-drawn task list + inactivity timeout)
At entry the pen pre-draws `rounds` random subgames and reads out **all their announcements
in sequence** (each drawn subgame's PLL1); the task set = the **first list-1 OID of each
drawn subgame**. The player then hunts them in any order: new find → PLL2 +1 score; already
found → PLL5; list-2/list-3 wrongs → u3/u4-limited PLL3/PLL6 then PLL8 (ends the hunt);
unknown → PLL4. **~20 s of inactivity** (200 heartbeat ticks; timer paused while audio plays)
→ the roundEnd game-PLL as "time's up" → scoring. Scoring = ten-tier cascade (finish PLL idx
0); a **top-tier win sets `akoidpara->menu_unlock_dde`** (products ≠ 0x13) — read by
`game18_entry`/`game18_select_handler` for product 0x12: the second menu-unlock weld.
Product 0x13 instead gets a random-index finish PLL1 and a reminder cue every second of the
timeout window. OID welds 0x1A2B→0x1A2A, 0x1A1F→0x1A20 (adjacent print areas of one target)
pin the type-22 host books to products 0x12/0x13 (covers 0x18A2/0x1A32) [P mechanism / I
products]. NOTE (firmware quirk): the feedback PLLs 2..8 come from the LAST pre-drawn
subgame's record — per-task subgames are not re-parsed during the hunt. Its EA also calls
state-63's `game63_actionq_drain` (shared global cue arrays make this safe).

### Type 23 (state 68) — find-everything percentage game
Single round, one random subgame (no played-mask — repeated launches may repeat the page).
Find-all over list-1 with an alias table: `game68_oid_alias_map` HARDWIRES per-subgame OID
merge groups in 0x1B54–0x1B70 (several printed areas = one target; subgame 0 additionally
caps the hunt at 6 finds). New find → PLL2; already → PLL5; **any list-3 tap ends the round**
(PLL6); unknown → PLL4. Result: 100 % (or the cap) → the game finish PLL; otherwise **fixed
percentage tiers** — >75 % → finish PLL1, 50–75 % → finish PLL2, <50 % → finish PLL3 (the ten
target scores are loaded but never read; finish PLLs 4–10 dead). Keeps a "Repeat Addr" debug
print (the string that once mis-named its list-3 matcher `study_repeat_addr`).

### Subgame-word scorecard update (types 17–23)

| word | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
|---|---|---|---|---|---|---|---|
| u0–u2 | stored, unread | stored, unread | stored, unread | stored, unread | stored, unread | stored, unread | stored, unread |
| u3 (list-2 limit) | unread | **read** (silent) | unread | unread | unread | **read** | unread |
| u4 (list-3 limit) | **read** | unread | **read** | **read** | **read** | **read** | unread |
| u5 | unread | unread | **TIME LIMIT ×10 ticks** | **TAPS TO COMPLETE** | unread | unread | unread |

So `u5` — dead in every type ≤16 — is live in types 19 (time limit) and 20 (completion
count). The u0/u2 feedback-index modes are ignored by all seven (they use fixed idx-0 or
random picks).

### Corpus / product attribution
GME-Format.md already records that "a few published products carry such records" for types
17–23; a per-type title tally was not preserved on this machine (the 205-GME corpus itself is
not on disk) — **[O]**. From the firmware welds alone: type 19 ↔ product 0xC, type 22 ↔
products 0x12/0x13, type 17 ↔ the book containing OIDs 0x11EA/0x11EB, type 23 ↔ the book
with OID range 0x1B54–0x1B70, type 21 ↔ a 13-subgame record [P welds / I attribution].

### Handler address quick-reference (types 17–23)

| type | state | entry | loader | subgame parser | phase engine | engine (EA) |
|---|---|---|---|---|---|---|
| 17 | 58 | 0x080894fc | 0x08088910 | 0x08088b70 | 0x08089244 | 0x08089558 |
| 18 | 59 | 0x0808adb0 | 0x08089df8 | 0x0808a2ec | 0x0808aa44 | 0x0808ae10 |
| 19 | 61 | 0x0807a8e4 | 0x0807992c | 0x08079e4c | 0x0807a530 | 0x0807a940 |
| 20 | 62 | 0x0807bd68 | 0x0807b240 | 0x0807b50c | 0x0807bc10 | 0x0807bdc8 |
| 21 | 63 | 0x0807d440 | 0x0807c718 | 0x0807cbd0 | 0x0807d248 | 0x0807d888 |
| 22 | 66 | 0x0807ebe0 | 0x0807dd50 | 0x0807e2fc | 0x0807e8ac | 0x0807ecac |
| 23 | 68 | 0x0807fcbc | 0x0807f004 | 0x0807f4d0 | 0x0807faec | 0x0807fd5c |
