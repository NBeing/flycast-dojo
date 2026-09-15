#include "tas_branch.h"
#include "tas_clip.h"
#include "dojo.h"
#include "oslib/oslib.h"	// hostfs::scanSavestateInfo for the fork anchor (state frame + prefix hash)
#include "log/Log.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace tas_branch
{

std::string branchesDir(const std::string& clipDir)
{
	return (ghc::filesystem::path(clipDir) / "branches").string();
}

bool isBranchDir(const std::string& dir)
{
	if (dir.empty())
		return false;
	return ghc::filesystem::path(dir).parent_path().filename().string() == "branches";
}

std::string rootOf(const std::string& dir)
{
	// A branch lives at <clip>/branches/<id>: two levels up is the root clip. Anything else is already a root.
	if (isBranchDir(dir))
		return ghc::filesystem::path(dir).parent_path().parent_path().string();
	return dir;
}

void ensureMainNode(const std::string& clipDir)
{
	if (clipDir.empty())
		return;
	nlohmann::json j = tas_clip::read(clipDir);
	if (j.contains("node") && j["node"].is_object())
		return;								// already a node (seed stamps it now; this backfills pre-feature clips)
	nlohmann::json node = nlohmann::json::object();
	node["id"] = "main";
	node["kind"] = "main";
	node["name"] = "main";
	node["tags"] = nlohmann::json::array();
	node["color"] = "";
	j["node"] = node;
	tas_clip::write(clipDir, j);
}

// Folder-safe UTC timestamp "YYYY-MM-DDTHH_MM_SSZ" (colons -> underscores), matching the clip-folder convention
// (e.g. 2026-09-06T10_13_51Z). dev: the folder date MUST be UNIQUE and in UTC - a bare local YYYY-MM-DD is too
// generic (same-day branches collide on the date, differ only by NN). Unique to the second; the per-state NN then
// disambiguates the rare same-second case.
static std::string utcStamp()
{
	std::string s = tas_clip::utcNowIso();	// "2026-09-06T17:33:19Z"
	for (char& c : s)
		if (c == ':')
			c = '_';
	return s;
}

std::string create(const std::string& clipDir, int fromSlot, u32 atFrame, const std::string& name,
		const std::string& tagsCsv, const std::string& notes)
{
	if (clipDir.empty())
		return "";
	std::error_code ec;
	ghc::filesystem::path bdir(branchesDir(clipDir));
	ghc::filesystem::create_directories(bdir, ec);

	// Per-state sequence: one past the highest NN among existing "*_state_<N>_<NN>" folders for THIS source state.
	// (Old-format "<NN>_<name>" folders don't match, so they're ignored - the new id can't collide with them.)
	const std::string tail = "_state_" + std::to_string(fromSlot) + "_";
	int used = 0;
	for (const auto& d : ghc::filesystem::directory_iterator(bdir, ec))
	{
		if (!d.is_directory(ec))
			continue;
		const std::string fn = d.path().filename().string();
		const size_t at = fn.rfind(tail);
		if (at == std::string::npos)
			continue;
		const std::string nn = fn.substr(at + tail.size());
		if (!nn.empty() && std::all_of(nn.begin(), nn.end(), [](char c){ return std::isdigit((unsigned char)c) != 0; }))
			used = std::max(used, atoi(nn.c_str()));
	}
	const int nn = used + 1;
	if (nn > 99)
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: create refused - 99-branch limit reached for state %d under %s", fromSlot, bdir.string().c_str());
		return "";
	}

	char folder[96];
	snprintf(folder, sizeof(folder), "%s_state_%d_%02d", utcStamp().c_str(), fromSlot, nn);
	const std::string id = folder;
	ghc::filesystem::path dst = bdir / folder;
	if (ghc::filesystem::exists(dst, ec))
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: create refused - %s already exists", dst.string().c_str());
		return "";
	}
	ghc::filesystem::create_directories(dst, ec);
	if (ec)
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: create failed - cannot mkdir %s (%s)", dst.string().c_str(), ec.message().c_str());
		return "";
	}

	// Hazard #1: flush the live clip (replay tail, macro, wave/ruler, stats) BEFORE the copy, or the branch inherits a
	// stale movie missing up to the last ~119 unflushed movie frames.
	dojo.FlushLiveClip();

	u64 bytes = 0;
	const int files = tas_clip::copyLiveSet(clipDir, dst.string(), &bytes);
	if (files <= 0)
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: create copied 0 files - is the clip empty? (%s)", clipDir.c_str());
		return "";
	}

	const std::string createdAt = tas_clip::utcNowIso();
	const std::string display = name.empty() ? id : name;
	// The user-facing metadata: tags (CSV -> array) + notes. Tags are the branch's identity to the user (dev) - the
	// disk id / created / modified / frame / state are all machine bookkeeping.
	nlohmann::json tagsArr = nlohmann::json::array();
	{
		std::vector<std::string> tv;
		tas_clip::parseTags(tagsCsv.c_str(), tv);
		for (const auto& t : tv)
			tagsArr.push_back(t);
	}

	// Fork ANCHOR (merge groundwork, dev 2026-09-06): main's state[fromSlot] identity at the moment of the fork -
	// frame + prefix hash + rerecord seq. The eventual merge-check compares THIS against main's CURRENT state N: same
	// frame AND same prefixHash => the fork point is still frame-aligned + byte-identical, so the branch's tail can
	// splice back. A changed frame (dev's throw corruption) or a diverged prefix (main re-recorded below) is caught.
	// create() runs on main (canCreate gate), so scanSavestateInfo reads main's states here.
	nlohmann::json forkAnchor = nlohmann::json::object();
	{
		std::vector<hostfs::SavestateInfo> slots = hostfs::scanSavestateInfo();
		if (fromSlot >= 0 && fromSlot < (int)slots.size() && slots[fromSlot].exists)
		{
			forkAnchor["slot"] = fromSlot;
			forkAnchor["frame"] = slots[fromSlot].movieFrame;
			forkAnchor["prefixHash"] = slots[fromSlot].prefixHash;	// 0 if the sidecar predates v3
			if (slots[fromSlot].haveSeq)
				forkAnchor["rerecordSeq"] = slots[fromSlot].rerecordSeq;
		}
	}

	// main is a node too (dev): make sure the ROOT carries its "main" node before it gains a child, so the graph can
	// show main + branches from one file. No-op once seeded (seed stamps it); this backfills pre-feature clips.
	ensureMainNode(clipDir);

	// 1. Register in the ROOT clip.json branches[] (append; preserve every other field). Done AFTER the copy so the
	//    branch's own copied clip.json doesn't list itself. tas_clip::write is locked + atomic + bumps the version.
	//    This branches[] entry is AUTHORITATIVE for the child's display fields (name/tags/color/notes) - the graph +
	//    tagging UI read/write it here, so a branch's tags live in the root, reachable whether HEAD is main or a fork.
	{
		nlohmann::json root = tas_clip::read(clipDir);
		if (!root.contains("branches") || !root["branches"].is_array())
			root["branches"] = nlohmann::json::array();
		nlohmann::json e = nlohmann::json::object();
		e["id"] = id;
		e["name"] = display;			// the human name / for-each tag target (source state's label by default)
		e["fromSlot"] = fromSlot;
		e["atFrame"] = atFrame;
		e["createdAt"] = createdAt;
		e["modifiedAt"] = createdAt;	// dev: json carries created + modified dates; equal at birth, bumped on a tag/notes edit
		e["files"] = files;
		e["bytes"] = bytes;
		e["tags"] = tagsArr;
		e["notes"] = notes;
		e["forkAnchor"] = forkAnchor;	// {slot, frame, prefixHash, rerecordSeq} of main's state at fork time (merge-check)
		root["branches"].push_back(e);
		if (!tas_clip::write(clipDir, root))
			NOTICE_LOG(NETWORK, "TAS BRANCH: WARN - could not write branches[] into %s/clip.json", clipDir.c_str());
	}
	// 2. Overwrite the BRANCH's own "node" to a "branch" node (it was copied as main's node) and drop the inherited
	//    branches[] (a branch is a leaf - depth-1 star, no branch-of-branch). Makes the folder self-describing if ever
	//    opened standalone; the AUTHORITATIVE tags/color/name are the root's branches[] entry, this node mirrors them.
	{
		nlohmann::json bj = tas_clip::read(dst.string());
		bj.erase("branches");
		nlohmann::json node = nlohmann::json::object();
		node["id"] = id;
		node["kind"] = "branch";
		node["name"] = display;
		node["tags"] = tagsArr;			// mirror of the root's authoritative entry (self-describing folder)
		node["notes"] = notes;
		node["color"] = "";
		node["parent"] = ghc::filesystem::path(clipDir).filename().string();
		node["fromSlot"] = fromSlot;
		node["atFrame"] = atFrame;
		node["createdAt"] = createdAt;
		node["modifiedAt"] = createdAt;
		node["forkAnchor"] = forkAnchor;	// the branch remembers what main's fork state was (merge-check + desync detection)
		bj["node"] = node;
		tas_clip::write(dst.string(), bj);
	}

	NOTICE_LOG(NETWORK, "TAS BRANCH: create '%s' id=%s from slot %d @frame %u -> %s (%d file(s), %.1f MB)",
			display.c_str(), id.c_str(), fromSlot, atFrame, dst.string().c_str(), files, bytes / 1048576.0);
	return dst.string();
}

