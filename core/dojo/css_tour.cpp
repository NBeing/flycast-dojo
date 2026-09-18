/*
	THE CSS TOUR - David's character-select utility exercised on DC, and the Dhalsim
	base it builds (module `css`, Surface Tour v4 rails; docs/TEST-PLAN.md §7).

	`[2026-09-18]` the user, of every clip on this machine: "none of those are from
	me" - and David ships no Dreamcast savestates. His `mcp/charselect.py` (the globe as
	a torus, plan(), build_picks(), a timing table; verified by him on DC 2026-09-12)
	picked Dhalsim/Cable/Sentinel vs Ryu/Ken/Guile on THIS build from power-on, first
	try. So the base is AUTHORED from inputs: fastVS_mcp seeds the boot to the globe
	(dojo:OnEnterFile, handoff at 742), then every press below is the tour's own,
	through tas_auto::playLive (the sender's path), and every claim is a byte the game
	reports (tas_mvc2::readRamSafe at css::id2Addr / assistAddr / paletteAddr,
	SPREADSHEET names for Health_Big / Is_Point / the skip rate).

	A SEEDED boot: no clip, no slot 0 (surface_tour.cpp cssTour(): ready() waits for the
	handoff pause, buildSteps() builds only this module). The harness passes
	RecordMatches=yes (Replay::CreateReplayFile binds a clip folder at the first maple
	poll - where step 6 saves slot 0) and MacroMode=yes (TRAP #2: a non-macro handoff
	drops READ-WRITE to WRITE, and WRITE clobbers the seed's remaining rows and every
	live press with the neutral pad).

	Steps (each: the beat, then the read-back):
	  css: on the globe            Paused at the handoff; ID_2 P1_A==19 RubyHeart, P2_A==23 Cable
	  css: walk press 1..4         plan("Dhalsim") pressed edge-wise, ONE press per step; the
	                               ID_2 after each equals the graph's prediction (verify_grid)
	  css: pick Dhalsim (LP, alpha) LP; PaletteID_2==0 (LP), Assist_Value==0; confirm; slot B live
	  css: pick the rest ...       build_picks for the rest of P1 + all of P2; six ID_2 as pinned
	  css: Start at SPEED SELECT   in-match: Is_Point on P1_A, Health_Big 144/144, skip rate 4
	  css: save the Dhalsim base   slot 0 written (saveScratchSlot's shape); CSS BASE: line
	  css finding: David's rows    OPTIONAL - his 4212-5699 on this base through intent; the
	                               peak is reported either way (a finding, never a pass condition)
	  css: end                     Paused on the base, the save's hash
	Arms: css-walk (P1 planned from Cable's home -> wrong path -> press 1 reddens), css-timing
	(navgap 0 -> a repeated direction never re-presses -> Sentinel's D,D,D,D lands wrong),
	css-team (the P2 stream skipped -> P2 slots never pick).
*/
#include "surface_tour.h"
#include "css.h"
#include "movie.h"
#include "intent.h"
#include "mvc2.h"
#include "oracle.h"
#include "tas_auto.h"
#include "tasmacro.h"
#include "dojo.h"
#include "emulator.h"
#include "rend/gui.h"
#include "rend/panel.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "deps/filesystem.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#undef verify

