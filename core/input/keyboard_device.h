/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "types.h"
#include "cfg/option.h"
#include "gamepad_device.h"
#include <map>
#include "rend/gui.h"
#include <memory>

extern u8 kb_key[4][6];	// normal keys pressed
extern u8 kb_shift[4];	// modifier keys pressed (bitmask)

enum DCKeyboardModifiers {
	DC_KBMOD_LEFTCTRL   = 0x01,
	DC_KBMOD_LEFTSHIFT  = 0x02,
	DC_KBMOD_LEFTALT    = 0x04,
	DC_KBMOD_LEFTGUI    = 0x08,
	DC_KBMOD_RIGHTCTRL  = 0x10,
	DC_KBMOD_RIGHTSHIFT = 0x20,
	DC_KBMOD_RIGHTALT   = 0x40,
	DC_KBMOD_S2         = 0x80,
};

class KeyboardInputMapping : public InputMapping
{
public:
	KeyboardInputMapping()
	{
		name = "Keyboard";
		set_button(DC_BTN_A, 27);				// X
		set_button(DC_BTN_B, 6);				// C
		set_button(DC_BTN_X, 22);				// S
		set_button(DC_BTN_Y, 7);				// D
		set_button(DC_DPAD_UP, 82);
		set_button(DC_DPAD_DOWN, 81);
		set_button(DC_DPAD_LEFT, 80);
		set_button(DC_DPAD_RIGHT, 79);
		set_button(DC_BTN_START, 40);			// Return
		set_button(DC_AXIS_LT, 9);				// F
		set_button(DC_AXIS_RT, 25);				// V
		set_button(EMU_BTN_MENU, 43);			// TAB
		set_button(EMU_BTN_FFORWARD, 44);		// Space
		set_button(DC_AXIS_UP, 12);				// I
		set_button(DC_AXIS_DOWN, 14);			// K
		set_button(DC_AXIS_LEFT, 13);			// J
		set_button(DC_AXIS_RIGHT, 15);			// L
		set_button(DC_BTN_D, 4);				// Q (Coin)

		set_button(EMU_BTN_RECORD, 58);			// F1
		set_button(EMU_BTN_RECORD_1, 59);		// F2
		set_button(EMU_BTN_RECORD_2, 60);		// F3
		set_button(EMU_BTN_PLAY, 61);			// F4
		set_button(EMU_BTN_PLAY_1, 62);			// F5
		set_button(EMU_BTN_PLAY_2, 63);			// F6
		set_button(EMU_BTN_SWITCH_PLAYER, 64);	// F7
		set_button(EMU_BTN_SAVESTATE, 65);		// F8
		set_button(EMU_BTN_LOADSTATE, 66);		// F9
		set_button(EMU_BTN_PLAY_RND, 67);		// F10

		/*
			THE TAS ACTIONS: DAVID'S KEY, PLUS SHIFT.

			`[FIXED 2026-09-10]` these shipped UNBOUND, which made every one of
			them unreachable - a registry, an audit, a test and a cheat sheet
			over six actions nobody could press. The cheat sheet was the
			punchline: a panel whose entire content read "unbound" six times.

			The reasoning that produced that was "the fork defaults these to
			F2/F4/F5/F8/F9 and every one of those is a TRAINING binding here",
			which is true (F1-F10 above are all taken) - and then it stopped,
			instead of looking for keys that are free. CHORDS ARE WHY THIS IS
			EASY NOW: Shift+F1..F10 are entirely unused, and the modifier layer
			was built two commits before this gap was noticed.

			So each action takes the fork's own key with Shift added. Anyone
			moving between the two forks keeps their muscle memory, and nothing
			collides with training, which owns the bare function keys here.

			SAVESTATE_SLOT_PREV and PIANO_ROLL have no default in the fork.
			Prev takes Ctrl+F2, symmetric with Shift+F2 for next - the same key,
			the other modifier. The piano roll takes the fork's F5 slot, which
			is free here because its TAS_UI action is not ported.

			F11 IS NOT AVAILABLE. `[MEASURED 2026-09-10]` core/sdl/sdl.cpp
			intercepts it for fullscreen and consumes the key DOWN, so a hotkey
			bound there receives only the release and every case is guarded on
			`pressed`. It is not bindable in this emulator at all.
		*/
		set_button(EMU_BTN_SAVESTATE_SLOT_NEXT, 59 | InputMapping::KEY_MOD_SHIFT);	// Shift+F2, as the fork's F2
		set_button(EMU_BTN_SAVESTATE_SLOT_PREV, 59 | InputMapping::KEY_MOD_CTRL);	// Ctrl+F2, the other half of the pair
		set_button(EMU_BTN_SLOT_PICKER,         61 | InputMapping::KEY_MOD_SHIFT);	// Shift+F4, as the fork's F4
		set_button(EMU_BTN_PIANO_ROLL,          62 | InputMapping::KEY_MOD_SHIFT);	// Shift+F5
		set_button(EMU_BTN_GEN_ARCHIVE,         65 | InputMapping::KEY_MOD_SHIFT);	// Shift+F8, as the fork's F8
		set_button(EMU_BTN_HOTKEY_HELP,         66 | InputMapping::KEY_MOD_SHIFT);	// Shift+F9, as the fork's F9

		set_button(EMU_BTN_PAUSE, 54);			// ,
		set_button(EMU_BTN_STEP, 55);			// .

		dirty = false;
	}
};

