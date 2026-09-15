#include "ui_text.h"
#include "tas_branch.h"
#include "tas_clip.h"
#include "fst.h"
#include "dojo.h"
#include "roll_host.h"
#include "roll_select.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "rend/gui_util.h"
#include "rend/imgui_driver.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include "deps/json/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

/*
	BRANCHES - fork a timeline from a state, work on it, merge it back.

	`[PORTED 2026-09-14]` from the fork's `tasBranchesWindow` + `tasBranchPropsBody`
	(~940 lines), WITHOUT `imgui-node-editor`. That library was called the blocker
	on this window for a week, and the closure survey found it is not one:

	  - the graph is a DEPTH-1 STAR by design ("every branch forks from main; no
	    branch-of-branch", tas_branch.h). A star has no layout problem: position
	    is a function of two integers, which his code already computes by hand.
	  - the library's own persistence is explicitly DISABLED in his window
	    (`cfg.SettingsFile = nullptr`); he writes his own `.nodelayout.json`.
	  - its editing subsystem is 100% unused - zero occurrences of
	    BeginCreate / BeginDelete / QueryNewLink. Edges are derived from
	    `fromSlot` every frame; the user cannot make or break one.
	  - six of its 46 call sites are Suspend/Resume, there only to undo the
	    library's own zoom transform so tooltips land on screen.
	  - and he already re-implemented this exact star with ImDrawList: the
	    minimap in the same file does its own bounds fitting and click picking.

	So the graph below is rectangles, text, bezier edges and hit-testing on
	stock ImGui, at the version both trees already share. What that gives up is
	smooth zoom; a star with a handful of nodes gets fit-and-pan instead, and
	the whole class of "tooltip flies off-screen" bugs never exists.

	THE DEPTH-1 INVARIANT IS ENFORCED HERE, NOT IN THE ENGINE. tas_branch.h says
	so in its port note: create() handed a branch dir will make
	branches/x/branches/y. The Create row below refuses on a branch, as his
	States row menu does (`canCreate = s.exists && !onBranch`).

	NOT IN THIS CUT: the minimap (fit-and-pan makes it decorative), the edge
	"flow" pulse on checkout, per-branch video export (drives Captures and lands
	with it), and "Open folder" (shells out).
*/
namespace roll
{
namespace branch
{

// ---------------------------------------------------------------------------------------
// The pure half: colours, layout and text. No ImGui, so the arm can drive it.
// ---------------------------------------------------------------------------------------

/*
	Ten hues, so a branch is auto-coloured by the state it forked from and every
	branch off one state shares a colour. His values, verbatim: a user's existing
	`color` overrides in clip.json are "#RRGGBB" strings and must keep meaning
	the same thing on both forks.
*/
static const char *const kStateColors[10] = {
	"#e0524f", "#e0913a", "#e0cf40", "#8fd04a", "#5fd07a",
	"#3ab0c0", "#5a86e0", "#8a6be0", "#c060d0", "#e06aa0",
};

const char *autoStateColor(int slot)
{
	return kStateColors[((slot % 10) + 10) % 10];
}

//! "#RRGGBB" -> colour; anything else, including empty, -> alpha 0 = "no colour set".
ImVec4 hexColor(const std::string& hex)
{
	if (hex.size() != 7 || hex[0] != '#')
		return ImVec4(0, 0, 0, 0);
	auto nib = [](char c) -> int {
		return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
				: (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0;
	};
	auto byte = [&](int i) { return (nib(hex[i]) << 4) | nib(hex[i + 1]); };
	return ImVec4(byte(1) / 255.f, byte(3) / 255.f, byte(5) / 255.f, 1.f);
}

std::string statesLine(const std::vector<int>& slots)
{
	if (slots.empty())
		return "-";
	std::string s;
	for (int v : slots)
	{
		if (!s.empty())
			s += ' ';
		s += std::to_string(v);
	}
	return s;
}

/*
	THE LAYOUT, as arithmetic. Columns are the DISTINCT fork slots in ascending
	order - ordinal, so slots 2, 5, 9 occupy columns 0, 1, 2 with no gaps for
	the slots nobody forked from, and a higher state still sits further right.
	Rows stack within a column in node order. main sits at the left, centred
	against the tallest column.
*/
struct Layout
{
	std::map<int, int> colOf;		//!< fork slot -> column index
	std::map<int, int> countIn;		//!< fork slot -> nodes in that column
	int maxRows = 1;
};

Layout layoutFor(const std::vector<int>& forkSlots)
{
	Layout L;
	for (int s : forkSlots)
		L.countIn[s]++;
	int ci = 0;
	for (const auto& kv : L.countIn)	// std::map is ascending
	{
		L.colOf[kv.first] = ci++;
		L.maxRows = std::max(L.maxRows, kv.second);
	}
	return L;
}

static const float kNodeW = 214.f, kRowH = 210.f, kColX0 = 300.f, kColW = kNodeW + 60.f;
static const float kTop = 30.f, kGrpInset = 14.f;

ImVec2 autoPos(int col, int row)
{
	return ImVec2(kColX0 + col * kColW, kTop + row * kRowH);
}

ImVec2 mainPos(int maxRows)
{
	return ImVec2(30.f, kTop + (maxRows - 1) * kRowH * 0.5f);
}

void panelSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "BRANCHPANEL SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- colours ---------------------------------------------------------------------
	{
		const ImVec4 c = hexColor("#e0524f");
		claim("a hex colour parses to its channels",
				c.w == 1.f && (int)(c.x * 255.f + 0.5f) == 0xe0 && (int)(c.y * 255.f + 0.5f) == 0x52
				&& (int)(c.z * 255.f + 0.5f) == 0x4f);
		claim("upper-case hex parses the same", hexColor("#E0524F").x == c.x);
		claim("no colour set is alpha 0", hexColor("").w == 0.f);
		claim("a malformed colour is alpha 0, not garbage",
				hexColor("e0524f").w == 0.f && hexColor("#e05").w == 0.f);
		claim("auto colour wraps at ten", std::string(autoStateColor(10)) == autoStateColor(0));
		// A negative slot is a "not a slot" sentinel in a few places; it must
		// not index before the table.
		claim("...and a negative slot does not read before the table",
				std::string(autoStateColor(-1)) == autoStateColor(9));
	}

	// ---- layout ----------------------------------------------------------------------
	{
		const Layout L = layoutFor({ 5, 2, 9, 5 });
		claim("distinct fork slots become ORDINAL columns, ascending",
				L.colOf.at(2) == 0 && L.colOf.at(5) == 1 && L.colOf.at(9) == 2);
		claim("...with no gaps for slots nobody forked from", L.colOf.size() == 3);
		claim("two branches off one slot share a column", L.countIn.at(5) == 2);
		claim("the tallest column sets maxRows", L.maxRows == 2);
		claim("a column's rows stack down by rowH",
				autoPos(1, 1).y - autoPos(1, 0).y == kRowH && autoPos(1, 1).x == autoPos(1, 0).x);
		claim("columns advance right by more than a node's width",
				autoPos(1, 0).x - autoPos(0, 0).x > kNodeW);
		claim("main sits left of every column", mainPos(2).x < autoPos(0, 0).x);
		claim("main is centred on a two-row column",
				mainPos(2).y == (autoPos(0, 0).y + autoPos(0, 1).y) * 0.5f);
		const Layout E = layoutFor({});
		claim("no branches is no columns and one row", E.colOf.empty() && E.maxRows == 1);
	}

	claim("an empty state list reads as '-'", statesLine({}) == "-");
	claim("states read as space-separated slots", statesLine({ 0, 3, 7 }) == "0 3 7");

	NOTICE_LOG(RENDERER, "BRANCHPANEL SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The graph data - read ONCE per change, never per frame.
// ---------------------------------------------------------------------------------------

struct Node
{
	std::string id, label, color, thumbPath, thumbFork;
	int fromSlot = 0;
	u32 atFrame = 0;
	bool mismatch = false;
	std::string mismatchReason;
	std::vector<int> states;
};

struct Graph
{
	bool valid = false;
	std::string rootDir, headId = "main", mainThumbPath, mainLabel = "main";
	bool onBranch = false;
	std::vector<int> mainStates;
	std::vector<Node> nodes;
};

static std::string highestThumb(const nlohmann::json& clip, const std::string& dir)
{
	int best = -1;
	std::string thumb;
	if (clip.contains("states") && clip["states"].is_array())
		for (const auto& s : clip["states"])
			if (s.is_object() && s.contains("thumb") && s.value("slot", -1) > best)
			{
				best = s.value("slot", -1);
				thumb = s.value("thumb", std::string());
			}
	return thumb.empty() ? std::string() : (dir + "/" + thumb);
}

//! Cached on (bound folder, tas_clip version). create / checkout / merge / a
//! state save all bump the version, so a change is seen next frame and a
//! quiet frame costs two string compares.
static const Graph& graph()
{
	static Graph d;
	static std::string cacheHead = "\x01";
	static u32 cacheVer = ~0u;
	const std::string headDir = hostfs::savestateFolderOverride;
	const u32 ver = tas_clip::libraryVersion();
	if (headDir == cacheHead && ver == cacheVer)
		return d;
	cacheHead = headDir;
	cacheVer = ver;
	d = Graph();
	if (headDir.empty())
		return d;
	d.rootDir = tas_branch::rootOf(headDir);
	d.onBranch = tas_branch::isBranchDir(headDir);
	d.headId = d.onBranch ? ghc::filesystem::path(headDir).filename().string() : std::string("main");
	const nlohmann::json root = tas_clip::read(d.rootDir);
	d.valid = root.is_object() && !d.rootDir.empty();
	if (root.contains("node") && root["node"].is_object())
	{
		const nlohmann::json& nd = root["node"];
		if (nd.contains("tags") && nd["tags"].is_array() && !nd["tags"].empty())
			d.mainLabel = tas_clip::joinTags(nd["tags"]);
	}
	if (root.contains("states") && root["states"].is_array())
		for (const auto& s : root["states"])
			if (s.is_object())
				d.mainStates.push_back(s.value("slot", -1));
	d.mainThumbPath = highestThumb(root, d.rootDir);
	if (root.contains("branches") && root["branches"].is_array())
		for (const auto& b : root["branches"])
		{
			if (!b.is_object())
				continue;
			Node n;
			n.id = b.value("id", std::string());
			if (n.id.empty())
				continue;
			if (b.contains("tags") && b["tags"].is_array() && !b["tags"].empty())
				n.label = tas_clip::joinTags(b["tags"]);
			else
			{
				const std::string nm = b.value("name", std::string());
				n.label = nm.empty() ? n.id : nm;
			}
			n.fromSlot = b.value("fromSlot", 0);
			n.atFrame = b.value("atFrame", 0u);
			// THE LIVE VERDICT, not the stored forkMismatch flag, so the node
			// names the specific reason: "fork frame moved" is a different
			// repair from "main changed below the fork".
			const tas_branch::MergeCheck mc = tas_branch::mergeStatus(d.rootDir, n.id);
			n.mismatch = mc.verdict == tas_branch::MergeVerdict::FrameMismatch
					|| mc.verdict == tas_branch::MergeVerdict::PrefixDiverged
					|| mc.verdict == tas_branch::MergeVerdict::MainMissing;
			if (n.mismatch)
				n.mismatchReason = tas_branch::mergeVerdictText(mc.verdict);
			n.color = b.value("color", std::string());
			const std::string bdir = (ghc::filesystem::path(d.rootDir) / "branches" / n.id).string();
			const nlohmann::json bclip = tas_clip::read(bdir);
			if (bclip.contains("states") && bclip["states"].is_array())
				for (const auto& s : bclip["states"])
					if (s.is_object())
					{
						n.states.push_back(s.value("slot", -1));
						if (s.value("slot", -1) == n.fromSlot && s.contains("thumb"))
							n.thumbFork = bdir + "/" + s.value("thumb", std::string());
					}
			n.thumbPath = highestThumb(bclip, bdir);
			d.nodes.push_back(std::move(n));
		}
	return d;
}

// ---------------------------------------------------------------------------------------
// Session verbs: the half of checkout / merge that touches the running game.
// ---------------------------------------------------------------------------------------

/*
	Ported from his gui_branch_checkout. Two of its guards are ours by a
	different name: `fst.running` is roll::frameSkipTestRunning(), and the roll
	selection is roll::selection() rather than a pair of file statics.
*/
static bool checkout(const std::string& targetDir, int loadSlot, const std::string& label)
{
	if (gui_state != GuiState::Paused)
	{
		gui_display_notification("Pause first", 2000);
		return false;
	}
	if (frameSkipTestRunning())
	{
		gui_display_notification("A Frame Skip Test run is in progress", 2000);
		return false;
	}
	if (targetDir.empty())
		return false;
	dojo.SwitchClipFolder(targetDir);
	// Land ON the fork state and reload it - the reload replaces the machine,
	// clearing whatever was being done on the branch being left. Falls back to
	// BASE if the fork state is missing.
	int slot = loadSlot;
	std::error_code ec;
	if (!ghc::filesystem::exists(hostfs::getSavestatePath(slot, false), ec))
		slot = 0;
	u32 landed = 0;
	if (ghc::filesystem::exists(hostfs::getSavestatePath(slot, false), ec))
	{
		config::SavestateSlot.set(slot);
		cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(slot));
		gui_loadState();
		landed = dojo.frame_number.load();
	}
	selection().clear();
	dojo.savestate_epoch++;
	tas_clip::bump();
	char m[220];
	snprintf(m, sizeof(m), "Checked out %s (slot %d @frame %u)",
			label.empty() ? ghc::filesystem::path(targetDir).filename().string().c_str() : label.c_str(),
			slot, landed);
	gui_display_notification(m, 4000);
	NOTICE_LOG(NETWORK, "TAS BRANCH: checkout -> %s (slot %d, frame %u)", targetDir.c_str(), slot, landed);
	return true;
}

/*
	His gui_branch_merge. The order is the point: FlushLiveClip main FIRST so
	the F8 backup inside tas_branch::merge captures main's current, then re-sync
	the session to the merged files WITHOUT another flush - SwitchClipFolder
	would flush again and clobber the merge.
*/
static bool merge(const std::string& mainDir, const std::string& branchId)
{
	if (gui_state != GuiState::Paused)
	{
		gui_display_notification("Pause first", 2000);
		return false;
	}
	if (frameSkipTestRunning())
	{
		gui_display_notification("A Frame Skip Test run is in progress", 2000);
		return false;
	}
	if (mainDir.empty() || branchId.empty())
		return false;
	dojo.FlushLiveClip();
	const int n = tas_branch::merge(mainDir, branchId);
	if (n < 0)
	{
		gui_display_notification("Merge refused - the branch no longer aligns with main (see log)", 4500);
		return false;
	}
	std::error_code ec;
	std::string movie;
	for (const auto& f : ghc::filesystem::directory_iterator(ghc::filesystem::path(mainDir), ec))
	{
		if (f.is_directory(ec))
			continue;
		const std::string ext = f.path().extension().string();
		if (ext == ".flyr" || ext == ".flyreplay")
		{
			movie = f.path().string();
			break;
		}
	}
	dojo.BeginClipStats();
	if (!movie.empty())
		dojo.replay.AttachFile(movie);
	dojo.stale_tail_from = ~0u;
	dojo.loaded_macro_path.clear();
	selection().clear();
	if (ghc::filesystem::exists(hostfs::getSavestatePath(0, false), ec))
	{
		config::SavestateSlot.set(0);
		cfgSetVirtual("config", "Dreamcast.SavestateSlot", "0");
		gui_loadState();
	}
	dojo.savestate_epoch++;
	tas_clip::bump();
	char m[240];
	snprintf(m, sizeof(m), "Merged into main (%d files). Main was backed up first (F8, tagged).", n);
	gui_display_notification(m, 5500);
	NOTICE_LOG(NETWORK, "TAS BRANCH: merge '%s' done (%d files)", branchId.c_str(), n);
	return true;
}

// ---------------------------------------------------------------------------------------
// Drawing helpers.
// ---------------------------------------------------------------------------------------

//! His gui_draw_branch_thumbnail: a PNG as an ImGui image, cached on the
//! imguiDriver texture cache keyed by path, reloaded when the file changes.
static void drawThumb(const std::string& pngPath, float maxW, float maxH)
{
	if (imguiDriver == nullptr || pngPath.empty())
		return;
	std::error_code ec;
	if (!ghc::filesystem::exists(pngPath, ec))
		return;
	const int64_t mtime = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
			ghc::filesystem::last_write_time(pngPath, ec).time_since_epoch()).count();
	const uintmax_t fsize = (uintmax_t)ghc::filesystem::file_size(pngPath, ec);
	struct Meta { int64_t mtime; uintmax_t size; int w; int h; };
	static std::map<std::string, Meta> meta;
	ImTextureID id = imguiDriver->getTexture(pngPath);
	auto it = meta.find(pngPath);
	if (!(id != ImTextureID() && it != meta.end() && it->second.mtime == mtime && it->second.size == fsize))
	{
		int w = 0, h = 0;
		u8 *data = loadImage(pngPath, w, h);
		if (data == nullptr)
			return;
		id = imguiDriver->updateTexture(pngPath, data, w, h);
		free(data);
		if (id == ImTextureID())
			return;
		meta[pngPath] = Meta{ mtime, fsize, w, h };
		it = meta.find(pngPath);
	}
	const int w = it->second.w, h = it->second.h;
	if (w <= 0 || h <= 0)
		return;
	float sc = std::min(maxW / (float)w, maxH / (float)h);
	if (sc > 1.f)
		sc = 1.f;
	ImGui::Image(id, ImVec2(w * sc, h * sc));
}

// ---------------------------------------------------------------------------------------
// The panel.
// ---------------------------------------------------------------------------------------

static bool branchesOpen = false;
static bool showProps = true;

// Layout is VIEW state - dragging never touches clip.json. Same sidecar as his,
// same keys, so a layout arranged on either fork reads on the other.
static std::string layoutRoot;
static std::map<std::string, ImVec2> layout;
static bool layoutDirty = false;
static ImVec2 pan(0, 0);

// Selection is an ID, never an index - the node list rebuilds on every version bump.
static std::string selId, selRoot;

// Properties edit buffers, flushed on blur and before a selection switch.
static std::string loadedId, loadedRoot;
static char tagsBuf[256], notesBuf[1400];
static bool bufDirty = false;

// Create row.
static int createSlot = -1;
static char createTags[256] = "";
static char createNotes[512] = "";

// Deferred op, run AFTER the graph is drawn: these swap folders and reload
// states, which must not happen mid-frame with node rects in flight.
struct Op { int type = 0; std::string id, label, colorHex; int slot = 0; };	// 1 checkout 2 merge 3 delete 4 colour

static void loadLayout(const std::string& rootDir)
{
	layoutRoot = rootDir;
	layout.clear();
	layoutDirty = false;
	pan = ImVec2(0, 0);
	std::ifstream in(rootDir + "/branches/.nodelayout.json");
	if (!in.good())
		return;
	try {
		nlohmann::json j;
		in >> j;
		if (j.is_object())
			for (auto it = j.begin(); it != j.end(); ++it)
				if (it.value().is_array() && it.value().size() == 2)
					layout[it.key()] = ImVec2(it.value()[0].get<float>(), it.value()[1].get<float>());
	} catch (...) {}
}

static void saveLayout(const std::string& rootDir)
{
	nlohmann::json j = nlohmann::json::object();
	for (const auto& kv : layout)
		j[kv.first] = { kv.second.x, kv.second.y };
	std::error_code ec;
	ghc::filesystem::create_directories(rootDir + "/branches", ec);
	std::ofstream out(rootDir + "/branches/.nodelayout.json", std::ios::binary | std::ios::trunc);
	if (out.good())
		out << j.dump(0);
	layoutDirty = false;
}

static void flushEdits()
{
	if (bufDirty && !loadedId.empty() && !loadedRoot.empty())
		tas_branch::setTagsNotes(loadedRoot, loadedId, tagsBuf, notesBuf);
	bufDirty = false;
}

/*
	One node: a fixed-width box with a header strip that is the drag / click /
	context target, and ordinary widgets below it. Widgets go to the FRONT draw
	channel; the frame is added to the BACK channel after the group so it sits
	under them - the splitter merges back-under-front.
*/
static void drawNode(ImDrawList *dl, ImDrawListSplitter& split, const ImVec2& origin,
		const std::string& key, ImVec2& pos, float nodeH, const ImVec4& border,
		bool isHead, Op& op, const Graph& d, const Node *n)
{
	const ImVec2 p0(origin.x + pan.x + pos.x, origin.y + pan.y + pos.y);
	const ImVec2 p1(p0.x + kNodeW, p0.y + nodeH);
	const float headerH = 24.f;

	split.SetCurrentChannel(dl, 1);
	ImGui::SetCursorScreenPos(p0);
	ImGui::PushID(key.c_str());
	// The header strip is the interaction surface. Submitted before the text
	// so the text draws over it; text is not interactive so the strip keeps
	// hover. Nodes are submitted after the canvas's pan button, so they win.
	ImGui::InvisibleButton("##hdr", ImVec2(kNodeW, headerH));
	const bool hdrHovered = ImGui::IsItemHovered();
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 dlt = ImGui::GetIO().MouseDelta;
		pos.x += dlt.x;
		pos.y += dlt.y;
		layout[key] = pos;
		layoutDirty = true;
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		selId = n != nullptr ? n->id : "main";
		selRoot = d.rootDir;
	}
	if (hdrHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		if (n == nullptr && d.onBranch)
			op = Op{ 1, "", "main", "", 0 };
		else if (n != nullptr && !isHead)
			op = Op{ 1, n->id, n->label, "", n->fromSlot };
	}
	if (ImGui::BeginPopupContextItem("##nodemenu"))
	{
		if (n == nullptr)
		{
			tasTextDisabled("main");
			ImGui::Separator();
			if (tasMenuItem("Checkout", nullptr, d.headId == "main", d.headId != "main"))
				op = Op{ 1, "", "main", "", 0 };
		}
		else
		{
			tasTextDisabled("%s", n->label.c_str());
			ImGui::Separator();
			if (tasMenuItem("Checkout", nullptr, isHead, !isHead))
				op = Op{ 1, n->id, n->label, "", n->fromSlot };
			if (d.headId == "main")
			{
				const tas_branch::MergeCheck mc = tas_branch::mergeStatus(d.rootDir, n->id);
				const bool okm = mc.verdict == tas_branch::MergeVerdict::Ok;
				ImGui::BeginDisabled(!okm);
				// A VISIBLE two-step confirm, his own correction of an earlier
				// hidden Shift-guard "dev couldn't see".
				if (ImGui::BeginMenu("Merge into main"))
				{
					if (tasMenuItem("Confirm - adopt this branch into main"))
						op = Op{ 2, n->id, "", "", 0 };
					tasTextDisabled("main is backed up first (an F8 generation)");
					ImGui::EndMenu();
				}
				ImGui::EndDisabled();
				if (!okm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					tasTip("%s", tas_branch::mergeVerdictText(mc.verdict));
			}
			if (ImGui::BeginMenu("Colour"))
			{
				tasTextDisabled("Override (default = by state)");
				for (int c = 0; c < 10; c++)
				{
					ImGui::PushID(c);
					if (c % 5 != 0)
						ImGui::SameLine();
					if (ImGui::ColorButton("##sw", hexColor(kStateColors[c]),
							ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20)))
						op = Op{ 4, n->id, "", kStateColors[c], 0 };
					ImGui::PopID();
				}
				if (tasSmallButton("Auto (by state)"))
					op = Op{ 4, n->id, "", "", 0 };
				ImGui::EndMenu();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.40f, 1.f));
			const bool delOpen = ImGui::BeginMenu("Delete");
			ImGui::PopStyleColor();
			if (delOpen)
			{
				if (tasMenuItem("Confirm - move branch to .trash"))
					op = Op{ 3, n->id, "", "", 0 };
				tasTextDisabled("recoverable from the clip's .trash folder");
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}

	// Body, over the header strip.
	ImGui::SetCursorScreenPos(ImVec2(p0.x + 8.f, p0.y + 4.f));
	ImGui::BeginGroup();
	{
		const bool selected = (selId == (n != nullptr ? n->id : "main"));
		const ImVec4 titleCol = isHead ? TAS_BRANCH : (selected ? TAS_ACCENT : TAS_TEXT);
		if (n != nullptr && border.w > 0.f)
		{
			tasTextColored(border, "%s", "\xe2\x97\x8f");	// a filled circle: the branch's colour chip
			ImGui::SameLine();
		}
		tasTextColored(titleCol, "%s", n != nullptr ? n->label.c_str() : d.mainLabel.c_str());
		if (isHead)
		{
			ImGui::SameLine();
			tasTextColored(TAS_BRANCH, "(HEAD)");
		}
		if (n != nullptr)
		{
			tasTextColored(border.w > 0.f ? border : TAS_DIM, "[%d]", n->fromSlot);
			ImGui::SameLine(0, 5.f);
			tasTextDisabled("@ %u", n->atFrame);
		}
		tasTextDisabled("States: %s", statesLine(n != nullptr ? n->states : d.mainStates).c_str());
		{
			const bool thumbFork = cfgLoadBool("dojo", "BranchThumbFork", true);
			std::string th;
			if (n == nullptr)
				th = d.mainThumbPath;
			else
				th = (thumbFork && !n->thumbFork.empty()) ? n->thumbFork
						: (!n->thumbPath.empty() ? n->thumbPath : n->thumbFork);
			if (!th.empty())
				drawThumb(th, 150.f, 84.f);
		}
		if (n != nullptr && n->mismatch)
		{
			tasTextColored(TAS_WRITE, "%s", n->mismatchReason.empty() ? "misaligned" : n->mismatchReason.c_str());
			if (ImGui::IsItemHovered())
				tasTip("This branch can't merge back: its fork state no longer matches main.\n"
						"Same frame but different inputs below it = a different machine.\n"
						"Restore main's fork state to the original take, or re-fork.");
		}
	}
	ImGui::EndGroup();
	ImGui::PopID();

	// The frame, UNDER the widgets.
	split.SetCurrentChannel(dl, 0);
	const ImU32 bg = ImGui::GetColorU32(isHead ? tasCol(TAS_BRANCH, 0.16f) : tasCol(TAS_PANEL, 0.96f));
	const ImU32 bd = border.w > 0.f ? ImGui::GetColorU32(border)
			: ImGui::GetColorU32(selId == (n != nullptr ? n->id : "main") ? TAS_ACCENT : tasCol(TAS_DIM, 0.6f));
	dl->AddRectFilled(p0, p1, bg, 6.f);
	dl->AddRect(p0, p1, bd, 6.f, 0, isHead ? 2.5f : 1.5f);
	dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + headerH), ImGui::GetColorU32(tasCol(TAS_TEXT, 0.06f)), 6.f, ImDrawFlags_RoundCornersTop);
}

