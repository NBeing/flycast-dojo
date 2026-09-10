# The roll's edit model — the design, before the port

`[MEASURED 2026-09-09]` A survey of all ~37 edit-tool symbols in the fork's
`show_piano_roll` (`flycast-rr-oracle/core/dojo/dojo_gui.cpp`, 13141–16972 —
3,831 lines in one function). `docs/PIANO-ROLL-LIFT.md` counted them and left
the important question open:

> 37 edit-tool symbols is the largest cluster and none of it has been read yet -
> it may itself split into generic (insert/delete/repeat) and profile-shaped
> (what a "mash" pattern means for a given game).

It does split. **Not where that guess said.**

---

## 1. The finding

| | count |
|---|---|
| generic row/column arithmetic — same meaning on any game, any emulator | ~25 |
| **profile chokepoints** | **3** |
| generic bodies with ONE profile-shaped payload threaded through | ~9 |
| host — savestates, files, clipboard, the funnel | ~12 |

The three chokepoints. Everything profile-shaped reaches the game through one
of them:

1. **the column table** — 11 columns bound to Dreamcast bits and MvC2 names
2. **the canon ↔ packet mapping** — including the trigger duality, where a
   trigger is both a byte (`>= 0x20`) and a `kcode` bit
3. **the notation parsers** — five dialects of the same 11 bits, including
   `"MP"` meaning LP and a keyboard alphabet `P1: WSADZXCVBNM, P2: TGFHUIOJKLP`

Everything else that *looks* game-coupled is coupled only because a `u16` canon
word passes through it.

## 2. The abstraction that falls out

**One function, written four times.** These are the same operation:

    tasMashPlace2          tile two canon tracks over a row range, every Nth row
    tasFillRowsWithMacro   cycle a clip's frames across a selection
    the brush stroke       stamp a canon pattern down a drag, every Nth row
    our paintColumn        set one column down a drag, every Nth row

The shared shape: **a periodic payload, applied to a row range, at a phase
anchored somewhere, with a combine rule.** They differ only in what the payload
is — two canon tracks, a clip's frames, a brush pattern, a single column bit —
and the type system never named it, so the loop got written four times and
drifted four ways (§4).

So the model is three concepts, not thirty-seven verbs:

**A CELL IS OPAQUE TO THE TOOL.** A tool never asks what a cell means. The
profile owns `combine(cell, cell, merge)`, `subtract(cell, mask)` and
`flip(cell)`. That is where SOCD cleaning belongs — up+down is illegal on this
hardware, a pad fact and not arithmetic — along with "a direction replaces but
buttons OR", and a trigger's two representations.

**A PATTERN IS A PERIODIC SEQUENCE OF CELLS PER LANE.** Length L, tiled;
`step = gap + 1`; a phase anchor. Mash, brush, stamp, fill, autofire and our
paint are all "apply this pattern here" with different patterns. Note there is
no symbol named `autofire` anywhere in the fork — it is a cadence dial on the
brush, `{ ALL 60Hz, 2ND 30Hz, 3RD 20Hz }`, which is exactly `gap`.