class KeyboardDevice : public GamepadDevice
{
protected:
	KeyboardDevice(int maple_port, const char* apiName, bool remappable = true)
		: GamepadDevice(maple_port, apiName, remappable) {
		_name = "Keyboard";
	}

	std::shared_ptr<InputMapping> getDefaultMapping() override {
		return std::make_shared<KeyboardInputMapping>();
	}

	/*
		KEYBOARD CHORDS - Shift+F6 and friends, as one bindable code.

		`[PORTED 2026-09-10]` from the TAS fork. A chord is the key's scancode
		with modifier flags OR'd into its high bits (InputMapping::KEY_MOD_*),
		so the map, the mapping file and every lookup handle it unchanged.

		TWO RULES, and they are the whole design - both are bugs a naive version
		has, and the fork's own comment names them:

		  1. THE CHORD IS ONLY USED IF IT IS ACTUALLY BOUND. Otherwise the raw
		     key is sent, exactly as before. So holding Shift while playing can
		     never stop a game input registering - which is the failure that
		     would make this unshippable, since Shift is a perfectly ordinary
		     thing to be holding.

		  2. A KEY RELEASES WITH WHATEVER CODE IT PRESSED WITH. Letting go of
		     the modifier before the key would otherwise send a release for a
		     code nobody is holding, and strand the real one down forever.

		`_chordSent` is what rule 2 needs: the code each physical key went out
		with, remembered until it comes back up.
	*/
	u32 modifierFlags() const
	{
		u32 f = 0;
		if (_modifier_keys & (DC_KBMOD_LEFTSHIFT | DC_KBMOD_RIGHTSHIFT))
			f |= InputMapping::KEY_MOD_SHIFT;
		if (_modifier_keys & (DC_KBMOD_LEFTCTRL | DC_KBMOD_RIGHTCTRL))
			f |= InputMapping::KEY_MOD_CTRL;
		if (_modifier_keys & (DC_KBMOD_LEFTALT | DC_KBMOD_RIGHTALT))
			f |= InputMapping::KEY_MOD_ALT;
		return f;
	}

	//! The code to hand the mapping layer for this physical key, right now.
	u32 chordCode(u8 keycode, bool pressed)
	{
		if (!pressed)
		{
			// RULE 2. An unknown key releasing as itself is the right answer
			// for anything that was down before this code existed.
			auto it = _chordSent.find(keycode);
			if (it == _chordSent.end())
				return keycode;
			const u32 sent = it->second;
			_chordSent.erase(it);
			return sent;
		}
		u32 sent = keycode;
		const u32 mods = modifierFlags();
		// RULE 1. `get_button_id` answering EMU_BTN_NONE means nothing is bound
		// to this chord, so the plain key goes out untouched.
		if (mods != 0 && input_mapper != nullptr
				&& input_mapper->get_button_id(0, keycode | mods) != EMU_BTN_NONE)
			sent = keycode | mods;
		_chordSent[keycode] = sent;
		return sent;
	}

