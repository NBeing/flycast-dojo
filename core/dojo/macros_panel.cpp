#include "ui_text.h"
#include "roll_library.h"
#include "roll_select.h"
#include "roll_pattern.h"
#include "roll_edit.h"
#include "tas_colors.h"
#include "dojo.h"
#include "movie.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "stdclass.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

/*
	THE MACROS BROWSER - saved clip combos, across every clip folder, as a panel.

	`[PORTED 2026-09-15]` from the fork's Macros window (his movieMacros* substrate +
	window, dojo_gui.cpp:9704/12612). The second half of the Macros+Snippets pair, and
	the same "engine's here, window's missing" shape as Snippets: a macro .txt is just
	another sequence file, and roll_library::libraryRead already turns ANY .txt (via
	tas_macro::FromText) into a Sequence. So PLACING a macro is the identical path the
	Snippets panel proved - libraryRead -> patternReplacing/Overdubbing -> applyPattern
	-> Dojo::ApplyEdit. What Snippets did NOT need and this does is the SCANNER: macros
	live in clip folders (<game>/replays/<clip>/*.txt), not the one snippets library,
	so this walks the game's replays dir. His movieMacrosScan carried a lot of metadata
	(tags, pngs, generations) via GUI helpers this tree lacks; scanMacros() keeps the
	load-bearing facts (clip, file, has-State0) and draws with stock ImGui.

	FULL LOAD vs PLACE, the two things you do with a clip macro:
	  - PLACE drops it at the Piano Roll selection through the edit funnel (undoable).
	  - LOAD FULL (Dojo::LoadMacroFull) re-homes the session on the macro's clip and
	    plays it from State 0 - the "open this combo" gesture; it takes effect on the
	    next boot handoff, as its engine comment says.
*/
namespace roll {
namespace macros {

static bool macrosOpen = false;
static int selectedRow = -1;

struct MacroFile
{
	std::string clip;		// clip folder name
	std::string file;		// macro .txt filename
	std::string path;		// full path
	std::string clipDir;	// clip folder
	u32 frames = 0;
	bool hasState0 = false;
};

//! The game's replays root - the same derivation replay.cpp uses.
static std::string replaysRoot()
{
	return (ghc::filesystem::path(get_writable_data_path("replays")) / get_game_name()).string();
}

static std::map<u32, Row> wholeMovie()
{
	std::map<u32, Row> m;
	for (const auto& kv : dojo.session_inputs)
		m[kv.first] = kv.second;
	return m;
}

//! Walk every clip folder for real macro .txt files (a valid Sequence, not a movie
//! export). File is the index, like libraryScan - nothing to fall out of step with.
static std::vector<MacroFile> scanMacros()
{
	std::vector<MacroFile> out;
	std::error_code ec;
	const std::string root = replaysRoot();
	if (root.empty() || !ghc::filesystem::exists(root, ec))
		return out;
	std::string stateBase = get_file_basename(settings.content.fileName);
	for (const auto& clipIt : ghc::filesystem::directory_iterator(root, ec))
	{
		if (!clipIt.is_directory(ec))
			continue;
		const std::string clipName = clipIt.path().filename().string();
		const bool hasS0 = !stateBase.empty() && ghc::filesystem::exists(clipIt.path() / (stateBase + ".state"), ec);
		for (const auto& f : ghc::filesystem::directory_iterator(clipIt.path(), ec))
		{
			if (f.is_directory(ec))
				continue;
			const std::string fn = f.path().filename().string();
			if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".txt")
				continue;
			if (fn.size() >= 8 && fn.substr(fn.size() - 8) == ".tas.txt")
				continue;		// whole-movie exports, not combos
			Sequence seq;
			std::string err;
			if (!libraryRead(f.path().string(), seq, err) || seq.empty())
				continue;		// only .txt files that are real macros
			MacroFile mf;
			mf.clip = clipName;
			mf.file = fn;
			mf.path = f.path().string();
			mf.clipDir = clipIt.path().string();
			mf.frames = (u32)seq.length();
			mf.hasState0 = hasS0;
			out.push_back(std::move(mf));
		}
	}
	std::sort(out.begin(), out.end(), [](const MacroFile& a, const MacroFile& b) {
		return a.clip != b.clip ? a.clip < b.clip : a.file < b.file;
	});
	return out;
}

//! Place a macro FILE at the roll selection - libraryRead -> Sequence -> the snippets
//! place path. Returns the first changed frame, or -1. Shared by the button and probe.
static s64 place(const MacroFile& mf, bool overdub)
{
	if (!movie::authored())
		return -1;
	const Selection& sel = selection();
	if (sel.empty())
		return -1;
	Sequence s;
	std::string err;
	if (!libraryRead(mf.path, s, err) || s.empty())
		return -1;
	const Pattern pat = overdub ? patternOverdubbing(s) : patternReplacing(s);
	Edit e = applyPattern(wholeMovie(), sel.lo(), sel.hi(), pat, 0);
	const s64 first = dojo.ApplyEdit(e, overdub ? "macro: overdub" : "macro: replace");
	NOTICE_LOG(RENDERER, "MACRO: placed '%s/%s' (%zu frames, %s) at %u..%u -> first changed %lld",
			mf.clip.c_str(), mf.file.c_str(), s.length(), overdub ? "overdub" : "replace",
			sel.lo(), sel.hi(), (long long)first);
	return first;
}

// ---------------------------------------------------------------------------------------
// INTEGRATION PROBE, dojo:MacrosProbe=yes - the twin of SnippetProbe. Self-seeds a macro
// by writing the live movie to the bound clip (Dojo::WriteMacroFile), scans the replays
// root, places the first macro at a mid-movie selection through place(), reports the result.
// ---------------------------------------------------------------------------------------
static void probe()
{
	static bool done = false;
	if (done || !cfgLoadBool("dojo", "MacrosProbe", false) || !movie::authored())
		return;
	done = true;
	std::vector<MacroFile> macs = scanMacros();
	if (macs.empty())
	{
		// self-seed: write the current movie as this clip's macro.txt, then rescan.
		dojo.WriteMacroFile();
		macs = scanMacros();
	}
	NOTICE_LOG(RENDERER, "MACROS PROBE: found %d macro file(s) under %s", (int)macs.size(), replaysRoot().c_str());
	if (macs.empty())
	{
		NOTICE_LOG(RENDERER, "MACROS PROBE RESULT: placed=NO reason=no-macros");
		return;
	}
	const u32 mid = movie::end() > 40 ? movie::end() - 40 : 0;
	const size_t len = std::max<size_t>(1, (size_t)macs.front().frames);
	selection().clear();
	selection().press(mid, Mods{});
	if (len > 1)
		selection().press(mid + (u32)std::min<size_t>(len, 8) - 1, Mods{true, false, false});
	const Row before = [&]{ auto it = dojo.session_inputs.find(mid); return it == dojo.session_inputs.end() ? Row() : it->second; }();
	const s64 first = place(macs.front(), /*overdub*/ false);
	const Row after = [&]{ auto it = dojo.session_inputs.find(mid); return it == dojo.session_inputs.end() ? Row() : it->second; }();
	NOTICE_LOG(RENDERER, "MACROS PROBE RESULT: placed=%s first=%lld rowChanged=%s count=%d",
			first >= 0 ? "yes" : "NO", (long long)first, (after != before) ? "yes" : "NO", (int)macs.size());
}

// ---------------------------------------------------------------------------------------

static void draw()
{
	probe();

	const std::vector<MacroFile> macs = scanMacros();
	tasTextColored(TAS_ACCENT, "%d macro%s", (int)macs.size(), macs.size() == 1 ? "" : "s");
	ImGui::SameLine();
	tasTextDisabled("across the clips in %s", replaysRoot().c_str());

	const Selection& sel = selection();
	if (sel.empty())
		tasTextDisabled("Select rows in the Piano Roll to place a macro there.");
	else
		tasTextColored(TAS_READ, "target: %u..%u (%d rows)", sel.lo(), sel.hi(), (int)sel.count());

	ImGui::Separator();
	if (ImGui::BeginChild("##maclist", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), true))
	{
		for (int i = 0; i < (int)macs.size(); i++)
		{
			const MacroFile& mf = macs[i];
			char lbl[256];
			snprintf(lbl, sizeof(lbl), "%s   (%u)##mac%d", mf.file.c_str(), mf.frames, i);
			if (ImGui::Selectable(lbl, selectedRow == i))
				selectedRow = i;
			ImGui::SameLine();
			tasTextDisabled("%s%s", mf.clip.c_str(), mf.hasState0 ? "  [S0]" : "");
		}
	}
	ImGui::EndChild();

