# Scripted-Input Phase — Roadmap

The end goal: author MvC2 inputs as **text** and inject them into the movie through the same
`session_inputs[frame]` path recording and playback already use, with a piano-roll editor on top.
This file is the contract for the first stretch: six tasks over ~3 days, each verifying the ones
before it and being re-verified by the ones after. Nothing past Task 6 starts until both gates hold.

**The two gates (from the northstar — never break sync):**

- **G1 Input fidelity.** What the movie sends is what the game reads, every frame, both players,
  provable programmatically — not by eye.
- **G2 Timeline exactness.** The dead-timeline guard flags exactly the states invalidated by an
  edit or rewind — no misses, no false positives — and every *future* edit path (text import,
  editor cells) funnels through it.

---

## The six tasks

### Day 1 — see the truth

**T1. Memory probe layer** *(small)*
- Build: `tas_mvc2` reader over `ReadMem8_nommu(0x8C000000 + offset)` for the Appendix A
  addresses: input flag bytes (both players), frameskip rate/count, in-game frame counter.
  Gated on the MvC2 gameId; logs a `TAS MEM` trace line on demand.
- Verify (forward): boot the game, probe logs `frameskiprate = 6` at normal speed (4 = turbo).
  Hold a button; the flag byte shows the documented bit. **If rate does not read 6, the offsets
  are wrong for this rip and get hunted before anything else is built.**
- Re-verified by: T2 renders it, T3 asserts against it every frame.

**T2. Input visualizer panel** *(the instrument)*
- Build: ImGui panel (mirroring the VS Code extension's layout: P1 | P2 pads, dpad diamond,
  LP/HP/A1 · LK/HK/A2 · Start) with THREE data rows per player:
  - **SENT** — the movie packet for this frame (`session_inputs`),
  - **READ** — the game-RAM flag bytes (T1),
  - mismatch highlighted in red.
  Header: movie frame / movie length, in-game frame counter, skip phase `count/rate`.
- Verify: live play — press buttons, SENT==READ tracks in real time. Replay — same, hands off.
- Re-verified by: every later task uses this panel to debug its failures.

### Day 2 — make the truth programmatic

**T3. Sent-vs-read fidelity harness** *(gate G1)*
- Build: `dojo:VerifyInputs=yes` — during replay, compare the movie packet against the game-read
  flags each frame (respecting the read-latency offset measured in T2; expected 1 frame).
  Mismatches log `INPUT FIDELITY: frame N sent X read Y`; clean runs log a one-line verdict.
  test.ps1 learns to require `fidelity-ok` for PASS.
- Verify: magnetoNew + hayato replays pass with 0 mismatches.
- Re-verified by: runs inside every later test forever; T5/T6 cannot pass without it.

**T4. Guard verification, scripted** *(gate G2)*
- Build: `guardtest.ps1` (keybd_event harness, same rig as scrubstuck.ps1): record → save slot A
  @ ~600 → save slot B @ ~1200 → F3 to A (rewind) → record past 600 → quit. Then assert offline:
  clip.json `rewinds == [[1, ~600]]`, slot B sidecar is 12 bytes, slot B STALE by the rule,
  slot A clean, and a re-save of B clears it.
- Verify: PASS on current build; FAIL if the rule or plumbing regresses.
- Re-verified by: re-run after T6 — text edits must produce the identical stale verdicts.

### Day 3 — the text layer, gated by days 1–2

**T5. Text codec + round-trip proof** *(DONE — see results below)*
- Build: exporter/importer for the Appendix B grammar. Export `.flyr` → `.tas.txt`,
  import back, byte-compare `session_inputs`. `tastext.ps1` runs it headlessly on magnetoNew.
- Verify: round-trip byte-identical; hand-edited line changes exactly the intended frame
  (T2 panel shows it; T3 confirms the game reads it).
- Re-verified by: T6 uses the importer as its only write path.
- **Results:** round-trip byte-identical on magnetoNew (3606 frames) and on a fresh recording
  (1706 frames → a SIX-line file, zero `@raw`). Hand-edit proof: editing one line changed exactly
  frame 100 and nothing else. **Legacy movies export as `@raw` hex** — the old recorder left
  uninitialized stack garbage in the packets' dead bytes (varying per frame, so it neither
  collapses nor names); the recorder now zeroes packets, so everything recorded from today is
  clean text. The fresh clip `2026-08-23T18_16_00Z` is kept as the clean-text fixture for T6.
- **Acceptance demo (the button-check movie):** author, as text, the full path the user specified —
  boot → wait → Start → VS mode → pick 6 characters (duplicates fine) → in-match button check of
  every direction and button for BOTH players, then the parity sweep: the same LP repeated at
  frames N, N+1, N+2, N+3, N+4 to observe skip-cycle parity. Verified offline from
  `fidelity.jsonl` (which logs the skip phase per frame) against the toggle anchor. This is also
  the replacement for the parked keyboard boot-macro idea.

