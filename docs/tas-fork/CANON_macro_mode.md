# CANON — Macro Mode ("TAS Combo Studio")

**Status: DESIGN, not built.** Ground-truth for the Macro-mode feature. David + Claude, 2026-08-30.
This is the contract; code follows it. Sections marked **(proposed)** are Claude's best call awaiting David's nod.

---

## 1. The idea

A second authoring mode that sits **beside** the current Record/Play **Movie** system — it does not move or
change anything in it. The core difference:

> **There is NO replay.** The only artifact is a single canonized input list — the **macro**. It starts at
> frame 0 and ends whenever. Everything else is **dumb save states**: blind raw snapshots of the game with no
> replay attached, used only as launch points ("skip to frame X and start playing there, as we build the whole
> thing").

David: *"we can compartmentalize the system really cleanly this way."*

---

## 2. One buffer, two modes

The piano roll IS a frame-indexed input buffer — that buffer is how the roll exists, so the macro must live in
it (there is no way to have the roll edit anything else). Sharing the buffer does NOT make it "the movie": the
**mode** decides everything below. Macro mode is a thin bolt-on over the same buffer + roll + jump machinery.

| | **Movie** (today, untouched) | **Macro** (new) |
|---|---|---|
| Save states | timeline points, greenzone-verified | **dumb raw checkpoints** — "the game at frame 75", no replay |
| On disk | `.flyr` (binary replay) + states | **`macro.txt`** (editable) + states, **no `.flyr`** |
| Playback | replay the timeline | **re-pointable inject** — load a checkpoint → playhead + game jump there → macro continues |
| Baggage | spectate / desync-verify / greenzone / branching | **none** — no replay to drift |
| Authoring | controller + roll + notepad + input sender | **same four**, all writing the ONE macro |

---

## 3. Launch menu ("TAS Combo Studio")

David's mockup, extended to two columns. Picking a Macro entry is what flips every row of the table above.

```
   1  Record Movie      |   4  Record Macro
   2  Play Movie         |   5  Play Macro
              3  Just Play (no recording)
   Settings                              Quit
```

---

## 4. The macro

- The single canonized input list. Frame 0 = the first input. Runs to whatever length.
- Edited by ALL current authoring paths, each writing this one buffer (see §6).
- Saved as **`macro.txt`** (the notepad text format we already have — human-editable, portable, re-derivable).
  No binary movie. Because it is short and text-sourced, there is nothing long enough to desync.
- **Frame indexing: 0-indexed (DECIDED).** Verified in code: the piano roll gutter draws the 0-based `row`
  (frames `0..movieLen-1`), and `session_inputs` + the glyph notepad both count from 0. The ONE place showing
  1-based is the **Cardinal notepad**, and only because that's the text editor's built-in LINE numbering (a text
  convention, not a frame one) — that's what made it look 1-indexed. Macro mode is 0-indexed: **State 0 = macro
  frame 0.** (Papercut noted: the Cardinal gutter is off-by-one vs the roll/glyph/timeline — optional future fix.)

---

## 5. Dumb checkpoint save states

- A checkpoint is a **blind raw snapshot** of the game at some macro frame. No replay, no verify, no greenzone.
- **State 0 = Macro Frame 0 (REQUIRED binding).** David: *"make the user have to pick Save State 0 = Macro
  Frame 1."* Recording a macro starts by establishing this base state — the position the macro begins from.
- States 1..N are optional mid-macro checkpoints: "here is the clip at frame 75 / 100 / 150."
- **Re-point playback (the whole point):** loading a checkpoint restores its snapshot AND jumps the
  playhead (notepad + piano roll) to that frame; the macro plays forward from there. So you iterate on frames
  100–105 without sitting through 0–99. **This is the movie's existing savestate-frame-jump reused on the macro
  buffer — the smallest part of the build, not a new engine.**
- Each checkpoint stores the macro frame it sits at (so load → playhead jump is exact).
- **Making a checkpoint (DECIDED — Q4):** a "Checkpoint State + Frame" gesture, reachable from the **Timeline,
  the piano roll, AND the notepad** — it saves a dumb state at the current frame and tags it with that macro
  frame (the reload point + jump-off point, e.g. frame 75). No UX exists yet; it wires into several modules.
- **Play Macro expects a PAIR (DECIDED — Q3):** pick a macro folder → it launches `macro.txt` + **State 0** by
  default (State 0 is the expected canon launch state). The user may instead pick a different saved state, or a
  `_setup` sub-folder's own macro + state. State 0 stays the heuristic default.

---

## 6. Authoring sources (all write the ONE macro)

Controller, piano roll, notepad, and input sender all edit the same macro buffer. David: *"I'd like to use
controllers as well, but have them send inputs into the piano/notepad."* — i.e. the controller AUTHORS the
macro (writes the buffer), it does not drive a separate replay. This already matches how the shared buffer
works; Macro mode just has no `.flyr` capturing it.

---

## 7. The setup problem (everything BEFORE State 0) **(DECIDED — Q1)**

Real case: elaborate 2-character combos need a 20-second+ setup (positioning, meter, its OWN base state + a few
checkpoints + 100+ macro lines) just to REACH State 0. David wants those kept, but disjointed.

**Answer: a setup is an in-folder BACKUP, captured with the existing generation-archive (F8) mechanism.**

- **"Store as setup"** archives the current state(s) + `macro.txt` into a **sub-folder inside the main macro
  folder** — auto-named `_setup` (or user-named), exactly like F8 copies movie+states+metadata into `gen_NN`.
  Leverage `EMU_BTN_GEN_ARCHIVE` (F8) → `Dojo::ArchiveGeneration` / `ArchiveClipDir` (dojo.cpp ~1100), don't
  invent storage.
- A setup is itself a full little Macro clip (its own base state + checkpoints + 100+ macro lines) — it just
  lives as a backup beside the main macro, not as a separate top-level clip.
- State 0 (the combo start) is a frozen snapshot; the setup backup is how you FIRST produced it, kept so you can
  regenerate/share it. No live dependency, no chaining in v1.

Net: setups stay extant, disjointed, and preserved as in-folder backups — reusing the F8 archive path.

---

## 8. On-disk shape (a Macro clip folder)

```
<clip>/
  macro.txt          # the one canonized input list (notepad format)
  state_0.<ext>       # base state  (bound to macro frame 0, REQUIRED)
  state_1.<ext>       # dumb checkpoint @ frame 75   (optional)
  state_2.<ext>       # dumb checkpoint @ frame 100  (optional)
  clip.json           # mode: "macro"; per-state frame + optional provenance note
  _setup/             # optional in-folder backup(s) via F8 archive: the setup's own macro.txt + states
  NO .flyr
```

`clip.json` gains: `mode: "macro"`, and per-state `{ frame, provenance? }`. Movie clips are unchanged.

---

## 9. Bolt-on discipline (reuse vs new)

**Reuse (do NOT rebuild):** the frame-indexed input buffer, the piano roll, the notepad, the input sender, the
savestate-frame-jump, the controller authoring path.

**New (the actual build):**
1. Launch-menu entries: Record Macro / Play Macro (two-column layout).
2. A global **`macro_mode` flag** (`macro` vs `movie`, DECIDED — Q5) that: skips `.flyr` capture, marks states
   dumb (no verify/greenzone), saves `macro.txt`, and **hides Movie-only controls across every module** (spectate,
   branching, greenzone UI, replay-only send options). Modules read the flag to trim their UI.
3. Dumb-checkpoint save/load with the per-state frame + the required State-0↔frame-0 binding on Record.
4. `clip.json` `mode: "macro"` + per-state frame/provenance.
5. Play Macro = load base state (State 0 default), load `macro.txt` into the buffer, inject forward;
   checkpoints re-point.
6. The **"Checkpoint State + Frame"** gesture (save dumb state @ current frame + tag), reachable from the
   Timeline, piano roll, and notepad.
7. **"Store as setup"** = archive state(s) + `macro.txt` into a `_setup` sub-folder via the F8 archive path.

Do NOT touch the Movie path. This is additive.

---

## 10. Resolved (this pass) + what's left

**RESOLVED:** (1) setups = in-folder backups via the F8 archive path (§7); (2) 0-indexed, State 0 = frame 0
(§4); (3) Play Macro launches `macro.txt` + State 0 by default, user may pick another state or a `_setup` pair
(§5); (4) checkpoint gesture on Timeline + roll + notepad (§5/§9); (5) hide Movie-only controls behind a global
`macro_mode` flag (§9).

---

## 11. Build progress

**SLICE 1 — SHIPPED (2026-08-30):**
- `ab572ee` — Cardinal gutter 0-indexed (`SetLineNumberStart(0)`), the off-by-one fix.
- `0387220` — launch menu is two columns (Record Movie | Record Macro, Play Movie | Play Macro, Just Play; keys
  1/2/3/4/5). Record Macro boots Training with `dojo:MacroMode=yes`; Movie/Just Play set it `no`. Play Macro is a
  stub notification (no browser yet). The flag is SET but nothing reads it yet.
- `1f89b52` — F8 archives as `<clip>_setup_NN` in Macro mode (tag param on `ArchiveClipDir`), no gen bookkeeping;
  Movie F8 byte-identical.

**SLICE 2 — SHIPPED (2026-08-30):** (backed by a 4-agent Movie-only-control sweep)
- `1fc5d20` — `tasMacroMode()` accessor (dojo_gui.h/.cpp) now GATES the genuinely Movie-only, reachable UI: the
  STATE BACKUPS / generations panel (gui.cpp F4 slot browser) and the two-player name overlay. **Key finding:**
  everything keyed on `play_match` (READ/WRITE/driver banner, frame counters, input display, cell-edit guards) is
  SHARED — CANON reuses `play_match` for macro re-point playback — so it stays. There is NO greenzone in the
  codebase. The End-of-Replay dead-end IS Movie-only but sits behind a null-deref guard in the emu path — DEFERRED.
- `d5e6e4e` — macro.txt WRITE: at session-end, a Macro session persists `session_inputs` → `<clip>/macro.txt` in
  the **Notepad text format** (`canonFromPacket` + `tas_macro::Save`; NOT the `tas_text` bk2 grid). `.txt` added to
  the `ArchiveClipDir` copy list so `_setup` carries it. Movie sessions unaffected.

**NEXT SLICES (not built):**
1. **Dumb checkpoint save/load** + the required State-0 ↔ frame-0 binding on Record; per-state frame tag.
2. **"Checkpoint State + Frame"** gesture on Timeline + roll + notepad.
3. **Play Macro** browser (pick a macro folder → `macro.txt` + State 0 pair) — this brings macro.txt **LOAD**.
4. **Deferred cleanups:** gate the End-of-Replay dead-end in macro (needs care at the null-deref-guarded state
   entry, dojo.cpp:1624/1654); skip the redundant `.flyr` in macro mode (CreateReplayFile refactor that keeps
   the clip folder but writes no `.flyr`).

---

## 12. The end-to-end pipeline (David's Demul workflow → our tool)

David's Demul pipeline (ground truth for what macro mode serves):
1. Start Demul (no record/play prompt), VS mode, pick chars, reach the match, set the characters up.
2. Save **State 0** (match start) and program the **setup** macro (arduous just-frames; States 1-9 were avoided
   because of the input-offset problem — the reason our re-point model exists).
3. When setup done: Demul Macro Sender "Save" → a folder with **State 0 + the setup macro(s)**. That is the
   raw state the two players need to *begin* the elaborate setup.
4. Back in-game: make **State 1** — the combo launch point, where the setup left off — and begin a **new** macro:
   the real combo.
5. Capture: Demul had no AVI, so OBS / ShadowPlay screen-grabbed it.

**Mapping onto our tool:**
- His State 0 (match start) + setup macro = our **`_setup`** in-folder backup (§7).
- His State 1 (combo launch) + combo macro = our **main macro clip**, whose **State 0 = the combo launch** (our
  frame 0).
- **We have AVI built in — F12 (`EMU_BTN_AVI_TOGGLE`).** The one thing Demul lacked. No OBS needed.

**The capture flow (our advantage):** `F12 (start AVI) → load the launch state → Play Macro → the game plays the
combo out → F12 (stop)`. No replay.

**PLAYBACK-MODEL DECISION (from the capture flow):** the game must **keep running past the macro's last input**
so the combo finishes on screen (the KO, the damage, the aftermath). So **Play Macro = a LIVE injection into a
free-running game** (the §2 "re-pointable inject", reusing `tas_auto::playLive`): the macro's inputs are sent
frame-by-frame from the loaded state, and when they run out the pad goes **neutral** and the game keeps
running. It is NOT read-only R / `play_match` playback — that STOPS at the end via the ReplayEnd dead-end, which
would cut a combo video short. (The ReplayEnd gate is still worth doing for the case where the user *does* press
R, but it is not the capture path.)

**ENTRY STATES (codified 2026-08-31, David's "pass-off state" model; UI shipped same day):** in a Macro
session, savestates are tied to the piano-roll frame they were saved on (the .frame sidecar - inherited
Movie machinery, nothing macro-gates it):
- **State 0 (BASE) = the macro's ANCHOR** - canonized to the macro (the clip.json State-0 pairing).
- **Every other state (SS1..) = an ENTRY POINT into the macro** - loading it seeks frame_number to the
  state's frame and the roll stays ONE continuous timeline from the origin. Example: macro content starts
  @700, authored to 800, SS1 saved @800 -> loading SS1 resumes at 800 with the roll continuous from 700.
  In READ-WRITE (macro sessions' resident mode), resuming hands-off PLAYS the macro's cells from there.
- This plugs straight into the Play Macro capture flow above: an entry state is what you load before the
  live injection - the future Play Macro can source its start from any entry state, not only State 0.
- UI (STRENGTHENED 2026-08-31, ca059d0): the Timeline savestate section reads **ENTRY STATES** in Macro
  mode; an always-visible **ENTRY MAP** lists every occupied state with its jump-in frame
  ("Entry points:  BASE @633   1 @800   2 @1500", BASE flagged as the anchor); each slot NUMBER
  hover-tells its role + frame ("Entry point @ frame N ..." / "BASE ... State 0 = start"); the
  selected-slot line names role + frame and is robust to a frame-0 BASE (keys on used[cur]). The
  Timeline frame tag is the true 3-way mode (a READ-WRITE session used to show the old binary "[WRITE]").
- Related UX (same day): a fresh Record boot resets the Notepad to **scratch** (clean buffers only -
  unsaved work is never discarded, a toast says which happened). The session authors the ROLL; the
  Notepad is its scratch pad, not the file it last held (the "am I editing the fastVS snippet?" scare -
  the seed injects into the roll regardless of what the Notepad shows).

**PLAY MACRO WIRED - PRE-BOOT REDESIGN (2026-09-01, 2f9f7f8 + 94c9ae9; David corrected the first cut):**
Launch "5 Play Macro" opens a PRE-BOOT browser (GuiState::MacroBrowser -> gui_display_macro_browser, the Macro
twin of the Replays window: game UNLAUNCHED, black screen) listing this game's clip macros with an S0 chip
(pre-boot-safe basename via tasGameBaseName/LastRomPath), a folder icon, and per-row Load Full / Stage. Picking:
- **FULL** (clip has State 0): boots a MACRO SESSION (READ-WRITE, no new clip). A macro is RELATIVE - its own
  0-based line numbering with NO inherent tie to the game frame it plays at (David's fix, f7d2bf5). So
  Dojo::LoadMacroFull does NOT place it at absolute 0; it stashes the rows in dojo.macro_pending + arms
  macro_fullload. The OSD boot-handoff loads State 0 WHILE PAUSED (deferred - the machine can't load mid-unload),
  which seeks frame_number to State 0's .frame (say 2303), THEN calls Dojo::InjectPendingMacroAt(frame_number) to
  lay macro line i at session frame (2303 + i). The playhead is already on the macro's first cell -> EXACT sync,
  aligned to the anchor wherever State 0 was saved. Resume plays the combo (infinite roll past the end for
  capture). The in-session macroLoadFull (studio window) does the same: gui_loadState FIRST (sets base), then
  writes rows at base+f. The roll auto-follows the playhead so it scrolls to State 0's frame.
- **STAGE** (no State 0): boots with the fastVS OnEnter seed through the menus.
The teardown macro.txt SAVE is DENSE (one line per absolute frame 0..max, gaps=neutral) so a full-session archive
stays index-stable; the RELATIVE load rebases onto State 0's frame regardless. Notepad scratches on
a Play Macro boot (persist-reload gate honors PlayMacro) and File>Unload keeps the window open. Folder icon +
right-click "Open folder" on both the Macros and Replays browsers. AutoLoadState/TrainingNetState neutralized in
the pick so Full/Stage fully control the boot-load; a failed Full falls back to the seed. Built + verified by a
2-agent adversarial pass (ordering/pause/no-leak OK; 4 edge RISKs fixed).

**STILL-OPEN MINORS:**
- **Play Macro with no base state** — force State 0, or allow launching from the current live position?
  (Leaning: allow it, default State 0.)
- **`_setup` naming/UX** — currently auto `<clip>_setup_NN`; keep, or allow user-named?
- **MacroMode staleness** — it's a virtual cfg flag; the startup menu sets it on every path, but other boot
  entry points (main-menu Record etc.) don't touch it, so a stale `yes` could leak in. Same class as the
  documented Replay-flag hazard; tidy when the flag gets a real reader (slice 1).
