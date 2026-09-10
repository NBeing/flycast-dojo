#pragma once
#include "types.h"
#include "roll_profile.h"
#include "roll_remap.h"
#include <map>
#include <set>
#include <vector>

/*
	THE ROLL'S EDIT PRIMITIVES, as pure transforms.

	Every one of these READS rows and RETURNS an edit map. None of them writes
	the movie. The caller hands the result to Dojo::ApplyEdit (in place) or
	ApplyEditResize (row count changes), which is the tree's existing choke
	point for every non-recording write: it diffs, persists to the .flyr, logs
	the timeline event the dead-timeline guard needs, and captures the old bytes
	for undo.

	SPLITTING IT THIS WAY IS THE POINT. The transforms are arithmetic over
	std::map, so they are testable with synthetic rows and no emulator - and
	they cannot break record/replay sync, because they do not have a path to
	the movie at all. The one place that can is the funnel, which was already
	written and already guarded.

	THE FUNNEL WANTS THE WHOLE MOVIE, both ways. `[MEASURED 2026-09-09]` an
	edit map naming one frame was REFUSED: "edited movie (11516..11516) does not
	cover the original (0..11519)". ApplyEdit's guard is deliberate - a map that
	does not span the movie silently orphans the frames it omits, which is the
	ambiguity a funnel exists to refuse - so mergeIntoMovie() is the bridge from
	a focused transform to a fundable map. An earlier version of this comment
	said only resize ops needed full coverage. That was wrong, and the unit
	tests could not see it because they never touch the funnel; the integration
	probe caught it on its first run.

	IN PLACE vs RESIZE is a real distinction, not a naming one:

	  blankRows / setColumn   touch the frames they name, nothing moves
	  deleteRows / insertBlanks  RENUMBER the tail, so every frame after the
	                             edit point changes identity - and savestates
	                             anchored past it are stranded (the roll shows
	                             that through Host::slotStale).

	A resize transform therefore returns the WHOLE movie, because
	ApplyEditResize treats "present now, absent from the map" as a deletion.
	Returning only the changed rows there would silently truncate everything
	else - which is exactly the kind of mistake worth a comment rather than a
	bug report.
*/
namespace roll
{

using Row  = std::vector<u8>;
using Edit = std::map<u32, Row>;

//! Bytes in one movie row: both ports' input records, side by side.
size_t rowBytes();

//! A row with no input held - the "blank frame" every insert writes.
Row blankRow();

//! Is this column pressed in this row, for this player? Absent/short rows read
//! as not pressed rather than raising: a hole in the movie is a legitimate
//! state (see movie.h).
bool rowHas(const Row& r, int player, const Column& c);

//! Set or clear one column, returning the changed row.
Row rowWith(const Row& r, int player, const Column& c, bool on);

// ---- THE CELL CODEC -------------------------------------------------------
//
// The row FORMAT half of the cell abstraction: roll_profile owns what a cell
// MEANS, this owns how one is stored. Splitting it here is what lets the
// pattern tools be written against Cells and never touch FrameInputs.

//! How many lanes a row holds. Derived from the record size, never assumed:
//! `sizeof(FrameInputs) * 2` appears 60+ times in the fork being ported, and
//! every one of them is a place a third port would have to be found.
int laneCount();

//! One lane of a row, decoded. An absent or short lane reads as neutral - a
//! hole in the movie is a legitimate state, not an error (see movie.h).
Cell cellOf(const Row& r, int lane);

/*
	One lane of a row, encoded - NON-LOSSILY.

	It starts from `r` and rewrites only the bits this profile models, so
	anything it does not model (analog stick axes, kcode bits no column names)
	SURVIVES. The fork round-trips through its canon word and loses them; that
	is invisible until a movie carrying analog input meets an edit tool.
*/
Row cellInto(const Row& r, int lane, Cell c);

// ---- IN PLACE (hand to ApplyEdit) -----------------------------------------

//! Every named row, blanked. Rows absent from the movie are still written: a
//! selection over a hole means "make these frames exist and be empty".
Edit blankRows(const std::set<u32>& rows);

//! One column set or cleared across the named rows, for one player.
Edit setColumn(const std::map<u32, Row>& src, const std::set<u32>& rows,
		int player, const Column& c, bool on);

/*
	MASH: one column, across a range, every (gap+1)th row.

	THE PHASE IS ANCHORED AT `anchor`, NOT AT THE RANGE START - the fired rows
	are those whose DISTANCE FROM THE ANCHOR is a multiple of the step. Painting
	up from a row and painting down from it therefore produce the same pattern,
	and the anchor itself always fires. Keying the phase off the range start
	instead would shift the pattern with the drag direction, which a user feels
	as "mash lands differently if I drag upwards".

	gap 0 = every frame, 1 = every other (30 Hz on a 60 Hz movie), 2 = every
	third (20 Hz) - the rates a mash actually wants.

	Returns the WHOLE movie, extended with blank rows if the range runs past the
	end. ApplyEdit permits that: "extension is fine, truncation is not".
*/
Edit paintColumn(const std::map<u32, Row>& all, u32 anchor, u32 to,
		int player, const Column& c, bool on, int gap);

//! Overlay a focused edit onto the movie, producing a map the funnel accepts.
//! Rows the edit names win; every other frame is carried through unchanged.
Edit mergeIntoMovie(const std::map<u32, Row>& all, const Edit& e);

// ---- RESIZE (hand to ApplyEditResize) -------------------------------------

/*
	A RESIZE IS TWO THINGS AND BOTH ARE RETURNED.

	The new movie, and WHERE EVERY ROW WENT. Renumbering is the whole difference
	between an in-place edit and a resize, and several things hold row indices -
	the selection, bookmarks, a savestate anchored past the edit point.

	RETURNED RATHER THAN AN OUT-PARAMETER, because an out-parameter is a thing a
	caller can forget, and forgetting is precisely the fork's bug: its bookmark
	fixups are hand-called from five sites with nothing enforcing it, and its
	selection shift is written five times with two of them wrong
	(docs/ROLL-EDIT-MODEL.md §3).

	THE EDIT IS DERIVED FROM THE REMAP, not computed beside it. One owner for
	"where did row f go", so the map and the movie cannot disagree - which also
	means there is no test here asserting that they agree, since a check over one
	derivation of one fact cannot fail.
*/
struct Resize
{
	Edit  edit;
	Remap remap;
};

//! `rows` removed, everything after them pulled up. Returns the whole movie.
Resize deleteRows(const std::map<u32, Row>& all, const std::set<u32>& rows);

//! `count` blank frames inserted BEFORE `at`, pushing the tail down. Returns
//! the whole movie.
Resize insertBlanks(const std::map<u32, Row>& all, u32 at, u32 count);

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void editSelfTest();

}	// namespace roll
