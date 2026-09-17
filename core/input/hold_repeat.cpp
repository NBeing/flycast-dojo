#include "hold_repeat.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cmath>
#include <algorithm>

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
	// THE RAMP, STILL COUNTED IN CLOSED FORM. With a linear rate r(t) from r0 up to
	// rate_ over rampS_, the repeats due by t are the integral: r0*t + (rate_-r0)*t^2/(2T)
	// inside the ramp, then rate_*(t-T) more after it - one expression against the
	// press time, so the no-drift property above survives the ramp.
	const double t = now - due;
	const double r0 = std::min(15.0, rate_);
	double dueCount;
	if (rampS_ > 0.0 && rate_ > r0)
	{
		const double T = rampS_;
		dueCount = t < T ? r0 * t + (rate_ - r0) * t * t / (2.0 * T)
		                 : r0 * T + (rate_ - r0) * T / 2.0 + rate_ * (t - T);
	}
	else
		dueCount = t / period;
	const int total = 1 + (int)std::floor(dueCount + 1e-6);
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
			(double)cfgLoadInt("dojo", "HoldStepRate", 8),
			cfgLoadInt("dojo", "HoldStepRampMs", 1000) / 1000.0);	// David's ramp; inert at the default rate of 8 (<= 15)
	return h;
}

// ---- HoldOnce: the BASE overwrite guard ------------------------------------------

bool HoldOnce::press(double now, double delay)
{
	if (held_)
		return false;	// the OS repeating the key, not a new press
	held_ = true;
	fired_ = false;
	pressedAt_ = now;
	delay_ = delay < 0 ? 0 : delay;
	return true;
}

bool HoldOnce::tick(double now)
{
	if (!held_ || fired_)
		return false;
	if (now - pressedAt_ + 1e-9 < delay_)
		return false;
	fired_ = true;	// EXACTLY ONCE: a held key must not overwrite BASE every frame after it matures
	return true;
}

bool HoldOnce::release()
{
	// UNCONDITIONAL, like HoldRepeat::release() and for the same reason - only
	// here a latched hold would go on to OVERWRITE BASE, not just spin a scrub.
	const bool cancelled = held_ && !fired_;
	held_ = false;
	fired_ = false;
	pressedAt_ = 0;
	delay_ = 0;
	return cancelled;
}

double HoldOnce::progress(double now) const
{
	if (!held_)
		return 0.0;
	if (delay_ <= 0.0)
		return 1.0;
	const double p = (now - pressedAt_) / delay_;
	return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
}

HoldOnce& baseHold()
{
	static HoldOnce h;
	return h;
}

BaseHoldStats& baseHoldStats()
{
	static BaseHoldStats s;
	return s;
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
	{	// THE RAMP (dojo:HoldStepRampMs): a 60/s hold with a 1 s ramp starts near 15/s
		// and is at 60/s after the ramp. Both halves, or a ramp that never ends and a
		// ramp that never starts would each pass one of them.
		HoldRepeat h(0.5, 60.0, 1.0), flat(0.5, 60.0, 0.0);
		h.press(100.0); flat.press(100.0);
		int rampFirstHalf = 0, flatFirstHalf = 0, rampLater = 0;
		for (int i = 0; i <= 50; i++) { rampFirstHalf += h.tick(100.5 + i * 0.01); flatFirstHalf += flat.tick(100.5 + i * 0.01); }
		h.tick(102.5);	// consume everything due through 2.0 s into the hold
		for (int i = 1; i <= 100; i++) rampLater += h.tick(102.5 + i * 0.01);	// one second, fully ramped
		claim("a ramped hold starts slower than a flat one (first 0.5 s)", rampFirstHalf < flatFirstHalf && rampFirstHalf >= 5 && rampFirstHalf <= 20);
		claim("...and repeats at the full rate once the ramp is over", rampLater >= 57 && rampLater <= 63);
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

	// ---- BASEHOLD: the overwrite guard's state machine, time as a parameter ----
	// The four behaviours the rule names: a tap is rejected (and SAYS so), a hold
	// matures once and only once, a release before maturity cancels, and the
	// object is usable again afterwards. Every negative claim is paired with a
	// positive one in the same fixture, for the reason the block above records.
	int bpass = 0, bfail = 0;
	auto bclaim = [&](const char *what, bool ok) {
		(ok ? bpass : bfail)++;
		NOTICE_LOG(RENDERER, "BASEHOLD SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	{	// A TAP IS BLOCKED: press, release before the delay -> nothing fired, and
		// the release reports the cancellation (that is the BLOCKED trace's source).
		HoldOnce h;
		const bool down = h.press(100.0, 1.0);
		const bool early = h.tick(100.5);
		const bool cancelled = h.release();
		bclaim("a tap (press + release before BaseHoldMs) fires nothing and is reported BLOCKED",
				down && !early && cancelled && !h.held());
	}
	{	// A HOLD MATURES ONCE. Ticked well past maturity, it fires on the first
		// tick at/after the delay and never again - a held key must not overwrite
		// BASE every frame.
		HoldOnce h;
		h.press(100.0, 1.0);
		const bool before = h.tick(100.99);
		const bool at = h.tick(101.0);
		int again = 0;
		for (int i = 1; i <= 50; i++) if (h.tick(101.0 + i * 0.1)) again++;
		const bool cancelled = h.release();
		bclaim("a hold fires exactly once, at maturity, and the release after it is NOT a block",
				!before && at && again == 0 && !cancelled);
	}
	{	// THE OS REPEATS KEYS. A second press with no release is not a new hold
		// and must not restart the clock (or a held F1 would never mature).
		HoldOnce h;
		const bool first = h.press(100.0, 1.0);
		const bool again = h.press(100.9, 1.0);
		const bool fired = h.tick(101.0);
		bclaim("an OS key-repeat does not restart the hold clock", first && !again && fired);
	}
	{	// A LOST RELEASE IS HARMLESS, twice, and the object still works: on this
		// hold a latch would OVERWRITE BASE, so the recovery path has teeth.
		HoldOnce h;
		const bool r1 = h.release();
		const bool r2 = h.release();
		bclaim("a release with no press, twice, blocks nothing and leaves the key usable",
				!r1 && !r2 && !h.held() && h.press(100.0, 0.5) && h.held());
	}
	{	// progress() is a HUD number: 0 unheld, climbs, clamps at 1.
		HoldOnce h;
		const double idle = h.progress(100.0);
		h.press(100.0, 2.0);
		const double half = h.progress(101.0);
		const double past = h.progress(105.0);
		bclaim("progress reads 0 unheld, 0.5 halfway, 1 past maturity",
				idle == 0.0 && half > 0.49 && half < 0.51 && past == 1.0);
	}
	{	// BaseHoldMs = 0 is "no guard": a press matures on the first tick.
		HoldOnce h;
		h.press(100.0, 0.0);
		bclaim("a zero delay matures on the first tick (BaseHoldMs=0 disables the guard)", h.tick(100.0));
	}
	NOTICE_LOG(RENDERER, "BASEHOLD SELFTEST: %d passed, %d failed", bpass, bfail);
}

}	// namespace hotkeys
