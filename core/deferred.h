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
//
// WHAT YOU STILL CANNOT DO FROM HERE: run the machine forward more than one
// frame. `[MEASURED 2026-09-10]` docs/STEP-GRANULARITY.md - an action here may
// stop the emulator, start it, and watch dojo.frame_number (an atomic written on
// the emulation thread), and that DOES advance a frame. But exactly one: the
// emulation thread then needs the renderer, and the render thread is the one
// inside this action. One start with a 30-frame target advanced 1 frame in 10
// seconds; with a 3-frame target, also 1. Thirty separate starts advanced 30
// frames, at 94.11 ms each.
//
// It also requires rend.ThreadedRendering, which DEFAULTS TO FALSE: with no
// emulation thread, nothing here can make a frame happen at all (0 of 30).
//
// And it is not cheap: ~80-100 ms per frame, of which ~68 is emu.stop() and
// essentially all of THAT is the join - the emulation thread waiting on a
// renderer this thread is not running. The nvmem write stop() does every call
// is 0.3 ms, and audio is not a factor. There is nothing incidental to strip.
//
// And gui_open_step() cannot be borrowed for it. That sets a target frame and
// lets gui_display_osd() do the stopping - which runs LATER IN THIS SAME
// FUNCTION on this same thread, so an action here would have to return first.
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
