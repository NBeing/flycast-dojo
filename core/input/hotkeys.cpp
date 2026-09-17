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
	{ EMU_BTN_HOTKEY_HELP,          "btn_hotkey_help",          "Hotkey List"             },
	/*
		THE REST OF THE WINDOWS `[2026-09-17]`. One toggle per studio panel, so the
		Surface Tour can rebind and open every window with a key - and so a user
		can. Unbound by default, like the six above, for the same reason.
	*/
	{ EMU_BTN_PANEL_INPUTVIZ,       "btn_panel_inputviz",       "Input Viz Window"        },
	{ EMU_BTN_PANEL_SENDER,         "btn_panel_sender",         "Input Sender Window"     },
	{ EMU_BTN_PANEL_CAPTURES,       "btn_panel_captures",       "Captures Window"         },
	{ EMU_BTN_PANEL_TIMELINE,       "btn_panel_timeline",       "Timeline Window"         },
	{ EMU_BTN_PANEL_FST,            "btn_panel_fst",            "Frame Skip Test Window"  },
	{ EMU_BTN_PANEL_UITEXT,         "btn_panel_uitext",         "UI Text Window"          },
	{ EMU_BTN_PANEL_NOTEPAD,        "btn_panel_notepad",        "Notepad Window"          },
	{ EMU_BTN_PANEL_TESTLAB,        "btn_panel_testlab",        "Test Lab Window"         },
	{ EMU_BTN_PANEL_BRANCHES,       "btn_panel_branches",       "Branches Window"         },
	{ EMU_BTN_PANEL_SNIPPETS,       "btn_panel_snippets",       "Snippets Window"         },
	{ EMU_BTN_PANEL_MACROS,         "btn_panel_macros",         "Macros Window"           },
};

const Action *all()   { return ACTIONS; }
int           count() { return (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0])); }

// The window-toggle family: the panel id it opens -> the action. Registered ids
// are the ones in core/rend/panel.h's registry (see each registerXPanel).
//
// PANEL FIRST, deliberately. scripts/hotkeyaudit.py reads every `{ EMU_BTN_X, "` in
// this file as a REGISTRY row and demands a cfg name and a label of it; a table that
// led with the id would be reported as fourteen half-wired actions.
struct PanelToggle { const char *panel; DreamcastKey id; };
static const PanelToggle PANELS[] = {
	{ "pianoroll",     EMU_BTN_PIANO_ROLL     },
	{ "states",        EMU_BTN_SLOT_PICKER    },
	{ "hotkeys",       EMU_BTN_HOTKEY_HELP    },
	{ "inputviz",      EMU_BTN_PANEL_INPUTVIZ },
	{ "sender",        EMU_BTN_PANEL_SENDER   },
	{ "captures",      EMU_BTN_PANEL_CAPTURES },
	{ "timeline",      EMU_BTN_PANEL_TIMELINE },
	{ "frameskiptest", EMU_BTN_PANEL_FST      },
	{ "uitext",        EMU_BTN_PANEL_UITEXT   },
	{ "notepad",       EMU_BTN_PANEL_NOTEPAD  },
	{ "testlab",       EMU_BTN_PANEL_TESTLAB  },
	{ "branches",      EMU_BTN_PANEL_BRANCHES },
	{ "snippets",      EMU_BTN_PANEL_SNIPPETS },
	{ "macros",        EMU_BTN_PANEL_MACROS   },
};

const char *panelFor(DreamcastKey id)
{
	for (const PanelToggle& p : PANELS)
		if (p.id == id)
			return p.panel;
	return nullptr;
}

}	// namespace hotkeys
