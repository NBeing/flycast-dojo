#include "hold_repeat.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cmath>

namespace hotkeys
{

/*
	`[2026-09-10]` Written AFTER the ten claims below, which were run against
	empty stubs first and failed 9 of 9. The first draft of those claims failed
	only 4 of 10 - six were negative ("fires nothing", "is harmless") and a
	do-nothing implementation satisfies every negative claim, because it cannot
	tell "correctly refused" from "never did anything". Each is now paired with
	a positive assertion in the same fixture.
*/
int HoldRepeat::press(double now)
{
	// ALREADY HELD IS THE OS REPEATING THE KEY, not a new action. Reporting it
	// as one makes a held frame-advance advance twice per OS repeat on top of
	// its own rate.
	if (held_)
		return 0;
	held_ = true;
	pressedAt_ = now;
	fired_ = 0;
	return 1;
}

int HoldRepeat::tick(double now)
{
	if (!held_)
		return 0;
	if (now - pressedAt_ < delay_)
		return 0;
	if (rate_ <= 0.0)
		return 0;
	const double period = 1.0 / rate_;
	const double due = pressedAt_ + delay_;

	/*
		THE FIRST REPEAT IS AT MATURITY, not one period after it. `[MEASURED
		2026-09-10]` the first implementation deferred it, and the claim written
		BEFORE that implementation - "nothing repeats before the delay, and
		something does after it", sampled at 100.49 and 100.51 - failed. The
		test was the specification and the code disagreed with it. Building
		first, this would have shipped: a hold that takes 0.6 s to start instead
		of 0.5 s feels perfectly fine.

		COUNTED FROM THE PRESS, NOT ACCUMULATED. The first version walked
		`from += period` in a loop, which both drifts and lands on exact
		boundaries: 100.6 - 100.5 is 0.09999999999999432, so a repeat due
		exactly on a tick was missed and two claims failed for a reason that had
		nothing to do with holding keys. One division against the press time has
		no accumulated error, and the epsilon absorbs the representation.

		COUNTED, NOT CLAMPED TO ONE: a caller that ticks late - a slow frame, a
		stalled render thread - is owed the repeats that came due while it was
		away, or a hold silently slows down whenever the machine does.
	*/
	if (now < due)
		return 0;
	const int total = 1 + (int)std::floor((now - due) / period + 1e-6);
	const int n = total - fired_;
	fired_ = total;
	return n > 0 ? n : 0;
}

void HoldRepeat::release()
{
	// UNCONDITIONAL, and that is the point. `[SOURCE]` the TAS fork: "A GUARD
	// MUST NEVER SWALLOW A RELEASE ... letting gui_keyboard_captured() eat it
	// latches the hold forever." A caller that lost a release recovers by
	// calling this, and calling it twice is not an error.
	held_ = false;
	pressedAt_ = 0;
	fired_ = 0;
}

HoldRepeat& stepHold()
{
	static HoldRepeat h(cfgLoadInt("dojo", "HoldStepDelay", 300) / 1000.0,
			(double)cfgLoadInt("dojo", "HoldStepRate", 8));
	return h;
}

}	// namespace hotkeys

// ---- SELF-TEST ------------------------------------------------------------
//
// RUN:  flycast --config dojo:PanelSelfTest=yes
// PASS: "HOLDREPEAT SELFTEST: N passed, 0 failed"
//
// WRITTEN BEFORE THE IMPLEMENTATION, and run against the stubs above first, so
// every claim below was seen to FAIL before it was made to pass. That is the
// step this tree has been paying for separately - each check here would
// otherwise get a sabotage commit afterwards to prove it could fail at all.
//
// TIME IS A PARAMETER, not a clock. Every claim passes its own `now`, so the
// rate and the delay can be exercised in microseconds and no claim depends on
// how fast the machine running it is.


namespace hotkeys
{

void holdRepeatSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "HOLDREPEAT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// 0.5 s before repeating, then 10 per second.
	auto fresh = [] { return HoldRepeat(0.5, 10.0); };

