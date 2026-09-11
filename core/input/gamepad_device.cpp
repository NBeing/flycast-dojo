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

#include "gamepad_device.h"
#include "hotkeys.h"
#include "dojo/session.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "oslib/oslib.h"
#include "rend/gui.h"
#include "rend/panel.h"
#include "emulator.h"
#include "hw/maple/maple_devs.h"
#include "mouse.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "dojo/dojo.h"

#define MAPLE_PORT_CFG_PREFIX "maple_"

// Gamepads
u32 kcode[4] = { ~0u, ~0u, ~0u, ~0u };
s16 joyx[4];
s16 joyy[4];
s16 joyrx[4];
s16 joyry[4];
s16 joy3x[4];
s16 joy3y[4];
u16 rt[4];
u16 lt[4];
u16 lt2[4];
u16 rt2[4];
// Keyboards
u8 kb_shift[MAPLE_PORTS];	// shift keys pressed (bitmask)
u8 kb_key[MAPLE_PORTS][6];	// normal keys pressed

std::vector<std::shared_ptr<GamepadDevice>> GamepadDevice::_gamepads;
std::mutex GamepadDevice::_gamepads_mutex;

#ifdef TEST_AUTOMATION
#include "hw/sh4/sh4_sched.h"
#include <cstdio>
static FILE *record_input;
#endif

void GamepadDevice::comboPress(int port, std::vector<DreamcastKey> key_combo)
{
	u32 combo = 0;
	for (auto key : key_combo)
	{
		dojo.button_check_pressed[port].insert((int)key);
		if (key == DC_AXIS_LT)
			lt[port] = 255;
		else if (key == DC_AXIS_RT)
			rt[port] = 255;
		else
			combo |= key;
	}
	kcode[port] &= ~(combo);
}

void GamepadDevice::comboRelease(int port, std::vector<DreamcastKey> key_combo)
{
	u32 combo = 0;
	for (auto key : key_combo)
	{
		dojo.button_check_pressed[port].erase((int)key);
		if (key == DC_AXIS_LT)
			lt[port] = 0;
		else if (key == DC_AXIS_RT)
			rt[port] = 0;
		else
			combo |= key;
	}
	kcode[port] |= combo;
}

void GamepadDevice::comboAssign(int port, bool pressed, std::initializer_list<DreamcastKey> keys)
{
	if (gui_is_open() && gui_state != GuiState::ButtonCheck)
		return;
	std::vector<DreamcastKey> key_combo;
	key_combo.insert(key_combo.end(), keys);
	if (pressed)
		comboPress(port, key_combo);
	else
		comboRelease(port, key_combo);
}

