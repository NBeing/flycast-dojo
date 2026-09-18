# The CSS tour - David's character-select utility on DC

`[2026-09-18]` `core/dojo/css.{h,cpp}` (the port, pure), `core/dojo/css_tour.cpp` (the
module), `scripts/csstour.sh` (the harness), `scripts/fixtures/mvc2/css/` (the seed, the
picks, `PICKS.toml`, the gitignored `base/`). The full account is `docs/TEST-PLAN.md` §7.

## Run it

```
scripts/csstour.sh                       # ~3-5 min on a private Xvfb; exit 0/1/2/4/5/77
scripts/csstour.sh --watch               # on your display, hands off; the emulator stays open
scripts/csstour.sh --keep-base <dir>     # also copy the saved base's clip folder there
scripts/csstour.sh --sabotage css-walk   # css-walk | css-timing | css-team; --list-sabotage
scripts/csstour.sh --self-test           # the judge over a canned log, no emulator
scripts/fixtures-check.sh --make-dhalsim-base   # run the tour, keep the base, pin its hash
CSSTOUR_OUT=<dir> scripts/csstour.sh     # keep the sandbox (out.log, the clip folder)
```

## What it proves, and what it does not

- David's globe geometry, `plan()`, `build_picks()` and his timing table transfer to
  DC whole: every cursor press reads back the character the graph predicts, every pick
  reads back its palette and assist, both teams lock as asked, Start at SPEED SELECT
  starts the fight with Dhalsim on point.
- The base it saves is AUTHORED FROM INPUTS - no savestate is shipped; `RECIPE.toml
  [dhalsim_base]` pins the inputs (two seqHashMacro) and the machine hash the tour lands
  on. Regenerate with `--make-dhalsim-base`; a pin that moved is a finding.
- The team is OURS (`PICKS.toml`), not David's: his files never name his team.
- Whether David's Dhalsim97 rows connect on this base is the `css finding:` step -
  optional, reported with its peak, never a pass condition. PS2->DC timing may never
  connect; that is a finding, not a bug.

## The two traps (designed around, not to be rediscovered)

1. The Surface Tour's `Setup` calls `gui_pause_for_checkout()`, which clears
   `dojo.stepping` and would kill the seed's run-to-handoff - the runner waits for
   `TAS ONENTER: handoff` on a seeded boot.
2. A Record-MOVIE handoff drops READ-WRITE to WRITE and the neutral pad then clobbers
   every remaining row as the tour steps - the harness boots `MacroMode=yes` so the
   handoff keeps READ-WRITE.
