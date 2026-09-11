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
#include "input/hold_repeat.h"
#include "emulator.h"
#include "cfg/option.h"
#include <chrono>
#include <algorithm>
#include "cfg/cfg.h"
#include "dojo/dojo.h"
#include "hw/pvr/Renderer_if.h"
#include "gui.h"
#include "oslib/oslib.h"
#include "wsi/context.h"
#include "cfg/option.h"
#include "emulator.h"
#include "cfg/option.h"
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

/*
	WHAT GRANULARITY CAN A COUNTERFACTUAL BE DRIVEN AT, AND WHAT DOES IT COST?

	`dojo:StepProbe=N` - run N synchronous frame advances from the deferred
	point and report the time each took. Off unless set.

	THE QUESTION IT SETTLES. The pool/rollout work needs to run a machine
	forward by a known amount from a stopped state, and the plan for it recorded
	the granularity as UNSETTLED: flycast's Lua has no step binding, and
	`Emulator::step()` is one SH4 INSTRUCTION on the debugger's path.

	The tree does have a frame advance - `gui_open_step()` - but it is not a
	step primitive and cannot be reused here. It sets `dojo.target_step_frame`,
	releases the machine, and a LATER pass through `gui_display_osd()` stops it
	when the counter arrives. That is asynchronous and render-thread-driven, and
	this code runs at the top of the very function that would do the stopping.

	A synchronous advance does not need it, and the reason is one fact worth
	checking rather than reasoning about: `dojo.frame_number` is a
	`std::atomic<u32>` incremented in `Dojo::MapleApplyAction`, which runs on
	the EMULATION thread. So start the machine, watch the counter from here,
	and stop - two threads, no dependency, no deadlock. The render thread simply
	presents nothing while it waits, which for a bounded rollout is the cost
	rather than a fault.

	THE TIMEOUT IS THE POINT. If the counter never moves this reports a timeout
	instead of hanging - which is also what a wrong premise would look like, and
	is the only way this probe can tell us it is wrong.
*/
static void stepProbe()
{
	const int want = cfgLoadInt("dojo", "StepProbe", 0);
	if (want <= 0)
		return;
	static bool done = false;
	// Wait for a movie that is actually running: advancing a machine that has
	// nothing to advance would measure the timeout path and call it a cost.
	if (done || dojo.frame_number.load() < 120 || dojo.session_inputs.empty())
		return;
	done = true;

	const bool wasRunning = emu.running();
	double worst = 0.0, total = 0.0;
	int advanced = 0, timedOut = 0;
	// WHERE THE TIME GOES, split three ways. The whole-advance number alone
	// says a rollout runs at ~5.6x real time; it does not say whether that is
	// the emulator or the scaffolding around it, and those have opposite
	// implications. `Emulator::start` launches a std::async whose lambda calls
	// InitAudio(), and `stop` joins it after TermAudio() - so a full audio
	// teardown and re-init happens per advance, for one frame of sound.
	double tStart = 0.0, tFrame = 0.0, tStop = 0.0;

	/*
		BATCH MODE, dojo:StepProbeBatch=yes - one start, N frames, one stop.

		The per-frame form below pays a THREAD LAUNCH AND JOIN for every frame
		(`Emulator::start` spawns a std::async, `stop` joins it), and its own
		source carries "FIXME single thread is better" for the instruction-level
		twin of the same problem. A rollout advances many frames from one state,
		so the interesting number is the MARGINAL cost of a frame, not the cost
		of a frame plus a thread.
	*/
	if (cfgLoadBool("dojo", "StepProbeBatch", false))
	{
		if (emu.running())
			emu.stop();
		const u32 from = dojo.frame_number.load();
		const auto t0 = std::chrono::steady_clock::now();
		try { emu.start(); } catch (...) { return; }
		u32 now = from;
		while (now - from < (u32)want)
		{
			now = dojo.frame_number.load();
			if (std::chrono::duration<double>(
					std::chrono::steady_clock::now() - t0).count() > 10.0)
				break;
		}
		emu.stop();
		const double ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - t0).count();
		if (wasRunning)
			try { emu.start(); } catch (...) {}
		const int got = (int)(now - from);
		NOTICE_LOG(COMMON, "STEP PROBE BATCH: threaded=%s %d/%d frames in %.2f ms"
				"  => %.2f ms/frame  => %s",
				config::ThreadedRendering ? "yes" : "no", got, want, ms,
				got > 0 ? ms / got : 0.0, got == want ? "PASS" : "FAIL");
		return;
	}

	for (int i = 0; i < want; i++)
	{
		if (emu.running())
			emu.stop();
		const u32 from = dojo.frame_number.load();
		const auto t0 = std::chrono::steady_clock::now();
		try { emu.start(); } catch (...) { break; }
		const auto tA = std::chrono::steady_clock::now();
		if (i == 0)
			// THE INSTRUMENT, ONCE. A start that did not take and a machine
			// that cannot complete a frame both look like "the counter never
			// moved", and only one of them is a fact about the threading.
			NOTICE_LOG(COMMON, "STEP PROBE: after start, running=%s frame=%u",
					emu.running() ? "yes" : "no", dojo.frame_number.load());
		u32 now = from;
		while (now == from)
		{
			now = dojo.frame_number.load();
			if (std::chrono::duration<double>(
					std::chrono::steady_clock::now() - t0).count() > 0.5)
				break;			// bounded: a wrong premise reports, it does not hang
		}
		const auto tB = std::chrono::steady_clock::now();
		emu.stop();
		const auto tC = std::chrono::steady_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(tC - t0).count();
		if (now == from)
			timedOut++;
		else
		{
			advanced++;
			total += ms;
			worst = std::max(worst, ms);
			tStart += std::chrono::duration<double, std::milli>(tA - t0).count();
			tFrame += std::chrono::duration<double, std::milli>(tB - tA).count();
			tStop  += std::chrono::duration<double, std::milli>(tC - tB).count();
		}
	}
	if (wasRunning)
		try { emu.start(); } catch (...) {}
	NOTICE_LOG(COMMON, "STEP PROBE: threaded=%s %d/%d frames advanced, %d timed out, "
			"mean %.2f ms, worst %.2f ms  => %s",
			config::ThreadedRendering ? "yes" : "no",
			advanced, want, timedOut, advanced > 0 ? total / advanced : 0.0, worst,
			(advanced == want && timedOut == 0) ? "PASS" : "FAIL");
	if (advanced > 0)
		NOTICE_LOG(COMMON, "STEP PROBE SPLIT: start %.2f ms + frame %.2f ms + stop %.2f ms"
				" (mean per advance)",
				tStart / advanced, tFrame / advanced, tStop / advanced);
}

