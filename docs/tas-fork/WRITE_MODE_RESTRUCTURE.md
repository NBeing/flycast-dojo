# WRITE Mode Restructure — Notes & Open Investigation

**Status (2026-09-03): PCSX2-RR mapped from its source (sections 4-5) and the WRITE state-load semantics DECIDED and
built — v2.0+ parity: no truncate on load, the un-reached old take stays and is greyed in the roll (`f09a847`).**
Still open: the **Demul** comparison (David to drive), re-record-count semantics (PCSX2 counts loads; we count
divergence runs), lock-above-load-point behavior, and the two `Reset()`/`rewind_log` leaks noted in section 4.
The frameskip live-inject stopgap (section 1) remains until "observe the piano roll" is a first-class behavior.

---

## 1. What we built and VERIFIED (2026-09-03)

### The frameskip sweep methodology
Goal: test a combo at each of MvC2's frame-skip phases to find which phase(s) connect.

MvC2 **vs-mode skips every 4th frame** (`skipRate = 4`). So a combo whose timing straddles a skip boundary only
connects on **one of the four phases**. Verified live: **Magneto `s.HP × 33 → s.LP × 1` connects ~1/4 of the time**
(the s.HP recovers in time for the next normal only on the phase where the skip lands right).

The tool: the Notepad's **Send → Live** button (parity with the Input Sender; added 2026-09-03) + the shared
**"Wait for Frameskip" checkbox + "skip+ N" offset**, which times ONLY the live send:
- **Replace / Insert / Append** always BAKE into the piano roll (authoring; Follow tracks it). The box never
  touches them (an earlier version hijacked them - that broke Follow and was retracted).
- **Send → Live** LIVE-INJECTS the notepad macro: box **OFF** = next frame; box **ON** = held until the game's
  next skip/reset frame and fired at **skip+N**. Sweep N = 0,1,2,3 to walk the four phases.
- A bake into the WRITE amber (old-take) region clears the amber for the frames it writes.
- **Merge @N** (`22e63d9`): the OR twin of Replace and the only non-stomping bake - buttons OR onto the roll
  (roll LP + notepad HP = LP+HP, never 0), a notepad direction replaces the frame direction only if present (two
  directions are never ORed into SOCD), and an empty notepad frame leaves the roll frame untouched.

The loop (one pass per phase):
1. **WRITE** mode, sitting on a save state.
2. Wait for Frameskip **ON**, `skip+ x` (x = 0,1,2,3).
3. Send the notepad macro → this **arms** the injection (it does not fire yet).
4. **Step / resume** — the game runs into the next skip, the macro fires at `skip+x` and plays out the attacks.
5. **F3** (load state) to reset; bump `x`; repeat.

With **Auto re-send on reload** ON (beside Send → Live, `1faa914`), step 3 is automatic after every F3 - the loop
is just: set `skip+ x` → F3 → step → watch → bump `x` → F3 ... Session-only: it never auto-fires in a fresh session.

### Why LIVE-INJECT and not a bake — the crux
A **baked** macro (Replace @frame) cannot play in WRITE: WRITE overwrites every frame with the live pad, so it
**clobbers** the baked cells before they fire ("no extant way to observe the piano roll"). A **live injection**
instead ORs onto the pad each frame (the `tas_auto` live overlay, `core/dojo/dojo.cpp` `MapleApplyAction` ~L1969) —
it **drives** the guest and survives WRITE. So it plays un-clobbered. The live-inject is our stopgap for the
missing "observe the piano roll" behavior.

Config: `dojo:WaitForFrameskip` (bool) + `dojo:FrameskipOffset` (the N). Skip signal read in
`core/dojo/mvc2.cpp` — `skipRate`/`skipCount` and the toggle byte `0x8C289622` (255 on the reset frame).

---

## 2. The WRITE-mode gap (what we want to restructure)

Current driver model (`CANON_readwrite_model.md` / `CANON_driver_model.md`):