**T6. The edit funnel** *(closes the G2 hole)* *(DONE — see results below)*
- Build: `Dojo::ApplyEdit(firstChangedFrame)` — the single choke point every future edit path
  calls LAST (BizHawk's discipline): logs a timeline event into `rewind_log`, no-op-guarded
  (unchanged bytes = no event), with a `SingleInvalidation`-style batch wrapper so an N-line
  import costs one event. Text import calls it; nothing else may touch `session_inputs`.
- Verify: import a text file altering frame N → states above N flag stale (T4 harness re-run),
  states below N stay clean, T3 fidelity still passes on the edited movie.
- Re-verified by: it IS the guard from here on; the piano-roll editor plugs into it untouched.
- **Results (`textguard.ps1`, on a copy of the fixture):** a one-frame text edit at F=1058 through
  `Dojo::ApplyEdit` produced timeline event `[2,1058]`; BASE (below) stayed clean, slot1/2/3
  (above, saved earlier) all went STALE — verdicts identical to a rewind. Persistence is
  **append-only**: the changed frames are appended to the `.flyr` (records carry frame numbers,
  parser is last-write-wins), the file grew 60847→60891 bytes, and a full reload + re-export still
  showed the R at frame 1058. No-op edits log no event; truncating edits are refused. The harness
  also caught (and we fixed) a blind-scan bug: `WriteClipStats` at Init-time regenerated `states[]`
  from an empty scan and wiped the slot records.

**Post-stretch UX/guard pass (user-requested):** R from write mode at the frontier now enters
**review mode** — read-only + auto-seek to BASE (refusing politely when no BASE exists) — instead
of dying into End of Replay. And the timeline event is **deferred to true divergence**: a rewind
only ARMS the detector; the event fires at the first movie write whose bytes actually differ, with
the divergence frame as the event frame. Seeking back to look costs nothing, replaying an
identical stretch costs nothing, and slot verdicts sharpen (states between the seek target and the
divergence point stay clean). `guardtest` asserts all of it: refusal logged, event strictly inside
(slot1, slot2), review-only F3+R adds zero events.

**BOTH GATES NOW HOLD FOR THE TEXT LAYER.** The roadmap's first stretch (T1–T6) is complete; the
regression wall is `guardtest` + `textguard` + `test.ps1` (sync + fidelity) + `tastext`.

**Linkage map:** T1→T2→T3 (same data: probe → panel → assertion) · T4 independent, then re-run
after T6 · T5→T6 (importer is the funnel's first client) · T3+T4 together are the regression
wall every later commit must pass.

---

## Verification ledger — T1–T4 (established · expected · failure tracing)

The mental model everything hangs on — two pipelines, one instrument each:

```
INPUT PIPELINE (gate G1)
  session_inputs[frame]  ── the movie truth. Packet: ~kcode (pressed bits SET),
        │                   triggers as BTN_TRIGGER_* bits in the same field.
        ▼
  MapleApplyAction ────────── T3 hooks HERE: pushes SENT history, compares vs RAM
        ▼
  maple poll → game logic
        ▼
  game RAM flags @0x2681DC.. ─ T1 reads HERE (the game's own latched truth)
        ▼
  T2 visualizer (human view)   T3 harness (machine view)

TIMELINE PIPELINE (gate G2)
  savestate ──writes── .frame sidecar {frame, rerecordSeq, movieLen}
  F3-while-recording ──appends── rewinds[[seq,frame]] in clip.json
  IsStateStale: STALE iff some rewind has seq > state.seq AND frame < state.frame
  T4 proves all four verdict branches from clip.json alone
```

**Trust order when things disagree:** MemHunt raw probe (GetMemPtr) > T1 `read()` (ReadMem path)
> T3 canonical sets > T2 display. Trace failures downstream-to-upstream: a wrong pixel in T2 is
first checked against T1's hex row; a T3 mismatch with a sane hex row is decode-or-apply, not RAM.

### T1 — probe. Established (all MEASURED, not assumed)
- Map = trainer offset + 0x8C000000, unchanged. Proof: scene counter every-frame at 0x1F9D80.
- skipRate 0 menus · 6 attract/char-select · 4 match. count runs 4 3 2 1; toggle@0x289622 = 255
  exactly on the reset frame (the injection anchor). cycle@0x289600: uncharacterized.
- Input flags live at 0x2681DC/DD, F0/F1, trainer bit layout. Mirrors DF/E1/E3: uncharacterized.
- `mapValidated()` latches only once rate reads 2/4/6 → "probing" before the first fight is NORMAL.
- `read()` runs ONLY when something calls it: the visualizer (dojo:InputViz) or T3
  (dojo:VerifyInputs during playback). Both off = no probe, no TAS MEM, silently.

**T1 failure signatures**
| Symptom | First suspect | Tool |
|---|---|---|
| never VALID, even mid-match | offsets drifted (new rip/build) | dojo:MemHunt diff hunt |
| no TAS MEM despite presses | NETWORK channel muted, or no read() caller active | LOGGING panel; turn viz on |
| ReadMem values ≠ raw probe values | address-path bug | compare TAS HUNT window vs TAS MEM |

### T2 — visualizer. Established
- ring = SENT, source picked by MODE (REC → live pad; PLAY → movie packet). fill = READ (T1).
- Header clocks: movie frame/len + REC|PLAY|live, scene frame, skip c/r ('*' only when rate ≠ 0).
- Raw hex row of flag bytes at the bottom = the self-diagnosis line for any visual oddity.

**Offline record:** `dojo:FidelityDump=yes` writes `<clip>/fidelity.jsonl` during playback — one
line per applied frame: `{"f":N,"sp1","sp2","rp1","rp2","skip":"c/r"}` (canonical sets, hex).
The eye cannot follow ring→fill at 60 fps; this file is what you diff after the fact, and what
the VS Code extension can render as a timeline. Flushed per line, so a kill or crash keeps it.

**Expected:** stepping while recording = steady ring, no flap. Playback = rings from movie, fills
follow within ≤1 frame. Transient ring-no-fill (1 frame) is latency, fine.
**Alarms:** persistent ring-no-fill = game not reading (T3 will fail too → it's real).
fill-no-ring = GHOST — game sees what the movie never sent; suspect live-pad leakage into
playback, decode error, or map drift, in that order.
**Known risk:** T2's button mapping is a SEPARATE table from T3's canon converters. If they ever
disagree, T3 is authoritative (unify into one shared table when convenient).

### T3 — fidelity (gate G1). Established
- Per applied frame, canonical 11-button sets (packet convention PROVEN: ~kcode + trigger bits).
- READ may match any of the last 4 SENT frames; 4-frame warmup after any frame discontinuity.
- Baseline (magnetoNew+hayato): 3183 frames, 0 mismatches, histogram same≈95% / -1f≈5% / -2f=0.

**Expected:** fidelity-ok on every replay; the histogram SHAPE is part of the baseline.
| Symptom | Meaning |
|---|---|
| -2f/-3f become nonzero | read latency grew — timing/skip context changed; investigate before trusting anything downstream |
| mismatches, hex row sane | packet decode or apply path — after T5/T6: the importer wrote wrong bits (T3 is the tripwire FOR the text layer) |
| exactly-one mismatch after a seek | warmup regression |
| fidelity-skip verdict | map never validated in that run (clip never reached a rate 2/4/6 scene) — not a pass, not a fail |

### T4 — guard (gate G2). Established
- End-to-end scripted proof, asserted from clip.json alone. Verdict table exercised all branches:
  below-rewind clean · AT-rewind clean (rule strictly <) · before+above STALE · re-saved clean.
- INCONCLUSIVE (not FAIL) when window focus was never proven — keys may not have landed.

**T4 assertion → plumbing it binds**
| Failed assertion | Broken piece |
|---|---|
| rewinds count ≠ 1 | LoadStateFrame rewind logging, or F3 never landed (check focused/INCONCLUSIVE) |
| slot missing from states[] | WriteClipStats states[] writer or scanSavestateInfo |
| rerecordSeq missing | SaveStateFrame sidecar-v2 write |
| slot2 not STALE | IsStateStale rule or seq/rewind data |
| slot1 STALE | strict-< regressed to <= |
| 2 rewinds | key double-fire (Tap timing) or double-log |

### Not established yet (do not assume)
- Guard vs TEXT edits — that is T6; T4 re-runs after it as the proof.
- guardtest has no negative run (never shown to FAIL on a broken guard); each assertion binds a
  distinct piece, but run a deliberate break when the guard is next touched.
- Analog stick fidelity (MvC2 is digital; sticks unchecked), cycle@0x289600, mirror bytes.
- Fidelity only checks PLAYBACK (play_match); recording-side fidelity is indirectly covered by
  replaying what was recorded.

---

## Appendix A — MvC2 (Dreamcast) memory map

Source: the Demul-CE trainer Lua (`MvC2_Trainer_Script.CT`, targets the DC version — same as
NoBGM_VMU.cdi). Its `emubase 0x2C000000` is where *Demul* mapped guest RAM in its host process;
the offsets are **guest RAM offsets** (the Lua's `romdata`/`GetRomAddr` naming is a misnomer —
these are RAM, not ROM). In flycast, guest RAM is at SH4 `0x8C000000`, so:

> **flycast address = 0x8C000000 + trainer offset**, read with `ReadMem8_nommu` / `ReadMem32_nommu`.

| What | Offset | Flycast addr | Notes |
|---|---|---|---|
| P1 input flags A | 0x2681DC | 0x8C2681DC | L=128 A(LK)=64 B(HK)=32 R(A2)=16 |
| P1 input flags B | 0x2681DD | 0x8C2681DD | Up=32 Down=16 Left=8 Right=4 X(LP)=2 Y(HP)=1, Start=128 |
| P2 input flags A/B | 0x2681F0 / F1 | 0x8C2681F0 / F1 | same layout |
| frameskip rate | 0x289620 | 0x8C289620 | u8: 6 normal, 4 turbo, 2 turbo2 |
| frameskip count | 0x289621 | 0x8C289621 | u8 countdown; at 0 → reset to rate, extra logic frame |
| frameskip toggle | 0x289622 | 0x8C289622 | u8: **255 exactly on the reset frame**, 0 otherwise — the clean cycle-boundary anchor (user cheat table + measured) |
| frameskip cycle value | 0x289600 | 0x8C289600 | u16: idles 0, starts counting on some in-match event — not yet characterized |
| scene frame counter | 0x1F9D80 | 0x8C1F9D80 | u32, resets between scenes |
| total frames | 0x3496B0 | 0x8C3496B0 | u32 since boot |

**Measured on the rip (T1 done):** the table applies unchanged. Skip pair reads **0/0 in menus,
6 in attract/char-select scenes, 4 in a real match** (turbo default). Input flags latch/clear live
at 0x2681DC/DD and F0/F1 exactly as documented; additional mirror bytes at 0x2681DF/E1/E3 look
like prev-frame/edge buffers — useful later for edge-vs-held questions. Beware the mute: TAS MEM
logs on NETWORK because an INPUT-channel mute once ate the trace for a whole session. The trainer holds many
more (hitbox lists, positions) — port them the same way when needed. Measured cadence at match rate 4: count runs `4 3 2 1` repeating, one step per scene frame, and
the TOGGLE byte fires 255 exactly on the reset frame — use `toggle == 255` as the injection
alignment anchor (simpler and less ambiguous than `count == rate`). Fast measurement loop: replay
magnetoNew with `dojo:AutoSeekState=0` — state 0 lands mid-match in ~2 s, skip system live.

## Appendix B — text grammar v1 (bk2-derived)

One line = one frame = both players: `|system|P1 bools + axes|P2 bools + axes|`

```
|.....|.........|  128,  128,    0,    0,|.........|  128,  128,    0,    0,|
```

Rules (each traceable to BizHawk findings):
- **LogKey header** written once, mapping columns to button names; import rebuilds column order
  from the FILE, never from the live controller. This is what keeps old files replayable.
- `.` is the only "unpressed" char; any other char = pressed (chars are cosmetic on read).
- Axes are comma-terminated ints, delimiter-parsed (padding cosmetic).
- Lines not starting with `|` are ignored → comments and directives free. `@loadstate <slot>`
  replaces the in-band `FE FE...` sentinel in text form.
- **Strict group validation** (bk2 skips pipes silently and mis-assigns — do not copy).
- **Normalize on ingest**: canonicalize once, store canonical (comparisons are string-equality
  downstream).
- Repeat suffix `|...| *12` expands at parse; in memory, log index == frame number always.
- Encoder drives from a **name→bit table**, never column index: our packet has X=bit10, Y=bit9,
  and `trigger[1]=data[8]` / `trigger[0]=data[9]` (swapped). Frame and player are positional —
  never serialized into the text.
- Binary `session_inputs` stays authoritative; text is the authoring layer (BizHawk stores text
  natively and re-parses per toggle — deliberately not copied).

## Appendix C — deferred ledger (decided, not scheduled)

- **Piano-roll editor**: virtual grid (`ImGuiListClipper`), one grid with per-player column tint,
  blank-when-default cells, column-locked drag paint, Clear≠Delete keys, right-drag splicer,
  named undo batches, green-arrow restore anchor (edit-in-past → remember position → rewind →
  optional auto-restore, anchor frozen across an edit burst).
- **Combo snippets**: BizHawk MovieZone model — snippet file (LogKey + lines) placed with
  Replace vs Overlay (OR bools, take non-neutral axes). Overlay is the combo-injection semantic.
- **State strategy**: we are in the sparse-skeleton + fast-replay regime (28 MB states), not
  dense-greenzone. If/when density matters: capture interval `K = targetLen × stateSize / budget`,
  zstd the states, keep a coarse never-evict skeleton, force-capture at N-1 on edit and delete
  the previous forced capture.
- **DivergentPoint restore**: "Replace live with gen_NN" should diff the two movies and
  invalidate from the first differing frame only.
- **Integrity check ≈ Harness #2**: replay from 0, byte-compare every stored state against a
  fresh one; also the test of whether flycast states are deterministic enough for any greenzone.
- Explicitly skipped from BizHawk: string-mnemonic in-memory storage, lag-frame machinery
  (fixed-60 fighter), grid rotation, autofire pattern painting, their broken AutoAdjustInput.

## Phase PR — Piano Roll v2: the second way to program combos (planned 2026-08-24)

T1–T6 and the docking/studio UI phase are done; the piano roll (F6) is a working
view-and-edit grid. This phase makes it an AUTHORING surface. Target ≈ one focused day,
tasks sized to land one at a time (order negotiable, PR1 first — it unlocks the archive).

**PR1 — Load / Save Macro (the CE-trainer format).** The user's 6-year Demul combo archive
(e.g. `RubyHeartCombo34_P1.txt`) is written in the MvC2 Hitbox-View trainer's macro
language: **one line = one frame; uppercase letters = keys held that frame; blank/space =
neutral; stray numeric lines occur and are ignored on read**. Letters are the trainer's
per-player PHYSICAL KEYS (its UI maps label row `U D L R X Y L A B R S` over P1 keys
`W S A D Z X C V B N` + more for P2), so import needs a **key→canon-bit map confirmed by
the user** before his old files decode correctly (the M seen in `VM` pairs before supers is
still unidentified). Import = parse → place at current frame as a snippet with
**Replace vs Overlay** (MovieZone semantics, BIZHAWK_NOTES) → through `ApplyEdit("macro")`
so the guard prices it. Export = selection → same format (his archive stays round-trippable).
File dialogs: **native Win32 GetOpenFileName/GetSaveFileName** (comdlg32) — same approach as
the capture Save-As; ImGui has no built-in file dialog and we are Windows-only in practice.

**PR2 — Selection model. SHIPPED 2026-08-24.** Click / drag / shift-click in the frame
gutter selects a contiguous range (blue tint; gold current-frame wins). Consumers live
inline on the status line: **Save selection as macro** (tasMacroSaveAll grew a range) and
**Clear rows** — the content-only half of the surgery set, shipped early because it is
length-preserving and therefore safe through ApplyEdit("clear") today. Semantics settled in
review: macro placement is REPLACE (nothing shifts; a state AT the placement frame stays
valid — it snapshots the world before that frame's input; states above go STALE via the
guard). Insert/Delete remain PR3 because they SHIFT frames and savestate anchors.

**PR2b — Non-contiguous selection. SHIPPED 2026-08-24.** The contiguous (selA, selB) pair
became a `std::set<u32>` of rows. Full list-view grammar in the gutter: **click** = replace
+ anchor; **Shift+click** = anchor..row REPLACES (anchor stays put, so successive
Shift+clicks re-range); **Ctrl+click** = toggle one row (moves the anchor; toggling OFF
does not start a paint); **Ctrl+Shift+click** = anchor..row ADDS; **Alt+click** = clear;
**Ctrl+A** = whole movie (never the void); **drag** paints anchor..hovered on top of a
snapshot taken at mousedown (`selDragBase`) — so plain drag replaces, Ctrl+drag paint-adds,
and dragging back shrinks correctly. Implementation note: ALL selection logic keys off
mouse-DOWN (`IsItemClicked`); a click is just a zero-length drag. Selectable's return fires
on RELEASE, and once toggles joined the mix the press/release interleaving became ambiguous
(a Ctrl+click would toggle a row on at press and back off at release).
**Op semantics on gapped selections** — one rule: *content ops act on "the selected rows,
in order" (gaps dropped, toast says so); Delete removes exactly the selected rows.*
Copy/Save-macro/Repeat/Duplicate compact the selection; Paste lands at selLo; Insert-N
inserts before selLo (the whole selection shifts up with its content); Delete pulls
everything after each removed row down in ONE op / ONE guard event / ONE undo — scattered
mistakes die together (`tasDeleteSel` merges the sorted row list against session_inputs,
renumbering by removed-below count; selected rows absent from the sparse map but inside the
movie still count as deleted neutral frames and shift the tail; selected void rows never
shift anything). For contiguous selections every op reduces exactly to the old behavior. Insert N blank rows before the selection
(N shares the Repeat spinner), Duplicate selection after itself (raw 24-byte copies —
analog survives, unlike the canon-roundtrip macro path), Delete rows (tail pulls down).
Buttons on the selection line + entries in the right-click menu, canEdit-gated. All three
run through the new `Dojo::ApplyEditResize` — the ONE funnel allowed to shrink the movie —
which diffs both directions (changed frames AND removed keys), prices the op like any edit
(history patch, rerecord++, guard event, clip stats), then calls
`Replay::RewriteReplayFile()`: an append-only .flyr cannot express a shorter movie (the old
tail would resurrect on reload), so the file is rebuilt from scratch — the **original
SPECTATE_START header bytes are preserved verbatim** (`file_header`, captured at
load/create; GenHeader would re-derive analog/trigger/GGPO flags from CURRENT config and
silently change frame decoding) followed by all frames in fresh batches. Rewrite also
compacts append bloat (fixture: 60KB → 47KB). Semantics worth knowing:
- **The guard event lands at the first REAL content diff, not the op frame.** Deleting 10
  frames inside a run of identical frames diffs where the shifted movie actually deviates —
  states below that point remain genuinely valid and are NOT invalidated. The toast counts
  invalidated states from that same frame, so it never overcounts.
- The awareness toast: "N savestates past frame F invalidated (auto-purged; BASE kept)" —
  or "(kept, marked stale - purge is OFF)" when `dojo:PurgeStale` is unchecked. That
  checkbox (Settings → TAS) IS the off-switch for A/B-testing states that deliberately
  outlive the timeline they were cut from.
- **Redo-of-delete zeroes the tail instead of re-truncating** (inverse patches store
  absent-frames as empty → applied as zeroed). Undo of delete restores fully. v1 limit.
- Harness: `resizeguard.ps1` — plants a marker via TextApply, deletes below it through
  `dojo:ResizeProbe=lo-hi` (dash, NOT comma — the -config CLI splits entries on commas),
  asserts the event frame, per-slot staleness, the rewrite log, and that a fresh reload
  round-trips the shorter movie with the marker pulled down 10. The probe is one-shot
  guarded because Replay::Init runs twice per session and a relative delete is not
  idempotent (the harness caught exactly that).

**PR4 — The input sender (CE trainer parity).** Play a loaded macro INTO a live recording at
the current frame: each stepped/run frame consumes the next macro line instead of the pad
(pad still wins for un-mapped columns in Overlay mode). Anchor modes: immediate, and
**"Wait For Frame Skip"** — start on the next `toggle == 255` reset frame (Appendix A), the
trainer's trick for savestate-free alignment. Stop/Play/Loop controls in the roll's toolbar.

**PR5 — Editing feel.** Column-locked drag paint (BizHawk InputRoll), Clear≠Delete keys,
named undo batches. Last because everything above is useless-blocking without PR1–PR4 and
this is pure polish on top.

**Key map CONFIRMED by the user (2026-08-24)** — labels `U D L R X(LP) Y(HP) L(A1) A(LK)
B(HK) R(A2) S(tart)` map to P1 keys `W S A D Z X C V B N M` and P2 keys
`T G F H U I O J K L P` (a trailing `.` in the trainer's key string is a filler, ignored;
`VM` in the Ruby file = LK+Start, confirming V→LK, M→Start). The two alphabets are DISJOINT,
so **one line carries both players unambiguously** — one file per combo works with no format
change; his old `_P1` files simply never use P2 letters. (His live-emulator `;` binding for
P2 Start is irrelevant to files — import decodes trainer letters, not current bindings.)

**PR1 design decisions (user walkthrough, 2026-08-24):**
- **Storage**: one `.txt` per combo, saved IN the open clip's folder (next to the `.flyr`);
  native Win32 open/save dialogs default there.
- **Current State controls** (trainer parity): **Stop** (halt playing macro), **Play** (from
  a chosen line), **Loop** (whole macro or a selected range), **Record** (listen to the
  user's inputs - likely nearly free, the recorder already captures; recording INTO a macro
  = reading the recorded rows back out of the movie).
- **Line surgery**: **Add** (append row), **Insert** (between rows, mouse-selected),
  **Replace** (row's inputs = current), **Delete** (remove row - length change, so it rides
  the guarded path like PR3 row ops), **Clear Table** (wipe a player's column - or
  everything in one-row mode).
- **Open design tension, deliberately unresolved**: one merged row for both players vs two
  independently-editable synchronized columns. Decision: build the data layer player-split
  (it already is - canon bits per player) and add a VIEW SWITCH later; neither UI style is
  privileged by the format.
- **Visual translator**: display notation is a selectable skin (MvC2 names / trainer
  letters / numpad) over the same internal canon bits - the name→bit table pattern already
  used everywhere. What you SEE is presentation; what is SENT is canon.
- User's expectation: PR1 is the slow one - it is where the macro system, the piano roll,
  and the savestate/replay machinery learn to coexist in one editor surface.

**PR1 + PR4-minimal SHIPPED and user-verified (2026-08-24)**: Load/Save Macro with native
dialogs into the clip folder; placement through ApplyEdit; the input sender as a recording
SKIP (MapleApplyAction already plays session_inputs in every mode - the sender just stops
the pad from overwriting armed rows). First archive victory: RubyHeartCombo34 produced 7
hits through the full ritual (Play -> state 0 -> R -> reposition -> re-cut BASE -> slot 1
-> Load Macro -> resume), and F3-to-slot1 + P replays the macro because the arm survives
state loads. **Lesson for the next rung: the ARM is session-state, the rows are
movie-state.** A reloaded clip HAS the placed rows but records over them until re-armed
(user hit exactly this). **THE HARMONY MODEL (user, 2026-08-24) - the UX north star for this phase**: three ways to
author lines, one master switch. **ARMED** (red-bordered roll, click Arm/ARMED in its
toolbar; Load Macro auto-arms): the movie's rows ARE the input - txt and grid own the
timeline, the pad only fills row-less gaps. **UN-ARMED**: classic TAS - pad + frame advance
record over rows freely. **Surgical edits** (cell toggles now; row add/insert/replace/
delete pending PR2/PR3) work in either, paused. Arm is a session boolean now, not a range -
no auto-disarm, no re-load needed to re-send. Still ahead: Loop/play-from-line, notation
skins ("English" Down+Right files), maybe persist armed in clip.json.

**UX round (user, 2026-08-24, post-Ctrl-selections).** Three fixes from hands-on testing:
- **R (play -> write) auto-arms** when the roll is on (`dojo:PianoRoll`). The gap: during
  playback rows play regardless so clicks there don't arm; R then flipped to write and the
  rows went from "playing" to "silently recorded over". Now the transition is seamless; the
  toast says so and one click on ARMED releases to classic pad re-recording. Harness-safe:
  no harness enables PianoRoll, so scripted rewinds stay classic.
- **ARMED outline survives docking**: docking swallows per-window border styling, so the
  alarm draws a thin pulsing red frame on the FOREGROUND draw list around the whole panel.
- **Replace... (Ctrl+H for the roll)**: on the selection line + right-click menu. FROM
  (any of the 22 columns) must be pressed on a row to match; FROM clears, TO sets - TO can
  be the other player or "(nothing)" (= erase FROM only). Trigger duality honored (A1/A2
  set byte AND kcode bit). One ApplyEdit("replace") = one guard event = one undo. The
  right-click entry relays through a flag because the menu's ID stack lives in the table
  child while the popup lives at window level.

**Roll Tools module + brush painting (user, 2026-08-24).** Checkpoint tag
`piano-manual-checkpoint` marks the state before this round.
- **Roll Tools window** - Replace's permanent home ("its own section... room in the top
  right... room to grow"): a separate dockable window like Timeline/Input Viz, default
  top-right, `dojo:RollTools` checkbox in Settings -> TAS, F5-gated with the suite. Holds
  REPLACE (From/To combos + Replace-in-selection, acting on the roll's selection - the
  selection state moved to file scope so both windows share it) and BRUSH (the gap
  setting). The roll's "Replace..." button and right-click entry now just front this
  window (tasToolsFocusRequest).
- **Brush painting** - drag down a button column to paint it into every row passed.
  Column-locked stroke (BizHawk InputRoll style), begins on cell press, live green/red
  preview of exactly the cells the commit will touch, commits ON RELEASE as one
  ApplyEdit("paint") = one guard event = one undo. Mode from the anchor cell: off paints
  ON, on erases - so a zero-length stroke IS the old single-click toggle, unchanged; Alt
  forces erase; Shift = single cell (no drag-fill). **Gap setting** (Roll Tools): every
  row / every 2nd (30 Hz - the MvC2 mash rate, Tron's drill HK) / every 3rd (20 Hz).
  Extent + pattern, not per-hovered-row: a fast mouse skips rows and per-row painting
  would leave holes. Void strokes extend the movie with neutral gap-fill below, same as
  cell clicks. Shared plumbing extracted to file scope: COLS/NCOLS column model and
  tasColPressed/tasColWrite (trigger duality in one place).

**Tools strip merged into the roll (user, 2026-08-24).** The separate Roll Tools window
lasted one review: "put the replace and roll tools into the main piano roll... we don't
NEED that much UI space." Now one compact strip inside the roll under the macro line:
REPLACE [From] > [To] [Apply] - BRUSH [gap] (?). Static how-to text became tooltips (the
Apply tooltip explains exactly why it is disabled: no selection / same inputs / paused /
read-only). Replace combos are COLORIZED with the grid's P1 blue / P2 red accents
(tasColCombo). The window, its cfg key (RollTools), checkbox and focus relay are gone.
Same round, brush upgrades from the first hands-on:
- **Live preview got teeth**: painted cells show their POST-commit state - the label
  ghosts in green while painting, drops out on red while erasing (was: a faint tint the
  user never noticed).
- **Shift = straight-line lock** (replaces shift-as-single-cell): for 100-frame mashes the
  stroke follows the ROW under the mouse even when the hand drifts into other columns -
  before, drifting sideways stalled the stroke until the mouse came back. Alt+Shift =
  erase + straight line. Alt alone still forces erase; anchor-mode still decides
  paint-vs-erase (off paints ON, on erases).
- Queued idea from the same message: **selection savers** (named selections, save/recall).

**Selection savers + bookmarks + hover ghost (user, 2026-08-24). SHIPPED.**
- **Sels** (tools strip): named selections per clip - save the current selection under a
  name, recall/delete from the popup; recalling also scrolls the roll to the selection's
  first row. Stored as compact [lo, hi] range lists in clip.json ("selections") via
  read-modify-write (tasClipMetaRead/Write - same discipline as WriteClipStats, so fields
  never stomp each other).
- **Marks** (tools strip): bookmarks = named frames per clip ("bookmarks" in clip.json,
  kept sorted). Click to jump the roll there; delete inline; add via the popup ("+ @
  cursor") or right-click menu ("Bookmark frame N" at the selection start, auto-named).
  Visible three ways: GOLD frame number in the gutter (name in the tooltip, merged with
  the savestate tooltip), gold tick on the heat map, count on the button.
  jumpTo was promoted to file scope (tasJumpTo) so bookmark jumps ride the same scroll
  mechanism as heat-map scrubs.
- **Hover ghost**: cruising the mouse over an empty cell shows a half-opaque preview of
  that column's button (P1 blue / P2 red at 38%%) in place - no more climbing back up to
  the column header to check where you are. Suppressed while a paint stroke is live.
Bookmarks/selections are exactly the metadata the BRANCHING phase needs (fork points get
names before they get branches).

**Grid-tools round (user, 2026-08-24).** Five refinements from the savers/bookmarks pass:
- **Compact is the only version.** The full-size layout, its checkbox and the
  dojo:PianoCompact cfg read are gone; 0.80 font + tight rows are canon.
- **Replace dropdowns open full-height** (ImGuiComboFlags_HeightLargest) - all 22 columns
  visible, no inner scrolling.
- **Ctrl turns the grid into the gutter** (the "V=select, B=brush" idea, done as a
  modifier instead of sticky modes): Ctrl+click on any CELL toggles its row in the
  selection, Ctrl+drag paint-adds from the mousedown snapshot, Ctrl+Shift ranges from the
  anchor. Selection works even running/read-only; painting stays plain-click. Auto-arm
  skips Ctrl clicks (selecting is passive).
- **The cursor names the tool** (user ask): Ctrl over the grid = hand (selection), Alt =
  slash (eraser), plain = arrow (brush).
- **Right-click re-targets**: right-clicking a row OUTSIDE the selection selects that row
  first, so the context menu always acts on the row under the cursor (was: menu acted on
  a selection two screens up). Works from cells and gutter both; with no selection at
  all, right-click now conjures a 1-row selection and the menu appears.
- **Bookmark dot**: a gold dot at the gutter's right edge on bookmarked rows - one
  bookmark color everywhere (gold: dot, frame number, heat-map tick).

**Two-button grid (user design, 2026-08-24) - the tool-switch problem solved.** The
Ctrl-as-tool-modifier experiment lasted one session: Ctrl already means "non-contiguous
ADD" inside selection grammar, so it cannot ALSO be the switch INTO selection (the user
hit exactly that ambiguity). Their fix, implemented: one tool per mouse button.
- Default: **LEFT = selection** on the grid with the full gutter grammar (plain / Shift
  range / Ctrl toggle-add / Ctrl+Shift range-add / Alt clear - shared selPress lambda,
  gutter and grid now literally run the same code). **RIGHT held >0.2s or dragged to
  another row = the brush** (all brush behavior unchanged: anchor mode, Alt erase, Shift
  straight-line, gap, preview, one-undo commit). **Quick right press = context menu**,
  retargeting to the row under the cursor when outside the selection.
- **dojo:RollClassicPaint** (Settings -> TAS checkbox) flips it: left paints/toggles like
  the original grid, selection moves to right-hold. Strokes carry their owning button
  (selDragBtn/tasPaintBtn) so both mappings share every code path.
- Auto-arm moved from "any grid click" into paintStart - actual painting arms, selection
  never does. A right-hold that cannot paint (running/read-only) says so in a toast
  instead of silently eating the gesture. Cursor: ResizeNS while painting, hand while
  selecting, slash while Alt-erasing.
- Menu is opened manually (BeginPopup + explicit OpenPopup) - BeginPopupContextWindow's
  open-on-release conflicted with right-drag painting.
Same round: **RANGE maker** on the macro line (type lo-hi, Select builds the selection
and scrolls there - feeds Sels for the "Spiral HP Grab" save-recall flow); **Enter
commits** the bookmark and selection-saver name fields.

**Edge auto-scroll for strokes (user, 2026-08-24).** Dragging a paint or selection
stroke to within ~1.5 rows of the table's top/bottom edge scrolls in that direction
(speed grows with overshoot, frame-rate independent), and the stroke EXTENDS to the row
being revealed - past the edge the mouse hovers no row, so hover-based extension stalls;
the stroke chases the first/last visible row from the clipper instead. Follow suppresses
itself while a stroke is live so it cannot fight the drag. The 30 Hz mash workflow now
paints straight through hundreds of frames without lifting the button.

**Roll sizes in two chunks (user, 2026-08-24).** Settings -> TAS grew a PIANO ROLL group
with two percent sliders, mapped to the roll's two font-scale sites: **Panel text &
buttons** (window-level chrome - toolbar, tools strip, heat map) and **Grid cells &
headers** (the table's inner-child scale; row height and gutter width follow
automatically). Live preview while dragging (virtual cfg), file write once on release
(IsItemDeactivatedAfterEdit). Both default to 90%% - the hardcoded 0.80 was "too small".
Also noted for the record: FCEUX TASEditor's "Draw Input by dragging" default equals our
dojo:RollClassicPaint = ON; the help text now says so.

**Delineation round (user, 2026-08-24).** Settings -> TAS reorganized: general options
(viz + roll checkboxes with their own help markers - the viz marker had been stranded
after Auto-purge for weeks - then Auto-purge) sit ABOVE the PIANO ROLL group; Classic
paint moved INTO the group with the two size sliders. In the roll itself: the baked-in
hint line ("read-only replay: cells are locked..." / "click a cell...") is GONE - the
mode badge carries it now, in the Timeline's exact colors (user ask): green [REPLAY] =
read-only, dark red [REC] = write, BRIGHT red [REC] = write + armed; hover the badge for
the full sentence. The macro line slimmed to "places at frame N | RANGE lo-hi Select"
(the CE-trainer format note lives in a tooltip), pulling RANGE back into view.

**Paste x3 + synced clobber warning (user, 2026-08-24).** The selection toolbar grew
exactly one row (content ops), reorganizing into: info/Save/Copy/Deselect - **Paste
Replace | Paste Insert | Paste Append** + Clear + Repeat - Insert/Duplicate/Delete.
Paste Replace = the original overwrite at selLo; **Paste Insert** shifts the tail up by
the clip's length and drops the clip into the gap (rides the PR3 resize funnel - .flyr
rewrite + guard - and the selection shifts up with its content); **Paste Append** lands
at the movie end. All three arm the roll; the right-click menu mirrors them. And the
**savestate-clobber warning is now synced between panels**: the same gui_stale_blink()
window that drives the Timeline's digit strip paints a blinking red "! savestates
invalidated downstream" next to the roll's mode badge, tooltip noting auto-purge state.

**Undo feedback + stable toolbar layout (user, 2026-08-24).** Two fixes:
- **Undo/redo toast** on all three triggers (buttons + Ctrl+Z/Ctrl+Shift+Z), honest about
  the one thing undo cannot restore: "Undid the edit - frames restored; already-purged
  savestates do NOT come back" (the caveat appears when dojo:PurgeStale is ON - the purge
  deletes .state/.png/.frame the moment the guard event lands, and no undo recreates
  files). The synced stale-blink warning also fires on undo since undo IS a guard event.
- **The selection toolbar's three rows are ALWAYS rendered** (disabled without a
  selection). Collapsing them when a delete cleared the selection changed the layout
  height above the table and the view shifted under the user's cursor ("the screen
  shifted a bit"). Same always-reserved pattern as the browser's details pane.

**Sync + polish round (user, 2026-08-24).** Five items from the paste-round video:
- **Timeline shows the same clobber warning** as the roll - one gui_stale_blink() clock,
  identical text under the REPLAY/RECORD banner. The panels physically cannot disagree.
- **Ctrl+Z no longer re-alarms**: the blink window now starts only when an event FLIPS a
  state clean -> stale. Undo re-fires the guard but usually invalidates nothing new
  (already-stale stays stale, purged files stay gone) - so no blink; a state saved after
  the original edit and undone across DOES flip and still alarms (that one is real).
- **Auto-purge moved into the roll toolbar**, next to ARM, as a trash-can checkbox
  (ICON_FA_TRASH - Font Awesome 6 is already merged into the default font, so real icons,
  no emoji needed). Tooltip carries the full story incl. "deleted files do NOT come back
  on undo". Removed from Settings.
- **All TAS-tab section headers** (FRAME ADVANCE, REPLAYS, CAPTURE, UI, PIANO ROLL, TAS
  TRACES, DETAIL, CHANNELS, CONTROLLERS) go through tasSecHdr: the PIANO ROLL blue accent
  + vertical padding above each - the "row separation" delineation ask.
- **Skin-wide slider fix**: SliderGrab and SliderGrabActive were the SAME color (and near
  the frame bg), so the grab vanished when touched. Light lavender idle, brighter while
  dragging.

**Blink regression fix + live scale preview (user, 2026-08-24).** The flip-detection
quieting of Ctrl+Z broke the warning entirely with auto-purge ON: gui_purge_stale_tick
runs BEFORE the blink's flip scan in the frame and DELETES the files - by scan time the
slots read empty, not stale, so no flip was ever seen. Fix: one shared staleEventAt
clock, settable by BOTH detectors - the purge tick starts it when it deletes >=1 file
(deletion IS the invalidation), the flip scan starts it for kept-but-greyed states
(purge OFF). Ctrl+Z stays quiet on both paths (nothing deleted, nothing flipped).
Same round: **live scale preview** - dragging a roll-size slider stamps
gui_roll_scale_preview_at; gui.cpp fades the Settings window (BgAlpha 0.25) and draws
the piano roll OVER it while the stamp is fresh (+0.3s linger), so the sizes preview
under the user's finger ("fade out the black menu background... live-see the changes" -
turned out cheap, not the big refactor feared). Also: capture rows 25%% narrower;
LOGGING gets its blue header above the collapsible; the LOGGING sub-sections (TAS
TRACES / DETAIL / CHANNELS) switch to SeparatorText so sub-level and top-level headers
no longer share one style.