bool GamepadDevice::handleButtonInput(int port, DreamcastKey key, bool pressed)
{
	if (key == EMU_BTN_NONE)
		return false;

	if (key <= DC_BTN_BITMAPPED_LAST)
	{
		if (port >= 0)
		{
			if (pressed)
				kcode[port] &= ~key;
			else
				kcode[port] |= key;
		}
#ifdef TEST_AUTOMATION
		if (record_input != NULL)
			fprintf(record_input, "%ld button %x %04x\n", sh4_sched_now64(), port, kcode[port]);
#endif
	}
	else
	{
		/*
			EVERY EMULATOR ACTION THAT DISPATCHES, under dojo:HotkeyTrace.

			`[MEASURED 2026-09-10]` scripts/hotkeytest.sh needed to tell three
			states apart that are all SILENCE from outside: the key never
			reached the emulator, the key arrived but is bound to nothing, and
			the key arrived and its action declined to run (every case below is
			guarded on `pressed`, and most on !gui_is_open()). Without this the
			harness reports "the binding never reached the dispatch" for all
			three, which is a wrong diagnosis two times out of three.

			Off by default: this is one line per hotkey press, but the combos
			route through here too and a held combo is not rare.
		*/
		if (cfgLoadBool("dojo", "HotkeyTrace", false))
			NOTICE_LOG(INPUT, "HOTKEY: id=0x%x %s guistate=%d open=%s typing=%s", (unsigned)key,
					pressed ? "down" : "up", (int)gui_state,
					gui_is_open() ? "yes" : "no",
					gui_keyboard_captured() ? "yes" : "no");
		switch (key)
		{
		case EMU_BTN_ESCAPE:
			if (pressed)
				dc_exit();
			break;
		case EMU_BTN_MENU:
			if (pressed)
			{
				dojo.current_gamepad = _unique_id;
				gui_open_settings();
			}
			break;
		case EMU_BTN_FFORWARD:
			if (pressed && !gui_is_open())
				settings.input.fastForwardMode = !settings.input.fastForwardMode && !settings.network.online && !settings.naomi.multiboard;
			break;
		case EMU_BTN_LOADSTATE:
			// NOT WHILE TYPING. Deliberately only that - a menu being open is
			// still fine for these four, exactly as before.
			if (pressed && !gui_keyboard_captured())
				gui_loadState();
			break;
		case EMU_BTN_SAVESTATE:
			// NOT WHILE TYPING. Deliberately only that - a menu being open is
			// still fine for these four, exactly as before.
			if (pressed && !gui_keyboard_captured())
				gui_saveState();
			break;

		case EMU_BTN_PAUSE:
			// NOT WHILE TYPING. Deliberately only that - a menu being open is
			// still fine for these four, exactly as before.
			if (pressed && !gui_keyboard_captured())
				gui_open_pause();
			break;
		case EMU_BTN_STEP:
			// NOT WHILE TYPING. Deliberately only that - a menu being open is
			// still fine for these four, exactly as before.
			if (pressed && !gui_keyboard_captured())
				gui_open_step();
			break;

		// training
		case EMU_BTN_SWITCH_PLAYER:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.SwitchPlayer();
			}
			break;
		case EMU_BTN_RECORD:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(0);
			}
			break;
		case EMU_BTN_PLAY:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(0);
			}
			break;
		case EMU_BTN_RECORD_1:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(1);
			}
			break;
		case EMU_BTN_PLAY_1:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(1);
			}
			break;
		case EMU_BTN_RECORD_2:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(2);
			}
			break;
		case EMU_BTN_PLAY_2:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(2);
			}
			break;
		/*
			SLOTS 4, 5 AND 6, which existed everywhere except here.

			`[MEASURED 2026-09-10]` Training::record_slot is `[6]`, and the
			Controller Mapping window has offered "Record Slot 4/5/6" and "Play
			Slot 4/5/6" in both its button tables all along. Binding one did
			nothing and the binding was forgotten on restart: the ids were in
			the enum and in the two UI tables, but in neither mapping.cpp's
			persistence table nor this switch.

			A hotkey needs FIVE files to agree - the enum, mapping.cpp, both
			gui.cpp tables, and this switch - and nothing checked that they did.
			scripts/hotkeyaudit.py does now, which is how these were found.
		*/
		case EMU_BTN_RECORD_3:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(3);
			}
			break;
		case EMU_BTN_PLAY_3:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(3);
			}
			break;
		case EMU_BTN_RECORD_4:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(4);
			}
			break;
		case EMU_BTN_PLAY_4:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(4);
			}
			break;
		case EMU_BTN_RECORD_5:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRecording(5);
			}
			break;
		case EMU_BTN_PLAY_5:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.TogglePlayback(5);
			}
			break;
		/*
			THE TAS ACTIONS.

			`!gui_is_open()` on all of them, matching every other hotkey here:
			a key pressed with the settings window up belongs to that window.

			The two panel toggles go through panels::toggle rather than
			touching a bool, because the registry is the one owner of "is it
			open" - core/rend/panel.h exists because the fork being ported from
			had FOUR mechanisms for that one fact. A panel that failed to
			register logs loudly instead of doing nothing quietly.
		*/
		case EMU_BTN_PIANO_ROLL:
			if (pressed && gui_hotkey_allowed())
				panels::toggle("pianoroll");
			break;
		case EMU_BTN_SLOT_PICKER:
			if (pressed && gui_hotkey_allowed())
				panels::toggle("states");
			break;
		/*
			SLOT CYCLING WRAPS, and it goes through hostfs::clampSavestateSlot
			rather than doing its own arithmetic - that function is the one
			owner of the range, and config::SavestateSlot persists across games
			(oslib.h), so an out-of-range value written here would follow the
			user into a different ROM.
		*/
		case EMU_BTN_SAVESTATE_SLOT_NEXT:
		case EMU_BTN_SAVESTATE_SLOT_PREV:
			if (pressed && gui_hotkey_allowed())
			{
				const int n    = hostfs::MAX_SAVESTATE_SLOTS;
				const int step = (key == EMU_BTN_SAVESTATE_SLOT_NEXT) ? 1 : n - 1;
				const int was  = hostfs::currentSavestateSlot();	// read BEFORE the set, or the trace says "5 -> 5"
				const int slot = (was + step) % n;
				config::SavestateSlot.set(slot);
				// Held in a named string rather than built inside the call.
				// The temporary would in fact live long enough, but a reader
				// has to prove that to themselves every time they pass it.
				const std::string msg = slot == 0 ? std::string("Savestate slot BASE")
						: "Savestate slot " + std::to_string(slot);
				gui_display_notification(msg.c_str(), 1500);
				// The notification is on screen only; this is what a harness
				// outside the process can read.
				NOTICE_LOG(INPUT, "HOTKEY SLOT: %d -> %d", was, slot);
			}
			break;
		case EMU_BTN_GEN_ARCHIVE:
			// Self-guarding: it says so itself when there is no clip open, so
			// there is no second copy of that condition here to drift from it.
			if (pressed && gui_hotkey_allowed())
				dojo.ArchiveGeneration();
			break;

		case EMU_BTN_PLAY_RND:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleRandomPlayback();
			}
			break;
		case EMU_BTN_SELECT_SLOT:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.SelectRecordSlot();
			}
			break;
		case EMU_BTN_PLAY_SLOT:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleSelectedPlayback();
			}
			break;
		case EMU_BTN_RECORD_SLOT:
			if (pressed && !gui_is_open() && session::trainingEnabled())
			{
				dojo.training.ToggleSelectedRecording();
			}
			break;

		// button combinations
		case EMU_CMB_X_Y_A_B:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_Y, DC_BTN_A, DC_BTN_B });
			break;
		case EMU_CMB_X_Y_A:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_Y, DC_BTN_A });
			break;
		case EMU_CMB_X_Y_LT:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_Y, DC_AXIS_LT });
			break;
		case EMU_CMB_A_B_RT:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B, DC_AXIS_RT });
			break;
		case EMU_CMB_X_A:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_A });
			break;
		case EMU_CMB_Y_B:
			comboAssign(port, pressed, { DC_BTN_Y, DC_BTN_B });
			break;
		case EMU_CMB_LT_RT:
			comboAssign(port, pressed, { DC_AXIS_LT, DC_AXIS_RT });
			break;
		case EMU_CMB_1_2_3:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B, DC_BTN_C });
			break;
		case EMU_CMB_4_5:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_Y });
			break;
		case EMU_CMB_4_5_6:
			comboAssign(port, pressed, { DC_BTN_X, DC_BTN_Y, DC_BTN_Z });
			break;
		case EMU_CMB_1_4:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_X });
			break;
		case EMU_CMB_2_5:
			comboAssign(port, pressed, { DC_BTN_B, DC_BTN_Y });
			break;
		case EMU_CMB_3_4:
			comboAssign(port, pressed, { DC_BTN_C, DC_BTN_X });
			break;
		case EMU_CMB_3_6:
			comboAssign(port, pressed, { DC_BTN_C, DC_BTN_Z });
			break;
		case EMU_CMB_1_2:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B });
			break;
		case EMU_CMB_1_3:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_C });
			break;
		case EMU_CMB_2_3:
			comboAssign(port, pressed, { DC_BTN_B, DC_BTN_C });
			break;
		case EMU_CMB_1_2_4:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B, DC_BTN_X });
			break;
		case EMU_CMB_1_2_5:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B, DC_BTN_Y });
			break;
		case EMU_CMB_1_2_3_4:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_B, DC_BTN_C, DC_BTN_X });
			break;
		case EMU_CMB_2_4:
			comboAssign(port, pressed, { DC_BTN_B, DC_BTN_X });
			break;
		case EMU_CMB_1_5:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_Y });
			break;
		case EMU_CMB_A_START:
			comboAssign(port, pressed, { DC_BTN_A, DC_BTN_START });
			break;
		case DC_AXIS_LT:
			if (port >= 0)
				lt[port] = pressed ? 0xffff : 0;
			break;
		case DC_AXIS_RT:
			if (port >= 0)
				rt[port] = pressed ? 0xffff : 0;
			break;
		case DC_AXIS_LT2:
			if (port >= 0)
				lt2[port] = pressed ? 0xffff : 0;
			break;
		case DC_AXIS_RT2:
			if (port >= 0)
				rt2[port] = pressed ? 0xffff : 0;
			break;
		case DC_AXIS_UP:
		case DC_AXIS_DOWN:
			buttonToAnalogInput<DC_AXIS_UP, DIGANA_UP, DIGANA_DOWN>(port, key, pressed, joyy[port]);
			break;
		case DC_AXIS_LEFT:
		case DC_AXIS_RIGHT:
			buttonToAnalogInput<DC_AXIS_LEFT, DIGANA_LEFT, DIGANA_RIGHT>(port, key, pressed, joyx[port]);
			break;
		case DC_AXIS2_UP:
		case DC_AXIS2_DOWN:
			buttonToAnalogInput<DC_AXIS2_UP, DIGANA2_UP, DIGANA2_DOWN>(port, key, pressed, joyry[port]);
			break;
		case DC_AXIS2_LEFT:
		case DC_AXIS2_RIGHT:
			buttonToAnalogInput<DC_AXIS2_LEFT, DIGANA2_LEFT, DIGANA2_RIGHT>(port, key, pressed, joyrx[port]);
			break;
		case DC_AXIS3_UP:
		case DC_AXIS3_DOWN:
			buttonToAnalogInput<DC_AXIS3_UP, DIGANA3_UP, DIGANA3_DOWN>(port, key, pressed, joy3y[port]);
			break;
		case DC_AXIS3_LEFT:
		case DC_AXIS3_RIGHT:
			buttonToAnalogInput<DC_AXIS3_LEFT, DIGANA3_LEFT, DIGANA3_RIGHT>(port, key, pressed, joy3x[port]);
			break;

		default:
			return false;
		}
	}
	DEBUG_LOG(INPUT, "%d: BUTTON %s %d. kcode=%x", port, pressed ? "down" : "up", key, port >= 0 ? kcode[port] : 0);
	if (gui_state == GuiState::ButtonCheck)
	{
		if (pressed && port < 2)
		{
			dojo.button_check_pressed[port].insert((int)key);
		}
		else
		{
			dojo.button_check_pressed[port].erase((int)key);
		}
	}

	return true;
}

