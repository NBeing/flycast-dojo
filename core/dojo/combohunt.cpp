#include "combohunt.h"
#include "mvc2.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <string>

/*
	SCAFFOLD. The hunt proper (the exploration loop, the phase sweep, the candidate
	table, the RESULT line) is Track A's; this stub exists so the contract links,
	the tick and selfTest are wired, and dojo:ComboHunt is visibly a no-op until
	then - a stub that said nothing would read like a hunt that found nothing.
*/
namespace roll {
namespace combohunt {

void tick()
{
	static bool said = false;
	if (said)
		return;
	const std::string mode = cfgLoadStr("dojo", "ComboHunt", "");
	if (mode.empty() || mode == "no")
		return;
	said = true;
	NOTICE_LOG(RENDERER, "COMBO HUNT RESULT: found=no candidate=none phase=-1 d=0 peak=0 base=0 after=0 (stub - the hunt is not implemented yet)");
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	tas_mvc2::selfTest();		// SPREADSHEET SELFTEST: the field dictionary the hunt's oracle resolves through
	NOTICE_LOG(RENDERER, "COMBOHUNT SELFTEST: 0 passed, 0 failed");
}

}	// namespace combohunt
}	// namespace roll