**Purge-OFF desync + preview scope (user, 2026-08-24).** Three follow-ups:
- **Two-tier warning**: RED "! savestates DELETED downstream" when auto-purge is ON (files
  gone), ORANGE "! stale states downstream (files kept)" when OFF - the user's "desync but
  no state removed" tier. Same text and colors in BOTH panels; tooltips explain each.
- **The roll's gutter now shows staleness**: a frame whose anchored states are ALL stale
  renders its number dim red (matching the Timeline's grey digits - the desync the user
  hit: Timeline greyed slot 1 while the roll's "*1" looked healthy), and the tooltip
  appends "(STALE)" per slot. Ctrl+Z note stands: stale stays stale (one-way guard),
  files stay wherever they are - undo touches frames only, and fires NO warning since
  nothing new flips.
- **Preview stamps widened**: the user dragged "Overlay size" and nothing previewed - only
  the roll sliders stamped. Overlay size + Overlay opacity now stamp too, and the Settings
  preview draws the WHOLE suite (Timeline + Input Viz + roll) over the faded menu. The
  screen behind settings stays black (the game frame is not rendered in the Settings
  state - a renderer-path change if ever wanted); the previewed windows themselves are
  the payload. Capture rows narrowed again (0.75 -> 0.55 of default width); trash
  tooltip got line breaks.

**Preview ghost mode (user, 2026-08-24).** The preview windows drawn over the faded
settings could land UNDER the held cursor; the drag survived, but the next press hit the
overlaying window instead of the slider - "I lose connection to the slider" until the
fade expired. Fix per the user's instinct: while the preview is fresh (and only in the
Settings state), the suite windows Begin with NoInputs | NoFocusOnAppearing
(tasPreviewGhostFlags) - drawn as GHOSTS, every mouse event passes through to the slider
underneath, so you can scrub sizes continuously while watching them land.

**Fact-keyed warning tier + layout trims (user, 2026-08-24).** The user F3-loaded a
"deleted-looking" BASE that was fine on disk: BASE (slot 0) is purge-EXEMPT by design, so
a BASE-only invalidation deletes nothing even with the trash checkbox ON - but the
warning tier was keyed to the CHECKBOX and showed red "DELETED". Now the blink remembers
what its event actually did (gui_stale_blink_deleted): the purge tick stamps
deleted=true when it removes >=1 file; a lone clean->stale flip (purge off, or BASE-only)
stamps deleted=false - and the flip path does not clobber a fresh purge verdict from the
same event (0.5s guard). RED = files really deleted; ORANGE = stale but ON DISK, loads
fine, replays the OLD timeline (tooltip says exactly that). Layout: FRAME ADVANCE
sliders trimmed to 0.55x like the capture rows; the selection info is a compact
fixed-width slot ("sel 9 in 642..650 (gaps)" / "no selection") with the buttons anchored
at a constant x - Save/Copy/Deselect never move again.

**CONTENT REVALIDATION SHIPPED (user, 2026-08-24)** - the "staleness is one-way" ledger
entry is retired for kept states. The user's case: trash OFF, accidental early-row edit,
Ctrl+Z - net NOTHING happened, yet the states stayed grey forever. Now: **sidecar v3**
adds a u64 FNV-1a prefix hash (movie bytes strictly below the anchor, hashed at save
time; additive - v2 12-byte sidecars stay valid, seq-only). The verdict becomes: the seq
rule ARMS suspicion, an identical prefix EXONERATES - `IsStateStale(frame, seq, hash)`
re-hashes the current prefix only for seq-suspect states (cost scales with stale count,
not slot count). Undo with purge OFF -> bytes match -> states turn clean everywhere at
once (Timeline digits, roll gutter, F4 grid, F3 load check) with no new blink. What does
NOT change: purge-ON deletions stay permanent (the purge runs the moment the event lands,
before any undo); old sidecars without hashes stay seq-only until re-saved. Undo toast
now says "kept states revalidate where the bytes match" in the purge-OFF branch.

