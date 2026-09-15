#include "ui_text.h"
#include "tas_branch.h"
#include "roll_host.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "rend/video_recorder.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "stdclass.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

/*
	CAPTURES - record what is on screen, and find the file afterwards.

	`[PORTED 2026-09-14]` from the fork's `tasCapturesWindow` (155 lines), as a
	PANEL FOR OUR RECORDER rather than a transliteration. CLAUDE.md is explicit
	that the capture stack here is ours: his `avi_dump.cpp` sits in this tree
	unbuilt, and `core/rend/video_recorder.{cpp,h}` is what runs. So every
	setting this panel edits is a `record:` key the recorder already reads at
	start(), and every action is `videorec::requestStart/requestStop`. No second
	owner of "how a capture is configured".

	`[CORRECTED 2026-09-14]` THIS WINDOW WAS CALLED BLOCKED ON EIGHT tas_branch
	SYMBOLS. It uses ONE: `tas_branch::rootOf`, to resolve whatever folder is
	bound (a branch dir, or the clip) back to the clip, whose folder name is the
	UTC stamp the captures directory is keyed on. With no branches in play,
	rootOf is the identity - which the branch self-test asserts - so this panel
	would have worked without the engine at all.

	NOT IN THIS CUT, with reasons rather than for time:
	- The "Capture / Export" branch-export machine (bx*, ~230 lines): it drives
	  gui_branch_checkout, which is the Branches window's job and lands with it.
	- "Play" (shells out to the OS video player) and "Open folder".
	- "Hide studio while recording" / "Capture paused frames": both are avi_dump
	  policies. Our recorder is constant-frame-rate BY CONSTRUCTION ("one
	  submitted frame is always one frame in the file", video_recorder.h), so
	  the second is not a setting here, it is the behaviour.
*/
namespace roll
{
namespace captures
{

//! Extensions the recorder can produce or a user might drop in beside them.
bool isCaptureFile(const std::string& ext)
{
	return ext == ".avi" || ext == ".mov" || ext == ".mp4" || ext == ".mkv";
}

/*
	The clip's UTC stamp - the folder name of the ROOT clip - or empty when
	nothing is bound. Empty is a real answer the caller must render: "no clip"
	and "a clip with no captures yet" are different states.
*/
std::string clipStamp(const std::string& boundFolder)
{
	if (boundFolder.empty())
		return std::string();
	return ghc::filesystem::path(tas_branch::rootOf(boundFolder)).filename().string();
}

//! Where a clip's captures live. Under the data path, beside the recorder's
//! own default location, so a user looking for one finds the other.
std::string captureDir(const std::string& stamp)
{
	if (stamp.empty())
		return std::string();
	return get_writable_data_path("captures/" + stamp);
}

//! The file stem for a manual capture taken at `t`: record_HHMMSS. The
//! recorder's own default is a full date stamp because it is global; inside a
//! per-clip folder the date is already in the folder name.
std::string manualStem(time_t t)
{
	std::tm tm_{};
#ifdef _WIN32
	localtime_s(&tm_, &t);
#else
	localtime_r(&t, &tm_);
#endif
	char stem[32];
	std::strftime(stem, sizeof(stem), "record_%H%M%S", &tm_);
	return stem;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "CAPTURES SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	claim("the recorder's own container is a capture file", isCaptureFile(".avi"));
	claim("the fork's containers are too", isCaptureFile(".mov") && isCaptureFile(".mp4"));
	// THE CONTROL: the listing must not sweep up the recorder's own temp
	// files, which sit beside the output while a capture is muxing.
	claim("...but a .pcm audio temp is not", !isCaptureFile(".pcm"));
	claim("...and neither is a sidecar", !isCaptureFile(".json") && !isCaptureFile(""));

	claim("no bound folder means no clip stamp", clipStamp("").empty());
	const std::string clip = "/tmp/replays/G/2026-09-14T00_00_00Z";
	claim("a plain clip's stamp is its folder name",
			clipStamp(clip) == "2026-09-14T00_00_00Z");
	// THE ONE tas_branch DEPENDENCY, exercised: a branch head must resolve to
	// the ROOT clip's stamp, or a capture taken on a branch lands in a folder
	// named after the branch and is never found again beside main's.
	claim("a branch head's stamp is the ROOT clip's, not the branch's",
			clipStamp(clip + "/branches/2026-09-14T01_00_00Z_state_3_01")
				== "2026-09-14T00_00_00Z");
	claim("no stamp means no capture dir", captureDir("").empty());
	claim("a stamp's capture dir ends in captures/<stamp>",
			[&]{ const std::string d = captureDir("X"); return d.size() >= 10
			     && d.compare(d.size() - 10, 10, "captures/X") == 0; }());

	// A fixed instant, so the claim is about the FORMAT and not the clock.
	std::tm t{}; t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 14;
	t.tm_hour = 13; t.tm_min = 5; t.tm_sec = 9; t.tm_isdst = -1;
	claim("a manual stem is record_HHMMSS", manualStem(mktime(&t)) == "record_130509");

	NOTICE_LOG(RENDERER, "CAPTURES SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The panel.
// ---------------------------------------------------------------------------------------

static bool capturesOpen = false;

struct Cap { std::string name; u64 bytes; };
static std::vector<Cap> files;
static std::string scannedDir;
static double scannedAt = 0;

static void rescan(const std::string& dir, bool force)
{
	const double now = os_GetSeconds();
	if (!force && scannedDir == dir && now - scannedAt <= 2.0)
		return;
	files.clear();
	scannedDir = dir;
	scannedAt = now;
	std::error_code ec;
	if (!dir.empty() && ghc::filesystem::is_directory(dir, ec))
		for (const auto& e : ghc::filesystem::directory_iterator(dir, ec))
		{
			if (!e.is_regular_file(ec))
				continue;
			if (isCaptureFile(e.path().extension().string()))
				files.push_back({ e.path().filename().string(), (u64)e.file_size(ec) });
		}
	std::sort(files.begin(), files.end(),
			[](const Cap& a, const Cap& b) { return a.name < b.name; });
}

static void draw()
{
	const std::string stamp = clipStamp(hostfs::savestateFolderOverride);
	const std::string dir = captureDir(stamp);

	if (stamp.empty())
		tasTextDisabled("no clip loaded - captures are kept per clip");
	else
		tasTextDisabled("captures/%s", stamp.c_str());
	ImGui::SameLine();
	bool refresh = false;
	if (tasSmallButton("Refresh"))
		refresh = true;
	ImGui::Separator();

	// ---- settings: the recorder's OWN keys, read back each frame -----------------------
	{
		static const char *codecs[] = { "mjpeg", "libx264", "ffv1", "prores_ks" };
		static const char *labels[] = { "MJPEG (fast, large)", "H.264", "FFV1 (lossless)", "ProRes" };
		const std::string codec = cfgLoadStr("record", "codec", "mjpeg");
		int ci = 0;
		for (int k = 0; k < 4; k++)
			if (codec == codecs[k])
				ci = k;
		ImGui::SetNextItemWidth(200.f);
		if (ImGui::BeginCombo("codec", labels[ci]))
		{
			for (int k = 0; k < 4; k++)
				if (ImGui::Selectable(labels[k], ci == k))
					cfgSaveStr("record", "codec", codecs[k]);
			ImGui::EndCombo();
		}
		int fps = cfgLoadInt("record", "fps", 60);
		ImGui::SetNextItemWidth(100.f);
		if (ImGui::InputInt("output fps", &fps))
			cfgSaveInt("record", "fps", std::max(1, std::min(240, fps)));
		ImGui::SameLine();
		tasTextDisabled("%s", cfgLoadInt("record", "fps", 60) >= 60 ? "real-time" : "slow-mo");
		tasTextDisabled("one video frame per emulated frame; pausing adds none");
	}
	ImGui::Separator();

	// ---- record / stop -------------------------------------------------------------
	const bool recording = videorec::isRecording();
	const bool starting = videorec::startPending();
	if (recording)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, TAS_WRITE);
		if (tasButton("Stop recording"))
		{
			videorec::requestStop();
			NOTICE_LOG(RENDERER, "CAPTURES: stop requested");
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
		tasTextColored(TAS_WRITE, "REC  %dx%d", videorec::width(), videorec::height());
	}
	else
	{
		ImGui::BeginDisabled(starting || stamp.empty());
		if (tasButton("Record current"))
		{
			std::error_code ec;
			ghc::filesystem::create_directories(dir, ec);
			const std::string path = dir + "/" + manualStem(time(nullptr)) + ".avi";
			videorec::requestStart(path);
			NOTICE_LOG(RENDERER, "CAPTURES: start requested -> %s", path.c_str());
		}
		ImGui::EndDisabled();
		if (starting)
		{
			ImGui::SameLine();
			// SAID. The recorder opens on the renderer's next frame; between
			// the click and that frame "not recording" is true and misleading.
			tasTextColored(TAS_STAGED, "starting...");
		}
		else if (stamp.empty())
		{
			ImGui::SameLine();
			tasTextDisabled("load a clip first");
		}
	}
	ImGui::Separator();

	// ---- what is in the folder -------------------------------------------------------
	rescan(dir, refresh);
	if (files.empty())
	{
		// NOT AN EMPTY LIST. "nothing captured yet", "no clip" and "the folder
		// does not exist" are three answers and a blank gives none.
		tasTextDisabled(stamp.empty() ? "No clip." : "No captures yet for this clip.");
		return;
	}
	tasTextDisabled("%d file%s", (int)files.size(), files.size() == 1 ? "" : "s");
	if (ImGui::BeginChild("##caplist", ImVec2(0, 0), false))
		for (const Cap& f : files)
		{
			ImGui::PushID(f.name.c_str());
			ImGui::TextUnformatted(f.name.c_str());
			ImGui::SameLine();
			tasTextDisabled("(%.1f MB)", (double)f.bytes / (1024.0 * 1024.0));
			ImGui::PopID();
		}
	ImGui::EndChild();
}

}	// namespace captures

void registerCapturesPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	// BOTH streams: Stop has to be reachable while the game runs, or a capture
	// can only end by pausing - which is itself a frame the file must not lose.
	panels::add({ "captures", "Captures", &captures::capturesOpen, captures::draw,
			panels::Both, /*persist*/ true, /*defW*/ 460.f, /*defH*/ 320.f });
	NOTICE_LOG(RENDERER, "CAPTURES PANEL: registered=%s open=%s",
			panels::find("captures") != nullptr ? "yes" : "NO",
			captures::capturesOpen ? "yes" : "no");
}

}	// namespace roll
