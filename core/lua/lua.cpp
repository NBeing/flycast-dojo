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
#include "lua.h"

#ifdef USE_LUA
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include "rend/gui.h"
#include "network/ggpo.h"
#ifndef LIBRETRO
#include "rend/video_recorder.h"
#endif
#include "hw/mem/_vmem.h"
#include "cfg/option.h"
#include "emulator.h"
#include "input/gamepad_device.h"
#include "input/mouse.h"
#include "hw/maple/maple_devs.h"
#include "hw/maple/maple_if.h"
#include "stdclass.h"
#include <vector>
#include <unordered_map>
#include <limits>
#include <xxhash.h>
#include "hw/sh4/sh4_if.h"
#include "serialize.h"
#include "imgui/imgui.h"
#include "rend/transform_matrix.h"
#include "dojo/DojoSession.hpp"
#include <stdexcept>

namespace lua
{
const char *CallbackTable = "flycast_callbacks";
static lua_State *L;
using namespace luabridge;

static std::recursive_mutex mutex;
using lock_guard = std::lock_guard<std::recursive_mutex>;

u32 pressed_buttons[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

void releasePressedButtons()
{
	for (int i = 0; i < 2; i++)
	{
		pressed_buttons[i] = kcode[i];
		kcode[i] = ~0;
	}
}

void restorePressedButtons()
{
	for (int i = 0; i < 2; i++)
	{
		if (dojo.record_player == i)
			continue;
		kcode[i] = pressed_buttons[i];
		pressed_buttons[i] = ~0;
	}
}

static void emuEventCallback(Event event, void *)
{
	if (L == nullptr)
		return;
	// GGPO re-simulates a frame for every rollback, and the VBlank event fires
	// again on each pass. A script that accumulates - a counter, a tally, a log
	// line - would count the same logical frame several times, so re-simulated
	// frames are not dispatched. The overlay callback needs no such guard: it
	// runs on the render path, and rollback frames are dropped before render.
	//
	// A script that deliberately wants to observe re-simulation can check
	// flycast.state.isRollback() from a callback that does fire.
	if (event == Event::VBlank && ggpo::rollbacking())
		return;
	lock_guard lock(mutex);
	try {
		LuaRef v = LuaRef::getGlobal(L, CallbackTable);
		if (!v.isTable())
			return;
		const char *key = nullptr;
		switch (event)
		{
		case Event::Start:
			key = "start";
			break;
		case Event::Resume:
			key = "resume";
			break;
		case Event::Pause:
			key = "pause";
			break;
		case Event::Terminate:
			key = "terminate";
			break;
		case Event::LoadState:
			key = "loadState";
			break;
		case Event::VBlank:
			key = "vblank";
			break;
		}
		if (v[key].isFunction())
			v[key]();
	} catch (const LuaException& e) {
		WARN_LOG(COMMON, "Lua exception: %s", e.what());
	}
}

static void eventCallback(const char *tag)
{
	if (L == nullptr)
		return;
	lock_guard lock(mutex);
	try {
		LuaRef v = LuaRef::getGlobal(L, CallbackTable);
		if (v.isTable() && v[tag].isFunction())
			v[tag]();
	} catch (const LuaException& e) {
		WARN_LOG(COMMON, "Lua exception[%s]: %s", tag, e.what());
	}
}

/*
 * Drawing is only legal from the overlay callback.
 *
 * Callbacks do not all run on the same thread. `overlay` is dispatched from
 * gui_display_osd() on the render thread, inside an ImGui frame; `vblank` is
 * dispatched from Emulator::vblank() on the emulation thread, which is a
 * separate std::async thread whenever ThreadedRendering is on - the default on
 * desktop. ImGui keeps global per-frame state, so a script that draws from
 * vblank races the render thread and corrupts it.
 *
 * The flag is thread_local, so the emulation thread always reads false no
 * matter what the render thread is doing, and no synchronisation is needed. It
 * also catches drawing from outside any callback at all, where there is no
 * ImGui frame to draw into.
 */
static thread_local bool inDrawCallback;

struct DrawContextGuard
{
	DrawContextGuard() { inDrawCallback = true; }
	~DrawContextGuard() { inDrawCallback = false; }
};

static const char *DrawContextMessage =
		": drawing is only allowed from the overlay callback, which runs on the"
		" render thread. Other callbacks run on the emulation thread. Buffer what"
		" you want to draw and emit it from overlay.";

//! For bindings LuaBridge wraps: it catches C++ exceptions and turns them into
//! Lua errors.
static void checkDrawContext(const char *fn)
{
	if (!inDrawCallback)
		throw std::runtime_error(std::string(fn) + DrawContextMessage);
}

//! For bindings registered as a raw lua_CFunction. Those are called straight
//! from Lua with no wrapper, so a thrown exception unwinds past the interpreter
//! and reaches terminate instead of becoming a catchable Lua error. luaL_error
//! is the correct mechanism there; it must be reached before any local with a
//! destructor is constructed, which is why every call sits first in its body.
static int checkDrawContextL(lua_State *L, const char *fn)
{
	if (!inDrawCallback)
		return luaL_error(L, "%s%s", fn, DrawContextMessage);
	return 0;
}

void overlay()
{
	DrawContextGuard guard;
	eventCallback("overlay");
}

template<typename T>
static LuaRef readMemoryTable(u32 address, int count, lua_State* L)
{
	LuaRef t(L);
	t = newTable(L);
	while (count > 0)
	{
		t[address] = _vmem_readt<T, T>(address);
		address += sizeof(T);
		count--;
	}

	return t;
}

#define CONFIG_ACCESSORS(Config) 	\
template<typename T>				\
static T get ## Config() {			\
	return config::Config.get();	\
}									\
template<typename T>				\
static void set ## Config(T v)		\
{									\
	config::Config.set(v);			\
}

// General
CONFIG_ACCESSORS(Cable);
CONFIG_ACCESSORS(Region);
CONFIG_ACCESSORS(Broadcast);
CONFIG_ACCESSORS(Language);
CONFIG_ACCESSORS(FullMMU);
CONFIG_ACCESSORS(ForceWindowsCE);
CONFIG_ACCESSORS(AutoLoadState);
CONFIG_ACCESSORS(AutoSaveState);
CONFIG_ACCESSORS(SavestateSlot);
// TODO Option<std::vector<std::string>, false> ContentPath;
CONFIG_ACCESSORS(HideLegacyNaomiRoms)

// Video
CONFIG_ACCESSORS(RendererType)
CONFIG_ACCESSORS(Widescreen)
CONFIG_ACCESSORS(UseMipmaps)
CONFIG_ACCESSORS(SuperWidescreen)
CONFIG_ACCESSORS(ShowFPS)
CONFIG_ACCESSORS(RenderToTextureBuffer)
CONFIG_ACCESSORS(TranslucentPolygonDepthMask)
CONFIG_ACCESSORS(ModifierVolumes)
CONFIG_ACCESSORS(TextureUpscale)
CONFIG_ACCESSORS(MaxFilteredTextureSize)
CONFIG_ACCESSORS(ExtraDepthScale)
CONFIG_ACCESSORS(CustomTextures)
CONFIG_ACCESSORS(DumpTextures)
CONFIG_ACCESSORS(ScreenStretching)
CONFIG_ACCESSORS(Fog)
CONFIG_ACCESSORS(FloatVMUs)
CONFIG_ACCESSORS(Rotate90)
CONFIG_ACCESSORS(PerStripSorting)
CONFIG_ACCESSORS(DelayFrameSwapping)
CONFIG_ACCESSORS(WidescreenGameHacks)
//TODO CrosshairColor;
CONFIG_ACCESSORS(SkipFrame)
CONFIG_ACCESSORS(MaxThreads)
CONFIG_ACCESSORS(AutoSkipFrame)
CONFIG_ACCESSORS(RenderResolution)
CONFIG_ACCESSORS(VSync)
CONFIG_ACCESSORS(PixelBufferSize)
CONFIG_ACCESSORS(AnisotropicFiltering)
CONFIG_ACCESSORS(TextureFiltering)
CONFIG_ACCESSORS(ThreadedRendering)

// Audio
CONFIG_ACCESSORS(DSPEnabled)
CONFIG_ACCESSORS(AudioBufferSize)
CONFIG_ACCESSORS(AutoLatency)
CONFIG_ACCESSORS(AudioBackend)
CONFIG_ACCESSORS(AudioVolume)

// Advanced
CONFIG_ACCESSORS(DynarecEnabled)
CONFIG_ACCESSORS(DynarecIdleSkip)
CONFIG_ACCESSORS(SerialConsole)
CONFIG_ACCESSORS(SerialPTY)
CONFIG_ACCESSORS(UseReios)
CONFIG_ACCESSORS(FastGDRomLoad)
CONFIG_ACCESSORS(OpenGlChecks)

// Network
CONFIG_ACCESSORS(NetworkEnable)
CONFIG_ACCESSORS(ActAsServer)
CONFIG_ACCESSORS(DNS)
CONFIG_ACCESSORS(NetworkServer)
CONFIG_ACCESSORS(EmulateBBA)
CONFIG_ACCESSORS(GGPOEnable)
CONFIG_ACCESSORS(GGPODelay)
CONFIG_ACCESSORS(NetworkStats)
CONFIG_ACCESSORS(GGPOAnalogAxes)

// Dojo
CONFIG_ACCESSORS(ShowTrainingGameOverlay)

// Maple devices

static int getMapleType(int bus, lua_State *L)
{
	luaL_argcheck(L, bus >= 1 && bus <= 4, 1, "bus must be between 1 and 4");
	if (MapleDevices[bus - 1][5] == nullptr)
		return MDT_None;
	return MapleDevices[bus - 1][5]->get_device_type();
}

static int getMapleSubType(int bus, int port, lua_State *L)
{
	luaL_argcheck(L, bus >= 1 && bus <= 4, 1, "bus must be between 1 and 4");
	luaL_argcheck(L, port >= 1 && port <= 2, 2, "port must be between 1 and 2");
	if (MapleDevices[bus - 1][port - 1] == nullptr)
		return MDT_None;
	return MapleDevices[bus - 1][port - 1]->get_device_type();
}

static void setMapleType(int bus, int type, lua_State *L)
{
	luaL_argcheck(L, bus >= 1 && bus <= 4, 1, "bus must be between 1 and 4");
	switch ((MapleDeviceType)type) {
	case MDT_SegaController:
	case MDT_AsciiStick:
	case MDT_Keyboard:
	case MDT_Mouse:
	case MDT_LightGun:
	case MDT_TwinStick:
	case MDT_None:
		config::MapleMainDevices[bus - 1] = (MapleDeviceType)type;
		maple_ReconnectDevices();
		break;
	default:
		luaL_argerror(L, 2, "Invalid device type");
		break;
	}
}

static void setMapleSubType(int bus, int port, int type, lua_State *L)
{
	luaL_argcheck(L, bus >= 1 && bus <= 4, 1, "bus must be between 1 and 4");
	luaL_argcheck(L, port >= 1 && port <= 2, 2, "port must be between 1 and 2");
	switch ((MapleDeviceType)type) {
	case MDT_SegaVMU:
	case MDT_PurupuruPack:
	case MDT_Microphone:
	case MDT_None:
		config::MapleExpansionDevices[bus - 1][port - 1] = (MapleDeviceType)type;
		maple_ReconnectDevices();
		break;
	default:
		luaL_argerror(L, 3, "Invalid device type");
		break;
	}
}

// Inputs

static void checkPlayerNum(lua_State *L, int player) {
	luaL_argcheck(L, player >= 1 && player <= 4, 1, "player must be between 1 and 4");
}

static u32 getButtons(int player, lua_State *L)
{
	checkPlayerNum(L, player);
	return kcode[player - 1];
}

/*
 * Buttons as named booleans.
 *
 * kcode is a raw bitmask whose layout is specific to this system, and it is
 * active-low: a *cleared* bit means the button is held. Neither fact is
 * discoverable from Lua, and no constants were ever exposed, so scripts had to
 * hardcode inverted tests against Dreamcast bit values.
 *
 * The table form is the portable contract - see docs/lua_api_spec.lua. A
 * button reads as true when it is held, which is the opposite polarity to the
 * bitmask underneath, deliberately.
 */
struct ButtonMapping
{
	const char *name;
	u32 bit;
};

static const ButtonMapping ButtonMappings[] = {
	{ "a",      DC_BTN_A },
	{ "b",      DC_BTN_B },
	{ "c",      DC_BTN_C },
	{ "d",      DC_BTN_D },
	{ "x",      DC_BTN_X },
	{ "y",      DC_BTN_Y },
	{ "z",      DC_BTN_Z },
	{ "start",  DC_BTN_START },
	{ "up",     DC_DPAD_UP },
	{ "down",   DC_DPAD_DOWN },
	{ "left",   DC_DPAD_LEFT },
	{ "right",  DC_DPAD_RIGHT },
	{ "up2",    DC_DPAD2_UP },
	{ "down2",  DC_DPAD2_DOWN },
	{ "left2",  DC_DPAD2_LEFT },
	{ "right2", DC_DPAD2_RIGHT },
	{ "reload", DC_BTN_RELOAD },
};

// Every button name this system understands, so a script can adapt rather
// than assume.
static LuaRef getButtonNames(lua_State *L)
{
	LuaRef t(L);
	t = newTable(L);
	int i = 1;
	for (const ButtonMapping& m : ButtonMappings)
		t[i++] = m.name;
	return t;
}

static LuaRef getButtonTable(int player, lua_State *L)
{
	checkPlayerNum(L, player);
	const u32 k = kcode[player - 1];
	LuaRef t(L);
	t = newTable(L);
	for (const ButtonMapping& m : ButtonMappings)
		t[m.name] = (k & m.bit) == 0;	// active-low
	return t;
}

// Present and true presses, present and false releases, absent leaves the
// button alone - so a script can drive one button without disturbing the rest.
static void setButtonTable(int player, LuaRef buttons, lua_State *L)
{
	checkPlayerNum(L, player);
	if (!buttons.isTable())
	{
		luaL_argerror(L, 2, "expected a table of button names");
		return;
	}
	u32& k = kcode[player - 1];
	for (const ButtonMapping& m : ButtonMappings)
	{
		LuaRef v = buttons[m.name];
		if (v.isNil())
			continue;
		if (v.cast<bool>())
			k &= ~m.bit;	// held
		else
			k |= m.bit;		// released
	}
}

static void setButton(int player, const std::string& name, bool pressed, lua_State *L)
{
	checkPlayerNum(L, player);
	for (const ButtonMapping& m : ButtonMappings)
	{
		if (name == m.name)
		{
			if (pressed)
				kcode[player - 1] &= ~m.bit;
			else
				kcode[player - 1] |= m.bit;
			return;
		}
	}
	luaL_argerror(L, 2, "unknown button name; see input.buttonNames()");
}

static void pressButtons(int player, u32 buttons, lua_State *L)
{
	checkPlayerNum(L, player);
	kcode[player - 1] &= ~buttons;
}

static void releaseButtons(int player, u32 buttons, lua_State *L)
{
	checkPlayerNum(L, player);
	kcode[player - 1] |= buttons;
}

static int getAxis(int player, int axis, lua_State *L)
{
	checkPlayerNum(L, player);
	luaL_argcheck(L, axis >= 1 && axis <= 6, 2, "axis must be between 1 and 6");
	switch (axis - 1)
	{
	case 0:
		return joyx[player - 1];
	case 1:
		return joyy[player - 1];
	case 2:
		return joyrx[player - 1];
	case 3:
		return joyry[player - 1];
	case 4:
		return lt[player - 1];
	case 5:
		return rt[player - 1];
	default:
		return 0;
	}
}

static void setAxis(int player, int axis, int value, lua_State *L)
{
	checkPlayerNum(L, player);
	luaL_argcheck(L, axis >= 1 && axis <= 6, 2, "axis must be between 1 and 6");
	switch (axis - 1)
	{
	case 0:
		joyx[player - 1] = value;
		break;
	case 1:
		joyy[player - 1] = value;
		break;
	case 2:
		joyrx[player - 1] = value;
		break;
	case 3:
		joyry[player - 1] = value;
		break;
	case 4:
		lt[player - 1] = value;
		break;
	case 5:
		rt[player - 1] = value;
		break;
	default:
		break;
	}
}

static int getAbsCoordinates(lua_State *L)
{
	int player = luaL_checkinteger(L, 1);
	checkPlayerNum(L, player);
	lua_pushnumber(L, mo_x_abs[player - 1]);
	lua_pushnumber(L, mo_y_abs[player - 1]);
	return 2;
}

static void setAbsCoordinates(int player, int x, int y, lua_State *L)
{
	checkPlayerNum(L, player);
	SetMousePosition(x, y, settings.display.width, settings.display.height, player - 1);
}

static int getRelCoordinates(lua_State *L)
{
	int player = luaL_checkinteger(L, 1);
	checkPlayerNum(L, player);
	lua_pushnumber(L, mo_x_delta[player - 1]);
	lua_pushnumber(L, mo_y_delta[player - 1]);
	return 2;
}

static void setRelCoordinates(int player, float x, float y, lua_State *L)
{
	checkPlayerNum(L, player);
	SetRelativeMousePosition(x, y, player - 1);
}

// UI

static void beginWindow(const char *title, int x, int y, int w, int h)
{
	checkDrawContext("beginWindow");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::SetNextWindowPos(ImVec2(x, y));
	ImGui::SetNextWindowSize(ImVec2(w * settings.display.uiScale, h * settings.display.uiScale));
	ImGui::SetNextWindowBgAlpha(0.7f);
	ImGui::Begin(title, NULL, ImGuiWindowFlags_AlwaysAutoResize |  ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus);
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.557f, 0.268f, 0.965f, 1.f));
}