nlohmann::json list(const std::string& clipDir)
{
	nlohmann::json root = tas_clip::read(clipDir);
	if (root.contains("branches") && root["branches"].is_array())
		return root["branches"];
	return nlohmann::json::array();
}

const std::set<int>& forkSlots(const std::string& headDir)
{
	static std::string cacheHead;
	static u32 cacheVer = ~0u;
	static std::set<int> cacheSet;
	const u32 ver = tas_clip::libraryVersion();
	if (headDir == cacheHead && ver == cacheVer)
		return cacheSet;			// unchanged - no clip.json parse this frame
	cacheHead = headDir;
	cacheVer = ver;
	cacheSet.clear();
	if (headDir.empty())
		return cacheSet;
	if (isBranchDir(headDir))
	{	// on a branch: its own fork point (from its node lineage)
		const nlohmann::json bj = tas_clip::read(headDir);
		if (bj.is_object() && bj.contains("node") && bj["node"].is_object() && bj["node"].contains("fromSlot"))
			cacheSet.insert(bj["node"].value("fromSlot", -1));
	}
	else
	{	// on main: every branch's fork slot
		const nlohmann::json branches = list(rootOf(headDir));
		for (const auto& b : branches)
			if (b.is_object() && b.contains("fromSlot"))
				cacheSet.insert(b.value("fromSlot", -1));
	}
	cacheSet.erase(-1);
	return cacheSet;
}

