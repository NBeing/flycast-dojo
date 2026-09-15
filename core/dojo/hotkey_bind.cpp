#include "hotkey_bind.h"
#include "input/gamepad_device.h"
#include "input/keyboard_device.h"
#include "input/mapping.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstdio>

/*
	See hotkey_bind.h. This file is the state machine, the device plumbing and
	the arm that proves the state machine.
*/
namespace roll {
namespace rebind {

Phase step(Phase now, double elapsed, bool fired, bool cancelled)
{
	if (now != Phase::Waiting)
		return now;
	// The order IS the decision - see the header.
	if (cancelled)
		return Phase::Cancelled;
	if (fired)
		return Phase::Fired;
	if (elapsed >= kTimeout)
		return Phase::TimedOut;
	return Phase::Waiting;
}

// ---------------------------------------------------------------------------------------

static Phase g_phase = Phase::Idle;
static DreamcastKey g_key = EMU_BTN_NONE;
static double g_started = 0;
static std::vector<int> g_armed;			//!< indices we armed, so every one is cancelled
static std::shared_ptr<GamepadDevice> g_target;

/*
	WRITTEN BY THE INPUT THREAD, READ BY THE UI THREAD.

	The detect callback is a plain C function pointer invoked from wherever the
	press arrives. It may only RECORD - the mapping write happens in tick(), on
	the thread that owns the UI. `volatile` is the fork's, and is enough here
	for the same reason it is enough there: one writer, one reader, and a single
	flag published last.
*/
static volatile bool g_fired = false;
static u32 g_code = (u32)-1;
static bool g_analog = false;
static bool g_positive = false;

static void detected(u32 code, bool analog, bool positive)
{
	g_code = code;
	g_analog = analog;
	g_positive = positive;
	g_fired = true;		// published LAST: the reader tests this one
}

bool isKeyboard(const std::shared_ptr<GamepadDevice>& dev)
{
	return dev != nullptr && dynamic_cast<KeyboardDevice *>(dev.get()) != nullptr;
}

std::shared_ptr<GamepadDevice> keyboard()
{
	for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
	{
		const std::shared_ptr<GamepadDevice> dev = GamepadDevice::GetGamepad(i);
		if (isKeyboard(dev) && dev->get_input_mapping() != nullptr)
			return dev;
	}
	return nullptr;
}

std::vector<std::shared_ptr<GamepadDevice>> pads()
{
	std::vector<std::shared_ptr<GamepadDevice>> out;
	for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
	{
		const std::shared_ptr<GamepadDevice> dev = GamepadDevice::GetGamepad(i);
		if (dev != nullptr && !isKeyboard(dev) && dev->get_input_mapping() != nullptr
				&& dev->remappable()
				// "Default Mouse (P0)" is remappable and is not a pad. Binding a
				// hotkey to it produces a binding the emulator never delivers.
				&& dev->unique_id().find("mouse") == std::string::npos)
			out.push_back(dev);
	}
	return out;
}

void cancel()
{
	for (int i : g_armed)
	{
		const std::shared_ptr<GamepadDevice> dev = GamepadDevice::GetGamepad(i);
		if (dev != nullptr)
			dev->cancel_detect_input();
	}
	g_armed.clear();
	g_target.reset();
	g_phase = Phase::Idle;
	g_fired = false;
}

void arm(DreamcastKey id, const std::shared_ptr<GamepadDevice>& target)
{
	cancel();
	if (target == nullptr || target->get_input_mapping() == nullptr || !target->remappable())
	{
		gui_display_notification("No device to bind on", 2000);
		return;
	}
	g_key = id;
	g_fired = false;
	g_code = (u32)-1;
	g_analog = false;
	g_positive = false;
	g_started = os_GetSeconds();
	g_target = target;
	// Arm EXACTLY the target. When it is a pad the keyboard stays live so
	// Escape can cancel; when it IS the keyboard, Escape arrives as a keycode.
	for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
		if (GamepadDevice::GetGamepad(i).get() == target.get())
		{
			target->detectButtonOrAxisInput(&detected);
			g_armed.push_back(i);
		}
	g_phase = g_armed.empty() ? Phase::Idle : Phase::Waiting;
	if (g_phase == Phase::Waiting)
		NOTICE_LOG(INPUT, "HOTKEY REBIND: armed action %d on [%s]",
				(int)id, target->name().c_str());
}

/*
	Which armed device actually fired: the one that cleared its own detection
	callback. The device nulls it immediately after invoking us, so on the rare
	frame where we look too early we simply wait one more.
*/
static std::shared_ptr<GamepadDevice> firedDevice()
{
	for (int i : g_armed)
	{
		const std::shared_ptr<GamepadDevice> dev = GamepadDevice::GetGamepad(i);
		if (dev != nullptr && !dev->is_detecting_input())
			return dev;
	}
	return nullptr;
}

void tick()
{
	if (g_phase != Phase::Waiting)
		return;
	const double elapsed = os_GetSeconds() - g_started;
	const bool fired = g_fired;
	/*
		ESCAPE CANCELS, and it has to be read from the KEYBOARD's own mapping
		rather than from a raw keycode, because "which key is Escape" is a
		property of the device. When the keyboard is the target its Escape
		press arrives through the detector instead, and is filtered below.
	*/
	const bool cancelled = false;	// the panel calls cancel() directly on Escape

	const Phase next = step(g_phase, elapsed, fired, cancelled);
	if (next == g_phase)
		return;
	g_phase = next;

	if (next == Phase::TimedOut)
	{
		NOTICE_LOG(INPUT, "HOTKEY REBIND: timed out after %.0f s", kTimeout);
		cancel();
		g_phase = Phase::TimedOut;	// cancel() resets to Idle; say what happened
		return;
	}
	if (next != Phase::Fired)
		return;

	const std::shared_ptr<GamepadDevice> dev = firedDevice();
	if (dev == nullptr)
	{
		// Looked too early - the device has not cleared its callback yet. Go
		// back to waiting rather than binding to nothing.
		g_phase = Phase::Waiting;
		g_fired = false;
		return;
	}
	/*
		ESCAPE ON THE KEYBOARD MEANS CANCEL, NOT "BIND ESCAPE".

		Asked of the device rather than by number: `get_button_name` is how this
		tree already identifies keys (core/dojo/hotkey_panel.cpp uses the same
		call to decide what a device even is), and a hardcoded scancode is a
		claim about one keyboard layout.
	*/
	const char *nm = dev->get_button_name(g_code);
	if (!g_analog && nm != nullptr && std::string(nm) == "Escape")
	{
		NOTICE_LOG(INPUT, "HOTKEY REBIND: cancelled by Escape");
		cancel();
		g_phase = Phase::Cancelled;
		return;
	}

	const std::shared_ptr<InputMapping> map = dev->get_input_mapping();
	if (map != nullptr)
	{
		// REPLACE, not add. An action with a stale binding and a new one would
		// fire on both, and the cheat sheet can only show one of them.
		map->clear_button(0, g_key);
		map->clear_axis(0, g_key);
		if (g_analog)
			map->set_axis(0, g_key, g_code, g_positive);
		else
			map->set_button(0, g_key, g_code);
		map->set_dirty();
		dev->save_mapping();
		NOTICE_LOG(INPUT, "HOTKEY REBIND: action %d -> %s on [%s]", (int)g_key,
				nm != nullptr ? nm : "(unnamed)", dev->name().c_str());
	}
	cancel();
	g_phase = Phase::Fired;
}

bool active() { return g_phase == Phase::Waiting; }
Phase phase() { return g_phase; }
DreamcastKey pendingKey() { return g_key; }

double remaining()
{
	if (g_phase != Phase::Waiting)
		return 0.0;
	const double left = kTimeout - (os_GetSeconds() - g_started);
	return left > 0.0 ? left : 0.0;
}

std::string targetName()
{
	return g_target != nullptr ? g_target->name() : std::string();
}

void clearBinding(DreamcastKey id, const std::shared_ptr<GamepadDevice>& dev)
{
	if (dev == nullptr)
		return;
	const std::shared_ptr<InputMapping> map = dev->get_input_mapping();
	if (map == nullptr)
		return;
	map->clear_button(0, id);
	map->clear_axis(0, id);
	map->set_dirty();
	dev->save_mapping();
	NOTICE_LOG(INPUT, "HOTKEY REBIND: action %d cleared on [%s]", (int)id,
			dev->name().c_str());
}

std::string bindingName(const std::shared_ptr<GamepadDevice>& dev, DreamcastKey id)
{
	if (dev == nullptr)
		return std::string();
	const std::shared_ptr<InputMapping> map = dev->get_input_mapping();
	if (map == nullptr)
		return std::string();

	const u32 code = map->get_button_code(0, id);
	if (code != (u32)-1)
	{
		const char *nm = dev->get_button_name(code);
		if (nm != nullptr)
			return nm;
		// A key this device cannot name is still bound, and showing nothing
		// would make it read as unbound.
		char b[32];
		snprintf(b, sizeof(b), "[%u]", code);
		return b;
	}
	const std::pair<u32, bool> ax = map->get_axis_code(0, id);
	if (ax.first != (u32)-1)
	{
		const char *nm = dev->get_axis_name(ax.first);
		char b[64];
		snprintf(b, sizeof(b), "%s%s", nm != nullptr ? nm : "axis", ax.second ? "+" : "-");
		return b;
	}
	return std::string();
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "REBIND SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- the state machine ------------------------------------------------------------
	claim("waiting with nothing happening keeps waiting",
			step(Phase::Waiting, 0.5, false, false) == Phase::Waiting);
	claim("a press fires", step(Phase::Waiting, 0.5, true, false) == Phase::Fired);
	claim("the clock running out times out",
			step(Phase::Waiting, kTimeout, false, false) == Phase::TimedOut);
	claim("...and just under the limit does NOT",
			step(Phase::Waiting, kTimeout - 0.01, false, false) == Phase::Waiting);
	claim("Escape cancels", step(Phase::Waiting, 0.5, false, true) == Phase::Cancelled);

	/*
		THE PRECEDENCE CLAIMS, which are the reason step() exists. All three
		inputs can be true on one frame, and each of these is a decision
		somebody could reasonably have made the other way - so each is asserted
		rather than left to the order of a chain of ifs.
	*/
	claim("a press AND Escape on one frame cancels, and binds nothing",
			step(Phase::Waiting, 0.5, true, true) == Phase::Cancelled);
	claim("a press AND the timeout on one frame FIRES - the press came first",
			step(Phase::Waiting, kTimeout + 1.0, true, false) == Phase::Fired);
	claim("Escape AND the timeout on one frame cancels",
			step(Phase::Waiting, kTimeout + 1.0, false, true) == Phase::Cancelled);

	// A resolved detection stays resolved. Without this, a stale `fired` flag
	// would re-fire a binding every frame until something re-armed.
	claim("Idle stays Idle whatever happens",
			step(Phase::Idle, 99.0, true, true) == Phase::Idle);
	claim("a fired detection is not re-fired",
			step(Phase::Fired, 0.1, true, false) == Phase::Fired);
	claim("a timed-out detection does not become a fired one",
			step(Phase::TimedOut, 0.1, true, false) == Phase::TimedOut);
	claim("a cancelled detection does not become a fired one",
			step(Phase::Cancelled, 0.1, true, false) == Phase::Cancelled);

	// ---- device queries, whatever this machine happens to have ------------------------
	/*
		NOT AN ASSERTION ABOUT THE HARDWARE. What is checked is the CONTRACT -
		a pad is never a keyboard, the mouse is never a pad - because those hold
		on a machine with no devices at all, and "how many pads are plugged in"
		is a fact about the tester's desk. `[SOURCE]` CLAUDE.md: the sandbox's
		defaults are not the behaviour.
	*/
	{
		const std::vector<std::shared_ptr<GamepadDevice>> p = pads();
		bool anyKeyboard = false, anyMouse = false;
		for (const auto& d : p)
		{
			if (isKeyboard(d))
				anyKeyboard = true;
			if (d->unique_id().find("mouse") != std::string::npos)
				anyMouse = true;
		}
		claim("no keyboard is listed as a pad", !anyKeyboard);
		claim("no mouse is listed as a pad", !anyMouse);
		claim("the keyboard, if there is one, IS a keyboard",
				keyboard() == nullptr || isKeyboard(keyboard()));
		NOTICE_LOG(RENDERER, "REBIND SELFTEST: this machine has %u pad(s) and %s keyboard",
				(unsigned)p.size(), keyboard() != nullptr ? "a" : "no");
	}

	NOTICE_LOG(RENDERER, "REBIND SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace rebind
}	// namespace roll
