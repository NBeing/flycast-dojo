#pragma once
#include "roll_remap.h"
#include <map>
#include <string>

/*
	BOOKMARKS: frames the user named so they can find them again.

	`[MEASURED 2026-09-09]` docs/PIANO-ROLL-LIFT.md counts 7 bookmark symbols in
	the fork's roll, and docs/ROLL-EDIT-MODEL.md §3 records what is wrong with
	how they are maintained there: `tasBookmarksOnInsert` / `tasBookmarksOnDelete`
	are called BY HAND from five structural tools, and NOTHING ENFORCES IT. Miss
	one and every bookmark past the edit silently points at the wrong frame.

	Here they are a Remap customer by construction. A structural edit returns
	where every row went; this applies it. There is no per-tool fixup to forget.

	A DELETED BOOKMARK IS DROPPED, and that is a deliberate difference from the
	savestate anchors, which COLLAPSE to where the tail closed up. The two are
	not the same kind of thing: a savestate still exists and has to be drawn
	somewhere, while a bookmark is only a pointer at a frame - when the frame
	goes, so does it. Pointing it at whatever moved into the gap would silently
	rename the user's landmark.
*/
namespace roll
{

class Marks
{
public:
	bool has(u32 frame) const               { return at_.count(frame) != 0; }
	size_t count() const                    { return at_.size(); }
	const std::map<u32, std::string>& all() const { return at_; }

	//! The label, or nullptr when the frame is not marked. An empty string is a
	//! marked frame with no name, which is a different thing from no mark.
	const char *labelAt(u32 frame) const;

	void set(u32 frame, const std::string& label);
	void erase(u32 frame)                   { at_.erase(frame); }
	void toggle(u32 frame);
	void clear()                            { at_.clear(); }

	//! The nearest mark strictly after / before `from`. False when there is none.
	bool next(u32 from, u32& out) const;
	bool prev(u32 from, u32& out) const;

	//! Follow a structural edit. The whole reason this module is not a set of
	//! hand-called fixups.
	void remap(const Remap& m);

	std::string serialise() const;
	void load(const std::string& blob);

private:
	std::map<u32, std::string> at_;
};

Marks& marks();

//! Register with roll_meta so an undo restores them alongside the frames.
void marksInstall();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void marksSelfTest();

}	// namespace roll
