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
// Returns false and clears the request if the encoder could not be launched.
bool start(int width, int height);
void stop();

bool isRecording();
int width();
int height();
// Bytes in one RGB24 frame at the recording size.
size_t frameBytes();

// Hands a finished RGB24 frame to the writer thread. Takes ownership.
// Frames are dropped (and counted) if the encoder cannot keep up.
void submitFrame(std::vector<u8>&& rgb);

u64 framesWritten();
u64 framesDropped();
// Human-readable one-liner for the UI.
std::string status();
const std::string& outputPath();

}
