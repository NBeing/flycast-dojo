#pragma once

/*
	A KEY THAT DOES ONE THING ON A TAP AND REPEATS WHILE HELD.

	`EMU_BTN_STEP` is the first customer: "Tap for one frame - hold to scrub in
	slow motion" `[SOURCE]` the TAS fork's own help text for it. Slot cycling
	wants the same shape ("Hold it to keep moving"), and so does the BASE
	overwrite guard ("HOLD to overwrite BASE (slot 0)").

	IT LIVES IN core/input/ AND IN `hotkeys`, not in core/dojo/. The first
	attempt put it under `namespace dojo`, which does not compile: dojo.h
	declares a GLOBAL OBJECT called `dojo`. The compiler was right for a better
	reason than it knew - this is an input concept, it serves the hotkey
	registry next door, and nothing about it belongs to the movie layer.

	A UNIT, NOT A HOTKEY, deliberately. The thing worth testing here is the
	state machine - when does it fire, how often, and what happens when a
	release goes missing - and none of that needs an emulator, a key, or a
	window. It is written before the hotkey that uses it, and tested first.

	THE RULE THAT MATTERS IS THE RELEASE. `[SOURCE]` the TAS fork:

		"A GUARD MUST NEVER SWALLOW A RELEASE. The release is what clears a
		 hold; letting gui_keyboard_captured() eat it latches the hold forever.
		 That is exactly what stranded the scrub at End of Replay: the
		 End-of-Replay window takes keyboard focus, so the Space keyup was
		 dropped, step_held stayed true, and mainui spun on a dead movie ...
		 Same shape for the slot and BASE holds - and on BASE a latched hold
		 would have gone on to OVERWRITE it."

	So `release()` is unconditional and always safe, including when nothing was
	ever pressed, and a caller that loses a release can recover by calling it.
*/
namespace hotkeys
{

class HoldRepeat
{
public:
	//! `delay` seconds held before repeating begins; `rate` repeats per second
	//! after that. A tap shorter than `delay` fires exactly once.
	HoldRepeat(double delay, double rate) : delay_(delay), rate_(rate) {}

	//! A key went down. Returns how many times to act NOW: 1 for a fresh
	//! press, 0 for a repeat of a press already held (key auto-repeat from the
	//! OS must not double-count).
	int press(double now);

	//! Time passed. Returns how many repeats are due since the last call - 0
	//! before the delay matures, and 0 whenever the key is not held.
	int tick(double now);

	//! The key came up. ALWAYS SAFE: a release with no press is not an error,
	//! it is how a caller that lost one recovers.
	void release();

	bool held() const { return held_; }

private:
	double delay_;
	double rate_;
	bool   held_ = false;
	double pressedAt_ = 0;
	//! Repeats already reported for this hold. Counted, so `tick` can answer
	//! from one division against the press time instead of accumulating.
	int    fired_ = 0;
};

/*
	THE FRAME-ADVANCE HOLD, shared by the two places that need it.

	The dispatch presses and releases it; the frame loop ticks it. A file-static
	in either would leave the other unable to reach it, and a copy in each is
	two machines disagreeing about whether a key is down.

	`dojo:HoldStepDelay` seconds before the scrub starts (default 0.3 - long
	enough that a quick tap cannot slide two frames, which is the debounce the
	TAS fork records needing) and `dojo:HoldStepRate` frames per second once it
	does (default 8 - "slow motion", not fast-forward).
*/
HoldRepeat& stepHold();

/*
	A KEY THAT DOES NOTHING ON A TAP AND ONE THING WHEN HELD LONG ENOUGH.

	The BASE overwrite guard, ported 2026-09-17 (docs/PORT-DEFECT-CENSUS.md #17).
	`[SOURCE]` the TAS fork: "Slot 0 (BASE) is write-protected. Once it holds a
	state, a plain F1 tap is rejected - F1 must be HELD for BaseHoldMs (default
	1 s) to overwrite it. BASE is the state every seek returns to; losing it costs
	a whole session." Same for a slot a branch forks from (its jump-off anchor).

	The dispatch presses and releases it; the frame loop ticks it and performs the
	write when tick() says the hold matured. A tap is a press and a release before
	maturity: tick() never fires, and the release reports it as BLOCKED. The
	release rule above applies verbatim - unconditional, always safe - and on
	THIS hold a swallowed release would not strand a scrub, it would overwrite
	BASE a second later with nobody touching anything.
*/
class HoldOnce
{
public:
	//! A key went down; `delay` seconds from `now` the hold matures. Returns
	//! true for a fresh press, false for an OS repeat of a press already held.
	bool press(double now, double delay);
	//! Time passed. Returns true EXACTLY ONCE per hold, when it matures.
	bool tick(double now);
	//! The key came up. Returns true when this release CANCELLED an immature
	//! hold (a tap) - the caller reports that as BLOCKED. Unconditional.
	bool release();
	bool held() const { return held_; }
	bool matured() const { return held_ && fired_; }
	//! 0..1 of the way to maturity, for a HUD bar; 0 when not held.
	double progress(double now) const;

private:
	bool   held_ = false;
	bool   fired_ = false;
	double pressedAt_ = 0;
	double delay_ = 0;
};

//! The BASE / fork-point overwrite hold. `dojo:BaseHoldMs` (default 1000) is
//! read at each press, so the TAS menu can change it live.
HoldOnce& baseHold();

//! What the guard has done this process - the tour reads these back to prove a
//! tap was BLOCKED and a hold WROTE, instead of trusting the log.
struct BaseHoldStats { int blocked = 0; int armed = 0; int written = 0; };
BaseHoldStats& baseHoldStats();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void holdRepeatSelfTest();

}	// namespace hotkeys
