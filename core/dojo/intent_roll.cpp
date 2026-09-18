/*
	INTENT MODULE: the roll and its history (Surface Tour v4, 2026-09-18).

	Owns the steps for: piano roll edit (module 1), undo/redo (2), macros/snippets
	place (5), the ruler/skip map (10). Registers them from this TU through
	surfacetour::registerModule - the runner's tables are not edited here. See
	docs/tour-intent-roll.md, docs/TEST-PLAN.md §6 and intent.h for the ceremony.

	THE RULE (the user, 2026-09-17): "nothing opened starting on your roll tests...
	each feature should be tested with its INTENT." Every step here is four beats:
	OPEN the window by its tour hotkey (the human sees it), ACT through the panel's
	OWN edit path (rollpanel::blankRange/undo/redo, macros::placeFileWindowAt - the
	buttons' bodies, never a hook that bypasses the panel), INTENT - the game is run
	and the fighter does the thing, read as Combo_Meter_HitsToOpponent's peak - and
	CLOSE.

	THE FIXTURE: BASE = the tour clip's slot 0 (in-match), THE COMBO = David's
	Combo_Dhalsim97 window (dojo:IntentMacro, staged by surfacetourtest.sh), which
	lands peak 19 on this base (scripts/fixtures/mvc2/RECIPE.toml result.combo_peak;
	measured on all four phases, 2026-09-17). intent::pinnedPeak() below IS that pin.

	David's intent, quoted (docs/PORT-DEFECT-CENSUS.md context / his tree):
	  roll edit  "swap-twice and flip-twice are IDENTITIES, doing the op again restores
	             the exact bytes" - here: place -> 19, Blank -> 0, Undo -> 19.
	  undo/redo  "an undo/redo is itself an edit - it fires a guard event like any other
	             change" - Redo -> 0, Undo -> 19, through the panel's Undo/Redo.
	  macros     "State 0 = Macro Frame 0 (REQUIRED binding)" - the window's row 0 is
	             placed AT BASE+1 through the Macros panel's place path -> 19.
	  ruler      "MvC2 skips game-logic frames on a fixed cadence... rows `x` (skip
	             frame)" - 240 frames of the combo hold ~60 skip samples (rate 4).
	  snippets   a movie-mover only: a snippet has no in-match intent; said so.

	ARMS (judged by scripts/lib/arms.sh like every runner arm):
	  roll-clear    the clear edits session_inputs DIRECTLY (bypasses the funnel, so the
	                undo history is empty): "roll intent: undo the clear (panel Undo)"
	                must redden - the cell stays blank - and the place step stays green.
	  macro-window  the panel places the WRONG window of the file - its first 1487 rows
	                instead of the CLIP markers' 4212..5699 (RECIPE: "trust the markers,
	                not the burst heuristic"): "macro intent: the macro lands the hit
	                (the fixture's peak)" must redden; the roll module's own place step stays green.
	                `[MEASURED 2026-09-18]` a `macro-anchor` arm (the window 30 rows late)
	                was DECORATIVE: peak 19 regardless - this combo tolerates the delay on
	                this base, as the hunt's phase sweep already showed. Dropped, recorded.
*/
#include "surface_tour.h"
#include "intent.h"
#include "roll_edit.h"
#include "tas_ruler.h"
#include "dojo.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "input/mapping.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace roll {

// The panels' own edit paths, given a name for this module (defined with external linkage
// in roll_panel.cpp / macros_panel.cpp; declared here so no other TU is touched).
namespace rollpanel { s64 blankRange(u32 lo, u32 hi); bool undo(); bool redo(); }
namespace macros    { s64 placeFileWindowAt(const std::string& path, u32 lo, u32 hi, u32 at, std::string& why); }