	void input(u8 keycode, bool pressed, int modifier_keys)
	{
		const int port = maple_port();

		// Checking if we're in-game with an emulated keyboard first,
		// then check if the "Bypass Emulated Keyboard" hotkey is held and we're not in GUI,
		// if so: send emu hotkeys only and return earlier.
		if ((settings.platform.isConsole() && config::MapleMainDevices[port] == MDT_Keyboard)
				|| (settings.platform.isArcade() && settings.input.keyboardGame))
		{
			if (keycode == input_mapper->get_button_code(0, EMU_BTN_BYPASS_KB))
				bypass_kb = pressed && !gui_keyboard_captured();

			if (bypass_kb)
			{
				set_maple_port(-1);
				gamepad_btn_input(chordCode(keycode, pressed), pressed);
				set_maple_port(port);
				return;
			}
		}

		// Some OSes (Mac OS) don't distinguish left and right modifier keys so we set them both.
		// But not for Alt since Right Alt is used as a special modifier keys on some international
		// keyboards.
		switch (keycode)
		{
			case 0xE1: // Left Shift
			case 0xE5: // Right Shift
				setFlag(_modifier_keys, DC_KBMOD_LEFTSHIFT | DC_KBMOD_RIGHTSHIFT, pressed);
				break;
			case 0xE0: // Left Ctrl
			case 0xE4: // Right Ctrl
				setFlag(_modifier_keys, DC_KBMOD_LEFTCTRL | DC_KBMOD_RIGHTCTRL, pressed);
				break;
			case 0xE2: // Left Alt
				setFlag(_modifier_keys, DC_KBMOD_LEFTALT, pressed);
				break;
			case 0xE6: // Right Alt
				setFlag(_modifier_keys, DC_KBMOD_RIGHTALT, pressed);
				break;
			case 0xE7: // S2 special key
				setFlag(_modifier_keys, DC_KBMOD_S2, pressed);
				break;
			default:
				break;
		}
		if (port >= 0 && port < (int)std::size(kb_shift))
			kb_shift[port] = _modifier_keys;

		if (keycode != 0)
		{
			gui_keyboard_key(keycode, pressed);
			if (keycode < 0xE0 && port >= 0 && port < (int)std::size(kb_key))
			{
				if (pressed)
				{
					if (_kb_used < std::size(kb_key[port]))
					{
						bool found = false;
						for (u32 i = 0; !found && i < _kb_used; i++)
						{
							if (kb_key[port][i] == keycode)
								found = true;
						}
						if (!found)
							kb_key[port][_kb_used++] = keycode;
					}
				}
				else
				{
					for (u32 i = 0; i < _kb_used; i++)
					{
						if (kb_key[port][i] == keycode)
						{
							_kb_used--;
							for (u32 j = i; j < std::size(kb_key[port]) - 1; j++)
								kb_key[port][j] = kb_key[port][j + 1];
							kb_key[port][std::size(kb_key[port]) - 1] = 0;
							break;
						}
					}
				}
				kb_shift[port] |= modifier_keys;
			}
		}
		if (gui_keyboard_captured())
		{
			// chat: disable the keyboard controller. Only accept emu keys (menu, escape...)
			set_maple_port(-1);
			gamepad_btn_input(chordCode(keycode, pressed), pressed);
			set_maple_port(port);
		}
		// Do not map keyboard keys to gamepad buttons unless the GUI is open
		// or the corresponding maple device (if any) isn't a keyboard
		else if (gui_is_open()
				|| port == (int)std::size(kb_key)
				|| (settings.platform.isConsole() && config::MapleMainDevices[port] != MDT_Keyboard)
				|| (settings.platform.isArcade() && !settings.input.keyboardGame))
			// ALL THREE dispatch sites go through chordCode, not just this one.
			// A chord that worked in the menu and not in chat would be a bug
			// nobody could describe.
			gamepad_btn_input(chordCode(keycode, pressed), pressed);
	}

public:
	/*
		A CHORD NAMES ITSELF - and this lives in ONE place, called by every
		get_button_name override, because there is more than one.

		`[MEASURED 2026-09-10]` the first version of this put the decoding in
		KeyboardDevice::get_button_name alone. SDLKeyboardDevice OVERRIDES that
		(sdl_keyboard.h), so on the device that actually names keys the code
		compiled, linked and WAS NEVER REACHED - a bound Shift+F5 still printed
		as `? (code 65598)`. Caught only by making the emulator print the name
		and looking at it; the code read correctly the whole time.

		Returns nullptr when the plain key has no name, because an unnamed key
		under a modifier is still unnamed - and the caller's fallback prints the
		raw code, which is the honest answer.

		The buffer is thread_local: this hands back a const char* by contract,
		the caller renders it immediately, and a chord is the only case that has
		to compose anything.
	*/
	const char *chordName(u32 code)
	{
		static thread_local std::string chord;
		chord.clear();
		if (code & InputMapping::KEY_MOD_CTRL)
			chord += "Ctrl+";
		if (code & InputMapping::KEY_MOD_SHIFT)
			chord += "Shift+";
		if (code & InputMapping::KEY_MOD_ALT)
			chord += "Alt+";
		// Virtual, so the subclass names the plain key its own way.
		const char *base = get_button_name(code & ~InputMapping::KEY_MOD_MASK);
		if (base == nullptr)
			return nullptr;
		chord += base;
		return chord.c_str();
	}

