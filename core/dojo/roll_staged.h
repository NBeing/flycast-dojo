#pragma once
#include "roll_edit.h"
#include "roll_pattern.h"
#include <map>
#include <set>
#include <vector>

/*
	THE STAGED BUFFER: a SECOND DOCUMENT, not a staging area for the movie.

	`[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md §5. The name suggests
	edit-then-apply, and that is not what it is. It holds a clip, an IMMUTABLE
	BASELINE of that clip, and an ordered queue of transforms replayed from the
	baseline on every change - which is what makes a LOSSY operation reversible.
	Compress a staged clip by 3 and then remove the op, and the dropped frames
	come back, because they were never dropped: the baseline still has them.

	THAT IS THE WHOLE IDEA WORTH PORTING, and the movie's own undo stack cannot
	provide it. An undo stack remembers what a document USED to be; an op queue
	remembers what was ASKED FOR, so the recipe can be edited rather than only
	walked back.

	TWO HAZARDS FROM THE FORK, named so they are not acquired by accident:

	  ITS BUTTON ROW EDITS WHICHEVER DOCUMENT IS ACTIVE. `if (stagedActive)
	  stagedTogOp(TXK_SWAP, ...); else tasSwapSelPlayers(selRows());` - the same
	  button means two different things depending on state that lives somewhere
	  else on screen. Here the staged ops are their own controls.

	  AND THERE ARE TWO UNDO SYSTEMS ON ONE KEY - the movie's patch stack and
	  the queue's pop. Ctrl+Z means different things depending on focus. Here the
	  queue is edited by its own buttons and never by undo.

	THE OPS ARE THE SAME ARITHMETIC AS THE MOVIE TOOLS, over the same Cells, so
	nothing here is a second implementation of reverse or stretch. Only Flip
	needs the profile, because mirroring is about what LEFT means.
*/
namespace roll
{

//! One frame of a staged clip: one Cell per lane.
using Frame = std::vector<Cell>;
using Clip  = std::vector<Frame>;

enum class Op
{
	SwapLanes,		//!< lane 0 <-> lane 1
	CopyLaneUp,		//!< lane 0 -> lane 1
	CopyLaneDown,	//!< lane 1 -> lane 0
	Flip,			//!< mirror left and right - the one op that asks the profile
	Reverse,
	Stretch,		//!< each frame held `k` times
	Compress,		//!< every `k`th frame kept
};

struct Step
{
	Op  op;
	int k = 2;		//!< Stretch / Compress only
};

class Staged
{
public:
	//! Take the named rows out of a movie as the baseline. Replaces whatever
	//! was staged; the movie is not touched, now or ever, until place().
	void load(const std::map<u32, Row>& all, const std::set<u32>& rows);
	void unload();

	bool  empty() const  { return now_.empty(); }
	size_t size() const  { return now_.size(); }
	size_t baseSize() const { return base_.size(); }
	const Clip& clip() const { return now_; }
	const std::vector<Step>& ops() const { return ops_; }

	//! Append a step and replay the whole queue from the baseline.
	void push(Step s);
	//! Drop the last step and replay. THE REASON THE BASELINE EXISTS.
	void pop();
	void clearOps();

	/*
		The staged clip written into the movie at `at`, as a funnel-ready edit.

		IN PLACE - it overwrites `size()` frames from `at` and does not renumber,
		so it is an ApplyEdit and needs no remap. Inserting a staged clip is
		insertBlanks followed by this, which is two edits and two undo steps, and
		that is the honest shape rather than a third resize primitive.

		`merge` masks only the bits each cell carries, so an overdub cannot clear
		what it does not name.
	*/
	Edit place(const std::map<u32, Row>& all, u32 at, bool merge) const;

private:
	void recompute();

	Clip base_;
	Clip now_;
	std::vector<Step> ops_;
};

Staged& staged();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void stagedSelfTest();

}	// namespace roll
