#pragma once
#include "gamepad.h"

/*
	THE TAS ACTIONS, AS DATA - one row instead of three.

	WHAT PROBLEM THIS SOLVES. `docs/HOTKEYS.md`: a hotkey in this tree is five
	declarations in four files - the enum id, a row in mapping.cpp so it
	PERSISTS, a row in each of gui.cpp's two button tables so it is BINDABLE,
	and a case in gamepad_device.cpp so it DOES something. Each file is
	self-consistent and complete-looking alone, which is why an action with four
	of the five is invisible: `[MEASURED 2026-09-10]` six existed
	(EMU_BTN_RECORD_3/4/5, PLAY_3/4/5), bindable in the settings window, doing
	nothing, forgotten on restart.

	scripts/hotkeyaudit.py catches that AFTER it is written. This removes three
	of the five places, so most of it cannot be written.

	WHY NOW, AND NOT BEFORE. The audit was enough while the set was frozen; it
	is not enough while the set is GROWING. The TAS fork's hotkey list is a
	specification of a TAS workflow by someone expert at it, and the actions we
	have not ported yet are features we have not built yet - so this table is
	about to gain rows, repeatedly, and five edits per row is a tax paid every
	time rather than once.

	THREE FIELDS, BECAUSE THREE THINGS READ THEM. No `run` function pointer and
	no guard enum: dispatch is still the switch in gamepad_device.cpp (see
	below), so a field for it would be a field nothing reads - which is the
	exact defect scripts/configaudit.py was written to find in emu.cfg, and it
	would be odd to add one here in the same week.
*/
namespace hotkeys
{

struct Action
{
	DreamcastKey id;
	//! Persistence, in mapping.cpp's "emulator" section. NAMED FOR WHAT IT
	//! DOES, never for a key: an option called btn_f11 is a lie the first time
	//! anyone rebinds it, and this string is what emu.cfg is keyed on.
	const char *cfg;
	//! Menu text, in BOTH of gui.cpp's button tables.
	//!
	//! ONE LABEL, NOT TWO, and that is a fact rather than a simplification.
	//! `[MEASURED 2026-09-10]` the Dreamcast and arcade tables differ ONLY for
	//! button combos - "X+A" against "1+4" - because a combo names guest
	//! buttons and a layout decides what those are called. An emulator action
	//! names itself, so its row is identical in both.
	const char *label;
};

/*
	DELIBERATELY NOT HERE: the button combos, and dispatch.

	THE COMBOS (23 of them, every one `comboAssign(port, pressed, {...})`) are
	not emulator actions at all - they synthesize GUEST INPUT, they take a port,
	and they act on both edges. They are also the only rows whose two labels
	differ. Folding them in would need a second label, a port, and a button
	list that no other row has, which is three fields carried by 23 rows and
	dead on all the rest.

	DISPATCH stays a switch. `[MEASURED 2026-09-10]` its 52 cases are two
	regular families plus a dozen singletons - 23 combos, 17 training actions
	all shaped `pressed && !gui_is_open() && trainingEnabled()` - so it is not
	the tangle its size suggests, and it could be moved. It is not moved yet
	because only 6 of those 52 actions have any test proving they still work
	after a change (scripts/hotkeytest.sh), and rewriting input dispatch for
	training and netplay bindings on that evidence is a bad trade. THE EXIT
	CONDITION IS COVERAGE, not taste: when the training family can be driven
	end to end the way the TAS actions now are, this becomes a safe change.
*/

//! Every TAS action, in the order the settings window shows them.
const Action *all();
int count();

}	// namespace hotkeys
