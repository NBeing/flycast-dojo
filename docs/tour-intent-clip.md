# Surface Tour v4 — the CLIP intent module (`core/dojo/intent_clip.cpp`)

`[2026-09-18]` Modules 3 branches, 4 generations, 6 test lab, 9 captures. The user,
having watched the 77-step tour: *"nothing opened starting on your roll tests... we
don't have any real meat to these tests. each feature should be tested with its
INTENT."* Every feature here is four beats — **open** its window by the hotkey the
tour rebound (the human sees it), **act** through the feature's own verb, **intent**:
run the game and read what the fighter did (`Combo_Meter_HitsToOpponent`'s peak and
the machine hash, through `intent.h`'s shared ceremony), **close**.

The fixture: the tour clip's slot 0 (in-match, Sonson vs Marrow) as BASE and David's
`Combo_Dhalsim97` window as THE COMBO (`dojo:IntentMacro`, staged by
`surfacetourtest.sh`; `RECIPE.toml`: peak 19 on this base). The module runs first
(name order `clip` < `roll` < `send`), right after `roll: redo the flip + undo`, and
ends on BASE with the roll restored (`intent::end`).

## What each feature must make the game do

| feature | David's intent (his words) | the meat | expected |
|---|---|---|---|
| branches | "fan out the wakeup options into separate movies, the base intact"; acceptance "root .flyr byte-identical after a branch+checkout round-trip; branch .flyr has the new tail" | place the combo on main → run → **19**; branch from slot 0 → checkout → clear the combo ON THE BRANCH → run → **0**; back to main → run → **19 still**; root `.flyr` hash unchanged since the checkout, branch `.flyr` differs | 19 / 0 / 19 |
| generations | "immutable"; F8 "copy the clip's replay + every state + sidecars + clip.json into the next `<clip>_gen_NN`" | Shift+F8 (btn_gen_archive's default) → a `_gen_NN` with a `.flyr`; damage main (clear + run → **0**); `tas_clip::restore` + re-attach the movie + BASE → run → **19** and the machine hash equals the pre-damage hash | 19 → 0 → 19, hash == |
| test lab | "a test = a folder whose BASE (slot 0) is the permanent fixture... tests never stomp each other" | add a test from slot 0 (through the lab's verb) → bind it (`checkoutFolder`) → its BASE loads at the same frame → place the combo on the test's roll → run → **19**; trash it | 19 |
| captures | "a capture contains every emulated frame exactly once" | start the recorder, run the combo window in real time (fast-forward off — the recorder captures presented frames), stop: `framesWritten` within +6 of the frames emulated and the paused-duplicate count reported | written == emulated (+stop presents) |

## Honest limits

- **Test lab**: this tree's lab is BASE-only by design (`lab_panel.cpp`: "A test
  created here is BASE-only; record its sequence afterwards") — there is no roll→macro
  writer and no lab runner, so `results.jsonl` / `peakP1` is **unmeasured**; the step
  `lab intent: results.jsonl (no lab runner in this tree)` SKIPs and says so.
- **Generations**: the restore ceremony ("back up live before proceeding?") is a
  pre-boot UI in David's tree; in-session here the restore is the pure folder verb
  `tas_clip::restore` plus `Replay::AttachFile` of the restored movie. No second gen
  tagged `pre-restore` is asserted.
- **Captures**: the recorder is asked to stop on the *next presented frame*, and the
  stop step's own `gui_step_frames(5)` emulates frames too — the count is bounded
  `emulated ≤ written ≤ emulated + 6`, not equality; the line prints all three numbers.

## Arms (`ArmSpec`, judged like every runner arm)

| arm | restores | must redden | must stay green |
|---|---|---|---|
| `branch-leak` | the checkout never happens, so the "branch" clear lands on main | `branch intent: run main again - the combo still lands (peak 19)` | `branch intent: create a branch from slot 0` |
| `gen-noop` | the restore copies nothing (says it restored) | `gen intent: run main - the combo is back (peak 19, same hash)` | `gen intent: Shift+F8 archives a generation` |
| `capture-dup` | `dojo:CapturePausedFrames=yes` for the recording — the pcsx2-rr duplicate bug | `capture intent: stop - frames written == frames emulated, 0 paused duplicates` | `capture intent: record the combo (every emulated frame once)` |

Arms are read only in the step lambdas (`surfacetour::sabotaged`), never as a switch
in a panel's shipping code.

## Measured

See the commit message and TEST-PLAN §6 for the step lines of the run this landed on.