static void endWindow()
{
	checkDrawContext("endWindow");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::PopStyleColor();
	ImGui::End();
	ImGui::PopStyleVar(2);
}

static void uiText(const std::string& text)
{
	checkDrawContext("uiText");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::Text("%s", text.c_str());
}

static void uiTextRightAligned(const std::string& text)
{
	checkDrawContext("uiTextRightAligned");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
	uiText(text);
}

static void uiTextColor(const std::string& text, float r, float g, float b, float a)
{
	checkDrawContext("uiTextColor");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::TextColored(ImVec4(r, g, b, a), "%s", text.c_str());
}

static void uiTextColorRightAligned(const std::string& text, float r, float g, float b, float a)
{
	checkDrawContext("uiTextColorRightAligned");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
	uiTextColor(text, r, g, b ,a);
}

static void uiSameLine()
{
	checkDrawContext("uiSameLine");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::SameLine();
}

static void uiSameLinePlaceholder(const std::string& text)
{
	checkDrawContext("uiSameLinePlaceholder");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
}

static void uiSameLinePlaceholderRightAligned(const std::string& text)
{
	checkDrawContext("uiSameLinePlaceholderRightAligned");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
}

static void uiBargraph(float v)
{
	checkDrawContext("uiBargraph");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::ProgressBar(v, ImVec2(-1, 10.f * settings.display.uiScale), "");
}