| Mode | Flag | Behavior | Plays a baked macro? | State reload |
|---|---|---|---|---|
| READ | `play_match` | plays the roll, read-only | yes (roll drives) | replays same roll |
| READ-WRITE | `macro_armed` | roll DRIVES + overdub | yes | roll **persists** (no clean wipe) |
| WRITE | neither | advancing OVERWRITES every frame w/ the live pad | **no — clobbered** | effectively **wipes** |

The sweep wants **play the roll's content (observe) AND wipe on reload for a clean re-test**. No mode gives both:
- READ-WRITE observes but doesn't wipe.
- WRITE wipes but clobbers (doesn't observe).

The live-inject stopgap sidesteps this (it drives the guest live instead of relying on the roll), but it is not
the right long-term model.

**David's concern:** the current WRITE behavior during **RECORD MOVIE** and **RECORD MACRO** does **not** exactly
match **PCSX2-RR** or **Demul**. That must be pinned down before redesigning.

---

## 3. Open questions / next steps (deferred — David to drive the testing)

1. **Reference behavior — document exactly how PCSX2-RR and Demul handle:**
   - Recording *over* existing movie input (overwrite vs insert vs observe-and-pass-through).
   - A state load *during* recording (truncate the movie? wipe? keep the tail?).
   - Playing existing input *while* recording (the "observe" case).
2. **RECORD MOVIE vs RECORD MACRO:** same WRITE semantics, or intentionally different?
3. **"Observe piano roll" as a first-class behavior:** a WRITE-like mode that PLAYS the roll where it has content
   and records the pad where it doesn't — so a sent macro plays *and* a state reload gives a clean slate. This is
   the behavior the frameskip sweep really wants; the box-gated live-inject only approximates it.
4. **Retire the stopgap:** once the model is right, the box-gated Notepad live-inject may fold into the mode itself
   (and the "Wait for Frameskip" box in the Notepad — currently the on/off switch for it — gets re-evaluated).

---

---

## 4. Movie-mode savestate semantics — as MAPPED (2026-09-03, two read-only agents)

Scope: Record Movie / Play Movie / `.flyr` replays. **Macros excluded.** This is what the code DOES today; the
design decisions built on it are in §5.

### 4.1 State 0 ("BASE") is an ordinary slot wearing a protective UI
Everything special about slot 0 is filename + UI + a seek convention — **there is no slot-0 branch in save, load,
or timeline mutation.**

| Aspect | Slot 0 | Where |
|---|---|---|
| Filename | `<game>.state` (no `_N`) | `oslib.cpp:144-159, 265-283` |
| Session open | active slot forced to 0 (record + playback) | `replay.cpp:20-34, 396-407` |
| F1 save | must HOLD `BaseHoldMs` (1 s) when a BASE exists; F4 shows a confirm modal | `gamepad_device.cpp:176-207`, `mainui.cpp:177-191`, `gui.cpp:5175-5203` |
| Delete | blocked in the Timeline menu + lock strip | `dojo_gui.cpp:14277-14282, 14700-14704` |
| Auto-purge | exempt (files kept when stale) | `gui.cpp:4625-4650` |
| Staleness | NOT exempt — flagged like any slot | `gui.cpp:4600-4607` |
| Lock | locks the PRE-roll `[0, earliest anchored frame)`, not a forward range; on by default | `gui.cpp:4707-4726`, `dojo.h:289` |
| R WRITE→READ at frontier | auto-seeks slot 0; refuses if none | `gamepad_device.cpp:361-377` |
| Sidecar `.frame` | **identical** to other slots (`{frame, seq, movieLen, prefixHash}`) | `dojo.cpp:627-650` |
| Load path | **identical** to other slots | `gui.cpp:4445-4482` → `nullDC.cpp:219-306` → `dojo.cpp:652-750` |
| Timeline anchoring | **none** — the movie's origin is power-on frame 0; the "prefix" role is the per-state `prefixHash` | `gui.cpp:929-936` |

### 4.2 There is NO truncate-on-load — for any slot, in any mode
The load chain (F3 → `gui_loadState` → `dc_loadstate` → `Dojo::LoadStateFrame`) does exactly two things to the
movie: `frame_number = fn` (`dojo.cpp:711`) and, in write modes, `divergence_open = false` (`:718`).
`session_inputs` is never touched. `ApplyEdit` explicitly REFUSES truncation (`:1317-1330`). The only shrink path
is `ApplyEditResize` (`:1420-1504`, row delete/insert) — never reached by a load. The sidecar even records
`movieLen`, but nothing consumes it (write-only).

### 4.3 Load-state-N behavior (N = 0..99 — identical for all slots)

| Mode | At load | On the next advances |
|---|---|---|
| READ | seek `frame_number = fn`; movie untouched | pure playback; ReplayEnd when exhausted; `.flyr` untouched |
| READ-WRITE | seek; movie untouched — **KEPT by design** | silent pad preserves the cell (roll drives); a pressed pad stomps that ONE cell in place |
| WRITE | seek; movie untouched — **LINGERS** | every advanced frame REPLACED by the live pad (neutral included), one cell per advance (`dojo.cpp:426`). **Cells beyond where you stop advancing SURVIVE**, in memory and on disk |

### 4.4 What substitutes for truncation today
Instead of deleting downstream INPUTS, the fork invalidates downstream STATES:
- **Dead-timeline guard:** `rewind_log` + `IsStateStale` (`dojo.cpp:582-625`) — a state is stale iff a re-record
  happened BELOW its frame after it was saved (strict `<`: the loaded state itself stays clean).
- **Auto-purge:** `gui_purge_stale_now` (`gui.cpp:4625-4669`) deletes stale slots 1-99 (slot 0 exempt); default ON.
- **Range locks:** `dojo.locked_slots` protect cells from a WRITE pass (the opposite tool).
- **`.flyr` is APPEND-ONLY, last-write-wins per frame** (`dojo.cpp:2452-2457`; `replay.cpp:222-285`). A re-record
  leaves both the old and new records; the untouched tail resurrects on reload — mirroring the in-memory map.

### 4.5 vs PCSX2-RR

| | PCSX2-RR | This fork |
|---|---|---|
| READ load | seek, movie untouched | **matches** |
| READ-WRITE | (no analog) | fork extension: keep-on-reload + overdub |
| WRITE load | **truncate at `fn`**, re-record from there | **no truncate** — lazy overwrite-in-place on advance; tail survives |
| Re-record count | bumps on every LOAD while recording | bumps only at the first byte-DIFFERING write after a load/R (a divergence run = 1); a load alone bumps nothing; a hands-off retry over already-neutral cells bumps nothing |
| State 0 | no special slot | no special slot at the timeline level (UI protection only) |

### 4.6 The surgical hook for truncate-on-load-in-WRITE (NOT built — design decision pending, §5)
`Dojo::LoadStateFrame`, **`dojo.cpp:712-720`** — the write-mode branch. It already runs only when
`!play_match && recording_started`, already has `fn`, and can gate on `!macro_armed` to leave READ-WRITE's keep
intact. Hand the cut to the one funnel that already knows how to shrink: build `edited = session_inputs` restricted
to keys `< fn`, call `ApplyEditResize(edited, "...")` (`:1420-1504`) — it does the erase (`:1493`), the undo patch
(`:1470-1488`), `rerecord_count++` + `rewind_log` with `first = fn` (`:1495-1496` — the loaded state stays clean under
the strict `<`; everything above goes stale/purged), `RewriteReplayFile()` (`:1498`), and stats. `tasWriteGrow` then
materializes neutral cells past the new end, so the frontier falls out for free.

Caveats: (a) `ApplyEditResize`'s structural-lock refusal (`:1453-1465`) vetoes the cut if any locked range's
`hi > fn`; (b) `gui_loadState` holds `emu.stop()` (`:4468`), so the funnel runs with the emu parked — good.

### 4.7 Piano roll vs "the movie's internal input history" — they are the same thing
The piano roll renders `session_inputs` row by row, and `session_inputs` is exactly what is fed to the game. There
is no separate internal history. The `.flyr` on disk is the persistent copy (append-only, last-write-wins on load;
rewritten shorter by `ApplyEditResize`, so after a truncate-on-load disk == roll). No second window is needed for
the movie. The only thing the roll does not show is the EDIT history (what a truncation/edit removed) — that is
the undo stack (Ctrl+Z), a per-session thing, not a movie.

---

## 5. Design decisions (OPEN — David)

1. **Truncate-on-load in WRITE** (PCSX2-RR): implement at the §4.6 hook? Gate `!macro_armed` so READ-WRITE keeps.
2. **State 0 / BASE:** with truncation added, does loading BASE truncate too (pure PCSX2-RR — it's an ordinary
   slot), or is BASE the exception that KEEPS the timeline (David's "amalgamation" model: loading BASE = return to
   the anchor and re-record over it; loading 1-99 = branch, wipe the future)?
3. **Re-record count semantics:** keep the fork's divergence-run counting, or switch to PCSX2-RR's count-the-load?
4. **Locked ranges vs truncation:** if a locked range sits above the loaded frame, refuse the load, refuse the cut,
   or cut around it?
5. **PCSX2 parity — CORRECTED 2026-09-03 from the `pcsx2-1.4.0-rr` source (pocokhc's fork, `pcsx2/TAS/`).**
   The earlier claim here that PCSX2-RR "embeds the movie in each savestate" (self-contained branches) was WRONG.
   What the code does:
   - A savestate embeds ONLY `g_FrameCount` (4 bytes, `SaveStateBase::keymovieFreeze`, `KeyMovie.cpp:19-27`).
     No input log, no hash, no filename.
   - The movie is ONE `.p2m2`: fixed 12-byte stride/frame, random-access (`fseek` by frame), written IN PLACE
     (`KeyMovieOnFile.cpp:5-16, 52-82`). `MaxFrame` only ever RAISES in v2.0+ (`updateFrameMax`, L198-207).
     Nothing validates that a state still matches the file.
   - Load in RECORD: frame counter reset from the state; the next recorded frames OVERWRITE those blocks; the
     stale tail past where you stop STAYS on disk. `UndoCount++` on EVERY load, any mode, written immediately.
   - Load in READ-ONLY: seek only; log untouched; playback continues from that frame (David's hypothesis —
     CONFIRMED, `KeyMovie.cpp:76-89`). A later state "remains loadable" after a re-record only because the array
     never shrinks and nothing checks it — it silently resumes the OLD tail: a desync PCSX2 does not detect.
   - No slot 0 / base concept; all 10 slots identical (`GlobalCommands.cpp:466-475`).
   - Truncation HISTORY: v1.1 truncated (`FrameMax = g_FrameCount`) at the R REPLAY→RECORD toggle; pre-v2.0
     tracked FrameMax at the write head; v2.0+ dropped truncation (only-raise), presumably for the KeyEditor.
   - No history toward David's 0.9.6 binary (that is the old code.google pcsx2-rr; this repo starts at a 1.4.0
     snapshot). The old `.p2m` converter comment implies the same fixed-stride/random-access model (inference).
   - FILE (agent B): 152-byte text header {version=1, ID=0xCC, emu[50], author[50], cdrom[50]} + u32 `MaxFrame`
     @152 + u32 `UndoCount` @156 = 160-byte header; then 12 bytes/frame (2 ports × 6 pad bytes); seek =
     160 + frame×12; every recorded byte is fseek+fwrite+fflush IN PLACE, indexed by the VSync `g_FrameCount`.
   - MOVIE END = `MaxFrame` INCLUSIVE, the highest frame EVER recorded (high-water). After a rewind + shorter
     re-record, REPLAY RUNS PAST YOUR NEW ENDING into the stale old tail — the concrete cost of "never truncate".
     New Record opens "wb+" = truncates the file to EMPTY (after one `<file>_backup`) — the only fresh-start path;
     extending/re-recording an existing movie is ONLY via the R toggle on a "rb+" Play.
   - RERECORD = a pure LOAD COUNTER: `UndoCount++` inside `keymovieFreeze` on every `IsLoading()` — in REPLAY too
     (written to disk, so "read-only" is not read-only on disk) and even on the plugin-apply round-trip.
   - NO ANCHOR: a movie starts wherever `g_FrameCount` is; New Record at frame N>0 leaves 0..N-1 zero-filled =
     active-low "every button pressed" (silently non-replayable from power-on). Our explicit State 0 anchor is a
     real improvement over the reference; their zero-fill hazard cannot happen in ours.
   - 0.9.6 `.p2m` (converter comment, KeyMovieOnFile.cpp:261-264): 8-byte {FrameMax, Rerecs} + 6 bytes/frame,
     pad 1 only, seek 8+frame×6 — the same table / in-place / any-load-bumps model (inference; v1.0 copied it
     verbatim), so David's 0.9.6 binary almost certainly behaves this way.
   CONSEQUENCE: our fork's ORIGINAL WRITE behavior (overwrite-in-place, un-reached tail survives) was already
   PCSX2-RR v2.0+ behavior — plus safety PCSX2 lacks (prefix-hash + STALE warning). What felt wrong is that our
   piano roll SHOWS the lingering tail; PCSX2 has no roll view, so it is invisible there. The "per-state movie
   prefix / branching" restructure is NOT a PCSX2 feature and is RETRACTED as a parity goal.
6. **Interim, BUILT 2026-09-03:** truncate-on-load in WRITE (`60663fa`; BASE exempt, `dojo:TruncateOnBase=yes`
   cuts BASE too) — STRICTER than PCSX2-RR v2.0+ (never truncates) but matches its v1.1 toggle-truncate in
   spirit; auto-purge default OFF (`71ea209`) — matches PCSX2 (never deletes states). Loading a stale state
   warns (STALE toast) and proceeds; PCSX2 does the same thing SILENTLY.
7. **DECIDED 2026-09-03 (David): (a) v2.0+ parity — NO truncate-on-load.** Built as a WRITE-only stale-tail
   marker (`dojo.stale_tail_from`: set on a WRITE load, advanced as frames are re-recorded, cleared on Reset)
   that greys the un-reached old take in the roll; the `60663fa` truncate and `dojo:TruncateOnBase` are removed;
   the BASE exception is moot (no slot truncates, matching PCSX2's no-slot-0 model). Original framing follows.
   **The decision was — which truncation semantics?** (a) PCSX2-RR v2.0+ parity: NO truncate on load;
   re-recording overwrites in place and the un-reached tail persists (the roll should then de-emphasize the tail
   past the write head, e.g. grey it, so it reads like PCSX2 where it is invisible); or (b) keep
   truncate-on-load (v1.1-style; what David originally asked for: "wipe from that state onward"). Secondary:
   re-record count — PCSX2 bumps on every LOAD (any mode); ours on the first divergence.

## 6. Macro parity (2026-09-03) - the movie-focused work re-checked against macro sessions
Audit (read-only agent) of everything above against Record Macro / Play Macro Full / Play Macro Stage. The flag
matrix that drives it: Record Macro = MacroMode + RecordMatches + a redundant .flyr; Play Macro = MacroMode +
PlayMacro, NO .flyr (`HasAppendTarget()` false) and `recording_started` NEVER true.

**A. BUILT (gate drops / widenings - display- or gate-level, no model change):**
- The stale-tail marker no longer excludes macro sessions (`!MacroMode` dropped); Play Macro counts as writing via
  `PlayMacro` (the `tasWriteGrow` rule). Hygiene: the marker resets whenever the roll is REPLACED (Full-load
  inject, in-session Load Full, boot).
- Record Macro without an OnEnter seed booted WRITE (Reset() clears `macro_armed`; only the seed re-armed it)
  while the launch tooltip promised READ-WRITE. Every MacroMode boot now arms READ-WRITE; Record Movie stays WRITE.
- `Dojo::MovieEnd()` (= last key + 1) replaces `session_inputs.size()` in every frontier test (R -> READ, the
  banner READ click, the step-hold, ReplayEnd). A Play-Macro-Full roll is keyed from State 0's frame, so size()
  undercounted and EVERY R -> READ yanked the playhead to State 0.
- Session label says PLAY MACRO for a Stage play (was RECORD MACRO); the cheats door is closed in Play Macro (the
  determinism hole); the coordination trace covers Play Macro; MacroMode context is cleared when the Macros picker
  closes without a pick; the F4 header reads ENTRY STATES in macro mode; stale strings (purge "default ON",
  "no player yet", "can branch/truncate here") corrected.

**B. OPEN - needs a design call (David):**
1. **Play Macro has NO persistence and NO re-record detection** - `recording_started` is never true, so the
   divergence detector (`PollRecordAction`), the live macro save, the teardown save and `gui_flush_macro` all
   skip it. Every bake / live send lands in memory only; ONLY Overwrite-Save writes (to `loaded_macro_path`).
   The Send -> Live tooltip ("records itself into the roll as it goes") implies otherwise. Questions: which file
   is canonical in Play Macro (`loaded_macro_path` vs `<clip>_macro.txt` - `WriteMacroFile` hard-codes the
   latter)? Should Play auto-persist at all? Risk: a live save into `<clip>_macro.txt` from a session that loaded
   a DIFFERENT .txt in the same clip would create/overwrite a second file.
2. **Auto re-send sweep in READ-WRITE accumulates residue** - the loop was designed in WRITE (F3 -> the pad
   re-clobbers -> clean pass). In READ-WRITE (every macro session) each pass's bake PERSISTS (overdub) and F3
   restores the machine, not the roll. Options: auto re-send undoes its previous bake first; "reload macro from
   file on F3" in Play Macro; or require WRITE for the loop.
3. **Record Macro's own `<clip>_macro.txt` is never the "loaded macro"** - Edit-loaded / Overwrite-Save need
   `loaded_macro_path` (Full loads only). Setting it at CreateReplayFile would light them up, BUT
   `tasMacroSaveToLoaded` rebases to `session_inputs.begin()` while `WriteMacroFile` rebases to
   `MacroAnchorFrame` - two writers, two conventions on one file. Switch the former to `MacroAnchorFrame` first.
4. **Relative vs absolute macro files in the Macros picker** - `tasMacroSaveAll` (Save Movie) is absolute from
   frame 0; `WriteMacroFile` / Overwrite-Save are relative to State 0. The picker lists every .txt with an S0
   chip, and Load Full of an absolute file double-offsets. Options: S0-chip only clip.json's `macroFile`; make
   Save Movie relative; a `# base=` header in the CE text.
5. **`_setup_NN` (F8) has no restore UI in macro mode** (STATE BACKUPS panel hidden; only the Replays pane
   restores).
6. **Picker parity**: Replays has rename / delete / multi-select / "N states" / inline Tags+Notes on the selected
   row / the PNG click viewer (a 70-line modal with function-local statics - needs hoisting to share); Macros has
   Frames/Players/Contents columns / Rescan / sortable names.
7. **Emu-thread cost of the live macro save** - every 30 recorded frames: `MacroAnchorFrame` ->
   `scanSavestateInfo` (directory walk + sidecar reads) + a dense rewrite of macro.txt, O(session). Cache the
   anchor on `savestate_epoch`; write only when the roll changed.

## Related
- `CANON_readwrite_model.md`, `CANON_driver_model.md` — the current driver model.
- `CANON_macro_mode.md` — macro sessions.
- Commits: frameskip MVP `9f3dc72`, skip+N tuning `ddd2d23`, Notepad box-gated live-inject `e198317`,
  Follow anchor fix `9d38c72`.
