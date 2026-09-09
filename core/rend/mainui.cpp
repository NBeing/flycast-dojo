/*
	Copyright 2020 flyinghead

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

#include "mainui.h"
#include "deferred.h"
#include "cfg/cfg.h"
#include "dojo/dojo.h"
#include "hw/pvr/Renderer_if.h"
#include "gui.h"
#include "oslib/oslib.h"
#include "wsi/context.h"
#include "cfg/option.h"
#include "emulator.h"
#include "imgui_driver.h"
#include "profiler/fc_profiler.h"
#include "video_recorder.h"

#include <atomic>
#include <chrono>
#include <thread>

static bool mainui_enabled;

// THE HEADLESS ONE-SHOTS, AND WHY THEY ARE NOT FUNCTION-LOCAL ANY MORE.
//
// A replay boots PAUSED and headless has nobody to un-pause it, so these fire
// exactly once per game to un-pause and to seek. As function-local statics they
// fired once per PROCESS, which was indistinguishable until a game could be
// restarted in one - and now one can (lua emulator.restartLater).
//
// `[MEASURED 2026-09-08]` the first in-process restart booted correctly and then
// emulated nothing for two minutes. Boot 1 logged "auto-play -> un-pausing the
// replay"; boot 2 could not, because the latch was already spent, so the second
// machine sat at GuiState::Paused forever. Every other trace was identical up to
// that line, which is why it read as a wedged restart rather than as a latch.
//
// This is the same class of defect the cold-boot-twice probe exists to hunt -
// state that survives an init because nothing had ever re-inited before - found
// in the harness rather than in the emulated machine.
static bool autoPlayDone = false;
static bool autoSeekDone = false;

static void resetHeadlessOneShots(Event, void *)
{
	if (autoPlayDone || autoSeekDone)
		NOTICE_LOG(NETWORK, "TAS TEST: game terminated - re-arming auto-play/auto-seek");
	autoPlayDone = false;
	autoSeekDone = false;
}
u32 MainFrameCount;
static bool forceReinit;

void UpdateInputState();

std::atomic<bool> display_refresh(false);

void display_refresh_thread()
{
	auto dispStart = std::chrono::steady_clock::now();
	long long period = 16683; // Native NTSC/VGA by default
	while(1)
	{
		// Native NTSC/VGA
		if (config::FixedFrequency == 2 ||
			(config::FixedFrequency == 1 &&
				(config::Cable == 0 || config::Cable == 1)) ||
			(config::FixedFrequency == 1 && config::Cable == 3 &&
				(config::Broadcast == 0 || config::Broadcast == 4)))
			period = 16683; // 1/59.94
		// Approximate VGA
		else if (config::FixedFrequency == 3)
			period = 16666; // 1/60
		// PAL
		else if (config::FixedFrequency == 4 ||
				 (config::FixedFrequency == 1 && config::Cable == 3))
			period = 20000; // 1/50
		// Half Native NTSC/VGA
		else if (config::FixedFrequency == 5)
			period = 33333; // 1/30

		auto now = std::chrono::steady_clock::now();
		long long duration = std::chrono::duration_cast<std::chrono::microseconds>(now - dispStart).count();
		if (duration > period)
		{
			display_refresh.exchange(true);
			dispStart = now;
		}
	}
}

void start_display_refresh_thread()
{
	std::thread t1(&display_refresh_thread);
	t1.detach();
}

bool mainui_rend_frame()
{
	FC_PROFILE_SCOPE;

	os_DoEvents();
	UpdateInputState();

	// Actions that must run outside the ImGui frame and outside the emulation
	// loop. This is the same point gui_loadState() is called from by the
	// auto-seek block below, which is the evidence that stopping the emulator
	// here is safe - see core/deferred.h for what is not.
	deferred::drain();

	// TAS test harness: -config dojo:AutoSeekState=N automates the
	// "Play a Movie -> F3" step. Once playback is actually running (~2 s in),
	// load state slot N exactly once; the movie then seeks to that state's
	// frame, exactly as pressing F3 would. This is what lets a harness drive
	// the full seek-and-play loop headlessly. Unset (-1) means never.
	//
	// Ported from the TAS fork. It is pure engine - no piano roll, no States
	// window - which is why it can come across while the rest of that UI
	// cannot.
	// A REPLAY BOOTS PAUSED, and headless has nobody to un-pause it.
	//
	// `[SOURCE]` core/rend/gui.cpp, gui_open_pause() -- it is a TOGGLE:
	//   `else if (gui_state == GuiState::Paused) { ... dojo.buffering = false;
	//    dojo.manual_pause = false; gui_setState(GuiState::Closed);
	//    emu.start(); }`
	// bound to EMU_BTN_PAUSE, which keyboard_device.h maps to ',' (54).
	// That is dojo's playback control, and it is the same one rollback
	// playback uses.
	//
	// `[MEASURED 2026-09-07]` Without this, a harness-driven replay sits at
	// `play_match=1 gui_state=22 (Paused) frame=0` forever. Every "successful"
	// headless capture I produced was ~2800 frames of a still SEGA logo with
	// the OSD reading "Stepping 0 / 2640" - a file of the right size and
	// duration containing nothing. AutoSeekState could not fire either, since
	// it waits for GuiState::Closed and frame > 120.
	//
	// Gated on a harness flag rather than on play_match alone: a human opening
	// a replay should still get the paused-on-frame-0 behaviour they expect.
	// `dojo:AutoPlay` says PLAY, and only that. AutoSeekState and AutoCapture
	// both imply it, which was fine while every harness wanted one of them -
	// but a record/replay round trip wants neither: its clip has no savestate
	// to seek to and it is not capturing. `[MEASURED 2026-09-08]` without this
	// the replay half sat on frame 0 for three minutes and logged nothing at
	// all, because "paused" and "wedged" look identical from outside.
	if (!autoPlayDone && dojo.play_match && gui_state == GuiState::Paused
			&& (cfgLoadBool("dojo", "AutoPlay", false)
				|| cfgLoadInt("dojo", "AutoSeekState", -1) >= 0
				|| cfgLoadBool("dojo", "AutoCapture", false)))
	{
		autoPlayDone = true;
		NOTICE_LOG(NETWORK, "TAS TEST: auto-play -> un-pausing the replay (no hotkey headless)");
		gui_open_pause();
	}

	// A MOVIE THAT DOES NOT START AT POWER-ON CANNOT BE REPLAYED HEADLESSLY,
	// and this is where the attempt to support it was removed rather than left
	// half-working. `[MEASURED 2026-09-08]` seeking such a movie before playback
	// lands the state correctly and then never advances a frame; three separate
	// patches here did not change that.
	//
	// It is not an oversight in the seek - it is the replay model. This engine
	// records NETPLAY MATCHES: the .flyr header carries Player, Opponent, Quark
	// and Relay Key, and at least six sites compare a frame number against
	// session_inputs.size() because a match recording is dense from frame 0.
	// core/rend/gui.cpp:769 names the assumption outright. A movie that starts
	// at frame 9948 violates it everywhere at once.
	//
	// The studio never makes one: "Movies record from power-on (frame 0);
	// savestate-seek is a bookmark into that timeline" (CLAUDE.md). Only Lua's
	// replay.startRecording() can, and its own docstring already warns the clip
	// "needs a savestate to be replayable". That gap is real and recorded in
	// TODOS.md; it is not closed by pretending the seek path handles it.
	if (!autoSeekDone && dojo.play_match
			// PAUSED ONLY for the mid-session branch, and that is ordering, not
			// taste: auto-play above fires on exactly the same condition and is
			// checked first, so restricting to Paused guarantees the movie has
			// been un-paused before it is seeked. `[MEASURED 2026-09-08]`
			// allowing Closed here let the seek fire on an earlier pass than
			// auto-play could, which set its one-shot, left the session paused
			// forever, and produced a seek with no playback after it.
			// ORDERED ON autoPlayDone, not on a GuiState, because the two are
			// not the same question and the state cannot express this one.
			// `[MEASURED 2026-09-08]` gui_open_pause() changes gui_state
			// SYNCHRONOUSLY, so by the time this line is reached on the pass
			// where auto-play fired, the state is already Closed - a
			// Paused-only branch can never run after it, and a branch allowing
			// Closed runs BEFORE it and leaves the session paused forever.
			// Both failures look identical from outside: a seek with no
			// playback, or playback with no seek.
			&& gui_state == GuiState::Closed && dojo.frame_number > 120)
	{
		autoSeekDone = true;	// one-shot either way, which also stops the per-frame cfg poll
		const int autoSlot = cfgLoadInt("dojo", "AutoSeekState", -1);
		if (autoSlot >= 0)
		{
			config::SavestateSlot.set(hostfs::clampSavestateSlot(autoSlot));
			NOTICE_LOG(NETWORK, "TAS TEST: auto-seek -> loading state slot %d", (int)config::SavestateSlot);
			gui_loadState();

			// Auto-capture starts HERE rather than at Replay::Init when a seek
			// is configured, so the recording begins at the seek target instead
			// of carrying the boot and the pre-seek stretch. Replay::Init skips
			// arming for exactly this case; see the comment there.
			if (cfgLoadBool("dojo", "AutoCapture", false) && !videorec::isRecording())
			{
				NOTICE_LOG(NETWORK, "TAS TEST: auto-capture -> starting recorder after the seek");
				videorec::requestStart("");
			}
		}
	}

	if (gui_is_open() || gui_state == GuiState::VJoyEdit)
	{
		gui_display_ui();
		// TODO refactor android vjoy out of renderer
		if (gui_state == GuiState::VJoyEdit && renderer != nullptr)
			renderer->DrawOSD(true);
#ifndef TARGET_IPHONE
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif
	}
	else
	{
		try {
			if (!emu.render())
				return false;
			if (config::ProfilerEnabled && config::ProfilerDrawToGUI)
				gui_display_profiler();
		} catch (const FlycastException& e) {
			gui_stop_game(e.what());
			return false;
		}
	}
	MainFrameCount++;

	return true;
}

void mainui_init()
{
	// Event::Terminate is fired by Emulator::unloadGame, which every game start
	// runs first - so the one-shots re-arm for the next machine whether it was
	// started by the menu, the command line, or a restart.
	EventManager::listen(Event::Terminate, resetHeadlessOneShots);
	if (!rend_init_renderer()) {
		ERROR_LOG(RENDERER, "Renderer initialization failed");
		gui_error("Renderer initialization failed.\nPlease select a different graphics API");
	}
}

void mainui_term()
{
	// Close the encoder before the GL context goes away, so quitting mid-capture
	// still leaves a playable file rather than a truncated one.
	videorec::stop();
	rend_term_renderer();
}

void mainui_loop()
{
	mainui_enabled = true;
	mainui_init();
	RenderType currentRenderer = config::RendererType;

	if (config::FixedFrequency != 0)
		start_display_refresh_thread();

	while (mainui_enabled)
	{
		fc_profiler::startThread("main");

		if (mainui_rend_frame())
		{
			if (config::FixedFrequency != 0 &&
				!gui_is_open() &&
				!settings.input.fastForwardMode)
			{
				while (!display_refresh.load());
				display_refresh.exchange(false);
			}
		}

		if (imguiDriver == nullptr)
			forceReinit = true;
		else
			imguiDriver->present();

		if (config::RendererType != currentRenderer || forceReinit)
		{
			mainui_term();
			int prevApi = isOpenGL(currentRenderer) ? 0 : isVulkan(currentRenderer) ? 1 : currentRenderer == RenderType::DirectX9 ? 2 : 3;
			int newApi = isOpenGL(config::RendererType) ? 0 : isVulkan(config::RendererType) ? 1 : config::RendererType == RenderType::DirectX9 ? 2 : 3;
			if (newApi != prevApi || forceReinit)
				switchRenderApi();
			mainui_init();
			forceReinit = false;
			currentRenderer = config::RendererType;
		}

		fc_profiler::endThread(config::ProfilerFrameWarningTime);
	}

	mainui_term();
}

void mainui_stop()
{
	mainui_enabled = false;
}

void mainui_reinit()
{
	forceReinit = true;
}