	const char *get_button_name(u32 code) override
	{
		if ((code & InputMapping::KEY_MOD_MASK) != 0)
			return chordName(code);
		switch (code)
		{
		case 0x04:
			return "A";
		case 0x05:
			return "B";
		case 0x06:
			return "C";
		case 0x07:
			return "D";
		case 0x08:
			return "E";
		case 0x09:
			return "F";
		case 0x0A:
			return "G";
		case 0x0B:
			return "H";
		case 0x0C:
			return "I";
		case 0x0D:
			return "J";
		case 0x0E:
			return "K";
		case 0x0F:
			return "L";
		case 0x10:
			return "M";
		case 0x11:
			return "N";
		case 0x12:
			return "O";
		case 0x13:
			return "P";
		case 0x14:
			return "Q";
		case 0x15:
			return "R";
		case 0x16:
			return "S";
		case 0x17:
			return "T";
		case 0x18:
			return "U";
		case 0x19:
			return "V";
		case 0x1A:
			return "W";
		case 0x1B:
			return "X";
		case 0x1C:
			return "Y";
		case 0x1D:
			return "Z";

		case 0x1E:
			return "1";
		case 0x1F:
			return "2";
		case 0x20:
			return "3";
		case 0x21:
			return "4";
		case 0x22:
			return "5";
		case 0x23:
			return "6";
		case 0x24:
			return "7";
		case 0x25:
			return "8";
		case 0x26:
			return "9";
		case 0x27:
			return "0";

		case 0x28:
			return "Return";
		case 0x29:
			return "Escape";
		case 0x2A:
			return "Backspace";
		case 0x2B:
			return "Tab";
		case 0x2C:
			return "Space";

		case 0x2D:
			return "-";
		case 0x2E:
			return "=";
		case 0x2F:
			return "[";
		case 0x30:
			return "]";
		case 0x31:
			return "\\";
		case 0x32:
			return "#";		// non-US
		case 0x33:
			return ";";
		case 0x34:
			return "'";
		case 0x35:
			return "`";
		case 0x36:
			return ",";
		case 0x37:
			return ".";
		case 0x38:
			return "/";
		case 0x39:
			return "CapsLock";

		case 0x3A:
			return "F1";
		case 0x3B:
			return "F2";
		case 0x3C:
			return "F3";
		case 0x3D:
			return "F4";
		case 0x3E:
			return "F5";
		case 0x3F:
			return "F6";
		case 0x40:
			return "F7";
		case 0x41:
			return "F8";
		case 0x42:
			return "F9";
		case 0x43:
			return "F10";
		case 0x44:
			return "F11";
		case 0x45:
			return "F12";

		case 0x46:
			return "PrintScreen";
		case 0x47:
			return "ScrollLock";
		case 0x48:
			return "Pause";
		case 0x49:
			return "Insert";
		case 0x4A:
			return "Home";
		case 0x4B:
			return "Page Up";
		case 0x4C:
			return "Delete";
		case 0x4D:
			return "End";
		case 0x4E:
			return "Page Down";
		case 0x4F:
			return "Right";
		case 0x50:
			return "Left";
		case 0x51:
			return "Down";
		case 0x52:
			return "Up";

		case 0x53:
			return "NumLock";
		case 0x54:
			return "Num /";
		case 0x55:
			return "Num *";
		case 0x56:
			return "Num -";
		case 0x57:
			return "Num +";
		case 0x58:
			return "Num Enter";
		case 0x59:
			return "Num 1";
		case 0x5A:
			return "Num 2";
		case 0x5B:
			return "Num 3";
		case 0x5C:
			return "Num 4";
		case 0x5D:
			return "Num 5";
		case 0x5E:
			return "Num 6";
		case 0x5F:
			return "Num 7";
		case 0x60:
			return "Num 8";
		case 0x61:
			return "Num 9";
		case 0x62:
			return "Num 0";
		case 0x63:
			return "Num .";

		case 0x64:
			return "\\";	// non-US
		case 0x65:
			return "Application";
		case 0x66:
			return "Power";
		case 0x67:
			return "Num =";

		case 0x68:
			return "F13";
		case 0x69:
			return "F14";
		case 0x6A:
			return "F15";
		case 0x6B:
			return "F16";
		case 0x6C:
			return "F17";
		case 0x6D:
			return "F18";
		case 0x6E:
			return "F19";
		case 0x6F:
			return "F20";
		case 0x70:
			return "F21";
		case 0x71:
			return "F22";
		case 0x72:
			return "F23";
		case 0x73:
			return "F24";

		case 0x87:
			return "Int1";
		case 0x88:
			return "Int2";
		case 0x89:
			return "Yen";
		case 0x8A:
			return "Int4";
		case 0x8B:
			return "Int5";
		case 0x8C:
			return "Int6";
		case 0x8D:
			return "Int7";
		case 0x8E:
			return "Int8";
		case 0x8F:
			return "Int9";

		case 0x90:
			return "Hangul";
		case 0x91:
			return "Hanja";
		case 0x92:
			return "Katakana";
		case 0x93:
			return "Hiragana";
		case 0x94:
			return "Zenkaku/Hankaku";
		case 0x95:
			return "Lang6";
		case 0x96:
			return "Lang7";
		case 0x97:
			return "Lang8";
		case 0x98:
			return "Lang9";

		case 0xE0:
			return "Left Ctrl";
		case 0xE1:
			return "Left Shift";
		case 0xE2:
			return "Left Alt";
		case 0xE3:
			return "Left Meta";
		case 0xE4:
			return "Right Ctrl";
		case 0xE5:
			return "Right Shift";
		case 0xE6:
			return "Right Alt";
		case 0xE7:
			return "Right Meta";

		default:
			return nullptr;
		}
	}

private:
	//! Rule 2's memory: what code each physical key was dispatched with, kept
	//! until it is released. Small and short-lived - at most the number of keys
	//! actually held down.
	std::map<u8, u32> _chordSent;

	void setFlag(int& v, u32 bitmask, bool set)
	{
		if (set)
			v |= bitmask;
		else
			v &= ~bitmask;
	}

	int _modifier_keys = 0;
	u32 _kb_used = 0;
	bool bypass_kb = false;
};