**A ROW IS N LANES, NOT TWO PLAYERS.** `sizeof(FrameInputs) * 2` appears **60+
times** in the fork. Lane count is derivable — row size over record size — and
the tools that look player-specific (clone P1→P2, swap lanes, the "player is
empty" sentinels computed as `index / NCOLS`) are lane arithmetic once named.

## 3. The second abstraction: a structural edit returns a REMAP

The fork's structural tools each hand-maintain everything that holds a row
index. Selection shifting is written **five times**, and **two of the five are
wrong** — they shift unconditionally instead of only past the insert point, so
inserting at frame 500 slides a selection at rows 10–20 forward for no reason.
Bookmarks are worse: `tasBookmarksOnInsert` / `tasBookmarksOnDelete` must be
called by hand by every structural tool, from five sites, and **nothing enforces
it**.

Every one of those is the same fact — *frame f is now frame g* — recomputed by
each caller. So an insert / delete / stretch / compress should RETURN that
mapping, and everything holding a row index applies it: the selection,
bookmarks, and **savestate anchors, which are row indices too**.

That last one is not a bonus. `docs/STATES-LIFT.md` §G9 records that a resize
renumbers `session_inputs` and never rewrites any `.frame` sidecar, so an
anchored state's frame becomes **wrong**, not merely suspect — and nothing in
the tree distinguishes those two conditions. The remap is the fix for both, and
it is the reason to settle this design before porting either window.

## 4. What NOT to port — inherited bugs, with evidence

Named here because each is something our version could quietly acquire by
copying the shape.

**The brush stamps backwards on an upward drag.** The fired-row ordinal is
`d / step` where `d` is the ABSOLUTE distance from the anchor, so dragging up
from row 100 writes pattern index 0,1,2… at rows 100,99,98 — the pattern plays
in reverse frame order. A quarter-circle-forward brush dragged upward stamps a
quarter-circle-back shape. **Our paint is immune only because it is
single-column**: every fired row gets the same value, so the generic path cannot
see the bug. It becomes reachable the moment a pattern payload lands.

**REPLACE-mode brush zeroes the rows it skips.** Non-fired rows are written with
`0` and `merge=false`, wiping what was there; in MERGE mode they are left alone.
Same gesture, opposite destructiveness, decided by a switch in another panel.
Our stroke already states the opposite rule and must keep it: *gap rows are
SKIPPED, NOT INVERTED*.

**"Success" stopped meaning "changed".** A merge that finds nothing to do
returns the target frame — success, no timeline event — so every caller that
branches on `first >= 0` to report "placed N frames" lies. Our funnel has the
mirror ambiguity: `ApplyEdit` returns −1 for *refused*, for *nothing changed*,
and for *hit a locked range*. Three outcomes, one value.

**Six tools, five hole policies.** A movie with holes is a legitimate state —
`core/dojo/movie.h` exists to keep hole and end distinct — and the fork's tools
disagree: clear skips holes, replace materialises them, delete counts them as
frames, reverse and stretch materialise them as neutral, compress counts them
for bookmarks but not for content. The toast always reports the selection size,
never what was actually touched. **We declare one policy and state it once.**

**Three answers to "what does a gapped selection mean".** Stretch, compress and
apply-queue refuse it; reverse, delete, clear, swap, flip and fill allow it;
copy and repeat allow it and silently drop the gaps.

**Two undo systems on one key.** The movie has a patch stack (one `ApplyEdit` =
one undo, capped at 128, with a documented redo hole where redoing a delete
zeroes the tail instead of re-shrinking). The staged buffer has a separate op
queue with pop-based undo. Ctrl+Z means different things depending on focus.

**Insert-before versus insert-after in one gesture handler.** The same gutter
drag over the same `lo..hi` inserts at `lo` with one modifier and at `hi + 1`
with another.

## 5. The staged buffer is a second document, not a staging area

Worth separating from "edit-then-apply", which is what the name suggests. The
fork's staged buffer holds *a macro, an immutable baseline, and an ordered op
queue*, replayed from the baseline on every change — which is what makes lossy
operations (compress) reversible. The movie is byte-typed; the buffer is
canon-typed; **the same button row edits whichever is active**. A real idea
worth having, and a real hazard worth not acquiring by accident.

## 6. What this changes about the port

The remaining ~32 unported symbols are not 32 pieces of work:

- **stretch, compress, reverse, clear, the tail splice** — fully generic, and
  three of them exist in the fork in three type systems each. Small once rows
  are opaque.
- **mash, fill, brush, stamp, paint** — ONE function under a pattern payload.
  We have its generic core already, in `roll_paint` + `paintColumn`.
- **clone-lane, swap-lanes, flip** — lane arithmetic plus one profile call.
- **replace (find one column, write another)** — generic given the `Column`
  concept we already have.
- **notation, parsers, the sequence library** — profile and chrome, and the
  place to be most careful, since five dialects is where the game leaks in.

Order that follows: name the cell and the pattern first, because nine tools
collapse onto them; then make structural edits return a remap, because that is
what the selection, bookmarks and savestate anchors all need, and it is the
join with the States window.

---

## 7. LANDED `[2026-09-09]` — the cell and the pattern

`core/dojo/roll_profile.{h,cpp}` gained `Cell`, `cellHas`, `cellWith`,
`cellApply`, `cellAll`, and the direction group as **data** on `Profile`
(`dirs`, `opposed[]`) so a second pad states its own grouping rather than
inheriting a Dreamcast's. `core/dojo/roll_edit.{h,cpp}` gained the codec —
`laneCount`, `cellOf`, `cellInto`. `core/dojo/roll_pattern.{h,cpp}` is the one
function.

**Three things came out better than the fork, deliberately:**

**Merge is not a flag; it is what a mask means.** A `CellOp` is `{bits, mask}`
and the four uses — paint on, paint off, stamp-replacing, overdub — are four
masks. A flag is how the fork ended up with a brush that zeroes the rows it
skips in one mode and leaves them alone in the other.

**The pattern advances in FRAME order however the drag went.** The fork indexes
by absolute distance from the anchor, so an upward drag plays the pattern
backwards in time; the phase stays anchored while step 0 lands on the lowest
firing row. Its single-column case cannot see the difference, which is why it
survived there and why our own paint was immune by accident.

**The codec is non-lossy.** `cellInto` starts from the existing row and rewrites
only modelled columns, so analog axes and unnamed kcode bits survive. The fork
round-trips through its canon word and drops them.

### The remap, `[2026-09-09]`

`core/dojo/roll_remap.{h,cpp}`. `deleteRows` and `insertBlanks` now return a
`Resize { edit, remap }` — **returned, not an out-parameter**, because an
out-parameter is a thing a caller can forget and forgetting is the fork's bug.

**The edit is DERIVED from the remap**, not computed beside it. One owner for
"where did row f go", so the movie and the map cannot disagree — which is also
why there is no test asserting that they agree: a check over one derivation of
one fact cannot fail. What the tests assert instead is the fact itself.

Spans, not a table: a run of surviving rows shares one delta, so deleting N
scattered rows costs N+1 spans rather than an entry per frame on an 11,520-frame
movie. A row covered by no span **does not exist** — which is the answer a
deletion has to be able to give, and answering "unchanged" instead is what makes
a selection keep naming frames that now hold something else.

`Selection::remap()` is the first customer: the panel used to `clear()` after a
resize, which is defensible but throws work away. A live drag is ENDED rather
than remapped — the mouse is still down, but what it was dragging over has
different frame numbers now.

**Savestate anchors are the second customer, `[2026-09-10]`.** `Host::rowsRemapped()`
rewrites the `.frame` sidecars so a slot's marker follows its row. Six
constraints, because this writes the user's work: only with a clip folder open
(with none, the read and write derivations can name different directories,
`docs/STATES-LIFT.md` G13); only the sidecar, never the `.state`; only slots
that already have one; the file's length and every other field survive, so a v1
4-byte sidecar does not silently become a v3; atomic temp-plus-rename; and
`dojo:RemapAnchors=no`.

**The staleness verdict is deliberately untouched.** Moving the anchor puts the
marker on the right ROW. Whether the state still belongs to the timeline is a
different question the rewind log already answers, and a resize logs an event at
or below every row this moves — so these states remain correctly flagged.

**An anchor whose row was deleted** gets `Remap::collapsed()` — the index the
tail closed up to. The two alternatives are worse: the old number points at a
frame now holding different content, and zero means "no anchor at all", a value
already overloaded (`docs/STATES-LIFT.md` §4.3).

**Undo works because the rails already existed and had no customer.** dojo's
`EditPatch` carries an opaque `gui_meta` captured pre-edit and reapplied on
undo, written for bookmarks — and neither `edit_meta_capture` nor
`edit_meta_apply` had ever been assigned anywhere in the tree. The host
registers them now.

**The collapse is verified by what did NOT change.** `paintColumn` is now four
lines delegating to `applyPattern`, and roll_edit's 29 claims and roll_paint's
18 pass unaltered — that, rather than the new tests, is the evidence the two
loops were one loop. `patternSelfTest` adds 18 more, and four sabotages each
break their own claims: indexing by absolute distance breaks only the frame-order
claim; writing neutral into gap rows breaks the gap claims **in both pattern and
paint**, which is itself proof they share the code now; dropping the direction
rule breaks only the direction and SOCD claims; and treating an empty track as
neutral breaks only the empty-track claim.
