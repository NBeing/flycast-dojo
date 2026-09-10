/*
	One descriptor, one array, three loops.

	A PANEL IS AN ENTRY IN THE REGISTRY AND NOTHING ELSE. The View menu, the
	draw dispatch and the open-state persistence are all loops over the same
	array, so a panel cannot be half-registered: it cannot appear in a menu it
	is never drawn from, or be drawn in a stream it was not declared for, or
	persist under a key nobody reads.

	WHY, MEASURED RATHER THAN ASSERTED. `docs/MODULARIZATION.md` S1 counted what
	"a studio tool" costs today in the fork we are porting from: one 14-line
	Windows menu using FOUR different mechanisms for "is it open" (a cfg key, a
	file-static bool, a member of the Dojo god-object, and a cross-file accessor
	pair); a ten-branch strcmp chain over ~730 lines to dispatch focus; and the
	draw list written out FOUR times, the four of them not agreeing. Two shipped
	defects there - a double-rendered Timeline, and overlays vanishing along with
	the piano roll - are both "wrong stream set", which is exactly one field
	below. nbneo-rr measured the same list a month earlier and reached the same
	shape (docs/panel-scaffolding.md there); this is that shape, plus the one
	field their fork does not need.

	THE ORDER MATTERS. This lands BEFORE the first ported panel, because it is
	the one seam that cannot be retrofitted: retrofitting means editing 161
	file-static functions inside a 21,877-line translation unit. Every other
	seam in that survey can wait; this one closes.
*/
#pragma once
#include "types.h"
#include <vector>

namespace panels
{

/*
	WHICH IMGUI FRAME STREAM a panel is drawn in, and this is the field the
	other forks do not have.

	This fork runs TWO independent ImGui frame streams, each with its own
	NewFrame: `gui_display_ui` (menus, and the Paused state) and
	`gui_display_osd` (gameplay, called by the RENDERERS from their present).
	Submitting a window in the wrong one draws it twice or not at all, and
	because asserts are compiled out of release builds the failure is silent -
	it surfaces later as a rendering bug somewhere else.

	A bitmask rather than an enum of three, so `Both` is the OR of the two it
	is made of instead of a third thing that has to be kept in step with them.
*/
enum Stream : u8
{
	Osd  = 1 << 0,		//!< gameplay
	Menu = 1 << 1,		//!< menus and Paused
	Both = Osd | Menu,
};

struct Panel
{
	//! STABLE, and used on disk. Never the label: a settings file keyed on
	//! prose silently forgets a panel the day someone improves the wording.
	const char *id;

	//! Menu text. Expected to be reworded; nothing depends on its value.
	const char *label;

	//! THE one owner of "is it open". The panel declares where its bool lives
	//! and the registry points at it - it does not keep a second copy, which
	//! is the shape that produced four mechanisms for one fact.
	bool *open;

	//! FILLS THE BODY. It does NOT call Begin or End - the registry owns the
	//! window, and that is not a convenience.
	//!
	//! A panel that opens its own window can mismatch the pair, and in this
	//! build ImGui's asserts are compiled out, so an unmatched End does not
	//! fail - it silently corrupts the frame and surfaces somewhere else
	//! entirely. That exact bug shipped in this tree's Lua ui layer and was
	//! found by someone else's conformance suite, not by us. Host-owned
	//! Begin/End makes it unrepresentable rather than discouraged.
	//!
	//! The host also needs `open` for the window's own close button, so a panel
	//! that opened its own window would have to pass the flag out and back.
	//! And flags, docking and focus are furniture: a panel calling Begin is
	//! choosing them.
	void (*draw)();

	u8 stream;