bool GamepadDevice::gamepad_btn_input(u32 code, bool pressed)
{
	if (_input_detected != nullptr && _detecting_button
			&& os_GetSeconds() >= _detection_start_time && pressed)
	{
		_input_detected(code, false, false);
		_input_detected = nullptr;
		return true;
	}
	if (!input_mapper || _maple_port > (int)std::size(kcode))
		return false;

	bool rc = false;
	if (_maple_port == 4)
	{
		for (int port = 0; port < 4; port++)
		{
			DreamcastKey key = input_mapper->get_button_id(port, code);
			rc = handleButtonInput(port, key, pressed) || rc;
		}
	}
	else
	{
		DreamcastKey key = input_mapper->get_button_id(0, code);
		rc = handleButtonInput(_maple_port, key, pressed);
	}

	return rc;
}

static DreamcastKey getOppositeAxis(DreamcastKey key)
{
	switch (key)
	{
	case DC_AXIS_RIGHT: return DC_AXIS_LEFT;
	case DC_AXIS_LEFT: return DC_AXIS_RIGHT;
	case DC_AXIS_UP: return DC_AXIS_DOWN;
	case DC_AXIS_DOWN: return DC_AXIS_UP;
	case DC_AXIS2_RIGHT: return DC_AXIS2_LEFT;
	case DC_AXIS2_LEFT: return DC_AXIS2_RIGHT;
	case DC_AXIS2_UP: return DC_AXIS2_DOWN;
	case DC_AXIS2_DOWN: return DC_AXIS2_UP;
	case DC_AXIS3_RIGHT: return DC_AXIS3_LEFT;
	case DC_AXIS3_LEFT: return DC_AXIS3_RIGHT;
	case DC_AXIS3_UP: return DC_AXIS3_DOWN;
	case DC_AXIS3_DOWN: return DC_AXIS3_UP;
	default: return key;
	}
}

