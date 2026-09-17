#include "ui_text.h"
#include "surface_tour.h"
#include "tas_clip.h"
#include "dojo.h"
#include "roll_host.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "stdclass.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

/*
	THE TEST LAB - a permanent library of fixture states, each with its own
	scratch slots.

	`[PORTED 2026-09-14]` from the TAS fork's `tasTestLabWindow` (371 lines).
	Chosen by the CALLER CENSUS rather than by size: `core/dojo/tas_clip.cpp`
	already carries the entire backend - `labDir`, `labIsActive`, `labTests`,
	`labNewTestDir` - and `[MEASURED 2026-09-14]` NONE of them had a caller
	outside `tas_clip.cpp`, inside a 1,025-line module that is otherwise live.
	Second instance of the shape `tas_auto` turned out to be in, found the same
	way and fixed the same way: build the panel, not the engine.

	WHAT A TEST IS, in the backend's own words: a folder under
	`replays/<game>/_lab`, where "BASE (slot 0) is its permanent fixture - only
	Overwrite BASE changes it; slots 1-99 are that test's own outcomes /
	scratch, so tests never stomp each other". The folder NAME is what marks the
	lab, so nothing about it can leak across sessions.

	WHAT IS NOT IN THIS CUT, each for a reason rather than for time:

	- THE ROLL -> MACRO LINK. His `gui_lab_add_test` also writes the current
	  Piano Roll into the test as `<folder>_macro.txt`, 0-based relative to the
	  copied state's movie frame, via `gui_lab_write_roll_macro`. We have no
	  equivalent writer, and inventing one would be a second owner of "how a
	  roll becomes a macro file" beside `Dojo::WriteMacroFile`. A test created
	  here is BASE-only; record its sequence afterwards.
	- THE LOCK / UNLOCK DRIVER MACHINERY. His window can freeze the active
	  test's macro against the perpetual auto-save (`dojo.macro_locked`, which
	  this tree does not have) and drives it through `tasDriver()` /
	  `tasDriverSet()`. That is a change to the SAVE path, not to a panel, and
	  it belongs in its own commit with its own evidence.
	- `tasOpenDir`, which shells out to a file manager.
*/
namespace roll
{
namespace lab
{

//! The game's base name, which is what the lab folder is keyed on.
static std::string gameBaseName()
{
	return get_game_name();
}

/*
	MAKE THE CURRENT SLOT THE BASE OF A NEW TEST.

	Ported from his `gui_lab_add_test`: copy the slot's state and every sidecar
	into the next TEST_NN folder as slot 0, then seed the folder's clip.json so
	the library can see it.

	THE SIDECAR LIST IS THE INTERESTING PART and it is his, kept verbatim. A
	state is not one file - `.frame` anchors it to a movie index, `.png` is the
	thumbnail, `.label` is its name, `.wave` is the audio. Copying only the
	`.state` produces a test that loads and has forgotten where it was.
*/
bool addTestFromSlot(int slot)
{
	const std::string src = hostfs::getSavestatePath(slot, false);
	std::error_code ec;
	if (!ghc::filesystem::exists(src, ec))
	{
		gui_display_notification("No state in that slot", 2000);
		return false;
	}
	const std::string label = hostfs::loadSavestateLabel(slot);
	const std::string dir = tas_clip::labNewTestDir(gameBaseName(), label);
	ghc::filesystem::create_directories(dir, ec);
	if (ec)
	{
		gui_display_notification("Test Lab: could not create the test folder", 3000);
		return false;
	}
	const std::string name = ghc::filesystem::path(dir).filename().string();
	// Slot 0 IS the base, so the destination is whatever slot 0 is named here.
	const std::string dstBase = (ghc::filesystem::path(dir)
			/ ghc::filesystem::path(hostfs::getSavestatePath(0, false)).filename()).string();
	static const char *const sidecars[] = { "", ".frame", ".png", ".label", ".wave" };
	int copied = 0;
	for (const char *sc : sidecars)
	{
		const std::string from = src + sc;
		if (!ghc::filesystem::exists(from, ec))
			continue;
		ghc::filesystem::copy_file(from, dstBase + sc,
				ghc::filesystem::copy_options::overwrite_existing, ec);
		if (!ec)
			copied++;
	}
	tas_clip::seed(dir, gameBaseName(), tas_clip::utcNowIso(), "test-lab",
			label.empty()
				? std::string("Test Lab test: BASE (slot 0) is the fixture; "
						"slots 1-99 are its outcomes.")
				: label);
	char msg[224];
	snprintf(msg, sizeof(msg), "New test %s - BASE from slot %d (%d file%s)%s",
			name.c_str(), slot, copied, copied == 1 ? "" : "s",
			label.empty() ? " - unlabeled: label the state first, the label names the test" : "");
	gui_display_notification(msg, 4500);
	NOTICE_LOG(SAVESTATE, "TAS LAB: new test %s <- %s (%d file%s)",
			dir.c_str(), src.c_str(), copied, copied == 1 ? "" : "s");
	// The list rescans on the library version, so tell it something moved.
	tas_clip::bump();
	if (tas_clip::labIsActive(hostfs::savestateFolderOverride))
		dojo.savestate_epoch++;
	return true;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "LAB SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	/*
		THE TAG GRAMMAR, which this panel edits and which had NO COVERAGE. The
		census that found the lab backend unreachable found these the same way:
		parseTags / hasTag / toggleTagCsv are pure, shipped, and were called by
		nothing. A pure function nobody calls and nobody checks is a guess with
		a signature.
	*/
	{
		std::vector<std::string> t;
		tas_clip::parseTags("a, b ,c", t);
		claim("tags split on commas and lose their padding",
				t.size() == 3 && t[0] == "a" && t[1] == "b" && t[2] == "c");
		tas_clip::parseTags("", t);
		claim("an empty tag string is no tags", t.empty());
		tas_clip::parseTags("  ,  ,  ", t);
		claim("...and so is a string of only separators", t.empty());
		tas_clip::parseTags("solo", t);
		claim("one tag needs no comma", t.size() == 1 && t[0] == "solo");
	}
	{
		std::vector<std::string> t;
		tas_clip::parseTags("Combo, Ready", t);
		claim("a tag is found whatever its case",
				tas_clip::hasTag(t, "combo") && tas_clip::hasTag(t, "READY"));
		// THE CONTROL. A hasTag() that answered true for everything satisfies
		// the claim above on its own.
		claim("...and a tag that is not there is not found",
				!tas_clip::hasTag(t, "broken"));
		claim("a prefix of a tag is not that tag", !tas_clip::hasTag(t, "comb"));
	}
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%s", "alpha, beta");
		tas_clip::toggleTagCsv(buf, sizeof(buf), "gamma");
		std::vector<std::string> t;
		tas_clip::parseTags(buf, t);
		claim("toggling an absent tag ADDS it", t.size() == 3 && tas_clip::hasTag(t, "gamma"));
		tas_clip::toggleTagCsv(buf, sizeof(buf), "gamma");
		tas_clip::parseTags(buf, t);
		claim("...and toggling it again REMOVES it",
				t.size() == 2 && !tas_clip::hasTag(t, "gamma"));
		claim("...leaving the others alone",
				tas_clip::hasTag(t, "alpha") && tas_clip::hasTag(t, "beta"));
	}

