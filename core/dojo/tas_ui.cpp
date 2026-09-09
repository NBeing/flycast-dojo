#include "tas_ui.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace tas_ui
{

/*
	THE STATE, in one place, private to this file.

	It is still static - the alternative is threading a context object through
	every panel's draw, which buys nothing here because there is exactly one
	user and one UI thread. What changed from the fork is that it is no longer
	REACHABLE from every panel: they go through the functions below, so
	"who last selected a window" has one writer instead of fourteen.
*/
static const char *selectedId = nullptr;	//!< owns the menu bar; nullptr = global
static const char *lastSelectedId = nullptr;	//!< for deselect()'s toggle-back
static const char *focusNow = nullptr;		//!< focused THIS frame
static const char *focusPrev = nullptr;		//!< focused last frame

//! Ids are literals owned by the registry, so pointer equality would ALMOST
//! work - and would fail silently the day one is built at runtime. Compare the
//! strings; there are a dozen panels and this runs once per panel per frame.
static bool sameId(const char *a, const char *b)
{
	if (a == b) return true;
	if (a == nullptr || b == nullptr) return false;
	return std::strcmp(a, b) == 0;
}

const char *selected() { return selectedId; }

void select(const char *panelId)
{
	// NO lastSelected BOOKKEEPING HERE, deliberately. deselect() records the
	// panel it is leaving, which is what "last selected BEFORE deselect" means,
	// so recording it on every select as well is dead - and if it were ever read
	// first it would toggle back to the panel BEFORE last rather than the one
	// you were on.
	//
	// `[MEASURED 2026-09-08]` found by sabotage. Deleting the line left the
	// self-test at 10/10, so it was exercised and nothing depended on it. A
	// sabotage that cannot fail names dead code as surely as a test that cannot
	// fail names a missing one.
	if (sameId(selectedId, panelId))
		return;
	selectedId = panelId;
}

void deselect()
{
	// Toggle: the first call drops to the global bar remembering where we were,
	// the second puts it back. That is the [MAIN] chip's whole behaviour.
	if (selectedId != nullptr)
	{
		lastSelectedId = selectedId;
		selectedId = nullptr;
	}
	else
	{
		selectedId = lastSelectedId;
	}
}

void noteFocused(const char *panelId) { focusNow = panelId; }
bool focusChanged() { return !sameId(focusNow, focusPrev); }
void endFrame() { focusPrev = focusNow; }

const char *labelFor(const char *panelId)
{
	// The registry already holds the label. In the fork this was a twelve-branch
	// strcmp chain that had to gain a line every time a panel was added, and a
	// panel whose line was forgotten showed its cfg key to the user.
	const panels::Panel *p = panels::find(panelId);
	return p != nullptr ? p->label : (panelId != nullptr ? panelId : "");
}

//! `Zoom.<id>`, derived rather than hand-written per window.
static std::string zoomKey(const char *panelId)
{
	return std::string("Zoom.") + (panelId != nullptr ? panelId : "");
}

//! The steady outline on whichever window owns the menu bar and hotkeys. Not
//! the sender glow - this one only says "your menus and hotkeys act HERE".
static void activeWindowOutline(const char *panelId)
{
	if (!sameId(selectedId, panelId))
		return;
	const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
	const float in = 3.f;
	ImGui::GetForegroundDrawList()->AddRect(ImVec2(wp.x + in, wp.y + in),
			ImVec2(wp.x + ws.x - in, wp.y + ws.y - in),
			ImGui::GetColorU32(TAS_FOCUS_RING), 5.f, 0, 2.2f);
}

float zoom(const char *panelId, int defaultPercent)
{
	ImGuiIO& io = ImGui::GetIO();
	const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
	const std::string key = zoomKey(panelId);

	// Focus-change select catches title-bar and tab clicks, which never produce
	// a click inside the body - so the two selection paths are not redundant.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		noteFocused(panelId);
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		select(panelId);

	activeWindowOutline(panelId);

	// !WantTextInput so Ctrl+wheel inside a text field is the field's, not ours.
	if (hovered && io.KeyCtrl && io.MouseWheel != 0.f && !io.WantTextInput)
	{
		const int was = cfgLoadInt("dojo", key, defaultPercent);
		const int now = std::max(60, std::min(140, was + (io.MouseWheel > 0.f ? 5 : -5)));
		if (now != was)
			// ONE store. The "write both" rule in CLAUDE.md is conditional on a
			// -config flag being able to shadow the key, and no launch flag sets
			// Zoom.*. cfgSaveInt already lands in the same cfgdb cfgLoadInt
			// reads, so the shadow would buy no liveness and cost persistence:
			// the virtual section wins every later read, so the value on disk
			// could never be seen again this process. See dojocfg's launchable
			// column, which is this rule made lookup-able.
			cfgSaveInt("dojo", key, now);
	}
	return std::max(0.6f, std::min(1.4f, cfgLoadInt("dojo", key, defaultPercent) / 100.f));
}

void resetAllZoom()
{
	// A LOOP OVER THE REGISTRY, not a table of twelve cfg keys. His version had
	// to be edited every time a panel was added, and a panel missing from it
	// simply never reset.
	for (const panels::Panel& p : panels::all())
	{
		cfgSaveInt("dojo", zoomKey(p.id), 100);	// one store; see zoomFor()
	}
}

/*
	SELF-TEST. Same reasoning as the panel registry's: this state has one writer
	now, and the whole value of that is properties nobody can see break. A
	selection that fails to toggle, or a label that silently falls back to a cfg
	key, look exactly like working software.

	Runs under `-config dojo:PanelSelfTest=yes`, alongside the registry's.
	Everything here is testable WITHOUT an ImGui frame; zoom() is not, because
	it asks ImGui what is hovered - so it is exercised by using the panel, and
	this file says so rather than pretending otherwise.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "TASUI SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const char *savedSel = selectedId, *savedLast = lastSelectedId;
	selectedId = lastSelectedId = nullptr;

	claim("nothing is selected to begin with", selected() == nullptr);

	select("alpha");
	claim("select() takes", sameId(selected(), "alpha"));

	select("beta");
	claim("selecting another takes", sameId(selected(), "beta"));

	// THE TOGGLE IS THE POINT of keeping lastSelected at all: the [MAIN] chip
	// drops to the global bar and puts you back where you were.
	deselect();
	claim("deselect() drops to the global bar", selected() == nullptr);
	deselect();
	claim("deselect() again restores the previous panel", sameId(selected(), "beta"));

	// The toggle target is whatever deselect() was LEAVING, not the panel before
	// it. This claim replaced one that guarded a line since found to be dead.
	select("gamma");
	deselect();
	deselect();
	claim("the toggle returns to the panel you left, not the one before it",
			sameId(selected(), "gamma"));

	// Focus: change detection is what catches title-bar and tab clicks, which
	// never produce a click inside the body.
	focusNow = focusPrev = nullptr;
	noteFocused("alpha");
	claim("focus moving is detected", focusChanged());
	endFrame();
	claim("and is NOT still reported after the frame it happened in", !focusChanged());

	// labelFor resolves through the registry, and BOTH directions are asserted.
	//
	// The first version of this claim was `sameId(labelFor(x), x) || find(x)`,
	// which passes whichever branch is true - a claim that cannot fail, written
	// by someone who has spent all day telling other people not to write them.
	// Registering a panel here makes both halves answerable.
	static bool labelProbeOpen = false;
	panels::add({ "selftest.label", "A Readable Label", &labelProbeOpen,
			[]() {}, panels::Osd, false });
	claim("labelFor() gives a registered panel's LABEL, not its id",
			sameId(labelFor("selftest.label"), "A Readable Label"));
	claim("labelFor() falls back to the id for an unregistered panel",
			sameId(labelFor("selftest.nosuchpanel"), "selftest.nosuchpanel"));

	// ZOOM MUST STAY PERSISTABLE. resetAllZoom touches only cfg (no ImGui
	// window), so it is the one zoom path testable before any frame exists.
	// This claim is red against the version of this file that wrote
	// cfgSetVirtual beside every cfgSaveInt: a shadow entry wins every later
	// read, so the value on disk becomes unreachable for the life of the
	// process and the panel looks like it "won't stay where I put it".
	{
		const std::string zk = zoomKey("selftest.label");
		resetAllZoom();
		cfgSaveInt("dojo", zk, 120);	// stands in for a later writer: a UI, a profile
		claim("a zoom key is not shadowed, so a later write is still readable",
				cfgLoadInt("dojo", zk, 0) == 120);
		claim("...and the cfg store agrees it holds no virtual entry for it",
				!cfgIsVirtual("dojo", zk));
	}

	selectedId = savedSel;
	lastSelectedId = savedLast;
	NOTICE_LOG(RENDERER, "TASUI SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace tas_ui
