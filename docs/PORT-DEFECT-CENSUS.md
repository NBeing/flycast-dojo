# Port-defect census — "set but never consumed" / "defined but never called"

`[AUDIT 2026-09-17]` Read-only, dojo layer, `dojo7` vs David's `0915/flycast-rr`.
Triggered by two finds the same day: `Dojo::SeedOnEnter()` was ported but never
called (fixed, `443017d4c`), and `replay_bootload` / `macro_fullload` are SET but
never CONSUMED. This is the census of that class. **Nothing here is fixed by this
document**; every "Port" is a decision for the owner, listed by size so the
decision can be made from the list, not one item at a time.

Method: every `class Dojo` member/method from `core/dojo/dojo.h` (162 symbols)
counted across `core/**/*.{cpp,h}` in both trees, each reference classified read
vs write (assignment/`++`/`.clear()`/`.store()`/`.push_back()` = write; comment
lines excluded); every Dojo method and every `core/dojo/*.h` free function that
David's `gui.cpp`/`mainui.cpp`/`emulator.cpp` calls checked for real callers; every
`cfgLoad*("dojo", K)` in David's `gui.cpp`/`mainui.cpp`/`dojo.cpp` (54 keys)
checked against any `cfgLoad*` in ours.

Already tracked elsewhere and marked "tracked" below: `docs/STATES-LIFT.md:173`
(G10 lock stub), `CLAUDE.md:142-149` (Timeline HUD), `docs/DIVERGENCE-FROM-DAVID.md`
`:240` (autosave ticks), `:283` (hold-scrub + slot-repeat), `:289` (startup prompt),
`:314` (hotkey overlay).

Paths: ours = `core/...`; David = `/home/nbee/dev/davids_fly/0915/flycast-rr/core/...`.

## 1. Dojo members WRITTEN in ours but never READ (38)

