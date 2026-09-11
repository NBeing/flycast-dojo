#include "hotkeys.h"

namespace hotkeys
{

/*
	THE TAS ACTIONS. Adding one here gives it persistence and a row in both
	button tables; it still needs its `case` in gamepad_device.cpp, and
	scripts/hotkeyaudit.py fails until that exists.

	`[PORTED 2026-09-10]` five of the TAS fork's eighteen. The cut is not
	arbitrary: an id here must have something to dispatch to TODAY. The fork's
	remaining thirteen drive an input visualizer, a frame-skip test and an AVI
	recorder - features this tree does not have - and a bindable key that
	silently does nothing is the defect the audit was written to catch, six of
	which it found on its first run. They arrive with their features, not before.

	NO DEFAULT KEYS. `[SOURCE]` the fork defaults these to F2/F4/F5/F8/F9, and
	in this tree every one of those is already a training binding
	(EMU_BTN_RECORD_1, PLAY, PLAY_1, SAVESTATE, LOADSTATE). That fork repurposed
	flycast-dojo's training hotkeys for TAS; this one keeps training, so
	adopting its defaults would silently take five keys off every existing user.
	They ship bindable and unbound, and both panels are reachable from the View
	menu regardless - the hotkey is a convenience here, not the only door.
*/
static const Action ACTIONS[] = {
	{ EMU_BTN_PIANO_ROLL,           "btn_piano_roll",           "Piano Roll"              },
	{ EMU_BTN_SLOT_PICKER,          "btn_slot_picker",          "States Window"           },
	{ EMU_BTN_SAVESTATE_SLOT_NEXT,  "btn_savestate_slot_next",  "Next Savestate Slot"     },
	{ EMU_BTN_SAVESTATE_SLOT_PREV,  "btn_savestate_slot_prev",  "Previous Savestate Slot" },
	{ EMU_BTN_GEN_ARCHIVE,          "btn_gen_archive",          "Archive Generation"      },
};

const Action *all()   { return ACTIONS; }
int           count() { return (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0])); }

}	// namespace hotkeys