static void uiBargraphColor(float v, u32 color)
{
	checkDrawContext("uiBargraphColor");
	if (!config::ShowTrainingGameOverlay)
		return;
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
	ImGui::ProgressBar(v, ImVec2(-1, 10.f * settings.display.uiScale), "");
	ImGui::PopStyleColor();
}

static int uiButton(lua_State *L)
{
	checkDrawContext("uiButton");
	if (!config::ShowTrainingGameOverlay)
		return 0;
	const char *label = luaL_checkstring(L, 1);
	if (ImGui::Button(label))
	{
		LuaRef callback = LuaRef::fromStack(L, 2);
		if (callback.isFunction())
			callback();
	}
	return 0;
}

/*
 * ImGui baseline profile - see docs/lua_api_spec.lua, THE UI SURFACE.
 *
 * Names match ImGui's own on purpose: a script author already knows this API,
 * and renaming it to look neutral would only put a translation layer between
 * the documentation and the binding.
 *
 * Nothing here applies settings.display.uiScale. The older beginWindow does,
 * to its size but not its position, which means at any scale other than 1 the
 * two are in different units; that is kept for the scripts that depend on it
 * rather than silently changed. New code should position in raw pixels and
 * scale deliberately using ui.GetScale().
 *
 * Widgets that own a value return (value, changed) - Lua has multiple returns,
 * so there is no need for the pointer dance the C++ API uses.
 */