int revalidateForks(const std::string& headDir, int savedSlot)
{
	if (headDir.empty() || savedSlot < 0)
		return 0;
	const std::string rootDir = rootOf(headDir);
	nlohmann::json root = tas_clip::read(rootDir);
	if (!root.contains("branches") || !root["branches"].is_array())
		return 0;
	const bool onBranch = isBranchDir(headDir);
	const std::string headId = onBranch ? ghc::filesystem::path(headDir).filename().string() : std::string();
	// The state just written, in the CURRENT head (main's state on main; the branch's own copy on a branch). Both are
	// judged against the SAME fork anchor (main's state at fork time), so either side drifting is caught.
	std::vector<hostfs::SavestateInfo> slots = hostfs::scanSavestateInfo();
	const bool haveNow = savedSlot < (int)slots.size() && slots[savedSlot].exists;
	const u32 nowFrame = haveNow ? slots[savedSlot].movieFrame : 0;
	const u64 nowHash = haveNow ? slots[savedSlot].prefixHash : 0;
	int changed = 0;
	for (auto& b : root["branches"])
	{
		if (!b.is_object() || b.value("fromSlot", -1) != savedSlot)
			continue;
		if (onBranch && b.value("id", std::string()) != headId)
			continue;					// on a branch, only THAT branch's own fork copy is affected
		bool mismatch = true;
		if (haveNow && b.contains("forkAnchor") && b["forkAnchor"].is_object())
		{
			const auto& fa = b["forkAnchor"];
			const u64 faHash = fa.value("prefixHash", (u64)0);
			// Aligned = same frame AND (a hash is unknown OR the hashes match). Frame is the hard gate (dev's throw
			// case); the prefix hash catches a same-frame-but-different-inputs drift. Unknown hash (pre-v3) -> frame-only.
			if (fa.value("frame", 0u) == nowFrame && (faHash == 0 || nowHash == 0 || faHash == nowHash))
				mismatch = false;
		}
		if (b.value("forkMismatch", false) != mismatch)
		{
			b["forkMismatch"] = mismatch;
			changed++;
		}
	}
	if (changed != 0)
	{
		tas_clip::write(rootDir, root);
		NOTICE_LOG(NETWORK, "TAS BRANCH: revalidateForks slot %d -> %d branch flag(s) changed (%s)",
				savedSlot, changed, onBranch ? headId.c_str() : "main");
	}
	return changed;
}