namespace surfacetour {

namespace {

// The tour's own chords (surface_tour.cpp KEYS): the panels were rebound to these in
// the rebind phase, which runs before every module. Raw SDL scancodes F1=58..F12=69.
const u32 CTRL = InputMapping::KEY_MOD_CTRL, ALT = InputMapping::KEY_MOD_ALT;
const u32 KEY_PIANOROLL = CTRL | 58;	// Ctrl+F1
const u32 KEY_SNIPPETS  = ALT  | 62;	// Alt+F5
const u32 KEY_MACROS    = ALT  | 63;	// Alt+F6


struct ModState
{
	u32 t0 = 0, len = 0;		// the placed window: first row, rows
	u32 probeRow = 0;			// the first NON-neutral row of the window (the row the clear/undo is read at)
	u32 macroLo = 0, macroHi = 0, macroAt = 0;
	int skips = -1;
} ms;

bool cellNeutral(u32 f)
{
	auto it = dojo.session_inputs.find(f);
	return it == dojo.session_inputs.end() || it->second == blankRow();
}

bool panelIs(const char *id, bool open)
{
	const panels::Panel *p = panels::find(id);
	if (p == nullptr) { why("no such panel: %s", id); return false; }
	if (*p->open != open) { why("open=%s captured=%s", *p->open ? "true" : "false", gui_keyboard_captured() ? "yes" : "no"); return false; }
	why("%s", open ? "open" : "closed");
	return true;
}

// The window of the macro file: its own CLIP markers, or dojo:IntentWindow=a-b. The
// same parse intent.cpp does for the roll placement (kept here so the macros step reads
// the FILE the panel is handed, not the helper's cache).
u32 marker(const std::string& text, const char *m)
{
	const size_t at = text.find(m);
	if (at == std::string::npos) return 0;
	const size_t fr = text.find("frame ", at);
	return fr == std::string::npos ? 0 : (u32)strtoul(text.c_str() + fr + 6, nullptr, 10);
}

bool macroWindow(const std::string& path, u32& lo, u32& hi)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.good()) return false;
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	lo = marker(text, "CLIP LIKELY BEGINS HERE");
	hi = marker(text, "CLIP LIKELY ENDS HERE");
	const std::string win = cfgLoadStr("dojo", "IntentWindow", "");
	if (!win.empty())
	{
		lo = (u32)strtoul(win.c_str(), nullptr, 10);
		const size_t dash = win.find('-');
		hi = dash == std::string::npos ? 0 : (u32)strtoul(win.c_str() + dash + 1, nullptr, 10);
	}
	return true;
}

// ---- step shapes -------------------------------------------------------------------
Step click(const char *name, std::function<bool()> act, std::function<bool()> verify, bool needsPrev = false)
{
	Step s;
	s.name = name;
	s.kind = Kind::Click;
	s.needsPrev = needsPrev;
	s.act = [act] { why(""); return act(); };
	s.verify = std::move(verify);
	return s;
}

Step openStep(const char *name, const char *panel, u32 code)
{
	return click(name, [code] { return injectKey(code); }, [panel] { return panelIs(panel, true); });
}

Step closeStep(const char *name, const char *panel, u32 code)
{
	return click(name, [code] { return injectKey(code); }, [panel] { return panelIs(panel, false); });
}

//! A run over the combo: runToStop under fast-forward, the peak read while stopped.
Step runStep(const char *name, u16 want, bool needsPrev = true)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.needsPrev = needsPrev;
	s.maxWaitMs = 90000;		// ~8 s measured for the 1487-row window + 60 run-out on an idle box; a shared box is slower
	s.act = [] { why(""); if (!intent::runToStop()) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [want] {
		if (!intent::settled()) return false;
		const u16 p = intent::peak(0);
		why("peak=%u (want %u) at frame %u, %s", (unsigned)p, (unsigned)want, dojo.frame_number.load(), p == want ? "the fighter did the thing" : "the fighter did NOT");
		return p == want;
	};
	return s;
}

//! Back to BASE: every reload lands on the same machine `load slot 0` landed on (converge).
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

Step beginStep(const char *name, const char *who)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 10000;
	s.act = [who] { why(""); if (!intent::begin(who)) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [] {
		if (!intent::settled()) return false;
		why("BASE @%u hash=%08X", intent::baseFrame(), intent::baseHash());
		return true;
	};
	return s;
}

