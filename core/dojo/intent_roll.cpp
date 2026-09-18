/*
	INTENT MODULE: the roll and its history (Surface Tour v4, 2026-09-17).

	Owns the steps for: piano roll edit (module 1), undo/redo (2), macros/snippets
	place + load full (5), the ruler/skip map (10). Registers them from this TU
	through surfacetour::registerModule - the runner's tables are not edited here.
	See docs/TEST-PLAN.md §6 and intent.h for the ceremony every step shares.

	STUB until the module is built: registers zero steps, zero arms.
*/
#include "surface_tour.h"
#include "intent.h"
#include <vector>

namespace roll {
namespace surfacetour {

namespace {

void addSteps(std::vector<Step>& out)
{
	(void)out;
}

const Module MOD = { "roll", addSteps, nullptr, 0, nullptr, 0 };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
