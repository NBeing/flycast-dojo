#include "surface_tour.h"
#include "dojo.h"
#include "roll_host.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstring>
#include <string>

/*
	SCAFFOLD. Every hook that lives in THIS file refuses with "not implemented" so the
	runner can already score it as SKIP/FAIL honestly; Track B replaces each body with
	the real verb + read-back. Hooks whose verbs are static in a feature's own TU
	(rollEditFlipUndo, senderSend/Stop, notepadAnalyze, snippetsPlace, macrosPlace,
	labAddTest/TrashTest, fstArmSweep/SweepDone) are DEFINED THERE by Track B; their
	scaffold bodies are here only so the tree links today, and are removed when the
	real ones land (a duplicate definition is the linker telling Track B it forgot).

	probeTick() is the unit drive: dojo:TourHook=<name> runs ONE hook once the machine
	is ready and logs `TOUR HOOK: <name> -> PASS|FAIL (<why>)` - how a track proves a
	hook alone, before the runner exists.
*/
namespace roll {
namespace surfacetour {
namespace hooks {

#define NOT_YET(name) bool name() { why("not implemented"); return false; }

// surface_tour_hooks.cpp's own (Track B fills these in place)
NOT_YET(statesLabelRoundTrip)
NOT_YET(saveScratchSlot)
NOT_YET(loadScratchSlot)
NOT_YET(deleteScratchSlot)
NOT_YET(slotNext)
NOT_YET(slotPrev)
NOT_YET(driverRead)
NOT_YET(driverReadWrite)
NOT_YET(driverWrite)
NOT_YET(branchCreate)
NOT_YET(branchCheckout)
NOT_YET(branchBackToMain)
NOT_YET(capturesStart)
NOT_YET(capturesStop)
NOT_YET(exportLaunch)
NOT_YET(exportDone)

// scaffold stand-ins for the hooks that belong in their feature's TU (Track B moves them)
NOT_YET(rollEditFlipUndo)
NOT_YET(senderSend)
NOT_YET(senderStop)
NOT_YET(notepadAnalyze)
NOT_YET(snippetsPlace)
NOT_YET(macrosPlace)
NOT_YET(labAddTest)
NOT_YET(labTrashTest)
NOT_YET(fstArmSweep)
NOT_YET(fstSweepDone)

#undef NOT_YET

struct Entry { const char *name; bool (*fn)(); };
static const Entry kHooks[] = {
	{ "rollEditFlipUndo",     rollEditFlipUndo },
	{ "statesLabelRoundTrip", statesLabelRoundTrip },
	{ "saveScratchSlot",      saveScratchSlot },
	{ "loadScratchSlot",      loadScratchSlot },
	{ "deleteScratchSlot",    deleteScratchSlot },
	{ "slotNext",             slotNext },
	{ "slotPrev",             slotPrev },
	{ "driverRead",           driverRead },
	{ "driverReadWrite",      driverReadWrite },
	{ "driverWrite",          driverWrite },
	{ "senderSend",           senderSend },
	{ "senderStop",           senderStop },
	{ "notepadAnalyze",       notepadAnalyze },
	{ "snippetsPlace",        snippetsPlace },
	{ "macrosPlace",          macrosPlace },
	{ "branchCreate",         branchCreate },
	{ "branchCheckout",       branchCheckout },
	{ "branchBackToMain",     branchBackToMain },
	{ "labAddTest",           labAddTest },
	{ "labTrashTest",         labTrashTest },
	{ "capturesStart",        capturesStart },
	{ "capturesStop",         capturesStop },
	{ "fstArmSweep",          fstArmSweep },
	{ "fstSweepDone",         fstSweepDone },
	{ "exportLaunch",         exportLaunch },
	{ "exportDone",           exportDone },
};

void probeTick()
{
	static bool done = false;
	if (done)
		return;
	const std::string name = cfgLoadStr("dojo", "TourHook", "");
	if (name.empty())
		return;
	// Ready = the same floor the runner uses: frames flowing and slot 0 visible to
	// the host. A hook run against a cold machine would fail for the wrong reason.
	if (dojo.frame_number.load() < 120)
		return;
	{
		SlotView v;
		if (host() == nullptr || !host()->slotView(0, v) || !v.exists)
			return;
	}
	done = true;
	for (const Entry& e : kHooks)
	{
		if (name != e.name)
			continue;
		why("");
		const bool ok = e.fn();
		NOTICE_LOG(RENDERER, "TOUR HOOK: %s -> %s (%s)", e.name, ok ? "PASS" : "FAIL", lastWhy());
		return;
	}
	NOTICE_LOG(RENDERER, "TOUR HOOK: %s -> FAIL (no such hook)", name.c_str());
}

}	// namespace hooks
}	// namespace surfacetour
}	// namespace roll