/*
 * Memory watches: did this range change since I last asked?
 *
 * A snapshot compared on each poll, rather than the dirty-page tracking in
 * hw/mem/mem_watch.h. That tracking exists for rollback and is only armed when
 * GGPO is enabled, it reports whole pages rather than the bytes asked for, and
 * arming it outside netplay would add page faults to every write in the
 * emulated machine. Comparing a copy costs the caller proportionally to what
 * they actually watch, needs nothing from the emulator, and answers exactly the
 * question asked.
 *
 * changed() reports "since the last call", updating the snapshot, which is the
 * shape a per-frame poll wants.
 */
struct MemoryWatch
{
	u32 addr;
	u32 len;
	std::vector<u8> snapshot;
};

static std::unordered_map<int, MemoryWatch> memoryWatches;
static int nextWatchId = 1;

//! Bounded so a typo cannot ask for a gigabyte of comparison every frame.
static constexpr u32 MaxWatchBytes = 1024 * 1024;

static void readRange(u32 addr, u32 len, std::vector<u8>& out)
{
	out.resize(len);
	for (u32 i = 0; i < len; i++)
		out[i] = _vmem_ReadMem8(addr + i);
}

static int watchCreate(u32 addr, u32 len, lua_State *L)
{
	if (len == 0 || len > MaxWatchBytes)
		luaL_argerror(L, 2, "watch length must be between 1 and 1048576 bytes");
	MemoryWatch w;
	w.addr = addr;
	w.len = len;
	readRange(addr, len, w.snapshot);
	const int id = nextWatchId++;
	memoryWatches[id] = std::move(w);
	return id;
}

static bool watchChanged(int id, lua_State *L)
{
	auto it = memoryWatches.find(id);
	if (it == memoryWatches.end())
		luaL_argerror(L, 1, "no such memory watch (already released?)");
	MemoryWatch& w = it->second;
	std::vector<u8> now;
	readRange(w.addr, w.len, now);
	if (now == w.snapshot)
		return false;
	w.snapshot = std::move(now);
	return true;
}

static void watchRelease(int id, lua_State *L)
{
	if (memoryWatches.erase(id) == 0)
		luaL_argerror(L, 1, "no such memory watch");
}

//! Watches are per-session state; a new game must not inherit them.
static void clearWatches()
{
	memoryWatches.clear();
	nextWatchId = 1;
}

/*
 * SH4 registers by name.
 *
 * Reads are coherent from the frame callback, which runs on the emulation
 * thread; from a draw callback they race the running CPU, exactly as the
 * existing memory accessors do. The interface documents that rather than
 * paying for a guard on every access.
 */
struct RegisterMapping
{
	const char *name;
	u32 *ptr;
};

static bool findRegister(const std::string& name, u32*& out)
{
	// General purpose registers, r0..r15.
	if (name.size() >= 2 && name[0] == 'r')
	{
		const std::string idx = name.substr(1);
		if (!idx.empty() && idx.find_first_not_of("0123456789") == std::string::npos)
		{
			const int n = std::stoi(idx);
			if (n >= 0 && n <= 15)
			{
				out = &Sh4cntx.r[n];
				return true;
			}
		}
	}
	static const RegisterMapping named[] = {
		{ "pc",   &Sh4cntx.pc },
		{ "pr",   &Sh4cntx.pr },
		{ "gbr",  &Sh4cntx.gbr },
		{ "vbr",  &Sh4cntx.vbr },
		{ "ssr",  &Sh4cntx.ssr },
		{ "spc",  &Sh4cntx.spc },
		{ "sgr",  &Sh4cntx.sgr },
		{ "dbr",  &Sh4cntx.dbr },
		{ "fpul", &Sh4cntx.fpul },
	};
	for (const RegisterMapping& m : named)
	{
		if (name == m.name)
		{
			out = m.ptr;
			return true;
		}
	}
	return false;
}

static u32 getRegister(const std::string& name, lua_State *L)
{
	u32 *p = nullptr;
	if (!findRegister(name, p))
		luaL_argerror(L, 1, "unknown register; try r0..r15, pc, pr, gbr, vbr, ssr, spc, sgr, dbr, fpul");
	return p != nullptr ? *p : 0;
}

static void setRegister(const std::string& name, u32 value, lua_State *L)
{
	u32 *p = nullptr;
	if (!findRegister(name, p))
		luaL_argerror(L, 1, "unknown register; try r0..r15, pc, pr, gbr, vbr, ssr, spc, sgr, dbr, fpul");
	if (p != nullptr)
		*p = value;
}

/*
 * Savestates as strings.
 *
 * The same serializer the file path uses, sized by a dry run first so the
 * buffer is exact. rollback=false, so this is a whole state including memory -
 * expect megabytes, and do not call it every frame.
 */
static int saveStateToString(lua_State *L)
{
	Serializer sizer(nullptr, std::numeric_limits<size_t>::max(), false);
	dc_serialize(sizer);
	std::vector<u8> buf(sizer.size());
	Serializer ser(buf.data(), buf.size(), false);
	dc_serialize(ser);
	lua_pushlstring(L, (const char *)buf.data(), ser.size());
	return 1;
}

