# Lifting the piano roll: what it actually depends on

The §6b method — put the thing in its own translation unit, compile it, and take
the compiler's errors as the dependency list. Measured 2026-09-09 against
`davidrr/main` (`edca8915b`).

Used because estimating this by reading is unreliable: the same method on the
Input Visualizer earlier showed a "372 lines, cheapest" reading was short by nine
shared helpers and three colour aliases.

## The subject

`DojoGui::show_piano_roll()` — `core/dojo/dojo_gui.cpp` lines 13141-16972.
**3,832 lines in one function**, inside a 26,591-line file.

## Method

Extracted verbatim into its own TU with David's include block, compiled with the
project's real flags (taken from `ninja -t commands`), `-fsyntax-only
-fmax-errors=0`.

One trap worth recording: compiled as `DojoGui::show_piano_roll` it produced
**one** error — `no declaration matches` — because our `dojo_gui.h` does not
declare that member, so gcc rejects the definition and never analyses the body.
It looks like a clean compile and is the opposite. Renaming it to a free function
forced real analysis: **739 errors**.

## Result

| | count |
|---|---|
| distinct undeclared symbols | 222 |
| already present in our tree | 25 |
| **real dependencies** (defined elsewhere in his file) | **161** |
| cascade noise (locals gcc lost after earlier errors) | 36 |

The last row is separated deliberately. `bh2`, `hm`, `pv`, `COLS` are locals, not
dependencies; quoting 197 would overstate the coupling. A symbol counts as real
only if it is *defined* elsewhere in `dojo_gui.cpp`, outside the lifted range.

## What the 161 are, which is the interface design

| cluster | ~count | belongs to |
|---|---|---|
| edit tools — mash / brush / paint / stretch / repeat / staged | 37 | the roll itself |
| selection — anchor, drag, set, copy / delete / compress | 12 | the roll itself |
| bookmarks | 7 | the roll itself |
| sequence library | 6 | the roll itself |
| undo/redo verbs (`RA_UNDO`, `RA_REDO`, ...) | 4 | the roll itself |
| ImGui wrappers + palette | 11 | chrome (we already have `tas_colors.h`) |
| clip / movie | 7 | host |
| **column model** — `COLS`, `NCOLS`, `TAS_CANON_OF_COL`, `tasCanonToPacket` | **4** | **game profile** |
| **flycast `gui.cpp`** — `gui_slot_stale`, `gui_stale_blink`, `gui_stale_blink_deleted`, `gui_state_frames` | **4** | **host** |

## The conclusion

**Only 4 of 161 symbols touch flycast directly**, and all four ask savestate
questions: which slots are stale, what frame each state sits at. **Four more are
the MvC2 coupling**, and they are a column table plus a canon-to-packet mapping —
data, not logic.

So the roll is overwhelmingly coupled to *itself*, not to the emulator and not to
the game. The port is therefore not "untangle it from flycast". It is:

  take the roll AND its edit tools as one unit, behind a four-function host
  interface and a column-table profile.

That is a far better position than a 26,591-line file implies, and it is the
reason to measure before planning.

## Not yet answered

- The same lift has not been run on the States window; its coupling is assumed
  similar and that assumption is untested.
- 37 edit-tool symbols is the largest cluster and none of it has been read yet -
  it may itself split into generic (insert/delete/repeat) and profile-shaped
  (what a "mash" pattern means for a given game).
- The lift proves what the roll REFERENCES. It does not prove the referenced
  code is portable; a symbol that turns out to reach into `dojo` globals moves
  from "the roll itself" to "host".
