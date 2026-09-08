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

	//! Called when the panel is open, inside whichever stream it declared.
	void (*draw)();

	u8 stream;

	//! EXPLICIT, with no default. A registry that persisted everything would
	//! quietly reverse a decision somebody made deliberately - a transient
	//! tool that reopens itself every launch is a bug report.
	bool persist;
};

//! Register a panel. Call before the first frame; the registry does not own
//! the descriptor's strings or its bool, so both must outlive the process.
void add(const Panel& p);

const std::vector<Panel>& all();

//! Look one up by its stable id, or nullptr. For the one ordering exception
//! that has to name a panel (see gui.cpp's Game panel) - so that exception is
//! a lookup that FAILS LOUDLY if the id ever changes, rather than an index
//! that silently points at whatever moved into its place.
const Panel *find(const char *id);

//! THE DRAW LOOP. Calls every open panel whose stream mask includes `s`.
//! Panels that declared the other stream are skipped, not drawn twice.
void drawStream(Stream s);

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