static int loadStateFromString(lua_State *L)
{
	size_t len = 0;
	const char *data = luaL_checklstring(L, 1, &len);
	if (len == 0)
		return luaL_error(L, "empty savestate string");
	Deserializer deser(data, len, false);
	dc_deserialize(deser);
	return 0;
}

//! Cheap identity for a state, for hunting desyncs: two machines that agree
//! frame by frame produce the same hash, and the first frame they differ is
//! where to look.
static int hashState(lua_State *L)
{
	if (lua_isstring(L, 1))
	{
		size_t len = 0;
		const char *data = lua_tolstring(L, 1, &len);
		lua_pushinteger(L, (lua_Integer)XXH32(data, len, 0));
		return 1;
	}
	Serializer sizer(nullptr, std::numeric_limits<size_t>::max(), false);
	dc_serialize(sizer);
	std::vector<u8> buf(sizer.size());
	Serializer ser(buf.data(), buf.size(), false);
	dc_serialize(ser);
	lua_pushinteger(L, (lua_Integer)XXH32(buf.data(), ser.size(), 0));
	return 1;
}

/*
 * The game image's rectangle inside the window, in window pixels.
 *
 * A content overlay - a hitbox, a position marker - has to be positioned in
 * game pixels and land on the game image, which is letterboxed inside the
 * window whenever the window's aspect ratio differs from the game's. This is
 * the same computation renderLastFrame() uses to place the blit
 * (core/rend/gles/gldraw.cpp), against the same aspect-ratio source, so the
 * rectangle reported here is the one the frame is actually drawn into.
 *
 * getDCFramebufferAspectRatio() reads only config - Rotate90 and
 * ScreenStretching - so this needs nothing from a renderer and is correct for
 * all of them.
 */
static int getGameViewport(lua_State *L)
{
	const float screenW = (float)settings.display.width;
	const float screenH = (float)settings.display.height;
	const float screenAR = screenH > 0 ? screenW / screenH : 1.f;
	const float renderAR = getDCFramebufferAspectRatio();

	float dx = 0.f;
	float dy = 0.f;
	if (renderAR > screenAR)
		dy = roundf(screenH * (1 - screenAR / renderAR) / 2.f);
	else
		dx = roundf(screenW * (1 - renderAR / screenAR) / 2.f);

	lua_pushnumber(L, dx);
	lua_pushnumber(L, dy);
	lua_pushnumber(L, screenW - dx * 2);
	lua_pushnumber(L, screenH - dy * 2);
	return 4;
}

//! The logical resolution the game draws in, which is what game-pixel
//! coordinates are relative to. The Dreamcast displays 640x480; 240p content is
//! line-doubled to it rather than presented at half height.
static int getGameResolution(lua_State *L)
{
	const bool rotated = config::Rotate90;
	lua_pushinteger(L, rotated ? 480 : 640);
	lua_pushinteger(L, rotated ? 640 : 480);
	return 2;
}

static float uiGetScale()
{
	checkDrawContext("GetScale");
	return settings.display.uiScale;
}

static int uiBegin(lua_State *L)
{
	checkDrawContextL(L, "Begin");
	const char *name = luaL_checkstring(L, 1);
	// Returns whether the window is expanded, matching ImGui: skip the body
	// when it is false.
	lua_pushboolean(L, ImGui::Begin(name));
	return 1;
}

static void uiEnd()          { checkDrawContext("End"); ImGui::End(); }
static void uiSeparator()    { checkDrawContext("Separator"); ImGui::Separator(); }
static void uiSpacing()      { checkDrawContext("Spacing"); ImGui::Spacing(); }

static void uiSetNextWindowPos(float x, float y)
{
	checkDrawContext("SetNextWindowPos");
	ImGui::SetNextWindowPos(ImVec2(x, y));
}

static void uiSetNextWindowSize(float w, float h)
{
	checkDrawContext("SetNextWindowSize");
	ImGui::SetNextWindowSize(ImVec2(w, h));
}

static int uiCheckbox(lua_State *L)
{
	checkDrawContextL(L, "Checkbox");
	const char *label = luaL_checkstring(L, 1);
	bool v = lua_toboolean(L, 2) != 0;
	const bool changed = ImGui::Checkbox(label, &v);
	lua_pushboolean(L, v);
	lua_pushboolean(L, changed);
	return 2;
}

static int uiSelectable(lua_State *L)
{
	checkDrawContextL(L, "Selectable");
	const char *label = luaL_checkstring(L, 1);
	const bool selected = lua_toboolean(L, 2) != 0;
	lua_pushboolean(L, ImGui::Selectable(label, selected));
	return 1;
}

static int uiSliderFloat(lua_State *L)
{
	checkDrawContextL(L, "SliderFloat");
	const char *label = luaL_checkstring(L, 1);
	float v = (float)luaL_checknumber(L, 2);
	const float lo = (float)luaL_checknumber(L, 3);
	const float hi = (float)luaL_checknumber(L, 4);
	const bool changed = ImGui::SliderFloat(label, &v, lo, hi);
	lua_pushnumber(L, v);
	lua_pushboolean(L, changed);
	return 2;
}

static int uiSliderInt(lua_State *L)
{
	checkDrawContextL(L, "SliderInt");
	const char *label = luaL_checkstring(L, 1);
	int v = (int)luaL_checkinteger(L, 2);
	const int lo = (int)luaL_checkinteger(L, 3);
	const int hi = (int)luaL_checkinteger(L, 4);
	const bool changed = ImGui::SliderInt(label, &v, lo, hi);
	lua_pushinteger(L, v);
	lua_pushboolean(L, changed);
	return 2;
}