	//! EXPLICIT, with no default. A registry that persisted everything would
	//! quietly reverse a decision somebody made deliberately - a transient
	//! tool that reopens itself every launch is a bug report.
	bool persist;
};

/*
	ONE PANEL WITH TWO PRESENTATIONS IS STILL ONE PANEL.

	Four of the eleven panels arriving from the fork are dual-mode: a single
	draw function with two ImGui::Begin sites, chosen inside itself by
	`tasStudioMode()` - a dockable window with the studio shell up, and a
	pinned NoDecoration|NoInputs overlay with it down. The savestate HUD, the
	input visualizer, the hotkey cheat sheet and the slot picker are all this.

	DO NOT REGISTER THE OVERLAY ARM AS A SECOND PANEL. One feature would get two
	ids, two menu rows and two persistence keys, and the user would be able to
	close half of itself. The mode belongs inside draw(), where it already is;
	the registry holds the feature, not each of its costumes.

	NO `shortcut` FIELD, and that is a deliberate departure from nbneo's
	descriptor. Their panels carry a literal key; ours are rebindable actions
	resolved through the hotkey map, and only two of the eleven have a default
	key at all. A field that is empty for nine entries and lies for the other
	two is worse than a lookup.
*/

//! Register a panel. Call before the first frame; the registry does not own
//! the descriptor's strings or its bool, so both must outlive the process.
void add(const Panel& p);

const std::vector<Panel>& all();

//! Look one up by its stable id, or nullptr. For the one ordering exception
//! that has to name a panel (see gui.cpp's Game panel) - so that exception is
//! a lookup that FAILS LOUDLY if the id ever changes, rather than an index
//! that silently points at whatever moved into its place.
const Panel *find(const char *id);

//! Open a panel from outside itself, by id.
//!
//! A FUNCTION RATHER THAN A FIELD, and the distinction is forced by a real
//! case: David's Test Lab sets its own open flag from ABOVE its early return,
//! so a session started while the window is closed would never be able to show
//! it - `drawStream`'s `if (!*open) continue` runs first and the code that
//! would open it never executes. Whatever starts a lab session calls this.
//!
//! DELIBERATELY NOT a `tick()` field for panels that want work while closed.
//! Frame Skip Test runs a whole variant sweep with its window shut, and giving
//! the registry a per-frame hook for that would make it a scheduler. A panel
//! that needs to run while closed is not a panel with an extra field; it is a
//! feature with a panel attached, and the feature keeps its own tick.
void open(const char *id);

/*
	Flip a panel, and answer where it landed.

	SEPARATE FROM open(), because they are wanted by different callers for
	different reasons and collapsing them loses one. A feature that starts a
	session needs the window SHOWN (open); a hotkey needs it flipped. A single
	"open(id, bool)" would push the caller into tracking a state the registry
	already owns, which is the shape this whole header exists to refuse.

	Returns the new state so a caller can report it without asking again, and
	false for a panel that is not registered - a hotkey bound to a panel that
	failed to register is indistinguishable from one that is not bound at all.
*/
bool toggle(const char *id);

//! THE DRAW LOOP. Opens a window for every open panel whose stream mask
//! includes `s`, calls its body, and closes it. Panels that declared the other
//! stream are skipped, not drawn twice.
//!
//! A BODY THAT THROWS DOES NOT TAKE THE WINDOW WITH IT. The error is caught and
//! printed IN THE PANEL, in red, rather than counted somewhere nobody looks - a
//! fault swallowed into a counter is indistinguishable from a tool that drew
//! almost nothing, which is a lesson this package learned the expensive way.
//! Draws every OPEN panel declared for this stream, except one.
//!
//! `skipId` exists for the game panel, which is a registry MEMBER (the View
//! menu and its open flag come from there) but is submitted by hand at a known
//! point because it consumes the dockspace's central node. Without the skip it
//! is drawn TWICE per frame - `[MEASURED 2026-09-09]` two Begin("Game") calls
//! left the picture letterboxed into a shrunken central node, 604x453 inside a
//! 640x480 window, which scripts/tests/tour.lua caught as "undocked, the
//! picture spans the window on its long axis".
//!
//! Named rather than flagged: an exception one call site states out loud is
//! findable, a seventh field on every descriptor is not.
void drawStream(Stream s, const char *skipId = nullptr);

//! The SELECTION half of drawStream, without any ImGui. Exists so the choice of
//! which panels a stream draws can be exercised outside a frame - the self-test
//! runs at startup, before any ImGui frame exists, and testing a copy of this
//! logic would only prove the copy.
void visitStream(Stream s, void (*fn)(const Panel&));

//! Open state, for the panels that asked to persist it. Keyed on `id` under
//! the `dojo` section as `Panel.<id>`, so the key is legible in emu.cfg and a
//! renamed label cannot orphan it.
void loadOpenState();
void saveOpenState();

//! Exercise every loop above and print one line per claim. Gated on
//! `dojo:PanelSelfTest`; see the comment on its definition for why it uses the
//! real registry rather than a copy.
void selfTest();

}	// namespace panels