/*
	DOES gui_loadState() ACTUALLY LOAD FROM HERE, OR IS IT REFUSED?

	`dojo:LoadProbe=gui` or `=raw`, fires once. Off unless set.

	THE QUESTION. core/lua/lua.cpp records four ways of loading a savestate from
	this point that all WEDGE, including `emu.stop(); dc_loadstate(); start();`
	- and concludes that the same call is fine via gui_loadState(), so the
	difference must be the path.

	But gui_loadState() opens with `if (gui_state == GuiState::Closed &&
	savestateAllowed())` and has no else. In any other state it does NOTHING,
	and DOING NOTHING DOES NOT WEDGE. "It worked" and "it was refused" are the
	same evidence from outside, which is a defect this tree has shipped four
	times (CLAUDE.md doctrine rule 1). So the comparison has to assert the state
	it ran in before it means anything.

	This probe reports, for either path: the GuiState it ran in, whether the
	guard would have refused, and the frame number BEFORE and AFTER. A load that
	took moves the frame number; a refusal does not; a wedge never reaches the
	second log line at all, which is the third outcome the earlier note could
	not distinguish.
*/
static void loadProbe()
{
	const std::string mode = cfgLoadStr("dojo", "LoadProbe", "");
	if (mode.empty())
		return;
	static int  stage = 0;
	static std::chrono::steady_clock::time_point loadedAt;
	static u32  afterLoad = 0;

	// STAGE 2: DID THE MACHINE KEEP RUNNING? That is what "wedge" means, and
	// returning from the call does not answer it. Reported a full second later,
	// from a drain that only happens if the render thread is still turning.
	if (stage == 1)
	{
		if (std::chrono::duration<double>(
				std::chrono::steady_clock::now() - loadedAt).count() < 1.0)
			return;
		stage = 2;
		const u32 now = dojo.frame_number.load();
		NOTICE_LOG(COMMON, "LOAD PROBE: 1 s later frame %u -> %u  => %s",
				afterLoad, now, now != afterLoad ? "STILL RUNNING" : "WEDGED");
		return;
	}
	// GATE WELL PAST THE SAVESTATE'S OWN FRAME. `[MEASURED 2026-09-10]` an
	// earlier gate of 300 fired while the machine was still sitting on the
	// frame AutoSeekState had just loaded, so a successful load and a refusal
	// both read as "NO CHANGE" - the fixture could not discriminate.
	if (stage != 0 || dojo.frame_number.load() < 10400 || dojo.session_inputs.empty())
		return;
	stage = 1;

	// THE SLOT IS ITS OWN KNOB. `[MEASURED 2026-09-10]` passing
	// `config:Dreamcast.SavestateSlot=7` on the command line does NOT reach
	// here: Replay::Init does `cfgSetVirtual("config", "Dreamcast.SavestateSlot",
	// "0")` on every replay boot, deliberately, so a clip always opens on BASE.
	// Two probe arms meant to load different states therefore ran the identical
	// configuration and produced identical passing output. Only the `slot=` in
	// the line below made that visible.
	const int slot = cfgLoadInt("dojo", "LoadProbeSlot", (int)config::SavestateSlot);
	const u32  before = dojo.frame_number.load();
	const int  state  = (int)gui_state;
	// The guard's own two conditions, read HERE so the report says whether the
	// path under test would even have run - not inferred from the outcome.
	const bool closed = gui_state == GuiState::Closed;
	NOTICE_LOG(COMMON, "LOAD PROBE: mode=%s guistate=%d closed=%s slot=%d frame=%u"
			" - about to try", mode.c_str(), state, closed ? "yes" : "no",
			slot, before);

	if (mode == "gui")
	{
		config::SavestateSlot.set(slot);	// gui_loadState reads the option
		gui_loadState();
	}
	else
	{
		try {
			emu.stop();
			dc_loadstate(slot);
			emu.start();
		} catch (const std::exception& e) {
			NOTICE_LOG(COMMON, "LOAD PROBE: raw threw - %s", e.what());
		}
	}
	// REACHING THIS LINE AT ALL is the first result; the frame number is the
	// second. A wedge prints the line above and never this one.
	afterLoad = dojo.frame_number.load();
	loadedAt  = std::chrono::steady_clock::now();
	NOTICE_LOG(COMMON, "LOAD PROBE: returned. frame %u -> %u  => %s",
			before, afterLoad, afterLoad != before ? "LOADED" : "NO CHANGE");
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
	/*
		THE SCRUB'S REPEATS. The dispatch presses and releases the hold; only
		this loop knows time has passed.

		HERE rather than in the emulation thread: gui_open_step() is the same
		call the hotkey makes, and it is safe from this point for the same
		reason everything else in this function is - outside the ImGui frame and
		outside the emulation loop.

		COUNTED, NOT ONE PER FRAME. tick() answers how many repeats came due,
		so a slow frame owes the ones it missed instead of silently halving the
		scrub rate.
	*/
	{
		const int due = hotkeys::stepHold().tick(os_GetSeconds());
		if (due > 0)
		{
			if (cfgLoadBool("dojo", "HotkeyTrace", false))
				NOTICE_LOG(INPUT, "HOTKEY STEP: scrub +%d", due);
			for (int i = 0; i < due; i++)
				gui_open_step();
		}
	}

	stepProbe();		// dojo:StepProbe=N - off unless set
	loadProbe();		// dojo:LoadProbe=gui|raw - off unless set

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