namespace roll {
namespace surfacetour {

namespace {

using css::Slot;

const int kExpectHealth = 144;
const u32 kFightChunk = 120;		// frames per poll after Start
const u32 kFightMaxFrames = 1500;	// the intro must be over by then (measured: in-match by ~+1000)

struct Team { std::vector<css::Pick> p1, p2; };

struct State
{
	u32 runOutTarget = 0;
	Team team;
	std::vector<std::string> walkPath;		// the graph's predicted character after each P1 press
	std::vector<u16> pending;				// the walk's presses, one per step
	u32 runTo = 0;
	u32 baseFrame = 0, baseHash = 0;
	std::string baseFolder;
	int findingPeak = -1;
	int findingPhase = 0;
	u32 fightFrom = 0;
} st;

// ---- reads ---------------------------------------------------------------------------
bool rd8(u32 demul, u32& v)
{
	v = 0;
	return demul != 0 && tas_mvc2::readRamSafe(tas_mvc2::toFlycast(demul), 1, v);
}
int id2(Slot s) { u32 v; return rd8(css::id2Addr(s), v) ? (int)v : -1; }
int assist(Slot s) { u32 v; return rd8(css::assistAddr(s), v) ? (int)v : -1; }
int palette(Slot s) { u32 v; return rd8(css::paletteAddr(s), v) ? (int)v : -1; }
int health(int player)
{
	u32 v;
	const u32 a = tas_mvc2::addrOf("Health_Big", player, 0);
	return a != 0 && tas_mvc2::readRamSafe(a, 1, v) ? (int)v : -1;
}

std::string teamNames()
{
	std::string s;
	for (const css::Pick& p : st.team.p1) s += (s.empty() ? "" : "/") + p.name;
	s += " vs ";
	bool first = true;
	for (const css::Pick& p : st.team.p2) { s += (first ? "" : "/") + p.name; first = false; }
	return s;
}

bool loadTeam(std::string& why)
{
	if (!st.team.p1.empty()) return true;
	const char *p1[] = { "Dhalsim-LP-A", "Cable-LP-A", "Sentinel-LP-A" };
	const char *p2[] = { "Ryu-LP-A", "Ken-LP-A", "Guile-LP-A" };
	for (const char *x : p1) { css::Pick p; if (!css::parsePick(x, p, why)) { st.team.p1.clear(); return false; } st.team.p1.push_back(p); }
	for (const char *x : p2) { css::Pick p; if (!css::parsePick(x, p, why)) { st.team.p1.clear(); st.team.p2.clear(); return false; } st.team.p2.push_back(p); }
	return true;
}

//! Run n frames from now; the step's verify polls stopped().
void runFrames(u32 n)
{
	st.runTo = dojo.frame_number.load() + n;
	gui_step_frames((int)n);
}
bool stopped()
{
	if (gui_state != GuiState::Paused) return false;
	if (dojo.frame_number.load() < st.runTo) return false;
	return oracle::machineStopped();
}

void setSlot(int slot)
{
	config::SavestateSlot.set(slot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(slot));
}

bool exists(const std::string& p) { std::error_code ec; return !p.empty() && ghc::filesystem::exists(p, ec); }

//! One press-edge: the canon held ONE frame, then `gap` neutral frames.
std::vector<u16> pressStream(u16 canon, int gap)
{
	std::vector<u16> v;
	v.push_back(canon);
	for (int i = 0; i < gap; i++) v.push_back(0);
	return v;
}

// ---- step makers ---------------------------------------------------------------------
//! A P1 stream through tas_auto::playLive (the sender's path), run to its end + runAfter,
//! then `check` on the stopped machine.
Step press(const char *name, std::function<std::vector<u16>()> stream, u32 runAfter, std::function<bool()> check)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.needsPrev = true;
	s.maxWaitMs = 30000;
	s.act = [stream, runAfter] {
		why("");
		if (gui_state != GuiState::Paused) { why("not paused"); return false; }
		if (tas_auto::liveActive()) tas_auto::stopLive();
		const std::vector<u16> p1 = stream();
		if (p1.empty()) { why("empty stream (css not ported?)"); return false; }
		tas_auto::playLive(p1, std::vector<u16>(), (u64)dojo.frame_number.load());
		if (!tas_auto::liveActive()) { why("playLive did not go live"); return false; }
		runFrames((u32)p1.size() + runAfter);
		return true;
	};
	s.verify = [check] { if (!stopped()) { why("running"); return false; } return check(); };
	return s;
}

// ---- the module ----------------------------------------------------------------------
void addSteps(std::vector<Step>& out)
{
	auto add = [&](Step s) { out.push_back(std::move(s)); };

	// 1. on the globe: the handoff paused us here. The READY banner (gui.cpp's boot
	//    handoff) opened the States panel over the globe; Setup closes every panel, and
	//    this step closes it again in case it re-opened.
	{
		Step s;
		s.name = "css: on the globe (the seed's handoff)";
		s.kind = Kind::Record;
		s.act = [] {
			why("");
			std::string w;
			if (!loadTeam(w)) { why("%s", w.c_str()); return false; }
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			if (dojo.onenter_ff) { why("the seed is still playing"); return false; }
			const panels::Panel *p = panels::find("states");
			if (p != nullptr && p->open != nullptr && *p->open) *p->open = false;
			return true;
		};
		s.verify = [] {
			const int a = id2(Slot::P1_A), b = id2(Slot::P2_A);
			const int wa = css::idOf(css::home(0)), wb = css::idOf(css::home(1));
			why("ID_2 P1_A=%d (%s, want %d %s) P2_A=%d (%s, want %d %s) at frame %u",
					a, css::nameOf(a), wa, css::home(0).c_str(), b, css::nameOf(b), wb, css::home(1).c_str(), dojo.frame_number.load());
			return wa >= 0 && a == wa && b == wb;
		};
		add(s);
	}

	// 2. the walk: planned once, then ONE press per tour step so the human sees the cursor
	//    hop and the game's ID_2 after each press is held to the graph's prediction.
	{
		Step s;
		s.name = "css: plan the walk to Dhalsim";
		s.needsPrev = true;
		s.act = [] {
			why("");
			std::vector<std::string> plan = css::plan("Dhalsim", css::home(0));
			if (plan.empty()) { why("plan() is empty (css not ported?)"); return false; }
			if (plan.size() != 4) { why("plan has %u presses, this tour walks 4 (D,L,L,L)", (u32)plan.size()); return false; }
			st.walkPath.clear();
			st.pending.clear();
			std::string cur = css::home(0), path;
			for (const std::string& d : plan)
			{
				cur = css::neighbor(cur, d.c_str());
				st.walkPath.push_back(cur);
				path += (path.empty() ? "" : ">") + cur;
				st.pending.push_back(css::dirCanon(0, d.c_str()));
			}
			if (sabotaged("css-walk"))
			{	// SABOTAGE css-walk: the walker presses a direction the plan did not say (the
				// letter table off by one axis) - the prediction stays the plan's, and the game's
				// ID_2 after press 1 must refute it. `[MEASURED 2026-09-18]` "plan from Cable's
				// home" was DECORATIVE: Cable shares RubyHeart's row, so both plans start with D.
				const char *wrongDir = plan[0] == "R" ? "D" : "R";
				st.pending[0] = css::dirCanon(0, wrongDir);
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE css-walk - press 1 is %s where the plan said %s; the prediction is still %s (-> %s)",
						wrongDir, plan[0].c_str(), st.walkPath[0].c_str(), css::neighbor(css::home(0), wrongDir).c_str());
			}
			why("%s: predicted %s", (plan[0] + plan[1] + plan[2] + plan[3]).c_str(), path.c_str());
			return true;
		};
		add(s);
	}
	static const char *WALK[] = { "css: walk press 1", "css: walk press 2", "css: walk press 3", "css: walk to Dhalsim (press 4)" };
	for (int i = 0; i < 4; i++)
	{
		add(press(WALK[i],
			[i] { std::vector<u16> v; if ((size_t)i < st.pending.size()) v = pressStream(st.pending[i], 6); return v; },
			0,
			[i] {
				const int got = id2(Slot::P1_A);
				const std::string want = (size_t)i < st.walkPath.size() ? st.walkPath[i] : "?";
				why("ID_2 P1_A=%d (%s), the graph predicted %s", got, css::nameOf(got), want.c_str());
				return got >= 0 && want == css::nameOf(got);
			}));
	}