| # | member(s) | set in ours | David's consumer | class |
|---|---|---|---|---|
| 1 | `replay_bootload` | `dojo/replay.cpp:161` (Replay::Init); cleared `dojo/dojo.cpp:3060` | `rend/gui.cpp:4704-4715` boot-handoff: State-0 `gui_loadState()` while paused, else log "no State 0" | (a) Play a Movie → boot freezes at power-on (the `stepping=true` half works) but never seeks State 0 **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| 2 | `macro_fullload` | `dojo/dojo.cpp:1981` (LoadMacroFull), `:2007` (LoadClipState0Boot); cleared `:3061` | `rend/gui.cpp:4679-4691`: slot 0 → `gui_loadState()` → `InjectPendingMacroAt(frame_number)` | (a) Macros panel **"Load full (from State 0)"** (`dojo/macros_panel.cpp:253`) → `LoadMacroFull` → flag never consumed **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| 3 | `macro_pending` | filled `dojo/dojo.cpp:1953-1961`; only reader is `InjectPendingMacroAt` (`:2018-2028`), which has no caller | same block, `gui.cpp:4691` | (a) transitively dead — macro rows sit in the stash until `Reset()` (`:3062`) drops them **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| 4 | `boot_ready_arm` | `dojo/dojo.cpp:1922, 1978, 2004`, `dojo/replay.cpp:162`; cleared `:3070` | `rend/gui.cpp:4717-4743`: composes the READY banner, `gui_show_slot_picker()`, logs `TAS READY` | (a) the boot-ready banner + auto-open States never fires **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| 5 | `clip_ready_pending`, `clip_ready_text` | only `Reset()` `dojo/dojo.cpp:3071-3072` (never set) | set `gui.cpp:4739-4740`; drawn `gui.cpp:6242-6246`; cleared `gui.cpp:6617, 6647, 6671` | (a) part of #4 **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| 6 | `live_from_gen`, `live_from_local` | `dojo/dojo.cpp:897-898` (BeginClipStats reads clip.json `restoredFrom`); cleared `:868-869, 3065-3066` | `gui.cpp:4732-4733` (banner), `dojo_gui.cpp:10165`, `12256` (delete-gen guard), `25434-25447` + `26004-26038` (States: "live = gen NN", "restored …") | (a) a session on restored files never says so on screen (`live_from_edited` IS consumed → clip.json, `dojo.cpp:1031-1033`) **LANDED 2026-09-17 `396b3c982` (States status line)** |
| 7 | `loaded_macro_path` | `dojo/dojo.cpp:1982`; cleared `:951, 2008, 3063`, `dojo/branch_panel.cpp:416` | 30 reads: save-back `dojo_gui.cpp:3516-3562`, "Edit loaded macro" `6479-6489`, `7610-7640`, `26408-26420`, browser highlight `10297-10378`, banner `gui.cpp:4731` | (a) Load full sets it; no Save-to-loaded-macro / Edit-loaded-macro exists |
| 8 | `loaded_macro_rr` | `dojo/dojo.cpp:952, 1983, 2009`; reset `:3064` | **same gap in David's tree** (9 refs, all writes) | (c) |
| 9 | `load_seq` | `dojo/dojo.cpp:773` (`++` per sidecar load) | `dojo_gui.cpp:8355-8361` Notepad "Auto re-send on reload"; `13442-13446` ruler pause-peek re-arm after a load | (a) ours' `notepad_panel.cpp` has no auto re-send. Ruler-peek equivalent in ours: uncertain |
| 10 | `macro_save_result`, `macro_save_time`, `macro_save_frames` | `dojo/dojo.cpp:2987-2989, 2994`; reset `:3074-3075` | `dojo_gui.cpp:25977-25995` title-bar "saved HH:MM / unsaved / SAVE FAILED" stamp; `26046` frame count | (a) the atomics exist so "a failed write can never hide again" (dojo.h:317); in ours a failed macro write is log-only **LANDED 2026-09-17 `346fc6bdc` (Timeline macro-save stamp, 5 selftest claims)** |
| 11 | `snapshot_prompt_pending`, `snapshot_reveal`, `snapshot_prompt_name` | `dojo/dojo.cpp:1300-1307` (ArchiveGeneration) | `input/gamepad_device.cpp:370-376` (F8 pauses + opens/focuses States), `dojo_gui.cpp:11925-11940` (Generations pane: cursor into the new row's Tags cell), `25451-25456` (cue line) | (a) F8 (`input/gamepad_device.cpp:374-378`) makes the gen; no prompt/reveal |
| 12 | `snapshot_prompt_num/_files/_bytes/_frame/_movie` | `dojo/dojo.cpp:1301-1305` | **same gap in David's tree** (0 reads each) | (c) |
| 13 | `hotkey_overlay` | 0 refs in ours | `gui.cpp:996`, `gamepad_device.cpp:348-351` (F9), `dojo_gui.cpp:24764-24924` | (c) replaced: ours' F9 = `panels::toggle("hotkeys")`, cheat sheet = `dojo/hotkey_panel.cpp` |
| 14 | `shift_held_since` | only `ReleaseTasHolds` `dojo/dojo.cpp:1320` | `input/keyboard_device.h:149`, `dojo_gui.cpp:24778-24785` (hold-Shift peek) | (c) `dojo/hotkey_panel.cpp:39-40` lists Shift-to-peek as deliberately not here |
| 15 | `next_step_time`, `step_held`, `step_held_since` | 0 refs / `:1316` reset only | `rend/mainui.cpp:102-141`, `rend/gui.cpp:4612-4635` (hold-Space scrub pacing + ramp) | (c) replaced by `hotkeys::stepHold()` `HoldRepeat` (`input/hold_repeat.cpp:80-83`). **See finding X** |
| 16 | `slot_held`, `slot_held_prev`, `slot_held_since`, `slot_next_repeat` | 0 refs / `:1317-1318` reset only | `input/gamepad_device.cpp:239-256, 299-311`, `rend/mainui.cpp:164-176` (hold-F2 accelerating slot repeat) | (a) missing: ours' F2 is one step per press (`input/gamepad_device.cpp:354-373`). Tracked DIVERGENCE §2.1 #11 |
| 17 | `save_hold_since`, `save_hold_done`, `save_blocked_at`, `save_flash_at`, `save_flash_slot` | 0 refs / `:1319` reset only | `input/gamepad_device.cpp:180-209` (tap on guarded slot BLOCKED, hold arms), `rend/mainui.cpp:184-192` (hold matures → save), `gui.cpp:4914-4915`, `dojo_gui.cpp:27448-27462` (HUD) | (a) **BASE (slot 0) write-protection is absent** — ours' F1 = `gui_saveState()` on press (`input/gamepad_device.cpp:173-178`); `hold_repeat.h:29` says "same shape for the slot and BASE holds" but only `stepHold()` exists |
| 18 | `load_fail_at`, `load_fail_slot` | 0 refs | `gui.cpp:4860-4861` (F3 on an empty slot), `dojo_gui.cpp:27665-27666` ("Slot N is empty", red, 3 s) | (c)/(a) small; whether ours' `gui_loadState` toasts on an empty slot: not checked **NOT PORTED 2026-09-17: already present here - dc_loadstate warns + toasts "Save state not found" on a missing file (nullDC.cpp); load_fail_* was David's HUD carrier for the same fact** |

### 1b. The inverse — consumer present, producer missing (9)

| member(s) | read in ours | never written; David's producer | class |
|---|---|---|---|
| `frameskip_send_pending`, `_deadline`, `_p1`, `_p2` | `dojo/dojo.cpp:2062-2070` (MapleApplyAction releases at skip+N) | `dojo_gui.cpp:2986-2993` `tasArmFrameskipSend` (Sender + Notepad "Wait for Frameskip") | (a) ours' `sender_panel.cpp` has no frameskip option; `dojo:WaitForFrameskip` is referenced only in `dojo.h:109` **LANDED 2026-09-17 `363e6c6f3` (Sender: Wait for Frameskip + skip+N; tour step 61 measured the release)** |
| `send_merge` | `dojo/dojo.cpp:593, 2297` | `gui.cpp:1039` (seed from `dojo:SendMerge`), `dojo_gui.cpp:2255-2270` toggle | (b) MERGE sends permanently off; `-config dojo:SendMerge=yes` does nothing **LANDED 2026-09-17 `363e6c6f3` (boot seed + MERGE checkbox; SENDER SELFTEST +2)** |
| `locked_slots`, `base_prelock`, `locked_ranges_cache`, `locked_ranges_mtx` | `dojo/dojo.cpp:1553` (ApplyEdit gate), `:1493-1497` (FrameLockedEmu) | `gui.cpp:5196-5231` real `gui_locked_ranges` + the ONLY writer of the cache; toggles `dojo_gui.cpp:25705-25726` | (a) tracked (STATES-LIFT G10, CLAUDE.md:142-149): ours' `gui_locked_ranges` is a stub (`rend/gui.cpp:5349-5352`), so `FrameLockedEmu` is always false |

**Finding X (new, not tracked):** `Dojo::ReleaseTasHolds()` (`dojo/dojo.cpp:1314-1321`)
clears only the dead fields above and never calls `hotkeys::stepHold().release()` —
the only real release is `input/gamepad_device.cpp:205`. Its callers
(`dojo/dojo.cpp:2109, 2167`, replay end) therefore do nothing: the End-of-Replay
belt-and-braces the header describes is inert in ours. Whether the ReplayEnd window
swallows a Space keyup on ours' SDL path is untested; the guard is nevertheless a no-op.

## 2. Methods/functions DEFINED in ours with no caller (8)

| function | ours definition | David's caller | class |
|---|---|---|---|
| `Dojo::InjectPendingMacroAt` | `dojo/dojo.cpp:2018` | `rend/gui.cpp:4691` | (a) part of #2 **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| `Dojo::LoadClipState0Boot` | `dojo/dojo.cpp:1994` | `rend/gui.cpp:1069` (Play Macro STAGE boot; needs `PlayMacroClip/File/Stage`) | (a)/(b) no pre-boot Play-Macro staging in ours **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| `Dojo::ApplyRedo` | `dojo/dojo.cpp:1761` | `dojo_gui.cpp:14002, 14013` (Ctrl+Shift+Z / Redo), `15912, 15943` | (a) undo IS wired (`dojo/roll_panel.cpp:206, 270, 362, 422, 1274`, `dojo/roll_slots.cpp:717`); redo unreachable **LANDED 2026-09-17 `67ac52ba3` (Undo/Redo buttons + Ctrl+Z/Ctrl+Shift+Z; tour step 47)** |
| `Dojo::ResetPause` | declared `dojo/dojo.h:403`, **defined nowhere in either tree** | none | (c) |
| `tas_wave::writeStateSnapshot` | `dojo/tas_wave.cpp:248` | `rend/gui.cpp:4908` inside `gui_saveState` (`<state>.wave` sidecar) | (a) ours' `gui_saveState` (`rend/gui.cpp:4972-4995`) writes only the thumbnail **LANDED 2026-09-17 `ce5495cc4` - and the finding: the audio tap (David's audiostream.cpp:53 `tas_wave::onSample`) was never ported either, so NO audio.env/.wave had ever been written; wired in the same commit; thumbtest W1 0/4 -> 4/4** |
| `tas_wave::measuredFrames`, `tas_ruler::seenFrames` | `dojo/tas_wave.cpp:145`, `dojo/tas_ruler.cpp:105` | `rend/gui.cpp:4197-4220` `gui_tas_sidecar_autosave_tick` (2 s paused tick) | (a) tracked (DIVERGENCE #7): ours saves wave/ruler only in `FlushLiveClip` and `Reset` — a crash loses `audio.env`/`skip.map` **LANDED 2026-09-17 `588d7f32d` (mainui paused autosave tick; thumbtest S1)** |
| `tas_branch::forkSlots` | `dojo/tas_branch.cpp:218` | `rend/gui.cpp:5188` (`gui_slot_overwrite_guarded`), `5838`, `6141` | (a) part of #17 |

Excluded as false positives: `FlushRecordUndoGroup` (record-undo-group deliberately
absent) and `keys` (David's `tas_golden::keys`, a module ours does not have).

## 3. `cfgLoad*("dojo", K)` keys David reads that ours never reads (32)

| key | David reader | ours | verdict |
|---|---|---|---|
| `PlayMacroClip`, `PlayMacroFile`, `PlayMacroStage` | `rend/gui.cpp:1062-1064` | none | (b)/(a) pre-boot Play-Macro staging; prerequisite for `LoadClipState0Boot` and a boot-time `LoadMacroFull` **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| `TestLabBoot` | `rend/gui.cpp:1080` | none | (b) Test Lab scratch boot **NOT PORTED: no consumer here (ours' Test Lab is in-session; David's key drives his pre-boot lab-scratch launcher)** |
| `PlayTestLocked` | `rend/gui.cpp:4693` | none | (b) Play-Test lands READ/locked **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| `OnEnterHandoff` | `dojo/dojo.cpp:1834` (early handoff N frames into the seed) | none | (b) harness flag **LANDED 2026-09-17 (boot handoff port; TEST-PLAN §5.5)** |
| `SendMerge` | `rend/gui.cpp:1039` | none | (b) see 1b |
| `PurgeStale` | `rend/gui.cpp:5147` | none | (b) auto-purge stale states after a rewind **LANDED 2026-09-17 `f81414268`** - mainui purgeStaleTick on rewind_log growth; measured by scripts/purgestaletest.sh (slot 7 orphaned by an edit at F-20, purged; `--sabotage off` reddens) |
| `BaseHoldMs` | `rend/mainui.cpp:186` | none | (a) see #17 |
| `HoldStepFPS`, `HoldStepRampMs` | `rend/mainui.cpp:123, 133`, `rend/gui.cpp:4615, 4622` | renamed `HoldStepRate` (`input/hold_repeat.cpp:83`); no ramp equivalent | (c) rename; ramp absent **RampMs LANDED 2026-09-17 `2245c0c59`** (closed-form ramp in HoldRepeat, HOLDREPEAT SELFTEST 11/11); `HoldStepFPS` stays the rename (`HoldStepRate`), not read here by design |
| `ShowHotkeyOverlay` | `rend/gui.cpp:996` | replaced by the panels registry's per-panel key | (c) **not read here, by design** (the panels registry key `Panel.hotkeys` persists the cheat sheet) |
| `StartupPrompt` | `rend/gui.cpp:4241` | none (only a comment, `dojo/replay.cpp:450`) | (a) no startup prompt in ours; tracked DIVERGENCE #12 |
| `TasUi`, `HideStudioWhileRecording`, `CapturePausedFrames` | `rend/gui.cpp:4421, 6188 / 557 / 567` | none | (b)/(c) F5 blanket studio hide + capture options; ours has no `EMU_BTN_TAS_UI` **ALL THREE LANDED 2026-09-17** - `ba432e1bc` panels::studioVisible() (the blanket veil, EMU_BTN_TAS_UI/btn_tas_ui; tour 77/77 with the hide/show steps) + hide-while-recording; `f6dffd4f8` videorec::wantsFrame() dedup, measured `60 paused duplicates skipped` on the tour capture |
| `MenuGamepadNav`, `InputTrace` | `rend/gui.cpp:170, 2534 / 633` | none | (b) debugging/ghost-input flags **BOTH LANDED 2026-09-17** - `4ffb19412` (`UI: MenuGamepadNav=yes|no` at init, both measured); `23c5e685c` the tracer whole, scripts/inputtracetest.sh (named press, 3 lines/400 frames, `--sabotage silent` reddens) |
| `Skin`, `GlobalFont` | `rend/gui.cpp:331 / 515` | none | (c) cosmetic **not read here, by design** (cosmetic; this tree has no skin/font layer to consume them) |
| `StatesOpen`, `StatesThumbW`, `StatesBoardCols`, `StatesPreview`, `SlotBrowserSort`, `SlotBrowserHideEmpty` | `rend/gui.cpp:6173 / 5822,6286 / 5776 / 5956 / 5754 / 5755` | `dojo/states_panel.cpp` reads only probe/trace keys | (c) for `StatesOpen`; whether ours' States panel offers sort/hide-empty/thumb-width: uncertain **not read here, by design** (`StatesOpen` -> `Panel.states`; the wall options are the panel's own) |
| `AutoLoadStateSlot`, `AutoPauseFrame`, `AutoPausePng`, `DumpEveryStep`, `DumpLoadFrame`, `DumpOnGameTick` | `rend/mainui.cpp:205 / 253 / 261 / 473 / 368 / 482` | none (ours has its own: `LoadProbeSlot`, `AutoSeekState`, `StepProbe`…) | (b) David's Windows-harness flags **not read here, by design** (David's Windows-harness flags; ours are `LoadProbeSlot`, `AutoSeekState`, `StepProbe`, ...) |

