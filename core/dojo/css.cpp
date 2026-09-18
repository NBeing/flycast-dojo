#include "css.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"

// STUB - Agent A ports charselect.py here (see css.h). Every function returns "unknown"
// so css_tour.cpp links and its steps SKIP honestly until the port lands.
namespace roll {
namespace css {

bool known(const std::string&) { return false; }
int idOf(const std::string&) { return -1; }
const char *nameOf(int) { return ""; }
std::string neighbor(const std::string&, const char *) { return ""; }
std::vector<std::string> plan(const std::string&, const std::string&) { return {}; }
std::string home(int player) { return player == 0 ? "RubyHeart" : "Cable"; }
u16 dirCanon(int, const char *) { return 0; }
u16 paletteCanon(int, const std::string&) { return 0; }
std::vector<u16> buildPicks(int, const std::vector<Pick>&, const Timing&) { return {}; }
std::vector<std::string> renderLetters(const std::vector<u16>&, const std::vector<u16>&) { return {}; }
bool parsePick(const std::string&, Pick&, std::string& err) { err = "css not ported yet"; return false; }
u32 id2Addr(Slot) { return 0; }
u32 assistAddr(Slot) { return 0; }
u32 paletteAddr(Slot) { return 0; }

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	NOTICE_LOG(RENDERER, "CSS SELFTEST: 0 passed, 0 failed");
}

}	// namespace css
}	// namespace roll
