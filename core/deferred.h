#pragma once
#include <functional>

// Run something OUTSIDE both the ImGui frame and the emulation loop.
//
// WHY THIS EXISTS. Anything that stops the emulator has to run from such a
// place, and until now a script had nowhere to stand:
//
//   * a `vblank` callback runs ON the emulation thread inside the emulation
//     loop, and emu.stop() joins the thread it is called from - deadlock;
//   * a draw callback runs on the render thread but INSIDE an ImGui frame, and
//     stopping there wedges the emulator: the stop is taken and no frame is
//     ever presented again.
//
// `[MEASURED 2026-09-07]` Both were measured while trying to restore a
// savestate with the loop quiesced, which is the prerequisite for running
// several copies of one machine in one process. The supported shape
// (gui_loadState, gui.cpp) is `emu.stop(); dc_loadstate(); emu.start();` and it
// works only because the hotkey path calls it from outside any frame.
//
// So: post an action, and it runs at the top of the next mainui_rend_frame -
// the same place gui_loadState() is already called from by the auto-seek block,
// which is the proof that stopping there is safe.
namespace deferred
{
	// Thread-safe. Actions run once, in the order posted, on the main thread.
	void post(std::function<void()> action);
	// Called by mainui_rend_frame. Runs everything queued and clears the queue.
	void drain();
	// True while drain() is running an action, so code that must only stop the
	// emulator from a safe place can check rather than assume.
	bool inDrain();
	// Drop anything queued - game teardown, where a pending action would run
	// against a machine that no longer exists.
	void clear();
}
