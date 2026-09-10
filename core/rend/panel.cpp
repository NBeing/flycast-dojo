#include "panel.h"
#include "cfg/cfg.h"
#include "imgui.h"
#include <stdexcept>
#include "log/LogManager.h"
#include <cstring>

namespace panels
{

/*
	One array. Registration is additive and happens once, before the first
	frame; nothing removes a panel, so `all()` can hand out a reference and
	callers can hold it across frames.

	A vector rather than nbneo's fixed C array because our panels are declared
	across translation units - gui.cpp owns some, dojo_gui.cpp will own the
	ported ones - and a single static table would need every draw function
	visible in one file, which is the coupling this is trying to remove.
*/
static std::vector<Panel> registry;

static std::string keyFor(const Panel& p);

void add(const Panel& p)
{
	// A DUPLICATE ID IS A BUG, NOT A SECOND PANEL. Two entries sharing an id
	// means two windows fighting over one persistence key and one menu row,
	// and the loser loses silently.
	if (find(p.id) != nullptr)
	{
		ERROR_LOG(RENDERER, "panel '%s' registered twice - ignoring the second", p.id);
		return;
	}
	if (p.id == nullptr || p.open == nullptr || p.draw == nullptr)
	{
		ERROR_LOG(RENDERER, "panel '%s' is missing a required field",
				p.id != nullptr ? p.id : "(null id)");
		return;
	}
	registry.push_back(p);

	// RESTORE AT REGISTRATION, not from a startup sweep. Panels register at
	// different times - some at init, some lazily on first draw - so any single
	// "now load them all" call runs before the late ones exist and silently
	// does nothing for them.
	//
	// `[MEASURED 2026-09-09]` loadOpenState() was never called ANYWHERE outside
	// this file's own self-test, so `persist: true` restored nothing: a panel
	// opened from cfg stayed shut, which looks exactly like a panel that failed
	// to register. Found by launching a persist=true panel with its cfg key set
	// and watching it not appear.
	if (registry.back().persist)
		*registry.back().open = cfgLoadBool("dojo", keyFor(registry.back()),
				*registry.back().open);
}

const std::vector<Panel>& all() { return registry; }

const Panel *find(const char *id)
{
	if (id == nullptr)
		return nullptr;
	for (const Panel& p : registry)
		if (std::strcmp(p.id, id) == 0)
			return &p;
	return nullptr;
}

void open(const char *id)
{
	const Panel *p = find(id);
	if (p == nullptr)
	{
		// LOUD, because the alternative is a feature that silently never shows
		// its window and a user who reports that the button does nothing.
		ERROR_LOG(RENDERER, "panels::open('%s') - no such panel", id != nullptr ? id : "(null)");
		return;
	}
	*p->open = true;
}

bool toggle(const char *id)
{
	const Panel *p = find(id);
	if (p == nullptr)
	{
		// Same reasoning as open(): a hotkey bound to a panel that is not
		// registered looks exactly like a hotkey that is not bound.
		ERROR_LOG(RENDERER, "panels::toggle('%s') - no such panel", id != nullptr ? id : "(null)");
		return false;
	}
	*p->open = !*p->open;
	// TRACED UNCONDITIONALLY. A toggle is a deliberate user action a few times
	// a session, not a per-frame event, so this costs nothing - and it is the
	// only observable a test outside the process has for "the hotkey arrived".
	// Without it, "the key is not bound", "the dispatch never ran" and "the
	// panel toggled" are the same silence from the log.
	NOTICE_LOG(RENDERER, "PANEL TOGGLE: %s -> %s", id, *p->open ? "open" : "closed");
	return *p->open;
}

void visitStream(Stream s, void (*fn)(const Panel&))
{
	for (const Panel& p : registry)
	{
		if ((p.stream & s) == 0)
			continue;		// declared for the other stream
		if (!*p.open)
			continue;
		fn(p);
	}
}

//! Ids are compared by VALUE, not by pointer: a caller may pass a literal that
//! is not the same object the descriptor holds.
static bool sameId(const char *a, const char *b)
{
	return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

static const char *skipThisFrame = nullptr;

void drawStream(Stream s, const char *skipId)
{
	skipThisFrame = skipId;
	visitStream(s, [](const Panel& p) {
		if (skipThisFrame != nullptr && sameId(p.id, skipThisFrame))
			return;
		// Begin/End are the REGISTRY's, always paired, whatever the body does.
		// `open` is handed to ImGui so the window's own close button writes
		// straight into the one owner of that fact.
		const bool expanded = ImGui::Begin(p.label, p.open);
		if (expanded)
		{
			try {
				p.draw();
			} catch (const std::exception& e) {
				// VISIBLE, not counted. A fault printed into the panel that
				// caused it is a fault someone will fix; a fault added to a
				// counter looks exactly like a tool that drew almost nothing.
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "panel '%s' raised:", p.id);
				ImGui::TextWrapped("%s", e.what());
			}
		}
		ImGui::End();		// unconditional, per ImGui's contract
	});
}

/*
	Open state, only for the panels that asked for it.

	KEYED ON THE ID, under `dojo` as `Panel.<id>`. Legible in emu.cfg, and
	immune to a label being reworded - which is the whole reason the two are
	separate fields.
*/
static std::string keyFor(const Panel& p) { return std::string("Panel.") + p.id; }

void loadOpenState()
{
	for (Panel& p : registry)
		if (p.persist)
			*p.open = cfgLoadBool("dojo", keyFor(p), *p.open);
}

void saveOpenState()
{
	for (const Panel& p : registry)
		if (p.persist)
			cfgSaveBool("dojo", keyFor(p), *p.open);
}

/*
	A SELF-TEST FOR THE THREE LOOPS, because a registry nothing has exercised is
	a registry that will be wrong the first time it matters - and its failures
	are silent by construction (a panel drawn in the wrong stream, or not at
	all, does not raise).

	Run with `-config dojo:PanelSelfTest=yes`. It registers synthetic panels,
	drives every loop, and prints one line per claim. It uses the real registry
	rather than a copy: a self-test against a second implementation proves the
	second implementation.

	Deliberately NOT compiled out. It costs one cfg read at startup, and the
	whole point of the exercise is that this machinery ships correct.
*/
static bool selfOsdOpen = true, selfMenuOpen = true, selfClosedOpen = false;
static int selfOsdDraws = 0, selfMenuDraws = 0, selfClosedDraws = 0;
static void selfDrawOsd()    { selfOsdDraws++; }
static void selfDrawMenu()   { selfMenuDraws++; }
static void selfDrawClosed() { selfClosedDraws++; }

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	const size_t before = registry.size();
	add({ "selftest.osd",    "Osd",    &selfOsdOpen,    selfDrawOsd,    Osd,  false });
	add({ "selftest.menu",   "Menu",   &selfMenuOpen,   selfDrawMenu,   Menu, false });
	add({ "selftest.closed", "Closed", &selfClosedOpen, selfDrawClosed, Both, false });

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "PANEL SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	claim("three panels registered", registry.size() == before + 3);

