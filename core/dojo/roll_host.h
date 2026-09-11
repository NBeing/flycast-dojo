#pragma once
#include "types.h"
#include "roll_remap.h"
#include <map>
#include <string>
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

/*
	ONE SLOT, AS A TOOL SEES IT.

	`[MEASURED 2026-09-09]` docs/STATES-LIFT.md - the States window asks 17
	questions of its host and the roll's four cover about one and a half. This is
	the READ half of the rest, kept as one struct rather than five accessors
	because a wall of a hundred slots asks all of it at once and a per-field call
	would be five scans.

	NO EMULATOR NAMES AND NO FILE PATHS. A slot is an index; whether it is a file,
	a snapshot in memory or a row in someone else's database is the host's
	business, and a path leaking into a tool is what made the fork's `#` column
	necessary - its folder numbers restart per kind, so the number on screen and
	the number in the name disagreed.

	`haveFrame` EXISTS BECAUSE ZERO IS OVERLOADED. Everywhere else in this tree a
	recorded frame of 0 means BOTH "anchored at frame 0" and "no anchor at all"
	(docs/STATES-LIFT.md §4.3), and a power-on BASE state is exactly the case that
	gets wrong. A tool asking this struct can tell them apart.
*/
struct SlotView
{
	bool        exists = false;
	bool        haveFrame = false;	//!< NOT the same as frame != 0
	u32         frame = 0;			//!< movie index this state is anchored on
	bool        stale = false;		//!< no longer a point on THIS timeline
	bool        judged = false;		//!< false = too old to judge, NOT "clean"
	u64         bytes = 0;
	s64         mtime = 0;
	std::string label;
};

/*
	ONE SNAPSHOT of a whole slot set - a "generation" in the fork's words.

	`[MEASURED 2026-09-09]` docs/STATES-LIFT.md: the pane that shows these has
	131 real dependencies and ZERO game-specific symbols, so the port's whole
	cost is this interface.

	THE ID IS OPAQUE AND NEVER A FOLDER NAME ON SCREEN. The fork had to invent a
	`#` column precisely because its folder numbers restart per kind - "8 or 6
	backups?" says its own comment, because the last row read 06 while the count
	was 8. A tool that shows the stored number shows a different number from the
	one it counted.
*/
struct SnapshotView
{
	std::string id;				//!< opaque handle; pass it back, never show it
	std::string kindLabel;		//!< what sort of snapshot, in the host's words
	int         ordinal = 0;	//!< the host's own numbering WITHIN that kind
	std::string createdLocal;
	int         files = 0;
	u64         bytes = 0;
	bool        haveFrame = false;
	u32         atFrame = 0;
	u32         movieFrames = 0;
	u32         rerecords = 0;
	std::vector<int> slots;
	bool        onDisk = true;		//!< false = recorded but the files are gone
	bool        synthesized = false;//!< the record was rebuilt from the folder
	std::string tags, notes;
};

struct Host
{
	virtual ~Host() = default;

	//! How many slots this host addresses. A wall draws this many cells.
	virtual int slotCount() const { return 0; }

	//! Everything a tool shows about one slot, in one call. False = out of range.
	virtual bool slotView(int slot, SlotView& out) const { (void)slot; (void)out; return false; }

	/*
		Name a slot, or clear its name with an empty string.

		THE FIRST WRITE IN THIS INTERFACE, and the default REFUSES rather than
		silently doing nothing - a host that cannot name slots should make a
		tool's rename fail visibly, not appear to work until the next refresh.

		Naming is the safest write there is here: it touches a sidecar and never
		the state, so the worst outcome is a lost label.
	*/
	virtual bool setSlotLabel(int slot, const std::string& label)
	{ (void)slot; (void)label; return false; }

	/*
		Delete a slot's state and everything that describes it.

		THE ONLY IRREVERSIBLE OPERATION IN THIS INTERFACE. It refuses by default
		for the same reason setSlotLabel does, and more so: a host that cannot
		delete must not let a tool believe it did.

		A CONFIRMATION IS THE CALLER'S JOB and not this function's. A host that
		asked would be a host that blocks, and this one is called from a draw.
	*/
	virtual bool deleteSlot(int slot) { (void)slot; return false; }

	// ---- SNAPSHOTS of the whole slot set -----------------------------------

	//! How many snapshots the bound set has.
	virtual int snapshotCount() const { return 0; }
	virtual bool snapshotView(int i, SnapshotView& out) const
	{ (void)i; (void)out; return false; }

	/*
		A number that CHANGES when the snapshot library does, and never
		otherwise.

		The pane's only refresh trigger. The alternative - rescanning per draw -
		is a directory walk plus a JSON parse per frame, and the fork's comment
		says its equivalent pane asked ~200 times a frame before it was
		memoised.
	*/
	virtual u32 snapshotRevision() const { return 0; }

	//! Name or annotate a snapshot. Refuses by default, like every other write
	//! in this interface.
	virtual bool setSnapshotTags(const std::string& id, const std::string& tagsCsv)
	{ (void)id; (void)tagsCsv; return false; }
	virtual bool setSnapshotNotes(const std::string& id, const std::string& notes)
	{ (void)id; (void)notes; return false; }

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

// Registers the States panel with the registry. Idempotent.
void registerStatesPanel();

//! The hotkey cheat sheet: every TAS action and its LIVE binding. Idempotent.
void registerHotkeyPanel();

}	// namespace roll
