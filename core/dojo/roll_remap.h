#pragma once
#include "types.h"
#include <set>
#include <vector>

/*
	WHERE EVERY ROW WENT.

	A structural edit renumbers the movie: delete three frames and everything
	after them is a different frame than it was. Several things hold row indices
	and every one of them has to be told - the selection, bookmarks, a savestate
	anchored past the edit.

	`[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md §3. In the fork being ported
	each of those is the CALLER's job, and it goes exactly as that implies:

	  the selection shift is written FIVE times, and TWO of the five are wrong -
	  they shift unconditionally instead of only past the insert point, so
	  inserting at frame 500 slides a selection at rows 10-20 forward for no
	  reason;

	  bookmarks have hand-called fixups (tasBookmarksOnInsert / OnDelete)
	  invoked from five sites, and NOTHING ENFORCES IT;

	  savestate sidecars are never rewritten at all, so an anchored state's
	  frame becomes WRONG rather than merely suspect, and nothing in the tree
	  distinguishes those two conditions.

	All five copies are computing the same fact. So the edit COMPUTES IT ONCE and
	hands it back, and holders of row indices apply it.

	IT IS RETURNED, NOT OPTIONAL. roll_edit's resize transforms return a Resize
	{ edit, remap } rather than taking an out-parameter, because an out-parameter
	is a thing a caller can forget and that forgetting is the fork's bug.

	SPANS, NOT A TABLE. A run of surviving rows shares one delta, so a delete of
	N scattered rows is N+1 spans rather than a map with an entry per frame - and
	an 11,520-frame movie is a normal size here. A row covered by no span DOES
	NOT EXIST any more, which is the answer a deletion has to be able to give.
*/
namespace roll
{

class Remap
{
public:
	//! Rows [from, from+count) all moved by `delta`.
	struct Span { u32 from; u32 count; s64 delta; };

	//! Nothing moved. What an in-place edit hands back.
	static Remap identity();

	//! `count` rows inserted BEFORE `at`. Rows below `at` DO NOT MOVE - which is
	//! the half the fork gets wrong at two of its five sites.
	static Remap inserted(u32 at, u32 count, u32 movieRows);

	//! `rows` deleted, survivors pulled up by however many went from below them.
	static Remap deleted(const std::set<u32>& rows, u32 movieRows);

	bool isIdentity() const { return identity_; }
	size_t spans() const    { return spans_.size(); }

	//! Where a row went. FALSE means it no longer exists - not "unchanged".
	bool at(u32 row, u32& out) const;

	/*
		WHERE A ROW'S POSITION IS NOW, whether or not the row survived.

		For a surviving row this is at(). For a DELETED one it is the index its
		successor now occupies - the place the tail closed up to - which is the
		count of rows still below it.

		Needed because a savestate anchored on a deleted frame still has to be
		drawn SOMEWHERE, and the two wrong answers are worse than this one:
		leaving the old number points the marker at a row now holding different
		content, and zeroing it means "no anchor at all" (a value already
		overloaded, docs/STATES-LIFT.md §4.3) which makes the state vanish.

		It does NOT claim the state is valid. The timeline guard flags it stale
		on its own, because a resize logs an event at or below this row.
	*/
	u32 collapsed(u32 row) const;

	//! A set of rows, remapped in place: deleted rows drop out, survivors move.
	void applyTo(std::set<u32>& rows) const;

private:
	std::vector<Span> spans_;	// sorted by `from`, non-overlapping
	bool identity_ = false;
};

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void remapSelfTest();

}	// namespace roll
