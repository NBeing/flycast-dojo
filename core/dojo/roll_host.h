#pragma once
#include "types.h"
#include "roll_remap.h"
#include <map>
#include <vector>

/*
	EVERYTHING THE PIANO ROLL NEEDS FROM THE MACHINE UNDER IT.

	`[MEASURED 2026-09-09]` this is the whole of it. Lifting David's
	show_piano_roll() (3,832 lines) into its own translation unit produced 161
	real dependencies; exactly FOUR reach outside the roll into the emulator, and
	all four ask the same kind of question - which saved states still match the
	timeline, and which frame each one sits on. The rest of the 161 are the
	roll's own edit tools, selection, bookmarks, sequence library and chrome.

	So the roll is not entangled with the emulator. It is entangled with ITSELF,
	which is a different and much better problem.

	NO EMULATOR NAMES APPEAR HERE. The functions this replaces were
	gui_slot_stale / gui_stale_blink / gui_stale_blink_deleted / gui_state_frames
	- named for the file they lived in rather than the question they answer. A
	roll that calls gui_* cannot be given a different machine, and cannot be
	tested without one.

	A "slot" is just an index. Whether it is a savestate file, a snapshot in
	memory or a row in someone else's database is the host's business.
*/
namespace roll
{

struct Host
{
	virtual ~Host() = default;

	// Does this slot still belong to the timeline currently being edited?
	// A re-record past the frame a state was saved on strands it: the state is
	// still a valid machine, but it is no longer a point on THIS movie.
	virtual bool slotStale(int slot) const = 0;

	// The frame a slot's state is anchored on; 0 when the slot holds nothing.
	virtual u32 slotFrame(int slot) const = 0;

	// frame -> the slots anchored on it. One call because the roll draws a
	// whole visible range at once and a per-row query would be O(rows) scans.
	virtual void framesToSlots(std::map<u32, std::vector<int>>& out) const = 0;

	// Presentation of a just-happened staleness event, so the roll can draw the
	// same notice the rest of the UI does: -1 = not in a notice window, else the
	// blink phase; and whether the event deleted states or merely stranded them.
	virtual int  staleNoticePhase() const { return -1; }
	virtual bool staleNoticeWasDeletion() const { return false; }

	/*
		THE MOVIE RENUMBERED - follow it.

		A slot's anchored frame is a row index like any other, so a resize makes
		it WRONG rather than merely suspect, and nothing distinguishes those two
		conditions from outside. `[MEASURED 2026-09-09]` docs/STATES-LIFT.md §4.4:
		in the fork, sidecars are never rewritten at all.

		Default is a no-op, because a host that anchors nothing has nothing to
		do - and because this must not become a function every host has to
		remember to implement correctly.
	*/
	virtual void rowsRemapped(const Remap& m) { (void)m; }
};

// The host in force. Null until one is installed, which is itself the useful
// state: a roll with no host has nothing to draw and should say so rather than
// invent zeroes.
Host *host();
void setHost(Host *h);

// Install the one this emulator provides (core/dojo/roll_slots.cpp). Separate
// from setHost so a test can still substitute a fake, and so the production
// host is a thing something CALLS rather than a static that hopes to be linked.
void installHost();

// `dojo:RollAnchorProbe` - the integration check for rowsRemapped(). Runs once,
// against the loaded movie and the REAL sidecars: resize, read the file back,
// undo, read it back again. A self-test cannot reach this - the whole question
// is whether bytes on disk moved.
void anchorProbe();

}	// namespace roll
