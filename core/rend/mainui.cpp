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

	// TAS test harness: -config dojo:AutoSeekState=N automates the
	// "Play a Movie -> F3" step. Once playback is actually running (~2 s in),
	// load state slot N exactly once; the movie then seeks to that state's
	// frame, exactly as pressing F3 would. This is what lets a harness drive
	// the full seek-and-play loop headlessly. Unset (-1) means never.
	//
	// Ported from the TAS fork. It is pure engine - no piano roll, no States
	// window - which is why it can come across while the rest of that UI
	// cannot.
	static bool autoSeekDone = false;
	if (!autoSeekDone && dojo.play_match && gui_state == GuiState::Closed && dojo.frame_number > 120)
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
