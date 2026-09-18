# Surface Tour v4 - the SEND intent module (`core/dojo/intent_send.cpp`)

`[2026-09-18]` Modules 7 (sender + notepad), 8 (frame skip test), 11 (state machine).
The user's rule, 2026-09-17: *"nothing opened starting on your roll tests... each
feature should be tested with its INTENT."* Every step here is four beats - OPEN the
window by the hotkey the tour rebound (the human sees it), ACT inside the feature's own
path, INTENT (the game runs and the fighter does the thing, read back through the game
oracle), CLOSE - on `intent.h`'s ceremony (the combo hunt's), with David's
`Combo_Dhalsim97` window (`dojo:IntentMacro`) on the tour clip's BASE as the fixture
(RECIPE.toml: peak 19).

## The steps

| step | beat | the claim |
|---|---|---|
| `send intent: open notepad (Ctrl+F12)` | open | the panel is open |
| `send intent: notepad round-trips the combo's first 60 rows` | act + intent (text) | 60 rows rendered in the roll's one-token-per-frame notation (`renderCell`) into the REAL editor, `notepad::analyze` says 60 frames / 0 errors, `parsePattern` of the editor's own text equals the source rows - `parse(render(rows)) == rows`, the round-trip law |
| `send intent: va2 grammar vectors` | act | `tas_va2::SelfTest()` returns "" - David's ~90 spec vectors incl. the must-fail cases, headless |
| `send intent: close notepad` | close | |
| `send intent: begin on BASE` | setup | `intent::begin`: pause, WRITE-authoring, roll snapshot, BASE reloaded |
| `send intent: open sender (Ctrl+F6)` | open | |
| `send intent: sender sends the combo window` | act | the full window through the SENDER's own path: `renderCell` -> `sender::patternToCanon` (must equal the source rows) -> `tas_auto::playLive` at BASE+1. The sender is P1-only; the count of P2-input rows in the source is printed |
| `send intent: the sent combo lands (peak 19)` | intent | `intent::runToStop`; `Combo_Meter_HitsToOpponent` peak == 19 - the SENDER, not the hunt, landed it |
| `send intent: close sender` | close | |
| `send intent: reload BASE` | mover/converge | back on `load slot 0`'s hash |
| `send intent: open frame skip test (Ctrl+F9)` | open | |
| `send intent: place the combo for the sweep` | act | `intent::placeCombo(0,0)` through the edit funnel |
| `send intent: FST sweeps the four phases` | intent | `fst::armSweepOver` over the placed window, k=0..3 at P=t0 (the FST's own generate/run), four peaks read back through `fst::resultOf` - all four must be 19 |
| `send intent: close frame skip test` | close | |
| `send intent: reload BASE for the sample` | mover/converge | |
| `send intent: Being_Hit sampled during the combo` | intent | `tas_mvc2::statesSampleArm(Being_Hit)` then the run: on every maple poll the evaluator reads P1's and P2's POINT character; P2 must enter Being_Hit, P1 never before the first hit |
| `send intent: end on BASE` | mover/converge | `intent::end`: the snapshot roll restored, BASE reloaded |

## What was ported for it

- **The state machine evaluator** (`tas_mvc2::loadStates / stateList / stateIndex /
  pointSlot / evalState / evalPointStates`, `core/dojo/mvc2.{h,cpp}`) from David's
  `tas_mvc2`, on OUR field dictionary (SPREADSHEET by name): a state is an
  ALL-conditions-AND over per-character fields; a state naming an unknown field is
  logged and SKIPPED, never silently always-false. Definitions are data:
  `core/dojo/mvc2_data/states_general.json` (24 general states, md5
  `7c1d12c5a9dcc6c13329dd8c57196e0f`, David's 2026-09-13 port of mvc2gen2's
  `State_Defines_General.js`). Located like SPREADSHEET.json (`dojo:Mvc2States`, data
  dirs, the source tree beside `mvc2.cpp`).
- **The sampler** (`statesSampleArm / statesSampleTake`): one state, both players, on
  every maple poll inside `comboPoll` - no per-frame callback (flycast is a RIDER).
  `thresholdOverride` exists for the `state-field` arm only.
- **`fst::armSweepOver`** (armFixedSweep's body with the rows chosen by the caller)
  and **`fst::resultOf`** (a variant's peaks).
- **`notepad::setAndAnalyze`**: text into the real editor, the panel's own analysis, the
  editor's text back.

## Arms (`surfacetour::sabotaged`, read only in the step lambdas)

| arm | restores | must redden | must stay green |
|---|---|---|---|
| `send-noop` | the send is skipped after the pattern parsed | `send intent: the sent combo lands (peak 19)` | `send intent: notepad round-trips the combo's first 60 rows` |
| `fst-phase` | the sweep runs at BASE+200..+210, where nothing is placed | `send intent: FST sweeps the four phases` | `send intent: the sent combo lands (peak 19)` |
| `state-field` | Being_Hit's first threshold is 31 (Knockdown_State==31) | `send intent: Being_Hit sampled during the combo` | `send intent: FST sweeps the four phases` |

## Measured

(filled from the runs below)

## Honest limits

- David's "connects 1 in 4" (Magneto `s.HP x33 -> s.LP x1`) is NOT this base (Sonson vs
  Marrow) and is not claimed. The FST step measures what the RECIPE measured: the
  Dhalsim97 window is phase-INSENSITIVE here - four phases, four 19s. A phase-sensitive
  fixture is a different combo on a different base; the sweep is ready for it.
- The sender is P1-only by design ("two-player chords are the Piano Roll's job"); the
  step prints how many source rows carried P2 input and would not be sent.
- The evaluator is v1 = the on-point character only (David's TODO(full-team) stands).