	if (selectedRow < 0 || selectedRow >= (int)macs.size())
	{
		tasTextDisabled("Pick a macro.");
		return;
	}
	const MacroFile& mf = macs[selectedRow];

	ImGui::BeginDisabled(sel.empty() || !movie::authored());
	if (tasButton("Place (overdub)"))
		place(mf, true);
	ImGui::SameLine();
	if (tasButton("Place (replace)"))
		place(mf, false);
	ImGui::EndDisabled();
	if (sel.empty())
		tasTip("Select target rows in the Piano Roll first.");

	// Full load: re-home the session on this macro's clip and play from State 0. Takes
	// effect on the next boot handoff (its engine comment). Only with a State 0 to anchor.
	ImGui::BeginDisabled(!mf.hasState0);
	if (tasButton("Load full (from State 0)"))
	{
		if (dojo.LoadMacroFull(mf.clipDir, mf.path))
			gui_display_notification("Macro armed - it plays from State 0 on the next boot", 4000);
	}
	ImGui::EndDisabled();
	if (!mf.hasState0)
		tasTip("This clip has no State 0, so a Full load has no anchor - use Place instead.");
}

}	// namespace macros

void registerMacrosPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "macros", "Macros", &macros::macrosOpen, macros::draw, panels::Menu,
			/*persist*/ true, /*defW*/ 480.f, /*defH*/ 520.f });
	NOTICE_LOG(RENDERER, "MACROS PANEL: registered=%s open=%s",
			panels::find("macros") != nullptr ? "yes" : "NO", macros::macrosOpen ? "yes" : "no");
}

}	// namespace roll
