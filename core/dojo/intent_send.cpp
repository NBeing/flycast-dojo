/*
	INTENT MODULE: inputs into the game (Surface Tour v4, 2026-09-18).

	Modules 7 sender + notepad, 8 frame skip test, 11 state machine. Registered from this
	TU through surfacetour::registerModule; the runner's tables are not edited here. The
	ceremony is intent.h's (the combo hunt's); every step is the four beats the user asked
	for: OPEN the window by its hotkey, ACT inside the feature, INTENT - the game runs and
	the fighter does the thing, read back through the game oracle - CLOSE.

	  notepad   "text becomes inputs": the combo's first 60 rows rendered in the roll's
	            one-token-per-frame notation into the REAL editor, analyzed by the panel,
	            parsed back: parse(render(rows)) == rows (the round-trip law), then the
	            va2 grammar's own ~90 spec vectors (tas_va2::SelfTest, headless).
	  sender    the FULL Dhalsim97 window through the sender's own path (renderCell ->
	            patternToCanon -> tas_auto::playLive at BASE+1) and the game run: the
	            SENDER, not the hunt, lands peak 19 (the RECIPE's pin).
	  FST       "which of the four phases connects": the FST's own sweep (k = 0..3 blank
	            rows at P = t0) over the placed window; four peaks read back. `[MEASURED]`
	            on this base every phase lands 19 - phase-insensitive; David's "1 in 4"
	            (Magneto s.HP x33 -> s.LP) is not reproducible on Sonson vs Marrow and is
	            not claimed.
	  states    Being_Hit = Knockdown_State==32 && Hitstop2>0 (states_general.json, the
	            evaluator ported into tas_mvc2): sampled on every maple poll during the
	            combo run - P2 enters it, P1 never does before the first hit.

	Arms (surfacetour::sabotaged, read only in the step lambdas below - never a switch in
	a feature): `send-noop` skips the send; `fst-phase` sweeps the wrong rows (base+200);
	`state-field` evaluates Being_Hit with Knockdown_State==31.
*/
#include "surface_tour.h"
#include "intent.h"
#include "mvc2.h"
#include "fst.h"
#include "tasva2.h"
#include "tasmacro.h"
#include "tas_auto.h"
#include "roll_notation.h"
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
namespace notepad { bool setAndAnalyze(const std::string& text, int& totalFrames, int& errorLines, std::string& back); }

