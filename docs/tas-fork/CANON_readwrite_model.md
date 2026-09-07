# CANON - Read/Write Model (the input-mode axis)

**Status: DESIGN, not built.** Replaces the driver model's DEVICE framing (CONTROLLER vs PIANO ROLL) with an
INTENT axis. David + Claude, 2026-08-31. Ground truth for the rework; code follows it.

---

## 1. Why

The driver model conflated two questions in one switch: *which device* (pad vs cells) and *what happens to
existing inputs* (overwrite vs protect). Consequences David hit:
- CONTROLLER always HARD-clobbers; PIANO ROLL always protects - you could not get "pad, but protect."
- The pad/keyboard are locked OUT of PIANO ROLL (the keyboard IS a controller), so you can't hand-author there.
- "Overwrite from the point I edit onward" wasn't a choice you had.

This splits **intent** onto its own axis and treats **every input source identically**.

---

## 2. The one principle

**All input sources are equal.** The Controller (pad/keyboard), the Notepad SEND, and the Input Sender are just
**signals**. No source has special behavior. The **MODE** decides what a signal does. Zero device exceptions.

The Piano Roll (`session_inputs`) is the single canonical list every signal writes into.

---

## 3. The three modes (the whole rule set)

Per frame, given the frame's **signal** (the combined live input from any source, if any):

| Mode | signal present this frame | no signal this frame |
|---|---|---|
| **READ** | ignored | ignored - playback only |
| **READ-WRITE** | **STOMP** the active frame (overwrite that cell), exactly like "Replace @ Active" | **PRESERVE** the frame |
| **WRITE** | write the signal to the frame | **NEUTRALIZE** the frame (clobber to empty) |

That's the entire model. One table, no per-device rules.

