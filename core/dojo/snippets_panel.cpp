#include "ui_text.h"
#include "roll_library.h"
#include "roll_select.h"
#include "roll_pattern.h"
#include "roll_edit.h"
#include "roll_profile.h"
#include "tas_colors.h"
#include "dojo.h"
#include "movie.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <map>
#include <string>
#include <vector>

/*
	THE SNIPPETS BROWSER - the sequence library, as a panel.

	`[PORTED 2026-09-15]` from the fork's Snippets window (his SnippetBrowser at
	dojo_gui.cpp:9363/12815). The ENGINE was already here and tested: the whole
	library lives in core/dojo/roll_library (libraryScan/Read/Retag/Rename/Delete,
	patternReplacing/Overdubbing, Sequence), and scripts/rolltest.sh's RollLibProbe
	already drives write -> scan -> PLACE -> undo end to end. So - like the Input
	Sender and Test Lab before it - this window was NOT a 2,349-line port; it is a
	browser on an engine that was already paid for. His SnippetBrowser extends a
	TasBrowser base this tree does not have, so the list is drawn with stock ImGui,
	the way branch_panel draws its graph rather than pulling in imgui-node-editor.

	The one fact this owns and must keep straight (roll_library states it): a stored
	sequence does NOT record replace-vs-overdub - the PLACE GESTURE decides. So there
	are two Place buttons, not a mode toggle: REPLACE (a gap clears the frame it lands
	on) and OVERDUB (a gap leaves the input under it). Both go through the roll's ONE
	edit funnel (Dojo::ApplyEdit), so undo, the .flyr and staging behave as ever.
*/
namespace roll {
namespace snippets {

static bool snippetsOpen = false;
static int selectedRow = -1;
static char renameBuf[128] = "";
static char tagsBuf[192] = "";

//! session_inputs as the map the pattern functions take (roll_panel keeps the same
//! file-local helper; it is a few-line copy, not a second owner of a fact).
static std::map<u32, Row> wholeMovie()
{
	std::map<u32, Row> m;
	for (const auto& kv : dojo.session_inputs)
		m[kv.first] = kv.second;
	return m;
}

/*
	Place a library sequence into the movie at the Piano Roll's current selection
	(anchor = its first row), through the roll's edit funnel. overdub picks the mask
	(see the header). Returns the first changed frame, or -1 (no movie / no selection
	/ the funnel refused). SHARED by the Place buttons and the probe, so the test
	drives exactly what the button does.
*/
static s64 place(const Sequence& s, bool overdub)
{
	if (s.empty() || !movie::authored())
		return -1;
	const Selection& sel = selection();
	if (sel.empty())
		return -1;
	const Pattern pat = overdub ? patternOverdubbing(s) : patternReplacing(s);
	Edit e = applyPattern(wholeMovie(), sel.lo(), sel.hi(), pat, 0);
	const s64 first = dojo.ApplyEdit(e, overdub ? "snippet: overdub" : "snippet: replace");
	NOTICE_LOG(RENDERER, "SNIPPET: placed '%s' (%zu frames, %s) at %u..%u -> first changed %lld",
			s.name.c_str(), s.length(), overdub ? "overdub" : "replace", sel.lo(), sel.hi(), (long long)first);
	return first;
}

// ---------------------------------------------------------------------------------------
// INTEGRATION PROBE, dojo:SnippetProbe=yes - the branch analogue of RollLibProbe, but for
// THIS panel's place path. One-shot: scan the real library, place its first sequence at the
// selection through place() (the same call the button makes), report first-changed. The
// harness seeds a known snippet and a selection, then asserts the movie changed at it.
// ---------------------------------------------------------------------------------------
static void probe()
{
	static bool done = false;
	if (done || !cfgLoadBool("dojo", "SnippetProbe", false) || !movie::authored())
		return;
	done = true;
	int skipped = 0;
	std::vector<Sequence> lib = libraryScan(&skipped);
	// Self-seed a known snippet if the library is empty, so the test is self-contained
	// (the fork's own RollLibProbe does the same). One lane: on, GAP, on - so replace
	// and overdub would differ, and a placement is visible.
	if (lib.empty())
	{
		std::error_code lec;
		ghc::filesystem::create_directories(libraryDir(), lec);
		const Profile& prof = profile();
		Sequence sq;
		sq.name = "snipprobe";
		sq.tags = { "probe" };
		sq.lanes.resize(1);
		sq.lanes[0] = { cellWith(0, prof.cols[0], true), 0, cellWith(0, prof.cols[0], true) };
		std::string werr;
		libraryWrite(libraryDir() + "/__snipprobe.txt", sq, werr);
		lib = libraryScan(&skipped);
	}
	NOTICE_LOG(RENDERER, "SNIPPET PROBE: library has %d sequence(s) (%d skipped)", (int)lib.size(), skipped);
	if (lib.empty())
	{
		NOTICE_LOG(RENDERER, "SNIPPET PROBE RESULT: placed=NO reason=empty-library");
		return;
	}
	// Select a mid-movie range to place onto (the harness leaves the roll unselected
	// headlessly, so the probe makes its own target - mid-match, where a wrong answer
	// looks wrong, not at the movie edge).
	const u32 mid = movie::end() > 40 ? movie::end() - 40 : 0;
	const size_t len = std::max<size_t>(1, lib.front().length());
	selection().clear();
	selection().press(mid, Mods{});							// anchor
	if (len > 1)
		selection().press(mid + (u32)len - 1, Mods{true, false, false});	// Shift = range replaces
	const Row before = [&]{ auto it = dojo.session_inputs.find(mid); return it == dojo.session_inputs.end() ? Row() : it->second; }();
	const s64 first = place(lib.front(), /*overdub*/ false);
	const Row after = [&]{ auto it = dojo.session_inputs.find(mid); return it == dojo.session_inputs.end() ? Row() : it->second; }();
	NOTICE_LOG(RENDERER, "SNIPPET PROBE RESULT: placed=%s first=%lld rowChanged=%s libCount=%d",
			first >= 0 ? "yes" : "NO", (long long)first, (after != before) ? "yes" : "NO", (int)lib.size());
}

// ---------------------------------------------------------------------------------------

static void draw()
{
	probe();

	int skipped = 0;
	const std::vector<Sequence> lib = libraryScan(&skipped);
	tasTextColored(TAS_ACCENT, "%d snippet%s", (int)lib.size(), lib.size() == 1 ? "" : "s");
	ImGui::SameLine();
	if (skipped > 0)
		tasTextColored(TAS_WRITE, "(%d unreadable)", skipped);
	else
		tasTextDisabled("in %s", libraryDir().c_str());

	const Selection& sel = selection();
	if (sel.empty())
		tasTextDisabled("Select rows in the Piano Roll to place a snippet there.");
	else
		tasTextColored(TAS_READ, "target: %u..%u (%d rows)", sel.lo(), sel.hi(), (int)sel.count());

	ImGui::Separator();
	if (ImGui::BeginChild("##sniplist", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), true))
	{
		for (int i = 0; i < (int)lib.size(); i++)
		{
			const Sequence& s = lib[i];
			char lbl[192];
			snprintf(lbl, sizeof(lbl), "%s   (%zu)##snip%d", s.name.empty() ? s.file.c_str() : s.name.c_str(), s.length(), i);
			if (ImGui::Selectable(lbl, selectedRow == i))
			{
				selectedRow = i;
				snprintf(renameBuf, sizeof(renameBuf), "%s", s.name.c_str());
				std::string tj; for (const auto& t : s.tags) { if (!tj.empty()) tj += ", "; tj += t; }
				snprintf(tagsBuf, sizeof(tagsBuf), "%s", tj.c_str());
			}
			if (!s.tags.empty())
			{
				ImGui::SameLine();
				std::string tj; for (const auto& t : s.tags) { if (!tj.empty()) tj += " "; tj += "#" + t; }
				tasTextDisabled("%s", tj.c_str());
			}
		}
	}
	ImGui::EndChild();

