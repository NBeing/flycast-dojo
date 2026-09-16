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