namespace surfacetour {

namespace {

// the tour's chords for the three windows (surface_tour.cpp KEYS - the tour rebound them)
const u32 KEY_SENDER  = InputMapping::KEY_MOD_CTRL | 63;	// Ctrl+F6
const u32 KEY_FST     = InputMapping::KEY_MOD_CTRL | 66;	// Ctrl+F9
const u32 KEY_NOTEPAD = InputMapping::KEY_MOD_CTRL | 69;	// Ctrl+F12

bool panelOpen(const char *id)
{
	const panels::Panel *p = panels::find(id);
	return p != nullptr && p->open != nullptr && *p->open;
}

// ---- the combo window, re-read here (intent.cpp keeps its own copy; the rows are the fixture) ----
struct Combo
{
	bool tried = false, ok = false;
	std::vector<u16> p1, p2;
	std::string name, why;
} g_combo;

u32 macroMarker(const std::string& text, const char *marker)
{
	const size_t at = text.find(marker);
	if (at == std::string::npos) return 0;
	const size_t fr = text.find("frame ", at);
	if (fr == std::string::npos) return 0;
	return (u32)strtoul(text.c_str() + fr + 6, nullptr, 10);
}

bool comboRows()
{
	if (g_combo.tried) return g_combo.ok;
	g_combo.tried = true;
	const std::string path = cfgLoadStr("dojo", "IntentMacro", "");
	if (path.empty()) { g_combo.why = "dojo:IntentMacro not set"; return false; }
	std::ifstream in(path, std::ios::binary);
	if (!in.good()) { g_combo.why = "cannot read " + path; return false; }
	std::stringstream ss; ss << in.rdbuf();
	const std::string text = ss.str();
	tas_macro::Macro m;
	if (!tas_macro::FromText(text, m) || m.frames.empty()) { g_combo.why = "no frames in " + path; return false; }
	u32 a = macroMarker(text, "CLIP LIKELY BEGINS HERE"), b = macroMarker(text, "CLIP LIKELY ENDS HERE");
	const std::string win = cfgLoadStr("dojo", "IntentWindow", "");
	if (!win.empty())
	{
		a = (u32)strtoul(win.c_str(), nullptr, 10);
		const size_t dash = win.find('-');
		b = dash == std::string::npos ? 0 : (u32)strtoul(win.c_str() + dash + 1, nullptr, 10);
	}
	if (b == 0 || b <= a) { a = 0; b = (u32)m.frames.size(); }
	if (b > (u32)m.frames.size()) b = (u32)m.frames.size();
	if (a >= b) { g_combo.why = "empty window"; return false; }
	for (u32 f = a; f < b; f++) { g_combo.p1.push_back(m.frames[f].p1); g_combo.p2.push_back(m.frames[f].p2); }
	g_combo.name = "[" + std::to_string(a) + "-" + std::to_string(b) + "]";
	g_combo.ok = true;
	return true;
}

// rows -> the roll's own notation, one token per frame (roll_notation.h renderCell)
std::string renderRows(const std::vector<u16>& rows, size_t n, const char *sep)
{
	std::string out;
	for (size_t i = 0; i < rows.size() && i < n; i++)
	{
		if (i) out += sep;
		out += renderCell((Cell)rows[i]);
	}
	return out;
}


int g_stateIdx = -1;
u32 g_hashBeforeRun = 0;

// ---- the steps ----------------------------------------------------------------------------
void addSteps(std::vector<Step>& out)
{
	auto add = [&](Step s) { out.push_back(std::move(s)); };
	auto toggle = [](const char *name, u32 code, bool wantOpen, const char *chord) {
		Step s;
		s.name = name;
		s.kind = Kind::Click;
		s.act = [code] { why(""); return injectKey(code); };
		s.verify = [wantOpen, chord] {
			const char *id = strstr(chord, "F6") ? "sender" : strstr(chord, "F9") ? "frameskiptest" : "notepad";
			const bool open = panelOpen(id);
			if (open != wantOpen) { why("%s is %s", id, open ? "open" : "closed"); return false; }
			why("%s with %s", wantOpen ? "opened" : "closed", chord);
			return true;
		};
		return s;
	};

	// ---- 7a notepad --------------------------------------------------------------------------
	add(toggle("send intent: open notepad (Ctrl+F12)", KEY_NOTEPAD, true, "Ctrl+F12"));
	{
		Step s;
		s.name = "send intent: notepad round-trips the combo's first 60 rows";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			if (!comboRows()) { why("%s", g_combo.why.c_str()); return false; }
			const std::string text = renderRows(g_combo.p1, 60, "\n");
			int frames = 0, errors = 0;
			std::string back;
			if (!notepad::setAndAnalyze(text, frames, errors, back)) { why("the editor refused the text"); return false; }
			std::vector<Cell> cells;
			std::string err;
			// the round-trip law: what the editor holds parses back to the rows it was rendered from
			std::string joined;
			for (char c : back) joined += (c == '\n') ? ' ' : c;
			if (!parsePattern(joined, cells, err)) { why("parse failed: %s", err.c_str()); return false; }
			size_t same = 0;
			for (size_t i = 0; i < cells.size() && i < 60; i++) if ((u16)cells[i] == g_combo.p1[i]) same++;
			if (frames != 60 || errors != 0 || cells.size() != 60 || same != 60)
			{
				why("frames=%d errors=%d parsed=%u equal=%u (want 60/0/60/60)", frames, errors, (u32)cells.size(), (u32)same);
				return false;
			}
			why("60 rows rendered, analyzed (60 frames, 0 errors), parsed back equal - the round-trip law holds");
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "send intent: va2 grammar vectors";
		s.kind = Kind::Click;
		s.act = [] {
			why("");
			const std::string r = tas_va2::SelfTest();
			if (!r.empty()) { why("va2 SelfTest: %s", r.substr(0, 160).c_str()); return false; }
			why("tas_va2::SelfTest() passed every spec vector");
			return true;
		};
		add(s);
	}
	add(toggle("send intent: close notepad", KEY_NOTEPAD, false, "Ctrl+F12"));

	// ---- 7b sender ---------------------------------------------------------------------------
	{
		Step s;
		s.name = "send intent: begin on BASE";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] { why(""); if (!intent::begin("send intent")) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] { if (!intent::settled()) { why("not settled"); return false; } why("BASE @%u hash=%08X", intent::baseFrame(), intent::baseHash()); return true; };
		add(s);
	}
	add(toggle("send intent: open sender (Ctrl+F6)", KEY_SENDER, true, "Ctrl+F6"));
	{
		Step s;
		s.name = "send intent: sender sends the combo window";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			std::string w;
			if (!comboRows()) { why("%s", g_combo.why.c_str()); return false; }
			if (!intent::comboLoaded(w)) { why("%s", w.c_str()); return false; }
			if (tas_auto::liveActive()) { why("already sending"); return false; }
			// the sender's own path: text -> patternToCanon -> playLive (sender_panel.cpp drawSend)
			const std::string text = renderRows(g_combo.p1, g_combo.p1.size(), " ");
			std::vector<u16> p1;
			std::string err;
			if (!sender::patternToCanon(text, p1, err)) { why("pattern refused: %s", err.c_str()); return false; }
			if (p1 != g_combo.p1) { why("the sender parsed %u rows, %u differ from the source", (u32)p1.size(), (u32)(p1.size() != g_combo.p1.size() ? 1 : 0)); return false; }
			size_t p2rows = 0;
			for (u16 v : g_combo.p2) if (v) p2rows++;
			if (sabotaged("send-noop"))
			{	// SABOTAGE: the arm - the pattern parses but nothing is sent
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE send-noop - the sender's playLive is skipped");
				why("sent nothing (send-noop)");
				return true;
			}
			const std::vector<u16> p2;
			tas_auto::playLive(p1, p2, (u64)intent::baseFrame() + 1);
			NOTICE_LOG(RENDERER, "INPUT SENDER: sent %u frame(s) at %u (tour intent: %s, P2 rows with input in the source: %u - the sender is P1-only)",
					(unsigned)p1.size(), intent::baseFrame() + 1, g_combo.name.c_str(), (u32)p2rows);
			if (!tas_auto::liveActive()) { why("playLive did not go live"); return false; }
			why("%u tokens through patternToCanon, live at %u (P2-input rows in the source: %u, not sent)", (u32)p1.size(), intent::baseFrame() + 1, (u32)p2rows);
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "send intent: the sent combo lands (the fixture's peak)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 60000;
		s.act = [] { why(""); g_hashBeforeRun = intent::machineHash(); if (!intent::runToStop()) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] {
			if (!intent::settled()) { why("running"); return false; }
			const u16 pk = intent::peak(0);
			if (pk != intent::pinnedPeak()) { why("peak %u (want %u - RECIPE.toml result.combo_peak); after=%08X", (unsigned)pk, (unsigned)intent::pinnedPeak(), intent::machineHash()); return false; }
			why("the SENDER landed it: peak %u, after=%08X (frame %u)", (unsigned)pk, intent::machineHash(), dojo.frame_number.load());
			return true;
		};
		add(s);
	}
	add(toggle("send intent: close sender", KEY_SENDER, false, "Ctrl+F6"));
	{
		// `[MEASURED 2026-09-18]` the live send BAKES the rows it drove (READ-WRITE: every
		// source writes the roll), so a plain reload left the combo in the roll and the FST
		// place below wrote the same rows again - "the movie did not move", VACUOUS. Restore
		// the snapshot (end) and start a fresh ceremony (begin) so the place has something to do.
		Step s;
		s.name = "send intent: restore the roll, back on BASE";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] { why(""); if (!intent::end()) { why("%s", intent::lastWhy()); return false; } if (!intent::begin("send intent (fst)")) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] { if (!intent::settled()) { why("loading"); return false; } if (dojo.frame_number.load() != intent::baseFrame()) { why("frame %u != base %u", dojo.frame_number.load(), intent::baseFrame()); return false; } why("roll restored, BASE @%u hash=%08X", intent::baseFrame(), intent::machineHash()); return true; };
		add(s);
	}