	// 3. pick Dhalsim: LP selects (the assist panel opens on alpha), LP again confirms.
	add(press("css: pick Dhalsim (LP selects, alpha assist)",
		[] { std::vector<u16> v(8, 0); v.push_back(css::paletteCanon(0, "LP")); for (int i = 0; i < 24; i++) v.push_back(0); return v; },
		0,
		[] {
			const int pal = palette(Slot::P1_A), ast = assist(Slot::P1_A), id = id2(Slot::P1_A);
			why("ID_2=%d (%s) PaletteID_2=%d (want 0 LP) Assist_Value=%d (want 0 alpha)", id, css::nameOf(id), pal, ast);
			return id == css::idOf("Dhalsim") && pal == 0 && ast == 0;
		}));
	add(press("css: confirm Dhalsim (slot B goes live)",
		[] { std::vector<u16> v(12, 0); v.push_back(css::paletteCanon(0, "LP")); for (int i = 0; i < 72; i++) v.push_back(0); return v; },
		0,
		[] {
			const int a = id2(Slot::P1_A), b = id2(Slot::P1_B);
			why("P1_A=%d (%s, locked) P1_B=%d (%s; the live cursor, reset to home)", a, css::nameOf(a), b, css::nameOf(b));
			return a == css::idOf("Dhalsim") && b == css::idOf(css::home(0));
		}));