- **READ** = play back the roll, write nothing. (= today's WATCH)
- **READ-WRITE** = safe authoring: signals stomp only where present; untouched frames survive, advancing does
  not clobber. The pad/keyboard finally work here. (~ today's PIANO ROLL, pad promoted to a signal)
- **WRITE** = hard record: advancing overwrites every frame it passes - your signal, or neutral if you send
  nothing from any source. (= today's CONTROLLER)

---

## 4. The single distinction (this is what makes it clean)

The three modes differ in exactly one thing: **what "no signal this frame" does** as the game advances -
READ ignores, READ-WRITE preserves, WRITE neutralizes. "A signal this frame" is written in both writable modes.
No exceptions beyond that.

**Released pad = "no signal."** So in READ-WRITE a released pad PRESERVES (it does not stamp a neutral); the pad
**overdubs** - it writes only the frames you actually press. To deliberately blank a frame in READ-WRITE, use an
explicit tool (clear the cell / send a neutral). Only WRITE turns "hands off" into neutrals.

---

## 5. Mapping to today (this is a re-frame, not a rebuild)

- **READ** = `play_match`.
- **READ-WRITE** = `!play_match` + protect (the roll persists; signals stomp only where present).
- **WRITE** = `!play_match` + clobber (advancing overwrites every frame; = the current CONTROLLER record path).

The current banner (CONTROLLER | PIANO ROLL switch, plus WATCH) becomes a **3-segment READ | READ-WRITE | WRITE**
selector. The recently-added LIVE/PROTECTED sub-mode **collapses into this one axis** - a net simplification.

---

## 6. Implementation sketch

- `Dojo::MapleRecordAction` goes from the current 2-way (`if (macro_armed) return;` else record) to **3-way keyed
  on the mode**:
  - **READ**: return (no record).
  - **READ-WRITE**: write `session_inputs[frame]` = signal **only if a signal is present** (pad non-neutral OR a
    live source send targeting this frame); else leave the cell untouched.
  - **WRITE**: write `session_inputs[frame]` = signal (neutral if absent) **every frame**.
- The pad, `-> Live` (notepad / input sender), and hold/auto all feed the same per-frame **signal**; the mode
  decides. Explicit **SEND** buttons (Replace / Insert / Append at a target) stay explicit edits usable in any
  writable mode - they are deliberate range stomps, orthogonal to the live per-frame rule.
- The driver banner -> a 3-segment **READ | READ-WRITE | WRITE** selector; the LIVE/PROTECTED pill is retired.
- Interacts cleanly with the infinite-roll fix: past the end, READ-WRITE preserves/extends on signal, WRITE
  extends with neutral+signal, READ has no end to hit while writing (it's playback).

---

## 7. Resolved (David) + edge-case handling

RESOLVED: colors = READ green / READ-WRITE orange / WRITE red. Signal = any non-neutral pad button (released =
absent = preserve). READ-WRITE overdubs while ADVANCING (stomps the active frame whenever a signal is present,
advancing OR paused - uniform rule). CONTROLLER is fully replaced by WRITE + pad-as-signal (demoted to a source).

EDGE CASES (fold into the mechanic slice):
1. **Emu hotkeys are NOT game signals.** The READ-WRITE stomp reads the *mapped game-input state*, not the raw
   device, so the Step/Pause keys (e.g. R1 = Step) never write themselves as a game button.
2. **Timing = per advanced frame.** The stomp lands on the frame you advance INTO (step or free-run tick) using
   what you hold then. Pause + hold does NOT write (nothing advances). Step for precision, hold-run to overdub.
3. **Explicit sends need a writable mode.** Notepad/sender Replace/Insert/Append blocked in READ, allowed in
   READ-WRITE + WRITE.
4. **Simultaneous signals COMBINE** (pad Down + sender LP -> Down+LP; buttons OR, dirs SOCD/last-wins). No
   cross-source syncing needed.
5. **Preserve-vs-neutralize only differs on EXISTING frames.** New/past-the-end frames become neutral on
   no-signal in both RW and WRITE (nothing to preserve). Consistent with the infinite-roll fix.

---

## 8. Build progress

- **SLICE 1 - banner SHIPPED (2026-08-31, b42798b):** the driver banner (mirrored in Notepad / Input Sender /
  Piano Roll / Timeline) is now a 3-segment **READ | READ-WRITE | WRITE** selector, green/orange/red.
  `tasBigSegSwitch` generalized to N segments; `tasDriverCol` recolored; READ click enters read-only via the new
  frontier-safe `gui_enter_readonly()` (gui.cpp); READ-WRITE/WRITE set the mode directly. **LOOK + selection
  only** - under the hood READ-WRITE still == PIANO ROLL (pad out) and WRITE == CONTROLLER (records) until the
  mechanic slice. Replay path unchanged (test 1/0).
- **SLICE 2 - mechanic SHIPPED (2026-08-31, 24ba960):** `MapleRecordAction`'s macro_armed branch went from
  PROTECTED(return)/LIVE(decay) to the READ-WRITE rule - the pad RECORDS the active frame when it presses a game
  input (signal via `canonFromPacket` on the built `maple_in`, so hotkeys/analog-center don't count - edge #1),
  PRESERVES on release (overdub). WRITE (!macro_armed) records every frame incl. neutral. LOCKED ranges never
  overwritten (WRITE warns). LIVE-decay/baseline path retired. Replay path untouched (record not called under
  play_match; test 1/0). Now the pad/keyboard author in READ-WRITE.
- **SLICE 3 - Auto-Fire / -> Live signals SHIPPED (2026-08-31, 6e08dd3):** the tas_auto live injection in
  MapleApplyAction was split on PrProtected() - WRITE baked the full overlay (auto-fire hold + -> Live seq),
  READ-WRITE baked ONLY the hold (seq guest-only). Unified: in BOTH writable modes the whole overlay bakes into
  the movie cell before the .flyr append, OR-ing on top so it COMBINES with a hand-held pad frame (edge #4). A
  LOCKED range still only drives the guest, never baked. Auto-Fire is NOT a duplicate of -> Live - same tas_auto
  engine (overlayCanon = hold/turbo, liveCanon = -> Live seq), different UI. WRITE + replay byte-identical (test 1/0).
- **SLICE 4 - R-cycle + banner overflow SHIPPED (2026-08-31):** `8ea8685` tasBigSegSwitch shrinks name+hint font
  to fit each segment (fixed the Timeline text overlap); `718ebde` R now cycles READ -> READ-WRITE -> WRITE ->
  READ (was binary), WRITE->READ keeps the frontier BASE-seek/refuse guard.
- **SLICE 5 - Movie-mode regression audit + R1/R2 fixes (2026-08-31):** 4-agent read-only audit (wf_c4294a22-c42)
  confirmed **classic Record Movie (WRITE) + Play Movie (READ) are byte-identical 1:1** after the whole batch -
  the new READ-WRITE signal gate is entirely inside `if (macro_armed)` so WRITE skips it; the infinite-roll fix is
  `!play_match`-gated; the tas_auto overlay is `!play_match`-gated; savestate STALE-detector / F8 gens / locks
  untouched. Two functional risks found + FIXED (c93def8): **R1** macro_armed never reset on a session boundary ->
  a fresh Record Movie booted without restarting the exe could silently start in READ-WRITE and drop idle frames
  (fixed: `bootRecord(false)` clears macro_armed - it is the sole RecordMatches=yes boot path); **R2** tasWriteGrow
  omitted `replay.HasAppendTarget()`, so a replay flipped to READ-WRITE and run past the movie end re-froze on the
  infinite-roll sibling path (fixed: `|| replay.HasAppendTarget()` at dojo.cpp:1604). test 1/0.
- **RESTART-REPLAY FIX (2026-08-31, d09bbd3 + 8f5abad):** user-reported "Play Movie -> reach end -> Restart replay
  lands in WRITE." The End-of-Replay "Restart replay" button trusted the Replay/ReplayFilename cfg to persist, but
  ReplayFilename comes back EMPTY at End-of-Replay (log: `restart replay REFUSED - movie file missing ('')`) and
  Replay can be flipped "no" by a failed LoadReplayFile - so the reboot skipped Replay::Init and stayed
  play_match=false = WRITE. Fix: source the .flyr from the LIVE `dojo.replay.filename` (still set at End-of-Replay,
  read into a local BEFORE gui_stop_game's teardown DetachFile), then FORCE the replay session cfg (Replay=yes,
  ReplayFilename, RecordMatches=no, ...) + macro_armed=false and reboot; refuse loudly if the file is truly gone.
  David confirmed fixed.
- **STALE-STRING SWEEP SHIPPED (2026-08-31, b682565):** 10-agent harvest->adversarial-verify workflow (wf_93fa99f1)
  -> ~35 user-facing strings retired WATCH/CONTROLLER/PIANO ROLL for READ/READ-WRITE/WRITE, applied by distinctive
  substring (tab-independent, no printf/##id moved): Input Sender status row -> WRITE(red)/READ-WRITE(orange); the
  now-FALSE Send text corrected (a Send BAKES post-unify - durable in READ-WRITE, reclobbered in WRITE); 4 "switch
  to PIANO ROLL" tooltips + Ctrl+X/V notifs; 8 "roll now PIANO ROLL" toasts; 19 "WATCH - press R to author"
  tooltips; the tasCoordLog trace token. Legit UI names kept (physical CONTROLLERS section, Piano Roll window/panel
  + Settings header, the roll toggle button). test 1/0.
- **DEAD-CODE CLEANUP DONE (2026-08-31, d966abd + 9013f9d + 8b336b7):** backlog item (b) complete, 3 commits, each
  build+test 1/0. (1) **Deleted** pr_protected/PrProtected() (zero live callers) + tas_live_baseline trio (write-
  only since SLICE 2; was deep-copying the whole movie map on EVERY state load - reloads are now faster) + dead
  color tokens TAS_PIANOROLL/TAS_PR_PROTECT/TAS_PR_LIVE/tasMute() + the stale MapleRecordAction/MapleApplyAction
  doc blocks. (2) **Renamed** the flip-trap: TasDriver enum DRV_WATCH/CONTROLLER/PIANOROLL -> DRV_READ/DRV_WRITE/
  DRV_READWRITE (+ reordered to READ|READ-WRITE|WRITE) and TAS_CONTROLLER -> TAS_READWRITE; behavior-preserving,
  mode->label->color mapping verified unchanged across all 8 sites. (3) **Freshened** the surviving stale comments
  (macro_armed doc, mode legend, tasBigSegSwitch/tasDriverBanner docs, header note, TAS_PROTECT). Codebase-wide
  grep confirms ZERO references to any retired symbol remain.
- **EDGE #3 ENFORCED - edits in WRITE + no auto-flip (2026-08-31, 7c88c32):** David hit the gap hands-on
  (couldn't brush away a pad input while paused in WRITE). ~20 edit gates relaxed from `paused && !play_match &&
  macro_armed` to `paused && !play_match` - brush/paint, Ctrl+X/V, mash, erasers, transforms, paste/loop/blanks,
  context menus, snippet/macro quick-place, Notepad SEND, Input Sender -> Movie verbs + quick-Replace/Enter.
  **Auto-Send stays READ-WRITE-only BY MECHANICS** (it writes a cell then STEPS; in WRITE the next frame
  re-records the pad over the cell before it applies - the send eats itself; tooltip says so). **Auto-arm REMOVED
  from all 11 edit paths (respect-the-toggle, David's explicit call):** the mode only changes via banner/R; an
  edit in WRITE stays WRITE (re-advancing reclobbers per the accepted contract; use READ-WRITE to protect).
  The 8 "roll now READ-WRITE" toast suffixes and the "switch to READ-WRITE" disabled-whys went with it.
- **STOMP SHIPPED (2026-08-31, 1ba6feb):** decision (c) settled - READ-WRITE re-authoring REPLACES, not accretes.
  In READ-WRITE the tas_auto overlay (Auto-Fire / -> Live) used to OR its buttons onto the PRESERVED prior cell,
  so a button could never be cleared. Now MapleRecordAction sets a per-frame `tas_rw_pad_wrote` flag; the overlay
  bake clears the target player's canon first when the pad did NOT record this frame (cell = prior -> STOMP), and
  ORs when the pad DID record (cell = current pad -> COMBINE, edge #4 preserved). Per-player; WRITE never clears
  (always records). MOVIE-AUDIT BACKLOG NOW FULLY SHIPPED.
- **KNOWN (WRITE-send, by design):** in WRITE, live sends fly by / get clobbered because WRITE clobbers every
  advancing frame - use READ-WRITE (sends stomp their frames, rest preserved) or Auto-Send (frame-steps). David
  diagnosed + accepted this; not a bug.
- **UX chore (2026-08-31, 16effdb):** Input Sender got a quick "Replace @N" button under each pad + Enter/Keypad-
  Enter hotkey (window focused, not typing) - both run the -> Movie Replace @ target write, shared isQuickReplace()
  lambda. Distinct from the bottom "Send" (-> Live, transient).