	if (selectedRow < 0 || selectedRow >= (int)lib.size())
	{
		tasTextDisabled("Pick a snippet.");
		return;
	}
	const Sequence& s = lib[selectedRow];

	// Place - the two gestures, disabled without a target selection.
	ImGui::BeginDisabled(sel.empty() || !movie::authored());
	if (tasButton("Place (overdub)"))
		place(s, true);
	ImGui::SameLine();
	if (tasButton("Place (replace)"))
		place(s, false);
	ImGui::EndDisabled();
	if (sel.empty())
		tasTip("Select target rows in the Piano Roll first.");

	// Rename / retag (metadata-only; the body is preserved byte-for-byte).
	ImGui::Separator();
	tasTextDisabled("Name");
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputText("##sniprename", renameBuf, sizeof(renameBuf));
	tasTextDisabled("Tags (comma-separated)");
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputText("##sniptags", tagsBuf, sizeof(tagsBuf));
	if (tasButton("Save name/tags"))
	{
		std::vector<std::string> tv;
		{
			std::string cur; for (const char *p = tagsBuf; ; p++)
			{
				if (*p == ',' || *p == '\0') { size_t a = cur.find_first_not_of(" \t"); if (a != std::string::npos) tv.push_back(cur.substr(a, cur.find_last_not_of(" \t") - a + 1)); cur.clear(); if (*p == '\0') break; }
				else cur += *p;
			}
		}
		std::string err;
		const std::string path = libraryDir() + "/" + s.file;
		if (libraryRetag(path, renameBuf, tv, err))
			NOTICE_LOG(RENDERER, "SNIPPET: retagged %s", s.file.c_str());
		else
			gui_display_notification(err.empty() ? "Retag failed" : err.c_str(), 3000);
	}
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.40f, 1.f));
	const bool delMenu = ImGui::BeginMenu("Delete");
	ImGui::PopStyleColor();
	if (delMenu)
	{
		if (tasMenuItem("Confirm - delete this snippet"))
		{
			std::string err;
			if (libraryDelete(libraryDir() + "/" + s.file, err))
			{
				NOTICE_LOG(RENDERER, "SNIPPET: deleted %s", s.file.c_str());
				selectedRow = -1;
			}
			else
				gui_display_notification(err.empty() ? "Delete failed" : err.c_str(), 3000);
		}
		ImGui::EndMenu();
	}
}

}	// namespace snippets

void registerSnippetsPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "snippets", "Snippets", &snippets::snippetsOpen, snippets::draw, panels::Menu,
			/*persist*/ true, /*defW*/ 460.f, /*defH*/ 520.f });
	NOTICE_LOG(RENDERER, "SNIPPETS PANEL: registered=%s open=%s",
			panels::find("snippets") != nullptr ? "yes" : "NO", snippets::snippetsOpen ? "yes" : "no");
}

}	// namespace roll
