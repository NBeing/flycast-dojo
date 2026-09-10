/*
	Copyright 2019 flyinghead

	This file is part of reicast.

    reicast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    reicast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with reicast.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include <vector>
#include <utility>
#include "types.h"
#include "dojo/kenney_icon_font_extended.h"
#include "dojo/IconsFontAwesome6.h"
#include "dojo/font_awesome_6_compressed.h"

#include <string>

void gui_init();
void gui_initFonts();
void gui_open_settings();
void gui_display_ui();
// Frame ranges the user has LOCKED against edits in the piano roll.
//
// STUB on this branch. The re-record pipeline (dojo.cpp) consults this before
// applying an edit, but the piano roll that populates it lives in the TAS
// fork's dojo_gui.cpp, which is not ported. Returning an empty list means
// "nothing is locked", which is the correct answer when there is no piano roll
// to lock anything - not a fudge. Replace when the editor lands.
void gui_locked_ranges(std::vector<std::pair<u32, u32>>& out);

void gui_display_notification(const char *msg, int duration);
void gui_display_osd();
void gui_display_profiler();
void gui_open_onboarding();
void gui_term();
void gui_cancel_load();
void gui_refresh_files();
void gui_cheats();
void gui_keyboard_input(u16 wc);
void gui_keyboard_inputUTF8(const std::string& s);
void gui_keyboard_key(u8 keyCode, bool pressed);
bool gui_keyboard_captured();
//! True while a text field has the caret, so emulator hotkeys can stand aside.
bool gui_typing_text();
bool gui_mouse_captured();
void gui_set_mouse_position(int x, int y);
// 0: left, 1: right, 2: middle/wheel, 3: button 4
void gui_set_mouse_button(int button, bool pressed);
void gui_set_mouse_wheel(float delta);
void gui_set_insets(int left, int right, int top, int bottom);
void gui_stop_game(const std::string& message = "");
void gui_start_game(const std::string& path);
void gui_error(const std::string& what);
void gui_setOnScreenKeyboardCallback(void (*callback)(bool show));
void gui_save();
void gui_loadState();
void gui_saveState();

void gui_open_pause();
void gui_open_step();

enum class GuiState {
	Closed,
	Commands,
	Settings,
	Main,
	Onboarding,
	VJoyEdit,
	VJoyEditCommands,
	SelectDisk,
	Loading,
	NetworkStart,
	Cheats,
	NetplayConnect,
	Disconnected,
	ReplayEnd,
	ButtonCheck,
	TestGame,
	QuickMap,
	QuickPlayerSelect,
	QuickSelectPlatform,
	Replays,
	DownloadState,
	DelaySelect,
	Paused,
	StreamWait,
};
extern GuiState gui_state;

void gui_setState(GuiState newState);

static inline bool gui_is_open()
{
	return gui_state != GuiState::Closed && gui_state != GuiState::VJoyEdit;
}
/*
	IS THE GAME ON SCREEN, WITH NO MODAL UI OWNING THE KEYBOARD?

	`!gui_is_open()` is NOT this, and the difference is the whole point:
	gui_is_open() is true for every state but Closed, so it is also true while
	PAUSED - and paused is exactly when a TAS user edits. A piano-roll hotkey
	guarded on !gui_is_open() cannot be pressed at the only moment it is wanted.

	`[MEASURED 2026-09-10]` scripts/hotkeytest.sh found this by pressing the key
	and reading GuiState out of the trace, which is also how it turned up that
	an EXHAUSTED replay sits in GuiState::ReplayEnd - a third state where these
	must stay quiet, because seeking a movie that has ended is meaningless.

	Deliberately spelled as the two states it allows rather than as a list of
	the fifteen it does not: a state added later is refused by default, which is
	the safe direction for something that takes keys away from a menu.
*/
static inline bool gui_is_closed_or_paused()
{
	return gui_state == GuiState::Closed || gui_state == GuiState::Paused;
}
static inline bool gui_is_content_browser()
{
	return gui_state == GuiState::Main;
}

extern std::string error_msg;
extern void error_popup();
