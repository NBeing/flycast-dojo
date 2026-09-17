#include "surface_tour.h"
#include "hotkey_bind.h"
#include "oracle.h"
#include "dojo.h"
#include "session.h"
#include "roll_host.h"
#include "ui_text.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "input/gamepad_device.h"
#include "input/mapping.h"
#include "input/hotkeys.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <xxhash.h>

// types.h defines a debug-assert macro named `verify(x)`; Step::verify is a member
// of the frozen contract, and this file never uses the assert.
#undef verify

/*
	THE RUNNER. See surface_tour.h for what the tour is and the contract it keeps.

	A PHASE MACHINE, NOT A LOOP, for the same reason the FST's is: every action
	hands control back so frames (and the human's eyes) can catch up. Per step:

	   Begin ──► Act (+TourArmMs) ──► Dwell (TourBpmMs | TourRecordMs) ──► Verify
	                                                                       (poll ≤ maxWaitMs)
	The dwell is the "bpm" the user asked for - a click lands, a second passes, the
	verdict is read. TourArmMs sits between Begin and Act because the rebind engine
	ignores a press for 0.2 s after arming (gamepad_device.cpp detectButtonOrAxisInput);
	it is clamped to at least that.

	THE WHOLE TOUR RUNS PAUSED. gui_hotkey_allowed() permits Paused, every panel
	(Menu- and Both-stream) draws there, and a stopped machine cannot wander while a
	window is being opened at it. Steps that need frames use gui_step_frames, which
	returns to Paused (steppable because the harness runs a RecordMatches=yes WRITE
	session).

	NOTHING IS LEFT BEHIND. Every panel's open flag and every TAS binding is
	snapshotted before the first step and put back after the last (or after an
	abort), so a human-watched run on a real config ends exactly as it began.
*/
namespace roll {
namespace surfacetour {

// ---- pure parts, covered by selfTest() ------------------------------------------

//! Click steps wait the bpm; Record steps wait the longer record dwell.
static int dwellMs(Kind k, int bpm, int rec) { return k == Kind::Record ? rec : bpm; }

//! The rebind engine ignores a press for 200 ms after arming; never act sooner.
static int clampArmMs(int ms) { return ms < 200 ? 200 : ms; }

//! The sabotage: the same key with Shift held is a chord nothing is bound to.
static u32 sabotageCode(u32 code) { return code | InputMapping::KEY_MOD_SHIFT; }

/*
	SABOTAGE CLASSES (surface_tour.h v2). `sabotage[:<cls>[+<cls>...]]` - '+' because
	the -config parser cuts a value at the first comma; bare "sabotage" == "open" so
	the existing twin keeps its meaning. Pure, so selfTest can cover the grammar.
*/
static std::vector<std::string> parseSabotage(const std::string& mode)
{
	std::vector<std::string> out;
	if (mode == "sabotage")
	{
		out.push_back("open");
		return out;
	}
	const std::string pfx = "sabotage:";
	if (mode.compare(0, pfx.size(), pfx) != 0)
		return out;
	std::string cur;
	for (size_t i = pfx.size(); i <= mode.size(); i++)
	{
		if (i == mode.size() || mode[i] == '+')
		{
			if (!cur.empty()) out.push_back(cur);
			cur.clear();
		}
		else
			cur += mode[i];
	}
	return out;
}

static std::vector<std::string> g_sabotage;		//!< the armed classes, parsed once in init()

bool sabotaged(const char *cls)
{
	for (const std::string& c : g_sabotage)
		if (c == cls)
			return true;
	return false;
}

/*
	THE ARM DECLARATIONS - ONE OWNER. For every class the runner says, at start, the
	one step the arm must redden and one step that must stay green, by the exact
	names buildSteps() emits. The harness judge (scripts/lib/arms.sh, the lifted
	arms.lua rules) parses these lines and keeps no table of its own - a second
	copy of this table is the drift both source repos already paid for.

	mustBreak == "" means NOTHING may redden: gate-can-pass (the whole run must go
	green with the class parser engaged - a gate that can never pass is as useless
	as one that can never fail) and write-clobber (`[MEASURED 2026-09-17]` the WRITE
	clobber does not manifest on this fixture - see the enter-authoring step - so
	the arm is inconclusive-by-design here and must not fake a redden).
*/
struct ArmDecl { const char *cls; const char *mustBreak; const char *mustNotBreak; };
static const ArmDecl ARMS[] = {
	{ "open",          "open: pianoroll",                     "rebind: pianoroll -> Ctrl+F1" },
	{ "rebind",        "rebind: macros -> Alt+F6",            "rebind: snippets -> Alt+F5" },
	{ "show",          "show: David's base state (1 frame)",  "load slot 0 (David's base)" },
	{ "write-clobber", "",                                    "savestate: load slot 99" },
	{ "gate-can-pass", "",                                    "open: pianoroll" },
	{ "flip",          "roll: flip a cell + undo",            "states: label round-trip" },
	{ "label",         "states: label round-trip",            "roll: flip a cell + undo" },
	{ "save",          "savestate: save slot 99",             "states: label round-trip" },
	{ "branch",        "branch: create from slot 0",          "test lab: add test from slot 0" },
};
static const int NARMS = (int)(sizeof(ARMS) / sizeof(ARMS[0]));

static const ArmDecl *armDeclOf(const char *cls)
{
	for (int i = 0; i < NARMS; i++)
		if (strcmp(ARMS[i].cls, cls) == 0)
			return &ARMS[i];
	return nullptr;
}

//! Verdict rules, in precedence: needsPrev -> SKIP; optional+false -> SKIP; else PASS/FAIL.
static int judge(bool ok, bool optional, bool needsPrev, bool prevPassed)
{
	if (needsPrev && !prevPassed)
		return 2;
	if (!ok)
		return optional ? 2 : 0;
	return 1;
}

static const char *verdictText(int v) { return v == 1 ? "PASS" : v == 0 ? "FAIL" : v == 2 ? "SKIP" : "..."; }

//! The facts ledger, one per step - documented at struct Rec below, where it lives.
struct Facts
{
	bool beforeOk = false, afterOk = false;	//!< the machine was stopped when read
	u32 machineBefore = 0, machineAfter = 0;
	u64 movieBefore = 0,   movieAfter = 0;
	u32 panelsBefore = 0,  panelsAfter = 0;		//!< open mask, every panel but surfacetour/game
	u32 bindingsBefore = 0, bindingsAfter = 0;	//!< XXH32 over the TAS actions' codes
	int slotBefore = 0,    slotAfter = 0;
	int modeBefore = 0,    modeAfter = 0;		//!< session::mode()
	u32 frameBefore = 0,   frameAfter = 0;
	int gate = -1;								//!< -1 not judged, 0 ok, 1 VACUOUS, 2 LEAK, 3 unmeasured
	std::string gateWhy;
};

// ---- the gate's pure half, covered by selfTest() -----------------------------------
//
// WHAT EACH STEP IS DECLARED TO DO to the machine and to the movie, by name prefix.
// A RUNNER-SIDE TABLE, not a Step field: the contract is frozen, and nbneo-rr's
// lesson (FlowStep::moves_machine, capture.cpp:555-560) is that BOTH directions
// must be asserted - a mover that moved nothing is vacuous, and a UI step that
// moved the machine is a leak, "a structural verb that quietly grew transport
// behaviour". The prefixes are the step names buildSteps() emits.
enum class MExp : u8 { Unchanged, Moved, Any };			//!< the machine
enum class VExp : u8 { Unchanged, Moved, Any };			//!< the movie
struct Expect
{
	MExp machine;
	VExp movie;
	bool converge;		//!< a mover that must LAND WHERE ANOTHER MOVER LANDED (an assertion, not an exemption)
	const char *label;
};

static bool startsWith(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static Expect expectOf(const char *name)
{
	// Loads and the 1-frame shows revisit the same two machine states over and over
	// (slot 0, and slot 0 + one frame); nbneo-rr's must_converge is the honest way to
	// say so - each must pair with another mover on an identical hash, or it is the
	// disagreement the journey exists to find.
	if (startsWith(name, "load slot 0"))                 return { MExp::Moved,     VExp::Unchanged, true,  "mover/converge" };
	if (startsWith(name, "show: slot 99"))               return { MExp::Moved,     VExp::Unchanged, false, "mover" };
	if (startsWith(name, "show:"))                       return { MExp::Moved,     VExp::Unchanged, true,  "mover/converge" };
	// `[MEASURED 2026-09-17]` the machine is ALREADY in the state slot 99 holds when it
	// is loaded (saved two UI steps earlier, nothing moved it since): aa983314 ->
	// aa983314. "Moved" is false by construction; the honest claim is IDENTITY - the
	// loaded machine equals the hash recorded when it was saved (gateStep's floor).
	if (startsWith(name, "savestate: load slot 99"))     return { MExp::Any,       VExp::Unchanged, true,  "identity" };
	if (startsWith(name, "roll: flip a cell + undo"))    return { MExp::Unchanged, VExp::Unchanged, false, "netzero" };
	if (startsWith(name, "snippets: place"))             return { MExp::Unchanged, VExp::Moved,     false, "movie-mover" };
	if (startsWith(name, "macros: place"))               return { MExp::Unchanged, VExp::Moved,     false, "movie-mover" };
	// The branch's slot 0 is a COPY of main's, so checkout lands where load slot 0
	// did; the movie is a copy too but is re-attached from disk - not asserted.
	if (startsWith(name, "branch: checkout"))            return { MExp::Moved,     VExp::Any,       true,  "mover/converge" };
	if (startsWith(name, "branch: back to main"))        return { MExp::Moved,     VExp::Any,       true,  "mover/converge" };
	if (startsWith(name, "captures:"))                   return { MExp::Moved,     VExp::Unchanged, false, "mover" };
	if (startsWith(name, "FST:"))                        return { MExp::Moved,     VExp::Unchanged, false, "mover" };
	if (startsWith(name, "branch export:"))              return { MExp::Moved,     VExp::Any,       false, "mover" };
	// "sender: send" is a UI step TODAY: playLive is consumed only at a maple poll and
	// the tour never steps here, so nothing reaches the machine or the movie. The
	// game-level module (not yet authorized) is what makes it a mover.
	return { MExp::Unchanged, VExp::Unchanged, false, "ui" };
}

//! Did anything in the whole tuple move? (nbneo-rr's `changed`, our six members.)
static bool tupleChanged(const Facts& f)
{
	return f.machineAfter != f.machineBefore || f.movieAfter != f.movieBefore
		|| f.panelsAfter != f.panelsBefore || f.bindingsAfter != f.bindingsBefore
		|| f.slotAfter != f.slotBefore || f.modeAfter != f.modeBefore;
}

/*
	The per-step gate verdict from the facts and the expectation. 0 ok, 1 VACUOUS
	(a declared mover that moved nothing), 2 LEAK (a declared non-mover that moved
	the machine or the movie), 3 unmeasured (either capture found the machine
	running). `[DEVIATION from nbneo-rr]` their no-op gate requires EVERY pressed
	step to move the tuple or bring its own instrument; our UI steps' read-back
	verify IS their instrument (it returns false unless the artifact changed), so
	the no-op gate here applies to declared movers only, and UI steps are held to
	LEAK.
*/
static int gateVerdict(const Facts& f, const Expect& e, std::string& why)
{
	if (!f.beforeOk || !f.afterOk) { why = "the machine was running at a capture"; return 3; }
	const bool mMoved = f.machineAfter != f.machineBefore;
	const bool vMoved = f.movieAfter != f.movieBefore;
	if (e.machine == MExp::Moved && !mMoved)
	{
		why = "vacuous: the machine did not move";
		return 1;
	}
	if (e.movie == VExp::Moved && !vMoved)
	{
		why = "vacuous: the movie did not move";
		return 1;
	}
	if (e.machine == MExp::Unchanged && mMoved)
	{
		char b[96];
		snprintf(b, sizeof(b), "leak: a UI step moved the machine %08x -> %08x", f.machineBefore, f.machineAfter);
		why = b;
		return 2;
	}
	if (e.movie == VExp::Unchanged && vMoved)
	{
		why = "leak: the movie moved under a step that must leave it alone";
		return 2;
	}
	why = tupleChanged(f) ? "" : "nothing in the tuple moved";
	return 0;
}

//! The override rule: a gate finding turns a PASS into a FAIL; it never touches a
//! SKIP (nothing ran) and never rescues a FAIL.
static int applyGate(int verdict, int gate)
{
	if (verdict == 1 && (gate == 1 || gate == 2))
		return 0;
	return verdict;
}

static const char *gateText(int g) { return g == 0 ? "ok" : g == 1 ? "VACUOUS" : g == 2 ? "LEAK" : g == 3 ? "unmeasured" : "-"; }

// ---- state --------------------------------------------------------------------

static char g_why[256] = "";

void why(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_why, sizeof(g_why), fmt, ap);
	va_end(ap);
}
const char *lastWhy() { return g_why; }

/*
	THE FACTS LEDGER - the machine, the movie and the surface EITHER SIDE of a step.

	`[PORTED 2026-09-17]` nbneo-rr's FlowStepFacts (shell/gui/capture.cpp:444-494),
	the ledger its flow_gates() judges ~90 journey steps by. Their rule, kept: the
	hash "and nothing derived from it" - 'which state am I in' is the question,
	never 'is this recent'. Captured only while the emulator is STOPPED (oracle.h):
	a running machine is not a value, and a step that could not be measured says so
	(unmeasured) rather than comparing noise.

	"CHANGED" IS THE TUPLE, NOT THE HASH ALONE (capture.cpp:30303-30314): a UI step
	legitimately leaves the machine alone and a mover legitimately leaves the
	windows alone, so each member answers a different question and the gate reads
	the pair (machine, movie) it was declared to care about.
*/
struct Rec
{
	Step step;
	int verdict = -1;
	std::string why;
	Facts facts;
	int armMsOverride = -1;		//!< the `rebind` arm: act at t=0, inside the engine's deaf window
};

enum class Phase { Idle, WaitReady, Setup, Begin, Act, Dwell, Verify, Restore, Done };

static struct State
{
	bool enabled = false, sabotage = false, slow = false, inited = false;
	int bpmMs = 1000, recordMs = 2000, armMs = 300;
	Phase phase = Phase::Idle;
	std::vector<Rec> steps;
	int cur = -1;
	double t0 = 0, tAct = 0, tVerify = 0, readyStart = 0;
	bool actOk = false;
	int focusRetries = 0;
	bool focusClear = false;
	u32 slot0frame = 0;
	bool haveSlot0frame = false;
	// snapshot
	bool snapped = false;
	std::vector<std::pair<std::string, bool>> panelsOpen;
	std::vector<std::pair<DreamcastKey, u32>> bindings;
	bool playMatch = false;
	int slot = 0;
	int passed = 0, failed = 0, skipped = 0;
	// the gate's own reference points and tallies
	u32 setupHash = 0;				//!< the machine at Setup (Paused, before step 1)
	bool haveSetupHash = false;
	u32 hash99 = 0;					//!< the machine right after "savestate: save slot 99" settled
	bool haveHash99 = false;
	int gateOk = 0, vacuous = 0, leak = 0, unmeasured = 0;
} st;

bool focusClearRequested() { return st.focusClear; }
int  current()             { return (st.phase == Phase::Begin || st.phase == Phase::Act || st.phase == Phase::Dwell || st.phase == Phase::Verify) ? st.cur : -1; }
int  total()               { return (int)st.steps.size(); }
const char *stepName(int i){ return (i >= 0 && i < (int)st.steps.size()) ? st.steps[i].step.name : ""; }
int  verdict(int i)        { return (i >= 0 && i < (int)st.steps.size()) ? st.steps[i].verdict : -1; }

bool injectKey(u32 code)
{
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	if (kbd == nullptr)
	{
		why("no keyboard device");
		return false;
	}
	// The SAME full code on press and release. A direct call bypasses the
	// keyboard's chordCode() bookkeeping, so nothing else pairs the release.
	kbd->gamepad_btn_input(code, true);
	kbd->gamepad_btn_input(code, false);
	return true;
}

// ---- the key table -------------------------------------------------------------
// Raw SDL scancodes (F1=58..F12=69) with the chord modifier in the high bits. Avoids
// Ctrl+F2 (SLOT_PREV), F11 (SDL's fullscreen intercept) and Alt+F4 (a WM close).
struct PanelKey { const char *panel; DreamcastKey action; u32 code; const char *chord; };
static const u32 CTRL = InputMapping::KEY_MOD_CTRL, ALT = InputMapping::KEY_MOD_ALT;
static const PanelKey KEYS[] = {
	{ "pianoroll",     EMU_BTN_PIANO_ROLL,     CTRL | 58, "Ctrl+F1"  },
	{ "states",        EMU_BTN_SLOT_PICKER,    CTRL | 60, "Ctrl+F3"  },
	{ "hotkeys",       EMU_BTN_HOTKEY_HELP,    CTRL | 61, "Ctrl+F4"  },
	{ "inputviz",      EMU_BTN_PANEL_INPUTVIZ, CTRL | 62, "Ctrl+F5"  },
	{ "sender",        EMU_BTN_PANEL_SENDER,   CTRL | 63, "Ctrl+F6"  },
	{ "captures",      EMU_BTN_PANEL_CAPTURES, CTRL | 64, "Ctrl+F7"  },
	{ "timeline",      EMU_BTN_PANEL_TIMELINE, CTRL | 65, "Ctrl+F8"  },
	{ "frameskiptest", EMU_BTN_PANEL_FST,      CTRL | 66, "Ctrl+F9"  },
	{ "uitext",        EMU_BTN_PANEL_UITEXT,   CTRL | 67, "Ctrl+F10" },
	{ "notepad",       EMU_BTN_PANEL_NOTEPAD,  CTRL | 69, "Ctrl+F12" },
	{ "testlab",       EMU_BTN_PANEL_TESTLAB,  ALT  | 58, "Alt+F1"   },
	{ "branches",      EMU_BTN_PANEL_BRANCHES, ALT  | 60, "Alt+F3"   },
	{ "snippets",      EMU_BTN_PANEL_SNIPPETS, ALT  | 62, "Alt+F5"   },
	{ "macros",        EMU_BTN_PANEL_MACROS,   ALT  | 63, "Alt+F6"   },
};
static const int NKEYS = (int)(sizeof(KEYS) / sizeof(KEYS[0]));

// Owned storage for generated step names. A DEQUE, not a vector: Step::name is a
// const char* into these strings, and a vector reallocates as it grows, leaving
// every earlier pointer dangling - `[MEASURED 2026-09-17]` step 16 printed as "e".
// deque::push_back never moves existing elements.
static std::deque<std::string> g_names;

static const char *keep(const std::string& s)
{
	g_names.push_back(s);
	return g_names.back().c_str();
}

// ---- snapshot / restore ---------------------------------------------------------

static void snapshot()
{
	st.panelsOpen.clear();
	for (const panels::Panel& p : panels::all())
	{
		if (strcmp(p.id, "surfacetour") == 0 || strcmp(p.id, "game") == 0)
			continue;
		st.panelsOpen.push_back({ p.id, *p.open });
	}
	st.bindings.clear();
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	const std::shared_ptr<InputMapping> map = kbd != nullptr ? kbd->get_input_mapping() : nullptr;
	for (int i = 0; i < hotkeys::count(); i++)
	{
		const DreamcastKey id = hotkeys::all()[i].id;
		st.bindings.push_back({ id, map != nullptr ? map->get_button_code(0, id) : (u32)-1 });
	}
	st.playMatch = dojo.play_match;
	st.slot = (int)config::SavestateSlot;
	st.snapped = true;
}

static void restore()
{
	if (!st.snapped)
		return;
	rebind::cancel();
	int np = 0;
	for (const auto& kv : st.panelsOpen)
	{
		const panels::Panel *p = panels::find(kv.first.c_str());
		if (p != nullptr && *p->open != kv.second)
		{
			*p->open = kv.second;
			np++;
		}
	}
	int nb = 0;
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	const std::shared_ptr<InputMapping> map = kbd != nullptr ? kbd->get_input_mapping() : nullptr;
	if (map != nullptr)
	{
		for (const auto& kv : st.bindings)
		{
			if (map->get_button_code(0, kv.first) == kv.second)
				continue;
			map->clear_button(0, kv.first);
			map->clear_axis(0, kv.first);
			if (kv.second != (u32)-1)
				map->set_button(0, kv.first, kv.second);
			nb++;
		}
		if (nb > 0)
		{
			map->set_dirty();
			kbd->save_mapping();
		}
	}
	if (st.playMatch && !dojo.play_match)
		gui_set_driver(0);
	config::SavestateSlot.set(st.slot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(st.slot));
	NOTICE_LOG(RENDERER, "SURFACE TOUR: restored panels=%d bindings=%d", np, nb);
}

// ---- the steps ------------------------------------------------------------------

static void setSlot(int n)
{
	config::SavestateSlot.set(n);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(n));
}

static void buildSteps()
{
	st.steps.clear();
	g_names.clear();
	auto add = [&](Step s) { Rec r; r.step = std::move(s); st.steps.push_back(std::move(r)); };

	/*
		SHOW IT - step the game ONE frame after every load.

		`[MEASURED 2026-09-17]` the user, watching: "the load state was never shown
		because of pause". The whole tour runs Paused, and while Paused the renderer
		never presents - so after gui_loadState the screen kept the PRE-load picture.
		The load verified (the frame number matched) and the human never saw it.
		gui_step_frames(1) runs exactly one frame, which forces a present, and the
		stop side in gui_display_osd returns the machine to Paused. It is also a
		claim of its own: the step lands frame-exact, +1. Kind::Record because it
		moves the machine and deserves the longer dwell for the eye.
	*/
	static u32 g_showFrom = 0;
	auto show = [&](const char *name) {
		Step s;
		s.name = name;
		s.kind = Kind::Record;
		s.needsPrev = true;			// showing a load that did not happen is not a claim
		s.maxWaitMs = 3000;			// the frame runs on the emu thread; poll the return to Paused
		// THE `show` ARM: skip the step for David's base show ONLY, and do not fake
		// anything - the step's own verify then reads frame == g_showFrom (FAIL) and
		// the gate reads a declared mover that moved nothing (VACUOUS). Both clauses
		// see it honestly; a faked frame+1 would test only the verify.
		const bool sab = sabotaged("show") && strcmp(name, "show: David's base state (1 frame)") == 0;
		s.act = [sab] {
			if (gui_state != GuiState::Paused) { why("not paused before the step"); return false; }
			g_showFrom = dojo.frame_number.load();
			if (!sab)
				gui_step_frames(1);
			return true;
		};
		s.verify = [] {
			const u32 fr = dojo.frame_number.load();
			if (gui_state != GuiState::Paused) { why("still running at frame %u", fr); return false; }
			if (fr != g_showFrom + 1) { why("frame %u, expected %u+1", fr, g_showFrom); return false; }
			why("frame %u presented", fr);
			return true;
		};
		add(s);
	};

	// 1. load David's base state
	{
		Step s;
		s.name = "load slot 0 (David's base)";
		s.kind = Kind::Record;
		s.act = [] { setSlot(0); gui_loadState(); return true; };
		s.verify = [] {
			if (gui_state != GuiState::Paused) { why("not paused after load"); return false; }
			if (st.haveSlot0frame && dojo.frame_number.load() != st.slot0frame)
			{
				why("frame %u != slot0 frame %u", dojo.frame_number.load(), st.slot0frame);
				return false;
			}
			why("frame %u", dojo.frame_number.load());
			return true;
		};
		add(s);
	}
	show("show: David's base state (1 frame)");

	/*
		3-44. PER WINDOW: rebind its key, open it WITH that key, close it.

		`[MEASURED 2026-09-17]` the user, watching: "whenever the rebind happens i
		dont see it, make them meaningful, i.e. open the window." A rebind is
		invisible - the engine arms, a key is pressed in-process, a mapping changes,
		and the screen does nothing. So the two phases are interleaved into TRIPLETS,
		cause beside effect: rebind X, then open X with the key just bound, then
		close it. And the HOTKEYS panel goes FIRST and STAYS OPEN through the rest:
		it is the live cheat sheet, so every later rebind is visibly a cell flipping
		from "unbound" to its chord. Its own close is the last step of the phase.
		Same 42 steps as before, reordered. Each rebind narrates "was -> now".
	*/
	static std::string g_was[NKEYS];
	int hotkeysIdx = 0;
	for (int i = 0; i < NKEYS; i++)
		if (strcmp(KEYS[i].panel, "hotkeys") == 0) hotkeysIdx = i;

	auto rebindStep = [&](int i) {
		const PanelKey& k = KEYS[i];
		Step s;
		s.name = keep(std::string("rebind: ") + k.panel + " -> " + k.chord);
		s.begin = [i] {
			const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
			if (kbd == nullptr) { why("no keyboard"); return false; }
			const std::string was = rebind::bindingName(kbd, KEYS[i].action);
			g_was[i] = was.empty() ? "unbound" : was;
			rebind::arm(KEYS[i].action, kbd);
			if (!rebind::active()) { why("arm refused"); return false; }
			return true;
		};
		s.act = [i] { return injectKey(KEYS[i].code); };
		s.verify = [i] {
			const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
			const std::shared_ptr<InputMapping> map = kbd != nullptr ? kbd->get_input_mapping() : nullptr;
			if (map == nullptr) { why("no mapping"); return false; }
			const u32 got = map->get_button_code(0, KEYS[i].action);
			if (got != KEYS[i].code)
			{
				why("bound code %u, wanted %u (%s)", got, KEYS[i].code, rebind::bindingName(kbd, KEYS[i].action).c_str());
				return false;
			}
			why("%s -> %s", g_was[i].c_str(), rebind::bindingName(kbd, KEYS[i].action).c_str());
			return true;
		};
		add(s);
		// THE `rebind` ARM: for the macros rebind ONLY, act at t=0 - inside the
		// engine's 0.2 s deaf window (gamepad_device.cpp detectButtonOrAxisInput sets
		// _detection_start_time = now + 0.2), so the press falls through to normal
		// dispatch (Alt+F6 is unbound: nothing happens), the binding is never written,
		// and the verify reads the old code. Its open/close cascade SKIPs via needsPrev.
		if (sabotaged("rebind") && strcmp(k.panel, "macros") == 0)
			st.steps.back().armMsOverride = 0;
	};
	auto openStep = [&](int i) {
		Step s;
		s.name = keep(std::string("open: ") + KEYS[i].panel);
		s.needsPrev = true;			// opening with a key that did not bind is not a claim
		s.act = [i] {
			// The sabotage: the piano roll's open presses a chord nothing is bound to.
			const bool sab = st.sabotage && strcmp(KEYS[i].panel, "pianoroll") == 0;
			return injectKey(sab ? sabotageCode(KEYS[i].code) : KEYS[i].code);
		};
		s.verify = [i] {
			const panels::Panel *p = panels::find(KEYS[i].panel);
			if (p == nullptr) { why("no such panel"); return false; }
			if (!*p->open) { why("open=false captured=%s", gui_keyboard_captured() ? "yes" : "no"); return false; }
			why("opened with %s", KEYS[i].chord);
			return true;
		};
		add(s);
	};
	auto closeStep = [&](int i) {
		Step s;
		s.name = keep(std::string("close: ") + KEYS[i].panel);
		s.needsPrev = true;
		s.act = [i] { return injectKey(KEYS[i].code); };
		s.verify = [i] {
			const panels::Panel *p = panels::find(KEYS[i].panel);
			if (p == nullptr) { why("no such panel"); return false; }
			if (*p->open) { why("open=true captured=%s", gui_keyboard_captured() ? "yes" : "no"); return false; }
			why("closed with %s", KEYS[i].chord);
			return true;
		};
		add(s);
	};

	// the cheat sheet first, and it stays up
	rebindStep(hotkeysIdx);
	openStep(hotkeysIdx);
	// every other window: rebind, open with it, close - with the sheet showing the flip
	for (int i = 0; i < NKEYS; i++)
	{
		if (i == hotkeysIdx)
			continue;
		rebindStep(i);
		openStep(i);
		closeStep(i);
	}
	// and the sheet last. Its close step's needsPrev looks at the previous step
	// (the last panel's close), which is the right dependency: the phase ended clean.
	closeStep(hotkeysIdx);

	/*
		44. enter authoring in READ-WRITE, not WRITE.

		The DOCUMENTED contract (docs/tas-fork/CANON_readwrite_model.md:98,
		dojo.cpp:582): in WRITE every advanced frame is overwritten by the pad,
		neutral included ("the stomp lands on the frame you advance INTO"), while
		READ-WRITE preserves a cell that carries no signal - the ceremony
		sendequiv.cpp:160-186 and fst.cpp:445 run before stepping. So the feature
		phase authors in READ-WRITE, which is what a stepping author wants.

		`[MEASURED 2026-09-17] HONEST NOTE:` the movie-hash gate did NOT redden the
		1-frame "show" steps under the old WRITE ordering - movie=88cbd30188935e26
		both sides of every show, in WRITE and in READ-WRITE alike. The clobber the
		doc warns of did not manifest on this fixture (the frames stepped into,
		9929/9930, evidently already hold neutral rows, or a tour-driven step past
		the movie frontier does not record). So this change rests on the DOCUMENTED
		contract, not on a gate we can see fail here; the gate holds shows to
		"machine moved, movie unchanged" in both modes and that is what it measured.
		gui_set_driver(1) sets macro_armed itself; the assignment documents intent.
	*/
	{
		// THE `write-clobber` ARM: restore the old ordering - enter WRITE (macro_armed
		// off), where a frame-step is DOCUMENTED to overwrite the row it advances into
		// with neutral. `[MEASURED 2026-09-17]` on this fixture the movie hash on
		// "show: slot 99 (1 frame)" does NOT move under WRITE either (the rows it steps
		// into already hold neutral, or a step past the movie frontier does not record),
		// so this arm is INCONCLUSIVE-BY-DESIGN here: the class stays, it is declared
		// with mustBreak "" and logs itself as such - it must never fake a redden.
		const bool wc = sabotaged("write-clobber");
		Step s;
		s.name = "driver: READ-WRITE (enter authoring)";
		s.act = [wc] {
			if (wc)
			{
				gui_set_driver(2); dojo.macro_armed = false;
				NOTICE_LOG(RENDERER, "SURFACE TOUR: arm write-clobber inconclusive-by-design (movie unchanged on this fixture)");
				return true;
			}
			gui_set_driver(1); dojo.macro_armed = true; return true;
		};
		s.verify = [wc] {
			if (wc)
			{
				if (session::mode() != session::Mode::Write || dojo.play_match || dojo.macro_armed) { why("mode=%s", session::label()); return false; }
				why("WRITE (the write-clobber arm)");
				return true;
			}
			if (session::mode() != session::Mode::ReadWrite || dojo.play_match || !dojo.macro_armed) { why("mode=%s", session::label()); return false; }
			return true;
		};
		add(s);
	}

	// 45-66. the features, one by one, each through its hook (Track B owns the bodies)
	auto hook = [&](const char *name, Kind kind, bool (*fn)(), bool needsPrev = false, bool optional = false, int maxWaitMs = 0) {
		Step s;
		s.name = name;
		s.kind = kind;
		s.needsPrev = needsPrev;
		s.optional = optional;
		s.maxWaitMs = maxWaitMs;
		s.act = [fn] { why(""); return fn(); };
		add(s);
	};
	hook("roll: flip a cell + undo",         Kind::Click,  hooks::rollEditFlipUndo);
	hook("states: label round-trip",         Kind::Click,  hooks::statesLabelRoundTrip);
	hook("savestate: save slot 99",          Kind::Record, hooks::saveScratchSlot);
	hook("savestate: load slot 99",          Kind::Record, hooks::loadScratchSlot, /*needsPrev*/ true);
	show("show: slot 99 (1 frame)");
	hook("slot: next",                       Kind::Click,  hooks::slotNext);
	hook("slot: prev",                       Kind::Click,  hooks::slotPrev);
	// The cycle ENDS in READ-WRITE, so every later show/capture step steps a frame
	// without clobbering a movie row (see step 44).
	// The cycle ENDS in READ-WRITE, so every later show/capture step steps a frame
	// in the authoring mode (see step 44).
	hook("driver: READ",                     Kind::Click,  hooks::driverRead);
	if (sabotaged("write-clobber"))
	{
		// the old ordering, restored: the cycle ends in WRITE
		hook("driver: READ-WRITE",           Kind::Click,  hooks::driverReadWrite);
		hook("driver: WRITE",                Kind::Click,  hooks::driverWrite);
	}
	else
	{
		hook("driver: WRITE",                Kind::Click,  hooks::driverWrite);
		hook("driver: READ-WRITE",           Kind::Click,  hooks::driverReadWrite);
	}
	hook("sender: send \"5LP _ _ 5LP\"",     Kind::Record, hooks::senderSend);
	hook("sender: stop",                     Kind::Click,  hooks::senderStop, true);
	hook("notepad: analyze",                 Kind::Click,  hooks::notepadAnalyze);
	hook("snippets: place",                  Kind::Click,  hooks::snippetsPlace);
	hook("macros: place",                    Kind::Click,  hooks::macrosPlace);
	hook("branch: create from slot 0",       Kind::Record, hooks::branchCreate);
	hook("branch: checkout",                 Kind::Record, hooks::branchCheckout, true);
	show("show: the branch (1 frame)");
	hook("branch: back to main",             Kind::Record, hooks::branchBackToMain, true);
	show("show: main again (1 frame)");
	hook("test lab: add test from slot 0",   Kind::Click,  hooks::labAddTest);
	// Trash follows add directly so needsPrev names the step it depends on.
	hook("test lab: trash the tour test",    Kind::Click,  hooks::labTrashTest, true);
	hook("captures: start",                  Kind::Record, hooks::capturesStart, false, /*optional*/ true);
	{
		// POLLED, like the FST and the export. requestStop() is one-shot inside the hook
		// and the .avi only gets its bytes on a later present, so a single verify at the
		// dwell reads an empty file (`[MEASURED 2026-09-17]` this step SKIPped). The act
		// issues the stop; the verify re-reads the file until it has size, or maxWaitMs.
		Step s;
		s.name = "captures: stop";
		s.kind = Kind::Click;
		s.needsPrev = true;
		s.optional = true;
		s.maxWaitMs = 10000;
		s.act = [] { why(""); hooks::capturesStop(); return true; };
		s.verify = [] { return hooks::capturesStop(); };
		add(s);
	}
	hook("savestate: delete slot 99",        Kind::Click,  hooks::deleteScratchSlot);

	if (st.slow)
	{
		{
			Step s;
			s.name = "FST: run the fixed sweep";
			s.kind = Kind::Record;
			s.maxWaitMs = 150000;
			s.act = [] { why(""); return hooks::fstArmSweep(); };
			s.verify = [] { return hooks::fstSweepDone(); };
			add(s);
		}
		{
			Step s;
			s.name = "branch export: main";
			s.kind = Kind::Record;
			s.optional = true;
			s.maxWaitMs = 150000;
			s.act = [] { why(""); return hooks::exportLaunch(); };
			s.verify = [] { return hooks::exportDone(); };
			add(s);
		}
	}
}

// ---- the machine -----------------------------------------------------------------

static bool ready()
{
	if (dojo.frame_number.load() < 120) { why("frame %u < 120", dojo.frame_number.load()); return false; }
	SlotView v;
	if (host() == nullptr || !host()->slotView(0, v) || !v.exists) { why("no slot 0"); return false; }
	if (rebind::keyboard() == nullptr) { why("no keyboard device"); return false; }
	if (hostfs::savestateFolderOverride.empty()) { why("no clip folder bound"); return false; }
	st.haveSlot0frame = v.haveFrame;
	st.slot0frame = v.frame;
	return true;
}

// ---- the gate's live half ------------------------------------------------------------

static u32 panelsMask()
{
	u32 m = 0, bit = 1;
	for (const panels::Panel& p : panels::all())
	{
		if (strcmp(p.id, "surfacetour") == 0 || strcmp(p.id, "game") == 0)
			continue;
		if (*p.open) m |= bit;
		bit <<= 1;
	}
	return m;
}

static u32 bindingsFold()
{
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	const std::shared_ptr<InputMapping> map = kbd != nullptr ? kbd->get_input_mapping() : nullptr;
	std::vector<u32> codes;
	for (int i = 0; i < hotkeys::count(); i++)
		codes.push_back(map != nullptr ? map->get_button_code(0, hotkeys::all()[i].id) : (u32)-1);
	return codes.empty() ? 0 : (u32)XXH32(codes.data(), codes.size() * sizeof(u32), 0);
}

//! One side of the ledger. The machine hash is read ONLY if the emulator is stopped;
//! everything else is host state and is always safe to read.
static void capture(Facts& f, bool before)
{
	const bool stopped = oracle::machineStopped();
	const u32 mh = stopped ? oracle::machineHash() : 0;
	const u64 vh = oracle::movieHash();
	const u32 pm = panelsMask(), bf = bindingsFold();
	const int sl = (int)config::SavestateSlot, md = (int)session::mode();
	const u32 fr = dojo.frame_number.load();
	if (before)
	{
		f.beforeOk = stopped; f.machineBefore = mh; f.movieBefore = vh; f.panelsBefore = pm;
		f.bindingsBefore = bf; f.slotBefore = sl; f.modeBefore = md; f.frameBefore = fr;
	}
	else
	{
		f.afterOk = stopped; f.machineAfter = mh; f.movieAfter = vh; f.panelsAfter = pm;
		f.bindingsAfter = bf; f.slotAfter = sl; f.modeAfter = md; f.frameAfter = fr;
	}
}

/*
	G3/G4/G7 for one settled step: the tuple verdict, then the per-mover NON-VACUITY
	FLOOR THAT CAN FIRE UNDER THE MODE TESTED (nbneo0909's rule - a floor that cannot
	fire is worse than none): a show must land exactly +1 frame; a load must leave the
	machine Paused (its verify already says so) and, for slot 99, on the very hash
	recorded when it was saved (the IDENTITY claim).
*/
static void gateStep(Rec& r, int n)
{
	Facts& f = r.facts;
	const Expect e = expectOf(r.step.name);
	std::string why;
	int g = gateVerdict(f, e, why);
	if (g == 0)
	{
		if (startsWith(r.step.name, "show:") && f.frameAfter != f.frameBefore + 1)
		{
			g = 1;
			char b[96];
			snprintf(b, sizeof(b), "vacuous: a show must step exactly one frame (%u -> %u)", f.frameBefore, f.frameAfter);
			why = b;
		}
		else if (startsWith(r.step.name, "savestate: load slot 99") && st.haveHash99 && f.machineAfter != st.hash99)
		{
			g = 1;
			char b[96];
			snprintf(b, sizeof(b), "identity: loaded %08x but slot 99 was saved at %08x", f.machineAfter, st.hash99);
			why = b;
		}
	}
	if (startsWith(r.step.name, "savestate: save slot 99") && f.afterOk)
	{
		st.hash99 = f.machineAfter;
		st.haveHash99 = true;
	}
	f.gate = g;
	f.gateWhy = why;
	NOTICE_LOG(RENDERER, "SURFACE TOUR: gate %d/%d \"%s\" machine=%08x->%08x movie=%016llx->%016llx expect=%s -> %s (%s)",
			n, (int)st.steps.size(), r.step.name, f.machineBefore, f.machineAfter,
			(unsigned long long)f.movieBefore, (unsigned long long)f.movieAfter, e.label, gateText(g), why.c_str());
}

/*
	THE SUMMARY - nbneo-rr's flow_gates() (capture.cpp:30273-30404), in the grammar
	lifted from tests/transport-target-check.py: two spaces, `ok  G<n>  <claim with
	the measured count inlined>`, or `FAIL G<n>  <measured> <why>:` and one indented
	offender per line. In their order: the guard fired (G1), every step completed
	(G2), no mover was a no-op (G3), no UI step leaked (G4), movers landed on DISTINCT
	states unless they declared convergence (G5), a declared convergence actually
	converged (G5b), and enough movers were measured for G5 to have compared anything
	(G6). Returns the number of red gates.
*/
static int gateSummary()
{
	const int n = (int)st.steps.size();
	int settled = 0, measured = 0, movers = 0, red = 0;
	std::vector<int> offenders;
	for (int i = 0; i < n; i++)
	{
		const Rec& r = st.steps[i];
		if (r.verdict == 1 || r.verdict == 0) settled++;
		if (r.verdict == 2) continue;
		if (r.facts.gate >= 0 && r.facts.gate != 3) measured++;
		if (expectOf(r.step.name).machine == MExp::Moved && r.facts.gate == 0) movers++;
	}
	// G1: the guard fired. bind-guard's inversion - assert the counter reached the
	// count FIRST, because a gate that judged nothing reports zero findings too.
	int judged = 0;
	for (int i = 0; i < n; i++) if (st.steps[i].verdict != 2 && st.steps[i].facts.gate >= 0) judged++;
	if (judged == settled) NOTICE_LOG(RENDERER, "  ok  G1  the gate judged all %d settled step(s)", settled);
	else { red++; NOTICE_LOG(RENDERER, "  FAIL G1  %d/%d settled step(s) were judged by the gate - the gate itself is decorative", judged, settled); }
	// G2: every step completed (SKIPs are dependencies, not completions)
	if (settled + st.skipped == n) NOTICE_LOG(RENDERER, "  ok  G2  all %d step(s) completed (%d settled, %d skipped)", n, settled, st.skipped);
	else { red++; NOTICE_LOG(RENDERER, "  FAIL G2  %d of %d step(s) completed", settled + st.skipped, n); }
	// G3: no-op movers
	offenders.clear();
	for (int i = 0; i < n; i++) if (st.steps[i].facts.gate == 1) offenders.push_back(i);
	if (offenders.empty()) NOTICE_LOG(RENDERER, "  ok  G3  no mover changed nothing (%d mover(s) moved)", movers);
	else
	{
		red++;
		NOTICE_LOG(RENDERER, "  FAIL G3  %d/%d mover(s) pressed a verb and changed NOTHING - a no-op is a verb that did not fire:", (int)offenders.size(), movers + (int)offenders.size());
		for (int i : offenders) NOTICE_LOG(RENDERER, "           %s   [%s]", st.steps[i].step.name, st.steps[i].facts.gateWhy.c_str());
	}
	// G4: leaks
	offenders.clear();
	for (int i = 0; i < n; i++) if (st.steps[i].facts.gate == 2) offenders.push_back(i);
	if (offenders.empty()) NOTICE_LOG(RENDERER, "  ok  G4  no UI step leaked into the machine or the movie (%d judged)", judged);
	else
	{
		red++;
		NOTICE_LOG(RENDERER, "  FAIL G4  %d UI step(s) moved the machine or the movie - a structural verb that grew transport behaviour:", (int)offenders.size());
		for (int i : offenders) NOTICE_LOG(RENDERER, "           %s   [%s]", st.steps[i].step.name, st.steps[i].facts.gateWhy.c_str());
	}
	// G5: movers land on DISTINCT states, unless they declared convergence - and a
	// declared convergence is an ASSERTION: it must pair with another mover.
	// A pairing candidate is a mover OR a step that declared convergence (an IDENTITY
	// step like "load slot 99" moves nothing by construction and still has to land
	// where another mover did - `[MEASURED 2026-09-17]` walking movers only left it
	// unpaired and G5b red against its own partner, aa983314).
	auto candidate = [&](int k) {
		const Rec& r = st.steps[k];
		const Expect e = expectOf(r.step.name);
		return (e.machine == MExp::Moved || e.converge) && r.verdict != 2 && r.facts.gate == 0;
	};
	std::vector<bool> paired(n, false);
	int distinctRed = 0;
	for (int i = 0; i < n; i++)
	{
		const Rec& a = st.steps[i];
		const Expect ea = expectOf(a.step.name);
		if (!candidate(i)) continue;
		for (int j = i + 1; j < n; j++)
		{
			const Rec& b = st.steps[j];
			const Expect eb = expectOf(b.step.name);
			if (!candidate(j)) continue;
			if (a.facts.machineAfter != b.facts.machineAfter) continue;
			if (ea.converge || eb.converge) { paired[i] = paired[j] = true; continue; }
			if (distinctRed == 0) red++;
			distinctRed++;
			NOTICE_LOG(RENDERER, "  FAIL G5  steps \"%s\" and \"%s\" both declare that they move the machine and both left it at %08x - one moved nothing, or one replayed the other",
					a.step.name, b.step.name, a.facts.machineAfter);
		}
	}
	if (distinctRed == 0) NOTICE_LOG(RENDERER, "  ok  G5  every non-converging mover landed on a distinct machine (%d mover(s))", movers);
	int loneRed = 0;
	for (int i = 0; i < n; i++)
	{
		const Rec& a = st.steps[i];
		if (!expectOf(a.step.name).converge || a.verdict == 2 || a.facts.gate != 0 || paired[i]) continue;
		if (loneRed == 0) red++;
		loneRed++;
		NOTICE_LOG(RENDERER, "  FAIL G5b step \"%s\" declares it must land where another mover landed, and it left the machine at %08x, which no other mover reached",
				a.step.name, a.facts.machineAfter);
	}
	if (loneRed == 0) NOTICE_LOG(RENDERER, "  ok  G5b every declared convergence paired with another mover");
	// G6: the mover floor
	if (movers >= 2) NOTICE_LOG(RENDERER, "  ok  G6  %d mover(s) measured, so G5 compared something", movers);
	else { red++; NOTICE_LOG(RENDERER, "  FAIL G6  only %d mover(s) measured, so the distinctness gate compares nothing and passes by saying nothing", movers); }
	// G7: the unmeasured count is stated, never hidden
	if (st.unmeasured == 0) NOTICE_LOG(RENDERER, "  ok  G7  every judged step was measured on a stopped machine (%d)", measured);
	else NOTICE_LOG(RENDERER, "  ok  G7  %d step(s) unmeasured (the machine was running at a capture) - stated, not counted", st.unmeasured);
	return red;
}

static void finish(const char *mode)
{
	const int red = gateSummary();
	// APPEND-ONLY: the fields before mode= are what every existing extractor reads.
	NOTICE_LOG(RENDERER, "SURFACE TOUR RESULT: passed=%d failed=%d skipped=%d total=%d mode=%s gate_ok=%d vacuous=%d leak=%d unmeasured=%d gates_red=%d",
			st.passed, st.failed, st.skipped, (int)st.steps.size(), mode, st.gateOk, st.vacuous, st.leak, st.unmeasured, red);
	st.phase = Phase::Done;
}

static void abort(const char *reason)
{
	NOTICE_LOG(RENDERER, "SURFACE TOUR: aborted - %s", reason);
	restore();
	finish(g_sabotage.empty() ? "yes" : "sabotage");
}

static bool prevPassed()
{
	return st.cur > 0 && st.steps[st.cur - 1].verdict == 1;
}

static void settle(bool ok)
{
	Rec& r = st.steps[st.cur];
	r.verdict = judge(ok, r.step.optional, r.step.needsPrev, prevPassed());
	r.why = (r.step.needsPrev && !prevPassed()) ? "previous step did not pass" : lastWhy();
	// THE GATE, on every settled step: the ledger's right side, then the tuple verdict.
	// A finding turns a PASS into a FAIL by name; a SKIP is left alone (nothing ran).
	if (r.verdict != 2)
	{
		capture(r.facts, /*before*/ false);
		gateStep(r, st.cur + 1);
		const int over = applyGate(r.verdict, r.facts.gate);
		if (over != r.verdict)
		{
			r.verdict = over;
			r.why = r.facts.gateWhy;
		}
		if (r.facts.gate == 0) st.gateOk++; else if (r.facts.gate == 1) st.vacuous++; else if (r.facts.gate == 2) st.leak++; else st.unmeasured++;
	}
	if (r.verdict == 1) st.passed++; else if (r.verdict == 0) st.failed++; else st.skipped++;
	NOTICE_LOG(RENDERER, "SURFACE TOUR: step %d/%d \"%s\" -> %s (%s)",
			st.cur + 1, (int)st.steps.size(), r.step.name, verdictText(r.verdict), r.why.c_str());
	st.cur++;
	st.focusRetries = 0;
	st.t0 = os_GetSeconds();
	st.phase = st.cur >= (int)st.steps.size() ? Phase::Restore : Phase::Begin;
}

static void init()
{
	st.inited = true;
	const std::string mode = cfgLoadStr("dojo", "SurfaceTour", "");
	st.enabled = !(mode.empty() || mode == "no");
	// v2: sabotage classes. The runner's own "open" arm keeps its bool; every other
	// arm asks sabotaged("<class>") where it lives (a hook in its feature's TU).
	g_sabotage = parseSabotage(mode);
	st.sabotage = sabotaged("open");
	// The runner is the source of truth for the arm matrix: say what it knows
	// (always - the harness's --list-sabotage reads a dry boot), then declare each
	// ARMED class's target and control by exact step name for the judge.
	{
		std::string known;
		for (int i = 0; i < NARMS; i++) known += (known.empty() ? "" : "+") + std::string(ARMS[i].cls);
		NOTICE_LOG(RENDERER, "SURFACE TOUR: arms known: %s", known.c_str());
	}
	if (!g_sabotage.empty())
	{
		std::string all;
		for (const std::string& c : g_sabotage) all += (all.empty() ? "" : "+") + c;
		NOTICE_LOG(RENDERER, "SURFACE TOUR: sabotage armed: %s", all.c_str());
		for (const std::string& c : g_sabotage)
		{
			const ArmDecl *d = armDeclOf(c.c_str());
			if (d == nullptr)
				NOTICE_LOG(RENDERER, "SURFACE TOUR: arm %s UNKNOWN (not in the matrix)", c.c_str());
			else
				NOTICE_LOG(RENDERER, "SURFACE TOUR: arm %s must_break=\"%s\" must_not_break=\"%s\"", d->cls, d->mustBreak, d->mustNotBreak);
		}
	}
	st.slow = cfgLoadBool("dojo", "TourSlow", false);
	st.bpmMs = cfgLoadInt("dojo", "TourBpmMs", 1000);
	st.recordMs = cfgLoadInt("dojo", "TourRecordMs", 2000);
	const int arm = cfgLoadInt("dojo", "TourArmMs", 300);
	st.armMs = clampArmMs(arm);
	if (st.armMs != arm)
		NOTICE_LOG(RENDERER, "SURFACE TOUR: TourArmMs %d clamped to %d (the rebind engine ignores a press for 200 ms after arming)", arm, st.armMs);
	if (st.enabled)
	{
		st.phase = Phase::WaitReady;
		st.readyStart = os_GetSeconds();
	}
}

void tick()
{
	hooks::probeTick();		// dojo:TourHook=<name> - the unit drive stays live
	if (!st.inited)
		init();
	if (!st.enabled || st.phase == Phase::Idle || st.phase == Phase::Done)
		return;
	const double now = os_GetSeconds();

	// A rebind step needs the engine pumped; it is otherwise pumped only by the
	// Hotkeys panel. Idempotent when nothing is armed.
	rebind::tick();

	switch (st.phase)
	{
	case Phase::WaitReady:
		if (ready())
		{
			st.phase = Phase::Setup;
			return;
		}
		if (now - st.readyStart > 180.0)
			abort(lastWhy());
		return;

	case Phase::Setup:
	{
		snapshot();
		panels::open("surfacetour");
		gui_pause_for_checkout();
		int closed = 0;
		for (const auto& kv : st.panelsOpen)
		{
			const panels::Panel *p = panels::find(kv.first.c_str());
			if (p != nullptr && *p->open) { *p->open = false; closed++; }
		}
		buildSteps();
		// The machine at Setup, stated for the record. `[MEASURED 2026-09-17]` it is NOT
		// slot 0: AutoSeekState's load lands after ready() fires (ready read frame=121,
		// slot 0 is 9928), so "load slot 0" is a real MOVER from here, not idempotent.
		st.haveSetupHash = oracle::machineStopped();
		st.setupHash = st.haveSetupHash ? oracle::machineHash() : 0;
		NOTICE_LOG(RENDERER, "SURFACE TOUR: ready frame=%u slot0frame=%u kbd=[%s] steps=%d mode=%s (closed %d) setup machine=%08x movie=%016llx",
				dojo.frame_number.load(), st.slot0frame, rebind::keyboard()->name().c_str(),
				(int)st.steps.size(), g_sabotage.empty() ? "yes" : "sabotage", closed, st.setupHash,
				(unsigned long long)oracle::movieHash());
		st.cur = 0;
		st.t0 = now;
		st.phase = Phase::Begin;
		return;
	}

	case Phase::Begin:
	{
		Rec& r = st.steps[st.cur];
		why("");
		if (r.step.needsPrev && !prevPassed())
		{
			settle(false);
			return;
		}
		capture(r.facts, /*before*/ true);		// the ledger's left side, before begin()
		if (r.step.begin && !r.step.begin())
		{
			settle(false);
			return;
		}
		st.t0 = now;
		st.phase = Phase::Act;
		return;
	}

	case Phase::Act:
	{
		// A per-step override exists for exactly one arm (`rebind`: 0 ms, inside the
		// engine's deaf window); every other step waits the clamped TourArmMs.
		const int armMs = st.steps[st.cur].armMsOverride >= 0 ? st.steps[st.cur].armMsOverride : st.armMs;
		if ((now - st.t0) * 1000.0 < armMs)
			return;
		// A focused text field makes gui_hotkey_allowed() refuse every key. Ask the
		// banner body to clear focus and try again next frame, a few times.
		if (gui_keyboard_captured() && st.focusRetries < 3)
		{
			st.focusClear = true;
			st.focusRetries++;
			st.t0 = now;
			return;
		}
		Rec& r = st.steps[st.cur];
		st.actOk = r.step.act ? r.step.act() : true;
		if (!st.actOk)
		{
			settle(false);
			return;
		}
		st.tAct = now;
		st.phase = Phase::Dwell;
		return;
	}

	case Phase::Dwell:
	{
		const Rec& r = st.steps[st.cur];
		if ((now - st.tAct) * 1000.0 < dwellMs(r.step.kind, st.bpmMs, st.recordMs))
			return;
		st.tVerify = now;
		st.phase = Phase::Verify;
		return;
	}

	case Phase::Verify:
	{
		const Rec& r = st.steps[st.cur];
		// A polled verify's reason is THIS poll's, not the last failed one's: a hook only
		// sets why() on its false paths, so without this a PASS on the third poll printed
		// the first poll's "bytes=0" (`[MEASURED 2026-09-17]`). Act-only steps keep the
		// reason their act set, which is the verdict's.
		if (r.step.verify)
			why("");
		const bool ok = r.step.verify ? r.step.verify() : st.actOk;
		if (!ok && r.step.maxWaitMs > 0 && (now - st.tVerify) * 1000.0 < r.step.maxWaitMs)
			return;		// keep polling
		settle(ok);
		return;
	}

	case Phase::Restore:
		restore();
		finish(g_sabotage.empty() ? "yes" : "sabotage");
		return;

	default:
		return;
	}
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "SURFACE TOUR SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	why("x=%d", 7);
	claim("why() formats into lastWhy()", std::string(lastWhy()) == "x=7");
	claim("a click dwells the bpm", dwellMs(Kind::Click, 1000, 2000) == 1000);
	claim("a record dwells the record time", dwellMs(Kind::Record, 1000, 2000) == 2000);
	claim("the arm delay never drops under the engine's 200 ms", clampArmMs(150) == 200 && clampArmMs(300) == 300);
	claim("the sabotage is the same key with Shift held", sabotageCode(CTRL | 58) == (CTRL | 58 | InputMapping::KEY_MOD_SHIFT));
	claim("a passing step is PASS", judge(true, false, false, true) == 1);
	claim("a failing step is FAIL", judge(false, false, false, true) == 0);
	claim("an optional step that fails is SKIP, never FAIL", judge(false, true, false, true) == 2);
	claim("needsPrev after a non-pass is SKIP even when it would pass", judge(true, false, true, false) == 2);
	claim("needsPrev after a pass judges normally", judge(true, false, true, true) == 1 && judge(false, false, true, true) == 0);
	claim("every key in the table is a chord (no bare F-key can collide with the game)",
			[] { for (int i = 0; i < NKEYS; i++) if ((KEYS[i].code & InputMapping::KEY_MOD_MASK) == 0) return false; return true; }());
	claim("every key in the table maps to a registered toggle action",
			[] { for (int i = 0; i < NKEYS; i++) if (hotkeys::panelFor(KEYS[i].action) == nullptr || strcmp(hotkeys::panelFor(KEYS[i].action), KEYS[i].panel) != 0) return false; return true; }());

	// ---- the gate's pure half ----------------------------------------------------
	{
		Facts f;
		f.beforeOk = f.afterOk = true;
		f.machineBefore = f.machineAfter = 0x1111; f.movieBefore = f.movieAfter = 0x22;
		std::string w;
		claim("a UI step that moved nothing is ok", gateVerdict(f, expectOf("open: pianoroll"), w) == 0);
		claim("a mover that moved nothing is VACUOUS", gateVerdict(f, expectOf("show: the branch (1 frame)"), w) == 1 && w.rfind("vacuous", 0) == 0);
		claim("a movie-mover that left the movie alone is VACUOUS", gateVerdict(f, expectOf("snippets: place"), w) == 1);
		f.machineAfter = 0x1112;
		claim("a UI step that moved the machine is a LEAK", gateVerdict(f, expectOf("rebind: states -> Ctrl+F3"), w) == 2 && w.rfind("leak", 0) == 0);
		claim("a mover that moved the machine is ok", gateVerdict(f, expectOf("show: the branch (1 frame)"), w) == 0);
		// Vacuity is judged BEFORE leak: a show whose machine did not move is vacuous
		// whatever the movie did. The clobber claim therefore moves the machine too.
		f.machineAfter = 0x1112; f.movieAfter = 0x23;
		claim("a show that moved the MOVIE is a LEAK (the WRITE-clobber, caught)", gateVerdict(f, expectOf("show: slot 99 (1 frame)"), w) == 2);
		claim("a branch checkout does not assert the movie either way", gateVerdict(f, expectOf("branch: back to main"), w) == 0);	// machine moved, movie Any
		f.machineAfter = 0x1111;
		claim("...but is vacuous when the machine did not move", gateVerdict(f, expectOf("branch: back to main"), w) == 1);
		f.beforeOk = false;
		claim("a capture on a running machine is unmeasured, never a finding", gateVerdict(f, expectOf("show: x"), w) == 3);
		claim("the tuple sees a window flip the hash cannot", [] { Facts g; g.panelsBefore = 1; g.panelsAfter = 3; return tupleChanged(g); }());
		claim("a gate finding turns PASS into FAIL", applyGate(1, 1) == 0 && applyGate(1, 2) == 0);
		claim("...never rescues a FAIL and never touches a SKIP", applyGate(0, 0) == 0 && applyGate(2, 1) == 2 && applyGate(2, 2) == 2);
		claim("an ok gate leaves a PASS alone", applyGate(1, 0) == 1 && applyGate(1, 3) == 1);
		claim("loads, shows and checkouts declare convergence; slot 99's show and the captures do not",
				expectOf("load slot 0 (David's base)").converge && expectOf("show: David's base state (1 frame)").converge
				&& expectOf("branch: checkout").converge && !expectOf("show: slot 99 (1 frame)").converge && !expectOf("captures: start").converge);
		claim("a sender step is a UI step today (nothing reaches the machine without a step)",
				expectOf("sender: send \"5LP _ _ 5LP\"").machine == MExp::Unchanged);
	}

	// ---- sabotage classes: the grammar and the declaration table ----------------------
	{
		auto is = [](const std::vector<std::string>& v, std::initializer_list<const char *> want) {
			if (v.size() != want.size()) return false;
			size_t i = 0;
			for (const char *w : want) if (v[i++] != w) return false;
			return true;
		};
		claim("bare 'sabotage' is the open arm", is(parseSabotage("sabotage"), { "open" }));
		claim("'sabotage:open+flip' arms both, in order", is(parseSabotage("sabotage:open+flip"), { "open", "flip" }));
		claim("'yes' arms nothing", parseSabotage("yes").empty());
		claim("'sabotage:' with no class arms nothing", parseSabotage("sabotage:").empty());
		claim("a trailing or doubled '+' is tolerated", is(parseSabotage("sabotage:open++flip+"), { "open", "flip" }));
		claim("every declared class names a control that must stay green",
				[] { for (int i = 0; i < NARMS; i++) if (ARMS[i].mustNotBreak[0] == '\0') return false; return true; }());
		claim("gate-can-pass and write-clobber break nothing (an empty target)",
				armDeclOf("gate-can-pass") != nullptr && armDeclOf("gate-can-pass")->mustBreak[0] == '\0'
				&& armDeclOf("write-clobber") != nullptr && armDeclOf("write-clobber")->mustBreak[0] == '\0');
		claim("every other class names the one step it must redden",
				[] { for (int i = 0; i < NARMS; i++) { const std::string c = ARMS[i].cls; if (c != "gate-can-pass" && c != "write-clobber" && ARMS[i].mustBreak[0] == '\0') return false; } return true; }());
		claim("an unknown class resolves to nothing, never to a neighbour", armDeclOf("opens") == nullptr && armDeclOf("") == nullptr);
		claim("the nine classes are all declared",
				NARMS == 9 && armDeclOf("open") && armDeclOf("rebind") && armDeclOf("show") && armDeclOf("flip")
				&& armDeclOf("label") && armDeclOf("save") && armDeclOf("branch"));
	}
	NOTICE_LOG(RENDERER, "SURFACE TOUR SELFTEST: %d passed, %d failed", pass, fail);
}

// ---- the narration panel --------------------------------------------------------

static bool tourOpen = false;

static void draw()
{
	if (st.focusClear)
	{
		ImGui::SetWindowFocus(nullptr);
		st.focusClear = false;
	}

	// The window body: a verdict table, the dockable fallback.
	const int n = total();
	if (!st.enabled)
	{
		tasTextDisabled("Surface Tour is off. Launch with -config dojo:SurfaceTour=yes");
		return;
	}
	if (st.phase == Phase::WaitReady)
	{
		tasTextColored(TAS_DIM, "waiting for the machine: %s", lastWhy());
		return;
	}
	tasTextColored(TAS_ACCENT, "passed %d  failed %d  skipped %d  of %d", st.passed, st.failed, st.skipped, n);
	const int c = current();
	const int from = std::max(0, (c >= 0 ? c : n) - 6);
	for (int i = from; i < n && i < from + 8; i++)
	{
		const int v = st.steps[i].verdict;
		const ImVec4 col = v == 1 ? TAS_READ : v == 0 ? TAS_WRITE : v == 2 ? TAS_DIM : TAS_ACCENT;
		tasTextColored(col, "%s %d/%d %s%s%s", verdictText(v), i + 1, n, st.steps[i].step.name,
				st.steps[i].why.empty() ? "" : "  - ", st.steps[i].why.c_str());
	}

	// The pinned banner: what a human reads from across the room. Drawn on the
	// foreground list so it sits above every window, in BOTH streams (this panel
	// is Both), and never through the toast, which the Paused stream eats.
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImDrawList *dl = ImGui::GetForegroundDrawList(vp);
	ImFont *font = ImGui::GetFont();
	const float big = 34.f, small = 20.f;
	std::string l1, l2;
	ImU32 c2 = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f));
	if (st.phase == Phase::Done)
	{
		l1 = "TOUR DONE passed=" + std::to_string(st.passed) + " failed=" + std::to_string(st.failed)
				+ " skipped=" + std::to_string(st.skipped);
		l2 = st.failed == 0 ? "every scored step passed" : "see the verdict table";
		c2 = ImGui::GetColorU32(st.failed == 0 ? TAS_READ : TAS_WRITE);
	}
	else if (c >= 0)
	{
		l1 = "TOUR " + std::to_string(c + 1) + "/" + std::to_string(n) + " - " + st.steps[c].step.name;
		if (c > 0)
		{
			const Rec& p = st.steps[c - 1];
			l2 = std::string("last: ") + verdictText(p.verdict) + "  " + p.step.name + (p.why.empty() ? "" : "  (" + p.why + ")");
			c2 = ImGui::GetColorU32(p.verdict == 1 ? TAS_READ : p.verdict == 0 ? TAS_WRITE : TAS_DIM);
		}
		else
			l2 = "hands off the keyboard - the tour presses its own keys";
	}
	else
		return;
	const ImVec2 s1 = font->CalcTextSizeA(big, FLT_MAX, 0.f, l1.c_str());
	const ImVec2 s2 = font->CalcTextSizeA(small, FLT_MAX, 0.f, l2.c_str());
	const float pad = 18.f, barH = 8.f;
	const float w = std::max(s1.x, s2.x) + pad * 2, h = pad + s1.y + 6.f + s2.y + 10.f + barH + pad * 0.6f;
	const float x0 = vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, y0 = vp->WorkPos.y + 12.f;
	dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.82f)), 10.f);
	dl->AddText(font, big, ImVec2(x0 + pad, y0 + pad), ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), l1.c_str());
	dl->AddText(font, small, ImVec2(x0 + pad, y0 + pad + s1.y + 6.f), c2, l2.c_str());
	const float frac = n > 0 ? (float)(st.passed + st.failed + st.skipped) / (float)n : 0.f;
	const float by = y0 + h - pad * 0.6f - barH;
	dl->AddRectFilled(ImVec2(x0 + pad, by), ImVec2(x0 + w - pad, by + barH), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.15f)), 4.f);
	dl->AddRectFilled(ImVec2(x0 + pad, by), ImVec2(x0 + pad + (w - pad * 2) * frac, by + barH), ImGui::GetColorU32(TAS_READ), 4.f);
}

}	// namespace surfacetour

void surfacetour::registerSurfaceTourPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "surfacetour", "Surface Tour", &surfacetour::tourOpen, surfacetour::draw, panels::Both,
			/*persist*/ false, /*defW*/ 520.f, /*defH*/ 220.f });
	NOTICE_LOG(RENDERER, "SURFACE TOUR PANEL: registered=%s open=%s",
			panels::find("surfacetour") != nullptr ? "yes" : "NO", surfacetour::tourOpen ? "yes" : "no");
}

}	// namespace roll
