# The PS2 side of David's archive - what transfers to Dreamcast, and why the menu walk does not

`[RESEARCH 2026-09-18]` read-only, on the 444 .p2m + the converter + David's docs (no PS2 states or pcsx2 source exist on this machine). Written to answer the user's "can we have an agent think about the ps2 side of things?" while the CSS tour (docs/tour-css.md, TEST-PLAN §7) authored a Dhalsim base on DC. Decisions it feeds: the finding step's match claims, and whether the archive's combos can be re-anchored (yes, in-match, with his setup rows) or need a tick-level translation (unknown until measured).


Scope: the 444 `.p2m` movies under `/home/nbee/dev/davids_fly/0915/flycast-rr/mvc2_data/`, the converter
(`mvc2_data/converter/{parse_p2m,p2m_to_flycast,scan_states}.py`), `_conversion_report.txt`, the pcsx2-rr
reference notes (`/home/nbee/dev/davids_fly/pcsx2_reference/*.md`), and the DC-side facts in
`mcp/charselect.py`, `CANON_onenter.md`, `core/dojo/mvc2.cpp`, `mvc2_data/SPREADSHEET.json`.
No PS2 savestates (`4D228733.*`) and no pcsx2 source exist on this machine (`find / -iname '4D228733*'` = 0 hits;
the states live only under `C:\Users\davil\OneDrive\MVC2_CORE\Old_Replays`, `p2m_to_flycast.py:35`).
Every number below was measured on the files in the tree; inferences are labelled as such.

---

## 0. The one finding that changes everything else: the converter's frame 0 is PS2 frame 502