	// 4. the rest of both teams: David's build_picks streams, merged frame by frame by playLive.
	{
		Step s;
		s.name = "css: pick the rest of P1 + all of P2 (build_picks)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 60000;
		s.act = [] {
			why("");
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			css::Timing t;
			if (sabotaged("css-timing"))
			{	// SABOTAGE css-timing: no neutral between presses - the same direction twice is one press
				t.navgap = 0;
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE css-timing - navgap 0, a repeated direction never re-presses");
			}
			const std::vector<css::Pick> rest(st.team.p1.begin() + 1, st.team.p1.end());
			std::vector<u16> p1 = css::buildPicks(0, rest, t);
			std::vector<u16> p2 = css::buildPicks(1, st.team.p2, t);
			if (sabotaged("css-team"))
			{	// SABOTAGE css-team: P2 never picks
				p2.clear();
				NOTICE_LOG(RENDERER, "SURFACE TOUR: SABOTAGE css-team - the P2 stream is skipped");
			}
			if (p1.empty()) { why("build_picks is empty (css not ported?)"); return false; }
			// `[MEASURED 2026-09-18]` tas_auto::liveTick ends the live send when the P1 vector is
			// exhausted (the sender is P1-only), so a longer P2 stream is silently truncated: P2's
			// third pick (Guile, frames 252..378) never happened and P2_C read Cable. Pad P1 with
			// neutral to P2's length. (A tas_auto fix is outside this module's files - noted.)
			if (p1.size() < p2.size()) p1.resize(p2.size(), 0);
			if (tas_auto::liveActive()) tas_auto::stopLive();
			tas_auto::playLive(p1, p2, (u64)dojo.frame_number.load());
			if (!tas_auto::liveActive()) { why("playLive did not go live"); return false; }
			runFrames((u32)std::max(p1.size(), p2.size()) + 30);
			return true;
		};
		s.verify = [] {
			if (!stopped()) { why("running"); return false; }
			const Slot slots[] = { Slot::P1_A, Slot::P1_B, Slot::P1_C, Slot::P2_A, Slot::P2_B, Slot::P2_C };
			std::string line;
			bool ok = true;
			for (int i = 0; i < 6; i++)
			{
				const css::Pick& p = i < 3 ? st.team.p1[i] : st.team.p2[i - 3];
				const int got = id2(slots[i]), want = css::idOf(p.name), ast = assist(slots[i]);
				line += std::string(i ? " " : "") + (i < 3 ? "P1_" : "P2_") + char('A' + i % 3) + "=" + std::to_string(got) + "/" + std::to_string(want);
				if (got != want || ast != 0) ok = false;
			}
			why("ID_2 got/want %s, assists all alpha: %s - %s", line.c_str(), ok ? "yes" : "NO", teamNames().c_str());
			return ok;
		};
		add(s);
	}

	// 5. SPEED SELECT -> the fight. Start, then the intro (the NEW HERO cards, the round
	//    call) runs `[MEASURED 2026-09-18]` well past 240 frames (health still 0, skip rate 6
	//    = the intro cadence at +243). So the step POLLS: run in 120-frame chunks until the
	//    game reports a fight (health 144/144, rate 4), bounded by kFightMaxFrames.
	{
		Step s;
		s.name = "css: Start at SPEED SELECT -> the fight";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 90000;
		s.act = [] {
			why("");
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			if (tas_auto::liveActive()) tas_auto::stopLive();
			st.fightFrom = dojo.frame_number.load();
			// `[MEASURED 2026-09-18]` one 3-frame Start alone left the game on SPEED SELECT for
			// 1500+ frames (health 0, rate 6). The probe that reached the fight pressed Start,
			// waited 90, then LP (the speed CONFIRM) - the same shape as a character pick:
			// select, then confirm. Mirror it.
			std::vector<u16> v(3, (u16)tas_macro::CANON_START);
			v.resize(3 + 90, 0);
			for (int i = 0; i < 3; i++) v.push_back((u16)tas_macro::CANON_LP);
			tas_auto::playLive(v, std::vector<u16>(), (u64)st.fightFrom);
			if (!tas_auto::liveActive()) { why("playLive did not go live"); return false; }
			runFrames((u32)v.size() + kFightChunk);
			return true;
		};
		s.verify = [] {
			if (!stopped()) { why("running"); return false; }
			u8 rate = 0, count = 0, toggle = 0;
			tas_mvc2::peekSkip(rate, count, toggle);
			const bool point = tas_mvc2::isPoint(0, 0);
			const int h1 = health(0), h2 = health(1);
			const u32 fr = dojo.frame_number.load();
			const bool fight = point && h1 == kExpectHealth && h2 == kExpectHealth && rate == 4;
			why("Is_Point(P1 A)=%d Health_Big P1=%d P2=%d (want %d/%d) skip rate=%u (want 4) at frame %u (+%u after Start)",
					(int)point, h1, h2, kExpectHealth, kExpectHealth, (unsigned)rate, fr, fr - st.fightFrom);
			if (fight) return true;
			if (fr - st.fightFrom < kFightMaxFrames) { runFrames(kFightChunk); return false; }	// the intro is still running: keep going
			return false;
		};
		add(s);
	}

