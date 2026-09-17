/*
	THE REBIND ENGINE - press a key, and that key is the binding.

	`[PORTED 2026-09-14]` from the TAS fork's rebind layer (reference/flycast-rr
	@ edca8915). The port map called this "island 4" and estimated ~150 lines;
	measured it is ~261 for the engine core and 388 with its query helpers, so
	it was the most undercounted of the four.

	It is the thing `core/dojo/hotkey_panel.cpp` has been waiting for. That file
	has said so since it landed: the cheat sheet reads bindings live from the
	device's own mapping "so this display can never drift from reality - and a
	future remap UI edits the same mapping". This is that UI's engine, and it
	writes to exactly the mapping the cheat sheet reads.

	ARMING DETECTION IS ALSO WHAT PROTECTS THE EMULATOR, and this is the part
	worth understanding before touching it. A device in detect mode consumes the
	press inside `gamepad_btn_input` / `gamepad_axis_input` and returns before
	it reaches the emulator. So "press a key to bind it" can never ALSO step a
	frame, fire a hotkey or move the guest - the emulator simply never sees the
	press. That is not a nicety; without it, binding a key to Frame Advance
	would advance a frame while you bound it.

	ONE KIND OF DEVICE IS ARMED AT A TIME, deliberately. While waiting for a
	GAMEPAD press the keyboard stays live, so Escape can still cancel. When the
	keyboard itself is the target, Escape arrives as an ordinary keycode and is
	treated as cancel.
*/
#pragma once
#include "types.h"
#include "input/gamepad.h"
#include <memory>
#include <string>
#include <vector>

class GamepadDevice;

namespace roll {
namespace rebind {

/*
	WHAT A DETECTION IS DOING, as a value.

	SEPARATED FROM THE DEVICES ON PURPOSE. Everything interesting about a
	rebind - when it gives up, what beats what when two things happen on the
	same frame - is a decision about time and events, and needs no gamepad to
	be made or to be checked. The fork makes these decisions inline inside the
	window body, where nothing can reach them.
*/
enum class Phase : u8
{
	Idle,			//!< nothing armed
	Waiting,		//!< armed, waiting for a press
	Fired,			//!< a press arrived and should be written
	TimedOut,		//!< nobody pressed anything in time
	Cancelled,		//!< Escape, or something else took the input
};

//! How long a detection waits before giving up. The fork's value, kept.
constexpr double kTimeout = 6.0;

/*
	The whole state machine, as a pure function.

	PRECEDENCE IS THE POINT, and it is why this is a function rather than a
	chain of ifs at a call site. All three inputs can be true on one frame: a
	press can arrive on the same frame Escape is held, on the same frame the
	clock runs out. The order below is the decision, said once:

	  cancelled  beats everything - the user asked to stop, and a binding
	             written after that is a binding they did not ask for.
	  fired      beats the timeout - the press happened BEFORE we noticed the
	             clock, so honouring the clock would throw away a real press.
	  timeout    last.

	Anything not Waiting is returned unchanged: a resolved detection stays
	resolved until something explicitly re-arms it.
*/
Phase step(Phase now, double elapsed, bool fired, bool cancelled);

// ---------------------------------------------------------------------------------------
// The live side. These touch real devices.
// ---------------------------------------------------------------------------------------

bool isKeyboard(const std::shared_ptr<GamepadDevice>& dev);

//! The keyboard that can actually be mapped, or nullptr.
std::shared_ptr<GamepadDevice> keyboard();

/*
	Every connected, remappable pad.

	A MOUSE IS NOT A PAD. The fork filters `unique_id()` containing "mouse"
	because "Default Mouse (P0)" otherwise appears as a bindable device and
	offers to put a hotkey on a mouse button that the emulator will never
	deliver as one.
*/
std::vector<std::shared_ptr<GamepadDevice>> pads();

/*
	Arm `target` to capture the next press and bind it to `id`.

	Cancels any detection already running first, so clicking a second cell
	while the first is waiting moves the arming rather than arming twice.
*/
void arm(DreamcastKey id, const std::shared_ptr<GamepadDevice>& target);

//! Stop waiting and bind nothing.
void cancel();

/*
	Advance the detection and, if a press arrived, WRITE IT.

	Called once per frame from the panel. It is here rather than in the
	callback because the callback runs on the input thread: it may only record
	what happened, and the mapping write has to happen somewhere the UI owns.
*/
void tick();

bool active();
Phase phase();
DreamcastKey pendingKey();
double remaining();				//!< seconds left, for the countdown
std::string targetName();		//!< which device this rebind will land on

//! Unbind `id` on `dev` entirely - both a button and an axis, if it has both.
void clearBinding(DreamcastKey id, const std::shared_ptr<GamepadDevice>& dev);

/*
	What `dev` has bound to `id`, as text: a key name, an axis with its
	direction, or empty when nothing is bound.

	EMPTY IS A REAL ANSWER and the caller must render it as one, because an
	empty cell reads as a row that failed to load rather than as a free slot.
	`[CORRECTED 2026-09-14]` an earlier version of this note claimed every TAS
	action ships unbound; `core/input/keyboard_device.h` defaults several
	(Shift+F4, Shift+F5), and a fresh config shows six bound.
*/
std::string bindingName(const std::shared_ptr<GamepadDevice>& dev, DreamcastKey id);

//! Gated on `dojo:PanelSelfTest`. One line per claim; no devices, no frame.
void selfTest();

//! `dojo:RebindProbe=write|nosave|verify` - the cross-process persistence journey
//! (rebind survives a restart). Off by default; ticked once devices are up.
void probeTick();

}	// namespace rebind
}	// namespace roll