static int uiInputText(lua_State *L)
{
	checkDrawContextL(L, "InputText");
	const char *label = luaL_checkstring(L, 1);
	const char *initial = luaL_optstring(L, 2, "");
	// A fixed buffer rather than a resize callback: the callback form needs a
	// std::string that outlives the call, and a script editing more than this
	// in a text field wants a different widget anyway.
	char buf[512];
	strncpy(buf, initial, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	const bool changed = ImGui::InputText(label, buf, sizeof(buf));
	lua_pushstring(L, buf);
	lua_pushboolean(L, changed);
	return 2;
}

static int uiGetMousePos(lua_State *L)
{
	checkDrawContextL(L, "GetMousePos");
	const ImVec2 p = ImGui::GetMousePos();
	lua_pushnumber(L, p.x);
	lua_pushnumber(L, p.y);
	return 2;
}

//! Mouse buttons are 1-based here, per the interface's indexing rule; ImGui's
//! own are 0-based.
static int checkMouseButton(lua_State *L, int arg)
{
	const int b = (int)luaL_checkinteger(L, arg);
	luaL_argcheck(L, b >= 1 && b <= 5, arg, "mouse button must be between 1 and 5");
	return b - 1;
}

static int uiIsMouseClicked(lua_State *L)
{
	checkDrawContextL(L, "IsMouseClicked");
	lua_pushboolean(L, ImGui::IsMouseClicked(checkMouseButton(L, 1)));
	return 1;
}

static int uiIsMouseDown(lua_State *L)
{
	checkDrawContextL(L, "IsMouseDown");
	lua_pushboolean(L, ImGui::IsMouseDown(checkMouseButton(L, 1)));
	return 1;
}

static int uiIsMouseReleased(lua_State *L)
{
	checkDrawContextL(L, "IsMouseReleased");
	lua_pushboolean(L, ImGui::IsMouseReleased(checkMouseButton(L, 1)));
	return 1;
}

static int uiRect(float x, float y, float w, float h, u32 fill, u32 border)
{
	checkDrawContext("uiRect");
	if (!config::ShowTrainingGameOverlay)
		return 0;
	ImDrawList *draw_list = ImGui::GetForegroundDrawList();
	draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill);
	draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), border, 0, 0, 4.0);
	return 0;
}

static int uiLine(float x1, float y1, float x2, float y2, u32 color)
{
	checkDrawContext("uiLine");
	if (!config::ShowTrainingGameOverlay)
		return 0;
	ImDrawList *draw_list = ImGui::GetForegroundDrawList();
	draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, 4.0f);
	return 0;
}

static int read8s(u32 addr)
{
	u8 data = _vmem_ReadMem8(addr);
	return (s8)data;
}

static int read16s(u32 addr)
{
	u16 data = _vmem_ReadMem16(addr);
	return (s16)data;
}

static int read32s(u32 addr)
{
	u32 data = _vmem_ReadMem32(addr);
	return (s32)data;
}

static f32 read32f(u32 addr)
{
	u32 data = _vmem_ReadMem32(addr);
	return *(f32 *)&data;
}

static int getFrameNumber()
{
	return (int)dojo.FrameNumber.load();
}

static void loadRecordSlotsFile(std::string filename)
{
	dojo.LoadRecordSlotsFile(filename);
}

static void playRecordSlot(int slot)
{
	dojo.PlayRecording(slot);
}

