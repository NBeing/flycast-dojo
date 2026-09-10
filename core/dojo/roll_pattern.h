#pragma once
#include "roll_edit.h"
#include "roll_profile.h"
#include <map>
#include <set>
#include <vector>

/*
	A PATTERN, AND THE ONE FUNCTION THAT APPLIES IT.

	`[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md. A survey of the 37 edit-tool
	symbols in the fork's show_piano_roll found that four of them are the same
	operation written four times:

	    tasMashPlace2          tile two canon tracks over a range, every Nth row
	    tasFillRowsWithMacro   cycle a clip's frames across a selection
	    the brush stroke       stamp a canon pattern down a drag, every Nth row
	    our own paintColumn    set one column down a drag, every Nth row

	The shared shape is: A PERIODIC PAYLOAD, APPLIED TO A ROW RANGE, AT A PHASE
	ANCHORED SOMEWHERE. They differ only in what the payload is - and the type
	system never named it, so the loop was written four times and drifted four
	ways: two different cycling-fill semantics where one destroys the other
	player, three gap-fill spellings, two different phase origins.

	This is that function. paintColumn is now a caller of it, not a rival.
*/
namespace roll
{

/*
	ONE PATTERN STEP, FOR ONE LANE: within `mask`, the lane's bits become `bits`.

	The mask IS the merge mode, which is why there is no merge flag:

	    paint a column on    mask = that column,   bits = that column
	    paint a column off   mask = that column,   bits = 0
	    stamp, replacing     mask = cellAll(),     bits = the pattern frame
	    overdub              mask = the frame's bits, bits = the same

	A flag instead is how the fork got a brush that ZEROES the rows it skips in
	one mode and leaves them alone in the other.
*/
struct CellOp
{
	Cell bits = 0;
	Cell mask = 0;
};

/*
	ONE TRACK PER LANE, each a sequence tiled over the fired rows.

	AN EMPTY TRACK MEANS "LEAVE THAT LANE ALONE" and never "write neutral".
	That distinction is not cosmetic: in the fork, Paste Fill and Mash Fill are
	both labelled "Fill" and disagree about exactly this, so one of them wipes
	the other player on every row it touches when the clip is single-player.
*/
struct Pattern
{
	std::vector<std::vector<CellOp>> tracks;

	size_t period() const;		//!< longest track; 0 when nothing would be written
	bool   empty() const { return period() == 0; }

	//! The single-lane, single-step case - what a paint stroke is.
	static Pattern one(int lane, Cell bits, Cell mask);
};

/*
	Apply `p` over anchor..to, every (gap+1)th row.

	THREE RULES, and each is a thing a user notices being wrong:

	  THE PHASE IS ANCHORED AT `anchor`. A row fires when its DISTANCE from the
	  anchor is a multiple of the step, so dragging up and dragging down fire
	  the same frames and the anchor itself always fires.

	  GAP ROWS ARE SKIPPED, NOT INVERTED, and never written. Writing neutral
	  into them - which the fork's replace-mode brush does - silently erases
	  input the user never touched.

	  THE PATTERN ADVANCES IN FRAME ORDER, always. `[CORRECTED from the fork]`
	  its brush indexes by the ABSOLUTE distance from the anchor, so dragging
	  UPWARD stamps pattern step 0,1,2... at rows 100,99,98 - the pattern plays
	  backwards in time. A quarter-circle-forward brush dragged up writes a
	  quarter-circle-back shape. It survived there because the single-column
	  case cannot see it: every fired row gets the same value.

	Returns the WHOLE movie, extended with blank rows if the range runs past the
	end, because ApplyEdit refuses a map that does not cover the movie and
	permits extension but not truncation.
*/
Edit applyPattern(const std::map<u32, Row>& all, u32 anchor, u32 to,
		const Pattern& p, int gap);

/*
	The same pattern, cycled over a SET of rows instead of a range.

	One pattern step per row, in ascending frame order - so a gapped selection
	gets consecutive steps on rows that are not consecutive, which is what
	"fill these frames with this" means when the frames are scattered.

	`[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md §4: the fork has TWO buttons
	labelled "Fill" whose loops disagree about untouched lanes, so one of them
	wipes the other player on every row it touches when the clip is single-
	player. Here that cannot happen: an empty track means LEAVE THAT LANE ALONE
	and a mask decides what a written lane clears.

	No gap and no phase - a set has no anchor to count from. Gaps belong to the
	range form, where "every Nth row" is meaningful.
*/
Edit applyPatternToRows(const std::map<u32, Row>& all, const std::set<u32>& rows,
		const Pattern& p);

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void patternSelfTest();

}	// namespace roll
