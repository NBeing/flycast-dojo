#include "frame_clock.h"
#include "dojo/dojo.h"
#include "network/ggpo.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"

#include <atomic>

namespace frames
{

/*
	THE VBLANK CLOCK, and the whole of it. Incremented in Emulator::vblank()
	BEFORE the dispatch below it, so an observer told about a frame sees that
	frame's number rather than the previous one.

	`[MEASURED 2026-09-11]` It is not ggpo::confirmedFrame() with a different
	name, though the two are incremented three lines apart: confirmed excludes
	re-simulated frames and resets in startSession(). Liveness needs this one,
	because a machine that is re-simulating is running.
*/
static std::atomic<u64> vblanks{0};

void countVblank()
{
	vblanks.fetch_add(1, std::memory_order_relaxed);
}

Vblank vblank()
{
	return Vblank(vblanks.load(std::memory_order_relaxed));
}

Movie movie()
{
	return Movie(dojo.frame_number.load());
}

Delivered delivered()
{
	return Delivered(ggpo::confirmedFrame());
}

}	// namespace frames

// ---- SELF-TEST ------------------------------------------------------------
//
// RUN:  scripts/selftest.sh
// PASS: "FRAMECLOCK SELFTEST: N passed, 0 failed"
//
// WHAT THIS SUITE CANNOT SEE, said out loud: the POINT of this module is that
// mixing two clocks does not compile, and code that does not compile cannot be
// run from here. That half is proved by scripts/clocktest.sh, which hands the
// compiler four mixtures and requires it to reject each one, plus a legal
// snippet it must ACCEPT. What IS here is the arithmetic that does compile -
// where a wrong answer is silent rather than fatal.

namespace frames
{

void frameClockSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "FRAMECLOCK SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	{	// THE NUMBER SURVIVES THE WRAPPER. Paired with a different value, so a
		// raw() that always answered zero cannot satisfy it.
		claim("raw() returns what was put in",
				Movie(9948).raw() == 9948u && Movie(0).raw() == 0u);
	}
	{	// A DELTA IS SIGNED, and this is the claim with a real bug behind it:
		// `[MEASURED 2026-09-08]` the movie index JUMPS - 0, 60, 9967 across one
		// boot as the auto-seek lands - and it runs BACKWARDS on a seek. An
		// unsigned difference turns one frame of rewind into 4,294,967,295.
		const s64 back = Movie(100) - Movie(9948);
		claim("a backwards delta is negative, not four billion",
				back == -9848 && (Movie(9948) - Movie(100)) == 9848);
	}
	{	// THE WIDE CLOCK STAYS WIDE. Vblank is u64 because it never resets, and
		// truncating it to u32 would put a wrap 828 days into a session that
		// nothing would ever catch.
		const Vblank high(0x1'0000'0003ull);
		claim("the vblank clock does not truncate at 32 bits",
				high.raw() == 0x1'0000'0003ull && (high - Vblank(0x1'0000'0000ull)) == 3);
	}
	{	// ADVANCING BY N AND SUBTRACTING GIVES N BACK.
		const Vblank a(500);
		claim("advance then subtract round-trips", (a + 17) - a == 17);
	}
	{	// ORDERING, both ways, so a comparison stuck at one answer fails.
		claim("counts order, and the order is not constant",
				Movie(10) < Movie(20) && !(Movie(20) < Movie(10))
				&& Movie(10) != Movie(20) && Movie(10) == Movie(10));
	}
	{	// A DEFAULT COUNT IS ZERO, which matters because the observable at boot
		// is a default-constructed one and "unset" must not read as "frame 1".
		claim("a default count is frame zero",
				Vblank().raw() == 0 && Movie() == Movie(0));
	}
	{	// THE ACCESSORS ARE WIRED TO SOMETHING. Not a value claim - at self-test
		// time nothing has run - but the calls must exist and answer, which is
		// what a dropped extern or a renamed counter would break.
		const Vblank v = frames::vblank();
		const Movie  m = frames::movie();
		claim("every clock accessor answers", v >= Vblank(0) && m >= Movie(0));
	}

	NOTICE_LOG(RENDERER, "FRAMECLOCK SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace frames
