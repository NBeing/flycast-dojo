#include "branch_export.h"
#include "tas_branch.h"
#include "tas_clip.h"
#include "dojo.h"
#include "roll_host.h"
#include "rend/gui.h"
#include "rend/video_recorder.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "stdclass.h"
#include "log/LogManager.h"
#include "deps/filesystem.hpp"
#include <fstream>
#include <set>
#include <string>

/*
	See branch_export.h. Phases follow his numbering so the log reads the same
	on both forks: 1 start-item, 2 settle+arm, 3 playing, 4 done-item, 5 finish.
*/
namespace roll {
namespace bexport {

std::string sanitize(const std::string& s)
{
	std::string out;
	for (char c : s)
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '_')
			out += c;
		else if (c == ' ')
			out += '_';
	}
	if (out.empty())
		out = "branch";
	if (out.size() > 60)
		out.resize(60);
	return out;
}

std::vector<Item> buildCandidates(const std::string& rootDir, const nlohmann::json& branches)
{
	std::vector<Item> out;
	if (rootDir.empty())
		return out;
	out.push_back({ rootDir, "main" });
	std::set<std::string> used;
	used.insert("main");
	if (!branches.is_array())
		return out;
	for (const auto& b : branches)
	{
		if (!b.is_object())
			continue;
		const std::string id = b.value("id", std::string());
		if (id.empty())
			continue;
		std::string label;
		if (b.contains("tags") && b["tags"].is_array() && !b["tags"].empty())
			label = tas_clip::joinTags(b["tags"]);
		else
			label = b.value("name", std::string());
		if (label.empty())
			label = id;
		std::string nm = sanitize(label);
		const std::string base = nm;
		for (int k = 2; used.count(nm) != 0; k++)
			nm = base + "_" + std::to_string(k);
		used.insert(nm);
		out.push_back({ (ghc::filesystem::path(rootDir) / "branches" / id).string(), nm });
	}
	return out;
}