//
// value must be >= -32768 and <= 32767 for full axes
// and 0 to 32767 for half axes/triggers
//
bool GamepadDevice::gamepad_axis_input(u32 code, int value)
{
	bool positive = value >= 0;
	if (_input_detected != NULL && _detecting_axis
			&& os_GetSeconds() >= _detection_start_time && std::abs(value) >= 16384)
	{
		_input_detected(code, true, positive);
		_input_detected = nullptr;
		return true;
	}
	if (!input_mapper || _maple_port < 0 || _maple_port > 4)
		return false;

	auto handle_axis = [&](u32 port, DreamcastKey key, int v)
	{
		if (gui_state == GuiState::ButtonCheck)
		{
			if (v > 0 && port < 2)
			{
				dojo.button_check_pressed[port].insert((int)key);
			}
			else
			{
				dojo.button_check_pressed[port].erase((int)key);
			}
		}
		if ((key & DC_BTN_GROUP_MASK) == DC_AXIS_TRIGGERS)	// Triggers
		{
			//printf("T-AXIS %d Mapped to %d -> %d\n", key, value, std::min(std::abs(v) >> 7, 255));
			if (key == DC_AXIS_LT)
				lt[port] = std::min(std::abs(v) << 1, 0xffff);
			else if (key == DC_AXIS_RT)
				rt[port] = std::min(std::abs(v) << 1, 0xffff);
			else if (key == DC_AXIS_LT2)
				lt2[port] = std::min(std::abs(v) << 1, 0xffff);
			else if (key == DC_AXIS_RT2)
				rt2[port] = std::min(std::abs(v) << 1, 0xffff);
			else
				return false;
		}
		else if ((key & DC_BTN_GROUP_MASK) == DC_AXIS_STICKS) // Analog axes
		{
			//printf("AXIS %d Mapped to %d -> %d\n", key, value, v);
			s16 *this_axis;
			int otherAxisValue;
			int axisDirection = -1;
			switch (key)
			{
			case DC_AXIS_RIGHT:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS_LEFT:
				this_axis = &joyx[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS_UP];
				break;

			case DC_AXIS_DOWN:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS_UP:
				this_axis = &joyy[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS_LEFT];
				break;

			case DC_AXIS2_RIGHT:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS2_LEFT:
				this_axis = &joyrx[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS2_UP];
				break;

			case DC_AXIS2_DOWN:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS2_UP:
				this_axis = &joyry[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS2_LEFT];
				break;

			case DC_AXIS3_RIGHT:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS3_LEFT:
				this_axis = &joy3x[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS3_UP];
				break;

			case DC_AXIS3_DOWN:
				axisDirection = 1;
				[[fallthrough]];
			case DC_AXIS3_UP:
				this_axis = &joy3y[port];
				otherAxisValue = lastAxisValue[port][DC_AXIS3_LEFT];
				break;

			default:
				return false;
			}
			int& lastValue = lastAxisValue[port][key];
			int& lastOpValue = lastAxisValue[port][getOppositeAxis(key)];
			if (lastValue != v || lastOpValue != v)
			{
				lastValue = lastOpValue = v;
				// Lightgun with left analog stick
				if (key == DC_AXIS_RIGHT || key == DC_AXIS_LEFT)
					mo_x_abs[port] = (std::abs(v) * axisDirection + 32768) * 639 / 65535;
				else if (key == DC_AXIS_UP || key == DC_AXIS_DOWN)
					mo_y_abs[port] = (std::abs(v) * axisDirection + 32768) * 479 / 65535;
			}
			// Radial dead zone
			// FIXME compute both axes at the same time
			const float nv = std::abs(v) / 32768.f;
			const float r2 = nv * nv + otherAxisValue * otherAxisValue / 32768.f / 32768.f;
			if (r2 < input_mapper->dead_zone * input_mapper->dead_zone || r2 == 0.f)
			{
				*this_axis = 0;
			}
			else
			{
				float pdz = nv * input_mapper->dead_zone / std::sqrt(r2);
				// there's a dead angular zone at 45° with saturation > 1 (both axes are saturated)
				v = std::round((nv - pdz) / (1 - pdz) * 32768.f * input_mapper->saturation);
				*this_axis = std::clamp(v * axisDirection, -32768, 32767);
			}
		}
		else if (key != EMU_BTN_NONE && key <= DC_BTN_BITMAPPED_LAST) // Map triggers to digital buttons
		{
			//printf("B-AXIS %d Mapped to %d -> %d\n", key, value, v);
			// TODO hysteresis?
			int threshold = 16384;
			if (code == leftTrigger || code == rightTrigger )
				threshold = 100;
				
			if (std::abs(v) < threshold)
				kcode[port] |=  key; // button released
			else
				kcode[port] &= ~key; // button pressed
		}
		else if ((key & DC_BTN_GROUP_MASK) == EMU_BUTTONS) // Map triggers to emu buttons
		{
			int lastValue = lastAxisValue[port][key];
			int newValue = std::abs(v);
			if ((lastValue < 16384 && newValue >= 16384) || (lastValue >= 16384 && newValue < 16384))
				handleButtonInput(port, key, newValue >= 16384);
			lastAxisValue[port][key] = newValue;
		}
		else
			return false;

		return true;
	};

	bool rc = false;
	if (_maple_port == 4)
	{
		for (u32 port = 0; port < 4; port++)
		{
			DreamcastKey key = input_mapper->get_axis_id(port, code, !positive);
			handle_axis(port, key, 0);
			key = input_mapper->get_axis_id(port, code, positive);
			rc = handle_axis(port, key, value) || rc;
		}
	}
	else
	{
		DreamcastKey key = input_mapper->get_axis_id(0, code, !positive);
		// Reset opposite axis to 0
		handle_axis(_maple_port, key, 0);
		key = input_mapper->get_axis_id(0, code, positive);
		rc = handle_axis(_maple_port, key, value);
	}

	return rc;
}