## Recommendations (port vs delete) — DECISIONS OPEN

**Port — one coherent block, the boot handoff** (~75 lines from `rend/gui.cpp:4679-4743`
+ 6 at `6242-6247` + 3 clears at `6617/6647/6671`): consumes `macro_fullload`,
`replay_bootload`, `boot_ready_arm`, `clip_ready_*`, `live_from_*` (banner half) and
calls `InjectPendingMacroAt`. Three UI paths already arm it (Play a Movie, Macros
"Load full", and `LoadClipState0Boot` once staging exists) and land on nothing. Ours'
handoff site is `rend/gui.cpp:4757-4771` (the `onenter_ff` stop). Ours calls
`LoadMacroFull` from an in-session panel, so a re-boot is required for the arm to be
consumed; David does it pre-boot (`gui.cpp:1062-1072`, ~12 lines) — port that too or
the button's promise needs the staging keys.

**Port — BASE/fork write-protection** (~55 lines: `gamepad_device.cpp:180-209`,
`mainui.cpp:184-192`, `gui.cpp:5182-5190` + `forkSlots`), optionally the HUD (~40
lines `dojo_gui.cpp:27448-27462`) and `BaseHoldMs`: losing slot 0 costs a session;
ours has no guard at all. `HoldRepeat` (`input/hold_repeat.h`) is the natural host.