static void luaRegister(lua_State *L)
{
	getGlobalNamespace(L)
		.beginNamespace ("flycast")
	  		.beginNamespace("emulator")
				.addFunction("startGame", gui_start_game)	// FIXME threading!
				.addFunction("stopGame", std::function<void()>([]() { gui_stop_game(""); }))
				.addFunction("pause", std::function<void()>([]() {
					if (gui_state == GuiState::Closed)
						gui_open_settings();
				}))
				.addFunction("resume", std::function<void()>([]() {
					if (gui_state == GuiState::Commands)
						gui_open_settings();
				}))
				// Slots are 0..9 here, matching config::SavestateSlot, while
				// players are 1-based. The neutral savestate.* alias is
				// 1-based per the spec; this one keeps its existing base so
				// current scripts are not silently shifted by one.
				.addFunction("saveState", std::function<void(int)>([](int index) {
					if (index < 0 || index > 9)
						throw std::runtime_error("savestate slot must be between 0 and 9");
					bool restart = false;
					if (gui_state == GuiState::Closed) {
						gui_open_settings();
						restart = true;
					}
					dc_savestate(index);
					if (restart)
						gui_open_settings();
				}))
				.addFunction("loadState", std::function<void(int)>([](int index) {
					if (index < 0 || index > 9)
						throw std::runtime_error("savestate slot must be between 0 and 9");
					bool restart = false;
					if (gui_state == GuiState::Closed) {
						gui_open_settings();
						restart = true;
					}
					dc_loadstate(index);
					if (restart)
						gui_open_settings();
				}))
				.addFunction("exit", dc_exit)
				.addFunction("isOnline", std::function<bool()>([]() {
					return settings.network.online;
				}))
				.addFunction("isReplay", std::function<bool()>([]() {
					return dojo.PlayMatch;
				}))
				//! Frames in the replay being played back, or recorded so far.
				.addFunction("getReplayFrameCount", std::function<int()>([]() {
					if (dojo.PlayMatch)
						return (int)dojo.maple_inputs.size();
					return dojo.recording_replay ? (int)dojo.FrameNumber : 0;
				}))
				//! AICA has 64 hardware voices and mixes at 44100 Hz.
				.addFunction("getVoiceCount", std::function<int()>([]() {
					return 64;
				}))
				.addFunction("getOutputRate", std::function<int()>([]() {
					return 44100;
				}))
				.addFunction("getSpeedMode", std::function<bool()>([]() {
					return settings.input.fastForwardMode;
				}))
				.addFunction("setSpeedMode", std::function<void(bool)>([](bool fast) {
					settings.input.fastForwardMode = fast;
				}))
				.addFunction("saveStateString", saveStateToString)
				.addFunction("loadStateString", loadStateFromString)
				.addFunction("hashState", hashState)
				.addFunction("displayNotification", gui_display_notification)
			.endNamespace()

#ifndef LIBRETRO
			// Capture of the presented frame, overlays included. Pass an empty
			// path to startRecording() for a timestamped file in the data folder.
	  		.beginNamespace("video")
				.addFunction("startRecording", std::function<void(std::string)>([](std::string path) {
					videorec::requestStart(path);
				}))
				.addFunction("stopRecording", std::function<void()>([]() {
					videorec::requestStop();
				}))
				.addFunction("toggleRecording", std::function<void()>([]() {
					videorec::toggle();
				}))
				.addFunction("isRecording", std::function<bool()>([]() {
					return videorec::isRecording();
				}))
				.addFunction("recordingPath", std::function<std::string()>([]() {
					return videorec::outputPath();
				}))
			.endNamespace()

			// Input-replay recording, independent of the video capture above.
	  		.beginNamespace("replay")
				.addFunction("startRecording", std::function<bool(std::string)>([](std::string name) {
					return dojo.StartReplayRecording(name);
				}))
				.addFunction("stopRecording", std::function<void()>([]() {
					dojo.StopReplayRecording();
				}))
				.addFunction("isRecording", std::function<bool()>([]() {
					return dojo.IsRecordingReplay();
				}))
				.addFunction("currentPath", std::function<std::string()>([]() {
					return dojo.replay_filename;
				}))
			.endNamespace()
#endif

	  		.beginNamespace("config")
#define CONFIG_PROPERTY(Config, type) .addProperty<type>(#Config, get ## Config, set ## Config)
				.beginNamespace("general")
					CONFIG_PROPERTY(Cable, int)
					CONFIG_PROPERTY(Region, int)
					CONFIG_PROPERTY(Broadcast, int)
					CONFIG_PROPERTY(Language, int)
					CONFIG_PROPERTY(AutoLoadState, bool)
					CONFIG_PROPERTY(AutoSaveState, bool)
					CONFIG_PROPERTY(SavestateSlot, int)
					CONFIG_PROPERTY(HideLegacyNaomiRoms, bool)
				.endNamespace()

				.beginNamespace("video")
// FIXME			.addProperty<RenderType>("RendererType", getRendererType, setRendererType)
					CONFIG_PROPERTY(Widescreen, bool)
					CONFIG_PROPERTY(SuperWidescreen, bool)
					CONFIG_PROPERTY(UseMipmaps, bool)
					CONFIG_PROPERTY(ShowFPS, bool)
					CONFIG_PROPERTY(RenderToTextureBuffer, bool)
					CONFIG_PROPERTY(TranslucentPolygonDepthMask, bool)
					CONFIG_PROPERTY(ModifierVolumes, bool)
					CONFIG_PROPERTY(TextureUpscale, int)
					CONFIG_PROPERTY(MaxFilteredTextureSize, int)
					CONFIG_PROPERTY(ExtraDepthScale, float)
					CONFIG_PROPERTY(CustomTextures, bool)
					CONFIG_PROPERTY(DumpTextures, bool)
					CONFIG_PROPERTY(ScreenStretching, int)
					CONFIG_PROPERTY(Fog, bool)
					CONFIG_PROPERTY(FloatVMUs, bool)
					CONFIG_PROPERTY(Rotate90, bool)
					CONFIG_PROPERTY(PerStripSorting, bool)
					CONFIG_PROPERTY(DelayFrameSwapping, bool)
					CONFIG_PROPERTY(WidescreenGameHacks, bool)
					// TODO CrosshairColor;
					CONFIG_PROPERTY(SkipFrame, int)
					CONFIG_PROPERTY(MaxThreads, int)
					CONFIG_PROPERTY(AutoSkipFrame, int)
					CONFIG_PROPERTY(RenderResolution, int)
					CONFIG_PROPERTY(VSync, bool)
					CONFIG_PROPERTY(PixelBufferSize, u64)
					CONFIG_PROPERTY(AnisotropicFiltering, int)
					CONFIG_PROPERTY(TextureFiltering, int)
					CONFIG_PROPERTY(ThreadedRendering, bool)
				.endNamespace()

				.beginNamespace("audio")
					CONFIG_PROPERTY(DSPEnabled, bool)
					CONFIG_PROPERTY(AudioBufferSize, int)
					CONFIG_PROPERTY(AutoLatency, bool)
					CONFIG_PROPERTY(AudioBackend, std::string)
					CONFIG_PROPERTY(AudioVolume, int)
				.endNamespace()

				.beginNamespace("advanced")
					CONFIG_PROPERTY(DynarecEnabled, bool)
					CONFIG_PROPERTY(DynarecIdleSkip, bool)
					CONFIG_PROPERTY(SerialConsole, bool)
					CONFIG_PROPERTY(SerialPTY, bool)
					CONFIG_PROPERTY(UseReios, bool)
					CONFIG_PROPERTY(FastGDRomLoad, bool)
					CONFIG_PROPERTY(OpenGlChecks, bool)
					CONFIG_PROPERTY(FullMMU, bool)
					CONFIG_PROPERTY(ForceWindowsCE, bool)
				.endNamespace()

				.beginNamespace("network")
					CONFIG_PROPERTY(NetworkEnable, bool)
					CONFIG_PROPERTY(ActAsServer, bool)
					CONFIG_PROPERTY(DNS, std::string)
					CONFIG_PROPERTY(NetworkServer, std::string)
					CONFIG_PROPERTY(EmulateBBA, bool)
					CONFIG_PROPERTY(GGPOEnable, bool)
					CONFIG_PROPERTY(GGPODelay, int)
					CONFIG_PROPERTY(NetworkStats, bool)
					CONFIG_PROPERTY(GGPOAnalogAxes, int)
				.endNamespace()

				.beginNamespace("maple")
					.addFunction("getDeviceType", getMapleType)
					.addFunction("getSubDeviceType", getMapleSubType)
					.addFunction("setDeviceType", setMapleType)
					.addFunction("setSubDeviceType", setMapleSubType)
				.endNamespace()

				.beginNamespace("dojo")
					CONFIG_PROPERTY(ShowTrainingGameOverlay, bool)
				.endNamespace()
			.endNamespace()

	  		.beginNamespace("memory")
				.addFunction("read8", _vmem_readt<u8, u8>)
				.addFunction("read16", _vmem_readt<u16, u16>)
				.addFunction("read32", _vmem_readt<u32, u32>)
				.addFunction("read64", _vmem_readt<u64, u64>)
				.addFunction("read8s", read8s)
				.addFunction("read16s", read16s)
				.addFunction("read32s", read32s)
				.addFunction("read32f", read32f)
				.addFunction("readTable8", readMemoryTable<u8>)
				.addFunction("readTable16", readMemoryTable<u16>)
				.addFunction("readTable32", readMemoryTable<u32>)
				.addFunction("readTable64", readMemoryTable<u64>)
				.addFunction("write8", _vmem_writet<u8>)
				.addFunction("write16", _vmem_writet<u16>)
				.addFunction("write32", _vmem_writet<u32>)
				.addFunction("write64", _vmem_writet<u64>)
				.addFunction("watchCreate", watchCreate)
				.addFunction("watchChanged", watchChanged)
				.addFunction("watchRelease", watchRelease)
				.addFunction("getRegister", getRegister)
				.addFunction("setRegister", setRegister)
			.endNamespace()

			.beginNamespace("input")
				// Named-boolean form: the portable contract. getButtons below
				// returns the raw active-low bitmask and is kept for existing
				// scripts.
				.addFunction("getButtonTable", getButtonTable)
				.addFunction("setButtonTable", setButtonTable)
				.addFunction("setButton", setButton)
				.addFunction("buttonNames", getButtonNames)
				.addFunction("getButtons", getButtons)
				.addFunction("pressButtons", pressButtons)
				.addFunction("releaseButtons", releaseButtons)
				.addFunction("getAxis", getAxis)
				.addFunction("setAxis", setAxis)
				.addFunction("getAbsCoordinates", getAbsCoordinates)
				.addFunction("setAbsCoordinates", setAbsCoordinates)
				.addFunction("getRelCoordinates", getRelCoordinates)
				.addFunction("setRelCoordinates", setRelCoordinates)
				.addFunction("loadRecordSlotsFile", loadRecordSlotsFile)
				.addFunction("playRecordSlot", playRecordSlot)
			.endNamespace()

			.beginNamespace("state")
				// True while GGPO is re-simulating a frame it has already run.
				// Game image rect inside the window, and the logical resolution
				// game-pixel coordinates are relative to. See docs/lua_api_spec.lua.
				.addFunction("getGameViewport", getGameViewport)
				.addFunction("getGameResolution", getGameResolution)
				.addFunction("isRollback", std::function<bool()>([]() {
					return ggpo::rollbacking();
				}))
				// Frame number that does not move during rollback, unlike
				// getFrameNumber(): dojo.FrameNumber is incremented from
				// endOfFrame() on every re-simulated frame and is not restored
				// by load_game_state, so it drifts upward across a rollback.
				.addFunction("getConfirmedFrameNumber", std::function<int()>([]() {
					return (int)ggpo::confirmedFrame();
				}))
				//! Cumulative frames re-simulated by rollback. Rises when
				//! prediction is failing, so it doubles as a connection readout.
				.addFunction("getResimSteps", std::function<int()>([]() {
					return (int)ggpo::resimSteps();
				}))
				.addProperty("system", &settings.platform.system, false)
				.addProperty("media", &settings.content.path, false)
				.addProperty("gameId", &settings.content.gameId, false)
				.beginNamespace("display")
					.addProperty("width", &settings.display.width, false)
					.addProperty("height", &settings.display.height, false)
				.endNamespace()
				.addFunction("getFrameNumber", getFrameNumber)
			.endNamespace()

			// ImGui baseline profile, under ImGui's own names. See
			// docs/lua_api_spec.lua, THE UI SURFACE.
			.beginNamespace("ui")
				.addFunction("Begin", uiBegin)
				.addFunction("End", uiEnd)
				.addFunction("Text", uiText)
				.addFunction("TextColored", uiTextColor)
				.addFunction("Button", uiButton)
				.addFunction("SameLine", uiSameLine)
				.addFunction("Checkbox", uiCheckbox)
				.addFunction("Selectable", uiSelectable)
				.addFunction("SliderFloat", uiSliderFloat)
				.addFunction("SliderInt", uiSliderInt)
				.addFunction("InputText", uiInputText)
				.addFunction("Separator", uiSeparator)
				.addFunction("Spacing", uiSpacing)
				.addFunction("SetNextWindowPos", uiSetNextWindowPos)
				.addFunction("SetNextWindowSize", uiSetNextWindowSize)
				.addFunction("GetMousePos", uiGetMousePos)
				.addFunction("IsMouseClicked", uiIsMouseClicked)
				.addFunction("IsMouseDown", uiIsMouseDown)
				.addFunction("IsMouseReleased", uiIsMouseReleased)
				.addFunction("GetScale", uiGetScale)

				// Pre-existing names, kept for the scripts that use them.
				.addFunction("beginWindow", beginWindow)
				.addFunction("endWindow", endWindow)
				.addFunction("text", uiText)
				.addFunction("rightText", uiTextRightAligned)
				.addFunction("textColor", uiTextColor)
				.addFunction("rightTextColor", uiTextColorRightAligned)
				.addFunction("sameLine", uiSameLine)
				.addFunction("sameLinePlaceholder", uiSameLinePlaceholder)
				.addFunction("sameLinePlaceholderRight", uiSameLinePlaceholderRightAligned)
				.addFunction("bargraph", uiBargraph)
				.addFunction("bargraphColor", uiBargraphColor)
				.addFunction("button", uiButton)
				.addFunction("rect", uiRect)
				.addFunction("line", uiLine)
			.endNamespace()
		.endNamespace();
}