bool remove(const std::string& clipDir, const std::string& id)
{
	// id must be a single folder segment (no traversal) - it comes from branches[]/the folder listing, but guard anyway.
	if (clipDir.empty() || id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos
			|| id.find("..") != std::string::npos)
		return false;
	std::error_code ec;
	ghc::filesystem::path src = ghc::filesystem::path(branchesDir(clipDir)) / id;
	if (!ghc::filesystem::is_directory(src, ec) || !isBranchDir(src.string()))
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: remove refused - %s is not a branch folder", src.string().c_str());
		return false;
	}
	// Move to <clip>/.trash/<utc>/<id> - never a hard delete (matches tas_clip::restore's trash discipline).
	ghc::filesystem::path trash = ghc::filesystem::path(clipDir) / ".trash" / utcStamp();
	ghc::filesystem::create_directories(trash, ec);
	ghc::filesystem::rename(src, trash / id, ec);
	if (ec)
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: remove failed - could not move %s to trash (%s)", src.string().c_str(), ec.message().c_str());
		return false;
	}
	// Drop the branches[] entry from the root clip.json (preserve everything else).
	nlohmann::json root = tas_clip::read(clipDir);
	if (root.contains("branches") && root["branches"].is_array())
	{
		nlohmann::json kept = nlohmann::json::array();
		for (const auto& b : root["branches"])
			if (!(b.is_object() && b.value("id", std::string()) == id))
				kept.push_back(b);
		root["branches"] = kept;
		tas_clip::write(clipDir, root);
	}
	NOTICE_LOG(NETWORK, "TAS BRANCH: remove '%s' -> %s", id.c_str(), (trash / id).string().c_str());
	return true;
}

