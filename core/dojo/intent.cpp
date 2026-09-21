#include "intent.h"
#include "mvc2.h"
#include "fst.h"
#include "oracle.h"
#include "dojo.h"
#include "emulator.h"
#include "tasmacro.h"
#include "roll_edit.h"
#include "roll_host.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

/*
	Lifted from combohunt.cpp (the hunt's tick(), 2026-09-17) - the same steps under
	names a tour step can call one at a time. Where the hunt's code is quoted, it is
	quoted verbatim; nothing here decides anything the hunt did not.
*/
namespace roll {
namespace intent {

namespace {

struct State
{
	bool begun = false;
	bool factsRead = false;
	fst::Movie original;		// the roll as the module found it
	u32 baseFrame = 0, baseHash = 0;
	int userSlot = 0;
	u32 runTarget = 0;			// the stop frame a runToStop() armed; 0 = none
	bool running = false;
	// the combo window
	bool comboTried = false, comboOk = false;
	std::string comboName;
	std::vector<u16> p1, p2;
	int phase = 0, d = 0;
	u32 run = 60;
	std::string why;
} st;

bool slotExists(int slot)
{
	SlotView v;
	return host() != nullptr && host()->slotView(slot, v) && v.exists;
}

void setSlot(int slot)
{
	config::SavestateSlot.set(slot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(slot));
}

// dojo:IntentSlot `[2026-09-20]`: which slot is BASE for the ceremony. 0 (the harness base) by
// default; the hand module stands on David's ironman98 state 3 (slot 3, frame 16215).
int intentSlot() { return cfgLoadInt("dojo", "IntentSlot", 0); }

// combohunt.cpp: the FST's bake ceremony - reload BASE around the user's slot.
bool reloadBaseRaw()
{
	st.userSlot = (int)config::SavestateSlot;
	setSlot(intentSlot());
	gui_loadState();
	setSlot(st.userSlot);
	return gui_state == GuiState::Paused;
}

u32 macroMarker(const std::string& text, const char *marker)
{
	const size_t at = text.find(marker);
	if (at == std::string::npos)
		return 0;
	const size_t fr = text.find("frame ", at);
	if (fr == std::string::npos)
		return 0;
	return (u32)strtoul(text.c_str() + fr + 6, nullptr, 10);
}

// combohunt.cpp loadMacroCandidate, on dojo:IntentMacro / dojo:IntentWindow.
bool loadCombo(std::string& why)
{
	const std::string path = cfgLoadStr("dojo", "IntentMacro", "");
	if (path.empty()) { why = "dojo:IntentMacro not set"; return false; }
	std::ifstream in(path, std::ios::binary);
	if (!in.good()) { why = "cannot read " + path; return false; }
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	tas_macro::Macro m;
	if (!tas_macro::FromText(text, m) || m.frames.empty()) { why = "no frames in " + path; return false; }
	u32 a = macroMarker(text, "CLIP LIKELY BEGINS HERE"), b = macroMarker(text, "CLIP LIKELY ENDS HERE");
	const std::string win = cfgLoadStr("dojo", "IntentWindow", "");
	if (!win.empty())
	{
		a = (u32)strtoul(win.c_str(), nullptr, 10);
		const size_t dash = win.find('-');
		b = dash == std::string::npos ? 0 : (u32)strtoul(win.c_str() + dash + 1, nullptr, 10);
	}
	if (b == 0 || b <= a)
	{
		a = 0;
		b = (u32)m.frames.size();
	}
	const u32 n = (u32)m.frames.size();
	if (b > n)
	{
		NOTICE_LOG(NETWORK, "INTENT: macro window %u-%u truncated to the file's %u frames", a, b, n);
		b = n;
	}
	if (a >= b) { why = "empty window"; return false; }
	std::string base = path;
	const size_t sl = base.find_last_of("/\\");
	if (sl != std::string::npos)
		base = base.substr(sl + 1);
	const size_t dot = base.find("_macro.txt");
	if (dot != std::string::npos)
		base = base.substr(0, dot);
	st.comboName = base + "[" + std::to_string(a) + "-" + std::to_string(b) + "]";
	st.p1.clear();
	st.p2.clear();
	for (u32 f = a; f < b; f++)
	{
		st.p1.push_back(m.frames[f].p1);
		st.p2.push_back(m.frames[f].p2);
	}
	return true;
}

// combohunt.cpp bakeCandidate: the phase pin through fst::bakeEdit, then the rows -
// or, for clearCombo, the window blanked and nothing laid down.
bool bake(bool lay, fst::Movie& edited)
{
	fst::Sweep s;
	s.selLo = s.selHi = s.P = st.baseFrame + 1;
	s.k0 = 0;
	s.k1 = 3;
	s.fkOn = false;
	fst::normalize(s);
	if (!fst::bakeEdit(st.original, s, st.phase, edited))
		return false;
	const u32 first = t0();
	const u32 end = first + (u32)st.p1.size() + st.run;
	for (u32 f = st.baseFrame + 1; f < end; f++)
		edited[f] = blankRow();
	if (!lay)
		return true;
	for (size_t i = 0; i < st.p1.size(); i++)
	{
		Row r = blankRow();
		r = cellInto(r, 0, st.p1[i]);
		r = cellInto(r, 1, i < st.p2.size() ? st.p2[i] : 0);
		edited[first + (u32)i] = r;
	}
	return true;
}

bool install(const fst::Movie& edited, const char *who)
{
	const s64 first = dojo.ApplyEditResize(edited, who);
	if (first < 0 && edited != dojo.session_inputs)
	{
		st.why = "the edit funnel refused the bake";
		return false;
	}
	dojo.stale_tail_from = ~0u;
	dojo.macro_armed = true;
	return true;
}

}	// namespace

bool ready(std::string& why)
{
	if (dojo.frame_number.load() < 120) { why = "frame < 120"; return false; }
	if (!slotExists(intentSlot())) { why = "no slot " + std::to_string(intentSlot()); return false; }
	if (hostfs::savestateFolderOverride.empty()) { why = "no clip folder bound"; return false; }
	return true;
}

bool begin(const char *who)
{
	if (!ready(st.why))
		return false;
	// THE CEREMONY (fst::armFixedSweep): Paused, WRITE-authoring, base reloaded.
	gui_pause_for_checkout();
	dojo.play_match = false;
	st.original = dojo.session_inputs;
	st.factsRead = false;
	st.runTarget = 0;
	st.running = false;
	if (!reloadBaseRaw())
	{
		st.why = "the base state did not load";
		return false;
	}
	st.begun = true;
	NOTICE_LOG(NETWORK, "INTENT: %s - begin: roll snapshot %u rows, BASE reloading", who, (u32)st.original.size());
	return true;
}

bool settled()
{
	if (st.running)
	{
		if (gui_state != GuiState::Paused)
			return false;
		if (dojo.frame_number.load() < st.runTarget)
			return false;
		settings.input.fastForwardMode = false;
		st.running = false;
	}
	if (!oracle::machineStopped())
		return false;
	if (!st.factsRead)
	{
		st.baseFrame = dojo.frame_number.load();
		st.baseHash = oracle::machineHash();
		st.factsRead = true;
		NOTICE_LOG(NETWORK, "INTENT: base slot0@%u hash=%08X", st.baseFrame, st.baseHash);
	}
	return true;
}

int baseSlot() { return intentSlot(); }
u32 baseFrame() { return st.baseFrame; }
u32 baseHash() { return st.baseHash; }
u32 machineHash() { return oracle::machineHash(); }

bool comboLoaded(std::string& why)
{
	if (!st.comboTried)
	{
		st.comboTried = true;
		st.comboOk = loadCombo(st.why);
		if (st.comboOk)
			NOTICE_LOG(NETWORK, "INTENT: combo %s - %u rows", st.comboName.c_str(), (u32)st.p1.size());
		else
			NOTICE_LOG(NETWORK, "INTENT: no combo - %s", st.why.c_str());
	}
	why = st.why;
	return st.comboOk;
}

u32 comboLen() { return (u32)st.p1.size(); }
const char *comboName() { return st.comboName.c_str(); }
u32 t0() { return st.baseFrame + 1 + (u32)st.phase + (u32)st.d; }
u32 stopFrame() { return t0() + (u32)st.p1.size() + st.run; }

bool placeCombo(int phase, int d)
{
	std::string why;
	if (!st.begun || !st.factsRead) { st.why = "begin()/settled() first"; return false; }
	if (!comboLoaded(why)) return false;
	if (dojo.frame_number.load() != st.baseFrame) { st.why = "not at the base frame"; return false; }
	st.phase = phase;
	st.d = d;
	fst::Movie edited;
	if (!bake(true, edited)) { st.why = "the bake failed"; return false; }
	return install(edited, "intent: place combo");
}

bool clearCombo()
{
	std::string why;
	if (!st.begun || !st.factsRead) { st.why = "begin()/settled() first"; return false; }
	if (!comboLoaded(why)) return false;
	if (dojo.frame_number.load() != st.baseFrame) { st.why = "not at the base frame"; return false; }
	fst::Movie edited;
	if (!bake(false, edited)) { st.why = "the bake failed"; return false; }
	return install(edited, "intent: clear combo");
}

bool runToFrame(u32 target)
{
	if (gui_state != GuiState::Paused) { st.why = "not paused before the run"; return false; }
	// READ-WRITE for the run: the roll drives the guest (install()'s arm). `[MEASURED 2026-09-20]`
	// without it the hand tour's run recorded the neutral pad over 26 authored strokes and read
	// the meter at BASE - WRITE clobbers every frame it passes (dojo.h, macro_armed).
	dojo.stale_tail_from = ~0u;
	dojo.macro_armed = true;
	tas_mvc2::comboPeakReset();
	tas_mvc2::comboSeriesReset();
	st.runTarget = target;
	const u32 fr = dojo.frame_number.load();
	gui_step_frames(st.runTarget > fr ? (int)(st.runTarget - fr) : 1);
	// dojo:TourRealtime=yes (the --watch run): the game plays at speed so the combo can be SEEN;
	// headless it fast-forwards. The user, watching: "the movie played at like really fast speed".
	settings.input.fastForwardMode = !cfgLoadBool("dojo", "TourRealtime", false);
	st.running = true;
	return true;
}

bool runToStop()
{
	if (gui_state != GuiState::Paused) { st.why = "not paused before the run"; return false; }
	tas_mvc2::comboPeakReset();
	tas_mvc2::comboSeriesReset();
	st.runTarget = stopFrame();
	const u32 fr = dojo.frame_number.load();
	gui_step_frames(st.runTarget > fr ? (int)(st.runTarget - fr) : 1);
	settings.input.fastForwardMode = true;
	st.running = true;
	return true;
}

u16 peak(int player) { return tas_mvc2::comboPeak(player); }
u16 pinnedPeak() { return (u16)cfgLoadInt("dojo", "IntentPeak", 19); }

bool reloadBase()
{
	if (!reloadBaseRaw()) { st.why = "the base state did not reload"; return false; }
	return true;
}

bool end()
{
	if (!st.begun) { st.why = "not begun"; return false; }
	if (gui_state != GuiState::Paused) { st.why = "not paused at end"; return false; }
	if (!install(st.original, "intent: restore"))
		return false;
	st.begun = false;
	return reloadBase();
}

const char *lastWhy() { return st.why.c_str(); }

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int passed = 0, failed = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? passed : failed)++;
		NOTICE_LOG(RENDERER, "INTENT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	const std::string text = "# x\n# CLIP LIKELY BEGINS HERE: frame 4212  (~70s)\n.\n# CLIP LIKELY ENDS HERE: frame 5699\n";
	claim("the BEGINS marker parses to its frame", macroMarker(text, "CLIP LIKELY BEGINS HERE") == 4212);
	claim("the ENDS marker parses to its frame", macroMarker(text, "CLIP LIKELY ENDS HERE") == 5699);
	claim("a missing marker is 0", macroMarker(text, "NO SUCH MARKER") == 0);
	st.baseFrame = 9928; st.phase = 2; st.d = 1; st.p1.assign(10, 0); st.run = 60;
	claim("t0 = base + 1 + phase + d", t0() == 9932);
	claim("stopFrame = t0 + rows + run-out", stopFrame() == 9932 + 10 + 60);
	st.baseFrame = 0; st.phase = 0; st.d = 0; st.p1.clear();
	NOTICE_LOG(RENDERER, "INTENT SELFTEST: %d passed, %d failed", passed, failed);
}

}	// namespace intent
}	// namespace roll
