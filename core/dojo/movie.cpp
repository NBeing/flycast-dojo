#include "movie.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"

namespace movie
{

/*
	`[2026-09-11]` Written after the four claims below, which were run against a
	`return 0` stub and failed two of four. The two that passed are the CONTROLS
	- the movie that really does start at 0, and the empty one - and they are
	what stops this being satisfied by a constant in either direction: always-0
	fails the late-start claims, always-first-key fails the empty one.

	std::map is ordered, so the first key IS the first frame. Worth saying out
	loud rather than relying on it silently: if this ever becomes an unordered
	container the answer changes without the type changing.
*/
u32 firstFrame(const std::map<u32, std::vector<u8>>& frames)
{
	return frames.empty() ? 0 : frames.begin()->first;
}

}	// namespace movie

// ---- SELF-TEST ------------------------------------------------------------
//
// RUN:  flycast --config dojo:PanelSelfTest=yes
// PASS: "MOVIE SELFTEST: N passed, 0 failed"
//
// WRITTEN BEFORE firstFrame() DID ANYTHING, and run against the stub above, so
// each claim was seen to fail before it was made to pass.
//
// SYNTHETIC MAPS, never dojo.session_inputs: these run inside flycast_init
// where the real movie is empty, and a self-test that mutated it to have
// something to look at would be testing a machine it had just disturbed.

namespace movie
{

void movieSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "MOVIE SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	using Frames = std::map<u32, std::vector<u8>>;
	const std::vector<u8> row(24, 0);

	{	// A MOVIE RECORDED FROM A SAVESTATE. The case that wedged a replay:
		// ordinary frames, none of them frame 0.
		Frames m;
		for (u32 f = 9948; f <= 10007; f++)
			m[f] = row;
		claim("a movie that starts late reports where it starts",
				firstFrame(m) == 9948);
	}
	{	// AND THE CONTROL: one that does start at 0, so "reports 9948" cannot
		// be satisfied by a function that returns whatever it was handed.
		Frames m;
		for (u32 f = 0; f < 10; f++)
			m[f] = row;
		claim("...and a movie that starts at 0 says 0", firstFrame(m) == 0);
	}
	{	// A HOLE IS NOT A START. Re-recording leaves gaps in the middle, and
		// the first frame is still the first frame.
		Frames m;
		m[100] = row;
		m[105] = row;		// 101..104 missing
		m[106] = row;
		claim("a gap after the first frame does not move the start",
				firstFrame(m) == 100);
	}
	{	// EMPTY. 0 is the honest answer and it is why `beforeStart` asks
		// `authored()` first - without that, every frame of an empty movie
		// would read as "before the start" instead of "there is no movie".
		Frames m;
		claim("an empty movie starts at 0", firstFrame(m) == 0);
	}

	NOTICE_LOG(RENDERER, "MOVIE SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace movie
