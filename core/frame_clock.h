#pragma once
#include "types.h"

#include <type_traits>

/*
	WHICH FRAME NUMBER IS THIS?

	There is no single frame number in this emulator. There are four, they are
	all small integers, and `[MEASURED 2026-09-08]` three alignment bugs were
	written in ONE DAY by reaching for whichever counter was nearest. Until this
	file the only thing keeping them apart was docs/FRAME-CLOCKS.md - prose, and
	prose cannot be checked against the code. `[MEASURED 2026-09-11]` it drifted
	the first time it was tested: that doc states "there is no fourth clock for
	frames since this boot", and one was added the day before without it.

	So each clock is its own TYPE. Mixing two is a compile error rather than a
	plausible-looking number, and the type name at a call site says which clock
	the caller is on without anyone having to remember.

	WHY THE FOUR ARE NOT INTERCHANGEABLE, shortest first:

	  movie index      WHERE ARE WE IN THE MOVIE. An index, not a clock: it
	                   JUMPS (`[MEASURED]` 0, 60, 9967 across one boot as the
	                   auto-seek lands), re-counts re-simulated frames, resets
	                   when a movie opens, and `[MEASURED]` does not tick until
	                   the game polls maple - 142 frames late at boot.

	  delivered        HAS A FRAME BEEN HANDED OVER. Excludes re-simulated
	                   frames, and resets in ggpo::startSession() - netplay
	                   only, so in a replay it never resets.

	  vblank           DID THE MACHINE COMPLETE A FRAME. Every vblank since the
	                   process started, re-simulated ones INCLUDED, never reset.

	  callback         HOW MANY TIMES WAS I CALLED. Script-local; no C++ owner,
	                   so it has no type here.

	THE TWO THAT LOOK IDENTICAL AND ARE NOT. `vblank` and `delivered` are
	incremented THREE LINES APART in Emulator::vblank(), and differ in exactly
	the two ways above. Liveness wants `vblank` for a reason that reads as a
	quibble until it bites: a machine that is re-simulating is ALIVE, and
	`delivered` would call it dead.
*/
namespace frames
{

/*
	A count on one clock. `Tag` carries the identity; `Tag::Rep` the width, because
	the underlying counters are not the same size and pretending they are is its
	own bug.

	WHAT IS DELIBERATELY NOT HERE: any conversion between clocks, implicit
	construction from a raw integer, and `operator+` between two counts. Adding
	two positions is meaningless on every one of these clocks. Subtracting them
	is not, and it yields a SIGNED delta - the movie index goes backwards on a
	seek, and an unsigned difference turns that into four billion.
*/
template <typename Tag>
class Count
{
public:
	using Rep = typename Tag::Rep;

	constexpr Count() = default;
	constexpr explicit Count(Rep v) : v_(v) {}

	//! The bare number, for logging and for the APIs that still take integers.
	constexpr Rep raw() const { return v_; }

	//! Frames from `o` to `*this`. Signed: a seek runs the movie index backwards.
	constexpr s64 operator-(Count o) const { return (s64)v_ - (s64)o.v_; }

	constexpr Count operator+(Rep n) const { return Count(v_ + n); }

	constexpr bool operator==(Count o) const { return v_ == o.v_; }
	constexpr bool operator!=(Count o) const { return v_ != o.v_; }
	constexpr bool operator< (Count o) const { return v_ <  o.v_; }
	constexpr bool operator> (Count o) const { return v_ >  o.v_; }
	constexpr bool operator<=(Count o) const { return v_ <= o.v_; }
	constexpr bool operator>=(Count o) const { return v_ >= o.v_; }

private:
	Rep v_{};
};

struct MovieClock		{ using Rep = u32; };
struct DeliveredClock	{ using Rep = u32; };
struct VblankClock		{ using Rep = u64; };

using Movie     = Count<MovieClock>;		//!< dojo.frame_number
using Delivered = Count<DeliveredClock>;	//!< ggpo::confirmedFrame()
using Vblank    = Count<VblankClock>;		//!< framesCompleted

// THE BARRIER, asserted where it is declared rather than hoped for. These cost
// nothing and they fail the build, which is the only kind of check that cannot
// be skipped. scripts/clocktest.sh proves the barrier can actually REJECT, by
// compiling code that mixes clocks and requiring the compiler to say no - a
// static_assert that is true of any two unrelated classes is weak evidence on
// its own.
static_assert(!std::is_convertible<Movie, Vblank>::value, "clocks must not convert");
static_assert(!std::is_convertible<Vblank, Movie>::value, "clocks must not convert");
static_assert(!std::is_convertible<u32, Movie>::value, "a raw integer is not a position");
static_assert(sizeof(Movie) == sizeof(u32), "a tag costs nothing at runtime");
static_assert(sizeof(Vblank) == sizeof(u64), "a tag costs nothing at runtime");

/*
	THE ONE OWNER OF WHERE EACH CLOCK LIVES. docs/FRAME-CLOCKS.md's table used to
	BE this fact; now it points here, so a counter that moves house cannot leave
	the documentation quietly wrong (CLAUDE.md rule 4).
*/
Movie     movie();
Delivered delivered();
Vblank    vblank();

/*
	The vblank clock's ONLY writer, called from Emulator::vblank(). The counter
	lives in frame_clock.cpp rather than beside the emulator because a counter
	anyone can reach is a counter someone will read with the wrong meaning -
	which is the entire defect this file exists to remove. Read it through
	vblank() above; there is no raw handle to take.
*/
void countVblank();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void frameClockSelfTest();

}	// namespace frames
