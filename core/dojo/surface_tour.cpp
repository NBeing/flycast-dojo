#include "surface_tour.h"
#include "hotkey_bind.h"
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
#include <deque>
#include <string>
#include <vector>

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

struct Rec
{
	Step step;
	int verdict = -1;
	std::string why;
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

	// 2-15. rebind every window's key through the real engine
	for (int i = 0; i < NKEYS; i++)
	{
		const PanelKey& k = KEYS[i];
		Step s;
		s.name = keep(std::string("rebind: ") + k.panel + " -> " + k.chord);
		s.begin = [i] {
			const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
			if (kbd == nullptr) { why("no keyboard"); return false; }
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
			why("%s", rebind::bindingName(kbd, KEYS[i].action).c_str());
			return true;
		};
		add(s);
	}

	// 16-43. open then close every window WITH its new key
	for (int i = 0; i < NKEYS; i++)
	{
		const PanelKey& k = KEYS[i];
		{
			Step s;
			s.name = keep(std::string("open: ") + k.panel);
			s.act = [i] {
				// The sabotage: the first open presses a chord nothing is bound to.
				const bool sab = st.sabotage && i == 0;
				return injectKey(sab ? sabotageCode(KEYS[i].code) : KEYS[i].code);
			};
			s.verify = [i] {
				const panels::Panel *p = panels::find(KEYS[i].panel);
				if (p == nullptr) { why("no such panel"); return false; }
				if (!*p->open) { why("open=false captured=%s", gui_keyboard_captured() ? "yes" : "no"); return false; }
				return true;
			};
			add(s);
		}
		{
			Step s;
			s.name = keep(std::string("close: ") + k.panel);
			s.needsPrev = true;
			s.act = [i] { return injectKey(KEYS[i].code); };
			s.verify = [i] {
				const panels::Panel *p = panels::find(KEYS[i].panel);
				if (p == nullptr) { why("no such panel"); return false; }
				if (*p->open) { why("open=true captured=%s", gui_keyboard_captured() ? "yes" : "no"); return false; }
				return true;
			};
			add(s);
		}
	}

	// 44. enter authoring - every feature step below runs in WRITE
	{
		Step s;
		s.name = "driver: WRITE (enter authoring)";
		s.act = [] { gui_set_driver(2); return true; };
		s.verify = [] {
			if (session::mode() != session::Mode::Write || dojo.play_match) { why("mode=%s", session::label()); return false; }
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
	hook("slot: next",                       Kind::Click,  hooks::slotNext);
	hook("slot: prev",                       Kind::Click,  hooks::slotPrev);
	hook("driver: READ",                     Kind::Click,  hooks::driverRead);
	hook("driver: READ-WRITE",               Kind::Click,  hooks::driverReadWrite);
	hook("driver: WRITE",                    Kind::Click,  hooks::driverWrite);
	hook("sender: send \"5LP _ _ 5LP\"",     Kind::Record, hooks::senderSend);
	hook("sender: stop",                     Kind::Click,  hooks::senderStop, true);
	hook("notepad: analyze",                 Kind::Click,  hooks::notepadAnalyze);
	hook("snippets: place",                  Kind::Click,  hooks::snippetsPlace);
	hook("macros: place",                    Kind::Click,  hooks::macrosPlace);
	hook("branch: create from slot 0",       Kind::Record, hooks::branchCreate);
	hook("branch: checkout",                 Kind::Record, hooks::branchCheckout, true);
	hook("branch: back to main",             Kind::Record, hooks::branchBackToMain, true);
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

static void finish(const char *mode)
{
	NOTICE_LOG(RENDERER, "SURFACE TOUR RESULT: passed=%d failed=%d skipped=%d total=%d mode=%s",
			st.passed, st.failed, st.skipped, (int)st.steps.size(), mode);
	st.phase = Phase::Done;
}

static void abort(const char *reason)
{
	NOTICE_LOG(RENDERER, "SURFACE TOUR: aborted - %s", reason);
	restore();
	finish(st.sabotage ? "sabotage" : "yes");
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
	st.sabotage = (mode == "sabotage");
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
		NOTICE_LOG(RENDERER, "SURFACE TOUR: ready frame=%u slot0frame=%u kbd=[%s] steps=%d mode=%s (closed %d)",
				dojo.frame_number.load(), st.slot0frame, rebind::keyboard()->name().c_str(),
				(int)st.steps.size(), st.sabotage ? "sabotage" : "yes", closed);
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
		if ((now - st.t0) * 1000.0 < st.armMs)
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
		finish(st.sabotage ? "sabotage" : "yes");
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
