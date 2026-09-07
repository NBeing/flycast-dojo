#pragma once
#include "types.h"
#include <string>
#include <vector>

// SKIP MAP for the piano roll REL ruler (David, 2026-09-03). MvC2 skips game-logic frames on a fixed cadence
// (the SKIP_RATE / SKIP_COUNT / SKIP_TOGGLE bytes in guest RAM - see mvc2.h). Wait-for-Frameskip already reads
// the toggle at the maple poll; this remembers the reading for EVERY frame so the roll can mark rows x (skip
// frame) or o and count skips between any two rows. The sample taken at frame N's poll reflects the game state
// after frame N-1 ran, so the roll shifts the display by dojo:FrameskipOffset (skip+N, default 1) - the same
// knob the live send aligns with. Re-running a frame overwrites its sample (last-write-wins, like inputs); a row
// insert or delete erases from the first changed frame (those frames have not run yet). Persisted as
// <clip>/skip.map, saved with the clip and at every state save, loaded with the clip.
namespace tas_ruler
{
	struct SkipSample
	{
		u8 seen = 0;	// 0 = never polled
		u8 skip = 0;	// 1 = the toggle read 255 at this poll (the reset / skip frame)
		u8 count = 0;	// SKIP_COUNT (counts down to the skip frame)
		u8 rate = 0;	// SKIP_RATE (6 normal, 4 turbo, 2 turbo2; 0 = idle: menus, scene resets)
	};

	void onPoll(u32 frame);			// emulator loop, once per maple poll (Dojo::MapleApplyAction)
	void onPausePeek(u32 frame);		// GUI, while PAUSED on <frame>: file the bytes the SKIP badge shows under that frame
	void eraseFrom(u32 frame);		// row surgery: frames from here have not run yet
	void reset();					// game unload
	void snapshot(u32 lo, u32 hi, std::vector<SkipSample>& out);	// GUI copy, out[i] = frame lo + i
	u32 seenFrames();
	u32 version();					// bumps on every change - the roll caches its prefix counts on it

	bool saveClip(const std::string& dir);	// <dir>/skip.map (no-op without a dir or without samples)
	bool loadClip(const std::string& dir);	// replaces the store (false = no file)
}