	// A duplicate id must be REFUSED, not appended. Two entries sharing an id
	// means two windows over one persistence key, and the loser loses silently.
	const size_t afterAdd = registry.size();
	add({ "selftest.osd", "Dup", &selfOsdOpen, selfDrawOsd, Osd, false });
	claim("a duplicate id is refused", registry.size() == afterAdd);

	claim("find() resolves a registered id", find("selftest.menu") != nullptr);
	claim("find() answers null for an unknown id", find("selftest.nope") == nullptr);

	// THE STREAM MASK IS THE FIELD THAT CAUSED TWO SHIPPED DEFECTS ELSEWHERE,
	// so it gets the most checks: each stream draws its own and NOT the other.
	// visitStream, not drawStream: this runs at startup, before any ImGui frame
	// exists. It is the same selection code, not a copy of it.
	static auto count = [](const Panel& p) { p.draw(); };
	selfOsdDraws = selfMenuDraws = selfClosedDraws = 0;
	visitStream(Osd, count);
	claim("the Osd stream drew the Osd panel", selfOsdDraws == 1);
	claim("the Osd stream did NOT draw the Menu panel", selfMenuDraws == 0);

	selfOsdDraws = selfMenuDraws = 0;
	visitStream(Menu, count);
	claim("the Menu stream drew the Menu panel", selfMenuDraws == 1);
	claim("the Menu stream did NOT draw the Osd panel", selfOsdDraws == 0);

	// A CLOSED PANEL IS NOT DRAWN, in either stream. Without this the open flag
	// is decoration and every panel is always on.
	claim("a closed panel is not drawn", selfClosedDraws == 0);

	// ...and the control: the same panel, opened, IS drawn in both streams it
	// declared. Without this the line above passes on a registry that draws
	// nothing at all.
	selfClosedOpen = true;
	selfClosedDraws = 0;
	visitStream(Osd, count);
	visitStream(Menu, count);
	claim("the same panel, opened, draws in BOTH streams it declared",
			selfClosedDraws == 2);
	selfClosedOpen = false;

	// Persistence: round-trip through the real cfg store, under the id.
	selfMenuOpen = false;
	saveOpenState();		// selftest panels are persist=false, so this must NOT save them
	selfMenuOpen = true;
	loadOpenState();
	claim("persist=false is not written or restored", selfMenuOpen == true);

	NOTICE_LOG(RENDERER, "PANEL SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace panels