**Drag teleport bug KILLED (user, 2026-08-24).** Root cause of the broken combo: leaving
the OS window mid-drag makes ImGui's MousePos (-FLT_MAX, -FLT_MAX); the edge auto-scroll
read that as "infinitely above the table", slammed the scroll to row 0, and the stroke
chased it - the selection (or a paint stroke) extended anchor..0, one commit rewrote the
movie's whole head. The heat-map scrub had the same hazard (scrub-to-0). Three guards,
implementing the user's "dragging can never produce a non-sequential jump" rule:
(1) IsMousePosValid() gates the edge auto-scroll and the heat-map scrub; (2) horizontal
bounds - the mouse must be within the table's width (+-8px) for edge scroll/extension to
run at all: out the SIDE, the stroke simply freezes until the mouse returns; (3) a rate
cap - even with a valid coordinate the scroll can move at most one viewport per frame,
so no glitch can ever slam the view across the movie. Extension endpoints were already
bounded (hovered visible row, or first/last visible row), so with these three the
endpoint can only ever walk, never jump. Literal +-1-per-frame was considered and
rejected: a legitimately fast drag skips rows, and extent+pattern fill depends on that.

**MASH BAR SHIPPED (user, 2026-08-24) - the Demul-CE manual input creator.** New strip
row: MASH [pattern] [P1|P2] [xN] [xN @ cursor] [fill sel] (?). The pattern language is
the user's spec: '/' advances a frame (empty token = neutral), '+' joins inputs, names
U D L R (or ^ v < >) LP HP LK HK A1 A2 ST - "L+LP///HK" = L+LP, two neutrals, HK.
tasParseMash -> canon bits; tasMashPlace writes ONLY the chosen player's 12-byte half
(the other player's rows untouched - the "assign to P1 or P2" ask), tiles xN at the
cursor or LOOPS across the selection (fill sel = the loop system), extension gap-fills,
one ApplyEdit("mash"), arms the roll. The user's Demul Misc Scripts templates were read
and confirmed to be the CE letter format tas_macro already parses - they load TODAY via
Load Macro (the codec was validated against RubyHeartCombo34_P1.txt from that folder).
Queued: template player-REMAP (apply a P1 macro file to P2 - needs a canon-level swap of
the loaded macro's player halves). Cosmetics same round: gap labels compact-caps ("2ND -
30Hz", no more clipping), the two-button help tooltip cut to four lines, and set cell
labels render fake-BOLD (0.7px double-draw; preview ghosts included).

**Arrow glyphs + VSCode zoom + bold fix (user, 2026-08-24).** The doubled-letter render
bug: the fake-bold overlay guessed the Selectable's text position (rect min) - correct
only by luck at 0.80 scale; at bigger grid scales the overlay visibly split from the base
glyph. Fixed: the overlay now lands at the TRUE text position (rect min + half
ItemSpacing, matching Selectable's internal layout). Direction cells go further
(dojo:RollDirArrows, default ON): ^ v < > render as DRAWN centered triangle arrows
(tasDrawArrow) that scale with the row and are naturally bold - the font's caret glyphs
were small and left-baselined next to LP/HK text (the user's complaint); empty arrow
cells show a faint centered dot; ghosts and paint previews draw arrow ghosts. The
classic text glyphs remain a checkbox away. VSCode-style zoom: Ctrl+mousewheel over the
roll = grid scale +-5 (io.FontAllowUserScaling enabled only while the roll is hovered,
so ImGui swallows the scroll and our cfg write does the zoom); Ctrl +/- (main or keypad)
= panel scale +-5. Notation skins for BUTTONS (beyond directions) remain queued (PR5
"English" files idea).

**Mash round 2 + zoom anchor (user, 2026-08-24).** Ctrl+/- fixed (needed hover OR focus;
focus-only felt dead when the game had focus). Ctrl+wheel zoom now RE-ANCHORS the table:
the scale ratio re-derives ScrollY so the rows under the mouse stay put ((scroll+my)*ratio
- my, applied inside the table the frame the new rowH lands). Header row is custom now:
FRAME caps, ST (was St), and direction columns draw the SAME triangle arrows as the cells
(the old text carets clashed - user). MASH notation grew: numpad 1-9 directions, and a
CE-letter FALLBACK dialect - "AZX/VB" parses via tas_macro::FromText with the chosen
player's letter map, warning when the letters belong to the other player. A presets
dropdown (NoPreview arrow) seeds the field: LP mash, HK drill, HyperGrav, throw mash,
down-tap hail. Mash tooltip re-broken to short lines. NOTE: guardtest keeps focus-flaking
when run mid-batch (keybd_event needs the flycast window focused; earlier tests' window
churn steals it) - passes solo every time; consider a focus-settle retry in the wall.

**Zoom/keys/tooltip fix round (user, 2026-08-24).** Three real bugs + formatting:
- **Ctrl+= / Ctrl+- were structurally dead**: flycast feeds ImGui keys through its own
  keycodeToImGuiKey map, and '=' '-' keypad +/- were never in it - IsKeyPressed could not
  fire (the same trap the map's own comment describes for the 1/2/3 prompt shortcuts).
  Mapped HID 0x2D/0x2E/0x56/0x57.
- **Zoom re-anchor was one frame early**: the wheel handler runs before the new grid
  scale loads (function top reads cfg), so the re-anchor math ran against the OLD row
  height - the misaligned jump the user screenshotted. Now the ratio applies the frame
  AFTER the wheel (compounding across queued notches), measured from the frozen header's
  bottom (rowsTopY) rather than the window top.
- **Tooltips bulleted**: mash + brush help markers reformatted as "-" bullet lines with
  blank-line groups (plain text is all ImGui tooltips can do - no rich formatting).
- Clarified for the record: mash patterns use FORWARD slashes, one per frame advance -
  "///" = three advances = two neutral frames between the tokens.

**In-frame zoom + sender UX order (user, 2026-08-24).** Ctrl+wheel "double duty" solved
for real: ImGui provably never scrolls while Ctrl is held (UpdateMouseWheel early-outs),
so the perceived scroll was the DEFERRED re-anchor leaving a transient unanchored frame
during continuous wheeling. The zoom now resolves entirely IN-FRAME at the table: read
wheel -> save scale -> re-apply SetWindowFontScale -> re-anchor SetScrollY, one spot, no
cross-frame state (FontAllowUserScaling machinery deleted). Layout, per the user's
numbered plan: the heat map moved to sit DIRECTLY above the table - controls first
(toolbar, macro line, REPLACE/BRUSH, MASH), then History/selection/paste/surgery rows,
then the movie overview, then the grid. And the sender pipeline unification begins:
MASH "-> clip" stages pattern x reps to the clipboard as macro text, so Paste Replace /
Insert / Append become general placement tools (their tooltip notes paste writes BOTH
players; the mash-local buttons remain the player-preserving path). NEXT for the sender
UX: player-preserving paste (merge mode), and renaming the paste row as the generic
"place" row.

**MASH sends inline (user, 2026-08-24).** The "-> clip" detour lasted one screenshot -
the user filled out pattern/player/reps and expected the HOW right there. The bar's send
cluster is now the same grammar as the paste row, player-preserving and immediate:
**Replace @cursor** (overwrite pattern x reps ahead; Enter = this), **Insert @cursor**
(raw rows through tasInsertRows/resize funnel - tail shifts, other player neutral on the
new rows, savestate awareness toast), **Append @movieEnd**, **fill sel** (loop across the
selection). "-> clip" is removed. The disabled cluster explains itself on hover ("Pause
first..." / "Read-only - press R" - the grey-out that confused the user was the pause
gate). The clipboard route for merge-paste remains future work.

**Mash target picker + group delineation (user, 2026-08-25).** The send row gained a
TARGET: "@ 2345 (cursor)" / "@ 2347 (sel)" combo that every verb aims at (the user
selected a non-active frame and wanted the inputs THERE) - Replace/Insert send to the
target, Append stays movie-end, fill sel stays selection-loop; the picker falls back to
cursor when the selection dies. Verb labels slimmed (the target carries the frame).
History moved BELOW the heat map - the timeline group (overview + history) is now the
last thing above the rows. And tasGroupSep() draws a thin blue rule between the module's
three logical groups: construction (REPLACE/BRUSH/MASH), selection-owned tools
(info/copy/paste/surgery), timeline (heat + history).

**Delegated polish round (user, 2026-08-25; implemented+verified by subagents).** Six
items from screenshots, each independently verified 6/6 PASS: blue REF label fronts the
Sels/Marks cluster; the four mash verbs (Replace / Insert / Append / Fill Selection)
moved to their OWN row under the MASH bar (the blue group rule states the ownership);
the target combo widened to 150px for breathing room; capitalizations (Fill Selection,
NO SELECTION, FRAME x/y); grey pipe separators between the toolbar's logical clusters
(Step | Follow | FRAME | badge+ARMED+trash | Undo Redo); help-marker bullet updated.
Post-verification fixes: the verbs' disabled tooltip now wraps the whole group
(BeginGroup - it only covered the last button before), and a stale comment.

**Editable mash templates + user hand-edits (2026-08-25).** The user now edits
dojo_gui.cpp directly (their casing/naming pass - "Selections"/"Bookmarks"/"Places at
Frame"/"(Active)/(Selection)" - reviewed clean and committed; the target dropdown ITEMS
harmonized to match). Answered: C++17 -> raw string literals R"(...)" ARE the
template-literal equivalent; the mash helpmarker converted as the demonstration. Build
workflow: edit -> .uild.ps1 (incremental) -> .
un.ps1 / launch.ps1 (launch does NOT
compile - that was the missed step). TEMPLATES became a real, editable store:
data/mash_templates.txt ("name = pattern" lines, '#' comments), seeded with the
built-ins on first run, re-read every time the dropdown opens, '+' button appends the
current pattern under a typed name. THE LINKING VISION (user, for the Shuma-Gorath /
Magneto HyperGrav 1-frame chaos-dimension setup - activate, wait ~87f, call assist,
wait 5f, jump 7f, HP): shapes to connect = mash templates (short loops, cross-clip file)
+ macro .txt files (long timed sequences - the Shuma setup is THIS; Save-selection-as-
macro already captures content cross-clip) + Selections (per-clip ranges) + Bookmarks
(per-clip anchors). Queued: "selection -> mash template" derivation, a template picker
inside Load Macro's flow, bookmark-targeted placement (send @ bookmark), and a shared
library folder for famous setups.

---

## THE SEQUENCE LIBRARY - the ASCII Pad V Pro phase (user priority, 2026-08-25)

**Declared BEFORE branching on purpose** (user: "I know we were supposed to add A-B
support, like branches and forking, but I want to focus on this stuff before that since
branching will be able to use this as well"). A branch is a clip + a BASE + sequences
applied to it - so the library is branching's raw material, built first.

**The reference machine**: the ASCII Pad V Pro (~2004), which the user knows inside and
out - per-button turbo/auto-fire, programmable macro buttons, speed control. The goal of
this phase is that every function that pad had exists inside the piano roll.

**The two-tier store, settled:**
- **Patterns** (mash tier): short loops. `data/mash_templates.txt`, one player per entry
  today. Cross-clip, hand-editable, dropdown + '+' save. SHIPPED.
- **Sequences** (template tier): timed, arbitrary-length, and - the user's key point -
  potentially BOTH players (P2 is Shuma/Magneto in the chaos-dimension setup). These are
  macro .txt files (CE format already carries both tracks per line); what they lack is a
  LIBRARY around them instead of raw file dialogs.

**The rungs:**

**L1 + L2 SHIPPED (2026-08-25).** The MASH bar is two stacked pattern rows - P1 (blue)
and P2 (red), an empty row leaves that player untouched, tracks pad with neutral to the
longer one per rep (CE line-pairing semantics). **Swap** exchanges the tracks; **Flip**
(checkbox) mirrors L<->R on both tracks at send time (tasFlipCanon; numpad diagonals
mirror for free). Templates carry both tracks ("name = p1 | p2"; single-track files stay
valid; '+' saves both; seed documents the form). The SELECTION got the table-side twins:
**Swap Players** (exchange the 12-byte halves per selected row) and **Flip L/R** (packet-
level DC_DPAD_LEFT<->RIGHT mirror, both players) - plus context-menu entries. THE
STALENESS STORY, verified by design (the user's explicit worry): both are content edits
through ApplyEdit, so the guard fires at the first real diff, kept states go orange/grey
with the trashcan unchecked, and because swap-twice and flip-twice are IDENTITIES, doing
the op again restores the exact bytes and the sidecar-v3 prefix hash REVALIDATES the
states - the round-trip A/B workflow costs nothing permanently. The old single-track
placer was deleted (tasMashPlace2 is the only path). BRANCHING HOOK RECORDED (user):
"Go into New/<Named> Branch from this point" - flip/swap a selection INTO a fresh branch
- fits perfectly once branching lands; the library pieces are its inputs.

**L1 - Two-track mash.** The MASH bar becomes two pattern rows, P1 and P2 (one may be
empty = that player untouched, composable with existing rows). **Swap Players** button
exchanges the tracks ("this will let us swap the players if we want to flip the
commands"). Template file format grows a two-track form (e.g. `name = p1pat | p2pat`);
single-track entries stay valid. All send verbs (Replace/Insert/Append/Fill Selection,
target picker) apply to both tracks at once; a track only writes its own player's half.

**L2 - Flip Directions.** A mirror transform (canon-level CANON_LEFT <-> CANON_RIGHT;
numpad diagonals mirror for free at canon level; U/D untouched) available at EVERY
placement site: mash sends, Load Macro, Paste tools. "So we can try combos on the left
and right." One function (tasFlipCanon), one checkbox surfaced consistently.

**L3 REDEFINED + L4 SKIPPED (user, 2026-08-25).** After L1/L2 hands-on: "I foresee
confusion with Template/Mash/Macro/Selection... we will have to spend some more time on
this setup before moving into L4... L5 is more germane after L3." So L3 now leads with
UI/terminology consolidation, L4 (Pad V Pro mapping) is DEFERRED, and L5 (interlinking)
follows L3 directly. The L3 UI ladder, from their screenshots:
- MASH renamed MANUAL; its second row gets grey pipes + a "Loops" label on the reps int
  (Swap stays a button = immediate action; Flip stays a checkbox = persistent send-time
  mode - the difference is the point);
- REPLACE moves INTO the selection module (it is a selection op): row 1 = info | Save
  selection as macro | Deselect | REPLACE cluster;
- row 2 = Copy + the three Pastes (copy belongs with paste);
- row 3 = content transforms: Clear | Swap Players | Flip L/R | Repeat xN;
- row 4 = structure: Insert N / Duplicate / Delete (the "4th row to really separate all
  this out logically");
- the '+' save-template button and the Template/Manual/Macro/Selection vocabulary get a
  naming re-assessment as part of L3's library work.

**L3 additions (user, 2026-08-25, second screenshot round).** Row 3/4 regrouped (SHIPPED):
row 3 = erasers + transforms (Clear | Delete | Swap Players | Flip L/R), row 4 =
add/multiply (Insert N | Duplicate | Repeat xN after | Repeat xN append - the append twin
was the parity gap the user spotted). Two L3 design items queued from the same round:
- **The verb-parity matrix**: families x verbs. Today - Paste {Replace, Insert, Append},
  MANUAL {Replace, Insert, Append, Fill Selection}, Repeat {after, append}, row ops
  {Insert-N-before, Duplicate, Delete}. Decide with the user which remaining cells are
  real (e.g. Paste Fill-Selection? Repeat insert-shifting? Insert-N *after*?) and which
  are noise - parity for its own sake breeds clutter.
- **The template manager**: mash_templates.txt entries need in-app edit + delete + TAGS
  ("figuring out how to library-ize this") - one list UI over the file, and the same
  treatment later for the sequences folder. Hand-editing the txt remains first-class.

**L3 label pass (user, 2026-08-25, third screenshot round). SHIPPED.** Every module row
now carries its blue name, top to bottom: **CONTROL** (Resume/Step/Follow/FRAME/badge/
ARMED/trash/Undo) - **MACROS** (Load/Save Macro | Places at Frame | RANGE | REFERENCE
Selections+Bookmarks - REF renamed and moved up to the "broad intent" line; popups stay
below, same ID stack) - BRUSH - **MANUAL** (P1 row / P2 row / modifiers row [Swap | Flip
| Loops | @target - their own dedicated row] / **INPUT** verbs row) - **SELECTION**
(label + compact "N in a..b" info, "Save as Macro" shortened, anchor widened to 230px) -
rows 2-4 - timeline. The panel reads as named modules now.

**L3 round 4 (user, 2026-08-25). SHIPPED + the staged-macro contract.** RANGE moved into
the SELECTION module (it BUILDS selections - the user could not tell what it was doing up
on the macros line); MACROS slimmed to "Save Macro | Load Macro || REFERENCE" (Save now
left of Load per request; "Places at Frame" text deleted - Load's tooltip carries it);
the INPUT label removed (MANUAL alone reads fine; the label also landed on the wrong row
- ImGui SameLine flow, noted). THE NEXT L3 STEP, user-specified: **the staged-macro
pipeline** - Load Macro becomes single-responsibility (load ONLY, no placement); a loaded
macro becomes visible STATE, and the SELECTION module changes UI to show (1) a macro is
loaded (name, frames, players) and (2) placement tools: frame target, Replace / Insert /
Append / Repeat / Flip - "that will be our pipeline", killing the duplication between
Load-and-place, Paste, and MANUAL sends. Before building it: a VISUAL-LANGUAGE
conversation (user wants a color system for process states - e.g. staged/armed/applied).

**THE STAGED-MACRO PIPELINE SHIPPED (2026-08-25).** Load Macro is load-ONLY (works in
ANY mode - loading touches nothing). A staged macro is visible STATE in the agreed amber
"awaiting action" language: a pulsing name next to Load Macro up top, and a pulsing
amber-ringed STAGED row inside SELECTION carrying the whole placement pipeline -
[name (N frames, P1/P2/both)] | @ target (Active/Selection) | Flip + Loops | Replace /
Insert / Append / Fill Selection | X unload. The two pulses share one clock - the
logical-connection visual the user specified. Placement details: Replace/Append =
tasMacroPlaceFrames (full-frame, both players); Insert = raw rows through the resize
funnel (savestate toast); Fill = tile-and-truncate to the selection span; Flip mirrors
both tracks at placement; the macro STAYS staged after placement (repeatable) until X.
tasMacroLoadPlace deleted. NEXT (user-declared detour before more L3): the selection
UX refinement pass - (1) how to select (Range, single frame), (2) the selection verb
inventory (clear/delete/copy/save-as-macro/bookmark/save-selection/duplicate...) - then
an aesthetic pass to reclaim UI space.

**Selection-UX detour, round 1 (user, 2026-08-25). SHIPPED.** The staged row split into
THREE ringed rows (identity / options / verbs) with the X now RED (cancel color, top
right of the cluster). Content-aware options (user: "detect... only P1, only P2, or
both - then the UI... can have context"): the macro is analyzed once at load; a
single-track macro shows "as [P1/P2]" retargeting (native marked, colorized), a
both-track macro shows a placement "Swap" checkbox, and the work builder applies
retarget/swap before Flip. New selection verb: **Copy P1>P2 / P2>P1** (copy one player's
inputs onto the other for the selected rows) - row 3 buttons + context-menu entries,
same funnel/staleness/revalidation story as Swap/Flip. Still open in the detour: the
how-to-select audit (Range, single frame), the verb inventory pass, and the
space-reclaiming aesthetic round.

**Select audit round 1 (user, 2026-08-25). SHIPPED, smoke-tested only per request.**
- **THE 720px RULE**: the roll's floating window is hard-constrained to 720px max width
  (user measured at 1920x1080); docked width stays user-dragged but 720 is the design
  budget every row must fit.
- **SELECTION row 1 = the SOURCES row** ("how to select"): info | RANGE lo-hi + ONE
  stateless Select/Deselect toggle (label reads haveSel, so a manual deselect in the
  piano flips it automatically - the user's broken-button worry answered by having no
  stored state at all; typing bounds commits a select via IsItemDeactivatedAfterEdit) |
  Selections (MOVED down from REFERENCE - stored selections are a selection SOURCE).
  REFERENCE up top keeps Bookmarks only.
- **Row 2 = "do what with this selection"** (the user's 2A): Save as Macro | Copy |
  Save as Selection (opens the popup name-field-focused via selPopupWantSave) || REPLACE.
  The paste row sheds Copy. Deselect button folded into the toggle (menu entry stays).
Detour continues: verb inventory completeness check, then the aesthetic/space pass.

**Select audit round 2 (user, 2026-08-25). SHIPPED, smoke-tested.** Row 2 order: Copy |
Save as Macro | Save as Selection || REPLACE. Selections popup UX: saving AUTO-CLOSES
the popup with a toast (the user saved and the window just sat there); per-entry delete
is a TRASH icon now in both popups (the "x" read as close-window). REPLACE combos use
the FA arrow glyphs for directions when arrow style is on (the text carets were the OLD
glyphs). BOOKMARKS BECAME SPANS: {name, frame, end} (additive json - old single-frame
clips load unchanged); a span marks EVERY gutter frame (gold number + dot + tooltip
"#N name" with the bookmark's index), renders as a translucent BAND on the heat map,
lists as "#N name @ lo..hi", and is created via "+ sel a..b" in the popup or the
selection-aware context-menu item ("Bookmark selection a..b"). Jump goes to the span
start. The user's super-flash example works: select 5 frames, bookmark, the marker rides
all 5 rows.

**Select audit round 3 (user, 2026-08-25). SHIPPED, smoke-tested.** Seven items:
bookmark adds auto-close the popup (parity with Selections); Copy blinks green on press
(feedback ON the button, plus tooltip - the how-to-copy expansion is noted for later);
"as" dropped -> Save Macro / Save Selection (##ids disambiguate from MACROS' Save
Macro); saving a selection blinks Save Selection AND the Selections source button in
sync (shared tasSelSavedFlashAt - the visual link between "saved" and "where it
lives"); both popup lists auto-size (Selectable (0,0)) so long names and 4-digit frames
never clip, and selections show their real span "name a..b (N rows)"; BOTH lists are
newest-first now (bookmarks insert at front + file order = display order; selections
via an additive "selectionsOrder" array in clip.json, alphabetical-legacy names fall to
the end); heat-map endpoint labels are bright white with a drop shadow.

**Select audit round 4 (user, 2026-08-25). SHIPPED, smoke-tested.** Clarified: bookmarks
ARE persistent (clip.json "bookmarks", same as selections). **VSCode-style bookmark
slots**: Ctrl+Shift+0-9 SETS "slot N" (the selection span when one exists, else the
cursor frame; re-set overwrites), Ctrl+0-9 JUMPS to it (empty slot toasts the set-chord).
Slots are ordinary named bookmarks - same list, gutter gold, heat band. The digit keys
were already ImGui-mapped (the startup-prompt fix) and the emulator's kb maps use
letters, so the chords were free; works whenever the roll is open, gated only on
!WantTextInput. Discoverability: hint line atop the Marks popup + the Bookmarks button
tooltip. Heat map: stripH 34->42 with a reserved 13px label band - bars never enter it,
endpoint labels fully opaque white with outline. NEXT: "when we have a selection, what
do we let the user do" - continued.

**Trash-overlap fix (user bug, 2026-08-25).** Round 3's popup auto-width introduced a
hit-box bug: a (0,0)-sized Selectable spans the FULL row width, so it sat underneath the
trash button - one click deleted AND jump-closed the popup in tandem, which read as
"delete broke the UI". Fix: both popups' Selectables are sized to their VISIBLE text
(CalcTextSize with the ##id hidden) + padding, so the trash owns its own pixels; delete
now deletes only, and the popup stays open. Also: slot overwrite announces itself -
"Slot 1 MOVED: 2334..2340 -> 2346 (overwritten)" - the intended squash semantics, now
spoken (the silent version read as a bug).

**Transform row promoted (user, 2026-08-25). SHIPPED, smoke-tested.** Settled the
Swap-vs-P1P2 question: Swap Players = EXCHANGE (that IS the user's "P1<->P2" - now ONE
button labeled with the FA right-left glyph); P1>P2 / P2>P1 = one-way CLONES (source
untouched; now labeled with FA arrow-right, tooltips shout the difference). The
transforms moved ABOVE capture per the user's spec. Selection module reads top-down:
1 sources (info | RANGE toggle | Selections) / 2 TRANSFORM (REPLACE from>to Apply ||
P1<->P2 | Flip L/R | P1->P2 | P2->P1) / [amber STAGED row] / 3 capture (Copy | Save
Macro | Save Selection) / 4 paste (x3) / 5 erasers (Clear | Delete) / 6 structure
(Insert | Duplicate || Repeats). NEXT: the paste-row half of "what do we let the user
do".

**Active/Selection color language + staged-as-selection (user, 2026-08-25). SHIPPED.**
- **The two running selectors are colorized module-wide**: ACTIVE = the playhead's GOLD
  (TAS_ACTIVE_COL, same as the grid's cursor row), SELECTION = the module BLUE
  (TAS_SELECT_COL, same as the row tint and the SELECTION label). Applied to: the
  toolbar's FRAME counter (gold), both target combos' previews AND dropdown items
  (gold/blue per choice), the bookmarks popup's "+ @ N" (gold) and "+ sel a..b" (blue).
  Anything offering the Active-vs-Selection choice now wears the answer's color.
- **Row order**: sources -> ERASERS (moved up: "the logically simplest options") ->
  TRANSFORM -> [STAGED] -> capture -> paste -> structure.
- **Staged = the extant selection** (user's model): while a macro is staged, SOURCES,
  ERASERS and CAPTURE mute (grey) - the flow is transform/place/structure or unload;
  the GRID's mouse selection stays live (Fill Selection remains reachable). The bright
  red X is DUPLICATED next to Load Macro at the very top, alongside the staged row's.

**Staged consolidation: the box is GONE (user, 2026-08-25). SHIPPED.** "Guide the user
into the existing buttons... remove this special box... fewer lines of code that we must
maintain at parity with the main module." The staged macro now rides the MAIN rows:
- **The STAGED box -> one slim line** (pulsing ORANGE `STAGED name (N frames, P1) -
  transform above, place below` + red X) between capture and the placement row. All of
  the box's private controls (target picker, Flip checkbox, Loops field, Swap/as-player,
  its own four verbs) are DELETED along with their statics.
- **The transform row drives the staged BUFFER**: while staged, REPLACE-Apply does a
  canon-level replace inside the macro (TAS_CANON_OF_COL maps roll columns to canon
  bits), and P1<->P2 / Flip L/R / P1->P2 / P2->P1 rewrite `tasStaged.frames` directly.
  Buffer edits are legal in ANY mode (no movie touch, no guard) and re-run
  `tasStagedAnalyze()` so the P1/P2 badge stays honest. Toast on every transform.
- **The paste row places it**: labels drop the word "Paste" while staged (`Replace` /
  `Insert` / `Append`), target = selection start when one exists else the cursor, and
  the structure row's **[N] is the staged loop count** (the macro is tiled xN through
  `tasMacroPlaceFrames` / `tasInsertRows` - the SAME guarded funnels as everything else).
- **Module gate relaxed**: the rows below the erasers disable on `!haveSel &&
  !stagedActive` - a staged macro with no table selection keeps transform + placement
  live. Structure row's actual row-ops (Insert/Duplicate/Repeats) still demand a real
  table selection; only [N] stays free.
- **Colors**: TAS_AMBER pushed to true ORANGE (1.0, 0.55, 0.15 - it sat too close to the
  Active gold), bookmarks now GREEN (TAS_BOOKMARK_COL 0.30/0.80/0.42, VSCode-style):
  gutter frame numbers, the row dot, and the timeline bookmark bands.
- **Disabled-reason audit (user: "check ALL the buttons that are greyed out")**: every
  disabled surface in the TAS window now explains itself on hover - Undo/Redo (mode /
  empty stack), erasers (mode vs no-selection, previously LIED "Pause first" when the
  real reason was no selection), transform cluster, capture trio, placement trio,
  structure clusters (grouped so ONE tooltip covers each cluster), MANUAL's Fill
  Selection, bookmarks' "+ sel". Staged-mute groups say "a macro is STAGED - it IS the
  selection" so the user is steered to transform/place/X.
- Wall: test.ps1 PASS, resizeguard PASS, textguard PASS (guardtest skipped - known
  focus flake mid-batch, coverage overlaps textguard).

**Staged polish round (user screenshots, 2026-08-25). SHIPPED.**
- **Slim STAGED line deleted** ("too redundant" - the MACROS row up top already names
  the staged macro with its own X). The ONE surprising state keeps an in-line warning:
  read-only + staged shows "READ-ONLY replay: you can still EDIT the staged macro -
  press R to place it" (transforms work, placement can't).
- **The @ target picker copied down to the placement row** ("so the user sees where
  they will Replace/Insert/Append"): factored `tasTargetPicker()` renders the SAME
  widget in MANUAL and the placement row with SHARED state (`tasSendTarget`) - one
  "where things land" for the whole tool. Append labels show @movieLen (MANUAL parity).
  The placement row no longer needs a table selection (the picker supplies the target,
  exactly like MANUAL's verbs); the module needs-selection gate now ends after capture,
  and the structure row self-gates. The grid's right-click Paste verbs keep landing at
  the right-clicked selection (explicit selLo), ignoring the picker.
- **Pencil = edit mode**: while staged the REPLACE label renders as an orange
  ICON_FA_PENCIL + REPLACE ("this row now EDITS the staged macro"), and a **Save
  Copy** button at the transform row's end writes the edited buffer to a new .txt
  (the originally loaded file untouched).
- ~~NEXT: MACRO EDIT mode~~ **DEFERRED by the user (2026-08-26)**: "I am going to back
  out from doing this whole edit-macro thing... there is still a lot of other stuff I
  want to add, and I cannot tell how that will work out if we do this NOW - we might
  have drifts of bugs that we wouldn't have if we just held off." The STAGED ->
  transform/place pipeline as shipped is declared sufficient for now.

**FUTURE CONCEPT - the "dumb notepad" macro viewer (user, 2026-08-26).** Instead of
MACRO EDIT mode (full piano-roll tooling retargeted at the buffer), a completely
separate, completely DUMB module: a read-mostly notepad-style view of the staged
macro's frames with ONLY the transform row's verbs available (REPLACE, P1<->P2, Flip
L/R, clones, Save Copy). No piano-roll tools, no selections, no savestates, no parity
obligation - "we will just treat it like Notepad with these transforms available."
The user notes the REPLACE row "will make more sense" once this viewer exists (you
can SEE what you are transforming). Design notes kept from the MACRO EDIT talk, since
the notepad inherits whatever the UI/X refactor lands on: the one-table-two-backends
routing argument, the RollViewState swap idea, the orange pulsing border, and "Play
Macro" (savestate -> feed buffer inputs as a transient override, never written to
session_inputs -> auto load-state back; the CE-trainer script feel, costs exactly one
savestate) all remain valid whenever this revives. Parity answer for posterity: with
backend routing, a verb written once (e.g. a future CUT = Copy + Delete) works in
both tools automatically - parity is a property, not a tax.

**Dependency-honest rows + TARGET footer (user, 2026-08-26). SHIPPED.** The UI/X
refactor resumed with the dependency audit ("a few of these options are predicated on
the user using the COPY button"):
- Dependency facts, now enforced visibly: the PASTE verbs are the ONLY clipboard
  consumers - they grey until the clipboard actually holds text (their own Copy, or
  macro text copied from any file - the CE-trainer import path stays alive), with the
  reason on hover ("Copy rows first - the clipboard is empty"). Low-rate 0.5s poll,
  not a per-frame OS clipboard read. The LOOPING/structure row needs NO copy - it
  tiles the SELECTION (its "select rows first" tooltips already said so); the
  transform row runs off the selection or the staged buffer.
- **TARGET footer**: the @ picker moved to the very end of the module on its own row
  ("the @ placer should definitely be at the very end on its own row"), labeled
  TARGET - the one shared landing spot for the paste/staged verbs and MANUAL's sends
  (same shared state, rendered in both places). The placement row reads the shared
  state directly (defensive fallback to cursor when the selection died this frame).

**Bookmark lifecycle + right-click menu parity (user, 2026-08-26). SHIPPED.**
- **Bookmarks now follow row surgery** ("Deleting Rows doesn't delete the bookmarks -
  I think that should be the default behavior"): the two GUI funnels grew adjusters -
  `tasInsertRows` shifts every mark at/after the insertion down-file (a mark spanning
  the insertion point STRETCHES - the super got longer), `tasDeleteSel` removes marks
  whose whole span died and pulls the rest up (partial overlap trims the span). All
  insert flavors (paste/mash/staged/duplicate) ride the same funnel, so they are all
  covered; Repeats overwrite in place and never move marks. Slots are ordinary named
  bookmarks and follow the same rules. The delete toast counts removed bookmarks.
  ~~KNOWN HONESTY GAP: undo restores frames, NOT removed bookmarks~~ **CLOSED next
  round** (see below - gui_meta rides the undo history). Still unhooked: the
  text-edit funnel (TextApply) can resize the movie without adjusting marks -
  power-user path, note kept here.
- **Right-click menu rebuilt for module parity** ("very very stale"): mirrors the
  SELECTION module's row order with the module's exact labels - bookmarks (green,
  with **Remove bookmark #N** for every mark covering the right-clicked row - the
  gutter finally has a removal path) / erasers / transforms (P1<->P2 exchange, Flip
  L/R, clones with the FA glyphs) / capture (Copy, Save Macro) / paste (Paste
  Replace/Insert, same clipboard gate as the module, landing at the clicked
  selection) / structure (Insert N before, Duplicate, Repeat xN after) / Deselect.
  "Paste append" dropped from the menu - it never acted on the clicked rows.
- `tasClipHasText()` extracted (cached 0.5s clipboard probe) - one probe feeds the
  placement row and the menu.

**Undo-safe bookmarks + empty finders + Ctrl+C/X/V + section names (user, 2026-08-26).
SHIPPED.** Four asks in one round:
- **Undo restores bookmarks** (the honesty gap CLOSED): `EditPatch` grew an opaque
  `gui_meta` blob; the dojo core captures it via a GUI-registered callback BEFORE
  every edit mutates anything and hands it back when a patch replays (undo AND redo
  are symmetric - the inverse patch snapshots the post-edit state). The piano roll
  registers serialize/apply lambdas (same JSON shape as clip.json). Core stays
  blob-agnostic; old patches (empty blob) are skipped.
- **The empty finders**: the REPLACE combos learned "(empty)" language (user: "Find
  P1/P2 Empty, and replace them with P1/P2 anything"). To-side "(nothing)" renamed
  "(empty)" (= erase the match). From-side gained "P1 (empty)" / "P2 (empty)" -
  match rows where that player is FULLY silent, then write the to-input. Absent
  in-movie frames count as empty (they spawn a frame when there is something to
  write); void rows never do. empty->empty is gated ("writes nothing"). The staged
  buffer's canon REPLACE has full parity.
- **Ctrl+C / X / V** over the focused roll (never while a text field owns the
  keyboard): C = Copy selection, X = **Cut** (Copy + Delete - the bookmark rules
  and toast ride along; the Cut question answers itself), V = Paste Replace at the
  shared TARGET (selection start when one exists, else cursor, needs a non-empty
  clipboard). The right-click menu shows the shortcuts and gained a Cut item.
- **Section names made explicit** (user: "these names help a lot"): every row now
  leads with its internal name in module blue - SELECTION/RANGE (as before),
  ERASERS, TRANSFORM (absorbs the old REPLACE label; pencil variant while staged),
  CAPTURE, PASTE (turns orange "PLACE" while staged - label doubles as state cue),
  STRUCTURE | LOOP (the two halves of the old row 4, finally delineated), TARGET.
- Wall: test.ps1, textguard, resizeguard all PASS (dojo core funnel touched).

**Timeline parity + found-mask + polish round (user, 2026-08-26). SHIPPED.** Items 1-5
of the six-ask round (item 6 - the STRUCTURE/LOOP row's English - deferred, "the final
row might need to be changed around a few times"):
- **Timeline<->roll stale parity audit**: the verdict engine already agreed everywhere
  (gui_slot_stale uses the same 3-arg content-aware IsStateStale as the roll; the
  blink banner and F4 grid were already wired). The gaps were PRESENTATION, now fixed:
  the big "> N" slot line says STALE in the roll's dim red (BASE variant: "STALE -
  hold F1 to re-cut"), "state @ frame N" gains "- STALE (dead timeline)", and the
  digit row's stale color changed from unexplained GREY to the roll's dim red (the
  two panels showed one verdict in two colors).
- **Glyph parity**: the pickers' direction entries switch ICON_FA_ARROW_* ->
  ICON_FA_CARET_* (filled triangles, the language the grid's drawn cells speak).
- **The FROM dropdown pre-searches the working set**: a signature-cached scan
  (rerecord count, selection shape, staged revision) marks which inputs actually
  exist in the selection - or the staged buffer - and the dropdown keeps every
  option but dims the absent ones to 0.28 alpha ("keep the dropdown filled...
  just highlighting to show which ones we found"). The P1/P2 (empty) finders
  participate (lit when silent rows exist).
- **Copy flash from every path**: the stamp moved INTO tasCopySel, so the module
  button lights green for Ctrl+C, Ctrl+X's copy half, and the menu Copy too.
- **Save Selection... joined the right-click menu** via a one-frame popup handoff
  (a menu cannot open a window-scope popup from inside its own popup scope -
  tasMenuWantSelSave defers the OpenPopup to the popup's own scope next frame).

**Copy-flash bug + Paste Fill + 65535 caps (user, 2026-08-26). SHIPPED.**
- **The Copy no-light bug root-caused**: not clipboard dedup (the user's guess) - the
  flash pushed only ImGuiCol_Button, and a just-clicked button renders the HOVERED
  color, which masked the green entirely. Ctrl+C showed it only because the mouse was
  elsewhere. Fix: push Button+Hovered+Active, and the flash now BLINKS twice over
  0.9s so a re-copy reads as a fresh event even mid-flash (re-copying is a comfort
  ritual - honored). Same hover-masking fix applied to the Save Selection <->
  Selections linked blink. Copies stay fully idempotent; the paste row was already
  sane about it (re-paste of identical content = "changed nothing" toast, buttons
  stay lit).
- **Paste Fill** (new verb, PASTE row end): fills the SELECTED rows by cycling the
  clipboard - copied 20 / selected 100 -> 1-20 tile until they cap out; copied 20 /
  selected 10 -> first 10 land. Length-preserving (plain ApplyEdit), skips void rows
  (never extends), needs a selection + clipboard, own reason chain. While STAGED the
  button reads "Fill" and cycles the staged buffer instead (tasFillRowsWithMacro is
  the one shared engine). Non-contiguous selections fill in selection order.
- **Caps raised 999/9999 -> 65535** on LOOP [N] and MANUAL Loops: one specific MvC2
  glitch (deliberate in-game buffer overflow) needs ~14.5 minutes (~65535 frames) of
  waiting to detonate.
- NEXT (user): the STRUCTURE row rework ("take a look at how the Structure Stuff
  works" + the deferred English pass).

**STRUCTURE dissolves: the PAYLOAD x VERB grammar lands (user, 2026-08-26). SHIPPED.**
The design talk crystallized the module's hidden model: everything below CAPTURE is
"take a PAYLOAD, apply a VERB (Replace/Insert/Append/Fill), xN, @ TARGET". Payloads:
clipboard (PASTE), staged macro (rides PASTE), MANUAL pattern, BLANKS, and the
SELECTION ITSELF. A row lights when its payload exists; Fill verbs additionally need
a selection to write into. Under that model:
- **LOOP row** (selection = the payload): `LOOP [N]x Replace | Insert | Append @end`,
  all landing @ TARGET (user: "target anywhere AND choose replace/insert... send
  loops to wherever i want (cursor)"). Replace = tasRepeatSelTo at the target;
  Insert = new raw-byte tiler through tasInsertRows (selection tracks its content
  past the insertion); Append unchanged. Selection-gated, copy-NEVER-needed.
- **BLANKS row** (blanks = a payload, "then i can just verb-it"): own count
  `tasBlankN`, `Insert @TARGET | Append @end`. Blank-Replace/Fill are already Clear
  rows (tooltip points to ERASERS). Append extends the movie: BLANKS 65535 Append =
  the 14.5-minute overflow wait in one press. Needs no selection - "click somewhere
  and X blanks" = @ Active.
- **DUPLICATE parked** (user: "drop it for now... comment it out"): it was the
  selection-payload's Insert verb hardcoded to x1 - LOOP Insert covers it. All
  three surfaces #if 0 / commented (tasDuplicateSel, doDuplicateRows, module
  button + menu item); grep found no other dependents (no harness, no hotkey, no
  Lua). Un-park by removing the #if 0s.
- Menu: "Insert %d before" -> "Insert %d blanks" (tasBlankN, at the clicked
  selection); "Repeat xN after" stays as the in-context quickie.
- The STRUCTURE label and row are gone; final module shape: sources / ERASERS /
  TRANSFORM / CAPTURE / PASTE(+staged PLACE) / LOOP / BLANKS / TARGET.

**THE GRAMMAR AUDIT (user-directed multi-agent sweep, 2026-08-26). SHIPPED.** Three
read-only scout agents audited the whole TAS UI against the module grammar (toolbar /
grid+menu / popups+timeline+F4+settings); an analyzer agent deduped ~65 findings into
a tiered plan; fixes applied by hand. Full findings: the audit_findings.md scratchpad
+ the three agent reports (this entry records outcomes only).
- **Tier 0 correctness**: (1) right-HOLD on the frame gutter ATE the gesture
  (engageRight cleared rcPending then bailed on rcCol<0 - the release could never
  open the menu); now gutter presses skip graduation. (2) The roll's stale-warning
  DELETED-vs-kept tier keyed to the PurgeStale CHECKBOX, not the fact -
  gui_stale_blink_deleted() is now the one verdict (BASE-exempt case fixed).
  (3) MANUAL Fill overwrote the GAPS of a gapped selection (contiguous-span fill);
  it now writes only the selected rows, cycling, player-preserving. (4) Analyzer
  wanted bookmark inserts flipped to append (the "#N renumbering" nit) - REJECTED:
  newest-first is the user's explicit order; stable bookmark IDs deferred instead.
- **Quick wins, everywhere**: MANUAL got the payload tier (sends grey until a
  pattern exists) + enabled-state tooltips + Fill outside the verb group + ", roll
  ARMED" on insert; the right-click menu got blue section names (ERASERS/TRANSFORM/
  CAPTURE/PASTE @ N/BLANKS-LOOP) + hover reasons on every gated cluster + the
  "[N]x" formats + a void-row gate on bookmarking; popups got names (SELECTIONS/
  BOOKMARKS/TEMPLATE), colored rows, recall-auto-close, delete toasts, trash
  tooltips, no-clip reasons in priority order, and a payload gate on template save;
  the chords (Ctrl+C/X/V) and Ctrl+A toast their no-ops/results; the heat strip is
  named TIMELINE with a scrub/zoom tooltip and ONE position denominator (endpoint
  label now shows the LAST frame index, not the count); HISTORY went blue with a
  Ctrl+Z hint; single-cell no-op toggles say "changed nothing"; classic-paint
  gets the left-click failure toast pattern [pre-existing right-path only];
  Arm/Armed/staged-X flashes push all three button colors; stale-warning colors
  unified (TAS_AMBER + the dim red); TAS_MODULE_COL named and swept (15 uses);
  cursor/selection row bgs + P1/P2 tints + bookmark literals derive from the
  constants; "Gold in the gutter" tooltip and three stale comments -> green;
  Settings says "Caret glyphs" and SAVESTATES & MOVIES uses tasSecHdr; the F4
  compact board finally shows staleness (fill + digits dim red), the details pane
  uses gui_slot_stale + the dim red, Load (F3) greys on empty slots, one backups
  phrasing; Save Macro (toolbar) gates on movie + explains its WHOLE-movie payload.
- **Verified, documented, not changed**: paint growing past the movie end through
  ApplyEdit is legitimate (growth APPENDS like live recording; only shrink needs
  the rewrite funnel) - comment added at the paint commit.
- **DEFERRED to the owner** (analyzer Tier 2): menu Append/Fill completeness +
  staged label swap; pressed-cell Header-tint collision (pick an ON-cell color);
  the RollDirArrows=off ASCII "^v<>" path (retire or caretize); History clickable
  walk-back; shared TAS palette header for gui.cpp; tasBookmarkRemove extraction;
  bookmark-popup Enter targeting (Active always vs the shared picker); mashReps vs
  tasRepN merge; packing-loop dedup; tasMashPlace2 blessing; Settings vocabulary
  (TARGET/LOOP/BLANKS/BaseHoldMs/PurgeStale entries); Overlay size+opacity
  co-location; stable bookmark IDs. REJECTED (deliberate design): [REPLAY] badge
  green, [REC] naming, menu (exchange)/(clone) suffixes + ellipses, white heat
  cursor (contrast over doctrine), heat-bar orange (predates staged amber),
  stroke-preview literals.

**L3 S1 - THE SEQUENCE LIBRARY SHIPS (user-designed, 2026-08-26).** The design talk
settled: **hybrid storage** - `data/sequences/*.txt` are the shareable payloads (pure
CE codec, archives drop straight in and SELF-REGISTER on scan), `library.json` is the
single metadata home (user: "keep everything in the JSON... a powerful library once we
start to import and amass"). Identity nuance the user called: identical CONTENT is
allowed as separate entries (byte-equal setups can serve different tracked purposes) -
the FNV hash powers duplicate NOTICES and filtering, never forced merges. VARIANTS
dropped from scope (user); instead **use-stamps**: placing a library-staged sequence
stamps `{clip, frame, len, date}` onto its entry - the window's USED IN list shows
them with per-stamp removal ("tag-stamping a clip with the option of removing that
library-stamp"). A stamp is bookkeeping at placement time, not a live link.
- **The window** (user: "like the F4 State Page with borrowed Replay-opener modules"):
  `Sequences (N)` on the MACROS row toggles a docked-style page - filter bar (Library
  | This clip scopes, name/tag search, Rescan), list with green tag lines, details
  pane (tags + notes editors committed on field-exit, duplicate-content notice,
  Stage / two-step Delete, USED IN with stamp removal), and a bottom adder ("+
  selection" -> new entry with name + tags). THIS CLIP scope lists the clip folder's
  .txt macros with direct Stage and a +Lib promote button.
- **Staging IS the placement story**: the library never grew placement code - Stage
  routes through tasMacroStageFile into the staged pipeline (pencil TRANSFORM, Flip,
  xN, colored verbs @ TARGET). tasStagedLibFile links the staged buffer to its entry
  for stamping; the X clears it.
- **ACCEPTANCE PASSED IN THE USER'S HANDS (2026-08-26)**: the Chaos Dimension bug
  (Shuma + Magneto-B, Cable dummy cornered) captured via "+ selection", dummy
  switched, staged, placed, ran - "It worked exactly as I wanted." First archive
  imported by drag-and-drop; USED IN stamped.
- **QUICK-PLACE shipped** (user: "quickly applying a combo from building blocks"):
  the details pane's action row gained `Replace @N | Insert @N` in the TARGET's
  color - place a sequence at the shared TARGET with NO staging detour (Stage
  remains the transform path). Insert packs raw rows through the resize funnel;
  both stamp USED IN. Follow-ups: folder icons everywhere (clip dir from roll +
  docked Timeline, sequences dir from the window), drop-import gated to the open
  Sequences window (SDL2 cannot report the drop POINT on Windows), width-draggable
  list pane.
- **THE USER'S THREE NAMED NEXT TOOLS** (2026-08-26), all feeding this system:
  1. **Notation switcher - SHIPPED (2026-08-26)**, spec locked by the user: styles =
     NUMPAD (home notation), CARDINALS (D/DR/R/UR/U/UL/L/DL - and they PARSE now,
     round-trip), GLYPHS (caret triangles; diagonals compose from two carets - FA
     free has no diagonal arrows, a font-range addition upgrades later, see the
     FONTS.md notes), CE LETTERS (tas_macro::ToText verbatim, byte round-trip).
     Buttons always LP/LK/HP/HK/A1/A2 - NEVER MP/MK (four attack buttons; "MP" is
     the reader's press-LP-twice convention) - but the parser now ACCEPTS MP->LP,
     MK->LK, AA->A1, AB->A2 as courtesies. '+' joins in-frame, '.' = neutral, '/'
     RESERVED as the horizontal separator (current mode: vertical, 1 row = 1
     frame). One shared persisted state (dojo:Notation) like the TARGET; combo =
     tasNotationCombo, engine = tasNotationFrame/tasNotationMacro. FIRST CONSUMER:
     the Sequences details pane's NOTATION preview (combo + clipped line view +
     Copy-text). The notepad tool consumes this next.
  2. **Notepad / text-editor - SHIPPED (2026-08-26)**. The pencil button on a
     library entry opens it. The buffer is the file RENDERED in the chosen text
     notation; switching notation CONVERTS the buffer in place (comments ride
     along; a parse error keeps the old style with an amber banner); Save
     re-parses and writes the file as canonical CE letters WITH comments -
     archives stay compatible - then refreshes the entry (frames/players/hash).
     Undo/redo, Ctrl+A/C/V/X, Shift/Ctrl+arrows, click: all free from ImGui's
     stb_textedit. GLYPH style = read-only view (carets cannot be typed) with
     code-comment-green notes. **THE COMMENT SYSTEM lives in the CODEC** (user:
     "we have to persist a rule"): '#' to end-of-line stripped by
     tas_macro::FromText itself - without it, capitals inside comments would
     letter-map into REAL inputs. Rules: "WC #note" = frame + note; a
     comment-ONLY line is an ANNOTATION, not a frame (a header block must not
     shift the combo); ". #note" pins a neutral frame WITH the note (the dot
     forces the frame). '/' in a token line expands to several frames. v1 cap
     ~128KB of text; re-saving normalizes neutral CE lines to "." (semantically
     identical, and the user's preferred empty-row visual).
     **Round 2 (same day)**: GLYPH view is now CLICK-TO-EDIT (the TRANSFORM-combo
     UX carried over, per the user): click a frame row -> picker popup with a 3x3
     caret direction pad + button toggles per track + the comment field; edits
     re-render the buffer (one source of truth; multi-frame "/" lines normalize
     to 1-per-line on first edit). LIVE DIAGNOSTICS strip: red parse errors
     (line-numbered), amber warnings (unrecognized chars dropped on save;
     CE lowercase-reads-as-CAPS note), green all-clear with counts. VSCODE KEYS
     via the input-text callback: Alt+Up/Down move line/block, Shift+Alt+Up/Down
     duplicate-with-copy-selected (repeated presses propagate a mash - the
     user's old trick), Ctrl+D token-select/step-next-occurrence, Ctrl+F
     find/replace (Next/Replace/All). **DEFERRED - the real-editor tier**: true
     multi-cursor (Ctrl+Alt+Up/Down), in-editor line gutter, in-editor syntax
     color - stb_textedit is single-cursor/single-color by design; adopting the
     santaclose ImGuiColorTextEdit fork (has multi-cursor + Ctrl+D semantics)
     is a VENDORED-DEPENDENCY decision for the owner. The glyph view's frame
     numbers + green comments cover gutter/highlighting meanwhile.
     **Round 4 - THE TWO-PANEL NOTEPAD (user design, 2026-08-26)**: P1 left /
     P2 right, one editor per player - plain tokens unambiguous, active panel
     wears its player-colored border, keys route to it alone. Mixed CE rows
     ("WXUI") split into the right panels on open (the alphabet IS the player);
     save zips frame-by-frame (shorter side pads neutral) into merged CE lines;
     annotations live LEFT with "#" alignment stubs on the right; the 2col
     toggle re-shapes through the merged parse losslessly; CE stays 1-col.
     One source of truth: tasNotepadCurrentLines (parse panels -> zip) feeds
     lint/save/convert/glyphs. ALSO shipped same arc: P1/P2 line prefixes +
     content-derived track widening (DATA-LOSS fix - token renders dropped the
     other player), glyph-view player colors + header, arrow-free multicursor
     (+cursor buttons, Ctrl+Shift+D duplicate), the NOTEPAD KEY arbiter log
     (proves whether mod+arrow combos even reach ImGui - GPU rotation-hotkey
     suspicion), text zoom (Ctrl+wheel, Ctrl+=/-), the selected-rows counter
     (the wait-counting trick), Ctrl+D first-press word-select patch, and the
     Alt-modifier key-map fix (io.KeyAlt had NEVER been fed - every Alt combo
     in ImGui-land was dead app-wide before this).
     QUEUED (user): share the notepad with Load Macro (open/edit/save arbitrary
     macro files, not just library entries); panel scroll-sync.
     **Round 5 - THE MULTI-AGENT NOTEPAD AUDIT (ultracode, 2026-08-26)**: the user
     called a Workflow (3 read-only scouts -> synthesize -> adversarial challenge,
     5 opus agents). It root-caused symptoms that surface fixes had masked:
     (1) the "washed out / transparent" colors were a BYTE-ORDER bug -
     SetPaletteColor stores raw into the editor's native 0xAABBGGRR palette but
     the flavor values were 0xRRGGBBAA, so every token rendered byte-swapped and
     near-transparent. Fixed: native IM_COL32 throughout; panel backgrounds now
     use TAS_P1_COL/TAS_P2_COL @ 0.32 via GetColorU32 (the roll's own colors).
     (2) Ctrl+Alt+Arrow was NOT the GPU driver I'd speculated - a verified SDL
     bug: core/sdl/sdl.cpp's mouse-capture chord `break` swallowed every key held
     under Ctrl+Alt, so the arrow never reached ImGui. Gated on
     !io.WantCaptureKeyboard. (3) H1 silent P2 wipe: the glyph frame-editor popup
     read panel 0 only then committed both, zeroing P2 in 2col - now reads both
     via tasNotepadCurrentLines. (4) find/replace + add-cursor buttons hardcoded
     panel 0 - routed through new npActiveEd(). (5) 2col now works with CE letters
     (per-track disjoint-alphabet render + parse masking; WXUI round-trip
     verified). (6) tooltips truncated. The challenger correctly overruled the
     synthesizer's "confirmed app bug" overclaim and flagged the byte-swap was
     class-wide (all flavor fields), not just the backgrounds.
     DEFERRED HOLES (audit, not blockers): H2 - tasNotepadEvenPanels evens by
     LINE count but token-style '/' expands a line to many frames, so equal lines
     != aligned frames (CE/numpad/cardinal without '/' are safe; token '/' can
     mis-pair the zip). H3/H4 - evenPanels runs during the glyph VIEW and can
     insert a '.' under the cursor mid-type. H6 - sel-count/find one-frame
     npActivePanel staleness. P2-comment-drop (right panel emits bare '#' stubs).
     Address if they surface.
     **Round 6 - polish + 2nd audit (2026-08-26)**: color pickers (custom bg/text,
     persisted), glyph view shows two columns in 2col + tinted bg, terse help
     (agent rewrite, 32->13 lines), CE-letter legend surfaced on the notation
     combo tooltip (found already in tasmacro.h:12-15, verified vs KEYS[]).
     Ctrl+Alt+Up confirmed OS-level (Intel rotate-screen hotkey) - +cursor button
     is the path. A 2nd multi-agent workflow (terse/CE/architecture) also mapped
     the SEQUENCES<->NOTEPAD architecture - see below.

**FUTURE - the per-frame INPUT SENDER (user, 2026-08-26).** The glyph-view frame
editor (a floating popup with a caret direction pad + LP/HP/LK/HK/A1/A2/ST toggles
+ #comment, scoped to one player via npEditTrack) is the "input sender" the user
likes: "the floating module that sends inputs per Frame. I think we will do
something more with this in the future. I might use it for the piano roll as well."
NOTE: factor the direction-pad + button-toggle widget (currently the inline
trackPad lambda in tasNotepadWindow) into a reusable component so the piano roll
(and staged/MANUAL) can pop the same per-frame input editor. Candidate: a
`tasFramePadEditor(u16& canon, player, colors)` helper returning changed.

**NOTEPAD <-> SEQUENCES: extend notepad to arbitrary macros (agent-mapped plan,
2026-08-26).** The user wants the notepad to open/edit/save ARBITRARY .txt macro
files (not just library entries) and suspected stale code. An architecture agent
mapped it; the plan:
- **The only coupling** blocking arbitrary-file editing: notepad opens ONLY via
  tasNotepadOpenEntry(SeqEntry&) and saves ONLY to seqLibDir()/notepadFile +
  the seqLib-refresh loop. notepadFile/Name are the leash; parse/render/split/
  lint/currentLines are already file-agnostic.
- **Stage 1 + 2 — SHIPPED (2026-08-26, commit af83c24).** notepadPath +
  notepadIsLibrary added; open split into the file-agnostic core
  `tasNotepadOpenText(absPath, name, fromLibrary, libFile)` (returns bool),
  tasNotepadOpenEntry now a one-line wrapper; Save writes notepadPath and gates
  the seqLib refresh on notepadIsLibrary; Revert re-reads notepadPath through the
  core (library AND loose); Close clears the new state. VISIBLE: Notepad Row 1
  gained **Save As...** + **Open...**, and the MACROS module gained **Edit...**
  next to Load Macro (cold entry) — both route through the shared
  tasNotepadOpen()/tasMacroDialog path. New helper tasFileBaseName. Fixed a real
  Close-block indentation defect (c83088d had wrongly called it already-fixed).
  Build+smoke green; GUI paths pending a manual pass.
- **Stage 3 — SHIPPED (2026-08-26, commit f518037).** tasLoadMacroChecked(path, m)
  = the load+validate+notify front door, shared by tasMacroStageFile (Load Macro /
  library Stage) and seqQuickPlace. The three silent scan/preview Load sites stay
  un-funnelled on purpose (they skip bad files without a toast).
- **UNIFY — SHIPPED (2026-08-26, commits b83fa62 + 302361f).** (1) raw-packet
  builder -> `tasMacroToRaw(m)`, the byte-identical Macro->insert-rows builder that
  was triplicated at paste/quickplace/staged (the LOOP/pattern builder is a
  distinct canon-array tiler, left alone). (2) players-of -> `seqPlayerBits(m,p1,p2)`,
  shared by seqPlayersOf + the notepad opener + tasNotationMacro. (3) notation
  render -> `tasNotationCell(p1,p2,style,tracks)`, the single P1|P2 layout+separator
  used by BOTH the seq preview and the notepad (killed the "  |  " vs " | " drift;
  preview now shows " | "). Kept OUT: the CE paths (no real drift - both emit CE
  letters) and tasNotepadRender's notepadTracks widening (a global side-effect the
  preview must not trigger). (4) **place+stamp+toast helper - DELIBERATELY NOT
  unified**: the three sites use different stamp fns (seqStampUse vs
  seqStampUseFile), different primitives (tasInsertRows vs tasMacroPlaceFrames), and
  distinct per-site toasts; the reusable logic is already factored into those
  primitives, so the only thing left to "share" is trivial wiring + intentionally
  distinct messages. Forcing a params-heavy helper would harm clarity, not help.
  **WALL** held throughout: SeqEntry/library.json/tags/use-stamps stay
  sequence-only - a loose file never gets a library entry (Rescan/drop is the only
  door in).

**POLISH ROUND 1 — SHIPPED (2026-08-26, commit 5b1b197).** After the user tested
the harmonization and moved to polish:
- **THE 2col "third color set" bug (root-caused + fixed).** Notepad colors loaded
  LAZILY (only in the palette button's click handler) while the render reads
  npEffCol/npUserColActive every frame - so a user with `NotepadHasDef=yes` saw the
  hardcoded flavor defaults on launch until they clicked the palette icon, then
  their colors snapped in. Extracted `npEnsureColors()`, call it at
  tasNotepadWindow top (eager). Also unified a FOURTH set: the glyph 2col hardcoded
  IM_COL32(20,25,36) default bg -> now the same TAS_P*_COL@0.32 formula as the text
  editor's flavor bg. Now: default bg is ONE formula across both 2col views; custom
  is npBg; stomped-default is npDefBg - no stray init set.
- **Default notation -> Cardinal.** Fresh-install `dojo:Notation` default Numpad->
  Cardinal; the notepad's read-only-Glyphs fallback opens the buffer in Cardinal,
  not Numpad. (This user's cfg was already Notation=1=Cardinal; they saw Glyphs only
  because it was selected live.)
- **P2-Start letter - DECISION: keep `P`** (user, 2026-08-26). We considered
  remapping P2-Start from `P` to `;` (to free `P` for a future pause/wait notation),
  but that is a FILE-FORMAT change (`;` is punctuation in a letter codec; the
  tokenizer would special-case it, and existing .txt with P2-Start=`P` would need
  re-save). Deferred until a pause/wait notation actually needs the letter freed.
  Codec stays `{ 'P', 1, CANON_START }` (tasmacro.cpp), legend tasmacro.h:15.
**POLISH ROUND 2 — Notepad header + Snippets rename (SHIPPED 2026-08-26/27).**
- Notepad header rebuilt over several commits: 3-row grouping; standard file-menu
  order (Open/Save/Save As.../Revert/Close); "NOTEPAD" label dropped; always-on
  find/replace bar (Ctrl+F focuses find, Ctrl+H replace - both were dead); "All"->
  "Replace All"; add-cursor buttons removed (Ctrl+Alt+Up/Down covers them). Row 2 =
  [notation] <reactive "2 Columns"/"1 Column"> <checkbox> <P1/P2>; notation moved
  Row 1->Row 2; the 1-col track button reads "P1/P2" (tinted by active track); the
  stale glyph "(read-only view...)" hint removed. Diagnostic uses TAS_ACTIVE_COL
  (matches the roll's FRAME X/Y); "annotations" surfaced as "comments".
- **Sequences -> Snippets rename (SHIPPED, commit + follow-ups).** THE NAMING
  DECISION (user): **Macros are 1st-class citizens** wired into the modules;
  **Snippets are building blocks with no ties to the bigger picture**. Renamed all
  user-facing text; KEPT internal seqLib/SeqEntry, the data/sequences/ folder, and
  the library.json "sequences" key (so files/metadata aren't orphaned). **DEFERRED**:
  a folder migration data/sequences/ -> data/snippets/ (only if wanted later).
- **Snippets window layout -> Replays-style table (SHIPPED, commit 4a475be).** The
  new-snippet row moved above the list; then the Library list became a full-width
  TABLE like the Replays window (RowBg | Resizable | BordersInnerV | ScrollY, frozen
  header, columns Name/Frames/Players/Tags/Added, SpanAllColumns row-select), with the
  rich detail (Edit/Stage/Place/Delete + NOTATION preview + USED IN) stacked in a
  detail panel BELOW the table. Window grew to 640x560. "This clip" scope keeps its
  file list in its own child above the same detail strip.
  **THE UNIFIED-PARADIGM (user): the TasBrowser CLASS - IN PROGRESS.**
  - **Base class SHIPPED (commit 723906e)**: `class TasBrowser` renders the whole
    Replays-style shell in `draw(size)` (RowBg | Resizable | BordersInnerV | ScrollY,
    frozen header, per-row SpanAllColumns Selectable, right-click context menu, dim rows).
    Subclasses override columns()/rowCount()/rowLabel()/rowKey()/drawCell()/
    rowContextMenu()/rowDim(). Selection is a stable rowKey (survives filtering).
    `TasBrowseCol {name, flags, width}`. `tasLcMatch` is the shared filter.
  - **Snippets migrated (723906e + This-clip follow-up)**: `SnippetBrowser` (Library,
    5 cols) and `ClipMacroBrowser` (This-clip, 1 col) both extend it; ~70 inline lines ->
    rebuild()+draw(). seqSelIdx is now an int& alias onto snippetBrowser.selKey.
  - **Replays MIGRATED (commit 99dcb23) - THE PARADIGM IS COMPLETE.** All four browsers now
    render through ONE TasBrowser loop. Replays has too much window-local state for a
    subclass, so it drives a bare TasBrowser via the std::function hooks (added in commit
    before it): lambdas capture clips / selDirs / pendingPlay / requestPlay / the PNG
    textures directly. Full Ctrl/Shift/plain MULTI-SELECT preserved (fnOnActivate +
    fnRowSelected), PNG-thumbnail + savestate cells in fnDrawCell, "Show PNGs" in
    fnContextMenu, colsOverride for the 5 cols, rowH=32px. ~200 inline lines -> the browser
    call; the detail pane (Play/Save/Delete) + clip discovery stay per-window. Compile+smoke
    green; the GUI (multi-select, thumbnails, Play/Delete) wants a manual pass.
    **THE SHARED FACELIFT (user, IN PROGRESS)** - shared UI tech, each hitting all four:
    - #1 SHIPPED: columns Hideable + Reorderable in the base flags (col 0 pinned NoHide|
      NoReorder). Right-click header to toggle/hide, drag to reorder, persists per-table.
    - #2 SHIPPED: `tasParseTags` (shared comma-parse - Snippets + Replays clip.json) and
      `tasMetaEditor` (tags-comma field + notes multiline, save-on-deactivate); Snippets'
      detail uses it. Replays' detail stays per-window (its Name-rename + batched commit);
      could adopt tasMetaEditor for its Tags/Notes later if we want the restyle.
    - QUEUED (user picked meta-editor first): **sortable columns** (base Sortable flag +
      per-browser fnSort hook), **tasBrowsePngStrip** (factor Replays' thumbnail cell into a
      reusable primitive), **tasBrowseToolbar** (the search/rescan/folder sub-row).
    - #3 SHIPPED: **sortable columns**. Base opt-in `sortable` adds ImGuiTableFlags_Sortable
      + reads TableGetSortSpecs() -> fnSort/onSort. Snippets (applySort persists across the
      rebuild), Macros (in-place), Replays (window-static replaySortCol/Asc; Recorded carries
      PreferSortDescending to keep newest-first as the default). Click a header to sort, again
      to flip. (First Replays attempt used the sort state before it was declared - reverted,
      moved the statics above the clips sort.)
    - **RESIZE / RIGHT-CLICK - verified consistent, no code change:** column resize (base
      Resizable) + window resize (Snippets/Macros ImGui::Begin, Replays BeginPopupModal, all
      resizable) + vertical overflow (ScrollY) all work across the four; right-click gives a
      row context menu (all four) AND the header hide/reorder/sort menu (base flags). The
      Replays detail FOOTER was a FIXED reserve on purpose (11062 "no layout jump when
      selecting"). The user chose the SPLITTER.
    - #4 SHIPPED: **draggable table/detail splitter** (Replays). `tasSplitterH(id, *belowH,
      minH, maxH)` - a shared 6px drag handle (ResizeNS cursor) that grows the table /
      shrinks the detail; file-static so the other windows can adopt it. Replays' `footer` is
      now a persistent static (defaults to the same paneH), clamped [40px, availH-80px]; the
      detail's Separator became the splitter. Keeps no-jump AND lets you reclaim the space.
    - #4b SHIPPED: splitter extended to **Snippets** (its seqDetailH is now a persistent
      clamped static; same tasSplitterH divider as Replays).
    - #5a SHIPPED: **tasBrowsePngStrip(pngs, scaling, onClick)** - Replays' thumbnail cell
      (texture load/cache, ImageButton, 6x hover) factored to a reusable primitive; Replays
      passes its PNG-viewer click handler.
    - #5b SHIPPED: **Macros browser shows each clip's state PNGs** (user: movie macros live in
      clip folders that have thumbnails; Snippets are portable so they don't). MovieMacro
      carries pngs (scanned once per clip); a PNG column draws them via tasBrowsePngStrip
      (display + hover, no click viewer there); rows -> 32px, window -> 680x480.
    - #6 SHIPPED: **tasBrowseToolbar** - the search / Rescan / folder sub-row, each part
      optional, SameLined only between rendered parts. Snippets (search+Rescan+folder), Macros
      (Rescan), Replays (filter only; AM/PM stays per-window). ALL SIX FACELIFT PIECES DONE.
    **THE FACELIFT IS COMPLETE.** Shared across the four browsers now: the table render loop,
    column resize + show/hide + reorder + sort, the PNG-strip cell, the tag/note editor, the
    tasSplitterH divider (Replays + Snippets), and the toolbar sub-row. Future browsers get all
    of it by extending TasBrowser + calling the helpers. POSSIBLE LATER: a shared detail-panel
    scaffold; Macros search+filter (the toolbar already supports the field, needs a vis filter
    in MovieMacroBrowser); the splitter on the Macros window if it grows a detail pane.
  - **Macros browser SHIPPED (commit after 4845fad)**: the user's spec - scan the replay
    CLIP folders (data/replays/<game>/<clip>/) for .txt files that parse as real CE macros,
    list them in the shared table. `MovieMacroBrowser` (3rd subclass; cols Clip/Macro/
    Frames/Players; right-click -> Stage / Edit / Promote to Snippets). movieMacrosScan()
    caches; a "Macros (N)" button + window sit beside Snippets. This is the movie-tied
    macro collection, distinct from the shared data/snippets/ library (9 exist in this
    build). tasGameReplaysDir() factors the game->replays path.
    The toolbar (search/rescan/folder) + detail panel stay per-window by design.
- **Snippets polish (SHIPPED)**: the FOLDER renamed data/sequences/ -> data/snippets/
  with a one-time auto-migration in seqLibLoad (rename the old dir if the new is absent;
  library.json reads "snippets" then legacy "sequences", writes "snippets") - verified
  live. "This clip" scope is now a TABLE too; both tables gained right-click row context
  menus (Library: Edit/Stage; This-clip: Stage/+Lib/Edit) - Delete stays two-step in the
  detail panel. This is the surface the shared tasBrowseTable() would factor (user's
  shared-components note: folder-open, trash, rescan, columns, resizing).
- **P2-Start letter**: still `P` (decision held - see Polish Round 1).

- **STILL OPEN (user-named)**: (a) more of the Replays-style Snippets layout (above);
  (b) **UNIFY THE UI across Piano Roll <-> Notepad <-> Snippets** - consistent module
  headers / button styling / colors / layout. The color work pinned the shared P1/P2
  palette (TAS_P1_COL/TAS_P2_COL) and the FRAME color (TAS_ACTIVE_COL) as common ground.
- **RISKS**: any open must set notepadStyle/npSplit/npTypeTrack before the first
  SetFromLines (else split-zip corrupts on Save); Save always writes CE (only
  lossless style) - never offer save-as-numpad/glyph to disk; 2col evening widens
  a P1-only archive to two-track CE on first edit (note in the open toast).
- **DEAD CODE swept first — SHIPPED (2026-08-26, commits 67fe368 + c83088d).**
  Removed the three verified-dead `#if 0` blocks (-161 lines): the stb callback
  tasNotepadEditCb + npPend*/npCursorLast/npSelEndLast (superseded by the vendored
  editor), the DUPLICATE remnants (tasDuplicateSel + doDuplicateRows lambda), and
  the commented-out "Duplicate" right-click menu item. Then retired two stale
  comments (the tasMacroSaveAll "PR2" future-tense header, the 2col-regression
  archaeology note). grep-confirmed 0 live refs to every removed symbol; build
  links clean, magnetoNew smoke green. TWO listed items turned out NON-actionable
  and were left: **npEditCmt is LIVE** (the glyph frame-editor's comment field —
  written at the picker ~4980, read into the frame ~5096; the "never read" note was
  stale), and the **Revert-tooltip SameLine was already at correct 2-tab depth** (no
  defect; earlier Read miscounted the cat -n format's own leading tab). The refactor
  (Stages 1-3 + UNIFY below) is now the next code to touch here.
     **Round 3 - THE FORK IS ABSORBED (user decision, same day)**:
     core/deps/ImGuiColorTextEdit vendored with marked local patches -
     boost::regex -> std::regex (vendor/regex stays out), a TasNotation
     language definition ('#' comments, keyword vocabulary incl. the CE
     alphabet, digit numbers, joiner punctuation), VSCode Alt-move bindings,
     and a NEW DuplicateCurrentLines for Shift+Alt+Up/Down (modeled on the
     fork's own undo idiom). The notepad now runs ON the editor: real
     multi-cursor (Ctrl+D per occurrence, Ctrl+click, find-bar "Sel all" =
     cursor on every match then type once), line-number gutter, in-editor
     syntax colors, Ctrl+/ comment toggle. The stb callback pack is parked
     under #if 0. KNOWN COSTS: programmatic SetText (notation convert,
     glyph-picker commits, Replace/All) resets the editor's undo history;
     in-editor error MARKERS still absent (this fork dropped SetErrorMarkers;
     the lint strip carries diagnostics - a small vendored patch could add
     markers later). USER'S STATED TRAJECTORY: "edit the piano roll in text"
     - the editor instance + notation layer are built to be reused for that.
  3. **Insert/wait-until-frameskip** - a placement verb that waits for the game's
     frame-skip cadence; the Input Viz already knows it ("scene N skip 3/4") -
     share that knowledge with the piano roll.
- Remaining S2 rungs: tag chips as clickable filters, import header-sniff,
  export-with-headers; module-tech backport INTO the F4 page and replay opener
  (user: "this module is a hybrid of both techs").

**L3 (original sketch) - The sequence library UI.** Promote macros from file-dialog artifacts to a browsed
library: a `data/sequences/` shared folder (famous setups travel between clips and
people) alongside per-clip macros; a list view with name, length, which players have
input, and a one-click "place @ target" using the same target picker; "Save selection as
sequence" writes there. The Shuma/HyperGrav 1-framer (activate -> wait ~87f -> assist ->
wait 5f -> jump 7f -> HP) is the acceptance demo: author it once, save it, place it into
a FRESH clip on the other side via Flip Directions.

**L4 - Pad V Pro function mapping.** Enumerate the pad's physical functions WITH the
user (they know the hardware; get the real switch list next session) and map each:
per-button TURBO -> per-column auto-fire (the brush gap generalized to a column toggle);
programmable macro buttons -> sequence quick-slots (bind library entries to keys 1-9,
press = place @ cursor); speed dial -> already exists as Hold-Space scrub speed. Anything
the pad did that the roll cannot yet do becomes a rung here.

**L5 - Interlinking (the user's "shapes" note).** Selections, Bookmarks, patterns and
sequences reference each other: selection -> sequence (Save-selection-as-macro grows a
"to library" path), bookmark-targeted placement (the target picker gains "@ bookmark"),
sequences referenced from clip.json notes/tags. "If in some clip I have a selection to
pass into the mashing library, I can do that."

**Then branching** consumes all of it: fork = new clip + truncated movie + BASE + a
library sequence applied at a bookmark.

---

**THE BRANCHING NORTH STAR (user, 2026-08-24) — the A-B Test Suite.** The PCSX2-rr
bottleneck this fork exists to kill: tutorial content like "Magneto knocks down, then does
1 of 5 wake-up setups" meant record route A → save states carefully → R → capture → ERASE
route A's timeline → record route B → repeat, manually copying files aside as snapshots.
What's wanted: **branch from a state** — record N routes that all fork from the same
timeline point, each branch kept as its own first-class thing, jump back to the fork state
and pick another branch. Architecture insight to build on when we get there (after
piano+manual edge cases are done): **the clip folder already IS the branch unit** — a
self-contained .flyr + states + clip.json + macros. So "Branch from here" ≈ create a
sibling clip folder, `RewriteReplayFile` the movie TRUNCATED at the fork frame into it (the
PR3 rewrite machinery already does 90% of this), copy the fork savestate in as its BASE,
and open it attached. The fork point stays pristine in the parent; each branch records its
own tail; the browser lists them side by side (a `branch-of` field in clip.json can draw
the family tree). Prerequisites queued first: tags/bookmarks/history persisted in
clip.json — all designed to survive undo and state purges.

**Undo scope — the honest ledger (2026-08-24).** Undo/redo covers FRAME DATA only
(session_inputs patches through the funnel). What it does NOT cover, by design today:
- **Staleness is one-way.** Undoing an edit fires a NEW guard event (higher seq); it does
  not erase the old one, so states staled by the original edit STAY stale even though the
  bytes are back. Conservative but safe — content-hash revalidation would be the fix if it
  ever matters.
- **Purged files are gone.** `dojo:PurgeStale` deletes .state/.png/.frame outright; nothing
  in the app can restore them (BASE is always exempt; gens only cover manual overwrites).
- clip.json state entries follow the files.
Candidate mitigation for the bookmarks round: **soft purge** — purge moves the doomed
files into `<clip>/.trash/` instead of deleting, with a "restore / empty trash" affordance
in the browser pane. Cheap, and it makes branch-anchor states effectively loss-proof.
