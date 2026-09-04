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
#include "lua_console.h"
#ifdef USE_LUA
#include <lua.hpp>
#include "stdclass.h"
#include "imgui/imgui.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <vector>

namespace luaconsole
{

struct Line
{
	Kind kind;
	std::string text;
};

//! Bounded, because a script printing every frame would otherwise grow without
//! limit and the interesting line is never the ten-thousandth.
static constexpr size_t MaxLines = 2000;

static std::mutex mutex;
static std::deque<Line> lines;
static std::vector<std::string> pending;
static bool shown;
static bool scrollToEnd;
static FILE *logFile;
static char inputBuf[512];

static const char *kindName(Kind k)
{
	switch (k)
	{
	case Kind::Input:  return "> ";
	case Kind::Error:  return "! ";
	case Kind::Info:   return "# ";
	default:           return "  ";
	}
}

//! Opened lazily and kept open: a script that prints every frame should not
//! pay an open/close per line, and a crash still leaves what was flushed.
static void openLog()
{
	if (logFile != nullptr)
		return;
	const std::string path = get_writable_config_path("flycast-lua.log");
	logFile = std::fopen(path.c_str(), "a");
	if (logFile == nullptr)
		return;
	std::time_t now = std::time(nullptr);
	char stamp[64];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	std::fprintf(logFile, "\n=== session %s ===\n", stamp);
	std::fflush(logFile);
}

void add(Kind kind, const std::string& text)
{
	std::lock_guard<std::mutex> lock(mutex);
	openLog();
	if (logFile != nullptr)
	{
		std::fprintf(logFile, "%s%s\n", kindName(kind), text.c_str());
		// Flushed per line rather than buffered: the lines that matter most are
		// the ones written immediately before whatever went wrong, and a
		// buffered tail is exactly what a crash discards.
		std::fflush(logFile);
	}
	lines.push_back({ kind, text });
	while (lines.size() > MaxLines)
		lines.pop_front();
	scrollToEnd = true;

	// An error opens the panel. A message nobody can see is the whole reason
	// this file exists.
	if (kind == Kind::Error)
		shown = true;
}

void addf(Kind kind, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	add(kind, buf);
}

void submit(const std::string& line)
{
	if (line.empty())
		return;
	add(Kind::Input, line);
	std::lock_guard<std::mutex> lock(mutex);
	pending.push_back(line);
}

void drain(lua_State *L)
{
	if (L == nullptr)
		return;
	std::vector<std::string> batch;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (pending.empty())
			return;
		batch.swap(pending);
	}
	for (const std::string& line : batch)
	{
		// "return <expr>" first, so typing an expression shows its value the
		// way a REPL should; falling back to the statement form for anything
		// that is not an expression.
		std::string chunk = "return " + line;
		if (luaL_loadstring(L, chunk.c_str()) != LUA_OK)
		{
			lua_pop(L, 1);
			if (luaL_loadstring(L, line.c_str()) != LUA_OK)
			{
				add(Kind::Error, lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "syntax error");
				lua_pop(L, 1);
				continue;
			}
		}
		const int base = lua_gettop(L) - 1;
		if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
		{
			add(Kind::Error, lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "error");
			lua_pop(L, 1);
			continue;
		}
		const int nres = lua_gettop(L) - base;
		for (int i = 1; i <= nres; i++)
		{
			const char *s = luaL_tolstring(L, base + i, nullptr);
			add(Kind::Output, s != nullptr ? s : "(nil)");
			lua_pop(L, 1);
		}
		lua_pop(L, nres);
	}
}

bool visible() { return shown; }
void setVisible(bool v) { shown = v; }

void reset()
{
	add(Kind::Info, "Lua interpreter (re)started");
}

void draw()
{
	if (!shown)
		return;
	ImGui::SetNextWindowSize(ImVec2(560, 300), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(40, 300), ImGuiCond_FirstUseEver);
	bool open = true;
	if (ImGui::Begin("Lua console", &open))
	{
		if (ImGui::Button("clear"))
		{
			std::lock_guard<std::mutex> lock(mutex);
			lines.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", get_writable_config_path("flycast-lua.log").c_str());
		ImGui::Separator();

		const float inputHeight = ImGui::GetFrameHeightWithSpacing() + 4;
		ImGui::BeginChild("scrollback", ImVec2(0, -inputHeight), false,
				ImGuiWindowFlags_HorizontalScrollbar);
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (const Line& l : lines)
			{
				ImVec4 col;
				switch (l.kind)
				{
				case Kind::Input: col = ImVec4(0.60f, 0.80f, 1.00f, 1.f); break;
				case Kind::Error: col = ImVec4(1.00f, 0.45f, 0.45f, 1.f); break;
				case Kind::Info:  col = ImVec4(0.65f, 0.65f, 0.65f, 1.f); break;
				default:          col = ImVec4(0.90f, 0.90f, 0.90f, 1.f); break;
				}
				ImGui::PushStyleColor(ImGuiCol_Text, col);
				ImGui::TextUnformatted(l.text.c_str());
				ImGui::PopStyleColor();
			}
		}
		if (scrollToEnd)
		{
			ImGui::SetScrollHereY(1.0f);
			scrollToEnd = false;
		}
		ImGui::EndChild();

		ImGui::PushItemWidth(-1);
		// Queued, never evaluated here: see THE UI NEVER EVALUATES in the header.
		if (ImGui::InputText("##lua_input", inputBuf, sizeof(inputBuf),
				ImGuiInputTextFlags_EnterReturnsTrue))
		{
			submit(inputBuf);
			inputBuf[0] = '\0';
			ImGui::SetKeyboardFocusHere(-1);
		}
		ImGui::PopItemWidth();
	}
	ImGui::End();
	if (!open)
		shown = false;
}

}
#endif	// USE_LUA
