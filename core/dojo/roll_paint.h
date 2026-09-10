#pragma once
#include "roll_edit.h"
#include "roll_profile.h"
#include "roll_pattern.h"
#include <map>
#include <vector>

/*
	A PAINT STROKE: the roll's central gesture.

	Press a cell, drag down a column, release - and the frames you crossed take
	that input. It is one edit, not one per row.

	FOUR RULES, all taken from the fork's stroke rather than invented, because
	each one is a thing a user notices being wrong:

	  COLUMN- AND PLAYER-LOCKED. A stroke stays in the column and port it began
	  in, however far sideways the mouse wanders.

	  SET vs ERASE IS DECIDED AT THE ANCHOR. Starting on a cell that was already
	  pressed ERASES for the whole stroke; starting on an empty one writes. Alt
	  forces erase. Deciding per-cell instead would make a drag flicker between
	  writing and erasing as it crossed existing input.

	  EXTENT, NOT PER-HOVERED-ROW. The stroke is anchor..last and is rebuilt
	  from that range every time; it is never accumulated. A fast mouse skips
	  rows between frames, and per-row painting would leave holes the user did
	  not ask for - and a stroke that only grew could never be walked back.

	  THE GAP IS COUNTED FROM THE ANCHOR, and gap rows are SKIPPED, NOT
	  INVERTED. `step = gap + 1`; a row is painted when `|row - anchor| % step`
	  is zero and otherwise LEFT ALONE. Counting from the anchor pins the
	  pattern to where the stroke began, so dragging up and dragging down fire
	  the same frames. Inverting the gap rows instead would quietly erase input
	  the user never touched - which is what "every 2nd row" would mean if you
	  guessed at it.

	No ImGui here, like the selection grammar: a stroke is arithmetic over rows,
	and the funnel is the only thing that writes.
*/
namespace roll
{

class Paint
{
public:
	//! Begin a stroke. `wasOn` is the anchor cell's current state - it decides
	//! set vs erase. `forceErase` is the Alt modifier. `gap` is 0 (every row),
	//! 1 (every 2nd) or 2 (every 3rd).
	void begin(u32 row, int player, int column, bool wasOn, bool forceErase, int gap);
	void extendTo(u32 row);
	void end();

	bool active() const  { return active_; }
	bool writes() const  { return on_; }		//!< false = this stroke erases
	int  column() const  { return col_; }
	int  player() const  { return player_; }
	u32  lo() const      { return anchor_ < last_ ? anchor_ : last_; }
	u32  hi() const      { return anchor_ < last_ ? last_ : anchor_; }

	//! Is this row one the stroke actually writes? Gap rows are not, and the
	//! preview must agree with the commit or the roll lies about what a release
	//! will do.
	bool touches(u32 row) const;

	/*
		ARM A PATTERN, so a stroke STAMPS instead of setting one column.

		Same gesture, same phase, same gap - only the payload changes, which is
		the whole point of docs/ROLL-EDIT-MODEL.md §2: the brush and the paint
		are one function once the payload has a name. There is no second stroke
		class and no second loop.

		SET-VS-ERASE DOES NOT APPLY WHILE ARMED. A pattern says what to write on
		every frame it fires; asking the anchor cell whether to set or clear
		would make the same brush mean two different things depending on where
		it was started. Alt still erases, by arming nothing.

		An empty vector disarms, which is why there is no separate disarm().
	*/
	void arm(const std::vector<Cell>& cells, bool merge);
	bool armed() const { return !brush_.empty(); }

	//! The stroke as a funnel-ready edit: the WHOLE movie, because ApplyEdit
	//! refuses a map that does not cover it. Frames past the end are created.
	Edit build(const std::map<u32, Row>& all) const;

private:
	std::vector<CellOp> brush_;		// empty = single-column mode
	bool active_ = false;
	bool on_ = true;
	int  player_ = 0, col_ = 0, step_ = 1;
	u32  anchor_ = 0, last_ = 0;
};

Paint& paint();
void paintSelfTest();

}	// namespace roll
