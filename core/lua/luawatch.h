#pragma once
#include "types.h"

/*
	CHANGE NOTIFICATION FOR A REGION OF GUEST MEMORY, once per confirmed frame.

	TODOS.md Observability Phase 4 proposed building this on memwatch's
	dirty-page tracking. `[MEASURED 2026-09-10]` THAT MECHANISM IS NOT AVAILABLE
	HERE, and the reason is worth stating because it is not a performance
	argument:

	  PAGE PROTECTION IS ARMED ONLY UNDER GGPO.
	      inline static void protect() { if (!config::GGPOEnable) return; ... }
	  Outside a rollback session there are no dirty pages at all - nothing is
	  write-protected, so nothing is tracked. Arming it for a watch would put a
	  SIGSEGV round trip on every guest write.

	  AND THE PAGE LISTS ARE DRAINED BY THE ROLLBACK PATH. getPages() copies the
	  pages out and then does `count = 0; clearBitmap()`, and those copies ARE
	  the deltas load_game_state walks backwards to undo writes. A watch that
	  read them would STEAL a frame's delta and corrupt a rollback - a desync,
	  not a slowdown.

	So the mechanism is a byte comparison against a shadow copy, and the page
	filter is refused rather than half-wired. Supporting two consumers of the
	dirty set is rollback surgery for a feature that does not need it.

	WHAT THIS IS NOT. There is no PC context: this says a region CHANGED, never
	which instruction changed it. fbneo-style execution hooks are a different
	capability that this emulator's dynarec does not offer, and calling this an
	equivalent would be the kind of claim TODOS.md asks not to make.

	WHY IN C++ AT ALL, when reactive.lua can already poll a value and notice it
	changing: the boundary. Pulling a 16 KB region through the Lua stack every
	frame to compare it is the cost this avoids; the script sees a revision
	counter and only reads bytes when it moves.

	CONFIRMED FRAMES ONLY, because it is evaluated from the same VBlank path
	that is already gated on !ggpo::rollbacking(). A region written and rewritten
	across a rollback counts once.
*/
namespace luawatch
{

/*
	The largest region one watch may cover, and the number is measured rather
	than picked.

	`[MEASURED 2026-09-10]` scripts/tests/memwatch.lua reports the tick's cost
	every run. A 256-byte watch: 1.0 us typical, 3.0 us peak - about 12 ns a
	byte through addrspace::readt<u32>.

	At that rate this cap is roughly 750 us a frame, which is 4.5% of a 60 Hz
	budget for ONE watch of that size. That is affordable and not free, and it
	is why the cap exists at all: a script that wants to watch a megabyte is
	asking for a mechanism this is not.

	The figure travels with the feature - watchMicros() reports it live - so a
	reader can judge their own case instead of trusting this comment.
*/
constexpr u32 MAX_BYTES = 64 * 1024;

//! Register a watch. Returns an id >= 0, or -1 for a zero or oversized length.
int add(u32 addr, u32 len);
bool remove(int id);

//! Increments once per confirmed frame in which the region's bytes differ from
//! the previous frame's. A COUNTER rather than a flag: a flag cannot tell "no
//! change" from "changed twice while nobody looked".
u32 revision(int id);

//! Evaluate every watch. Called once per confirmed frame, before script
//! callbacks, so a script's own frame hook sees an up-to-date revision.
void tick();

//! A new script owns new watches.
void reset();

int    count();
u64    compares();		//!< regions compared since reset
u64    changes();		//!< of those, how many differed
double lastMicros();	//!< what the last tick cost, so the price is visible
double maxMicros();

void selfTest();

}	// namespace luawatch