MergeCheck mergeStatus(const std::string& rootDir, const std::string& branchId)
{
	MergeCheck mc;
	nlohmann::json root = tas_clip::read(rootDir);
	// the branch record + its fork anchor
	const nlohmann::json *br = nullptr;
	if (root.contains("branches") && root["branches"].is_array())
		for (const auto& b : root["branches"])
			if (b.is_object() && b.value("id", std::string()) == branchId)
			{
				br = &b;
				break;
			}
	if (br == nullptr || !br->contains("forkAnchor") || !(*br)["forkAnchor"].is_object())
	{
		mc.verdict = MergeVerdict::NoAnchor;		// a branch made before fork anchors - can't verify alignment
		return mc;
	}
	const nlohmann::json& fa = (*br)["forkAnchor"];
	mc.slot = fa.value("slot", br->value("fromSlot", -1));
	mc.forkFrame = fa.value("frame", 0u);
	const u64 faHash = fa.value("prefixHash", (u64)0);
	// main's CURRENT state[slot] (root clip.json states[], stamped by WriteClipStats - fresh after any save on main)
	bool found = false;
	u64 mainHash = 0;
	if (root.contains("states") && root["states"].is_array())
		for (const auto& s : root["states"])
			if (s.is_object() && s.value("slot", -1) == mc.slot)
			{
				found = true;
				mc.mainFrame = s.value("movieFrame", 0u);
				mainHash = s.value("prefixHash", (u64)0);
				break;
			}
	if (!found)
		mc.verdict = MergeVerdict::MainMissing;			// main's fork state was deleted
	else if (mc.forkFrame != mc.mainFrame)
		mc.verdict = MergeVerdict::FrameMismatch;		// the fork frame moved (dev's throw corruption)
	else if (faHash != 0 && mainHash != 0 && faHash != mainHash)
		mc.verdict = MergeVerdict::PrefixDiverged;		// main re-recorded below the fork - same frame, different machine
	else
		mc.verdict = MergeVerdict::Ok;					// aligned + identical -> safe to splice
	return mc;
}

void setColor(const std::string& rootDir, const std::string& id, const std::string& hex)
{
	if (rootDir.empty() || id.empty())
		return;
	const std::string iso = tas_clip::utcNowIso();
	nlohmann::json root = tas_clip::read(rootDir);
	bool changed = false;
	if (root.contains("branches") && root["branches"].is_array())
		for (auto& b : root["branches"])
			if (b.is_object() && b.value("id", std::string()) == id)
			{
				b["color"] = hex;
				b["modifiedAt"] = iso;
				changed = true;
				break;
			}
	if (changed)
		tas_clip::write(rootDir, root);
	// mirror onto the branch's own node (standalone traceability)
	const std::string bdir = (ghc::filesystem::path(rootDir) / "branches" / id).string();
	nlohmann::json bj = tas_clip::read(bdir);
	if (bj.is_object() && bj.contains("node") && bj["node"].is_object())
	{
		bj["node"]["color"] = hex;
		bj["node"]["modifiedAt"] = iso;
		tas_clip::write(bdir, bj);
	}
	NOTICE_LOG(NETWORK, "TAS BRANCH: setColor %s = '%s'", id.c_str(), hex.c_str());
}

void setTagsNotes(const std::string& rootDir, const std::string& id, const std::string& tagsCsv, const std::string& notes)
{
	if (rootDir.empty() || id.empty())
		return;
	const std::string iso = tas_clip::utcNowIso();
	nlohmann::json tagsArr = nlohmann::json::array();
	{
		std::vector<std::string> tv;
		tas_clip::parseTags(tagsCsv.c_str(), tv);
		for (const auto& t : tv)
			tagsArr.push_back(t);
	}
	if (id == "main")
	{	// main edits the root clip's own node
		nlohmann::json root = tas_clip::read(rootDir);
		if (!root.is_object())
			return;
		if (!root.contains("node") || !root["node"].is_object())
			root["node"] = nlohmann::json::object();
		root["node"]["tags"] = tagsArr;
		root["node"]["notes"] = notes;
		root["node"]["modifiedAt"] = iso;
		tas_clip::write(rootDir, root);
		NOTICE_LOG(NETWORK, "TAS BRANCH: setTagsNotes main");
		return;
	}
	nlohmann::json root = tas_clip::read(rootDir);
	bool changed = false;
	if (root.contains("branches") && root["branches"].is_array())
		for (auto& b : root["branches"])
			if (b.is_object() && b.value("id", std::string()) == id)
			{
				b["tags"] = tagsArr;
				b["notes"] = notes;
				b["modifiedAt"] = iso;
				changed = true;
				break;
			}
	if (changed)
		tas_clip::write(rootDir, root);
	const std::string bdir = (ghc::filesystem::path(rootDir) / "branches" / id).string();
	nlohmann::json bj = tas_clip::read(bdir);
	if (bj.is_object() && bj.contains("node") && bj["node"].is_object())
	{
		bj["node"]["tags"] = tagsArr;
		bj["node"]["notes"] = notes;
		bj["node"]["modifiedAt"] = iso;
		tas_clip::write(bdir, bj);
	}
	NOTICE_LOG(NETWORK, "TAS BRANCH: setTagsNotes %s", id.c_str());
}

