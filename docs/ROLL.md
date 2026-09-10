# The TAS studio in this tree — a map

`[2026-09-10]` Twelve modules, ~5,900 lines, ported from a 26,591-line file.
This is what each one is for and, more usefully, **what would go wrong if you
put something in the wrong one**.

Read `docs/PIANO-ROLL-LIFT.md` for how the port was measured,
`docs/ROLL-EDIT-MODEL.md` for why the edit tools collapsed onto one function,
and `docs/STATES-LIFT.md` for the savestate side.

---

## The layers, and the one rule

`CLAUDE.md` §5 states it: **the tool never reads a memory address.** Here it
becomes: the roll never names a game, an emulator, or a file.

| layer | files | may know about |
|---|---|---|
| **the tool** | `roll_select`, `roll_edit`, `roll_pattern`, `roll_remap`, `roll_staged`, `roll_marks`, `roll_meta` | rows, lanes, cells — nothing else |
| **the game profile** | `roll_profile`, `roll_notation` | what an input MEANS: labels, bits, directions, notation |
| **the host** | `roll_host.h` (interface), `roll_slots.cpp` (this emulator's) | savestates, files, the movie |
| **the panels** | `roll_panel.cpp`, `states_panel.cpp` | ImGui, and everything above |

`[MEASURED 2026-09-09]` The lift found **4 of 161** symbols in the fork's roll
touch the emulator and 4 more are the game. That is why this split is cheap: the
roll was never entangled with flycast, it was entangled with itself.

---

## What each module owns

**`roll_profile`** — the column table, and `Cell`. A cell is one lane's input as
opaque bits, and `cellApply(have, bits, mask)` is **the only code in the roll
that knows an input has meaning**: that a direction replaces where a button
accumulates, and that an opposed pair cannot survive. The direction group is
DATA on `Profile`, so a second pad states its own.

**`roll_notation`** — text to cells. ONE dialect where the fork has five. Built
from the profile: buttons match its own labels, the numpad comes from the four
direction bits it names. *A motion is one frame per digit with the buttons on
the last* — the grammar changed once during its own self-test, because the first
version contradicted how anyone reads `236LP`.

**`roll_edit`** — the row codec (`cellOf` / `cellInto`, non-lossy, so unmodelled
bits survive) and the transforms. In-place ones return an `Edit`; **resizes
return a `Resize { edit, remap }`**, with the edit DERIVED from the remap so
there is one owner of "where did row f go".

**`roll_pattern`** — `applyPattern`. **Six customers and not one second loop:**
paint, mash, fill, the brush, `paintColumn`, and placing a staged clip. A
pattern is a periodic payload over a row range at a phase; merge is the MASK,
not a flag.

**`roll_remap`** — where every row went, as spans. Holders REGISTER and callers
make ONE call: savestate anchors on disk, bookmarks, the selection.

**`roll_select`** — the selection grammar (plain / Shift / Ctrl / Alt), a set of
rows and not a range.

**`roll_paint`** — the stroke. Column-locked, set-vs-erase decided at the anchor,
gap rows SKIPPED and never inverted, and the pattern advances in FRAME order
however the drag went.

**`roll_marks`** — bookmarks. A remap customer by construction; a mark on a
deleted frame is dropped where a savestate anchor collapses, because a bookmark
is only a pointer.

**`roll_staged`** — a second DOCUMENT: a clip, an immutable baseline, and an op
queue replayed from it, which is what makes a lossy op reversible.

**`roll_meta`** — the registry for GUI state that rides `dojo`'s single undo
blob. Two customers (anchors, bookmarks) is where one setter became a race.

**`roll_host.h` / `roll_slots.cpp`** — the four questions the roll asks of the
machine, plus the slot view the States wall needs. The implementation caches its
scan and memoises staleness, because both are asked once per drawn row.

---

## How it is tested

**Self-tests** prove a module. `dojo:PanelSelfTest=yes`:
profile 6, select 23, edit 50, paint 21, pattern 22, remap 29, meta 8, marks 18,
notation 15, staged 18, session 22.

**A self-test proves a module and never its integration** — this tree learned
that four times in a week (a panel registry with zero call sites, an edit funnel
that refused a map every unit test accepted, `roll::Host` with no production
implementation, `edit_meta_capture` never assigned). So there are also **probes**:
one-shot integration checks that run inside a real session against a real movie.

| probe | proves |
|---|---|
| `dojo:RollEditProbe` | a transform reaches the funnel and undoes |
| `dojo:RollPaintProbe` | a multi-row stroke, every row of the span |
| `dojo:RollMashProbe` | parser → pattern → funnel |
| `dojo:RollAnchorProbe` | a resize moves a `.frame` sidecar on disk AND a bookmark, and one undo brings both back |
| `dojo:RollMarkProbe` | bookmarks survive save and reload, read from the FILE |
| `dojo:StatesLabelProbe` | naming a slot round-trips through the disk |

**Harnesses**: `scripts/rolltest.sh` drives real clicks and drags;
`scripts/statestest.sh` reads traces with no mouse at all. Both are in ctest.

### Two things the harnesses learned the hard way

**Measure the layout, never assume it.** `rolltest` steered by fixed pixel
offsets and twice reported *"the paint stroke is unwired"* when the stroke was
fine and the table had moved — once because the panel grew a button row, once
because the panel *changed height when you selected something*. It now sweeps
for the columns and the rows and asks the panel where they are. The panel, in
turn, keeps a constant height: controls are disabled, never absent.

**Assert you are in the state you claim to test.** The pause key was sent with
`xdotool --window`, which uses `XSendEvent` — SDL ignores those. It landed only
sometimes, nothing checked, and the movie ran to its end while every
"measurement" was of the playhead moving. Keys go through XTest now and the
pause is confirmed before anything depends on it.

---

## Traces

All log **on change**, so a quiet log is a fact and not a gap.

`dojo:RollSelTrace` · `dojo:RollPaintTrace` (hover column AND row, begin, extend,
commit) · `dojo:RollSlotTrace` · `dojo:RollDecodeTrace` · `dojo:StatesTrace` ·
`dojo:MouseDragTrace` (the SDL drag position — note it was reading the very
function that was corrupting it, which is how a wrong conclusion survived four
"independent" measurements).

## Switches

`dojo:Panel.pianoroll` · `dojo:Panel.states` · `dojo:RemapAnchors=no` (stop
rewriting `.frame` sidecars) · `dojo:MarksPersist=no`.

---

## What is not here yet

The generations pane, slot delete (`hostfs::deleteSavestate` still has zero
callers), save/load from the wall, thumbnails (nothing in this tree writes one —
`GetLastFrameRGB` is DX9/DX11 only), the input/hotkey system, and the
sequence library. `TODOS.md` carries them.