static std::string getLuaFile()
{
	std::string initFile;
	if( !config::LuaFileName.get().empty()){
		initFile = get_readonly_config_path(config::LuaFileName.get());
	} else {
		initFile = get_readonly_config_path("flycast.lua");
	}

	return initFile;

}

static void doExec(const std::string& path)
{
	if (L == nullptr)
		return;
	DEBUG_LOG(COMMON, "Executing script: %s", path.c_str());
	int err = luaL_dofile(L, path.c_str());
	if (err != 0)
		WARN_LOG(COMMON, "Lua error: %s", lua_tostring(L, -1));
}

void exec(const std::string& path)
{
	std::string file = get_readonly_config_path(path);
	if (!file_exists(file))
		return;
	doExec(file);
}

void init()
{
	std::string initFile = getLuaFile();
	if (!file_exists(initFile))
		return;
	L = luaL_newstate();
	luaL_openlibs(L);
	luaRegister(L);
    EventManager::listen(Event::Start, emuEventCallback);
    EventManager::listen(Event::Resume, emuEventCallback);
    EventManager::listen(Event::Pause, emuEventCallback);
    EventManager::listen(Event::Terminate, emuEventCallback);
    EventManager::listen(Event::LoadState, emuEventCallback);
    EventManager::listen(Event::VBlank, emuEventCallback);

	doExec(initFile);
}

void term()
{
	if (L == nullptr)
		return;
    EventManager::unlisten(Event::Start, emuEventCallback);
    EventManager::unlisten(Event::Resume, emuEventCallback);
    EventManager::unlisten(Event::Pause, emuEventCallback);
    EventManager::unlisten(Event::Terminate, emuEventCallback);
    EventManager::unlisten(Event::LoadState, emuEventCallback);
    EventManager::unlisten(Event::VBlank, emuEventCallback);
	// Watches are per-session; a new game must not inherit the last one's.
	clearWatches();
	lua_close(L);
	L = nullptr;
}

void reinit(const std::string& initFile)
{
	term();
	if (!file_exists(initFile))
	{
		init();
		return;
	}
	L = luaL_newstate();
	luaL_openlibs(L);
	luaRegister(L);
	EventManager::listen(Event::Start, emuEventCallback);
	EventManager::listen(Event::Resume, emuEventCallback);
	EventManager::listen(Event::Pause, emuEventCallback);
	EventManager::listen(Event::Terminate, emuEventCallback);
	EventManager::listen(Event::LoadState, emuEventCallback);
	EventManager::listen(Event::VBlank, emuEventCallback);

	doExec(initFile);
}

}
#endif