	// 6. save the Dhalsim base: slot 0 of the session's clip folder (bound by RecordMatches).
	{
		Step s;
		s.name = "css: save the Dhalsim base (slot 0)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			if (hostfs::savestateFolderOverride.empty()) { why("no clip folder bound (RecordMatches=yes?)"); return false; }
			if (tas_auto::liveActive()) tas_auto::stopLive();
			const int user = (int)config::SavestateSlot;
			setSlot(0);
			gui_saveState();	// synchronous while Paused; a direct call, not the F1 tap the BASE guard blocks
			setSlot(user);
			return true;
		};
		s.verify = [] {
			if (!oracle::machineStopped()) { why("not stopped"); return false; }
			const std::string path = hostfs::getSavestatePath(0, false);
			if (!exists(path)) { why("no file at %s", path.c_str()); return false; }
			st.baseFrame = dojo.frame_number.load();
			st.baseHash = oracle::machineHash();
			st.baseFolder = hostfs::savestateFolderOverride;
			NOTICE_LOG(RENDERER, "CSS BASE: slot 0 @ frame %u hash=%08X team=%s folder=%s",
					st.baseFrame, st.baseHash, teamNames().c_str(), st.baseFolder.c_str());
			why("slot 0 @ frame %u hash=%08X -> %s", st.baseFrame, st.baseHash, path.c_str());
			return true;
		};
		add(s);
	}

	// 6b. THE RUN-OUT. `[MEASURED 2026-09-18]` the base copied out of this session ended its
	//     movie AT the base frame (2059 = slot 0's frame), so a Replay boot of the fixture
	//     auto-seeking slot 0 landed on the movie's end -> ReplayEnd, never Paused, and every
	//     tour step after it read `still running at frame 2059`. A base clip's movie must run
	//     PAST its slot 0 (the harness base ran 1600 frames past). Step 600 neutral frames here
	//     - the state stays at 2059; the movie grows through the normal record path - then the
	//     steps below reload the base as before.
	{
		Step s;
		s.name = "css: run-out (the movie must run past the base)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 60000;
		s.act = [] {
			why("");
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			st.runOutTarget = dojo.frame_number.load() + 600;
			gui_step_frames(600);
			settings.input.fastForwardMode = true;
			return true;
		};
		s.verify = [] {
			if (gui_state != GuiState::Paused || dojo.frame_number.load() < st.runOutTarget) return false;
			settings.input.fastForwardMode = false;
			if (!oracle::machineStopped()) return false;
			const u32 end = movie::end();
			if (end < st.baseFrame + 600) { why("movie end %u < base+600 (%u)", end, st.baseFrame + 600); return false; }
			why("movie end %u (base %u + %u); a Replay boot of this clip can now seek slot 0 and pause", end, st.baseFrame, end - st.baseFrame);
			return true;
		};
		add(s);
	}