Step endStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 10000;
	s.act = [] { why(""); if (!intent::end()) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [] {
		if (!intent::settled()) return false;
		const u32 h = intent::machineHash();
		why("roll restored, BASE @%u hash=%08X", dojo.frame_number.load(), h);
		return h == intent::baseHash();
	};
	return s;
}

void addSteps(std::vector<Step>& out)
{
	// ---- 5b. snippets: a movie-mover, no in-match intent (a snippet drives menus) ------
	out.push_back(openStep("snippet intent: open the snippets window", "snippets", KEY_SNIPPETS));
	out.push_back(click("snippet intent: place a snippet (the roll moves; no in-match intent)",
		[] { return hooks::snippetsPlace(); },
		[] { why("placed through the panel's place path; a snippet drives menus, not a match - movie-mover only"); return true; },
		true));
	// and UNDO it through the roll's own Undo, so the roll is pristine again: the base tour's
	// own `snippets: place` runs later and must find its rows unplaced (measured 2026-09-18:
	// "snippet: replace changed nothing" when this step left the placement behind).
	out.push_back(click("snippet intent: undo the placement (panel Undo)",
		[] { return rollpanel::undo(); },
		[] { why("undone through the roll, redo depth %zu", dojo.redo_stack.size()); return true; },
		true));
	out.push_back(closeStep("snippet intent: close the snippets window", "snippets", KEY_SNIPPETS));

	// ---- 1 + 2. the roll: place -> 19, Blank -> 0, Undo -> 19, Redo -> 0, Undo -> 19 ---
	out.push_back(openStep("roll intent: open the piano roll", "pianoroll", KEY_PIANOROLL));
	out.push_back(beginStep("roll intent: begin on BASE", "roll intent"));
	out.push_back(click("roll intent: place the combo at BASE+1",
		[] { if (!intent::placeCombo(0, 0)) { why("%s", intent::lastWhy()); return false; } return true; },
		[] {
			ms.t0 = intent::t0();
			ms.len = intent::comboLen();
			ms.probeRow = 0;
			for (u32 f = ms.t0; f < ms.t0 + ms.len; f++)
				if (!cellNeutral(f)) { ms.probeRow = f; break; }
			if (ms.probeRow == 0) { why("no non-neutral row in the window %u..%u", ms.t0, ms.t0 + ms.len); return false; }
			why("%s: %u rows at %u.. (first input at row %u)", intent::comboName(), ms.len, ms.t0, ms.probeRow);
			return true;
		}, true));
	out.push_back(runStep("roll intent: the combo lands (the fixture's peak)", intent::pinnedPeak()));
	out.push_back(reloadStep("roll intent: reload BASE (after the hit)"));
	out.push_back(click("roll intent: clear the combo through the roll (Blank)",
		[] {
			const u32 lo = ms.t0, hi = ms.t0 + ms.len - 1;
			if (sabotaged("roll-clear"))
			{	// THE RESTORED DEFECT: rows written directly, no funnel, no undo history.
				for (u32 f = lo; f <= hi; f++) dojo.session_inputs[f] = blankRow();
				dojo.stale_tail_from = ~0u;
				return true;
			}
			return rollpanel::blankRange(lo, hi) >= 0;
		},
		[] {
			if (!cellNeutral(ms.probeRow)) { why("row %u still has input", ms.probeRow); return false; }
			why("rows %u..%u blank, undo depth %zu", ms.t0, ms.t0 + ms.len - 1, dojo.undo_stack.size());
			return true;
		}, true));
	out.push_back(runStep("roll intent: the hit is gone (peak 0)", 0));
	out.push_back(reloadStep("roll intent: reload BASE (after the clear)"));
	out.push_back(click("roll intent: undo the clear (panel Undo)",
		[] { return rollpanel::undo(); },
		[] {
			if (cellNeutral(ms.probeRow)) { why("row %u is still blank - the undo restored nothing (peak would stay 0)", ms.probeRow); return false; }
			why("row %u has its input back, redo depth %zu", ms.probeRow, dojo.redo_stack.size());
			return true;
		}, true));
	out.push_back(runStep("roll intent: undo restores the hit (the fixture's peak)", intent::pinnedPeak()));
	out.push_back(reloadStep("roll intent: reload BASE (after the undo)"));
	out.push_back(click("roll intent: redo the clear (panel Redo)",
		[] { return rollpanel::redo(); },
		[] {
			if (!cellNeutral(ms.probeRow)) { why("row %u still has input - the redo did not re-clear", ms.probeRow); return false; }
			why("row %u blank again", ms.probeRow);
			return true;
		}, true));
	out.push_back(runStep("roll intent: redo removes the hit (peak 0)", 0));
	out.push_back(reloadStep("roll intent: reload BASE (after the redo)"));
	out.push_back(click("roll intent: undo again (panel Undo)",
		[] { return rollpanel::undo(); },
		[] {
			if (cellNeutral(ms.probeRow)) { why("row %u is still blank", ms.probeRow); return false; }
			why("row %u has its input back", ms.probeRow);
			return true;
		}, true));
	out.push_back(runStep("roll intent: the hit is back (the fixture's peak)", intent::pinnedPeak()));
	out.push_back(reloadStep("roll intent: reload BASE (after the second undo)"));
	out.push_back(closeStep("roll intent: close the piano roll", "pianoroll", KEY_PIANOROLL));
	out.push_back(endStep("roll intent: end on BASE (roll restored)"));

	// ---- 5. macros: the window's row 0 AT BASE+1, through the Macros panel -> 19 -------
	out.push_back(openStep("macro intent: open the macros window", "macros", KEY_MACROS));
	out.push_back(beginStep("macro intent: begin on BASE", "macro intent"));
	out.push_back(click("macro intent: place the window at BASE+1 (State 0 = macro frame 0)",
		[] {
			const std::string path = cfgLoadStr("dojo", "IntentMacro", "");
			if (path.empty()) { why("dojo:IntentMacro not set"); return false; }
			if (!macroWindow(path, ms.macroLo, ms.macroHi)) { why("cannot read %s", path.c_str()); return false; }
			// THE RESTORED DEFECT (macro-window): the file's first rows instead of its CLIP
			// markers - the wrong combo placed, so the fighter must NOT land 19.
			// THE RESTORED DEFECT (macro-window): a WRONG window of the file. `[MEASURED 2026-09-18]`
			// "the first N rows instead of the markers" was decorative on a file with no markers (the
			// whole file IS the window, n=0 placed the whole file); the walk-in without its buttons
			// (rows 0..204 of dhalsim_3hit; on David's file, 204 rows of menu Start-mashing) is wrong
			// on every fixture and cannot connect.
			if (sabotaged("macro-window")) { ms.macroLo = 0; ms.macroHi = 204; }
			ms.macroAt = intent::baseFrame() + 1;
			std::string w;
			const s64 first = macros::placeFileWindowAt(path, ms.macroLo, ms.macroHi, ms.macroAt, w);
			if (first < 0) { why("%s", w.c_str()); return false; }
			return true;
		},
		[] {
			// the window's first input landed where it was asked to (the module's own probe offset)
			const u32 at = ms.macroAt + (ms.probeRow - ms.t0);
			if (cellNeutral(at)) { why("row %u (window row 0 + %u) is neutral", at, ms.probeRow - ms.t0); return false; }
			why("window %u..%u placed at %u (first input at row %u)", ms.macroLo, ms.macroHi, ms.macroAt, at);
			return true;
		}, true));
	out.push_back(runStep("macro intent: the macro lands the hit (the fixture's peak)", intent::pinnedPeak()));
	// ---- 10. the ruler: the skip map over the run ------------------------------------
	out.push_back(click("ruler intent: x every 4th frame (skip map over the combo)",
		[] { return true; },
		[] {
			std::vector<tas_ruler::SkipSample> v;
			const u32 lo = ms.t0, hi = ms.t0 + 240;
			tas_ruler::snapshot(lo, hi, v);
			int skips = 0, seen = 0;
			for (const tas_ruler::SkipSample& s : v) { if (s.seen) seen++; if (s.seen && s.skip) skips++; }
			ms.skips = skips;
			why("%d skip frames in %d seen of %u..%u (rate 4 -> 60 expected)", skips, seen, lo, hi);
			return seen >= 200 && skips >= 55 && skips <= 65;
		}, true));
	out.push_back(reloadStep("macro intent: reload BASE (after the macro)"));
	out.push_back(closeStep("macro intent: close the macros window", "macros", KEY_MACROS));
	out.push_back(endStep("macro intent: end on BASE (roll restored)"));
}

