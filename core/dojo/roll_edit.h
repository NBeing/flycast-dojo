#pragma once
#include "types.h"
#include "roll_profile.h"
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

// ---- IN PLACE (hand to ApplyEdit) -----------------------------------------

//! Every named row, blanked. Rows absent from the movie are still written: a
//! selection over a hole means "make these frames exist and be empty".
Edit blankRows(const std::set<u32>& rows);

//! One column set or cleared across the named rows, for one player.
Edit setColumn(const std::map<u32, Row>& src, const std::set<u32>& rows,
		int player, const Column& c, bool on);

// ---- RESIZE (hand to ApplyEditResize) -------------------------------------

//! `rows` removed, everything after them pulled up. Returns the whole movie.
Edit deleteRows(const std::map<u32, Row>& all, const std::set<u32>& rows);

//! `count` blank frames inserted BEFORE `at`, pushing the tail down. Returns
//! the whole movie.
Edit insertBlanks(const std::map<u32, Row>& all, u32 at, u32 count);

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void editSelfTest();

}	// namespace roll
