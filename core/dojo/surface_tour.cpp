#include "surface_tour.h"
#include "hotkey_bind.h"
#include "ui_text.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "input/gamepad_device.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <cstdarg>
#include <cstdio>
#include <string>

/*
	SCAFFOLD. The runner proper (the step machine, snapshot/restore, the 14 panel
	hotkeys, the sabotage arm) is Track A's; this file exists so the contract links,
	the panel registers, injectKey() and why() are real, and the hook unit-drive
	(dojo:TourHook) runs end to end before the runner does. See surface_tour.h.
*/
namespace roll {
namespace surfacetour {

static char g_why[256] = "";

void why(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_why, sizeof(g_why), fmt, ap);
	va_end(ap);
}

const char *lastWhy() { return g_why; }

bool injectKey(u32 code)
{
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	if (kbd == nullptr)
	{
		why("no keyboard device");
		return false;
	}
	// The SAME full code on press and release. A direct call bypasses the keyboard's
	// chordCode() bookkeeping, so nothing else will pair the release for us.
	kbd->gamepad_btn_input(code, true);
	kbd->gamepad_btn_input(code, false);
	return true;
}

bool focusClearRequested() { return false; }
int  current()             { return -1; }
int  total()               { return 0; }
const char *stepName(int)  { return ""; }
int  verdict(int)          { return -1; }

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
	NOTICE_LOG(RENDERER, "SURFACE TOUR SELFTEST: %d passed, %d failed", pass, fail);
}

void tick()
{
	hooks::probeTick();		// dojo:TourHook=<name> - the unit drive, live before the runner is
	static bool reported = false;
	if (reported)
		return;
	const std::string mode = cfgLoadStr("dojo", "SurfaceTour", "");
	if (mode.empty() || mode == "no")
		return;
	reported = true;
	NOTICE_LOG(RENDERER, "SURFACE TOUR RESULT: passed=0 failed=0 skipped=0 total=0 mode=stub");
}

static bool tourOpen = false;

static void draw()
{
	tasTextColored(TAS_DIM, "Surface Tour - scaffold (runner not wired yet)");
	tasTextDisabled("%s", lastWhy());
}

}	// namespace surfacetour

void surfacetour::registerSurfaceTourPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "surfacetour", "Surface Tour", &surfacetour::tourOpen, surfacetour::draw, panels::Both,
			/*persist*/ false, /*defW*/ 420.f, /*defH*/ 160.f });
	NOTICE_LOG(RENDERER, "SURFACE TOUR PANEL: registered=%s open=%s",
			panels::find("surfacetour") != nullptr ? "yes" : "NO", surfacetour::tourOpen ? "yes" : "no");
}

}	// namespace roll
