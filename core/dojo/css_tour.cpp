/*
	THE CSS TOUR (module `css`, Surface Tour v4 rails) - STUB. Agent B builds it: David's
	character-select utility exercised on DC from power-on, ending on a saved Dhalsim base.
	Registers zero steps until then.
*/
#include "surface_tour.h"
#include "css.h"
#include <vector>

namespace roll {
namespace surfacetour {
namespace {
void addSteps(std::vector<Step>& out) { (void)out; }
const Module MOD = { "css", addSteps, nullptr, 0, nullptr, 0 };
const bool REGISTERED = (registerModule(&MOD), true);
}
}	// namespace surfacetour
}	// namespace roll