void GamepadDevice::load_system_mappings()
{
	for (int i = 0; i < GetGamepadCount(); i++)
	{
		std::shared_ptr<GamepadDevice> gamepad = GetGamepad(i);
		if (!gamepad->find_mapping())
		{
			// The other half of the pair: "no file, using built-in defaults" is
			// a different fact from "loaded a file", and only saying one of
			// them leaves the other as silence.
			// NAMES THE FILE IT LOOKED FOR, not just the device. The filename
			// is api_name() + "_" + name() with nine characters substituted
			// (make_mapping_filename), so reconstructing it outside the process
			// means reimplementing that - and a harness that guessed it wrong
			// would report "the hotkey did not fire" for a file nobody read.
			NOTICE_LOG(INPUT, "INPUT MAPPING: %s has no mapping file (wanted %s) - built-in defaults",
					gamepad->name().c_str(),
					gamepad->make_mapping_filename(false, settings.platform.system).c_str());
			gamepad->resetMappingToDefault(settings.platform.isArcade(), true);
		}
	}
}

std::string GamepadDevice::make_mapping_filename(bool instance, int system, bool perGame /* = false */)
{
	std::string mapping_file = api_name() + "_" + name();
	if (instance)
		mapping_file += "-" + _unique_id;
	if (perGame && !settings.content.gameId.empty())
		mapping_file += "_" + settings.content.gameId;
	if (system != DC_PLATFORM_DREAMCAST)
		mapping_file += "_arcade";
	std::replace(mapping_file.begin(), mapping_file.end(), '/', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '\\', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), ':', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '?', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '*', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '|', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '"', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '<', '-');
	std::replace(mapping_file.begin(), mapping_file.end(), '>', '-');
	mapping_file += ".cfg";

	return mapping_file;
}

