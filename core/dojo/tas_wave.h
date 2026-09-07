#pragma once
#include "types.h"
#include <string>
#include <utility>
#include <vector>

// TAS WAVEFORM (David, 2026-09-03): a per-movie-frame ENVELOPE of the game audio, so attack / hit cues can be read by
// eye while stepping with the sound muted.
//
// Samples are tapped in WriteSample (core/audio/audiostream.cpp) BEFORE the host volume is applied - the emulator
// volume and the OS mixer never see the tap - and keyed by dojo.frame_number, which increments on the same emulator
// loop (end of MapleApplyAction), so every sample lands on the frame it was synthesized in. onFrameEnd publishes the
// frame the moment its counter ticks, so a single Space step shows its own frame at once, not one step late.
// Fast-forward frames carry no samples at all (the AICA gate in sgc_if.cpp returns before WriteSample) and simply stay
// empty - David: gaps there are fine, he only fast-forwards boot and loading screens.
//
// Lifecycle mirrors the movie (session_inputs): a state load / an input edit does NOT truncate - frames from there
// are flagged STALE (an old take, drawn grey) and go live again as they are re-run. Dojo::Reset clears everything.
// Persistence: <clip>/audio.env (the whole timeline) and <state>.wave (the frames before a savestate, so an abandoned
// branch keeps its audio next to the state it belongs to).
namespace tas_wave
{
	static constexpr int BUCKETS = 16;	// ~1 ms slices of a ~735-sample frame

	struct FrameEnv
	{
		u8 flag = 0;			// 0 = never measured, 1 = live (this take), 2 = stale (an old take: rewound / edited before it)
		u8 peak[BUCKETS] = {};	// max |sample| per slice, 0..255 (mono mix of L and R)
		u8 rms[BUCKETS] = {};	// loudness per slice, 0..255
	};

	// Emulator loop
	void onSample(s16 l, s16 r);		// every synthesized stereo sample (audiostream.cpp WriteSample, pre-volume)
	void onFrameEnd(u32 frame);			// dojo.frame_number is about to leave <frame>: publish its envelope
	void onStateLoad(u32 frame);		// the movie rewound to <frame>: frames from there are an old take until re-run
	void markStaleFrom(u32 frame);		// an input edit at <frame>: the audio after it no longer matches until re-run
	void reset();						// game unload (Dojo::Reset)

	// GUI
	void snapshot(u32 lo, u32 hi, std::vector<FrameEnv>& out);	// out[i] = frame lo + i; never-measured frames come back flag 0
	u32 measuredFrames();										// frames holding a live or stale envelope
	u32 version();										// bumps on every store change - GUI caches key on it

	// Persistence (clip-scoped)
	bool saveClip(const std::string& dir);								// <dir>/audio.env - full rewrite, only measured frames
	bool loadClip(const std::string& dir);								// replaces the store with the file (false = no file)
	bool writeStateSnapshot(const std::string& statePath, u32 frame);	// <statePath>.wave - the frames leading up to <frame>
	bool readFile(const std::string& path, std::vector<std::pair<u32, FrameEnv>>& out);	// either file, frame-ascending
}