static void drawProps(const Graph& d)
{
	if (selId.empty() || selRoot.empty())
	{
		tasTextWrapped("Select a node in the graph to see and edit its tags and notes.");
		return;
	}
	const bool isMain = (selId == "main");
	static std::string fLabel, fFrom, fCreated, fModified, fMerge;
	static bool fMergeOk = true;
	bool justReloaded = false;
	if (loadedId != selId || loadedRoot != selRoot)
	{
		flushEdits();		// the previous node's pending edits, BEFORE the buffers reload
		loadedId = selId;
		loadedRoot = selRoot;
		justReloaded = true;
		tagsBuf[0] = notesBuf[0] = '\0';
		fLabel.clear(); fFrom.clear(); fCreated.clear(); fModified.clear(); fMerge.clear(); fMergeOk = true;
		if (isMain)
		{
			const nlohmann::json root = tas_clip::read(selRoot);
			const nlohmann::json nd = root.value("node", nlohmann::json::object());
			snprintf(tagsBuf, sizeof(tagsBuf), "%s", tas_clip::joinTags(nd.value("tags", nlohmann::json::array())).c_str());
			snprintf(notesBuf, sizeof(notesBuf), "%s", nd.value("notes", std::string()).c_str());
			fLabel = "main (the trunk)";
		}
		else
		{
			for (const auto& b : tas_branch::list(selRoot))
				if (b.is_object() && b.value("id", std::string()) == selId)
				{
					const bool hasTags = b.contains("tags") && b["tags"].is_array() && !b["tags"].empty();
					fLabel = hasTags ? tas_clip::joinTags(b["tags"]) : b.value("name", selId);
					snprintf(tagsBuf, sizeof(tagsBuf), "%s",
							(b.contains("tags") && b["tags"].is_array()) ? tas_clip::joinTags(b["tags"]).c_str() : "");
					snprintf(notesBuf, sizeof(notesBuf), "%s", b.value("notes", std::string()).c_str());
					char fb[64];
					snprintf(fb, sizeof(fb), "[%d] @ %u", b.value("fromSlot", 0), b.value("atFrame", 0u));
					fFrom = fb;
					fCreated = tas_clip::localUsTime(b.value("createdAt", std::string()));
					fModified = tas_clip::localUsTime(b.value("modifiedAt", std::string()));
					break;
				}
			const tas_branch::MergeCheck mc = tas_branch::mergeStatus(selRoot, selId);
			fMergeOk = mc.verdict == tas_branch::MergeVerdict::Ok;
			fMerge = tas_branch::mergeVerdictText(mc.verdict);
		}
	}
	tasTextColored(TAS_BRANCH, "%s", fLabel.c_str());
	if (!isMain)
	{
		tasTextDisabled("%s", fFrom.c_str());
		if (!fCreated.empty())
			tasTextDisabled("created %s", fCreated.c_str());
		if (!fModified.empty())
			tasTextDisabled("modified %s", fModified.c_str());
		tasTextColored(fMergeOk ? TAS_READ : TAS_WRITE, "%s", fMerge.c_str());
	}
	ImGui::Separator();
	tasTextDisabled("Tags (comma-separated)");
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputText("##btags", tagsBuf, sizeof(tagsBuf));
	if (ImGui::IsItemEdited())
		bufDirty = true;
	if (!justReloaded && ImGui::IsItemDeactivatedAfterEdit())
		flushEdits();
	tasTextDisabled("Notes");
	ImGui::InputTextMultiline("##bnotes", notesBuf, sizeof(notesBuf), ImVec2(-1.f, 60.f));
	if (ImGui::IsItemEdited())
		bufDirty = true;
	if (!justReloaded && ImGui::IsItemDeactivatedAfterEdit())
		flushEdits();
}