bool GamepadDevice::find_mapping(int system /* = settings.platform.system */)
{
	if (!_remappable)
		return true;
	instanceMapping = false;
	bool cloneMapping = false;
	while (true)
	{
		bool perGame = !settings.content.gameId.empty();
		while (true)
		{
			std::string mapping_file = make_mapping_filename(true, system, perGame);
			input_mapper = InputMapping::LoadMapping(mapping_file);
			if (!input_mapper)
			{
				mapping_file = make_mapping_filename(false, system, perGame);
				input_mapper = InputMapping::LoadMapping(mapping_file);
			}
			else
			{
				instanceMapping = true;
			}
			if (!!input_mapper)
			{
				if (cloneMapping)
					input_mapper = std::make_shared<InputMapping>(*input_mapper);
				perGameMapping = perGame;
				rumblePower = input_mapper->rumblePower;
				/*
					WHAT THE TAS ACTIONS ARE ACTUALLY BOUND TO, once, per device.

					Bounded by the registry (five rows today), so this cannot
					become a wall. It answers a support question directly - "is
					my key bound, and to what" - and it is the only place a
					CHORD's name is printed outside the settings window, which
					is what makes `get_button_name`'s modifier decoding
					observable from a harness rather than only by eye.
				*/
				if (cfgLoadBool("dojo", "HotkeyTrace", false))
					for (int i = 0; i < hotkeys::count(); i++)
					{
						const u32 code = input_mapper->get_button_code(0, hotkeys::all()[i].id);
						if (code == (u32)-1)
							continue;
						const char *nm = get_button_name(code);
						NOTICE_LOG(INPUT, "HOTKEY BOUND: [%s] %-24s %s (code %u)",
								name().c_str(), hotkeys::all()[i].label,
								nm != nullptr ? nm : "?", code);
					}
				return true;
			}
			if (!perGame)
				break;
			perGame = false;
		}
		if (system == DC_PLATFORM_DREAMCAST)
			break;
		system = DC_PLATFORM_DREAMCAST;
		cloneMapping = true;
	}
	return false;
}

