#include <ctime>
#include "ui_text.h"
#include "dojo.h"
#include "mvc2.h"
#include "tas_branch.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <string>

/*
	THE TIMELINE HUD - the one glanceable status line: where you are, what mode you
	are in, which branch you are editing.

	`[PORTED 2026-09-15]` from the fork's Timeline window (his dojo_gui.cpp:23209).
	The earlier port-gap notes called this "tangled", and the WINDOW was - it pulls in
	tasDriverBanner / tasWindowUiZoom / tasFolderIcon / tasPreviewGhostFlags, his GUI
	chrome this tree does not have. But its CONTENT is reads we already do: input_viz
	renders the same frame counter, the same READ/READ-WRITE/WRITE tag, and the same
	`scene N skip C/R` off tas_mvc2::read(). So this is the compact overlay of that
	content, minus the chrome - and it adds the one thing input_viz does not show: the
	BRANCH CHIP, which timeline you are on. Drawn with stock ImGui, like the rest.

	It reads, it never writes. The mode/branch strings are pure functions of state, so
	selfTest() covers the part that could be wrong without a frame or a ROM.
*/
namespace roll {
namespace timeline {

//! The 3-way session mode as its label, the exact discriminator input_viz and the
//! branch chip use: READ = the movie drives; else macro_armed ? READ-WRITE : WRITE.
static const char *modeLabel(bool playMatch, bool macroArmed)
{
	if (playMatch)
		return "READ";
	return macroArmed ? "READ-WRITE" : "WRITE";
}

//! THE MACRO-SAVE STAMP (lifted from David's dojo_gui.cpp:25975-25997, 2026-09-17; docs/PORT-
//! DEFECT-CENSUS.md #10 - the atomics existed so "a failed write can never hide again", and
//! nothing read them). Pure: what the stamp SAYS from the session's save facts.
//!   res < 0            -> "SAVE FAILED"          (red)   - the last WriteMacroFile could not open the file
//!   pending && paused  -> "unsaved"              (amber) - an edit the file does not have yet; only a PAUSED
//!                                                        pending edit is worth a flag (running, the file is
//!                                                        behind by definition)
//!   res > 0            -> "saved HH:MM (N fr)"   (dim)
//!   else               -> ""                     (never written yet)
//! `kind` is 0 none / 1 saved / 2 unsaved / 3 failed, for the colour and the selftest.
static std::string macroSaveStamp(int res, bool pending, bool paused, double when, u32 frames, int *kind)
{
	char sv[64] = "";
	int k = 0;
	if (res < 0)
	{
		snprintf(sv, sizeof(sv), "SAVE FAILED"); k = 3;
	}
	else if (pending && paused)
	{
		snprintf(sv, sizeof(sv), "unsaved"); k = 2;
	}
	else if (res > 0)
	{
		const time_t t = (time_t)when;
		struct tm lt;
		localtime_r(&t, &lt);
		snprintf(sv, sizeof(sv), "saved %02d:%02d (%u fr)", lt.tm_hour, lt.tm_min, frames); k = 1;
	}
	if (kind) *kind = k;
	return sv;
}

//! Which timeline the live head is on: a branch folder's id, else "main". The clip's
//! own folder (savestateFolderOverride) is the head; a branch sits under branches/.
static std::string branchName(const std::string& headDir)
{
	if (headDir.empty())
		return "-";
	if (tas_branch::isBranchDir(headDir))
		return ghc::filesystem::path(headDir).filename().string();
	return "main";
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "TIMELINE SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	// The 3-way mode, all four reachable combinations (READ ignores macro_armed).
	claim("play_match => READ", std::string(modeLabel(true, false)) == "READ");
	claim("play_match => READ even armed", std::string(modeLabel(true, true)) == "READ");
	claim("!play_match + armed => READ-WRITE", std::string(modeLabel(false, true)) == "READ-WRITE");
	claim("!play_match + !armed => WRITE", std::string(modeLabel(false, false)) == "WRITE");
	// The branch chip.
	claim("a branch folder reads its id", branchName("/x/clip/branches/2026_state_0_01") == "2026_state_0_01");
	claim("the clip root reads main", branchName("/x/clip") == "main");
	claim("an empty head reads -", branchName("") == "-");
	int k = -1;
	claim("macro stamp: a failed write says SAVE FAILED, whatever else is true", macroSaveStamp(-1, true, true, 0, 0, &k) == "SAVE FAILED" && k == 3);
	claim("macro stamp: a paused pending edit is unsaved", macroSaveStamp(1, true, true, 0, 9, &k) == "unsaved" && k == 2);
	claim("macro stamp: a RUNNING pending edit is not flagged (the file is behind by definition)", macroSaveStamp(1, true, false, 0, 9, &k).rfind("saved ", 0) == 0 && k == 1);
	claim("macro stamp: a successful write carries the frame count", macroSaveStamp(1, false, true, 0, 1234, &k).find("(1234 fr)") != std::string::npos && k == 1);
	claim("macro stamp: never written says nothing", macroSaveStamp(0, false, true, 0, 0, &k).empty() && k == 0);
	NOTICE_LOG(RENDERER, "TIMELINE SELFTEST: %d passed, %d failed", pass, fail);
}

static bool timelineOpen = false;

static void draw()
{
	// WHERE YOU ARE - the number you glance at most. On playback the total is known;
	// while recording there is no total yet, so it is a running count.
	const u32 frame = dojo.frame_number.load();
	const u32 total = (u32)dojo.session_inputs.size();
	ImGui::SetWindowFontScale(1.3f);
	ImGui::PushStyleColor(ImGuiCol_Text, TAS_ACTIVE_COL);
	if (dojo.play_match && total > 0)
		tasText("Frame %u / %u", frame, total);
	else
		tasText("Frame %u", frame);
	ImGui::PopStyleColor();
	ImGui::SetWindowFontScale(1.0f);

	// MODE - the true 3-way (input_viz's discriminator).
	ImGui::SameLine();
	const char *mode = modeLabel(dojo.play_match, dojo.macro_armed);
	const ImVec4 mc = dojo.play_match ? TAS_READ : (dojo.macro_armed ? TAS_READWRITE : TAS_WRITE);
	tasTextColored(mc, "[%s]", mode);

	// BRANCH CHIP - which timeline you are editing (input_viz does not show this).
	ImGui::SameLine(0, 14.f);
	const std::string br = branchName(hostfs::savestateFolderOverride);
	tasTextColored(TAS_BRANCH, "\xEF\x90\x86 %s", br.c_str());	// a small glyph + the branch id

	// MACRO-SAVE STAMP - a macro session's file is its product; say whether the file has it.
	if (cfgLoadBool("dojo", "MacroMode", false))
	{
		int kind = 0;
		const std::string sv = macroSaveStamp(dojo.macro_save_result.load(std::memory_order_relaxed),
				dojo.macro_save_pending.load(std::memory_order_relaxed), gui_state == GuiState::Paused,
				dojo.macro_save_time.load(std::memory_order_relaxed), dojo.macro_save_frames.load(std::memory_order_relaxed), &kind);
		if (!sv.empty())
		{
			ImGui::SameLine(0, 14.f);
			const ImVec4 col = kind == 3 ? ImVec4(0.96f, 0.36f, 0.30f, 1.f) : kind == 2 ? ImVec4(0.96f, 0.76f, 0.26f, 1.f) : TAS_DIM;
			tasTextColored(col, "%s", sv.c_str());
		}
	}

	// SCENE / SKIP clocks, when the MvC2 map has validated - mirrored from Input Viz.
	if (tas_mvc2::mapValidated())
	{
		const tas_mvc2::GameState gs = tas_mvc2::read();
		ImGui::SameLine(0, 14.f);
		tasTextColored(TAS_DIM, "scene %u  skip %u/%u", gs.sceneFrame, gs.skipCount, gs.skipRate);
	}
}

}	// namespace timeline

void registerTimelinePanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "timeline", "Timeline", &timeline::timelineOpen, timeline::draw, panels::Both,
			/*persist*/ true, /*defW*/ 360.f, /*defH*/ 64.f });
	NOTICE_LOG(RENDERER, "TIMELINE PANEL: registered=%s open=%s",
			panels::find("timeline") != nullptr ? "yes" : "NO", timeline::timelineOpen ? "yes" : "no");
}

}	// namespace roll
