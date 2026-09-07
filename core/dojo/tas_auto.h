#pragma once
#include "types.h"
#include <vector>

// ---- tas_auto: the shared Hold / Auto-fire engine ---------------------------------
//
// One small library both input regimes reuse, so hold/turbo is not reimplemented per
// module:
//   - LIVE overlay  - the injection path consults overlayCanon() every frame and ORs
//                     the held / auto-firing bits into that frame's input, per player.
//                     This is what lets one player be pinned (e.g. P2 holds Right to
//                     walk forward or block) while your hands stay on the other
//                     character. LIVE ONLY - the caller gates it off during replay so
//                     recorded .flyr never desync; the caller bakes it into the
//                     recording by applying it before the .flyr append.
//   - BAKED frames  - expand() writes the pattern into concrete movie frames at
//                     authoring time (piano roll). No runtime overlay.
//
// There is no separate Hold vs Auto MODE: an input is simply armed at an Hz.
//   hz 0   = off
//   hz >= 60 = HOLD (pressed every frame)
//   hz < 60  = auto-fire (one frame on, 60/hz - 1 off). Clean divisors: 30 20 15 12 10.
//
// Pure canon (the 11-bit tas_macro::CANON_* set, bit indices 0..10 = U D L R / LP HP LK
// HK / Start / A1 A2) - directions AND buttons. The canon->FrameInputs (kcode + A1/A2
// trigger bytes) mapping stays with the caller, next to Dojo::canonFromPacket.
namespace tas_auto
{
	static const int CANON_BITS = 11;

	// The rate NEW arms use (the UI picker). Default 60 = hold. Clamped 1..60.
	void setAutoHz(int hz);
	int  autoHz();

	// Is a bit armed at `hz` pressed on this tick? phase = frames since t0 (the global
	// frame number for the live overlay, or 0..frames-1 when baking).
	bool tickOn(int hz, u64 phase);

	// ---- live per-player registry (player 0 = P1, 1 = P2) ----
	void arm(int player, int canonBit, int hz);	// canonBit 0..10; hz 0 = off
	int  hzOf(int player, int canonBit);			// 0 = off
	void clearPlayer(int player);
	void clearAll();
	bool anyArmed();								// the "am I expecting auto-fire?" check
	bool anyArmed(int player);
	u16  overlayCanon(int player, u64 frame);		// OR-mask of the bits pressed this frame

	// ---- live sequence (Input Sender -> Live / pcsx2-rr): a finite queued sequence ----
	void playLive(const std::vector<u16>& p1, const std::vector<u16>& p2, u64 startFrame);
	void stopLive();
	bool liveActive();
	u16  liveCanon(int player, u64 frame);		// this frame's bits, 0 outside the sequence
	void liveTick(u64 frame);					// call once per applied frame; ends the sequence
	u64  liveRemaining(u64 frame);				// frames left (UI status)

	// ---- baking (piano roll / movie): expand one armed input into `frames` frames ----
	void expand(int hz, u16 canon, int frames, std::vector<u16>& out);
}