	// ---- 8 frame skip test -------------------------------------------------------------------
	add(toggle("send intent: open frame skip test (Ctrl+F9)", KEY_FST, true, "Ctrl+F9"));
	{
		Step s;
		s.name = "send intent: place the combo for the sweep";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			if (!intent::placeCombo(0, 0)) { why("%s", intent::lastWhy()); return false; }
			why("%s at t0=%u (%u rows) through the edit funnel", intent::comboName(), intent::t0(), intent::comboLen());
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "send intent: FST sweeps the four phases";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 300000;		// four variants: ~45 s each at speed, ~10 s fast-forwarded (measured)
		s.act = [] {
			why("");
			u32 lo = intent::t0(), hi = intent::t0() + intent::comboLen() - 1;
			int k0 = 0, k1 = 3;
			if (sabotaged("fst-phase"))
			{	// THE RESTORED DEFECT: the sweep's phase pin lands INSIDE the button burst with too many
				// blanks. `[MEASURED 2026-09-18]` "rows 200 past the base where nothing is placed" was
				// decorative on dhalsim_3hit (base+200 is inside its 204-row walk-in, which absorbs
				// blanks); on David's file it only worked because base+200 was P2-idle. The burst is the
				// window's last ~70 rows; 4..7 inserted blanks there stretch an 8-frame gap past the
				// measured 7..9 tolerance and the chain drops (gap 10 -> 2 hits, the author's table).
				lo = hi - 60; k0 = 4; k1 = 7;
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE fst-phase - the phase pin at %u..%u with k=%d..%d (inside the burst) instead of the window with k=0..3", lo, hi, k0, k1);
			}
			std::string w;
			if (!fst::armSweepOver("TOUR INTENT FST", lo, hi, k0, k1, 60, w)) { why("arm refused: %s", w.c_str()); return false; }
			return true;
		};
		s.verify = [] {
			if (frameSkipTestRunning()) { why("sweeping"); return false; }
			if (!intent::settled()) { why("not settled"); return false; }
			u16 pk[4] = { 0, 0, 0, 0 }, p2;
			int ran = 0, hit = 0;
			for (int n = 0; n < 4; n++) if (fst::resultOf(n, pk[n], p2)) { ran++; if (pk[n] == intent::pinnedPeak()) hit++; }
			if (ran != 4 || hit != 4)
			{
				why("phases 0..3 peaked %u/%u/%u/%u (want %u on all four - the RECIPE's measured phase-insensitivity; %d ran)", pk[0], pk[1], pk[2], pk[3], (unsigned)intent::pinnedPeak(), ran);
				return false;
			}
			why("phases 0..3 peaked %u/%u/%u/%u - phase-insensitive on this base (David's 1-in-4 is not this combo)", pk[0], pk[1], pk[2], pk[3]);
			return true;
		};
		add(s);
	}
	add(toggle("send intent: close frame skip test", KEY_FST, false, "Ctrl+F9"));

