#include "liveness.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "oslib/oslib.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace liveness
{

/*
	`[2026-09-11]` Written after the six claims below, which ran against
	do-nothing stubs first and failed 6 of 6. The first draft of those claims
	failed only 5 of 7: one was a bare negative ("no movement inside the grace
	period is not a verdict") whose positive partner sat in a SEPARATE claim, so
	a Quiet-always stub satisfied it. Merged, it discriminates.
*/
void Watch::arm(frames::Vblank frames, double now)
{
	std::lock_guard<std::mutex> lock(mtx_);
	armed_ = true;
	at_ = frames;
	when_ = now;
}

Verdict Watch::check(frames::Vblank frames, double now, bool running)
{
	std::lock_guard<std::mutex> lock(mtx_);
	if (!armed_)
		return Verdict::Quiet;
	if (frames != at_)
	{
		// ONE COMPLETED FRAME IS ENOUGH. The question is whether the machine
		// runs at all, not how fast - a slow frame is not a dead one.
		armed_ = false;
		return Verdict::Alive;
	}
	if (!running)
	{
		/*
			THE CLOCK ONLY RUNS WHILE THE MACHINE DOES. A paused machine has not
			failed to advance - it was not asked to - and a TAS tool is paused
			most of the time, so timing one would fire on nearly every load its
			user makes. A warning that is usually wrong gets turned off.

			PUSHED, NOT SKIPPED. Merely answering Quiet here passes that claim
			and then charges the machine for the entire pause the instant it
			resumes, reporting a perfectly healthy load as dead. The grace is
			seconds of RUNNING, and the self-test discriminates the two.
		*/
		when_ = now;
		return Verdict::Quiet;
	}
	if (now - when_ < grace_)
		return Verdict::Quiet;
	// DISARMED ON DEATH TOO, so the verdict is reported once. A dead machine is
	// dead on every later call, and thousands of lines would bury the one that
	// matters.
	armed_ = false;
	return Verdict::Dead;
}

bool Watch::armed() const
{
	std::lock_guard<std::mutex> lock(mtx_);
	return armed_;
}

Watch& stateWatch()
{
	static Watch w(cfgLoadInt("dojo", "LoadGraceSeconds", 3));
	return w;
}

static std::atomic<bool> watchdogStop{false};
static std::atomic<bool> watchdogUp{false};

void startWatchdog(std::function<frames::Vblank()> frames, std::function<bool()> running,
		std::function<u64()> cycles)
{
	if (watchdogUp.exchange(true))
		return;			// already up - one watch, one thread
	watchdogStop = false;
	std::thread([frames, running, cycles] {
		// SNAPSHOT TAKEN HERE, NOT IN arm(), so Watch stays a rule over four
		// numbers and knows nothing about an SH4. Up to one tick late, which
		// against a grace measured in seconds does not change the answer.
		u64  cyclesAtArm = 0;
		bool wasArmed = false;
		/*
			A QUARTER SECOND. The grace is measured in seconds, so finer
			granularity buys nothing, and four wakeups a second against one
			atomic load is invisible beside a 27 MB state load.
		*/
		while (!watchdogStop.load())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			const bool isArmed = stateWatch().armed();
			if (isArmed && !wasArmed)
				cyclesAtArm = cycles();
			wasArmed = isArmed;
			switch (stateWatch().check(frames(), os_GetSeconds(), running()))
			{
			case Verdict::Dead:
				/*
					LOGGED, NOT SHOWN. There is no UI to notify: in the case
					this exists for the frame loop is inside the guest and
					nothing will be drawn again. A notification would be
					queued into a window that never repaints, and drawing
					from this thread is worse than saying nothing.
				*/
			{
				const u64 spun = cycles() - cyclesAtArm;
				WARN_LOG(COMMON, "STATE LIVENESS: no frame completed in %ds of running "
						"after a state load - the state restored but the machine is "
						"not advancing (docs/GDB.md)",
						cfgLoadInt("dojo", "LoadGraceSeconds", 3));
				// THE NUMBER THAT SAYS WHICH BUG IT IS. Separate line because it
				// is the first thing anyone reading the warning above needs, and
				// burying it in that sentence is how it gets skimmed past.
				WARN_LOG(COMMON, "STATE LIVENESS: the SH4 advanced %llu cycles in that "
						"time - %s", (unsigned long long)spun,
						spun > 1000000
							? "so it IS executing and the SPG never fired: a scheduled "
							  "event did not survive the load"
							: "so it is not executing either, despite emu.running()");
				break;
			}
			case Verdict::Alive:
				NOTICE_LOG(COMMON, "STATE LIVENESS: the machine is advancing after the load");
				break;
			case Verdict::Quiet:
				break;
			}
		}
		watchdogUp = false;
	}).detach();
}