	{	// A TAP IS EXACTLY ONE ACTION. The whole point of the delay.
		HoldRepeat h = fresh();
		const int n = h.press(100.0);
		const int during = h.tick(100.2);	// still inside the delay
		h.release();
		claim("a tap fires once and only once", n == 1 && during == 0);
	}
	{	// THE OS REPEATS KEYS BY ITSELF. A second press with no release in
		// between is that, and it must not count as a new action.
		//
		// PAIRED WITH THE POSITIVE, like every negative claim below it: on its
		// own, "the second press fires nothing" is satisfied by a press that
		// NEVER fires. `[MEASURED 2026-09-10]` six of these ten claims passed
		// against a do-nothing stub for exactly that reason, and only running
		// them red first showed it.
		HoldRepeat h = fresh();
		const int first = h.press(100.0);
		const int again = h.press(100.05);
		claim("a first press fires, a repeated one with no release does not",
				first == 1 && again == 0);
	}
	{	// THE HOLD MATURES, and not before.
		HoldRepeat h = fresh();
		const int down  = h.press(100.0);
		const int early = h.tick(100.49);
		const int late  = h.tick(100.51);
		// The press is asserted in the SAME claim, so "nothing repeats early"
		// cannot be satisfied by a key that never went down.
		claim("the key went down and nothing repeats before the delay",
				down == 1 && early == 0);
		claim("...and something does after it", late >= 1);
	}
	{	// AND THEN IT KEEPS GOING, at the rate asked for. A full second at
		// 10 Hz past the delay is ten, and this is about the ORDER of the
		// number - a claim of exactly ten would be about the arithmetic of the
		// fixture rather than the behaviour.
		HoldRepeat h = fresh();
		h.press(100.0);
		h.tick(100.5);
		int fired = 0;
		for (int i = 1; i <= 100; i++)
			fired += h.tick(100.5 + i * 0.01);	// one second, sampled every 10 ms
		claim("a held key repeats at about the rate asked for",
				fired >= 8 && fired <= 12);
	}
	{	// RELEASE STOPS IT. The ordinary case.
		HoldRepeat h = fresh();
		h.press(100.0);
		const int wasRepeating = h.tick(100.6);
		const bool wasHeld = h.held();
		h.release();
		// IT HAD TO BE REPEATING FIRST. Otherwise "the repeat stopped" is true
		// of a machine that never started.
		claim("a key that WAS repeating stops when released",
				wasRepeating >= 1 && wasHeld && h.tick(101.0) == 0 && !h.held());
	}
	{	// THE RULE THE FORK LEARNED THE EXPENSIVE WAY. A release that arrives
		// with no press must be harmless - it is how a caller that LOST a
		// release recovers, and losing one is what latched the scrub at End of
		// Replay and could have overwritten BASE.
		HoldRepeat h = fresh();
		h.release();
		h.release();
		// STILL USABLE AFTERWARDS is the claim that has teeth. "held() is
		// false" is true of a class that does nothing at all; "and it still
		// works" is not.
		claim("a release with no press, twice, leaves the key usable",
				!h.held() && h.press(100.0) == 1 && h.held());
	}
	{	// NOT STUCK AFTERWARDS. A latch would show up here as a press that
		// stops answering.
		HoldRepeat h = fresh();
		h.press(100.0);
		h.release();
		claim("a key works again after being released", h.press(101.0) == 1);
	}
	{	// TICKING WITHOUT HOLDING IS NOT A REPEAT. Otherwise a caller that
		// polls every frame fires forever after one tap.
		HoldRepeat h = fresh();
		const int idle = h.tick(100.0);
		// And then prove the same object DOES fire when held - so "fires
		// nothing" is about the holding, not about the object.
		h.press(101.0);
		const int afterHold = h.tick(101.6);
		claim("ticking fires nothing unheld, and does fire held",
				idle == 0 && afterHold >= 1);
	}

	NOTICE_LOG(RENDERER, "HOLDREPEAT SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace hotkeys
