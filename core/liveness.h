#pragma once
#include "types.h"
#include "frame_clock.h"

#include <functional>
#include <mutex>

/*
	DID THE MACHINE SURVIVE THE LOAD?

	`[MEASURED 2026-09-11]` this tree checks a savestate's FIDELITY in six places
	- the idempotency probe, dojo:VerifyState, state hashes, determinism anchors,
	differential history, the record/replay round trips - and checked that a
	restored machine RUNS in exactly one, added the day before this file.

	Today's defect lives in that gap: "STATE VERIFY: idempotent OK" and the guest
	spinning forever at 0x8c191c90 (docs/GDB.md). Every fidelity check is blind
	to it BY CONSTRUCTION, because they are claims about the serialiser and the
	failure is in the machine. Byte-perfect and unrunnable is a reachable state,
	and the whole apparatus reads as healthy when it happens.

	Two structural reasons it recurs rather than being a one-off:

	  `[SOURCE]` verifyLoadedStateIdempotent's own comment - "two real desyncs
	  were found this way ... a SCIF timer reschedule and AICA envelope
	  side-effects clobbering restored state on load". A class with a history.

	  `[SOURCE]` dc_loadstate carries a HAND-MAINTAINED invalidation list -
	  texture cache, ARM recompiler, MMU table, block manager, memwatch, the SH4
	  cache. Every entry is derived state living OUTSIDE the blob. Add a cache
	  anywhere without adding a line there and states load subtly wrong, with the
	  bytes still round-tripping perfectly.

	THE GRACE CLOCK ONLY RUNS WHILE THE MACHINE IS SUPPOSED TO BE RUNNING. A TAS
	tool is paused most of the time - frame advance is the primary workflow, and
	loading a state while paused is the normal case, not the exception. A watch
	that timed a paused machine would fire on nearly every load a TAS artist
	makes, and a warning that is usually wrong is a warning everyone turns off.
	A paused machine has not failed to advance; it was not asked to.

	THE OBSERVABLE IS COMPLETED FRAMES, not cycles. In the defect that motivated
	this the SH4 was executing flat out at 99% CPU - a cycle counter would have
	called it healthy. What stopped was the machine finishing frames.

	A PURE UNIT over four values, so the rule can be claimed with no emulator.
	The THREAD that drives it is a separate concern - see startWatchdog below,
	and the reason it cannot be the frame loop.
*/
namespace liveness
{

enum class Verdict
{
	Quiet,		//!< nothing to say - not armed, or still inside the grace period
	Alive,		//!< the machine advanced; disarmed
	Dead,		//!< armed, and no frame completed within the grace period
};

class Watch
{
public:
	explicit Watch(double graceSeconds) : grace_(graceSeconds) {}

	/*
		A state was just loaded.

		`frames::Vblank` AND NOT A u64. The clock matters and the difference is
		not cosmetic: ggpo::confirmedFrame() is incremented three lines away in
		the same function and EXCLUDES re-simulated frames, so a machine busy
		re-simulating - which is running - would read as not advancing. The type
		is what stops the wrong counter being passed here later by someone who
		reasonably assumes a frame is a frame (core/frame_clock.h).
	*/
	void arm(frames::Vblank frames, double now);

	/*
		Has it moved? Call every frame; it is cheap and answers Quiet almost
		always.

		REPORTS ONCE. A dead machine is dead on every subsequent call, and a
		check that said so every frame would bury the one line that matters
		under thousands - which is the same reason the panel traces in this tree
		log on change rather than per frame.
	*/
	Verdict check(frames::Vblank frames, double now, bool running);

	bool armed() const;

private:
	// LOCKED BECAUSE THE TWO HALVES ARE ON DIFFERENT THREADS BY DESIGN: armed
	// from the emulation thread inside dc_loadstate, checked from the watchdog.
	// Uncontended in practice - one arm per load, four checks a second.
	mutable std::mutex mtx_;
	double grace_;
	bool   armed_ = false;
	frames::Vblank at_;
	double when_ = 0;
};

/*
	THE ONE WATCH, armed by dc_loadstate and checked by the frame loop.

	Shared because the two halves live in different files and a copy in each is
	two machines disagreeing about whether a load is outstanding - the same
	reason hotkeys::stepHold() is shared.

	`dojo:LoadGraceSeconds` (default 3) is how long a load is allowed to take
	before silence counts as death. Generous on purpose: a cold cache, a slow
	disk and a 27 MB state are all real, and a false "DEAD" on a healthy load
	would teach everyone to ignore the line.
*/
Watch& stateWatch();

/*
	THE WATCHDOG MUST NOT LIVE ON THE THREAD IT WATCHES.

	`[MEASURED 2026-09-11]` The first version of this checked from
	mainui_rend_frame() and reported NOTHING against the very defect it was
	written for - not even a verdict. gdb said why, and the stack is the whole
	argument:

	    Emulator::render -> Emulator::run -> recSh4_Run -> X64Dynarec::mainloop

	on THREAD 1. In single-threaded rendering the UI loop runs the guest inline
	until it yields a frame, so a guest that never finishes one never returns,
	mainui_rend_frame() never completes, and the check never runs again. Arming
	on one thread and checking on the thread the failure suspends is a watchdog
	that is asleep exactly when it is needed.

	Worse than useless, in fact: with `ThreadedRendering=yes` the UI thread IS
	separate and the check DOES fire, so the same code passes in one
	configuration and silently never runs in the other - the "green because it
	was skipped" shape CLAUDE.md rule 5 exists to catch.

	So the watch gets its own thread. Not display_refresh_thread, which looks
	like a free host and is not: `[SOURCE]` mainui_loop only starts it
	`if (config::FixedFrequency != 0)`, and a watchdog that exists only under a
	video setting is the same defect wearing a different hat.

	`frames`, `running` and `cycles` are read FROM that thread, so all three must
	be safe to call from any thread. Passed in rather than included so this unit
	stays a pure function of numbers and keeps needing no emulator to test.

	`cycles` IS DIAGNOSIS, NOT JUDGEMENT - it never reaches Watch, which stays a
	rule over four values. It answers the question a bare "not advancing" leaves
	open, and the two answers are different bugs:

	  SH4 cycles advanced, frames did not
	      the guest is executing and the SPG is not firing. rend_vblank() is
	      called from spg.cpp's scanline-0 handler on sh4_sched timing, with no
	      condition on it, so this means a SCHEDULED EVENT did not survive the
	      load. `[SOURCE]` verifyLoadedStateIdempotent's own comment names "a
	      SCIF timer reschedule" as one of two real desyncs found here, so that
	      family has form.

	  neither advanced
	      the machine is not executing at all, and emu.running() saying yes
	      makes that a different fault entirely.

	A torn read is acceptable and expected: `[SOURCE]` sh4_sched_now64() is
	`sh4_sched_ffb - Sh4cntx.sh4_sched_next`, two plain words off-thread. The
	question asked of it is "did this move by millions", which no tear changes.
*/
void startWatchdog(std::function<frames::Vblank()> frames, std::function<bool()> running,
		std::function<u64()> cycles);

//! Stops it. Called from flycast_term so the thread cannot outlive the logger.
void stopWatchdog();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void livenessSelfTest();

}	// namespace liveness