void stopWatchdog()
{
	watchdogStop = true;
}

}	// namespace liveness

// ---- SELF-TEST ------------------------------------------------------------
//
// RUN:  flycast --config dojo:PanelSelfTest=yes
// PASS: "LIVENESS SELFTEST: N passed, 0 failed"
//
// WRITTEN BEFORE arm() AND check() DID ANYTHING. Every negative claim here is
// paired with a positive one in the same fixture, because `[MEASURED
// 2026-09-10]` six of ten claims in the last unit written this way passed
// against a do-nothing stub - a stub satisfies "reports nothing" trivially,
// since it cannot tell "correctly quiet" from "never does anything".


namespace liveness
{

void livenessSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "LIVENESS SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	auto fresh = [] { return Watch(2.0); };		// two seconds of grace

	{	// AN UNARMED WATCH SAYS NOTHING - paired with the positive, so a class
		// that always says Quiet cannot satisfy it.
		Watch w = fresh();
		const Verdict idle = w.check(frames::Vblank(100), 10.0, true);
		w.arm(frames::Vblank(100), 10.0);
		const Verdict after = w.check(frames::Vblank(105), 10.1, true);	// frames moved
		claim("quiet until armed, and answers once it is",
				idle == Verdict::Quiet && after == Verdict::Alive);
	}
	{	// THE MACHINE MOVED. One completed frame is enough - the question is
		// whether it is running at all, not how fast.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		claim("a single completed frame is alive",
				w.check(frames::Vblank(501), 20.5, true) == Verdict::Alive);
	}
	{	// STILL INSIDE THE GRACE. A load takes real time; calling it dead at
		// once would fire on every healthy load.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		const Verdict early = w.check(frames::Vblank(500), 21.9, true);	// no movement, 1.9s < 2.0
		const Verdict late  = w.check(frames::Vblank(500), 22.1, true);	// no movement, past grace
		// ONE CLAIM, NOT TWO. `[MEASURED 2026-09-11]` as two, the first was the
		// only one of seven that passed against the do-nothing stub - a bare
		// negative whose positive partner sat in a separate claim is still a
		// bare negative. The grace period only means something if crossing it
		// changes the answer.
		claim("the grace period holds the verdict, and crossing it gives one",
				early == Verdict::Quiet && late == Verdict::Dead);
	}
	{	// REPORTS ONCE. A dead machine is dead every frame after, and thousands
		// of lines would bury the one that matters.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		const Verdict first  = w.check(frames::Vblank(500), 23.0, true);
		const Verdict second = w.check(frames::Vblank(500), 24.0, true);
		claim("death is reported once, not every frame",
				first == Verdict::Dead && second == Verdict::Quiet);
	}
	{	// AND ALIVE DISARMS. Otherwise a machine that ran and later paused
		// legitimately would be reported dead long after the load.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		const Verdict alive = w.check(frames::Vblank(501), 20.1, true);
		const Verdict later = w.check(frames::Vblank(501), 99.0, true);	// paused, long after
		claim("a machine that proved alive is not judged again",
				alive == Verdict::Alive && later == Verdict::Quiet && !w.armed());
	}
	{	// A SECOND LOAD RE-ARMS. Every load gets its own verdict.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		w.check(frames::Vblank(501), 20.1, true);				// alive, disarmed
		w.arm(frames::Vblank(600), 30.0);				// loaded again
		claim("a second load is judged on its own",
				w.armed() && w.check(frames::Vblank(600), 32.1, true) == Verdict::Dead);
	}

	{	// A PAUSED MACHINE IS NOT A DEAD ONE. The normal TAS case: load a state
		// while frame-advancing, then sit on it. Nothing is owed until the
		// machine is asked to run.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		const Verdict held = w.check(frames::Vblank(500), 99.0, false);		// paused 79s > grace
		claim("a paused machine earns no verdict, however long it is paused",
				held == Verdict::Quiet && w.armed());
	}
	{	// AND THE CLOCK RESTARTS WHEN IT RESUMES. This is the claim that
		// discriminates: simply answering Quiet while paused passes the one
		// above, but then charges the machine for the whole pause the instant
		// it resumes and reports a healthy load as dead.
		Watch w = fresh();
		w.arm(frames::Vblank(500), 20.0);
		w.check(frames::Vblank(500), 50.0, false);							// paused for 30s
		const Verdict justAfter = w.check(frames::Vblank(500), 51.0, true);	// 1s of running < 2.0
		const Verdict then      = w.check(frames::Vblank(500), 52.1, true);	// 2.1s of running
		claim("the grace clock restarts when the machine resumes",
				justAfter == Verdict::Quiet && then == Verdict::Dead);
	}

	NOTICE_LOG(RENDERER, "LIVENESS SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace liveness
