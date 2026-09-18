# Surface Tour v4 — the ROLL intent module

`core/dojo/intent_roll.cpp`, registered through `surfacetour::registerModule` (v3 of
the frozen contract). Modules 1 (roll edit), 2 (undo/redo), 5 (macros/snippets), 10
(ruler/skip map). `[2026-09-18]`

## Why this exists

The user, watching the 77-step tour: *"nothing opened starting on your roll tests… we
don't have any real meat to these tests. each feature should be tested with its
INTENT."* The old feature steps proved the roll could flip a byte. Nothing asked the
game whether the fighter cared.

## The four beats, every step

| beat | what the human sees | what the log proves |
|---|---|---|
| OPEN | the window opens by its tour hotkey (`Ctrl+F1` roll, `Alt+F6` macros, `Alt+F5` snippets) | `panels::find(id)->open` |
| ACT | the panel's OWN edit path — `rollpanel::blankRange/undo/redo` (the Blank/Undo/Redo buttons' bodies), `macros::placeFileWindowAt` (the place button's path) | `Dojo::ApplyEdit` verbs `roll: blank`, `macro: replace`; the undo/redo depth |
| INTENT | the game runs the combo under fast-forward and the fighter does — or does not — land it | `Combo_Meter_HitsToOpponent` peak while stopped, through `intent::peak` |
| CLOSE | the window closes | open flag back |

The fixture is the RECIPE's: BASE = the tour clip's slot 0, THE COMBO = David's
`Combo_Dhalsim97` window (`dojo:IntentMacro`, staged by `surfacetourtest.sh`), **peak 19**
(`scripts/fixtures/mvc2/RECIPE.toml result.combo_peak`).

## David's intent, and how each is asked

| module | David (quoted) | the ask | expected |
|---|---|---|---|
| 1 roll edit | "swap-twice and flip-twice are IDENTITIES, doing the op again restores the exact bytes" | place → run; Blank the window through the roll → run; Undo → run | 19 → 0 → 19 |
| 2 undo/redo | "an undo/redo is itself an edit — it fires a guard event like any other change" | Redo → run; Undo → run | 0 → 19 |
| 5 macros | "State 0 = Macro Frame 0 (REQUIRED binding)" | the window's row 0 placed AT BASE+1 through the Macros panel → run | 19 |
| 5 snippets | (a snippet drives menus) | placed through the panel — a movie-mover; **no in-match intent, said so** | roll changed |
| 10 ruler | "MvC2 skips game-logic frames on a fixed cadence… rows `x` (skip frame)" | `tas_ruler::snapshot` over 240 frames of the run | 55..65 skips (rate 4 → 60) |

Not measured: "the states REVALIDATE" — the module saves no scratch state of its own,
so `slotStale` has nothing to flip; left for the clip module or a later pass.

## Arms

| arm | restores | must redden | must stay green |
|---|---|---|---|
| `roll-clear` | the clear writes `session_inputs` directly — no funnel, no undo history | `roll intent: undo the clear (panel Undo)` (the row stays blank) | `roll intent: the combo lands (peak 19)` |
| `macro-window` | the file's FIRST rows placed instead of its CLIP window - the wrong combo (the code's arm; an earlier draft called it `macro-anchor`, BASE+31) | `macro intent: the macro lands the hit (peak 19)` | `roll intent: the combo lands (peak 19)` |

## Measured

(filled from the runs — see the commit message and TEST-PLAN §6)