**Fix (1 line) then delete:** add `hotkeys::stepHold().release()` to
`ReleaseTasHolds()`, then delete `step_held`, `step_held_since`, `next_step_time`,
`hotkey_overlay`, `shift_held_since`. Delete `ResetPause` (`dojo.h:403`),
`loaded_macro_rr`, `snapshot_prompt_num/files/bytes/frame/movie` (write-only in
David's tree too).

**Port, small:** `ApplyRedo` (~8 lines beside the existing undo);
`tas_wave::writeStateSnapshot` + wave/ruler `saveClip` in `gui_saveState` (3 lines);
`gui_tas_sidecar_autosave_tick` (~28 lines) — crash insurance; F8 reveal (~35 lines)
if the Generations Tags cell exists here (not verified); macro-save stamp (~25
lines); `send_merge` seed + toggle (~17 lines); `tasArmFrameskipSend` + a Sender
checkbox (~15 lines).

**Port with the Timeline HUD** (already planned, CLAUDE.md:142-149): slot-repeat
(~35 lines), lock UI (~58 lines), `load_fail_*` (~5), `live_from_*` States lines
(~15), `loaded_macro_path` save-back (~50 + a button).

**Keys, resolved 2026-09-17 (the user's call: FIX, not document-as-inert):** every
key with a consumer in David's tree that could have one here is now read - TasUi,
HideStudioWhileRecording, CapturePausedFrames, MenuGamepadNav, InputTrace, PurgeStale,
HoldStepRampMs (this batch), SendMerge, BaseHoldMs, PlayMacro*, PlayTestLocked,
OnEnterHandoff (earlier batches). Not read here, by design, and listed as such in
docs/HOTKEYS.md "Launch keys": the six Windows-harness flags, Skin, GlobalFont,
ShowHotkeyOverlay, StatesOpen and the wall options, HoldStepFPS (renamed HoldStepRate),
TestLabBoot (no consumer: the Test Lab is in-session here).

## Totals

- Category 1: **38** write-only members (+ **9** consumer-without-producer), of which
  6 are "same gap in David's tree" and 9 are replaced-by-design leftovers.
- Category 2: **8** uncalled functions.
- Category 3: **32** cfg keys David reads that ours never reads.
- New, not previously tracked: Finding X (`ReleaseTasHolds` inert), BASE
  write-protection absent, `ApplyRedo` unreachable, `<state>.wave` never written,
  `send_merge` / frameskip-send dead, macro-save stamp / F8 reveal / live-from display
  missing, the boot-handoff block.
