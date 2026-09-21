#include "combohunt.h"
#include "mvc2.h"
#include "fst.h"
#include "oracle.h"
#include "dojo.h"
#include "emulator.h"
#include "tasmacro.h"
#include "tas_clip.h"
#include "roll_edit.h"
#include "roll_host.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "json.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/*
	THE HUNT. See combohunt.h for what it is for; this is the loop.

	NOT A SECOND RUNNER. The Frame Skip Test (fst.cpp) already drives emuapi's
	exploration loop for ONE kind of edit - k blank rows at a row, swept over
	the four skip phases, re-run from slot 0, judged by comboPeak. The hunt keeps
	that loop and its ceremony (reload the base, edit the roll through the
	funnel, macro_armed, gui_step_frames to a stop frame, judge while Paused,
	restore) and changes only WHAT the edit is: a candidate input sequence
	written at base+1+phase+d, with the phase shift done by fst::bakeEdit itself
	so there is exactly one shifting algorithm in the tree.

	CANDIDATES, in order:
	  movie      the clip's own recorded tail from the base (nothing written -
	             if David's recording lands a hit from here, this finds it)
	  macro      dojo:ComboHuntMacro=<file> - a `_macro.txt` (David's converted
	             Dhalsim97 is the intended one); the window is the file's own
	             "CLIP LIKELY BEGINS/ENDS HERE" markers (trust the markers, not
	             the burst heuristic), overridable by dojo:ComboHuntWindow=a-b
	  singles    LP HP LK HK, each held 2 frames, at t0 offsets 0..6 (a single
	             blanks its window, so phase pin and delay collapse - measured)
	movie and macro swept over phase 0..3 (blank rows at base+1, the FST's axis).

	dojo:ComboHunt = yes | all | movie | macro | singles
	  yes      every candidate, stop at the first found=yes
	  all      every candidate, never stop (the full table)
	  others   only that candidate class, stop at the first found
*/
namespace roll {
namespace combohunt {

namespace {

struct Candidate
{
	std::string cls;			// movie | macro | singles
	std::string name;
	std::vector<u16> p1, p2;	// canon per frame from t0 (empty for "movie")
	int d = 0;					// delay after the phase pin
	int phase = 0;				// blank rows inserted at base+1
	u32 run = 60;				// frames to keep running past the sequence
};

struct Outcome
{
	bool ran = false;
	u16 peak1 = 0, peak2 = 0;
	u32 hash = 0;
	u32 frames = 0;
	u32 t0 = 0;
	std::vector<tas_mvc2::ComboSample> series;	// the meters on change, from the emulator loop (mvc2.h)
};

struct State
{
	std::string mode;
	bool armed = false, done = false;
	int step = 0;				// 0 arm, 1 base facts, 2 bake, 3 running, 4 judge, 5 finish
	double at = 0;
	fst::Movie original;
	u32 baseFrame = 0, baseHash = 0;
	int userSlot = 0;
	std::vector<Candidate> cands;
	std::vector<Outcome> out;
	int cur = -1;
	u32 stopFrame = 0;
	int found = -1;
	u32 afterHash = 0;
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

// The FST's bake ceremony: reload slot 0 around the user's slot.
static int baseSlot() { return cfgLoadInt("dojo", "ComboHuntSlot", 0); }

bool reloadBase()
{
	st.userSlot = (int)config::SavestateSlot;
	setSlot(baseSlot());
	gui_loadState();
	setSlot(st.userSlot);
	return gui_state == GuiState::Paused;
}

float readFloat(u32 addr)
{
	u32 v = 0;
	if (addr == 0 || !tas_mvc2::readRamSafe(addr, 4, v))
		return 0.f;
	float f;
	memcpy(&f, &v, sizeof(f));
	return f;
}

std::string pointChar(int player)
{
	for (int s = 0; s < 3; s++)
	{
		if (!tas_mvc2::isPoint(player, s))
			continue;
		u32 id = 0;
		std::string label;
		if (tas_mvc2::readRamSafe(tas_mvc2::addrOf("ID_2", player, s), 1, id)
				&& tas_mvc2::enumLabel("ID_2", id, label))
			return label + (s == 0 ? "" : s == 1 ? "(B)" : "(C)");
		return "id" + std::to_string(id);
	}
	return "nopoint";
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

/*
	dojo:ComboHuntMacro - David's macro as a candidate. The window comes from the
	file's own clip markers; a window that runs past the file is truncated and
	said so (122 of his 444 converted macros are shorter than their FrameMax).
*/
bool loadMacroCandidate(Candidate& c, std::string& why)
{
	const std::string path = cfgLoadStr("dojo", "ComboHuntMacro", "");
	if (path.empty()) { why = "dojo:ComboHuntMacro not set"; return false; }
	std::ifstream in(path, std::ios::binary);
	if (!in.good()) { why = "cannot read " + path; return false; }
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	tas_macro::Macro m;
	if (!tas_macro::FromText(text, m) || m.frames.empty()) { why = "no frames in " + path; return false; }
	u32 a = macroMarker(text, "CLIP LIKELY BEGINS HERE"), b = macroMarker(text, "CLIP LIKELY ENDS HERE");
	const std::string win = cfgLoadStr("dojo", "ComboHuntWindow", "");
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
		NOTICE_LOG(NETWORK, "COMBO HUNT: macro window %u-%u truncated to the file's %u frames", a, b, n);
		b = n;
	}
	if (a >= b) { why = "empty window"; return false; }
	c.cls = "macro";
	{
		std::string base = path;
		const size_t sl = base.find_last_of("/\\");
		if (sl != std::string::npos)
			base = base.substr(sl + 1);
		const size_t dot = base.find("_macro.txt");
		if (dot != std::string::npos)
			base = base.substr(0, dot);
		c.name = base + "[" + std::to_string(a) + "-" + std::to_string(b) + "]";
	}
	for (u32 f = a; f < b; f++)
	{
		c.p1.push_back(m.frames[f].p1);
		c.p2.push_back(m.frames[f].p2);
	}
	c.run = 60;
	return true;
}

void buildCandidates()
{
	st.cands.clear();
	const bool every = st.mode == "yes" || st.mode == "all";
	auto push = [&](const Candidate& base) {
		for (int ph = 0; ph < 4; ph++)
		{
			Candidate c = base;
			c.d = 0;
			c.phase = ph;
			st.cands.push_back(c);
		}
	};
	if (every || st.mode == "movie")
	{
		Candidate c;
		c.cls = "movie";
		c.name = "movie";
		c.run = (u32)cfgLoadInt("dojo", "ComboHuntMovieRun", 240);	// the recording's OWN rows from the base: how far to run them (a full combo needs ~3000; David's 2026-09-20 clip)
		push(c);
	}
	if (every || st.mode == "macro")
	{
		Candidate c;
		std::string why;
		if (loadMacroCandidate(c, why))
		{
			// dojo:ComboHuntDelays=a-b - an OFFSET axis for the macro (docs/PS2-SIDE.md §5: a
			// PS2 window re-anchored on a DC base needs a +-15 sweep x the 4 phases, because the
			// fight-start alignment between his base and ours is unknown to the frame). Default
			// 0-0 = the phase sweep alone, as before.
			int d0 = 0, d1 = 0;
			const std::string ds = cfgLoadStr("dojo", "ComboHuntDelays", "");
			if (!ds.empty())
			{
				d0 = atoi(ds.c_str());
				const size_t dash = ds.find('-', 1);
				d1 = dash == std::string::npos ? d0 : atoi(ds.c_str() + dash + 1);
				if (d1 < d0) d1 = d0;
			}
			for (int d = d0; d <= d1; d++)
			{
				Candidate v = c;
				v.d = d;
				push(v);	// push() keeps c.d? no - it zeroes d; set after
				for (int k = 0; k < 4; k++) st.cands[st.cands.size() - 4 + k].d = d;
			}
			if (d1 > d0)
				NOTICE_LOG(NETWORK, "COMBO HUNT: macro delays %d..%d x 4 phases = %d candidates", d0, d1, (d1 - d0 + 1) * 4);
		}
		else
			NOTICE_LOG(NETWORK, "COMBO HUNT: macro candidate skipped - %s", why.c_str());
	}
	if (every || st.mode == "singles")
	{
		struct B { const char *name; u16 canon; };
		static const B btns[] = {
			{ "LP", tas_macro::CANON_LP }, { "HP", tas_macro::CANON_HP },
			{ "LK", tas_macro::CANON_LK }, { "HK", tas_macro::CANON_HK },
		};
		for (const B& b : btns)
		{
			Candidate c;
			c.cls = "singles";
			c.name = b.name;
			c.p1 = { b.canon, b.canon };
			c.p2 = { 0, 0 };
			c.run = 60;
			// PHASE AND DELAY COLLAPSE FOR A SINGLE. `[MEASURED 2026-09-17]` phase=1,d=0
			// and phase=0,d=1 ended on the same hash (57DC1EEB) - a single blanks its
			// window, so the phase pin's inserted blanks and the delay are the same
			// thing: press later. 4x4 was 16 runs of 7 outcomes. Sweep t0 once.
			for (int k = 0; k <= 6; k++)
			{
				Candidate v = c;
				v.phase = std::min(k, 3);
				v.d = k - v.phase;
				st.cands.push_back(v);
			}
		}
	}
}

//! Where candidate `c`'s first row lands: base+1, shifted by the phase pin, plus the delay.
u32 t0Of(const Candidate& c)
{
	return st.baseFrame + 1 + (u32)c.phase + (u32)c.d;
}

/*
	The edit for one candidate, built from the ORIGINAL every time (never from
	the previous variant): the phase pin through fst::bakeEdit, then the rows.
	A non-movie candidate blanks its whole window first so the clip's own tail
	cannot land the hit the candidate gets credit for.
*/
bool bakeCandidate(const Candidate& c, fst::Movie& edited)
{
	fst::Sweep s;
	s.selLo = s.selHi = s.P = st.baseFrame + 1;
	s.k0 = 0;
	s.k1 = 3;
	s.fkOn = false;
	fst::normalize(s);
	if (!fst::bakeEdit(st.original, s, c.phase, edited))
		return false;
	if (c.cls == "movie")
		return true;
	const u32 t0 = t0Of(c);
	const u32 end = t0 + (u32)c.p1.size() + c.run;
	for (u32 f = st.baseFrame + 1; f < end; f++)
		edited[f] = blankRow();
	for (size_t i = 0; i < c.p1.size(); i++)
	{
		Row r = blankRow();
		r = cellInto(r, 0, c.p1[i]);
		r = cellInto(r, 1, i < c.p2.size() ? c.p2[i] : 0);
		edited[t0 + (u32)i] = r;
	}
	return true;
}

bool ready()
{
	if (dojo.frame_number.load() < 120) { st.why = "frame < 120"; return false; }
	if (!slotExists(baseSlot())) { st.why = "no slot " + std::to_string(baseSlot()); return false; }
	if (hostfs::savestateFolderOverride.empty()) { st.why = "no clip folder bound"; return false; }
	return true;
}

void writeResults()
{
	if (hostfs::savestateFolderOverride.empty())
		return;
	nlohmann::json j;
	j["schema"] = 1;
	j["createdUtc"] = tas_clip::utcNowIso();
	j["mode"] = st.mode;
	j["base"] = { { "slot", 0 }, { "frame", st.baseFrame }, { "machineHash", st.baseHash } };
	j["oracle"] = { { "field", "Combo_Meter_Value" },
			{ "addrP1", tas_mvc2::addrOf("Combo_Meter_Value", 0, 0) },
			{ "dictionary", tas_mvc2::dictionaryPath() } };
	nlohmann::json cands = nlohmann::json::array();
	for (size_t i = 0; i < st.cands.size(); i++)
	{
		const Candidate& c = st.cands[i];
		const Outcome& o = st.out[i];
		if (!o.ran)
			continue;
		nlohmann::json e;
		e["class"] = c.cls;
		e["name"] = c.name;
		e["phase"] = c.phase;
		e["d"] = c.d;
		e["t0"] = o.t0;
		e["len"] = c.p1.size();
		e["frames"] = o.frames;
		e["peakP1"] = o.peak1;
		e["peakP2"] = o.peak2;
		e["machineHash"] = o.hash;
		nlohmann::json series = nlohmann::json::array();
		for (const auto& s : o.series)
			series.push_back({ { "frame", s.frame }, { "p1", s.p1 }, { "p2", s.p2 } });
		e["combo"] = series;
		cands.push_back(e);
	}
	j["candidates"] = cands;
	j["found"] = st.found >= 0;
	if (st.found >= 0)
		j["winner"] = { { "name", st.cands[st.found].name }, { "phase", st.cands[st.found].phase },
				{ "d", st.cands[st.found].d }, { "peak", st.out[st.found].peak1 }, { "machineHash", st.afterHash } };
	const std::string path = hostfs::savestateFolderOverride + "/combohunt.json";
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (f.good())
		f << j.dump(2);
	NOTICE_LOG(NETWORK, "COMBO HUNT: results -> %s (%u candidates ran)", path.c_str(), (u32)cands.size());
}

void finish(const char *why)
{
	settings.input.fastForwardMode = false;
	if (gui_state != GuiState::Paused)
		gui_pause_for_checkout();
	// RESTORE: the original roll, and the machine back at the base.
	dojo.ApplyEditResize(st.original, "combo hunt restore");
	dojo.stale_tail_from = ~0u;
	reloadBase();
	dojo.savestate_epoch++;
	writeResults();
	int ran = 0;
	for (const Outcome& o : st.out)
		ran += o.ran ? 1 : 0;
	if (st.found >= 0)
	{
		const Candidate& c = st.cands[st.found];
		NOTICE_LOG(NETWORK, "COMBO HUNT RESULT: found=yes candidate=%s phase=%d d=%d peak=%u base=%08X after=%08X (%d candidate(s) ran)",
				c.name.c_str(), c.phase, c.d, (unsigned)st.out[st.found].peak1, st.baseHash, st.afterHash, ran);
	}
	else
	{
		NOTICE_LOG(NETWORK, "COMBO HUNT RESULT: found=no candidate=none phase=-1 d=0 peak=0 base=%08X after=%08X (%s; %d candidate(s) ran)",
				st.baseHash, st.afterHash, why, ran);
	}
	st.done = true;
}

}	// namespace

bool running() { return st.armed && !st.done; }

void tick()
{
	if (st.done)
		return;
	if (!st.armed)
	{
		if (st.mode.empty())
		{
			st.mode = cfgLoadStr("dojo", "ComboHunt", "");
			if (st.mode.empty() || st.mode == "no")
			{
				st.done = true;		// never armed: nothing to say, nothing to restore
				return;
			}
		}
		if (!ready())
			return;
		// THE CEREMONY (fst::armFixedSweep): Paused, WRITE-authoring, base reloaded.
		gui_pause_for_checkout();
		dojo.play_match = false;
		st.original = dojo.session_inputs;
		if (!reloadBase())
		{
			finish("the base state did not load");
			return;
		}
		st.armed = true;
		st.step = 1;
		st.at = os_GetSeconds();
		return;
	}
	const double now = os_GetSeconds();
	switch (st.step)
	{
	case 1:		// base facts, read while stopped
	{
		if (!oracle::machineStopped())
		{
			if (now - st.at > 5.0) finish("the base never stopped");
			return;
		}
		st.baseFrame = dojo.frame_number.load();
		st.baseHash = oracle::machineHash();
		st.afterHash = st.baseHash;
		u8 rate = 0, count = 0, toggle = 0;
		tas_mvc2::peekSkip(rate, count, toggle);
		u16 c1 = 0, c2 = 0;
		tas_mvc2::peekCombo(c1, c2);
		const float dist = readFloat(tas_mvc2::addrOf("X_Position_From_Enemy", 0, 0));
		NOTICE_LOG(NETWORK, "COMBO HUNT: base slot%d@%u hash=%08X p1=%s p2=%s dist=%.0f skip=%u/%u combo=%u/%u movie=%u rows mode=%s",
				baseSlot(), 				st.baseFrame, st.baseHash, pointChar(0).c_str(), pointChar(1).c_str(), dist,
				(unsigned)count, (unsigned)rate, (unsigned)c1, (unsigned)c2, (u32)st.original.size(), st.mode.c_str());
		if (rate == 0)
			NOTICE_LOG(NETWORK, "COMBO HUNT: the skip system reads 0/0 at the base - this base is NOT in a match; no candidate can connect here");
		buildCandidates();
		st.out.assign(st.cands.size(), Outcome());
		if (st.cands.empty())
		{
			finish("no candidates");
			return;
		}
		NOTICE_LOG(NETWORK, "COMBO HUNT: %u candidate(s) queued", (u32)st.cands.size());
		st.cur = 0;
		st.step = 2;
		st.at = now;
		return;
	}
	case 2:		// bake the current candidate and set it running
	{
		if (gui_state != GuiState::Paused)
		{
			if (now - st.at > 5.0) finish("not paused before a bake");
			return;
		}
		const Candidate& c = st.cands[st.cur];
		if (st.cur > 0 && !reloadBase())
		{
			finish("the base state did not reload");
			return;
		}
		if (dojo.frame_number.load() != st.baseFrame)
		{
			finish("the base reloaded at the wrong frame");
			return;
		}
		fst::Movie edited;
		if (!bakeCandidate(c, edited))
		{
			finish("the bake failed");
			return;
		}
		const s64 first = dojo.ApplyEditResize(edited, "combo hunt");
		if (first < 0 && edited != dojo.session_inputs)
		{
			finish("the edit funnel refused the candidate");
			return;
		}
		dojo.stale_tail_from = ~0u;
		dojo.macro_armed = true;
		tas_mvc2::comboPeakReset();
		tas_mvc2::comboSeriesReset();
		Outcome& o = st.out[st.cur];
		o.t0 = t0Of(c);
		st.stopFrame = o.t0 + (u32)c.p1.size() + c.run;
		{
			const u32 fr = dojo.frame_number.load();
			gui_step_frames(st.stopFrame > fr ? (int)(st.stopFrame - fr) : 1);
		}
		settings.input.fastForwardMode = true;
		st.step = 3;
		st.at = now;
		return;
	}
	case 3:		// run to the stop frame (the series records itself on the emulator loop)
	{
		if (gui_state == GuiState::Paused)
		{
			if (dojo.frame_number.load() >= st.stopFrame)
				st.step = 4;
			else if (now - st.at > 2.0)
				finish("the run did not start");
		}
		else if (now - st.at > (double)cfgLoadInt("dojo", "ComboHuntRunS", 180))
			finish("timeout");	// dojo:ComboHuntRunS: a 6000-row window under fast-forward needs ~5 min (David's 2026-09-20 clip)
		return;
	}
	case 4:		// judge while stopped
	{
		settings.input.fastForwardMode = false;
		if (!oracle::machineStopped())
			return;
		const Candidate& c = st.cands[st.cur];
		Outcome& o = st.out[st.cur];
		o.ran = true;
		o.peak1 = tas_mvc2::comboPeak(0);
		o.peak2 = tas_mvc2::comboPeak(1);
		o.hash = oracle::machineHash();
		o.frames = dojo.frame_number.load() - st.baseFrame;
		o.series = tas_mvc2::comboSeriesTake();
		NOTICE_LOG(NETWORK, "COMBO HUNT: candidate %s phase=%d d=%d peak=%u hash=%08X frames=%u (t0=%u len=%u p2peak=%u)",
				c.name.c_str(), c.phase, c.d, (unsigned)o.peak1, o.hash, o.frames, o.t0, (u32)c.p1.size(), (unsigned)o.peak2);
		if (o.peak1 >= 1 && st.found < 0)
		{
			st.found = st.cur;
			st.afterHash = o.hash;
			if (st.mode != "all")
			{
				st.step = 5;
				return;
			}
		}
		st.cur++;
		st.step = st.cur >= (int)st.cands.size() ? 5 : 2;
		st.at = now;
		return;
	}
	case 5:
		if (gui_state != GuiState::Paused)
			return;
		finish("no candidate raised Combo_Meter_Value above 0");
		return;
	}
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	tas_mvc2::selfTest();		// SPREADSHEET SELFTEST: the field dictionary the hunt's oracle resolves through
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "COMBOHUNT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	// THE PURE PARTS: where a candidate lands, and that the phase pin is the
	// FST's own shift (one algorithm), on a movie we control.
	st = State();
	st.baseFrame = 1000;
	st.original.clear();
	st.original[1000] = blankRow();
	st.original[1001] = cellInto(blankRow(), 0, tas_macro::CANON_HK);	// the clip's own next row
	Candidate c;
	c.cls = "singles";
	c.name = "LP";
	c.p1 = { tas_macro::CANON_LP, tas_macro::CANON_LP };
	c.p2 = { 0, 0 };
	c.run = 10;
	c.phase = 2;
	c.d = 1;
	claim("t0 = base+1+phase+d", t0Of(c) == 1004);
	fst::Movie e;
	claim("a candidate bakes", bakeCandidate(c, e));
	claim("the candidate's rows carry LP at t0 and t0+1",
			cellOf(e[1004], 0) == tas_macro::CANON_LP && cellOf(e[1005], 0) == tas_macro::CANON_LP);
	claim("...and nothing at t0+2", cellOf(e[1006], 0) == 0);
	claim("the clip's own next row is blanked inside the window (the tail cannot land the hit)",
			cellOf(e[1001], 0) == 0 && cellOf(e[1003], 0) == 0);
	claim("the P2 lane stays neutral", cellOf(e[1004], 1) == 0);
	c.phase = 0;
	c.d = 0;
	claim("phase 0 d 0 lands on base+1", t0Of(c) == 1001 && bakeCandidate(c, e) && cellOf(e[1001], 0) == tas_macro::CANON_LP);
	Candidate m;
	m.cls = "movie";
	m.name = "movie";
	m.phase = 1;
	claim("a movie candidate keeps the clip's rows, shifted by the phase (fst::bakeEdit)",
			bakeCandidate(m, e) && cellOf(e[1002], 0) == tas_macro::CANON_HK && cellOf(e[1001], 0) == 0);
	m.phase = 0;
	claim("...and unshifted at phase 0", bakeCandidate(m, e) && e == st.original);
	{
		const std::string text = "# CLIP LIKELY BEGINS HERE: frame 42  (~0.7s)\n# CLIP LIKELY ENDS HERE:   frame 99\n.\n";
		claim("clip markers parse", macroMarker(text, "CLIP LIKELY BEGINS HERE") == 42 && macroMarker(text, "CLIP LIKELY ENDS HERE") == 99);
		claim("...and a missing marker is 0", macroMarker(text, "NO SUCH MARKER") == 0);
	}
	{
		// The singles table: seven DISTINCT t0 per button, none repeated (the
		// measured phase/delay collapse), and the movie/macro classes untouched.
		st.mode = "singles";
		buildCandidates();
		bool distinct = st.cands.size() == 28;
		for (size_t i = 0; distinct && i < st.cands.size(); i++)
			for (size_t k = i + 1; k < st.cands.size(); k++)
				if (st.cands[i].name == st.cands[k].name && t0Of(st.cands[i]) == t0Of(st.cands[k]))
					distinct = false;
		claim("singles sweep 7 distinct t0 per button (28 runs, no phase/delay duplicates)", distinct);
		st.mode = "movie";
		buildCandidates();
		claim("the movie class is four phases", st.cands.size() == 4 && st.cands[3].phase == 3 && st.cands[3].d == 0);
	}
	st = State();
	NOTICE_LOG(RENDERER, "COMBOHUNT SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace combohunt
}	// namespace roll