`parse_p2m.py:112-119` (`_detect_origin`) locates the pad grid by walking back from EOF in 12-byte steps while
the 8 analog columns read 0x7E/0x7F/0x80, and calls the first frame that fails "the header". For
`Combo_Dhalsim97_pcsx2.p2m` that yields origin **6040** (`converter/README.md`: "6032 bytes in the sample =>
grid at 6040"). The real layout, measured on all 444 files:

| offset | content | evidence |
|---|---|---|
| 0..3 | u32 FrameMax (6707 for Dhalsim97) | `parse_p2m.py:107` |
| 4..7 | u32 Rerecs (1646) | `parse_p2m.py:108` |
| 8..15 | **8-byte timestamp `{0, sec, min, hour, 0, day, month, year}`** — Dhalsim97 = `00 0b 06 01 00 01 03 12` = 2018-03-01 01:06:11 (file mtime Mar 21 2018) | decodes as a sane date/time in **444/444** files (0 sanity failures; years 2014:84, 2015:148, 2016:69, 2017:52, 2018:27, 2019:6, 2020:57, 2023:1) |
| **16..EOF** | the 12-byte/frame grid, `[port A 6 bytes][port B 6 bytes]` | `(size-16) % 12 == 0` in **444/444** files; `(6040-16)/12 = 502` exactly |

What sits in true frames 0..501 (the part the converter drops) is the **PS2 BIOS/pad-init chatter**, identical in
every file: true frame 150 = `03 02 00 02 01 00 | 03 02 00 02 01 00` (a DualShock config-mode response) in
**444/444** files; frames 159..~490 read `ff ff 00 00 00 00` (digital-mode pad, analog bytes 0), frame 501 =
`ff ff 7f 7f 7f 7f | 00 00 00 00 00 00`, and from 502 on both pads are neutral `ff ff 7f 7f 7f 7f`. The
walk-back stops at 502 because frame 501's port-B analog bytes are 0x00 — so the heuristic lands on the same
wrong origin in **438/444** files (the other 6 stop even later, see §2). Frame 0 itself is `00…00`
("every button pressed" in active-low) in 416 files and neutral in 25 — the SIO buffer before the first real
poll; the game is still in the BIOS then, so it is inert.

Consequences (all verified on Dhalsim97):
1. **Every converted row index is PS2 `g_FrameCount − 502`.** His `beforecombo` marker "4212" is PS2 frame 4714;
   `_end` 5699 is 6201; `_default` 3172 is 3674. The macro and the `# savestates` header are internally
   consistent (scan_states matches from the same origin, `scan_states.py:59-62`), so nothing on our side is
   *mis-aligned with itself* — but "from power-on" playback of the macro starts 502 PS2 frames (8.4 s) late
   relative to a real PS2 boot, and ~12 files lose a Start press that David made at frame ≤501.
2. **The last 502 emitted rows are past the movie's end.** The converter emits `min(FrameMax, physical)` frames
   from the wrong origin (`p2m_to_flycast.py:98, 211-215`), i.e. true frames 502..FrameMax+501. For Dhalsim97 the
   stale zone is parser rows 6205..6706, and it contains the **P2 `l1` hold at 6663..6702** (true 7165..7204 >
   FrameMax 6707) — which is exactly what the header's `# combo start (est): frame 6663` points at
   (`p2m_to_flycast.py:113`). That estimate is garbage from an abandoned take; **26 of 436** estimates in
   `_conversion_report.txt` land in that stale zone.
3. **"122 files with FrameMax > stored frames" is an artifact.** 116 of them differ by 3..181 frames (< 502): with
   the true origin the file holds every frame. Only **2** clips are genuinely truncated: `Ataraxia
   Replays/Combo_Sentinel37_pcsx2/Combo_Sentinel37_2_SS/BC.p2m` (FrameMax 8803, 6382 stored — its slot .002/.003
   states resolve to "frame 5880" = the file's end, so the tail incl. part of the combo is gone) and
   `Combo_Magneto_Bison51_pcsx2_SS/Combo_Magneto_Bison52.p2m` (7562 vs 7488, last 74 frames missing).
4. `pcsx2_reference/movie_file_and_rerecord.md:116-125` describes the 0.9.6 `.p2m` as "8-byte header + 6 bytes/frame,
   pad 1 only" (from the 1.4.0-rr converter comment). David's binary writes a different 2P variant: 16-byte header,
   12 bytes/frame. The converter README already calls it "a richer 2-player variant"; the reference note should
   not be used for these files.

**Fix (not applied — read-only task):** `P2M(path, origin=16)` (drop the heuristic; the header is fixed-size 16).
That makes row N == PS2 frame N, removes the 502 stale rows, repairs the 6 broken clips (§2), collapses the
"truncated" list to 2, and the `--mark-states` frames become the states' own `g_FrameCount`.

---

## 1. Why the from-power-on walk diverges

### (a) Port lanes
`parse_p2m.py:75-76` slices `raw[p*6:(p+1)*6]` for `p` in {0,1}; `p2m_to_flycast.py:65-70` writes port 0 with
`FUNC_TO_LETTER[0]` = **P1 letters (WSADZXCVBNM)** and port 1 with `FUNC_TO_LETTER[1]` = **P2 letters
(TGFHUIOJKLP)** (`:46-51`). So the first 6 bytes → our P1 lane, the second 6 → P2. Whether the first 6 bytes are
pcsx2 *controller port 1* rests on the 1.4.0-rr block layout `HEADER + frame*12 + 6*port + bufIndex`
(`movie_file_and_rerecord.md:24-25`) and on the README's "verified lossless" round-trip — nobody has checked it
against a PS2 savestate. Evidence inside the corpus that it is right (not proof):
- In Dhalsim97 the combo window 4212..5699 has **634 first-lane rows vs 20 second-lane rows**; the human in a
  MvC2 combo clip is P1. Across the corpus the first lane is the more active one in 327/444 files.
- The boot/menu driver is NOT a lane signal: the second lane does the Start-mashing in Dhalsim97 (24 Start taps
  at rows 84..653 = PS2 586..1155, first-lane silent until 1884), but it is the *first* lane in 307 files, the
  second in 123, tie 14 — and within the 17-clip Dhalsim Revisited set alone it is P1 in 11 and P2 in 6. That is
  a habit (one human, two pads), not a port swap.
So: the converter did **not** swap lanes; the pre-match rows are on P2 because David walked the PS2 menus with
the P2 pad in this clip, then picked P2's team (6 `square` presses at 1404..1762 = 3 × (select + assist-type)),
then the P1 pad picked P1's team (2022..2541), then the P1 pad executed the combo.

### (b) The two boot/menu flows are different machines
Measured PS2 structure (Dhalsim97, converter rows; add 502 for PS2 frames):
- 84..653: 24 Start taps (P2). 683..780: `right`×5, `left`×1 (net +4), 792: `x` (LK) — a horizontal menu pick.
  927 `down`, 973 `x` — a second screen. 1076..1762: char-select cursor walk + 6 picks (P2). 1884..2548: P1's
  walk + 7 presses. 2542..2550: P2 `x` (ready). **Nothing** from 2551 to 3536.
- `_default` = row 3172 = **622 rows after the last pre-match press**. The same gap is 612/623/627/612/604/615 in
  the other Dhalsim Revisited clips that have both a `_default` state and a clean pre-match tail (Boat2, Bridge2,
  Carnival1, Clock1, Desert1, Desert2, Factory) — so `_default` is David's "fight start / control begins" anchor,
  saved ~10.2 s after the last confirm (stage load + intro), by hand (±12 frames).
- 3537..3549: P1 `x+square` = LK+LP (a partner-1 tag; David's own name for the state at 3551 is `at2CharTag`).
  3630..3650: seven 1-frame `l1` taps (A1 assist calls). 3679..3692: `d, df, f, f+x+circle` = QCF+KK, and
  3725..3731 QCB+KK — **at least one hyper 514 rows after `_default`**, so his base had meter available (Training
  meter option or pre-filled; VS mode starts at 0 — which one is not decidable from pads).
  3855..3992: `right` held 137 frames (walk). 4212: `beforecombo`. 4387..4399 P2 `left`, 4433..4438 P2 `right`,
  **4541 P2 `up`** (the dummy jumps), 4546 P1 `up+square+triangle` = first attack of the combo. 5399..5405 P1 `l2`
  (A2 — the *second* assist is used inside the combo). 5571 last P1 input; 5699 `_end`.
DC structure (`CANON_onenter.md:3-5`, David's own fastVS): Start ×3 at **119/210/345**, `Right`×2 + Start×3 at
~457-464, and the handoff at 633 lands on **STAGE SELECT** ("the first real choice") for VS. So on the DC the main
menu is reached by ~457 and a net +4 `right` + confirm at 792 selects a different item than on the PS2 (the DC
menu order is not in the tree; the user's measurement says it is Training). Then his char-select rows start at
1076 (P2) / 1884 (P1) against a screen the DC reached on its own schedule — the picks are press-edge, so what
matters is *which screen is up and where the cursor sits* when each row fires, not ±1 frame.
Where the geometry *does* seem to agree: with the DC grid and start cells (`charselect.py:26-27`: P1 starts on
RubyHeart (0,0), P2 on Cable (0,1); `GRID` at `:30-39`), P1's first rows `down` (1884), `left`×3 (1893..1924),
`square` (2022) read RubyHeart → Hayato → Anakaris → Jin → **Dhalsim** — the clip's character. That is one data
point that the PS2 char-select grid and P1 start cell equal the DC's (inference, one sample). The DC run landing
on "Sonson-ish" (Sonson = one `up` from RubyHeart) is consistent with the DC char-select becoming active at a
different frame than his rows assume, so an early row was eaten and the rest landed off-by-one — a timing
divergence of the *screen*, not the grid.
What is at PS2 frame 3172 (3674 true): the fight-start anchor (above). It is in-match because David's convention
is slot 0 = the clean start he returns to, not power-on (17 `_default` states in the corpus, all in the Dhalsim
Revisited set; 8 `_at2CharTag`, 8 `_beforecombo`).

### (c) Frame rate
Both formats are **one row per VSync**: pcsx2-rr writes each pad byte at `HEADER + g_FrameCount*12 + …` and "if a
game polls twice in one frame the second poll overwrites the same 12 bytes" (`movie_file_and_rerecord.md:42-45`);
flycast records per maple DMA per emulated frame into `session_inputs[frame_number]` (CLAUDE.md, Determinism).
59.94 vs 60 Hz therefore changes only wall time (4212 rows = 70.27 s vs 70.20 s); a frame-indexed movie has **no
accumulating drift** and no "1-frame drift over 4212 frames" exists to worry about. Menus are press-edge with
6-12-frame holds (see the log), so ±1 frame is irrelevant there; combos are frame-exact, but a *constant* offset
of the whole window is harmless once the game state is anchored — only per-frame divergence in game logic
matters. The real frame-rate question is **logic ticks per VSync**: MvC2's turbo skip (`SKIP_RATE/SKIP_COUNT/
SKIP_TOGGLE` at `0x8C289620/21/22`, `mvc2.cpp:26-28`; the count "counts down; at 0 resets to rate and the game
runs an extra logic frame", `mvc2.h:35`) means an input row can cover two logic ticks on the reset frame. Whether
the PS2 port runs the same skip cycle at the same rate is **not verifiable from this tree** (no PS2 emulation, no
PS2 RAM map). It is testable on our side (§5).

---

## 2. The 444 corpus

- **Provenance:** 444 `.p2m`, 0 errors, 426 folders `_SS` (states beside the movie), 9 `_duplicate`
  (`_conversion_report.txt:4`). **All 444 are PS2** (pcsx2-rr 2P `.p2m`); there are **0 DC-native** clips here —
  the archive's `_demul` folders hold Demul CE `.txt` and were skipped and not copied (`README_flycast.md`,
  "Behavior on the real archive"; `find -iname '*demul*'` = 0 hits). Top dirs: Chaos Dimension 353, Ataraxia 66,
  Dhalsim Revisited 25. Leaf kinds: Combo 249, Situation 60, Reset 32, NoDamage 18, Versus 14, TestIdea 13,
  SentinelInfinite 8, FBC 7. **157 dirs sit under `Chaos Dimension Replays/NG/`** (likely rejected takes — the
  label's meaning is not documented anywhere in the tree).
- **Grades:** `_OK` appears on **24 dirs, all in Dhalsim Revisited** = the 17-stage set (01_Abyss … 17_Training,
  where "Training" is the *stage* name — the folder prefixes are exactly the `Stage_Selector` enum in
  `SPREADSHEET.json`: 0 Boat1, 1 Desert1, 2 Factory, 3 Carnival1, 4 Bridge1, 5 Cave2, 6 Clock2, 7 Raft2, 8 Abyss,
  9 Boat2, 10 Desert2, 11 Training, 12 Carnival2, 13 Bridge2, 14 Cave1), 7 of which have a non-`_SS` twin.
  The number in a folder name is very likely the hit count (`Combo_Rogue&WarMachine&Magneto420HitCombo`), so
  **Dhalsim97 = 97 hits** — an oracle for our replays (§5).
- **Characters** (folder-name tokens): Magneto 52, Cyclops 31, Sentinel 27, Storm 27, Spiral 27, Strider 21,
  Cable 20, Dhalsim 17, Doom 15, RubyHeart 15, Psylocke 15, Servbot 12, SpiderMan 11, Blackheart 11, Akuma 11,
  Cammy 10, Charlie 10, Bison 9, Ryu 9, IronMan 9, Jill 9, …
- **Stored vs FrameMax:** with the true origin **442/444 store the full movie**; 2 are truncated (§0.3). 322 files
  carry a stale tail past FrameMax (physical > FrameMax; Dhalsim97: 47,224 stored vs 6,707 = 40,517 stale frames
  of earlier, longer takes — rerecs 1646), and 82 of those have non-neutral inputs within 2000 frames past
  FrameMax. FrameMax min/median/max = 4,257 / 15,046 / 819,314; rerecords 1 / 791 / 6,246.
- **Broken conversions (6 files, the non-6040 origins):** `NG/Combo_Cammy&Charlie50_pcsx2_SS` and
  `NG/Combo_Cammy65_pcsx2_SS` (origin 162,676 = true frame 13,555 > FrameMax 11,279/10,933): the emitted macro is
  **entirely stale tail** (952/839 active rows of garbage; the true movie has 2,629/1,730). `Combo_Venom&Doom30`
  (origin at true 29,547 of 34,248 — David used the analog stick at 29,040..29,546, which the heuristic treats
  as "header"): the macro holds only the last 4,701 frames. `NG/…Rogue&WarMachine&Magneto420` and
  `…Rogue&Magneto&WarMachine999` (true 5,459 dropped) and `Combo_Cable&Magneto37` (115,164 dropped) are partial.
  These are also the 3 "no Start press" files and 4 of the 6 "diff > 502" files — one root cause.
- **Markers:** 426 macros have a `# savestates` block; 367 clips got a state-derived `CLIP LIKELY BEGINS`, 77 a
  state-derived END too; 77 bracketed windows are 130..23,352 rows long (median 2,310). 201 slot lines across 65
  clips read `frame ? (not recovered)`. Name-hinted slots: `default` 17, `beforecombo` 8, `at2chartag` 8,
  `colorchange` 4, `start` 3; 1,051 slots are plain `.NNN` (slot 0 = START by convention).
- **In-match starts: none.** Every correctly parsed clip has Start presses and menu rows (the 3 without are the
  garbage conversions above). First input: min 0, median 480 rows, max 12,908 (`Combo_OmegaRed&Sentinel34`: 215 s
  of silence and then `left+start`, Start mashing, `right`, `x` — still a menu walk). 176 clips have their first
  input after row 600, 28 after 2,500 — all still walk menus.

---

## 3. What CAN transfer, and what must match

The combo is an **input stream anchored to a game state**; the only fair transfer is the window (4212..5699 for
Dhalsim97, plus the P2 dummy rows inside it — the dummy `up` at 4541 sets up the air combo) replayed on a DC base
whose relevant state equals his. Nothing in the boot/menu rows is worth translating (see §1b/§5). What must match
at the anchor, and where each fact can come from:

| fact | why it matters here | readable on OUR base (SPREADSHEET.json / mvc2.cpp) | knowable for HIS base (no states) |
|---|---|---|---|
| characters, team ORDER, point char | tag at 3537 → the 2nd char is on point at 4212; `l1` (A1) ×7 before, `l2` (A2) inside the window | `ID_2` per slot (`charselect.py ID2`), `Is_Point` (+0x411) | folder names; decode his pick rows through `charselect.py GRID` (P1's first pick = Dhalsim, §1b) — needs the PS2 grid = DC grid assumption; ask David |
| assist types (α/β/γ) | A2 is called at 5399 | not in the spreadsheet by that name (check `Assist_Type`-like rows in the CT) | the button used at each pick (`square` = one fixed type for all 6 of his picks) — mapping button→type unverified |
| stage | one combo per stage is the video's design; geometry differences are unverified (Abyss has 3 phases: `Abyss_Stage_Change` 0x2C26A8C8) | `Stage_Selector` 0x2C26A95C | folder name (Boat2 = 9) |
| health, both sides | damage scaling / kill timing | `Health_Big/Small` 0x2C268760/64 (+0x420/0x424 per slot) | assumed full (144/144) at `_default`; unknown at 4212 (a hyper landed before it?) |
| super meter | ≥1 hyper before the window, hypers inside | `Meter_Big` 0x2C28964A/B, `Meter_Small` 0x2C28966C/E | only that it was ≥1 bar at row 3686; mode/option unknown |
| positions | `X_Position_From_Enemy` at 4212 after a 137-frame walk | `X/Y_Position_Arena` +0x34/+0x38, `X_Position_From_Enemy` +0x298, `Camera_X` 0x2C26A56C | **not** readable; only reproducible by replaying his setup rows from fight start |
| damage scaling / dizzy / RNG | `Damage_Modifier` +0x205, `Throw_RNG` +0x25E, `Dizzy_Reset_Timer` | readable | unknown |
| skip PHASE at window start | a row straddles 2 logic ticks on the count-reset frame | `Frame_Skip_Rate/Counter/Cycle_Value/Toggle` 0x2C289620/21/00/22 (`peekSkip`, `mvc2.cpp:96-103`) | unknown; if `Frame_Skip_Cycle_Value` counts "since match start" on both ports, phase = (rows since fight start) mod rate — derivable once the fight-start row is pinned |
| mode (VS / Training) and its options | meter, dummy behaviour, health regen | n/a (we author it) | not decidable from pads; the P2 pad participates in both |
| palettes | irrelevant to inputs | — | `_newRed` / `_colorchange` state names show he cared for the video |

So the finding step can honestly say **"matched on: stage, characters/order/point, health-at-fight-start (by
construction), skip rate"** and **"unmatched/unknown: meter, positions, assist types, dummy state, skip phase"** —
unless the setup rows (from `_default` to `beforecombo`) are replayed too, which turns positions/meter/point into
*reproduced* rather than *matched* quantities (still not verified against his values, only against the oracle).

---

## 4. Recovering his base facts without the states

- **The `.p2m` is pads only.** Dhalsim97: 566,704 bytes = **16 + 47,224 × 12 exactly**, 0 trailing bytes; there is
  no integer N with 16 + 6707·N = 566,704 (N = 84.49). The 47,224 physical frames = 6,707 movie + 40,517 stale;
  no embedded state, no hash, no name, just the 8-byte timestamp.
- **His 0.9.6-rr savestates DO embed the movie prefix** — `scan_states.py:5-9` relies on it and recovered slot
  frames for 361 clips, so a copy of the pad grid up to the save frame sits inside every `4D228733.NNN` gzip.
  That contradicts the 1.4.0-rr note (`savestate_and_movie.md:5-22`: only `g_FrameCount` is frozen) — a version
  difference; the 1.4.0-rr doc must not be applied to David's states. Those states also hold the full EE RAM, so
  positions/meter/health/stage/skip phase are physically recoverable from them — but only with the **PS2 port's**
  RAM map, which nothing in this tree provides (the CT/SPREADSHEET addresses are Demul/DC `0x2C…`), and the state
  files are not here (only in David's OneDrive).
- One 15-minute test David could run on a state: decompress `4D228733.002_beforecombo`, find the anchor S where
  the embedded grid equals the file from offset 16, and check whether `raw[S-6024:S]` equals `data[16:6040]`
  (the 502 boot-chatter frames). If yes, the state's counter is 4714 and §0 is confirmed at the byte level.

---

## 5. Recommendation

**Minimal "match" claims for the finding step** (per clip, mechanically derivable now):
1. Stage from the folder prefix → `Stage_Selector` on the DC base (Dhalsim97: Boat2 = 9).
2. Characters + order from the folder name, cross-checked by decoding the pick rows on `charselect.py`'s GRID
   (P1 first pick decodes to Dhalsim); point character at the anchor from the tag rows (`x+square`/`triangle+circle`
   between `_default` and the window).
3. Fight-start anchor: his `_default` = last confirm + ~612 rows; on the DC pin it by RAM (`Match_Start_Throw_Timer`
   0x2C289602 starting its 60-count, or `sceneFrame` reset), never by row arithmetic.
4. Skip rate equal (4 on both; his phase unknown → sweep the 4 phases).
5. Oracle: `combo_peak_p1` (already in every test-run manifest, CLAUDE.md `TestSaveState` note) must reach the
   folder number (97). A clip is "transferred" iff some (offset, phase) reaches it; otherwise the verdict names
   the highest peak reached — a real per-clip fidelity score instead of "moved the byte to 19".

**Worth building:** the in-match re-anchor. Play his rows from `_default` (not from `beforecombo`) onto our
fight-start base with the setup included (tag, assist calls, hypers, walk), sweep the start offset over ±k rows
(k ≈ 15 covers his hand-saved ±12) × 4 skip phases, and take the peak. This uses only existing primitives
(`flycast_run_test`, `flycast_edit_inputs overlay`, `combo_peak_p1`) and it doubles as the test of the open
question in §1c: if **no** offset/phase ever reaches 97 for a clip whose base is matched on 1-4, the PS2 port's
logic-per-VSync differs and the whole corpus needs a tick-level translation, not a frame one — that is the
result that decides whether to continue.

**Dead end:** a "per-menu re-anchor" of the boot walk. His menu rows encode nothing but *which* items he picked,
on a different boot timeline with a different menu tree (24 Start taps vs fastVS's 6, net +4 right vs +2, an
extra screen at 927..980, a 612-row intro); the CSS tour already authors those choices deterministically on the
DC. Translating cursor rows is strictly worse than translating them to names first.

**Do first, cheaply:** re-run the converter with `origin=16` and `--mark-states` (when the states are reachable)
so that rows == PS2 frames, the 502 stale rows disappear, the 26 stale "combo start" estimates and the 6 broken
clips are repaired, and the "122 truncated" list shrinks to 2 real ones.

## 6. `[MEASURED 2026-09-18]` The re-anchor experiment - the go/no-go, run

§5 asked for it; here it is, on the authored Dhalsim base (`[dhalsim_base]` 5FC481A7 @
2059, Dhalsim/Cable/Sentinel vs Ryu/Ken/Guile, `dist=377 skip=3/4`), with the origin-16
`Combo_Dhalsim97` (window 4714-6201 = his `beforecombo`..`end` markers), through the hunt
(`dojo:ComboHunt=all`, the new `dojo:ComboHuntDelays=a-b` offset axis x the 4 phases).

| run | rows placed at base+1+phase+d | candidates | peak |
|---|---|---|---|
| A: the window | 4714-6201 (1487 rows) | 43 (d 0..10 x 4 phases) | **1 or 2**, every one; hashes collapse on phase+d (the hunt's "phase and delay are the same thing") |
| B: his setup + the window | 3674-6201 (his `_default` fight-start marker onward: the tag, A1 taps, QCF+KK / QCB+KK meter build, the walk right, then the combo) | 6 (d 0..1 x 4 phases) | **1**, every one |

The user, mid-run: *"we can tell we have a dhalsim player but we also don't know the
conditions of the combo right? his is just inputs."* That is the finding, exactly. Timing
is not the variable (A: no offset or phase gets past 2). His own actions before the combo
are not the variable either (B: replaying them lands FEWER hits than the bare window,
because the tag and the walk put Dhalsim somewhere his combo rows do not expect on THIS
opponent). What the file does not carry is what the inputs were REACTING to: the
opponent's state (a PS2 Training dummy with some behaviour set - ours is a live Ryu with
no inputs, standing), the distance at 4714 (ours 377 at fight start; his unknown), the
stage geometry (his Boat2; ours stage 0), meter, damage scaling, RNG.

**Verdict: the archive's combos do not transfer as input streams onto a matched-cast base;
97 -> 2 is not a timing problem.** The next thing that could move the number is not a
sweep but a CONDITION: (1) Training mode with the dummy set the way he had it (his rows
1076..1762 pick P2's team as a HUMAN would - so the base should probably be a Training
session, not VS; the `_default` state was hand-saved 600+ rows after the last confirm,
which is the Training intro length); (2) the distance at his window start, readable on our
side and settable by a walk. Both are authoring choices for the CSS tour's base, not
sweeps. The 442 other movies inherit this verdict until one is shown to transfer.

## 7. `[MEASURED 2026-09-18]` Our own combo on the Dhalsim base - the fixture the modules stand on

§6's verdict left the studio's intent modules on a base that is true (Dhalsim on point) with an
oracle that is thin (David's rows: 2 hits). The user chose to author a DC-native combo instead.
Twelve hunt runs on `[dhalsim_base]` (5FC481A7 @ 2059, P2 a live Ryu, no inputs, distance 377):

| rung | candidate | peak |
|---|---|---|
| no walk | LP / HP / LK / HK alone, 7 timings each | 0 - nothing reaches at 377 |
| walk-in | walk right 120 + s.HP | 0 |
| walk-in | walk right 200 / 220 / 240 + s.HP | 1 |
| chain | walk 200, then LP > LK > HP > HK (2-frame presses), gap 4 / 6 / 10 between press starts | 2 |
| chain | the same, gap **7 / 8 / 9** | **3** |
| all phases | gap 8, phases 0..3 | **3 / 3 / 3 / 3** (after F47533EF B79A6986 816C0F49 89B07C85) |

The gap window matches David's atlas for Dhalsim (`PL25.json`: LP s4/a2/r4, LK s4/a2/r6, FP s6/a6):
a link ~8 frames after the previous press lands inside the chain window; 6 is inside recovery,
10 is past it. `scripts/fixtures/mvc2/combos/dhalsim_3hit.txt` (276 rows, seqHashMacro
aa67a6dcf4f9b903) is pinned in RECIPE `[combo]` with its peak and its four after-hashes;
F1 checks the file, F2 the pins. A launcher / air chain would land more; three, phase-insensitive
and 276 rows long, is the right size for a fixture the tour places forty times a day.
