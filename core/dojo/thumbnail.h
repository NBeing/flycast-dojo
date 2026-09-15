#pragma once
// `[PORTED 2026-09-15]` from reference/flycast-rr @ edca8915, byte-identical modulo
// the David->dev comment rule. His GetLastFrameRGB is DX9/DX11-only; the GL half
// was added here (core/rend/gles/gles.h) so thumbnails also work under GL/llvmpipe,
// which is where the tests run. captureForState is wired from gui_saveState.
#include "types.h"
#include <string>

// TAS savestate thumbnails: a small PNG written next to each savestate as "<statePath>.png",
// mirroring the existing "<statePath>.frame" sidecar convention, so it lives in the clip folder
// and travels with state backups. The F4 slot grid displays them.
namespace tas_thumb
{
	// Grab the last rendered frame and write the thumbnail for this savestate. MUST be called on
	// the render thread with the emulator stopped (gui_saveState is exactly that spot) - never
	// from dc_savestate, which is also reachable from the emu thread. The GPU readback happens
	// inline; the PNG encode is handed to a worker thread. No-op if the renderer can't read back.
	void captureForState(const std::string& statePath);

	// Wait for a pending PNG encode (called at shutdown so a save-then-quit still lands).
	void flush();
}
