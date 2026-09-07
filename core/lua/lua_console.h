/*
	Copyright 2026 flycast-dojo contributors

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
#include <string>

//! At global scope: naming it inside the namespace below would declare a
//! distinct luaconsole::lua_State rather than referring to Lua's own.
struct lua_State;

/*
 * The Lua console: scrollback, a deferred evaluation queue, and a log file.
 *
 * It exists because a script that fails is otherwise indistinguishable from a
 * script that does nothing. `luaL_dofile` returning an error was written to the
 * log at warning level, and an emulator started from a desktop menu has no
 * terminal - so a broken script produced silence, which reads as "Lua is not
 * working" rather than "line 1 could not open that file".
 *
 * TWO RULES, both borrowed from nbneo-rr's console and both load-bearing here.
 *
 * THE UI NEVER EVALUATES. The panel can only queue; nothing runs Lua until
 * drain() is called from the frame boundary. A REPL evaluating inside an ImGui
 * draw would re-enter the host mid-frame, and a line that redefines a callback
 * table could do it while the dispatcher is walking it. It also means a typed
 * line runs on the emulation thread, so the drawing rules apply to it exactly
 * as they do to a script - ui.* from the console raises, which is correct.
 *
 * A THROWING LINE KILLS NOTHING. Every evaluation goes through lua_pcall. The
 * worst a bad line does is print red text; nothing unregisters a callback,
 * closes the interpreter, or stops the machine.
 */
namespace luaconsole
{

enum class Kind { Input, Output, Error, Info };

//! Thread-safe. Everything here also reaches the log file.
void add(Kind kind, const std::string& text);
void addf(Kind kind, const char *fmt, ...);

//! Queue a line for evaluation at the next frame boundary. Never evaluates.
void submit(const std::string& line);

//! Evaluate whatever was queued. Called from the frame boundary with the Lua
//! lock already held.
void drain(lua_State *L);

//! The ImGui panel. Render thread, inside a frame.
void draw();

//! Shown on demand, and opened automatically when something goes wrong - an
//! error nobody can see is the defect this file exists to remove.
bool visible();
void setVisible(bool v);

//! Called when the interpreter is (re)created, so scrollback survives a reload
//! but the log gets a session marker.
void reset();

}