	// ---- 11 state machine --------------------------------------------------------------------
	{
		Step s;
		s.name = "send intent: on BASE for the sample";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] { why(""); if (!intent::reloadBase()) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] { if (!intent::settled()) { why("loading"); return false; } if (dojo.frame_number.load() != intent::baseFrame() || intent::machineHash() != intent::baseHash()) { why("frame %u hash %08X != BASE %u/%08X", dojo.frame_number.load(), intent::machineHash(), intent::baseFrame(), intent::baseHash()); return false; } why("BASE @%u hash=%08X (identity: the FST's restore already reloaded it)", intent::baseFrame(), intent::machineHash()); return true; };
		add(s);
	}
	{
		Step s;
		s.name = "send intent: Being_Hit sampled during the combo";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 60000;
		s.act = [] {
			why("");
			g_stateIdx = tas_mvc2::stateIndex("Being_Hit");
			if (g_stateIdx < 0) { why("Being_Hit did not load (%u states loaded - a field is missing from the dictionary)", (u32)tas_mvc2::stateList().size()); return false; }
			int override_ = -1;
			if (sabotaged("state-field"))
			{	// SABOTAGE: the arm - Knockdown_State==31 instead of 32
				override_ = 31;
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE state-field - Being_Hit evaluated with its first threshold = 31");
			}
			tas_mvc2::statesSampleArm(g_stateIdx, override_);
			if (!intent::runToStop()) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) { why("running"); return false; }
			const tas_mvc2::StateSample ss = tas_mvc2::statesSampleTake();
			const u16 pk = intent::peak(0);
			if (ss.activeP2 == 0 || ss.activeP1BeforeFirstHit != 0 || ss.firstHitFrame == 0)
			{
				why("Being_Hit P2 active %d polls (first @%u), P1 before the first hit %d, first hit @%u, peak %u (want P2>0, P1 0, a hit) over %d polls; P2 point slot %d, clause-by-clause for P2: Knockdown_State==32 held %d, Hitstop2>0 held %d",
						ss.activeP2, ss.firstActiveP2Frame, ss.activeP1BeforeFirstHit, ss.firstHitFrame, (unsigned)pk, ss.polls, ss.pointP2, ss.condP2[0], ss.condP2[1]);
				return false;
			}
			why("P2 (point slot %d) entered Being_Hit on %d of %d polls (first @%u, first hit @%u), P1 never before the hit; peak %u", ss.pointP2, ss.activeP2, ss.polls, ss.firstActiveP2Frame, ss.firstHitFrame, (unsigned)pk);
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "send intent: end on BASE";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] { why(""); if (!intent::end()) { why("%s", intent::lastWhy()); return false; } return true; };
		s.verify = [] {
			if (!intent::settled()) { why("loading"); return false; }
			if (dojo.frame_number.load() != intent::baseFrame()) { why("frame %u != base %u", dojo.frame_number.load(), intent::baseFrame()); return false; }
			if (intent::machineHash() != intent::baseHash()) { why("hash %08X != BASE %08X", intent::machineHash(), intent::baseHash()); return false; }
			why("roll restored, BASE @%u hash=%08X", intent::baseFrame(), intent::machineHash());
			return true;
		};
		add(s);
	}
}