const ArmSpec ARMS[] = {
	{ "roll-clear",   "roll intent: undo the clear (panel Undo)",        "roll intent: the combo lands (the fixture's peak)" },
	{ "macro-window", "macro intent: the macro lands the hit (the fixture's peak)", "roll intent: the combo lands (the fixture's peak)" },
};

// What each step does to the machine and the movie (machine/movie: 0 unchanged, 1 moved, 2 any).
// A run may materialize cells past the frontier (a writable session grows), so the movie is
// not asserted on runs; every reload/end lands on BASE = the hash `load slot 0` landed on.
const ExpectDecl EXPECTS[] = {
	// the RUNS first: their names share prefixes with the click steps below ("undo", "redo")
	// CONVERGE, by design: the same window from the same BASE lands the same machine
	// (64FF89AB with the hit, A4E4A31C without) - that IDENTITY is the undo/redo claim,
	// so each run must pair with another run on its hash. `[MEASURED 2026-09-18]` as
	// plain movers, G5 read every pair as "one moved nothing" on a green run.
	{ "roll intent: the combo lands",       1, 2, true,  "mover/converge" },
	{ "roll intent: the hit is gone",       1, 2, true,  "mover/converge" },
	{ "roll intent: undo restores the hit", 1, 2, true,  "mover/converge" },
	{ "roll intent: redo removes the hit",  1, 2, true,  "mover/converge" },
	{ "roll intent: the hit is back",       1, 2, true,  "mover/converge" },
	{ "macro intent: the macro lands",      1, 2, true,  "mover/converge" },
	{ "snippet intent: open",          0, 0, false, "ui" },
	{ "snippet intent: place",         0, 1, false, "movie-mover" },
	{ "snippet intent: undo",          0, 1, false, "movie-mover" },
	{ "snippet intent: close",         0, 0, false, "ui" },
	{ "roll intent: open",             0, 0, false, "ui" },
	{ "roll intent: begin",            2, 0, false, "any" },
	{ "roll intent: place",            0, 1, false, "movie-mover" },
	{ "roll intent: reload",           1, 0, true,  "mover/converge" },
	{ "roll intent: clear",            0, 1, false, "movie-mover" },
	{ "roll intent: undo",             0, 1, false, "movie-mover" },
	{ "roll intent: redo",             0, 1, false, "movie-mover" },
	{ "roll intent: close",            0, 0, false, "ui" },
	{ "roll intent: end",              2, 1, true,  "identity" },
	{ "macro intent: open",            0, 0, false, "ui" },
	{ "macro intent: begin",           2, 0, false, "any" },
	{ "macro intent: place",           0, 1, false, "movie-mover" },
	{ "ruler intent:",                 0, 0, false, "ui" },
	{ "macro intent: reload",          1, 0, true,  "mover/converge" },
	{ "macro intent: close",           0, 0, false, "ui" },
	{ "macro intent: end",             2, 1, true,  "identity" },
};

const Module MOD = { "roll", addSteps,
	ARMS, (int)(sizeof(ARMS) / sizeof(ARMS[0])),
	EXPECTS, (int)(sizeof(EXPECTS) / sizeof(EXPECTS[0])) };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
