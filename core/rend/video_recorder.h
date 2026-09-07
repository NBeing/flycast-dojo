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
#pragma once
#include "types.h"
#include <string>
#include <vector>

/*
 * Video capture of the *presented* frame.
 *
 * Frames are read back from the default framebuffer immediately before the
 * buffer swap, so whatever the user sees is what gets recorded: the game
 * image, the OSD, and anything Lua draws through the "overlay" event
 * (see lua::overlay() / core/rend/gui.cpp).
 *
 * This unit is renderer-agnostic. It owns the encoder pipe and the writer
 * thread; a renderer backend is responsible for producing RGB24 frames and
 * handing them over via submitFrame(). The OpenGL backend does this in
 * do_swap_capture() (core/rend/gles/gles.cpp).
 */
namespace videorec
{

// Pixel layout submitted by a backend. Each maps to an ffmpeg -pix_fmt, so
// backends hand over their native swapchain layout and the encoder converts
// instead of the CPU.
enum class PixelFormat
{
	RGB24,		// OpenGL glReadPixels(GL_RGB)
	RGBA32,		// Vulkan eR8G8B8A8Unorm
	BGRA32,		// D3D9 X8R8G8B8, DXGI B8G8R8A8
};

int bytesPerPixel(PixelFormat format);

// --- Requests. Safe to call from any thread (UI, Lua). ---------------------
// The renderer applies these on its next frame, where the framebuffer
// dimensions are known.
void requestStart(const std::string& path);
void requestStop();
void toggle();

// --- Renderer side. ---------------------------------------------------------
bool startPending();
bool stopPending();
// Opens the encoder at the given size using the path stashed by requestStart().
// `flipVertically` is for backends whose framebuffer origin is bottom-left
// (OpenGL); D3D and Vulkan submit top-down rows and pass false.
// Returns false and clears the request if the encoder could not be launched.
bool start(int width, int height, PixelFormat format, bool flipVertically);
void stop();

bool isRecording();
int width();
int height();
// Bytes in one frame at the recording size and format.
size_t frameBytes();

// Hands a finished frame to the writer thread, in the format passed to
// start(). Takes ownership.
//
// The output is constant frame rate: one submitted frame is always one frame
// in the file, so frame N of the recording is emulated frame N. If the
// encoder falls behind, the frame's *contents* are dropped but its slot is
// preserved by repeating the previous frame, because losing a slot would
// shorten the video against the audio and desync everything after it. Set
// [record] blockonfull=yes to stall the caller instead and keep every frame's
// real contents, at the cost of frame pacing.
void submitFrame(std::vector<u8>&& frame);

// --- Audio ------------------------------------------------------------------
// Interleaved stereo s16 at AudioSampleRate, as produced by the AICA mixer.
// Called from the emulation thread via WriteSample(); a no-op when not
// recording. Audio is not emitted during rollback (muteAudio short-circuits
// the mixer), so no de-duplication is needed here.
constexpr int AudioSampleRate = 44100;
constexpr int AudioChannels = 2;

void submitAudio(const void *interleavedStereoS16, int frameCount);

u64 framesWritten();
u64 framesDropped();
// Human-readable one-liner for the UI.
std::string status();
const std::string& outputPath();

}
