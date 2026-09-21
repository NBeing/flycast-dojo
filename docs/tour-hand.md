# The hand tour - David's combo authored by hand in the piano roll

`scripts/handtour.sh` · module `hand` (`core/dojo/intent_hand.cpp`) · `[MEASURED 2026-09-20]`

The user, once the ironman98 chase had closed (docs/HYPER-OBJECTS.md): "the point of this was the
tour of building the combo" - "can you make a tour which programs this combo, by hand (but be
efficient, you can do it using shortcuts in the ui e.g. drag and hold frames)".

## What it does

It stands on **David's own state 3** of his 2026-09-20 clip (frame 16215, the on-screen meter at
36 mid-string) and, through the roll panel's OWN gestures, authors the rest of his string - then
runs the game and reads the meter. The meter must say **94**, the number in his video.

| beat | step | what the human sees |
|---|---|---|
| open | `hand intent: open the piano roll` | Ctrl+F1 (the tour's chord, rebound in the lite preamble) |
| begin | `hand intent: begin on BASE` | slot 3 loads; the meter reads 36 |
| honesty | `hand intent: blank the segment after BASE (panel Blank)` | rows 16216..16560 go blank through the Blank button's body |
| honesty | `hand intent: run - nothing lands on a blank roll` | 345 frames under fast-forward; the peak stays **36** - the recording is out of it |
| | `hand intent: reload BASE` | back on slot 3, the same machine hash |
| the hand | 26 × `drag` / `brush` / `tap` | each stroke lands and is read back row by row |
| intent | `hand intent: run - the hand's combo lands` | the peak reads **94** |
| close, end | | the roll closed; the recording's rows restored through the funnel; BASE reloaded |

The 26 strokes are generated from his macro by `tools/hand_strokes.py` (one drag per run of a
button, one BRUSH per run of a direction SET - a direction write replaces the direction group, so
the down-right hold is one brush `v>`, not two drags - a run of one row is a tap) and pinned in
`scripts/fixtures/mvc2/combos/ironman98_hand.txt`; the harness regenerates and diffs them every run.

```
v> 16221..16226   HP 16227..16235   < 16237   v 16238   > 16239..16246   v 16247..16249
HP+LP 16250       LP 16252..16259   v 16255..16271   LP 16261..63 / 65..66 / 68..69
A1 16274..16276 + A2 16274..16275 (the Team Hyper)   A1 16278..79, A2 16279   A1 16282..84, A2 16283
LK 16349..16366   LP 16349..16357   HK 16350..16361   HP 16350..16357
v 16390..16409    v< 16410..16430   v 16431
```

## The panel bodies it drives (roll_panel.cpp, `namespace rollpanel`)

- `blankRange(lo, hi)` - the Blank button (already there).
- `strokeColumn(lo, hi, player, label, gap)` - THE DRAG: `paint().begin/extendTo/build`, the
  stroke's own state machine, committed through `Dojo::ApplyEdit("roll: paint")`.
- `brushStroke(lo, hi, player, "v>", gap)` - a brush of several columns stamped as one pattern
  step (`applyPattern(Pattern::one(lane, bits, bits))`).
- `tapCell(f, player, label)` - the single-cell click.

Nothing writes `session_inputs` directly; every stroke is a funnel edit with undo.

## Runner and ceremony additions

- `dojo:TourPreamble=lite` - only the piano roll's rebind, then the modules (no slot-0 load: the
  module's ceremony loads ITS base). The harness scales G2/G8 for it (`TOUR_PREAMBLE=lite`).
- `dojo:IntentSlot=N` - the ceremony's BASE slot (0 by default; 3 here).
- `intent::runToFrame(f)` - the run to an explicit frame, and it ARMS the roll (`macro_armed`)
  first: `[MEASURED]` the first run recorded the neutral pad over 26 authored strokes and read the
  meter at BASE - WRITE clobbers every frame it passes (dojo.h).
- `TOUR_KEEP_SLOTS=yes` - the harness keeps the clip's numbered slots (it deletes them for the
  base tour, which writes its own).

## Measured

```
scripts/handtour.sh                    PASS  35 steps, gate_ok=35 vacuous=0 leak=0 gates_red=0
  step 5   run - nothing lands on a blank roll   peak=36 (want 36)
  step 33  run - the hand's combo lands           peak=94 (want 94)
scripts/handtour.sh --sabotage hand-thc   the A1+A2 press skipped: the run reads 59, the blank run stays green
                                          SABOTAGE BEHAVED AS PREDICTED
```

`--watch` runs it on the real display with the roll filling in, stroke by stroke.