	// 7. THE FINDING: David's Dhalsim97 rows on a Dhalsim base. Optional - a peak of 0 is a
	//    finding about PS2->DC transfer, never a defect in the studio.
	{
		Step s;
		s.name = "css finding: David's Dhalsim97 rows on the Dhalsim base";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.optional = true;
		s.maxWaitMs = 120000;
		s.act = [] {
			why("");
			st.findingPhase = 0;
			std::string w;
			if (!intent::ready(w)) { why("intent not ready: %s", w.c_str()); return false; }
			if (!intent::comboLoaded(w)) { why("no combo staged: %s", w.c_str()); return false; }
			if (!intent::begin("css finding")) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) { why("loading / running"); return false; }
			if (st.findingPhase == 0)
			{
				if (!intent::placeCombo(0, 0)) { why("%s", intent::lastWhy()); return false; }
				if (!intent::runToStop()) { why("%s", intent::lastWhy()); return false; }
				st.findingPhase = 1;
				why("running %s from %u", intent::comboName(), intent::t0());
				return false;
			}
			st.findingPeak = intent::peak(0);
			// What this run MATCHES of David's (PS2 researcher, scratchpad/ps2_side.md): the
			// characters, their order and the point (by construction); the fight-start anchor
			// only loosely (his `_default` marker sits ~615 rows after his last confirm; ours is
			// wherever the poll caught the fight). NOT matched: his stage (Boat2), meter,
			// positions, assists, the dummy's state - his rows 3172..4212 were that SETUP and
			// are not replayed here (a second optional variant needs intent to re-window the
			// macro in-process; intent.cpp is not this module's file - noted for the coordinator).
			u32 stage = 0, throwT = 0;
			tas_mvc2::readRamSafe(tas_mvc2::toFlycast(0x2C26A95C), 1, stage);		// Stage_Selector (SPREADSHEET SystemMemoryAddresses)
			tas_mvc2::readRamSafe(tas_mvc2::toFlycast(0x2C289602), 1, throwT);	// Match_Start_Throw_Timer
			u8 rate = 0, count = 0, toggle = 0;
			tas_mvc2::peekSkip(rate, count, toggle);
			NOTICE_LOG(RENDERER, "CSS FINDING: David's %s on the Dhalsim base -> peak %d (P2 %d) after=%08X | matched: chars/order/point; stage=%u (his Boat2) throw_timer=%u skip=%u/4; NOT matched: meter/positions/assists (his setup rows 3172..4212 not replayed)",
					intent::comboName(), st.findingPeak, (int)intent::peak(1), intent::machineHash(), stage, throwT, (unsigned)rate);
			why("peak %d (P2 %d) after=%08X - %s [stage=%u throw_timer=%u skip=%u; his target 97 assumed his stage+setup]",
					st.findingPeak, (int)intent::peak(1), intent::machineHash(),
					st.findingPeak >= 1 ? "his rows CONNECT on DC" : "his rows do not connect here (PS2->DC timing: a finding, not a defect)",
					stage, throwT, (unsigned)rate);
			return st.findingPeak >= 1;
		};
		add(s);
	}

	// 8. end on the base: the finding (if it began) restored through intent, the base reloaded.
	{
		Step s;
		s.name = "css: end on the Dhalsim base";
		s.kind = Kind::Record;
		s.maxWaitMs = 20000;
		s.optional = true;		// no base saved (an arm reddened the chain) -> SKIP, never a second FAIL
		s.act = [] {
			why("");
			if (tas_auto::liveActive()) tas_auto::stopLive();
			if (gui_state != GuiState::Paused) { why("not paused"); return false; }
			if (st.baseFrame == 0) { why("no base was saved - nothing to end on"); return false; }
			std::string w;
			if (!intent::ready(w)) { why("intent not ready: %s", w.c_str()); return false; }
			if (!intent::end() && !intent::reloadBase()) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) { why("loading"); return false; }
			const u32 h = oracle::machineHash();
			why("BASE @%u hash=%08X (saved %08X)", dojo.frame_number.load(), h, st.baseHash);
			return dojo.frame_number.load() == st.baseFrame && h == st.baseHash;
		};
		add(s);
	}
}

const ArmSpec ARMS[] = {
	{ "css-walk",   "css: walk press 1",                                   "css: on the globe (the seed's handoff)" },
	{ "css-timing", "css: pick the rest of P1 + all of P2 (build_picks)",  "css: walk to Dhalsim (press 4)" },
	{ "css-team",   "css: pick the rest of P1 + all of P2 (build_picks)",  "css: confirm Dhalsim (slot B goes live)" },
};

const ExpectDecl EXPECTS[] = {
	{ "css finding:",       1, 1, false, "mover" },
	{ "css: on the globe",  0, 0, false, "ui" },
	{ "css: plan",          0, 0, false, "ui" },
	{ "css: walk",          1, 1, false, "mover" },		// a live press bakes into the roll and moves the cursor
	{ "css: pick",          1, 1, false, "mover" },
	{ "css: confirm",       1, 1, false, "mover" },
	{ "css: Start",         1, 1, false, "mover" },
	{ "css: save",          2, 0, false, "any" },		// a file write; the slot is set and restored
	{ "css: run-out",       1, 1, false, "mover" },		// 600 recorded neutral frames: machine AND movie move
	{ "css: end",           1, 2, true,  "mover/converge" },	// lands on the save's hash
};

const Module MOD = { "css", addSteps, ARMS, (int)(sizeof(ARMS) / sizeof(ARMS[0])), EXPECTS, (int)(sizeof(EXPECTS) / sizeof(EXPECTS[0])) };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