/*
	A <stem>.json beside each capture: what it is a capture OF. His report with
	the recorder's own keys in place of avi_dump's - fps/codec come from
	`record:`, which is what actually produced the file.
*/
static void writeReport(const std::string& capDir, const std::string& stem, const std::string& tag, int fps)
{
	std::error_code ec;
	ghc::filesystem::create_directories(capDir, ec);
	const std::string root = tas_branch::rootOf(hostfs::savestateFolderOverride);
	nlohmann::json rep;
	rep["capturedAt"] = tas_clip::utcNowIso();
	rep["game"] = get_game_name();
	rep["clip"] = ghc::filesystem::path(root).filename().string();
	rep["tag"] = tag;
	rep["movieFrames"] = dojo.MovieEnd();
	nlohmann::json names = nlohmann::json::array();
	for (const auto& b : tas_branch::list(root))
		if (b.is_object())
		{
			const bool hasTags = b.contains("tags") && b["tags"].is_array() && !b["tags"].empty();
			names.push_back(hasTags ? tas_clip::joinTags(b["tags"])
					: b.value("name", b.value("id", std::string())));
		}
	rep["branches"] = names;
	rep["fps"] = fps;
	rep["codec"] = cfgLoadStr("record", "codec", "mjpeg");
	std::ofstream out(capDir + "/" + stem + ".json", std::ios::binary | std::ios::trunc);
	if (out.good())
		out << rep.dump(2);
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "BEXPORT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	claim("safe characters pass through", sanitize("Take-2_final") == "Take-2_final");
	claim("spaces become underscores", sanitize("wall combo") == "wall_combo");
	claim("punctuation is dropped, not escaped", sanitize("a/b:c?") == "abc");
	claim("an empty or all-junk name becomes 'branch'",
			sanitize("") == "branch" && sanitize("///") == "branch");
	claim("a long name is cut at 60", sanitize(std::string(80, 'x')).size() == 60);

	{
		const nlohmann::json none = nlohmann::json::array();
		std::vector<Item> c = buildCandidates("/tmp/clip", none);
		claim("no branches is main alone", c.size() == 1 && c[0].name == "main" && c[0].dir == "/tmp/clip");
		claim("no clip is no candidates at all", buildCandidates("", none).empty());
	}
	{
		nlohmann::json br = nlohmann::json::array();
		br.push_back({ { "id", "id-a" }, { "tags", { "wall combo" } } });
		br.push_back({ { "id", "id-b" }, { "name", "second" } });
		br.push_back({ { "id", "id-c" } });
		br.push_back({ { "id", "id-d" }, { "tags", { "wall combo" } } });	// same tag as the first
		std::vector<Item> c = buildCandidates("/tmp/clip", br);
		claim("main is first, then every branch", c.size() == 5 && c[0].name == "main");
		claim("a branch is named by its tags first", c[1].name == "wall_combo");
		claim("...then by its name", c[2].name == "second");
		claim("...then by its id", c[3].name == "id-c");
		// THE COLLISION: two branches tagged alike must not overwrite each
		// other's video. A de-dup that did nothing satisfies every claim above.
		claim("two branches with the same tag get distinct file names",
				c[4].name == "wall_combo_2" && c[1].name != c[4].name);
		claim("a branch's dir is <root>/branches/<id>",
				c[1].dir == "/tmp/clip/branches/id-a");
	}
	NOTICE_LOG(RENDERER, "BEXPORT SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The run.
// ---------------------------------------------------------------------------------------

static bool g_active = false;
static int g_phase = 0;
static std::vector<Item> g_queue, g_cands;
static std::vector<unsigned char> g_checked;
static size_t g_idx = 0;
static std::string g_origHead, g_capDir;
static int g_settle = 0, g_watchdog = 0, g_maxTicks = 0, g_done = 0, g_fps = 60;
static int g_savedAutoCapture = -1;
static u32 g_lastFrame = 0;
static int g_stall = 0;

void refreshCandidates()
{
	g_cands.clear();
	const std::string head = hostfs::savestateFolderOverride;
	if (head.empty())
	{
		g_checked.clear();
		return;
	}
	const std::string root = tas_branch::rootOf(head);
	g_cands = buildCandidates(root, tas_branch::list(root));
	if (g_checked.size() != g_cands.size())
		g_checked.assign(g_cands.size(), 1);
}

const std::vector<Item>& candidates() { return g_cands; }
std::vector<unsigned char>& checked() { return g_checked; }
bool active() { return g_active; }

std::string progress()
{
	if (!g_active || g_idx >= g_queue.size())
		return std::string();
	char b[128];
	snprintf(b, sizeof(b), "Exporting %u/%u: %s", (u32)(g_idx + 1), (u32)g_queue.size(),
			g_queue[g_idx].name.c_str());
	return b;
}

bool launch()
{
	if (g_active)
		return false;
	refreshCandidates();
	std::vector<Item> q;
	for (size_t i = 0; i < g_cands.size(); i++)
		if (i < g_checked.size() && g_checked[i])
			q.push_back(g_cands[i]);
	if (q.empty())
		return false;
	g_queue = q;
	g_origHead = hostfs::savestateFolderOverride;
	g_capDir = captures::captureDir(captures::clipStamp(g_origHead));
	g_fps = cfgLoadInt("record", "fps", 60);
	// The replay-end hook in dojo.cpp stops the recorder only under
	// dojo:AutoCapture; set it for the run and put it back after.
	g_savedAutoCapture = cfgLoadBool("dojo", "AutoCapture", false) ? 1 : 0;
	cfgSetVirtual("dojo", "AutoCapture", "yes");
	g_idx = 0;
	g_done = 0;
	g_phase = 1;
	g_active = true;
	NOTICE_LOG(NETWORK, "TAS EXPORT: start, %u item(s) -> %s", (u32)g_queue.size(), g_capDir.c_str());
	gui_display_notification("Exporting branches to captures/ ...", 4000);
	return true;
}

static void restoreCfg()
{
	if (g_savedAutoCapture >= 0)
		cfgSetVirtual("dojo", "AutoCapture", g_savedAutoCapture ? "yes" : "no");
	g_savedAutoCapture = -1;
}

void cancel()
{
	if (!g_active)
		return;
	if (videorec::isRecording())
		videorec::requestStop();
	restoreCfg();
	gui_pause_for_checkout();
	if (!g_origHead.empty())
		branch::checkoutFolder(g_origHead, 0, "");
	g_active = false;
	g_phase = 0;
	gui_display_notification("Branch export cancelled", 3000);
	NOTICE_LOG(NETWORK, "TAS EXPORT: cancelled after %d", g_done);
}

void tick()
{
	if (!g_active)
		return;
	switch (g_phase)
	{
	case 1:		// START_ITEM: pause, check out, re-establish replay playback from BASE
	{
		gui_pause_for_checkout();
		const Item& it = g_queue[g_idx];
		if (!branch::checkoutFolder(it.dir, 0, it.name))
		{
			NOTICE_LOG(NETWORK, "TAS EXPORT: checkout failed for '%s' - skipping", it.name.c_str());
			g_phase = 4;
			break;
		}
		dojo.play_match = true;		// play its movie back as a replay (checkout left us in WRITE)
		dojo.macro_armed = false;
		dojo.stepping = false;
		g_maxTicks = (int)dojo.MovieEnd() + 1800;
		g_watchdog = 0;
		g_settle = 6;
		g_phase = 2;
		break;
	}
	case 2:		// SETTLE: arm the named capture once the recorder is idle, then resume
		if (g_settle-- > 0)
			break;
		// The previous item's stop is applied on the renderer's next frame; do
		// not hand it a start that would cancel a pending stop.
		if (videorec::isRecording() || videorec::stopPending())
		{
			if (++g_watchdog > 600)
			{
				NOTICE_LOG(NETWORK, "TAS EXPORT: recorder never went idle before '%s' - skipping",
						g_queue[g_idx].name.c_str());
				g_phase = 4;
			}
			break;
		}
		{
			std::error_code ec;
			ghc::filesystem::create_directories(g_capDir, ec);
			const std::string path = g_capDir + "/" + g_queue[g_idx].name + ".avi";
			writeReport(g_capDir, g_queue[g_idx].name, g_queue[g_idx].name, g_fps);
			videorec::requestStart(path);
			NOTICE_LOG(NETWORK, "TAS EXPORT: '%s' -> %s", g_queue[g_idx].name.c_str(), path.c_str());
		}
		gui_resume_play();
		g_watchdog = 0;
		g_lastFrame = dojo.frame_number.load();
		g_stall = 0;
		g_phase = 3;
		break;
	case 3:		// PLAYING: until the movie ends, or a guard trips
		if (gui_state == GuiState::ReplayEnd)
		{
			g_phase = 4;
			break;
		}
		{
			const u32 fn = dojo.frame_number.load();
			if (fn > g_lastFrame)
			{
				g_lastFrame = fn;
				g_stall = 0;
			}
			else if (++g_stall > 300)
			{
				// ~5 s with the playhead not moving is a stuck item, not
				// backpressure; recording a minute of one frame helps nobody.
				NOTICE_LOG(NETWORK, "TAS EXPORT: '%s' stalled at frame %u - aborting item",
						g_queue[g_idx].name.c_str(), fn);
				g_phase = 4;
				break;
			}
		}
		if (++g_watchdog > g_maxTicks)
		{
			NOTICE_LOG(NETWORK, "TAS EXPORT: watchdog tripped on '%s' at frame %u",
					g_queue[g_idx].name.c_str(), dojo.frame_number.load());
			g_phase = 4;
		}
		break;
	case 4:		// DONE_ITEM: make sure the capture is stopping, advance
		if (videorec::isRecording())
			videorec::requestStop();
		g_done++;
		g_idx++;
		g_phase = (g_idx < g_queue.size()) ? 1 : 5;
		break;
	case 5:		// FINISH: cfg back, head back, paused
	{
		restoreCfg();
		gui_pause_for_checkout();
		if (!g_origHead.empty())
			branch::checkoutFolder(g_origHead, 0, "");
		g_active = false;
		g_phase = 0;
		char m[160];
		snprintf(m, sizeof(m), "Exported %d video%s to captures/", g_done, g_done == 1 ? "" : "s");
		gui_display_notification(m, 6000);
		NOTICE_LOG(NETWORK, "TAS EXPORT: done, %d video(s) -> %s", g_done, g_capDir.c_str());
		break;
	}
	}
}

}	// namespace bexport
}	// namespace roll
