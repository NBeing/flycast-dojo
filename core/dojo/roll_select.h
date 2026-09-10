#pragma once
#include "types.h"
#include "roll_remap.h"
#include <set>

/*
	WHAT IS SELECTED IN THE ROLL, and the grammar of selecting it.

	SELECTION IS A SET OF ROWS, NOT A RECTANGLE. That is a finding, not a
	choice: David's roll keeps `std::set<u32> selSet` of FRAME numbers, and
	every edit tool takes the whole frame. A cell-range model would be a
	different tool wearing the same name, and the ported tools would not fit it.

	NON-CONTIGUOUS ON PURPOSE. Ctrl adds a disjoint row, so the set cannot be
	collapsed to lo..hi - and `lo()`/`hi()` are the bounds of the set, never a
	claim that everything between them is in it.

	NO IMGUI HERE. The grammar takes the modifier state as data, so it can be
	exercised without a frame - the same reason panels::visitStream does no
	drawing. A selection model that can only be tested by clicking is a
	selection model nobody tests.
*/
namespace roll
{

//! Modifier state at the moment of a press. Passed in rather than read, so the
//! grammar is a pure function of its inputs.
struct Mods
{
	bool shift = false;
	bool ctrl  = false;
	bool alt   = false;
};

class Selection
{
public:
	/*
		THE GRAMMAR, ported verbatim from the fork's gutter handler:

		  plain       replace, and anchor here
		  Shift       range from the anchor REPLACES
		  Ctrl        toggle this row
		  Ctrl+Shift  range from the anchor ADDS
		  Alt         clear

		Shift with no anchor yet falls through to plain - a range needs two
		ends, and silently doing nothing would read as a dead click.
	*/
	void press(u32 row, Mods m);

	//! Extend a live drag to `row`: the mousedown snapshot, plus the inclusive
	//! range anchor..row. Repainting from the SNAPSHOT each time is what lets a
	//! drag shrink as well as grow, and what makes Ctrl-drag additive while a
	//! plain drag replaces.
	void dragTo(u32 row);
	void release();

	/*
		FOLLOW A STRUCTURAL EDIT.

		Rows that were deleted drop out; rows that moved move. The alternative
		this replaces was clearing the selection outright, which is defensible
		but throws away work - and the alternative the fork chose is worse: it
		shifts by hand at five call sites and gets two of them wrong.

		A live drag is ENDED rather than remapped. The mouse is still down, but
		what it was dragging over no longer has the same frame numbers, and
		continuing to extend from a stale anchor would select a range the user
		never crossed.
	*/
	void remap(const Remap& m);

	void clear();
	bool has(u32 row) const   { return rows_.count(row) != 0; }
	bool empty() const        { return rows_.empty(); }
	size_t count() const      { return rows_.size(); }
	bool dragging() const     { return dragging_; }
	u32  anchor() const       { return anchor_; }

	//! Bounds of the SET. With a non-contiguous selection the rows between them
	//! are not necessarily selected - ask has().
	u32 lo() const { return rows_.empty() ? ~0u : *rows_.begin(); }
	u32 hi() const { return rows_.empty() ? ~0u : *rows_.rbegin(); }

	const std::set<u32>& rows() const { return rows_; }

private:
	std::set<u32> rows_;
	std::set<u32> base_;		// snapshot at press; a drag repaints on it
	u32  anchor_   = ~0u;
	bool dragging_ = false;
};

Selection& selection();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void selectionSelfTest();

}	// namespace roll
