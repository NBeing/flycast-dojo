/*
	INTENT MODULE: the clip and its copies (Surface Tour v4, 2026-09-17).

	Owns the steps for: branches (module 3), generations (4), the test lab (6),
	captures (9). Registers them from this TU through surfacetour::registerModule -
	the runner's tables are not edited here. See docs/TEST-PLAN.md §6 and intent.h
	for the ceremony every step shares.

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

const Module MOD = { "clip", addSteps, nullptr, 0, nullptr, 0 };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