static void drawCreateRow(const Graph& d)
{
	// THE DEPTH-1 GUARD, here because the engine does not carry it.
	if (d.onBranch)
	{
		tasTextDisabled("On a branch - check out main to fork a new one (no branch-of-branch).");
		return;
	}
	Host *h = host();
	if (h == nullptr)
		return;
	tasText("New branch from state");
	ImGui::SameLine();
	// Only slots that exist are offered; a fork from an empty slot is a folder
	// with no anchor and can never merge.
	char cur[64];
	snprintf(cur, sizeof(cur), createSlot < 0 ? "pick a slot" : "slot %d", createSlot);
	ImGui::SetNextItemWidth(160.f);
	if (ImGui::BeginCombo("##fromslot", cur))
	{
		for (int i = 0; i < h->slotCount(); i++)
		{
			SlotView v;
			if (!h->slotView(i, v) || !v.exists)
				continue;
			char lbl[128];
			snprintf(lbl, sizeof(lbl), "slot %d  %s%s@ %u", i, v.label.c_str(),
					v.label.empty() ? "" : "  ", v.frame);
			if (ImGui::Selectable(lbl, createSlot == i))
				createSlot = i;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150.f);
	tasInputTextWithHint("##ctags", "tags", createTags, sizeof(createTags));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-90.f);
	tasInputTextWithHint("##cnotes", "notes", createNotes, sizeof(createNotes));
	ImGui::SameLine();
	SlotView v;
	const bool have = createSlot >= 0 && h->slotView(createSlot, v) && v.exists;
	ImGui::BeginDisabled(!have || gui_state != GuiState::Paused);
	if (tasButton("Create"))
	{
		const std::string label = hostfs::loadSavestateLabel(createSlot);
		const std::string dir = tas_branch::create(d.rootDir, createSlot, v.frame, label, createTags, createNotes);
		if (dir.empty())
			gui_display_notification("Branch not created (see log)", 3000);
		else
		{
			char m[160];
			snprintf(m, sizeof(m), "Created branch from state %d @ frame %u", createSlot, v.frame);
			gui_display_notification(m, 4000);
			createTags[0] = createNotes[0] = '\0';
			dojo.savestate_epoch++;
		}
	}
	ImGui::EndDisabled();
	if (gui_state != GuiState::Paused)
	{
		ImGui::SameLine();
		tasTextDisabled("(pause first)");
	}
}

