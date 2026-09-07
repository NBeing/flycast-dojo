# Research: the STAGGER EXPERIMENT layer (branches x staggered send) — flycast-dojo-7 `tas-tools`

Repo: `C:\_mvc2\other\flycast-dojo-7` (branch `tas-tools`, HEAD `ade663c`). All paths below are relative to that
root. Every code claim carries `file:line` from a read of the current tree; anything not read is marked INFERENCE.
Related design owned by the sibling agent: the git-like BRANCH backend (clip folders as branches). This report is
the EXPERIMENT layer on top of it and the inventory of the existing "testing ideas" it must reuse.

---

## 1. Testing-ideas inventory (what exists, where, implemented or not)

No file in the repo is named or titled "Testing Ideas" (grep of `*.md` for `[Tt]esting [Ii]deas`, `IDEAS`,
`experiment` under the root, `core/dojo`, `pcsx2_reference/`: no hit outside `core/deps`). The ideas live in
David's own tracker, the ROADMAP, the WRITE-mode notes and the CANON docs:

| # | Idea | Where | Status |
|---|------|-------|--------|
| I1 | **"Frame Skip Branching, Send signals to all 4 branches / shift timings around for all 4?"** — THIS experiment, in David's words | `david_work_tracker.md:6-7` | NOT built (the tracker is a bare note list) |
| I2 | "Using generations as a design pattern? States Panel: put generations and branches in here somehow? Each Generation can support Branches. Set up a dummy using some IMGUI Node maker" | `david_work_tracker.md:8-12` | NOT built; the States window's GENERATIONS pane exists (`dojo_gui.h:63-67`, commits `574665f`, `426b67d`) but has no branch concept |
| I3 | "InputSender: backspace doesn't work right; just show the timings of the buttons we send always" | `david_work_tracker.md:16` | not addressed in the tree I read |
| I4 | "Overlay over game that pulls from places in our panels; doesn't get captured" | `david_work_tracker.md:18` | NOT built (captures are already pre-OSD clean: `CLAUDE.md:233-234`) |
| I5 | **The frameskip SWEEP methodology** — test a combo at each MvC2 skip phase: Notepad Send -> Live + Wait for Frameskip + `skip+N`, sweep N = 0..3, F3 between passes; **Auto re-send on reload** makes the loop hands-free | `WRITE_MODE_RESTRUCTURE.md:13-39`, commits `9f3dc72`, `ddd2d23`, `e198317`, `1faa914` (`:281-282`) | BUILT + verified live ("Magneto s.HP x33 -> s.LP x1 connects ~1/4 of the time", `:16-18`). This is David's stagger experiment in manual, 4-phase, non-persisting form |
| I6 | T5 acceptance "parity sweep": the same LP repeated at frames N..N+4 to observe skip-cycle parity, judged offline from `fidelity.jsonl` | `ROADMAP.md:73-78`; dump at `core/dojo/dojo.cpp:1312-1340` | DONE once (offline); no UI |
| I7 | "Observe piano roll" as a first-class WRITE-like mode (play the roll where it has content, record the pad where it doesn't, wipe on reload) — the behavior the sweep "really wants" | `WRITE_MODE_RESTRUCTURE.md:75-86` | OPEN, not built; the live-inject is the stopgap (`:41-49`) |
| I8 | **Auto re-send sweep in READ-WRITE accumulates residue** (each pass's bake persists; F3 restores the machine, not the roll); options listed | `WRITE_MODE_RESTRUCTURE.md:257-260` | OPEN — a design fact the experiment must solve (see §6) |
| I9 | **The BRANCHING north star — the "A-B Test Suite"**: branch from a state, record N routes forking from one point, each branch a first-class thing; "the clip folder already IS the branch unit"; `branch-of` in clip.json | `ROADMAP.md:1667-1681`, `:1662-1663`, `:812-817`, `:846-848` (BRANCHING HOOK: "Go into New/<Named> Branch from this point") | NOT built — the sibling's job. `CLIP_SCHEMA.md:83` already stores `generations[].slotFrames` "(branching groundwork)" |
| I10 | BizHawk `TasBranch` (movie + state + screenshot snapshots) | `BIZHAWK_NOTES.md:119-121`, `:146` | REJECTED at the time ("generations[] suffices") — the sibling is reversing this; note the screenshot-per-branch idea is exactly the outcome PNG in §5 |
| I11 | Launch templates -> "real game-state tests (seed a template, run, assert a RAM value via the MvC2 RAM tables)" | `CANON_onenter.md:101-105` | FUTURE (deferred until fastVS proven; fastVS is proven, `:3-7`) |
| I12 | The Play Macro capture recipe: `F12 -> load the launch state -> Play Macro -> game plays the combo out -> F12` | `CANON_macro_mode.md:210-219` | the mechanism exists (live inject + infinite roll); no automation |
| I13 | Deferred ledger: DivergentPoint restore, integrity check (replay from 0 and byte-compare every state), dense greenzone rules | `ROADMAP.md:271-288` | not built |
| I14 | Harness #2: per-frame input/state-hash verdicts | `CLAUDE.md:456`, `ROADMAP.md:285-286` | not built |

Also relevant, already shipped and reused below: the Input Sender **Auto-Send** loop (write a cell, then step exactly
its length: `core/dojo/dojo_gui.cpp:10575-10639`), the OnEnter seed (`core/dojo/dojo.cpp:1711-1780`), Play Macro
Full/Stage boots (`:1793-1867`), the REL ruler skip map (`core/dojo/tas_ruler.*`), and `AutoSeekState`/`AutoCapture`
(`core/rend/mainui.cpp:193-215`).

---

## 2. The Notepad "Auto re-send on reload" feature — exactly

### 2.1 Trigger
- `dojo.load_seq` (`core/dojo/dojo.h:120-122`) is bumped in `Dojo::LoadStateFrame` (`core/dojo/dojo.cpp:742`,
  right after `frame_number = fn` at `:740`). `LoadStateFrame` returns early when the state has NO `.frame` sidecar
  (`:683-685`), so only sidecar-bearing loads count. It is called from `dc_loadstate(int, std::string)` at
  `core/nullDC.cpp:294` — i.e. EVERY state load (F3 hotkey, the boot handoff, `AutoSeekState`, Lua) — not a
  "jump" of any other kind. `emu.invoke_jump_state()` does not exist in this fork's dojo path; the movie clock moves
  only via `LoadStateFrame`.
- The Notepad polls it every GUI frame: `if (npAutoResend && dojo.load_seq != npAutoSeenLoad) { npAutoSeenLoad =
  dojo.load_seq; if (!dojo.play_match) npSendLive(true); }` (`core/dojo/dojo_gui.cpp:7610-7615`). The checkbox
  ("Auto re-send on reload") arms from NOW (`:7606-7607`: `npAutoSeenLoad = dojo.load_seq`), and is a
  session-scoped static (`:7564-7565`) so it never auto-fires in a fresh session. Tooltip: "every state load (F3)
  re-sends the notepad live automatically - set skip+N, F3, watch, bump N, F3... hands-free sweep loops. Honors the
  Wait-for-Frameskip box" (`:7609`).
- Ordering detail that matters: `gui_loadState` calls `tas_auto::stopLive()` AFTER `dc_loadstate` (`core/rend/gui.cpp:4585`,
  R14 `CANON_ux_batch.md:31`) — a reload cancels an in-flight send; the auto re-send then re-fires on the next GUI
  frame because `load_seq` changed inside `dc_loadstate`.

### 2.2 What it sends
- `npSendLive` (`dojo_gui.cpp:7544-7563`) parses the WHOLE current notepad through `tasNotepadCurrentLines`
  (`:5796-5818`: both track editors P1/P2 are parsed with a hard track and zipped; 1-column mode just hides one),
  drops annotation lines, and flattens every line's frames into `lp1`/`lp2` (`std::vector<u16>` canon bits per
  player, `:7552-7553`). Comments/annotations are not frames (`core/dojo/tasmacro.cpp:94-108`).
- It does NOT start from the TARGET frame. The TARGET (`@ 6833 (Active)`) only governs the bake verbs. The live send
  starts at `dojo.frame_number + 1` (`:7562`) — or, with **Wait for Frameskip** on, at the next skip frame + `skip+N`
  (`:7559-7560` -> `tasArmFrameskipSend`, `:2873-2882`; released in `MapleApplyAction`, `dojo.cpp:1907-1919`).
- `quiet=true` on the auto path suppresses the empty-buffer toasts (`:7549`, `:7556`).

### 2.3 How the TARGET is chosen
- `tasTargetPicker` (`dojo_gui.cpp:4452-4487`), state `tasSendTarget` (`:4451`): 0 = "(Active)" = the playhead
  `dojo.frame_number`, 1 = "(Selection)" = the piano-roll selection start; falls back to Active when no selection
  (`:4454-4455`). ONE shared picker for every verb row (`:4448-4450`). The Notepad resolves `npAt = (tasSendTarget
  == 1 && npHaveSel) ? npSelLo : npCur` (`:7429`). The MERGE checkbox sits beside it (`:4488-4499`, `tasMergeOn`
  `:2275-2279`, mirrored into `dojo.send_merge` for the emu thread `:2264-2271`).
- Bake verbs at the TARGET: `Replace @N` / `Merge @N` (`:7506`, `:7514`), `Insert @N` (`:7507`, `:7519`),
  `Append @end` (`:7508`, `:7523`), explicit `Merge @N` when Merge is off (`:7526-7534`); the diff-only `Merge`
  (`npMerge`, `:7455-7502`) is shelved behind `npSyncEnabled` (`:7535`). All require `!dojo.play_match &&
  !dojo.session_inputs.empty()` (`:7430`).

### 2.4 The code path from notepad text to the guest (live path)
1. `npSendLive` -> `tas_auto::playLive(p1, p2, start)` (`core/dojo/tas_auto.cpp:79-85`; the queue is `g_seqP1/P2`
   + `g_seqStart`).
2. Emu thread, every maple poll, `Dojo::MapleApplyAction` (`dojo.cpp:1886`): gate `!play_match && (anyArmed() ||
   liveActive())` (`:2069`); `liveCanon(pl, fr)` returns the frame's bits (`tas_auto.cpp:94-101`); SOCD-cleaned
   direction + OR of buttons (`dojo.cpp:2076-2077`); `liveTick` ends the sequence when the last frame applied
   (`:2081`, `tas_auto.cpp:102-108`).
3. The overlay is BAKED into both the frame being applied (`current_inputs`) AND the roll cell
   `session_inputs[frame]` (`:2097-2118`). READ-WRITE stomp rule: when the pad did not record this frame and Merge
   is off, the player's canon is CLEARED first so the send REPLACES the prior cell (`:2109-2114`); otherwise it
   ORs (`:2115-2117`). Locked ranges only DRIVE the guest, never bake (`:2087-2096`, `:2142-2150`).
4. Then the `.flyr` append (`:2127-2140`) and the kcode handoff to the maple state (`:2152-2159`).
5. Bake verbs take the other road: `tasMacroPlaceFrames` (`dojo_gui.cpp:2361-2376`) -> `Dojo::ApplyEdit`
   (`dojo.cpp:1371-1480`): refuses truncation (`:1376-1384`), diffs (`:1386-1405`, no-op = no event), drops locked
   frames (`:1407-1434`), pushes an undo patch (`:1436-1455`), writes the cells (`:1457-1458`), marks the macro
   autosave stale (`:1459`), advances the stale-tail marker (`:1463-1464`), logs the TIMELINE EVENT
   (`rerecord_count++`, `rewind_log`, `:1469-1473`), appends the changed frames to the `.flyr` (`:1474`).

### 2.5 Known limitations recorded in the repo
- The loop was designed for WRITE ("F3 -> the pad re-clobbers -> clean pass"); in READ-WRITE (every macro session)
  "each pass's bake PERSISTS (overdub) and F3 restores the machine, not the roll" — residue
  (`WRITE_MODE_RESTRUCTURE.md:257-260`). Options listed there: undo the previous bake first; reload the macro from
  file on F3; require WRITE.
- Play Macro sessions have no persistence and no re-record detection (`recording_started` never true) — a live
  send lands in memory only (`WRITE_MODE_RESTRUCTURE.md:250-256`).

---

## 3. The frameskip model and the STAGGER UNIT decision (the crux)

### 3.1 What the tool actually knows about frames (all measured, cited)
- **Source of truth = guest RAM, read every maple poll.** `core/dojo/mvc2.cpp:12-21`: `SKIP_RATE 0x8C289620` (u8: 6
  normal, 4 turbo, 2 turbo2; 0 idle — `mvc2.h:29`, `tas_ruler.h:21`), `SKIP_COUNT 0x8C289621` (u8 countdown; "at 0
  resets to rate and the game runs an extra logic frame", `mvc2.h:30`, `ROADMAP.md:230`), `SKIP_TOGGLE 0x8C289622`
  (u8: **255 exactly on the reset/skip frame**, 0 otherwise — `mvc2.cpp:19`, `ROADMAP.md:231`), `SCENE_FRAME
  0x8C1F9D80` (u32, resets between scenes), `TOTAL_FRAMES 0x8C3496B0`. Readers: `readSkipToggle` (`:31-36`),
  `peekSkip` (paused, `:38-46`), `read()` (`:48-62`, validation latch `:63-72`), MemTrace burst (`:94-104`),
  MemHunt (`:131-193`). Nothing is a tool-side counter; the "SCENE 4219" number is the game's own `0x1F9D80`.
- **Measured cadence** (`ROADMAP.md:143-146`, `:237-244`): rate 0 in menus, 6 in attract/char-select, **4 in a real
  match**; count runs `4 3 2 1` repeating, one step per scene frame; the toggle fires 255 exactly on the reset frame;
  "use `toggle == 255` as the injection alignment anchor (simpler and less ambiguous than `count == rate`)".
  `0x289600` (u16 cycle value) is uncharacterized (`:146`, `:232`).
- **The scene counter increments every emulator frame** (`mvc2.cpp:141-142`: "it changes every frame, exactly as
  the trainer documents"; `ROADMAP.md:144`: "scene counter every-frame at 0x1F9D80"). So the game's own frame count
  and the emulator frame count tick together; the skip cadence is a phase overlaid on that clock, not a separate
  slower clock.
- **Input latch latency** (T3, `ROADMAP.md:177-180`, code `dojo.cpp:1262-1311`): the game's latched flags match a
  SENT frame within the last 4 sent; baseline histogram same≈95%, -1f≈5%, -2f=0. Inputs are consumed on every
  emulator frame; there is NO finding in the repo that the game ignores input on skip frames. What the phase changes
  is the game's LOGIC timing (the extra logic frame on the reset), and the effect is real: "vs-mode skips every 4th
  frame (skipRate = 4). So a combo whose timing straddles a skip boundary only connects on one of the four phases.
  Verified live" (`WRITE_MODE_RESTRUCTURE.md:16-18`).
- NOT established (do not assume): cycle@0x289600, mirror bytes, analog fidelity (`ROADMAP.md:205-211`). Whether
  the -1f 5% correlates with skip frames is NOT stated anywhere — INFERENCE if you assume it.

### 3.2 The three displays and their two anchors
| Surface | What it shows | Anchor rule | Code |
|---|---|---|---|
| Input Viz header / Timeline HUD: `SCENE %u   SKIP %u/%u` + badge `DRAWN`/`SKIP` | `skipCount/skipRate` read NOW via `tas_mvc2::read()` | badge = **SKIP** when `skipRate != 0 && skipCount == skipRate` ("the injection-alignment anchor"), else `DRAWN` | `dojo_gui.cpp:16089-16108` |
| Piano Roll REL ruler glyphs `x / o / - / ?` | per-frame samples filed at each poll (`tas_ruler::onPoll` from `MapleApplyAction`, `dojo.cpp:1900`) | `x` = `toggle == 255` at that poll (`tas_ruler.cpp:29`); `o` = polled, not skip; `-` = idle (rate 0); `?` = never polled | `tas_ruler.h:16-22`, `dojo_gui.cpp:14772`, `:15439-15442` |
| Wait for Frameskip release | held send fires when `readSkipToggle() == 255` at a poll (or at a 60-frame deadline), at `fnow + FrameskipOffset` | `toggle == 255` | `dojo.cpp:1907-1919`, arm `dojo_gui.cpp:2873-2882`, control `:2884-2912` (offset clamped 0..20, default 1) |

So "SKIP 1/4" in the Input Viz is `count 1 of rate 4` — the LAST frame before the reset (count `4 3 2 1`), not
the anchor itself; the anchor badge reads `SKIP 4/4`. The ruler's `x` and the Wait release use the toggle
instead. `ROADMAP.md:242-243` treats the two anchors as the same frame; no line in the repo asserts they were
measured to coincide at the same poll — treat as "same frame, unverified at poll granularity".

**Display-offset conflict (record it, do not resolve it here):** `tas_ruler.h:9-10` says the sample at frame N's
poll "reflects the game state after frame N-1 ran, so the roll shifts the display by `dojo:FrameskipOffset`", but
the roll code uses a SEPARATE `dojo:RulerSkipOffset` (default 0) and states: "NOT the Wait-for-Frameskip skip+N
send alignment ... reusing it put the x two rows off (David)" (`dojo_gui.cpp:12372-12375`, tooltip `:17663`). The
header comment is stale relative to the code. Consequence for the experiment: store the RAW `(rate, count, skip)`
sample at each branch's start frame (from `tas_ruler::snapshot`, `tas_ruler.cpp:93-103`), never an interpreted
"phase N" label.

### 3.3 The roll's unit
- The roll is `dojo.session_inputs[frame]` keyed by `dojo.frame_number` (`dojo.h:104-105`), which increments once
  per `MapleApplyAction` (`dojo.cpp:1924` in the delay branch; the normal increment is at the end of the function,
  per the comment at `:1989-1990`). One row = one maple poll = one emulated frame = 1/60 s ("Replays are 60 fps
  movies keyed by frame number", `CLAUDE.md:418`; determinism "keyed to frame numbers, never wall clock",
  `CLAUDE.md:258-260`). `session_inputs` never distinguishes drawn vs skipped rows.
- A macro/notepad line = one such row (`tasmacro.h:8`, `CANON_macro_mode.md:57-61`: 0-indexed frames).

### 3.4 DECISION — the stagger unit
**k is counted in ROLL FRAMES (= emulator frames = maple polls = the game's scene-counter ticks).** A 1-row stagger
IS a 1-game-frame stagger; there is no separate "drawn-frame" clock to convert into. What a stagger sweep must add is
the SKIP PHASE annotation, because at rate 4 branches `k` and `k+4` land on the same phase while `k..k+3` walk all
four (`WRITE_MODE_RESTRUCTURE.md:25`: "Sweep N = 0,1,2,3 to walk the four phases"). David's N=10 therefore covers
2.5 cycles: the experiment table must show, per branch, the raw skip sample at `F + k` (and ideally at the first
frame of S's first attack) so equivalent-phase branches are visible.

Two addressing modes fall out, both expressible with existing primitives:
- **ABS (default):** branch k's S starts at `F + k` (bake `k` blank rows + S at F). Deterministic, no RAM read
  needed, works outside matches.
- **PHASE (optional, = today's Wait-for-Frameskip):** branch k's S starts at `A + k` where A is the first poll ≥ F
  whose toggle reads 255 (`dojo.cpp:1910-1914`); today's `skip+N` with N = k. Only valid mid-match (rate ≠ 0);
  the 60-frame deadline fallback (`dojo_gui.cpp:2879`) must be treated as an experiment ERROR, not a silent send.
Because A is a constant for a fixed base state (deterministic emulation from the same state), PHASE == ABS shifted
by `(A - F)`; ABS with a phase annotation is the general answer, PHASE is the convenience for "count from the skip".

---

## 4. Primitives table (input send, roll feed, state jump, savestates, scripted frames, capture)

| Primitive | What it does | Thread / preconditions | Location |
|---|---|---|---|
| `tas_macro::Macro{frames[]{p1,p2}}` | in-memory sequence; canon bits per player (U D L R LP HP LK HK Start A1 A2 = bits 0..10) | — | `core/dojo/tasmacro.h:26-45` |
| CE text codec `FromText/ToText/Load/Save` | one line = one frame; letters `WSADZXCVBNM` (P1) / `TGFHUIOJKLP` (P2); **blank line = neutral frame** (`ToText` writes neutral as an empty line, `:127-141`; `Save` `:143-154`); `#` comment; comment-ONLY line = annotation, not a frame; `. #note` pins a neutral frame; numeric-only lines ignored | pure | `tasmacro.cpp:14-23`, `:43-112` |
| bk2-style text movie `tas_text::Export/Import` | whole-movie grid with LogKey (byte-exact) — NOT the notepad format | pure | `core/dojo/tastext.h:7-34` |
| `tasCanonToPacket` / `tasPacketToCanon` | canon <-> `FrameInputs` (pressed bits SET; A1/A2 = trigger bytes 255) | — | `dojo_gui.cpp:2282-2320` |
| `tasCombineCell(v, pl, canon, merge)` | the ONE cell writer: replace, or merge (buttons OR, direction replaces only if non-neutral, empty leaves) | — | `:2325-2342` |
| **`tasMacroPlaceFrames(m, atFrame, source, mergeMode)`** | REPLACE (or merge) `m` into the roll from `atFrame` via `ApplyEdit`; `mergeMode -1` follows the Merge switch, `0` force replace, `1` force merge | GUI, paused, `!play_match` | `:2361-2376` |
| `tasMacroMergeFrames` | force-merge twin | same | `:2383-2386` |
| `tasMacroToRaw` + **`tasInsertRows(at, n, content, source)`** | INSERT: tail shifts up by n, via `ApplyEditResize` (the only shrink/grow path) | same | `:2391-2406`, `:3297-3314` |
| `tasFillRowsWithMacro` | cycle a pattern into selected rows | same | `:3149` |
| Stage buffer `tasStaged` / `tasMacroStageFile(path)` / `tasStagedRecompute` | load a macro into the amber STAGED buffer; the roll's PLACE row lands it at the TARGET; Swap/Flip/P1->P2 toggles | GUI | `:3749-3823`, `:3844-3861` |
| `tasTargetPicker` (`tasSendTarget`) | where bake verbs land: Active (playhead) or Selection start | GUI | `:4451-4487` |
| Notepad parse `tasNotepadCurrentLines` / `NotepadLine` | notepad -> frames (both tracks zipped) | GUI | `:5796-5818`, `:4878-4887`; notations `:4288-4290` (Numpad, Cardinals, Glyphs, CE letters, PPAD); 1/2 columns `:7050` |
| Snippet library | `data/snippets/*.txt` payloads (CE codec) + `library.json` tags/notes/hash | GUI | `:3870-3907` (`seqLibDir` = `get_writable_data_path("snippets")`) |
| **`Dojo::ApplyEdit(edited, source)`** | THE funnel: diff, lock filter, undo patch, write cells, stale-tail, timeline event (`rerecord_count++`, `rewind_log`), `.flyr` append, `WriteClipStats` | GUI (paused), `!play_match` | `core/dojo/dojo.cpp:1371-1480` |
| `Dojo::ApplyEditResize` | same, may shrink; rewrites the `.flyr` | same | `:1482` |
| `Dojo::MapleRecordAction` | pad -> roll: WRITE clobbers every frame; READ-WRITE (`macro_armed`) records only on a non-neutral pad ("signal"), preserves otherwise; Merge ORs; seed window keeps the pad OUT (`onenter_ff && stepping && frame < target`) | emu thread | `:503-609` (gate `:514-515`, lock `:520-533`, signal `:578-587`) |
| `Dojo::PollRecordAction` | divergence detector (timeline event at the first byte-differing write after a rewind), stale-tail advance, live macro autosave every 30 frames | emu thread | `:390-469` |
| **`Dojo::MapleApplyAction`** | roll -> guest each poll; infinite roll in write sessions (`tasWriteGrow`); skip-map poll; Wait-for-Frameskip release; macro READ pauses at the roll's last key; live overlay bake; `.flyr` append; locked-range drive | emu thread | `:1886-2160` (`:1895-1897`, `:1900`, `:1902-1919`, `:1933-1942`, `:2060-2119`, `:2127-2150`) |
| `tas_auto::playLive/stopLive/liveActive/liveCanon/liveTick/liveRemaining` | the live sequence queue | GUI arms, emu consumes | `core/dojo/tas_auto.cpp:79-115` |
| `tas_auto::arm/overlayCanon/expand` | hold/auto-fire overlay and its bake expander | — | `:28-72`, `:117-122` |
| `dojo.macro_armed` | READ-WRITE vs WRITE (`play_match` = READ overrides) | — | `dojo.h:296-303`; model `CANON_readwrite_model.md:29-57` |
| `dojo.stale_tail_from` | WRITE old-take marker (no truncate on load, PCSX2-RR v2.0+ parity) | — | `dojo.h:115-119`, `dojo.cpp:761-766` |
| `Dojo::SaveStateFrame` / `LoadStateFrame` | `.frame` sidecar `{frame, rerecordSeq, movieLen, prefixHash}`; load = seek `frame_number = fn` in ALL modes (never truncates), `load_seq++`, arms the divergence detector | called from `dc_savestate`/`dc_loadstate` | `dojo.cpp:656-679`, `:681-770` |
| Dead-timeline guard `IsStateStale` + prefix-hash exoneration | STALE iff a later event went below the state's frame; identical prefix bytes revalidate | — | `:611-617`, `:621-642`, `:644-654` |
| `Dojo::SeedOnEnter` | write frames 0..N-1 into the roll pre-boot, `macro_armed=true`, `stepping=true; target_step_frame=N; onenter_ff=true` -> FF to a handoff pause | GUI, pre-boot (`gui_start_game`) | `:1711-1780` |
| `Dojo::LoadMacroFull` / `LoadClipState0Boot` / `InjectPendingMacroAt` | stash a macro, arm a deferred State-0 load at the boot pause, inject relative to State 0's frame | GUI, pre-boot + boot handoff | `:1793-1867`, `:1873-1884`; handoff `core/rend/gui.cpp:4392-4405` |
| `Dojo::WriteMacroFile` / `MacroAnchorFrame` / `MacroFlush` | `<clip>_macro.txt` written RELATIVE to State 0's frame (dense, blank = neutral) | GUI tick / emu (throttled) | `:2738-2806` |
| `Dojo::Reset` | session teardown: flush, `clearAll()` + `stopLive()`, skip-map + wave save | GUI | `:2808-2895` |
| **`gui_loadState()`** | `emu.stop(); dc_loadstate(slot)`; stays Paused if Paused; `tas_auto::stopLive()`; refuses empty slots | GUI thread, `gui_state` in {Closed, Paused, ReplayEnd}, `savestateAllowed()` | `core/rend/gui.cpp:4553-4590`, `:993-996` |
| **`gui_saveState()`** | `emu.stop(); dc_savestate(slot)`; thumbnail (`tas_thumb::captureForState`), wave snapshot, skip map, epoch bump, `WriteClipStats` | GUI thread, Closed/Paused | `:4592-4625` |
| `dc_savestate(int slot)` | serialize twice, RZip write, sidecar via `SaveStateFrame` | any thread (Lua/AutoSave reach it) — but the THUMBNAIL must not be taken here | `core/nullDC.cpp:111-168`; `thumbnail.h:10-14` |
| `dc_loadstate(int)` / **`dc_loadstate(std::string filename)`** / `(int, std::string)` | read, deserialize, `LoadStateFrame`, `VerifyState` (~10 ms "against a ~500 ms state load"), `Event::LoadState` | with the emu stopped | `:209-306` (`:214-217` path overload, `:294-299`) |
| Slot paths | `hostfs::getSavestatePath(slot, writable)` honors `savestateFolderOverride`; slot 0 = `<game>.state`, N = `<game>_N.state`; 100 slots | — | `core/oslib/oslib.cpp:280-293`, `oslib.h:50`, `:92` |
| `hostfs::scanSavestateInfo()` | one directory read: occupancy, size, mtime, movieFrame, label for all slots | GUI | `oslib.cpp:182` |
| **`gui_open_step()` / `gui_step_frames(n)`** | `stepping=true; target_step_frame = frame + n; emu.start()` (only when `dojo:Training` or `play_match`) | GUI | `gui.cpp:5893-5919`, `:5923-5945` |
| The step STOP | render-thread OSD hook: `if (dojo.stepping && frame_number >= target_step_frame)` -> (hold-scrub pacing or) `emu.stop(); gui_setState(Paused)` + the boot-handoff work; ">= not ==: the emu thread can overshoot the target between OSD checks" | render thread, every presented frame | `gui.cpp:4341-4448` |
| `gui_open_pause()` | toggle pause; flushes the macro when stopping | GUI | `:5947-5965` |
| FF during scripted runs | `settings.input.fastForwardMode = true` while `onenter_ff` (re-armed each OSD pass), dropped at the handoff | render thread | `gui.cpp:4333-4340`, `:4384` |
| `Emulator::step()` / `stepRange()` | single SH4 INSTRUCTION / PC range — NOT frames | — | `core/emulator.cpp:769-783`, `:609-623` |
| `Emulator::start/stop/render` | threaded loop `while (state == Running || singleStep || stepRangeTo)` (`:911-930`); `stop()` = `sh4_cpu.Stop()` + `rend_cancel_emu_wait` + join (`:692-713`); `render()` = `rend_single_frame` per presented frame (`:963-987`) | — | as cited |
| Input Sender **Auto-Send** loop | freeze; on gesture: `tasMacroPlaceFrames(m, frame+1, "input sender step")` then `gui_step_frames(len)`; READ-WRITE only by mechanics | GUI | `dojo_gui.cpp:10575-10639` (`:10630-10632`) |
| `AutoSeekState` / `AutoCapture` | one-shot F3 to slot N once playback runs (`play_match`, frame > 120), then start the recorder | render thread | `core/rend/mainui.cpp:193-215` |
| `avi_toggle_recording()` (F12), `AviDump` | ffmpeg-direct `.mov`; every emulated frame exactly once (blocking queue); AutoCapture names `captures\<replay-stem>.mov` | render thread | `core/dojo/avi_dump.h:27-46`, `:104`; `avi_dump.cpp:231-256` |
| `tas_thumb::captureForState(statePath)` | `renderer->GetLastFrameRGB` (DX9/DX11 only) -> box downscale to `dojo:ThumbnailWidth` (320) -> async `stbi_write_png(<statePath>.png)` | render thread, emulator stopped | `core/dojo/thumbnail.cpp:74-113`; `hw/pvr/Renderer_if.h:71`; `rend/dx9/d3d_renderer.cpp:1211`; `rend/dx11/dx11_renderer.cpp:1465` |
| Fidelity dump | `<clip>/fidelity.jsonl` per applied frame `{f, sp1, sp2, rp1, rp2, skip:"c/r"}` — playback only (`play_match`) | emu thread | `dojo.cpp:1312-1340`, gate `:1264` |
| RAM reads available | skip rate/count/toggle, scene frame, total frames, P1/P2 latched input flags (`tas_mvc2::read`); per-game WIN counters for netplay score (none for MvC2) | — | `mvc2.cpp:12-21`, `:48-62`; `dojo.cpp:146-264` |
| Lua | `flycast.emulator.{startGame,stopGame,pause,resume,saveState,loadState,exit}` (save/load wrap `dc_savestate/dc_loadstate` directly), `flycast.memory.read8..64/readTable*/write*`, `flycast.input.*`, `flycast.dojo.{getFrameNumber,loadRecordSlotsFile,playRecordSlot}`; callbacks table `flycast_callbacks` with `vblank`, `overlay`, Start/Resume/Pause/Terminate/LoadState; init file `flycast.lua` | Lua VM on the render thread (`lua::overlay()` from the OSD, `gui.cpp:4304`) | `core/lua/lua.cpp:38`, `:67-123`, `:533-585`, `:669-691`, `:743-755`, `:776-781`; `CMakeLists.txt:460` |
| CLI keys used by harnesses | `dojo:StartupPrompt=no NativeConsole=no UiIni=no VerifyState VerifyInputs Replay ReplayFilename AutoSeekState AutoCapture`, plus `OnEnterFile`, `PlayMacro`, `PlayMacroClip`, `PlayMacroFile`, `PlayMacroStage`, `StageMacroFile`, `MacroMode`, `Training` | — | `test.ps1:89`, `run.ps1:72-76`, `gui.cpp:950-960`, `dojo.cpp:1713`, commit `53e1fff` |
| Hotkey gating | `tasHotkeysBlocked()` (End of Replay, the seed window) | input thread | `core/input/gamepad_device.cpp:115` |

No Lua binding exists for `session_inputs`, `playLive`, `stepping`, `savestateFolderOverride` or capture; Lua's
`saveState/loadState` bypass `gui_saveState` (no thumbnail, no `stopLive`).

---

## 5. The STAGGER EXPERIMENT design

### A. Inputs
| Input | Source today | Notes |
|---|---|---|
| Base state `B` at frame `F` | a slot in the live clip: file `hostfs::getSavestatePath(s,false)`, `F` = `SavestateInfo.movieFrame` (`oslib.cpp:182`) — default slot 0 / BASE (the "pass-off" anchor, `CANON_macro_mode.md:224-230`) or the current slot | or "Active": save a base state at the playhead first (needs a free slot; see §6.9) |
| Sequence `S` | the Notepad (`tasNotepadCurrentLines`), the staged buffer (`tasStaged`), or a snippet/macro file (`tas_macro::Load`) | keep `S` as `tas_macro::Macro`; hash = FNV-1a over `tas_macro::ToText(S)` (same mixer as `MoviePrefixHash`, `dojo.cpp:621-642`); the snippet library already keeps a `hash` per entry (`dojo_gui.cpp:3879`) |
| `N` branches, first offset `k0` | new cfg `dojo:ExpBranches` (default 10), `dojo:ExpFirstOffset` (0) | David's example: N = 10 |
| Unit | ROLL frames (§3.4); optional PHASE mode = start at the first `toggle==255` poll ≥ F, plus `FrameskipOffset` | PHASE reuses `tasFrameskipCheckbox`'s two cfg keys (`dojo_gui.cpp:2884-2912`) |
| Run length `R` | frames to play after S ends, `dojo:ExpRunFrames` (default e.g. 90) | outcome frame `E_k = F + k + len(S) + R` |
| Tail input `T` during `R` | default neutral; optional "hold back"/"mash" pattern from the MASH bar | must be EXPLICIT rows (see residue, §6.3) |
| Mode | READ-WRITE (`macro_armed`) with the pad OUT; refuse in WRITE/READ (Auto-Send's rule, `dojo_gui.cpp:10603`) | WRITE clobbers a bake (`WRITE_MODE_RESTRUCTURE.md:41-46`) |
| Outcome capture | PNG always; state always; `.mov` optional | |

### B. Generated artifacts
Per branch `k` (folder mechanics = the sibling; this is what the folder must CONTAIN so the branch is self-contained
and re-playable through today's Play Macro Full path, `dojo.cpp:1793-1842`):
1. `<game>.state` + `.state.frame` (`{F, seq, movieLen, prefixHash}`) + `.state.png` = a COPY of `B` as the branch's
   State 0 (Play Macro Full injects the macro relative to State 0's frame, `gui.cpp:4399-4404`).
2. The movie prefix `.flyr` truncated to `< F` (sibling: `RewriteReplayFile`, `ROADMAP.md:1676`) — so the branch
   replays from power-on too.
3. **`<branch>_macro.txt` = `k` blank lines + `S` (+ `R` tail lines)**, with a leading comment block
   (`# experiment <name>  k=7  S=<hash>  F=<F>`) — comment-only lines are annotations, not frames
   (`tasmacro.cpp:94-108`); neutral frames are empty lines (`:127-141`). Written by `tas_macro::Save`.
4. `clip.json` with `mode:"macro"`, `macroFile`, `branch-of: <parent clip>`, `experiment: {name, k}`.
5. Outcome: `outcome.state` (+ `.frame`, `.png`) at `E_k`, optional `outcome.mov`, optional `fidelity.jsonl`.

The experiment manifest `<parent clip>/experiments/<name>/experiment.json`:
```json
{ "schema": 1, "name": "cyc-hit19-20", "createdUtc": "...",
  "base": { "clip": "<parent>", "slot": 0, "file": "NoBGM_VMU.state", "frame": 6833,
            "prefixHash": "0x...", "stateBytes": 27793035 },
  "S":    { "hash": "0x...", "frames": 14, "source": "notepad", "text": "...CE text..." },
  "N": 10, "k0": 0, "unit": "roll", "phase": { "on": false, "frameskipOffset": 1 },
  "R": 90, "tail": "neutral", "mode": "READ-WRITE",
  "branches": [
    { "k": 7, "dir": "<parent>_exp_cyc-hit19-20_k07", "macro": "..._macro.txt",
      "startFrame": 6840, "endFrameTarget": 6944, "endFrameActual": 6944,
      "skipAtStart": { "rate": 4, "count": 2, "toggle": 0 },
      "outcome": { "state": "outcome.state", "png": "outcome.state.png", "frame": 6944,
                   "ram": { "sceneFrame": 4330 } }, "verdict": "", "notes": "" } ] }
```

### C. The execution loop (real functions, GUI/render-thread state machine)
The safe pattern already exists twice: the Input Sender's Auto-Send (`dojo_gui.cpp:10598-10639`: edit while
Paused, then `gui_step_frames`, then wait for the OSD stop) and the boot handoff (`gui.cpp:4341-4446`). The rule
it obeys: **the emu thread owns `session_inputs` while it runs** (`dojo.h:311`), so every roll write happens while
Paused, and the run is "step N frames" whose stop is the existing OSD hook.

```cpp
// core/dojo/tas_experiment.{h,cpp} (new), ticked from DojoGui::show_tas_tool_windows (render thread)
struct Exp { enum { IDLE, LOAD, BAKE, RUN, SNAP, RESTORE, DONE } st; int k; /* manifest, S, F, N, R ... */ };

void tas_experiment::tick()
{
    switch (e.st)
    {
    case LOAD:      // precondition: gui_state == Paused (call gui_open_pause() first if running)
        config::SavestateSlot.set(e.baseSlot);          // or dc_loadstate(<path>) for a branch-folder state
        gui_loadState();                                // emu.stop(); dc_loadstate; LoadStateFrame -> frame_number = F;
                                                        // stays Paused; stopLive()            (gui.cpp:4553-4590)
        if (dojo.frame_number != e.F) { fail("base state frame mismatch"); return; }
        e.st = BAKE; break;

    case BAKE: {    // k blanks + S + R tail, ALL explicit rows -> no residue from pass k-1 (§6.3)
        tas_macro::Macro m;
        for (int i = 0; i < e.k; i++) m.frames.push_back({});
        m.frames.insert(m.frames.end(), e.S.frames.begin(), e.S.frames.end());
        for (int i = 0; i < e.R; i++) m.frames.push_back(e.tailFrame(i));
        // one ApplyEdit = one timeline event; force REPLACE regardless of the Merge switch
        if (tasMacroPlaceFrames(m, e.F, "experiment k", /*mergeMode*/0) < 0 && !e.identicalToLive) { fail(); return; }
        dojo.macro_armed = true;                        // READ-WRITE: the roll drives, a silent pad preserves
        tas_auto::clearAll(); tas_auto::stopLive();     // no stray overlay (Reset does the same, dojo.cpp:2884-2885)
        dojo.experiment_running = true;                 // NEW gate: pad OUT + TAS hotkeys blocked (mirror of the
                                                        // seed window: dojo.cpp:514-515, gamepad_device.cpp:115)
        settings.input.fastForwardMode = e.fast;        // like onenter_ff (gui.cpp:4333-4340)
        e.stopFrame = e.F + e.k + (u32)e.S.frames.size() + e.R;
        gui_step_frames((int)(e.stopFrame - e.F));      // stepping + target_step_frame = stopFrame; emu.start()
        e.st = RUN; break; }

    case RUN:       // the OSD hook stops at frame_number >= target (gui.cpp:4341, 4390-4391)
        if (gui_state == GuiState::Paused && !dojo.stepping /*or*/ && dojo.frame_number >= e.stopFrame)
            e.st = SNAP;
        break;

    case SNAP: {    // emulator stopped, render thread: exactly gui_saveState's environment
        settings.input.fastForwardMode = false;
        const u32 actual = dojo.frame_number.load();    // record it: overshoot is possible (§6.2)
        const std::string out = e.branchDir(e.k) + "/outcome.state";
        dc_savestate_path(out);                         // NEW small overload (dc_savestate takes a slot today,
                                                        // nullDC.cpp:111; dc_loadstate(std::string) exists, :214)
        tas_thumb::captureForState(out);                // <out>.png, render thread + stopped = allowed (thumbnail.h:10-14)
        tas_ruler::snapshot(e.F + e.k, e.F + e.k + 1, s);   // raw skip sample at S's first frame
        const tas_mvc2::GameState gs = tas_mvc2::read();    // scene frame etc. (Phase 3: HP / hits)
        e.record(e.k, actual, s, gs);
        tas_macro::Save(e.branchDir(e.k) + "/" + name + "_macro.txt", m_k, err);   // the branch macro (B.3)
        e.k++;
        e.st = e.k < e.N ? LOAD : RESTORE; break; }

    case RESTORE:   // put the parent's roll back (the pre-sweep snapshot of [F, F+N+len+R)) so the prefix-hash
                    // exoneration revalidates the user's own states (dojo.cpp:644-654); write experiment.json
        dojo.ApplyEdit(e.rollBefore, "experiment restore");
        dojo.experiment_running = false; e.st = DONE; break;
    }
}
```
Notes on the loop:
- `gui_step_frames` only starts the emulator when `dojo:Training` or `play_match` (`gui.cpp:5936-5943`); Record /
  Macro sessions boot Training (`CLAUDE.md:186`, `CANON_macro_mode.md:167`), so this holds in the intended sessions.
- **Can it run without rendering each branch?** Not with today's primitives: the stop is a render-thread check per
  presented frame (`gui.cpp:4341`), threaded rendering presents one frame per `rend_single_frame` (`emulator.cpp:986`),
  and the capture invariant needs every frame rendered anyway (`CLAUDE.md:234-236`). What IS available: FF
  (`fastForwardMode`, the seed already uses it) — the branches run back-to-back on the one screen at FF with a
  progress line, i.e. "sequentially on one screen with a progress bar". Budget: ~0.5 s per state load
  (`nullDC.cpp:295-296`) + the run at FF; for N=10, len(S)+R≈120 frames, roughly 10-15 s total. A true "headless
  burst" (emu-thread-side stop after exactly R frames, no present) is Phase 2b research: the emu thread can arm the
  stop itself (the macro-READ pause does: `dojo.cpp:1937-1941`) but the actual `emu.stop()` still happens in the OSD.
- **Alternative batch path with only CLI keys (parallel across cores):** one headless process per branch folder:
  `-config dojo:PlayMacro=yes -config dojo:MacroMode=yes -config dojo:PlayMacroClip=<dir> -config
  dojo:PlayMacroFile=<dir>/<macro>.txt -config dojo:StartupPrompt=no -config dojo:NativeConsole=no -config
  dojo:UiIni=no` boots to State 0 with the macro injected (`gui.cpp:950-957`, `dojo.cpp:1793-1842`) — but it PAUSES
  at the handoff waiting for Space (`gui.cpp:4390-4391`), and `AutoCapture` only starts under `play_match`
  (`mainui.cpp:198`). Needs one new flag ("auto-resume after handoff, run R frames, snapshot, exit") to be usable;
  outcomes would be `.mov`/PNG per process. Good for overnight sweeps, not for the interactive "affect all 10" loop.

### D. Reviewing outcomes — the "Experiment" view
One row per branch: `k` · start frame · raw skip sample at start (`x/o`, `count/rate`) · outcome thumbnail
(`imguiDriver->getTexture(<png>)` re-queried every frame — never cache an `ImTextureID`, `CLAUDE.md:392-397`) · actual
end frame · RAM facts (Phase 3) · verdict/notes cells (click-to-edit like the Generations pane, `CLIP_SCHEMA.md:84`).
Row actions:
- **View k**: load `outcome.state` of branch k — ONLY after re-baking branch k's macro into the roll (the outcome
  state's prefix belongs to k's roll; loading it against another branch's roll is a dead-timeline state by the
  guard's own rule, `dojo.cpp:611-617`). In-session: `tasMacroPlaceFrames(m_k, F, ...)` then `dc_loadstate(path)`.
- **Replay k**: LOAD -> BAKE(k) -> RUN once, unpaused or frame-stepped, for eyeballing (the Phase 1 hotkey).
- **Adopt k into main**: the roll already holds k after Replay k; the sibling's "merge" = keep it, re-save the user's
  states (`CLAUDE.md:210-212`), write the parent macro. Until the sibling lands, Adopt == "leave k baked".
- **Jump to branch k / Open folder**: the sibling's checkout; today a checkout is a session boot
  (`LoadMacroFull` runs pre-boot only, `dojo.cpp:1786-1789`).
- **"Affect all 10 at the same time"** = the branch macros are DERIVED (`k blanks + S`), so editing `S` once (in the
  Notepad) and pressing **Re-run** regenerates all N and re-runs the loop; a changed `S` hash starts a new
  experiment version (old outcomes kept under the old hash). Also cheap: "Shift all by +1" = `k0 += 1`, re-run.
- RAM facts today: only what `tas_mvc2::read` returns (`mvc2.cpp:48-62`: skip rate/count, scene frame, total
  frames, latched input flags). There are NO HP / hit / combo-counter addresses in the repo (the only other
  `ReadMem8_nommu` calls are per-game win counters for netplay scoring, `dojo.cpp:146-264`, none for MvC2). The
  trainer CT "holds many more (hitbox lists, positions) — port them the same way when needed" (`ROADMAP.md:239-240`)
  = Phase 3. Lua can read RAM per vblank (`lua.cpp:95`, `:674-691`) to prototype an address before porting it.

### E. Reuse / placement
- **Where:** a new dockable studio module **Experiments** riding with `show_tas_tool_windows` (`dojo_gui.h:60`)
  like Notepad / Input Sender / Snippets / Macros — not a Notepad tab (the Notepad is the S editor, and the loop's
  status/progress and the outcome table need their own surface). Its "S = Notepad" button reads
  `tasNotepadCurrentLines`; "S = staged" reads `tasStaged`; "S = snippet" uses the library picker.
- **Reuse:** `tasFrameskipCheckbox` for PHASE mode (`dojo_gui.cpp:2884-2912`), `tasTargetPicker` semantics for a
  "base = Active" convenience, `tasMacroPlaceFrames` for the bake, `gui_loadState`/`gui_step_frames` for the run,
  `tas_thumb::captureForState` + `tas_ruler::snapshot` + `tas_mvc2::read` for outcomes, the Generations pane's
  click-to-edit table idiom, the `ClipUiRequest` dispatcher for popups (commit `5bad837`), `TAS_HOTKEYS[]` for
  hotkeys (`CLAUDE.md:176-178`, chain `CLAUDE.md`'s "Adding a baked-in hotkey" list).
- **Hook to the auto re-send:** the experiment is "auto re-send, N times, with a stagger". Its LOAD step is the same
  `gui_loadState` that bumps `load_seq`; the Notepad's watcher (`dojo_gui.cpp:7610-7615`) would ALSO fire and inject
  the whole notepad live on top of each pass — the experiment must set `npAutoSeenLoad = dojo.load_seq` after its
  own loads (or the loop must refuse to start while `npAutoResend` is on). Conversely the manual sweep today
  (Wait-for-Frameskip + `skip+N` + Auto re-send) is exactly the PHASE-mode experiment with N ≤ 4 and no outcomes —
  keep it as the zero-cost fallback.
- **cfg keys (new, `dojo:` section):** `ExpBranches`, `ExpFirstOffset`, `ExpRunFrames`, `ExpUnit` (roll|phase),
  `ExpFastForward`, `ExpOutcomePng`, `ExpOutcomeMov`, `ExpTail`; per-experiment values persist in `experiment.json`.
  Existing keys read: `WaitForFrameskip`, `FrameskipOffset`, `SendMerge` (must be forced OFF for the bake — pass
  `mergeMode 0`), `StateThumbnails`, `ThumbnailWidth`, `PurgeStale`, `MacroMode`, `Training`.

---

## 6. Blocking issues / risks (each with the code fact behind it)

1. **Thread ownership.** `session_inputs` belongs to the emu thread while running (`dojo.h:311`); `ApplyEdit` and the
   place verbs are GUI-side and gated on Paused everywhere today. The state machine must only touch the roll in
   LOAD/BAKE/SNAP/RESTORE with `gui_state == Paused` (the boot handoff and Auto-Send prove the pattern).
2. **Stop-frame overshoot.** The step stop is a render-thread check with `>=` because "the emu thread can overshoot
   the target between OSD checks" (`gui.cpp:4341`). Record `endFrameActual`; expect 0-1 frames. If exactness matters
   later, arm the stop on the emu thread (pattern `dojo.cpp:1937-1941`) — still stopped by the OSD.
3. **READ-WRITE residue** (`WRITE_MODE_RESTRUCTURE.md:257-260`). Solved by construction: pass k bakes
   `k blanks + S + R tail` as EXPLICIT rows over `[F, F+k+len+R)`, which fully covers pass k-1's rows (its S at
   `[F+k-1, F+k-1+len)` is overwritten by blank #k and S shifted by one; its tail by the new tail). Do NOT use the
   live path (`playLive`) for the experiment: it ORs/stomps per frame and leaves the R tail to whatever the roll
   held (`dojo.cpp:2097-2118`).
4. **Dead-timeline guard side effects.** Each pass = one timeline event (`dojo.cpp:1469-1473`) at frame F, so every
   user state saved above F goes STALE by rule (`:611-617`), and with `dojo:PurgeStale=yes` slots 1-99 get DELETED
   (`gui.cpp:4767`; default is OFF since `71ea209`, `WRITE_MODE_RESTRUCTURE.md:217-218`). Mitigations: force purge
   off during the sweep; save outcome states OUTSIDE the slot range (path overload) so they are never purge targets;
   RESTORE the pre-sweep roll bytes at the end so the prefix-hash exoneration turns the user's states clean again
   (`dojo.cpp:644-654`, `ROADMAP.md:841-845`). Outcome states are only valid against THEIR branch's roll (§5.D).
5. **Locked ranges.** `ApplyEdit` silently drops changed frames inside locked ranges (with a toast,
   `dojo.cpp:1407-1434`); `base_prelock` locks `[0, frame(BASE))` (`dojo.h:350`). Check `gui_frame_locked` over
   `[F, F+N+len+R)` before starting and refuse (the bake would be partial, the sweep meaningless).
6. **Pad leakage during runs.** In READ-WRITE any pad press records into the cell (`dojo.cpp:578-587`) and the live
   overlay bakes (`:2097-2118`); TAS hotkeys (F1/F3/Space) would derail the loop. Add an `experiment_running` gate
   in `MapleRecordAction` (mirror of `:514-515`) and in `tasHotkeysBlocked()` (`gamepad_device.cpp:115`), released
   in RESTORE/DONE and in `Dojo::Reset`.
7. **Macro autosave rewrites the CLIP's macro.txt.** Every `ApplyEdit` sets `macro_save_pending` (`dojo.cpp:1459`) and
   the GUI tick rewrites `<clip>_macro.txt` ~0.35 s later while paused (`dojo.h:310-317`); the live save also fires
   every 30 recorded frames (`dojo.cpp:440-451`). During a sweep the parent's macro file would flip through branch
   contents. Suppress while `experiment_running` and let RESTORE's `ApplyEdit` trigger one final correct write. The
   `.flyr` also grows by one appended record per changed frame per pass (`:1474`) — harmless but permanent.
8. **Merge switch.** With `dojo:SendMerge=yes` a bake ORs and an empty frame leaves the cell (`dojo_gui.cpp:2325-2342`)
   — blanks would NOT clear residue. Always pass `mergeMode = 0`.
9. **`dc_savestate` takes a slot only** (`nullDC.cpp:111`); outcomes in slots collide with the F2 cycle, the States
   wall and purge. Add `dc_savestate(const std::string& path)` (the load side already has the overload, `:214-217`).
   `gui_saveState`'s extras (wave snapshot, epoch, WriteClipStats) are not wanted for outcomes.
10. **Thumbnails need DX9/DX11** (`GetLastFrameRGB` returns false elsewhere, `Renderer_if.h:71`; `CLAUDE.md:325-327`).
    Toggling FF rebuilds the render context and invalidates GPU textures (`CLAUDE.md:392-396`) — the outcome table
    must re-query textures each frame (already the house rule).
11. **Base-state validity.** `gui_loadState` refuses empty slots (`gui.cpp:4560-4568`) and warns on STALE states
    (`dojo.cpp:721-729`) but loads anyway — the experiment must refuse a stale base (its prefix must match the live
    roll below F, else branches inherit a dead timeline).
12. **`play_match` sessions.** Bake verbs need `!play_match` (`dojo_gui.cpp:7430`, `dojo.cpp:1412`); READ sessions
    must flip to READ-WRITE first (`gui_enter_readonly` exists for the reverse, `CANON_readwrite_model.md:114`). Do
    not auto-flip silently — David removed auto-arm from edit paths ("respect the toggle", `:170-172`).
13. **Movie vs Macro sessions.** Record Movie is WRITE by default and the OnEnter handoff drops a borrowed READ-WRITE
    (`gui.cpp:4386-4387`); macro sessions are READ-WRITE (`gui.cpp:939-944`). The experiment requires READ-WRITE and
    should say so, like Auto-Send's tooltip (`dojo_gui.cpp:10582-10584`).
14. **Branch checkout is a reboot today.** `LoadMacroFull`/`LoadClipState0Boot` run pre-boot and the state load is
    deferred to the boot handoff (`dojo.cpp:1786-1789`, `gui.cpp:4392-4405`); `hostfs::savestateFolderOverride` is
    cleared by `gui_start_game` (`gui.cpp:887`). Until the sibling delivers an in-session checkout, the loop must
    run ALL branches inside ONE session against the parent's base state and materialize folders afterwards.
15. **Phase mode fallback.** The Wait-for-Frameskip deadline fires after 60 frames with "No frameskip seen - sending
    anyway" (`dojo.cpp:1911-1917`, `dojo_gui.cpp:2879`) — in an experiment that must be a hard error.

---

## 7. Phased plan

**Phase 0 — already available (document it as the baseline):** Notepad holds S; base state in a slot; READ-WRITE;
Wait for Frameskip ON, `skip+N`; Auto re-send on reload ON; F3 -> watch -> bump N -> F3
(`WRITE_MODE_RESTRUCTURE.md:31-39`). This is a 21-way (`0..20`) phase-relative stagger with no persistence and no
outcomes. Its two flaws for David's ask: nothing is kept, and he must watch each pass.

**Phase 1 — manual branches + "next branch" hotkey (no new engine):**
1. `tas_experiment` module: build `S` from Notepad/staged/snippet; generate `N` macros `k blanks + S (+ R tail)` and
   write them with `tas_macro::Save` into `<clip>/experiments/<name>/branch_kNN.txt`; write `experiment.json`
   (base slot/frame/prefixHash, S text+hash, N, k0, R).
2. Hotkey `EMU_BTN_EXP_NEXT` (chain: `gamepad.h` enum, `gamepad_device.cpp` case, `keyboard_device.h` default,
   `mapping.cpp`, `TAS_HOTKEYS[]`): if Paused -> `gui_loadState` (base slot) -> `tasMacroPlaceFrames(m_k, F, "exp",
   0)` -> toast "branch k/N baked at F+k — Space/P to play" -> k++. Optional: also `tasMacroStageFile` so the roll
   shows the amber STAGED chip. `EMU_BTN_EXP_PREV` mirrors it.
3. Experiments window v0: the list of branches with `k`, start frame, raw skip sample from `tas_ruler::snapshot`
   (if the frames were polled), a "bake this branch" button per row, "Re-generate from Notepad".
   Deliverable: David can cycle 10 timings by one key, each pass residue-free, without leaving the session; the
   sibling can adopt the folder layout from the generated files.

**Phase 2 — automated loop + outcome PNGs:**
1. The state machine of §5.C in `tas_experiment::tick()` (called from `show_tas_tool_windows`), with the
   `experiment_running` gates (pad out, hotkeys blocked, autosave suppressed, purge suppressed, Merge forced off),
   FF during RUN, progress line ("branch 7/10 — frame 6901/6944"), Esc = abort -> RESTORE.
2. `dc_savestate(path)` overload; outcome `.state/.frame/.png` per branch via `tas_thumb::captureForState` (add an
   optional full-res width for the experiment: today `ThumbnailWidth` caps at the source width, `thumbnail.cpp:88-92`).
3. RESTORE step + prefix-hash revalidation; `experiment.json` completed with actual end frames and skip samples.
4. Experiments window v1: thumbnail row (re-queried textures), View/Replay/Re-run/Adopt (Adopt = leave baked until
   the sibling's merge), notes/verdict cells.
5. Optional: per-branch `.mov` via `avi_toggle_recording()` around RUN (27 fps at full res — slow; default off), and
   `dojo:FidelityDump`-style per-frame JSONL for write sessions (today it is playback-only, `dojo.cpp:1264`).
6. Phase 2b research: an emu-thread-side exact stop (no overshoot) and a no-present burst mode; the headless CLI
   variant ("auto-resume after handoff + run R + snapshot + exit") for parallel overnight sweeps.

**Phase 3 — RAM-read outcome facts:**
1. Port HP / hit-count / combo-counter / positions from the trainer CT into `tas_mvc2` (same `0x8C000000 + offset`
   rule, `mvc2.h:9-11`), validate with `MemTrace`/`MemHunt` (`mvc2.cpp:94-193`), prototype addresses first with Lua
   `flycast.memory.read*` in a `vblank` callback (`lua.cpp:95`, `:674-691`).
2. Per-branch facts at `E_k` plus a per-run "max combo counter / total damage" probe on the emu thread (extend
   `tas_ruler::onPoll`'s per-poll read, `tas_ruler.cpp:22-47`, or a sibling `tas_probe`).
3. Verdict column auto-filled ("hits: 20 -> 21", "dropped at frame X"), sortable like the Generations table
   (commit `574665f`), and the "adopt the best" shortcut.