const char *mergeVerdictText(MergeVerdict v)
{
	switch (v)
	{
	case MergeVerdict::Ok:             return "ready to merge";
	case MergeVerdict::NoAnchor:       return "no fork anchor (older branch)";
	case MergeVerdict::MainMissing:    return "main's fork state is gone";
	case MergeVerdict::FrameMismatch:  return "fork frame moved";
	case MergeVerdict::PrefixDiverged: return "main changed below fork";
	}
	return "?";
}

static bool endsWith(const std::string& s, const char *suf)
{
	const size_t n = std::string(suf).size();
	return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

int merge(const std::string& mainDir, const std::string& branchId)
{
	// Gate: only a still-aligned branch may merge (mainDir IS the root - merge runs on main).
	const MergeCheck mc = mergeStatus(mainDir, branchId);
	if (mc.verdict != MergeVerdict::Ok)
	{
		NOTICE_LOG(NETWORK, "TAS BRANCH: merge REFUSED - %s: %s", branchId.c_str(), mergeVerdictText(mc.verdict));
		return -1;
	}
	std::error_code ec;
	const ghc::filesystem::path branchDir = ghc::filesystem::path(mainDir) / "branches" / branchId;
	if (!ghc::filesystem::is_directory(branchDir, ec))
		return -1;

	// 1. F8-BACKUP main first (the undo), TAGGED as a merge from this branch (dev).
	int bkFiles = 0;
	u64 bkBytes = 0;
	const int gen = dojo.ArchiveClipDir(mainDir, &bkFiles, &bkBytes, "gen");
	if (gen > 0)
	{
		const std::string base = ghc::filesystem::path(mainDir).filename().string();
		char gname[208];
		snprintf(gname, sizeof(gname), "%s_gen_%02d", base.c_str(), gen);
		std::string disp = branchId;
		{
			const nlohmann::json root = tas_clip::read(mainDir);
			if (root.contains("branches") && root["branches"].is_array())
				for (const auto& b : root["branches"])
					if (b.is_object() && b.value("id", std::string()) == branchId)
					{
						if (b.contains("tags") && b["tags"].is_array() && !b["tags"].empty())
							disp = tas_clip::joinTags(b["tags"]);
						else if (!b.value("name", std::string()).empty())
							disp = b.value("name", std::string());
						break;
					}
		}
		const std::string tag = "merge from " + disp;
		const std::string note = "Auto-backup of main before merging branch " + branchId;
		tas_clip::setGenerationTagsNotes(mainDir, gname, tag.c_str(), note.c_str());
		NOTICE_LOG(NETWORK, "TAS BRANCH: merge backup %s tagged '%s' (%d files)", gname, tag.c_str(), bkFiles);
	}
	else
		NOTICE_LOG(NETWORK, "TAS BRANCH: merge WARN - F8 backup of main failed (gen=%d); proceeding", gen);

	// 2. Trash main's ORPHAN states (states the branch doesn't have) - the branch's set replaces main's (restore's rule).
	std::set<std::string> inBranch;
	for (const auto& f : ghc::filesystem::directory_iterator(branchDir, ec))
		if (!f.is_directory(ec))
			inBranch.insert(f.path().filename().string());
	int trashed = 0;
	{
		std::vector<ghc::filesystem::path> orphan;
		for (const auto& f : ghc::filesystem::directory_iterator(mainDir, ec))
		{
			if (f.is_directory(ec))
				continue;
			const std::string fn = f.path().filename().string();
			if (fn.find(".state") == std::string::npos || inBranch.count(fn))
				continue;
			orphan.push_back(f.path());
		}
		if (!orphan.empty())
		{
			ghc::filesystem::path trash = ghc::filesystem::path(mainDir) / ".trash" / utcStamp();
			ghc::filesystem::create_directories(trash, ec);
			for (const auto& p : orphan)
			{
				ghc::filesystem::rename(p, trash / p.filename(), ec);
				if (!ec)
					trashed++;
			}
		}
	}

	// 3. Copy the branch's files over main EXCEPT clip.json AND except the macro .txt (both of them - handled in 4).
	int n = 0;
	for (const auto& f : ghc::filesystem::directory_iterator(branchDir, ec))
	{
		if (f.is_directory(ec))
			continue;
		const std::string fn = f.path().filename().string();
		if (fn == "clip.json" || endsWith(fn, "_macro.txt"))
			continue;
		ghc::filesystem::copy_file(f.path(), ghc::filesystem::path(mainDir) / fn,
				ghc::filesystem::copy_options::overwrite_existing, ec);
		if (!ec)
			n++;
	}

	// 4. Macro: bring the branch's REAL macro (its clip.json macroFile - the branch has a stale fork-time copy too)
	//    into main under MAIN's macro name, so main's clip.json macroFile keeps pointing at the right file.
	{
		const nlohmann::json bclip = tas_clip::read(branchDir.string());
		std::string bmac = bclip.value("macroFile", std::string());
		if (bmac.empty() || !ghc::filesystem::exists(branchDir / bmac, ec))
			bmac.clear();
		const std::string mainMacro = ghc::filesystem::path(mainDir).filename().string() + "_macro.txt";
		if (!bmac.empty())
		{
			ghc::filesystem::copy_file(branchDir / bmac, ghc::filesystem::path(mainDir) / mainMacro,
					ghc::filesystem::copy_options::overwrite_existing, ec);
			if (!ec)
				n++;
		}
	}

	// 5. MERGE clip.json: adopt the branch's movie facts + states, keep main's identity (branches[]/node/tags/notes/
	//    contents/macroFile), record mergedFrom + mark this branch merged. (restore step 3's shape; macroFile kept = main's.)
	{
		nlohmann::json live = tas_clip::read(mainDir);
		nlohmann::json snap = tas_clip::read(branchDir.string());
		if (!live.is_object())
			live = nlohmann::json::object();
		if (snap.is_object())
		{
			nlohmann::json lst = (live.contains("stats") && live["stats"].is_object()) ? live["stats"] : nlohmann::json::object();
			const nlohmann::json sst = (snap.contains("stats") && snap["stats"].is_object()) ? snap["stats"] : nlohmann::json::object();
			if (sst.contains("frames"))
				lst["frames"] = sst["frames"];
			if (sst.contains("durationSeconds"))
				lst["durationSeconds"] = sst["durationSeconds"];
			lst["rerecords"] = std::max(lst.value("rerecords", 0u), sst.value("rerecords", 0u));
			live["stats"] = lst;
			for (const char *k : { "rewinds", "bookmarks", "states", "macroBase", "macroHasState0", "macroPairState" })
				if (snap.contains(k))
					live[k] = snap[k];
		}
		nlohmann::json mf = nlohmann::json::object();
		mf["branch"] = branchId;
		const std::string iso = tas_clip::utcNowIso();
		mf["utc"] = iso;
		mf["local"] = tas_clip::localUsTime(iso);
		mf["filesCopied"] = n;
		mf["trashedStateFiles"] = trashed;
		if (gen > 0)
			mf["backupGen"] = gen;
		live["mergedFrom"] = mf;
		if (live.contains("branches") && live["branches"].is_array())
			for (auto& b : live["branches"])
				if (b.is_object() && b.value("id", std::string()) == branchId)
				{
					b["mergedInto"] = std::string("main");
					b["mergedAt"] = iso;
					b["forkMismatch"] = false;	// main now IS this branch at the fork - realigned
					break;
				}
		tas_clip::write(mainDir, live);
	}
	NOTICE_LOG(NETWORK, "TAS BRANCH: merge '%s' -> main OK (%d files, %d orphan state(s) trashed, backup gen %d)",
			branchId.c_str(), n, trashed, gen);
	return n;
}

}