	/*
		THE LAB FOLDER IS NAMED, NOT GUESSED. labIsActive keys on the folder
		name, so a clip that merely sits near the lab is not the lab - which is
		the property that keeps a test's scratch slots from leaking into a real
		recording.
	*/
	{
		const std::string dir = tas_clip::labDir("SomeGame");
		claim("the lab folder is under the game's replays", dir.find("SomeGame") != std::string::npos);
		claim("the lab folder is the one named _lab", dir.find("_lab") != std::string::npos);
		claim("the lab folder IS a lab session", tas_clip::labIsActive(dir));
		claim("a test folder inside it is too",
				tas_clip::labIsActive(dir + "/TEST_01_thing"));
		// THE CONTROL, and the one that matters: an ordinary clip must not be
		// mistaken for the lab, or its slots become a test's scratch.
		claim("an ordinary clip folder is NOT",
				!tas_clip::labIsActive("/tmp/replays/SomeGame/2026-09-14T00_00_00Z"));
		claim("nothing bound is not a lab session", !tas_clip::labIsActive(""));
	}

	NOTICE_LOG(RENDERER, "LAB SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The panel.
// ---------------------------------------------------------------------------------------

static bool labOpen = false;
static std::vector<tas_clip::LabTest> tests;
static u32 seenVer = ~0u;
static double scannedAt = 0;
static int selected = -1;			//!< index into `tests`; -1 = none
static std::string selectedDir;		//!< the selection's identity - see below
static char tagsBuf[256] = "";
static char notesBuf[1024] = "";

static void rescan(bool force)
{
	const double now = os_GetSeconds();
	// Keyed on the library version AND a 2 s floor, his pattern: a folder can
	// change under us without going through tas_clip, so the version alone
	// would miss it and a per-frame scan would stat the disk 60 times a second.
	if (!force && seenVer == tas_clip::libraryVersion() && now - scannedAt <= 2.0)
		return;
	tas_clip::labTests(gameBaseName(), tests);
	seenVer = tas_clip::libraryVersion();
	scannedAt = now;
}

static void draw()
{
	if (settings.content.fileName.empty())
	{
		tasTextDisabled("No game loaded - the lab is per game.");
		return;
	}
	rescan(false);

	const bool active = tas_clip::labIsActive(hostfs::savestateFolderOverride);
	tasTextColored(TAS_MODULE_COL, "TEST LAB");
	ImGui::SameLine();
	if (active)
		tasTextDisabled("%s", ghc::filesystem::path(hostfs::savestateFolderOverride)
				.filename().string().c_str());
	else
		// SAID, because it changes what the buttons below mean. Tests can be
		// created from any session; only a lab session has its slots bound to
		// one.
		tasTextDisabled("not a lab session - tests can still be added");

	{
		char b[72];
		snprintf(b, sizeof(b), "New test from slot %d", (int)config::SavestateSlot);
		if (tasButton(b))
		{
			addTestFromSlot((int)config::SavestateSlot);
			rescan(true);
		}
		tasTipItem("The slot's state, with its label and sidecars, becomes the BASE "
				"of a new test. Label the state first - the label names the test.");
	}
	ImGui::SameLine();
	tasTextDisabled("%d test%s", (int)tests.size(), tests.size() == 1 ? "" : "s");

	ImGui::Separator();
	if (tests.empty())
	{
		// NOT AN EMPTY TABLE. "no tests yet" and "the scan failed" look
		// identical as a blank list.
		tasTextDisabled("No tests yet. Save a state, label it, then add it here.");
		return;
	}

	const float availY = ImGui::GetContentRegionAvail().y;
	const float listH = availY > 200.f ? availY - 150.f : availY * 0.55f;
	if (ImGui::BeginChild("##labtests", ImVec2(0, listH), true))
	{
		if (ImGui::BeginTable("##tests", 4, ImGuiTableFlags_RowBg
				| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			tasTableSetupColumn("test");
			tasTableSetupColumn("base");
			tasTableSetupColumn("states");
			tasTableSetupColumn("tags");
			ImGui::TableHeadersRow();
			for (int i = 0; i < (int)tests.size(); i++)
			{
				const tas_clip::LabTest& t = tests[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				/*
					THE ROW'S IDENTITY IS ITS DIRECTORY, not its index.
					`[SOURCE]` core/rend/panel.h states the rule and
					core/dojo/ui_text_panel.cpp paid for breaking it earlier
					today: the list rescans on a timer, so an index selected on
					one frame can point at a different test on the next - and
					the edit box would then write one test's tags onto another.
				*/
				ImGui::PushID(t.dir.c_str());
				const bool sel = (t.dir == selectedDir);
				if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns))
				{
					selected = i;
					selectedDir = t.dir;
					std::string tags, notes;
					tas_clip::readTagsNotes(t.dir, tags, notes, nullptr, nullptr);
					snprintf(tagsBuf, sizeof(tagsBuf), "%s", tags.c_str());
					snprintf(notesBuf, sizeof(notesBuf), "%s", notes.c_str());
				}
				ImGui::SameLine(0, 0);
				ImGui::TextUnformatted(t.name.c_str());
				ImGui::TableSetColumnIndex(1);
				if (t.hasBase)
					tasTextColored(TAS_READ, "yes");
				else
					// A TEST WITHOUT A BASE IS BROKEN, and silently: it will
					// load nothing. Said in the colour that means "wrong".
					tasTextColored(TAS_WRITE, "MISSING");
				ImGui::TableSetColumnIndex(2);
				tasTextDisabled("%d", t.states);
				ImGui::TableSetColumnIndex(3);
				if (t.tags.empty())
					tasTextDisabled("-");
				else
				{
					std::string csv;
					for (size_t k = 0; k < t.tags.size(); k++)
						csv += (k ? ", " : "") + t.tags[k];
					tasTextDisabled("%s", csv.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
	ImGui::EndChild();

	// LOOK THE SELECTION UP BY DIRECTORY, for the reason above.
	const tas_clip::LabTest *cur = nullptr;
	for (const tas_clip::LabTest& t : tests)
		if (t.dir == selectedDir)
		{
			cur = &t;
			break;
		}
	if (cur == nullptr)
	{
		tasTextDisabled("Pick a test to edit its tags and notes.");
		return;
	}

	ImGui::Separator();
	tasTextColored(TAS_ACCENT, "%s", cur->name.c_str());
	if (!cur->createdLocal.empty())
	{
		ImGui::SameLine();
		tasTextDisabled("created %s", cur->createdLocal.c_str());
	}
	ImGui::SetNextItemWidth(-1.f);
	tasInputTextWithHint("##tags", "tags, comma separated", tagsBuf, sizeof(tagsBuf));
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputTextMultiline("##notes", notesBuf, sizeof(notesBuf), ImVec2(0, 44.f));
	if (tasButton("Save tags & notes"))
	{
		if (tas_clip::writeTagsNotes(cur->dir, tagsBuf, notesBuf))
		{
			NOTICE_LOG(RENDERER, "TAS LAB: %s tags/notes saved", cur->name.c_str());
			tas_clip::bump();
			rescan(true);
		}
		else
			gui_display_notification("Test Lab: could not write clip.json", 3000);
	}

	// FINALIZE: lock this test's macro against the perpetual auto-save
	// (dojo.macro_locked reads tas_clip::readLocked on the bound folder).
	bool locked = cur->locked;
	const bool isCur = cur->dir == hostfs::savestateFolderOverride;
	if (tasCheckbox("Finalize (lock macro)", &locked) && locked != cur->locked)
	{
		// His "Overwrite + lock": when finalizing the BOUND test, write the macro
		// ONE last time (macro_force_write bypasses the lock we are about to set) so
		// the finalized file is current, then sync dojo.macro_locked so the auto-save
		// is protected IN-SESSION - not only on the next clip-bind (BeginClipStats
		// reads readLocked). A non-bound test just persists the flag.
		if (locked && isCur)
		{
			dojo.macro_force_write = true;
			dojo.WriteMacroFile();
			dojo.macro_force_write = false;
		}
		tas_clip::setLocked(cur->dir, locked);
		if (isCur)
			dojo.macro_locked = locked;
		NOTICE_LOG(RENDERER, "TAS LAB: %s macro %s", cur->name.c_str(), locked ? "LOCKED" : "unlocked");
		tas_clip::bump();
		rescan(true);
	}

	// SAFE DELETE: move the whole test folder to <lab>/.trash - never a hard
	// delete (tas_clip::labTrashTest). Refused on the currently-bound test.
	ImGui::BeginDisabled(isCur);
	if (tasButton("Delete test (to .trash)"))
	{
		const std::string d = cur->dir;
		if (tas_clip::labTrashTest(d))
		{
			gui_display_notification("Test moved to .trash", 2800);
			selected = -1;
			selectedDir.clear();
			rescan(true);
		}
		else
			gui_display_notification("Could not move test - see log", 3500);
	}
	ImGui::EndDisabled();
	if (isCur)
		tasTip("Switch off this test before deleting it.");
}

}	// namespace lab

void registerLabPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	/*
		MENU ONLY. A test is picked and annotated between runs, not watched
		while the game moves - and the Input Sender next door is the opposite
		case, which is why the stream is a per-panel field rather than a
		default.
	*/
	panels::add({ "testlab", "Test Lab", &lab::labOpen, lab::draw, panels::Menu,
			/*persist*/ true, /*defW*/ 520.f, /*defH*/ 400.f });
	NOTICE_LOG(RENDERER, "TEST LAB: registered=%s open=%s",
			panels::find("testlab") != nullptr ? "yes" : "NO", lab::labOpen ? "yes" : "no");
}

/*
	SURFACE TOUR HOOKS - add a test from slot 0 through the real verb, read the
	library back one longer, remember the new folder; then trash exactly that one and
	read it back gone. Contract: surface_tour.h.
*/
static std::string g_tourLabDir;

bool surfacetour::hooks::labAddTest()
{
	if (settings.content.fileName.empty())
	{
		surfacetour::why("no game loaded");
		return false;
	}
	std::vector<tas_clip::LabTest> before, after;
	const int nb = tas_clip::labTests(lab::gameBaseName(), before);
	if (!lab::addTestFromSlot(0))
	{
		surfacetour::why("addTestFromSlot(0) refused (no state in slot 0?)");
		return false;
	}
	const int na = tas_clip::labTests(lab::gameBaseName(), after);
	if (na != nb + 1)
	{
		surfacetour::why("library count %d -> %d, expected +1", nb, na);
		return false;
	}
	g_tourLabDir.clear();
	for (const tas_clip::LabTest& t : after)
	{
		bool seen = false;
		for (const tas_clip::LabTest& b : before)
			if (b.dir == t.dir) { seen = true; break; }
		if (!seen) { g_tourLabDir = t.dir; break; }
	}
	if (g_tourLabDir.empty())
	{
		surfacetour::why("could not identify the new test in the library");
		return false;
	}
	return true;
}

bool surfacetour::hooks::labTrashTest()
{
	if (g_tourLabDir.empty())
	{
		surfacetour::why("no tour test to trash (add first)");
		return false;
	}
	if (!tas_clip::labTrashTest(g_tourLabDir))
	{
		surfacetour::why("labTrashTest refused for %s", g_tourLabDir.c_str());
		return false;
	}
	std::error_code ec;
	if (ghc::filesystem::exists(g_tourLabDir, ec))
	{
		surfacetour::why("%s still exists after trash", g_tourLabDir.c_str());
		return false;
	}
	g_tourLabDir.clear();
	return true;
}

}	// namespace roll