int GamepadDevice::GetGamepadCount()
{
	_gamepads_mutex.lock();
	int count = _gamepads.size();
	_gamepads_mutex.unlock();
	return count;
}

std::shared_ptr<GamepadDevice> GamepadDevice::GetGamepad(int index)
{
	_gamepads_mutex.lock();
	std::shared_ptr<GamepadDevice> dev;
	if (index >= 0 && index < (int)_gamepads.size())
		dev = _gamepads[index];
	else
		dev = NULL;
	_gamepads_mutex.unlock();
	return dev;
}

std::shared_ptr<GamepadDevice> GamepadDevice::GetGamepad(std::string uid)
{
	_gamepads_mutex.lock();
	std::shared_ptr<GamepadDevice> dev = NULL;
	for (int i = 0; i < _gamepads.size(); ++i)
	{
		if (_gamepads[i]->unique_id() == uid)
			dev = _gamepads[i];
	}
	_gamepads_mutex.unlock();
	return dev;
}

void GamepadDevice::save_mapping(int system /* = settings.platform.system */)
{
	if (!input_mapper)
		return;
	std::string filename = make_mapping_filename(instanceMapping, system, perGameMapping);
	InputMapping::SaveMapping(filename, input_mapper);
}

void GamepadDevice::setPerGameMapping(bool enabled)
{
	perGameMapping = enabled;
	if (enabled)
		input_mapper = std::make_shared<InputMapping>(*input_mapper);
	else
	{
		auto deleteMapping = [this](bool instance, int system) {
			std::string filename = make_mapping_filename(instance, system, true);
			InputMapping::DeleteMapping(filename);
		};
		deleteMapping(false, DC_PLATFORM_DREAMCAST);
		deleteMapping(false, DC_PLATFORM_NAOMI);
		deleteMapping(true, DC_PLATFORM_DREAMCAST);
		deleteMapping(true, DC_PLATFORM_NAOMI);
	}
}

static void updateVibration(u32 port, float power, float inclination, u32 duration_ms)
{
	int i = GamepadDevice::GetGamepadCount() - 1;
	for ( ; i >= 0; i--)
	{
		std::shared_ptr<GamepadDevice> gamepad = GamepadDevice::GetGamepad(i);
		if (gamepad != NULL && gamepad->maple_port() == (int)port && gamepad->is_rumble_enabled())
			gamepad->rumble(power, inclination, duration_ms);
	}
}

