# CANON - OnEnter boot seed ("fastVS")

**Status: SHIPPED + PROVEN (2026-08-31).** Backend 957e18a, audit fixes af28243; David authored the REAL
fastVS hands-on (633 frames: Start x3 @ 119/210/345, Right x2 + Start x3 @ ~457-464, neutral tail) and
verified it end-to-end - the handoff pause lands on STAGE SELECT, "the first real choice." He edited the
seed in the in-app Notepad; the library save refreshed frames/hash correctly (the 5c loop works as
designed). David + Claude, 2026-08-31. Ground truth for the feature.

---

## 1. The problem

MvC2 (Dreamcast) cannot reach VS mode at power-on - the user must sit through / navigate boot
screens (logo -> title -> menu) every single Record session. David: auto-inject a snippet whose
only job is to frame-perfect press through every screen until VS mode, then hand control over.

## 2. The design (David's constraints)

- **Inputs only, never a savestate** - "for sync related reasons. If we want to make sure that our
  replay starts at frame 0, always, I just want to send the necessary inputs in the movie."
- **Leverage the Snippets library, no new module** - a library snippet tagged **`OnEnter`** is the
  seed. Mode-specific tags **`OnEnter-Movie`** / **`OnEnter-Macro`** beat the generic tag; ties
  break by name. Tags are edited in the Snippets window like any other tag.
- **Record Movie**: the seed IS part of the movie - the .flyr carries the boot nav from frame 0,
  so a replay is input-aligned from power-on. This kills the desync worry.
- **Record Macro**: the seed lands in macro.txt at teardown like any other frames - accepted
  SCRATCH ("will get wiped immediately"); the user replaces the State0+macro pairing later.

## 3. How it works (the mechanism)

1. **Launch menu** (`bootRecord`, dojo_gui.cpp): `tasOnEnterResolve(macro)` scans the library for
   the tag and stages the path as the virtual cfg **`dojo:OnEnterFile`** - re-set EVERY boot, "" =
   boot clean. A checkbox row under the Record buttons shows the resolved seed; **untick to boot
   clean** (required to re-record the seed itself - the chicken-and-egg escape). CLI:
   `-config dojo:OnEnterFile=<path>` seeds without the menu.
2. **`Dojo::SeedOnEnter()`** (dojo.cpp; called from gui_start_game on the RecordMatches && !Replay
   branch, guiMutex held, emulator not yet running): loads the .txt via `tas_macro::Load`, clears
   `session_inputs` + zeroes `frame_number` (a stale write-session timeline would PLAY in
   READ-WRITE), writes frames 0..N-1 as zeroed packets via `tasWriteCanonIntoFrame`, sets
   `macro_armed = true`.
3. **READ-WRITE is what makes it work**: the seeded cells PLAY while the released pad preserves
   them; past frame N the infinite roll hands control to the user. (WRITE would clobber the seed
   with neutral as it advanced; READ could never hand control over.) David called this himself.
4. **Run-to-handoff**: `stepping = true; target_step_frame = N; fastForwardMode = true;
   onenter_ff = true` - the boot fast-forwards and the step-stop in gui.cpp lands PAUSED on the
   first user frame, dropping the FF there. The user takes over from a clean pause.
5. The .flyr records each seeded frame as it is applied (normal append path) - the movie is
   self-contained from power-on.

## 4. Emergent property (nice)

If the user saves State 0 / BASE at the handoff pause (frame N), `base_prelock` locks [0, N) -
the boot nav becomes write-protected automatically.

## 5. Authoring the real fastVS

1. Record Movie with the OnEnter checkbox UNTICKED (clean boot).
2. Navigate to VS mode by hand, note the handoff frame.
3. Select frames 0..handoff in the Piano Roll -> Save Sel. as Snippet -> name it `fastVS` ->
   tag it `OnEnter` in the Snippets window.
4. Every later Record boot seeds it automatically. Fixed environment required: same BIOS +
   NoBGM_VMU (menu timing depends on saved settings screens).

A PLACEHOLDER `fastVS` (720 frames, spaced P1 Start taps, tag OnEnter) was written to
`build/data/snippets/` + library.json to prove the mechanism - replace it via step 1-3.

## 5b. Audit + fixes (af28243)

The 4-agent adversarial audit found real bugs, all fixed: **FF dead-on-arrival** (Emulator::loadGame cleared
fastForwardMode right after the seed armed it - the OSD loop now arms it once the game runs, and fires the
hands-off toast there); **boot-clean stale timeline** (the frame-0 clear is hoisted into gui_start_game's
RecordMatches branch - EVERY record boot starts clean, seeded or not); **mode leak** (the handoff stop restores
WRITE for Record Movie - the seed only BORROWS READ-WRITE; Macro stays READ-WRITE); **seed protection** (pad OUT
+ TAS hotkeys locked during the seed window, both released at the handoff); **staleness hardening** (OnEnterFile
consumed one-shot; live-cfg online guard; refuse under AutoLoadState; dojo:Delay pinned to 0 virtually; SOCD-
cleaned masks; Reset clears onenter_ff + disarms tas_auto - a stale auto-fire arm STOMPED seed cells); and a
**pre-existing heap overread** (undersized delay rows now grown in place, not kept by the emplace).
ACCEPTED/DEFERRED: handoff may overshoot a few frames (neutral tail, benign); the in-game MENU key mid-seed can
strand the boot paused (reuse of `stepping` - refactor later); macroPairState vs a seeded prefix is a
Play-Macro-time question; gui_start_game's Reset-before-guard re-entrancy is latent.

## 5c. Iterating on the seed WITHOUT leaving the emulator

Edit the snippet in the in-app **Notepad** (Snippets window -> open the entry; it opens as a library file and
**Save writes the .txt back + refreshes the library live**). Then Close Game -> launch menu -> Record Movie:
the seed re-resolves from the fresh file at every boot. A game reboot per iteration is unavoidable (a frame-0
seed needs a true power-on) but the app never closes, and the boot FF makes each run fast. NOTE: editing the
.txt outside the app leaves library.json's cached frames/hash stale (the checkbox label may show an old frame
count) - hit **Rescan** in the Snippets window; the SEED itself always reads the real file.

## 6. Test plan

- test.ps1 stays 1/0 (replay path never seeds - gated RecordMatches && !Replay). VERIFIED.
- Hands-on: launch menu shows the checkbox row -> Record Movie -> expect the "OnEnter: playing N
  boot frames" toast, FF through the boot with Start taps visible, a PAUSE at frame N in
  READ-WRITE (orange banner), log line `TAS ONENTER: seeded N frames ... handoff pause @ N`.
- Then: Play Movie the resulting .flyr -> the boot nav must replay from frame 0.
- 4-agent adversarial audit (lifecycle / record-integrity / handoff / staleness) ran post-commit;
  findings tracked below.

## 7. Future (launch templates)

The same seed mechanism generalizes into named launch templates (boot-to-VS, boot-to-training,
VS + specific team) = the foundation for real game-state tests (seed a template, run, assert a
RAM value via the MvC2 RAM tables). Deferred until fastVS is proven.