static void draw()
{
	const Graph& d = graph();
	if (!d.valid || d.rootDir.empty())
	{
		tasTextDisabled("No clip loaded - open a movie or macro to see its branch tree.");
		return;
	}
	if (layoutRoot != d.rootDir)
		loadLayout(d.rootDir);

	// ---- top: create, and the view controls ----------------------------------------
	drawCreateRow(d);
	if (tasSmallButton("Frame all"))
		pan = ImVec2(0, 0);
	ImGui::SameLine();
	if (tasSmallButton("Auto-arrange"))
	{
		layout.clear();
		layoutDirty = false;
		pan = ImVec2(0, 0);
		std::error_code ec;
		ghc::filesystem::remove(d.rootDir + "/branches/.nodelayout.json", ec);
	}
	ImGui::SameLine();
	tasCheckbox("properties", &showProps);
	ImGui::SameLine();
	tasTextDisabled("%d branch%s   drag a header to move, right-click for actions, double-click to check out",
			(int)d.nodes.size(), d.nodes.size() == 1 ? "" : "es");

	if (showProps && ImGui::CollapsingHeader("Branch Properties", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const float ph = std::min(200.f, ImGui::GetContentRegionAvail().y * 0.45f);
		if (ImGui::BeginChild("##brprops", ImVec2(0, ph), true))
			drawProps(d);
		ImGui::EndChild();
	}

	// ---- the canvas -------------------------------------------------------------------
	Op op;
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (ImGui::BeginChild("##canvas", ImVec2(0, std::max(120.f, avail.y)), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();

		// PAN: a button under everything. Nodes are submitted after it, so a
		// drag that starts on a node header moves the node, not the view.
		ImGui::InvisibleButton("##pan", ImVec2(std::max(1.f, size.x), std::max(1.f, size.y)));
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 dlt = ImGui::GetIO().MouseDelta;
			pan.x += dlt.x;
			pan.y += dlt.y;
		}

		ImDrawListSplitter split;
		split.Split(dl, 2);

		// Layout: his arithmetic, as the pure functions the arm checks.
		std::vector<int> forks;
		forks.reserve(d.nodes.size());
		for (const Node& n : d.nodes)
			forks.push_back(n.fromSlot);
		const Layout L = layoutFor(forks);

		auto posFor = [&](const std::string& key, ImVec2 fallback) -> ImVec2& {
			auto it = layout.find(key);
			if (it == layout.end())
				it = layout.emplace(key, fallback).first;
			return it->second;
		};

		// Swimlanes: one tinted box per fork-state column, in the back channel.
		split.SetCurrentChannel(dl, 0);
		for (const auto& kv : L.countIn)
		{
			const int slot = kv.first;
			const ImVec4 gc = hexColor(autoStateColor(slot));
			const ImVec2 a = autoPos(L.colOf.at(slot), 0);
			const ImVec2 g0(origin.x + pan.x + a.x - kGrpInset, origin.y + pan.y - 6.f);
			const ImVec2 g1(g0.x + kNodeW + 2.f * kGrpInset, g0.y + 44.f + kv.second * kRowH);
			dl->AddRectFilled(g0, g1, ImGui::GetColorU32(ImVec4(gc.x, gc.y, gc.z, 0.05f)), 8.f);
			dl->AddRect(g0, g1, ImGui::GetColorU32(ImVec4(gc.x, gc.y, gc.z, 0.45f)), 8.f);
			char lbl[24];
			snprintf(lbl, sizeof(lbl), "State %d", slot);
			dl->AddText(ImVec2(g0.x + 8.f, g0.y + 6.f), ImGui::GetColorU32(gc), lbl);
		}

		// main
		ImVec2& mp = posFor("main", mainPos(L.maxRows));
		const float mainH = 96.f + (d.mainThumbPath.empty() ? 0.f : 90.f);
		drawNode(dl, split, origin, "main", mp, mainH, ImVec4(0, 0, 0, 0),
				d.headId == "main", op, d, nullptr);
		const ImVec2 mainOut(origin.x + pan.x + mp.x + kNodeW, origin.y + pan.y + mp.y + 12.f);

		// branches, and their edges from main
		std::map<int, int> rowIn;
		for (const Node& n : d.nodes)
		{
			const int row = rowIn[n.fromSlot]++;
			ImVec2& np = posFor(n.id, autoPos(L.colOf.at(n.fromSlot), row));
			const ImVec4 col = hexColor(n.color.empty() ? autoStateColor(n.fromSlot) : n.color);
			const bool isHead = d.onBranch && d.headId == n.id;
			const bool hasThumb = !(n.thumbFork.empty() && n.thumbPath.empty());
			const float nodeH = 96.f + (hasThumb ? 90.f : 0.f) + (n.mismatch ? 18.f : 0.f);
			drawNode(dl, split, origin, n.id, np, nodeH, col, isHead, op, d, &n);
			// THE EDGE. One bezier from main's right to the node's left - what
			// ed::Link was doing, and the whole reason the library was there.
			const ImVec2 in(origin.x + pan.x + np.x, origin.y + pan.y + np.y + 12.f);
			const float dx = std::max(40.f, (in.x - mainOut.x) * 0.5f);
			split.SetCurrentChannel(dl, 0);
			dl->AddBezierCubic(mainOut, ImVec2(mainOut.x + dx, mainOut.y), ImVec2(in.x - dx, in.y), in,
					ImGui::GetColorU32(col.w > 0.f ? col : ImVec4(0.55f, 0.60f, 0.68f, 0.9f)),
					isHead ? 5.f : 2.f);
		}
		split.Merge(dl);
	}
	ImGui::EndChild();

	// Persist the layout once the drag releases - a plain file, not
	// tas_clip::write, so it does not bump the version and re-read the graph.
	if (layoutDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		saveLayout(d.rootDir);

	// ---- deferred ops ---------------------------------------------------------------
	if (op.type == 1)
	{
		if (op.id.empty())
		{
			int mainAnchor = 0;
			if (d.onBranch)
				for (const Node& bn : d.nodes)
					if (bn.id == d.headId)
					{
						mainAnchor = bn.fromSlot;
						break;
					}
			checkout(d.rootDir, mainAnchor, "main");
		}
		else
			checkout((ghc::filesystem::path(d.rootDir) / "branches" / op.id).string(), op.slot, op.label);
	}
	else if (op.type == 2)
		merge(d.rootDir, op.id);
	else if (op.type == 3)
	{
		if (tas_branch::remove(d.rootDir, op.id))
		{
			if (selId == op.id)
				selId.clear();
			dojo.savestate_epoch++;
			gui_display_notification("Branch deleted (moved to .trash)", 3000);
		}
	}
	else if (op.type == 4)
		tas_branch::setColor(d.rootDir, op.id, op.colorHex);
}

}	// namespace branch

void registerBranchesPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	// MENU ONLY: checkout, merge and create all refuse unless paused.
	panels::add({ "branches", "Branches", &branch::branchesOpen, branch::draw, panels::Menu,
			/*persist*/ true, /*defW*/ 760.f, /*defH*/ 520.f });
	NOTICE_LOG(RENDERER, "BRANCHES PANEL: registered=%s open=%s",
			panels::find("branches") != nullptr ? "yes" : "NO", branch::branchesOpen ? "yes" : "no");
}

}	// namespace roll