void GamepadDevice::detect_btn_input(input_detected_cb button_pressed)
{
	_input_detected = button_pressed;
	_detecting_button = true;
	_detecting_axis = false;
	_detection_start_time = os_GetSeconds() + 0.2;
}

void GamepadDevice::detect_axis_input(input_detected_cb axis_moved)
{
	_input_detected = axis_moved;
	_detecting_button = false;
	_detecting_axis = true;
	_detection_start_time = os_GetSeconds() + 0.2;
}

void GamepadDevice::detectButtonOrAxisInput(input_detected_cb input_changed)
{
	_input_detected = input_changed;
	_detecting_button = true;
	_detecting_axis = true;
	_detection_start_time = os_GetSeconds() + 0.2;
}

#ifdef TEST_AUTOMATION
static FILE *get_record_input(bool write)
{
	if (write && !cfgLoadBool("record", "record_input", false))
		return NULL;
	if (!write && !cfgLoadBool("record", "replay_input", false))
		return NULL;
	std::string game_dir = settings.content.path;
	size_t slash = game_dir.find_last_of("/");
	size_t dot = game_dir.find_last_of(".");
	std::string input_file = "scripts/" + game_dir.substr(slash + 1, dot - slash) + "input";
	return nowide::fopen(input_file.c_str(), write ? "w" : "r");
}
#endif

void GamepadDevice::Register(const std::shared_ptr<GamepadDevice>& gamepad)
{
	int maple_port = cfgLoadInt("input",
			MAPLE_PORT_CFG_PREFIX + gamepad->unique_id(), 12345);
	if (maple_port != 12345)
		gamepad->set_maple_port(maple_port);
#ifdef TEST_AUTOMATION
	if (record_input == NULL)
	{
		record_input = get_record_input(true);
		if (record_input != NULL)
			setbuf(record_input, NULL);
	}
#endif
	_gamepads_mutex.lock();
	_gamepads.push_back(gamepad);
	_gamepads_mutex.unlock();
	MapleConfigMap::UpdateVibration = updateVibration;
}

void GamepadDevice::Unregister(const std::shared_ptr<GamepadDevice>& gamepad)
{
	_gamepads_mutex.lock();
	for (auto it = _gamepads.begin(); it != _gamepads.end(); it++)
		if (*it == gamepad) {
			_gamepads.erase(it);
			break;
		}
	_gamepads_mutex.unlock();
}

void GamepadDevice::SaveMaplePorts()
{
	for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
	{
		std::shared_ptr<GamepadDevice> gamepad = GamepadDevice::GetGamepad(i);
		if (gamepad != NULL && !gamepad->unique_id().empty())
			cfgSaveInt("input", MAPLE_PORT_CFG_PREFIX + gamepad->unique_id(), gamepad->maple_port());
	}
}

#ifdef TEST_AUTOMATION
#include "cfg/option.h"
static bool replay_inited;
FILE *replay_file;
u64 next_event;
u32 next_port;
u32 next_kcode;
bool do_screenshot;

void replay_input()
{
	if (!replay_inited)
	{
		replay_file = get_record_input(false);
		replay_inited = true;
	}
	u64 now = sh4_sched_now64();
	if (config::UseReios)
	{
		// Account for the swirl time
		if (config::Broadcast == 0)
			now = std::max((int64_t)now - 2152626532L, 0L);
		else
			now = std::max((int64_t)now - 2191059108L, 0L);
	}
	if (replay_file == NULL)
	{
		if (next_event > 0 && now - next_event > SH4_MAIN_CLOCK * 5)
			die("Automation time-out after 5 s\n");
		return;
	}
	while (next_event <= now)
	{
		if (next_event > 0)
			kcode[next_port] = next_kcode;

		char action[32];
		if (fscanf(replay_file, "%ld %s %x %x\n", &next_event, action, &next_port, &next_kcode) != 4)
		{
			fclose(replay_file);
			replay_file = NULL;
			NOTICE_LOG(INPUT, "Input replay terminated");
			do_screenshot = true;
			break;
		}
	}
}
#endif
