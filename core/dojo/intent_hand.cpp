/*
	INTENT MODULE `hand`: the combo authored BY HAND in the piano roll (2026-09-20).

	The user, after the ironman98 chase: "can you make a tour which programs this combo -
	by hand (but be efficient, you can do it using shortcuts in the ui e.g. drag and hold
	frames)". This is that tour. It stands on David's own state 3 (frame 16215, the meter
	at 26 mid-string) and authors, through the roll panel's OWN gestures, the 26 strokes a
	hand makes for the rest of the string - hold v six rows, hold HP nine, hold v seventeen
	under the LP mash, the A1+A2 press and the A1 taps that fire the Team Hyper, the 42-row
	v hold under Storm's hail - and then runs the game: the meter must read the video's 94.

	FOUR BEATS PER STEP (docs/tour-intent-roll.md): OPEN the roll with its tour chord, ACT
	through the panel's own path (rollpanel::blankRange / strokeColumn / tapCell - the
	Blank button's body, the paint stroke's begin/extendTo/build, the cell click), INTENT -
	the game runs and the fighters do the thing, read on Combo_Meter_Value's peak - CLOSE.

	THE HONESTY BEAT FIRST: the segment is BLANKED and run before a single stroke lands.
	A blank roll from this state lands NOTHING new (the peak stays at the meter BASE
	already shows), so the 94 at the end is the hand's, not the recording's.

	DATA, staged by scripts/handtour.sh from the RECIPE's [david_ironman]:
	  dojo:HandStrokes=<file>   lines "<label> <lo> <hi>" (absolute frames; tools/hand_strokes.py
	                            derives them from the macro - the runs of each column)
	  dojo:HandStop=<frame>     the run's stop (the string has dropped by 16544; 16560)
	  dojo:HandPeak=<n>         the video's number (94)
	  dojo:IntentSlot=<slot>    BASE for the ceremony (3)

	ARM (scripts/lib/arms.sh): hand-thc - the A1+A2 press at 16274..16275 is skipped, so the
	Team Hyper never fires: "hand intent: run - the hand's combo lands" must redden and
	"hand intent: run - nothing lands on a blank roll" must stay green.
*/
#include "surface_tour.h"
#include "intent.h"
#include "roll_edit.h"
#include "roll_profile.h"
#include "dojo.h"
#include "mvc2.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "input/mapping.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstdlib>
#include <deque>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace roll {

namespace rollpanel { s64 blankRange(u32 lo, u32 hi); s64 strokeColumn(u32 lo, u32 hi, int player, const char *label, int gap); s64 brushStroke(u32 lo, u32 hi, int player, const char *labels, int gap); s64 tapCell(u32 f, int player, const char *label);
                      void gesture(u32 lo, u32 hi, int player, const char *labels, double ms); void gestureEnd(); void gestureFollow(u32 row); }