const ArmSpec ARMS[] = {
	{ "send-noop",   "send intent: the sent combo lands (the fixture's peak)",        "send intent: notepad round-trips the combo's first 60 rows" },
	{ "fst-phase",   "send intent: FST sweeps the four phases",            "send intent: the sent combo lands (the fixture's peak)" },
	{ "state-field", "send intent: Being_Hit sampled during the combo",    "send intent: FST sweeps the four phases" },
};

// machine: 0 unchanged, 1 moved, 2 any; movie likewise
const ExpectDecl EXPECTS[] = {
	{ "send intent: open",                        0, 0, false, "ui" },
	{ "send intent: close",                       0, 0, false, "ui" },
	{ "send intent: notepad",                     0, 0, false, "ui" },
	{ "send intent: va2",                         0, 0, false, "ui" },
	{ "send intent: begin on BASE",               2, 2, false, "setup" },
	{ "send intent: sender sends",                0, 0, false, "ui" },
	// the live send DRIVES the guest and bakes what it drove; the claim is the peak and the
	// machine, the movie is the sender's business (measured: the machine lands on the
	// RECIPE's after=64FF89AB either way)
	// `[MEASURED 2026-09-18]` on the Dhalsim base with our own combo (no P2 rows) the SENDER's
	// path and the placed combo (the Being_Hit run) end on the SAME machine (F47533EF): the
	// sender is P1-only, and with nothing for P2 the two routes are byte-identical. On the
	// harness base they differed only because David's window carried 20 P2 rows the sender
	// skipped. So the two runs CONVERGE - a stronger claim than "both landed": the sender's
	// route into the roll and the funnel's produce the identical machine.
	{ "send intent: the sent combo lands",        1, 2, true,  "mover/converge" },
	{ "send intent: restore the roll",            1, 2, true,  "mover/converge" },
	// identity, like `savestate: load slot 99`: the FST's own restore reloaded BASE already
	{ "send intent: on BASE for the sample",      2, 0, true,  "identity" },
	{ "send intent: place the combo",             0, 1, false, "movie-mover" },
	{ "send intent: FST sweeps",                  1, 0, false, "mover" },
	{ "send intent: Being_Hit",                   1, 0, true,  "mover/converge" },
	{ "send intent: end on BASE",                 1, 1, true,  "mover/converge" },
};

const Module MOD = { "send", addSteps, ARMS, (int)(sizeof(ARMS) / sizeof(ARMS[0])), EXPECTS, (int)(sizeof(EXPECTS) / sizeof(EXPECTS[0])) };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