namespace surfacetour {

namespace {

const u32 KEY_PIANOROLL = InputMapping::KEY_MOD_CTRL | 58;	// Ctrl+F1, the tour's chord for the roll

struct Stroke { std::string label; u32 lo, hi; };

// Step names built per stroke must outlive the vector they are pushed into (surface_tour.cpp's
// own keep(): a deque never moves what it holds).
static std::deque<std::string> g_handNames;
const char *keepName(const std::string& s) { g_handNames.push_back(s); return g_handNames.back().c_str(); }

struct ModState
{
	std::vector<Stroke> strokes;
	bool loaded = false;
	u32 stop = 0;
	u16 want = 0;
	u16 meterAtBase = 0;
	int gestureMs = 0;			// dojo:HandGestureMs - the visible drag's travel time (0 = the tour's arm time, no override)
	int tapeSlot = -1;			// dojo:HandTapeSlot - the combo's start on the tape, replayed whole at the end (-1 = skip)
	u32 tapeFrame = 0;
	std::string why;
} ms;

bool loadStrokes()
{
	if (ms.loaded) return !ms.strokes.empty();
	ms.loaded = true;
	const std::string path = cfgLoadStr("dojo", "HandStrokes", "");
	ms.stop = (u32)cfgLoadInt("dojo", "HandStop", 0);
	ms.want = (u16)cfgLoadInt("dojo", "HandPeak", 0);
	ms.gestureMs = cfgLoadInt("dojo", "HandGestureMs", 0);
	ms.tapeSlot = cfgLoadInt("dojo", "HandTapeSlot", -1);
	if (path.empty()) { ms.why = "dojo:HandStrokes not set"; return false; }
	std::ifstream in(path);
	if (!in.good()) { ms.why = "cannot read " + path; return false; }
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		Stroke s;
		if (!(ss >> s.label >> s.lo >> s.hi) || s.hi < s.lo) continue;
		ms.strokes.push_back(s);
	}
	if (ms.strokes.empty()) ms.why = "no strokes in " + path;
	NOTICE_LOG(NETWORK, "HAND INTENT: %u stroke(s) from %s, stop %u, want %u", (u32)ms.strokes.size(), path.c_str(), ms.stop, (unsigned)ms.want);
	return !ms.strokes.empty();
}

// A stroke label is one column ("HP") or a brush of several ("v>" - a diagonal, "LPHP" - two
// punches): the columns it names, longest label first so "LP" is never read as "L"+"P".
std::vector<const Column *> columnsOf(const std::string& label)
{
	const Profile& p = profile();
	std::vector<const Column *> out;
	std::string rest = label;
	while (!rest.empty())
	{
		int hit = -1;
		for (int i = 0; i < p.count; i++)
			if (rest.rfind(p.cols[i].label, 0) == 0 && (hit < 0 || strlen(p.cols[i].label) > strlen(p.cols[hit].label))) hit = i;
		if (hit < 0) return {};
		out.push_back(&p.cols[hit]);
		rest = rest.substr(strlen(p.cols[hit].label));
	}
	return out;
}

bool rowsHold(u32 lo, u32 hi, const std::vector<const Column *>& cs, bool on)
{
	for (u32 f = lo; f <= hi; f++)
	{
		auto it = dojo.session_inputs.find(f);
		for (const Column *c : cs)
		{
			const bool has = it != dojo.session_inputs.end() && rowHas(it->second, 0, *c);
			if (has != on) return false;
		}
	}
	return true;
}

bool segmentBlank(u32 lo, u32 hi)
{
	for (u32 f = lo; f <= hi; f++)
	{
		auto it = dojo.session_inputs.find(f);
		if (it != dojo.session_inputs.end() && it->second != blankRow()) return false;
	}
	return true;
}

bool panelIs(const char *id, bool open)
{
	const panels::Panel *p = panels::find(id);
	if (p == nullptr) { why("no such panel: %s", id); return false; }
	if (*p->open != open) { why("open=%s", *p->open ? "true" : "false"); return false; }
	why("%s", open ? "open" : "closed");
	return true;
}

// ---- step shapes (the roll module's, restated for this TU) ---------------------------
Step click(const std::string& name, std::function<bool()> act, std::function<bool()> verify, bool needsPrev = false)
{
	Step s;
	s.name = keepName(name);
	s.kind = Kind::Click;
	s.needsPrev = needsPrev;
	s.act = [act] { why(""); return act(); };
	s.verify = std::move(verify);
	return s;
}

Step runStep(const char *name, std::function<u16()> want, const char *what)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.needsPrev = true;
	s.maxWaitMs = 120000;		// ~350 frames under fast-forward on a shared box
	s.act = [] { why(""); if (!intent::runToFrame(ms.stop)) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [want, what] {
		if (!intent::settled()) return false;
		const u16 p = intent::peak(0), w = want();
		why("peak=%u (want %u) at frame %u - %s", (unsigned)p, (unsigned)w, dojo.frame_number.load(), p == w ? what : "NOT what the hand meant");
		return p == w;
	};
	return s;
}

Step reloadStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 10000;
	s.act = [] { why(""); if (!intent::reloadBase()) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [] {
		if (!intent::settled()) return false;
		const u32 h = intent::machineHash();
		why("BASE @%u hash=%08X%s", dojo.frame_number.load(), h, h == intent::baseHash() ? "" : " (NOT the base hash)");
		return dojo.frame_number.load() == intent::baseFrame() && h == intent::baseHash();
	};
	return s;
}

void addSteps(std::vector<Step>& out)
{
	if (!loadStrokes())
	{
		NOTICE_LOG(NETWORK, "HAND INTENT: not staged (%s) - no steps", ms.why.c_str());
		return;
	}
	out.push_back(click("hand intent: open the piano roll", [] { return injectKey(KEY_PIANOROLL); }, [] { return panelIs("pianoroll", true); }));
	{
		Step s;
		s.name = "hand intent: begin on BASE (David's state, mid-string)";
		s.kind = Kind::Record;
		s.maxWaitMs = 10000;
		s.act = [] { why(""); if (!intent::begin("hand intent")) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] {
			if (!intent::settled()) return false;
			u16 p1 = 0, p2 = 0;
			tas_mvc2::peekCombo(p1, p2);
			ms.meterAtBase = p1;
			why("BASE = slot %d @%u hash=%08X, the meter reads %u", intent::baseSlot(), intent::baseFrame(), intent::baseHash(), (unsigned)p1);
			return true;
		};
		out.push_back(s);
	}
	// THE HONESTY BEAT: blank the recording's segment, run, nothing new lands.
	{
		Step b = click("hand intent: blank the segment after BASE (panel Blank)",
			[] { const u32 lo = intent::baseFrame() + 1; return rollpanel::blankRange(lo, ms.stop) >= 0 || segmentBlank(lo, ms.stop); },
			[] {
				const u32 lo = intent::baseFrame() + 1;
				rollpanel::gestureEnd();
				if (!segmentBlank(lo, ms.stop)) { why("rows %u..%u still hold input", lo, ms.stop); return false; }
				why("rows %u..%u blank through the Blank button's body", lo, ms.stop);
				return true;
			}, true);
		b.begin = [] { rollpanel::gesture(intent::baseFrame() + 1, intent::baseFrame() + 48, 0, "", ms.gestureMs > 0 ? ms.gestureMs : 300); return true; };
		if (ms.gestureMs > 0) b.armMs = ms.gestureMs;
		out.push_back(b);
	}
	out.push_back(runStep("hand intent: run - nothing lands on a blank roll (the meter stays at BASE's)",
		[] { return ms.meterAtBase; }, "the recording is out of it; what lands next is the hand's"));
	out.push_back(reloadStep("hand intent: reload BASE"));
	// THE STROKES: each one the gesture a mouse makes - press a cell, drag to the last row.
	for (size_t i = 0; i < ms.strokes.size(); i++)
	{
		const Stroke& st = ms.strokes[i];
		const bool tap = st.lo == st.hi;
		const bool thc = st.label.find("A2") != std::string::npos && st.lo == 16274;	// the arm's target: the Team Hyper press
		std::string name = "hand intent: " + std::string(tap ? "tap " : columnsOf(st.label).size() > 1 ? "brush " : "drag ") + st.label + " " + std::to_string(st.lo)
				+ (tap ? "" : ".." + std::to_string(st.hi)) + " (" + std::to_string(st.hi - st.lo + 1) + " row" + (tap ? "" : "s") + ")";
		Step sst = click(name,
			[i, tap, thc] {
				const Stroke& s = ms.strokes[i];
				if (thc && sabotaged("hand-thc")) { why("hand-thc: the press is skipped"); return true; }
				const bool brush = columnsOf(s.label).size() > 1;
				const s64 first = brush ? rollpanel::brushStroke(s.lo, s.hi, 0, s.label.c_str(), 0)
								: tap   ? rollpanel::tapCell(s.lo, 0, s.label.c_str())
										: rollpanel::strokeColumn(s.lo, s.hi, 0, s.label.c_str(), 0);
				if (first < 0) { why("the panel refused the %s", tap ? "tap" : "stroke"); return false; }
				return true;
			},
			[i] {
				const Stroke& s = ms.strokes[i];
				const std::vector<const Column *> cs = columnsOf(s.label);
				if (cs.empty()) { why("no column %s in this profile", s.label.c_str()); return false; }
				if (!rowsHold(s.lo, s.hi, cs, true)) { rollpanel::gestureEnd(); why("%s is not held on every row %u..%u", s.label.c_str(), s.lo, s.hi); return false; }
				// THE FRAME ADVANCES WITH THE HAND (the user: "as you make the frames with the hand etc,
				// you need to increment the frame you're inputting"): the stroke is written, then the
				// game steps one frame per tick up to its last row - through any gap before it - with
				// the pointer riding the playhead, the way a human draws a hold and then advances
				// through it watching. READ-WRITE for the steps, so a released pad preserves the rows.
				const u32 fr = dojo.frame_number.load();
				if (fr < s.hi)
				{
					if (gui_state != GuiState::Paused) return false;		// the previous frame is still running
					intent::armRoll();
					rollpanel::gestureFollow(fr + 1);
					gui_step_frames(1);
					return false;
				}
				if (gui_state != GuiState::Paused) return false;		// the last frame settles before the gate reads the machine
				rollpanel::gestureEnd();
				why("%s held on %u..%u; the game stepped to %u with it", s.label.c_str(), s.lo, s.hi, fr);
				return true;
			}, i == 0);
		sst.maxWaitMs = 60000;		// a 64-frame gap plus a 42-row hold, one frame per tick
		// THE VISIBLE HAND: from begin the roll centres on the rows and a large pointer travels the
		// stroke over the arm time (dojo:HandGestureMs when set - the --watch run's 1500 ms); the
		// act at the end of that travel is the commit, and the cells fill under the pointer.
		sst.begin = [i] {
			const Stroke& s = ms.strokes[i];
			rollpanel::gesture(s.lo, s.hi, 0, s.label.c_str(), ms.gestureMs > 0 ? ms.gestureMs : 300);
			return true;
		};
		if (ms.gestureMs > 0) sst.armMs = ms.gestureMs;
		out.push_back(sst);
	}
	out.push_back(reloadStep("hand intent: reload BASE (the hand's rows stay)"));
	out.push_back(runStep("hand intent: run - the hand's combo lands (the video's number)",
		[] { return ms.want; }, "the fighters did the thing the hand wrote"));
	// THE TAPE, WHOLE (the user: "replay the whole combo from the tape manually after you're done"):
	// load the combo's start (slot 1 on David's clip) and play the movie through to the stop in READ -
	// the whole string, the recording's rows up to BASE and the hand's after it - the meter reads 94.
	if (ms.tapeSlot >= 0)
	{
		Step l;
		l.name = "hand intent: load the combo's start on the tape";
		l.kind = Kind::Record;
		l.maxWaitMs = 10000;
		l.act = [] { why(""); if (!intent::loadSlot(ms.tapeSlot)) { why("%s", intent::lastWhy()); return false; } return true; };
		l.verify = [] {
			if (!intent::settled()) return false;
			ms.tapeFrame = dojo.frame_number.load();
			why("slot %d @%u, hash=%08X", ms.tapeSlot, ms.tapeFrame, intent::machineHash());
			return ms.tapeFrame < intent::baseFrame();
		};
		out.push_back(l);
		out.push_back(runStep("hand intent: replay the whole combo from the tape", [] { return ms.want; }, "the whole string, from the tape"));
	}
	out.push_back(click("hand intent: close the piano roll", [] { return injectKey(KEY_PIANOROLL); }, [] { return panelIs("pianoroll", false); }));
	{
		Step s;
		s.name = "hand intent: end (the recording's rows restored, BASE reloaded)";
		s.kind = Kind::Record;
		s.maxWaitMs = 10000;
		s.act = [] { why(""); if (!intent::end()) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] {
			if (!intent::settled()) return false;
			const u32 h = intent::machineHash();
			why("roll restored, BASE @%u hash=%08X", dojo.frame_number.load(), h);
			return h == intent::baseHash();
		};
		out.push_back(s);
	}
}

const ArmSpec ARMS[] = {
	{ "hand-thc", "hand intent: run - the hand's combo lands (the video's number)", "hand intent: run - nothing lands on a blank roll (the meter stays at BASE's)" },
};

const ExpectDecl EXPECTS[] = {
	{ "hand intent: run - nothing lands",   1, 2, false, "mover" },
	{ "hand intent: run - the hand's combo", 1, 2, false, "mover" },
	{ "hand intent: open",                  0, 0, false, "ui" },
	{ "hand intent: begin",                 2, 0, false, "any" },
	{ "hand intent: blank",                 0, 1, false, "movie-mover" },
	{ "hand intent: reload",                1, 0, true,  "mover/converge" },
	// a stroke writes the movie AND steps the game through its rows (machine "any": a stroke whose
	// rows the playhead already passed - LP inside the v hold - writes without stepping)
	{ "hand intent: drag",                  2, 1, false, "movie-mover (+steps)" },
	{ "hand intent: brush",                 2, 1, false, "movie-mover (+steps)" },
	{ "hand intent: tap",                   2, 1, false, "movie-mover (+steps)" },
	{ "hand intent: load the combo",        1, 0, false, "mover" },
	{ "hand intent: replay the whole",      1, 2, false, "mover" },
	{ "hand intent: close",                 0, 0, false, "ui" },
	{ "hand intent: end",                   2, 1, true,  "identity" },
};

const Module MOD = { "hand", addSteps,
	ARMS, (int)(sizeof(ARMS) / sizeof(ARMS[0])),
	EXPECTS, (int)(sizeof(EXPECTS) / sizeof(EXPECTS[0])) };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
