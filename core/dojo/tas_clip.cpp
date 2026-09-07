#include "tas_clip.h"
#include <mutex>
#include "log/Log.h"
#include "stdclass.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace tas_clip
{

static std::atomic<u32> libVersion{0};

u32 libraryVersion()
{
	return libVersion.load(std::memory_order_relaxed);
}

void bump()
{
	libVersion.fetch_add(1, std::memory_order_relaxed);
}

// ---- the Test Lab fixture library (see the header) ----
std::string labDir(const std::string& gameName)
{
	return (ghc::filesystem::path(get_writable_data_path("replays")) / gameName / "_lab").string();
}

bool labIsActive(const std::string& savestateFolderOverride)
{
	if (savestateFolderOverride.empty())
		return false;
	const ghc::filesystem::path p(savestateFolderOverride);
	return p.filename().string() == "_lab" || p.parent_path().filename().string() == "_lab";
}

int labTests(const std::string& gameName, std::vector<LabTest>& out)
{
	out.clear();
	const ghc::filesystem::path lab(labDir(gameName));
	std::error_code ec;
	if (!ghc::filesystem::is_directory(lab, ec))
		return 0;
	for (const auto& e : ghc::filesystem::directory_iterator(lab, ec))
	{
		if (!e.is_directory(ec))
			continue;
		const std::string name = e.path().filename().string();
		if (name.empty() || name[0] == (char)46)	// '.' - .trash and friends
			continue;
		LabTest t;
		t.name = name;
		t.dir = e.path().string();
		t.hasBase = ghc::filesystem::exists(e.path() / (gameName + ".state"), ec);
		// tags / notes / created straight from this test's clip.json (top-level fields)
		std::string tagsCsv, notes, created;
		readTagsNotes(t.dir, tagsCsv, notes, &created, nullptr);
		parseTags(tagsCsv.c_str(), t.tags);
		t.notes = notes;
		// states count + newest-file mtime (Modified) + BASE mtime (Created fallback), one pass
		ghc::filesystem::file_time_type modFt{}, baseFt{};
		bool modValid = false, baseValid = false;
		for (const auto& f : ghc::filesystem::directory_iterator(e.path(), ec))
		{
			if (f.is_directory(ec))
				continue;
			if (f.path().extension().string() == ".state")
				t.states++;
			std::error_code te;
			const auto ft = ghc::filesystem::last_write_time(f.path(), te);
			if (te)
				continue;
			if (!modValid || ft > modFt) { modFt = ft; modValid = true; }	// clip.json is in here too -> edits bump Modified
			if (f.path().filename().string() == gameName + ".state") { baseFt = ft; baseValid = true; }
		}
		// Created: clip.json "created" if it is an ISO timestamp (YYYY-...), else the BASE state file's mtime
		const bool createdIsIso = created.size() >= 5 && isdigit((unsigned char)created[0])
			&& isdigit((unsigned char)created[1]) && isdigit((unsigned char)created[2])
			&& isdigit((unsigned char)created[3]) && created[4] == (char)45;
		if (createdIsIso)
			t.createdUtc = created;
		else if (baseValid)
			t.createdUtc = utcIso(decltype(baseFt)::clock::to_time_t(baseFt));
		t.createdLocal = t.createdUtc.empty() ? std::string() : localUsTime(t.createdUtc);
		if (modValid)
			t.modifiedUtc = utcIso(decltype(modFt)::clock::to_time_t(modFt));
		t.modifiedLocal = t.modifiedUtc.empty() ? std::string() : localUsTime(t.modifiedUtc);
		out.push_back(t);
	}
	std::sort(out.begin(), out.end(), [](const LabTest& a, const LabTest& b) { return a.name < b.name; });
	return (int)out.size();
}

std::string labNewTestDir(const std::string& gameName, const std::string& label)
{
	std::vector<LabTest> tests;
	labTests(gameName, tests);
	int n = 0;
	for (const auto& t : tests)
		if (t.name.size() >= 7 && t.name.compare(0, 5, "TEST_") == 0 && isdigit((unsigned char)t.name[5]) && isdigit((unsigned char)t.name[6]))
			n = std::max(n, atoi(t.name.c_str() + 5));
	std::string safe;
	for (char ch : label)
	{
		if (safe.size() >= 32)
			break;
		safe += (isalnum((unsigned char)ch) || ch == (char)45 || ch == (char)95) ? ch : (char)95;	// '-' '_' kept, the rest -> '_'
	}
	while (!safe.empty() && safe.back() == (char)95)
		safe.pop_back();
	char name[64];
	snprintf(name, sizeof(name), "TEST_%02d%s%s", n + 1, safe.empty() ? "" : "_", safe.c_str());
	return (ghc::filesystem::path(labDir(gameName)) / name).string();
}

// ---- the clip.json date conventions (moved from dojo.cpp) ----
std::string utcIso(time_t t)
{
	char buf[40] = "";
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
	return buf;
}

std::string utcNowIso()
{
	return utcIso(time(nullptr));
}

// "2026-08-22T16_37_41Z" or "...T16:37:41Z" -> "08/22/2026 09:37 AM" in local time.
std::string localUsTime(const std::string& iso)
{
	int Y, M, D, h, m, sec;
	char c1, c2;
	if (sscanf(iso.c_str(), "%d-%d-%dT%d%c%d%c%d", &Y, &M, &D, &h, &c1, &m, &c2, &sec) != 8)
		return iso;
	std::tm tmv {};
	tmv.tm_year = Y - 1900;
	tmv.tm_mon = M - 1;
	tmv.tm_mday = D;
	tmv.tm_hour = h;
	tmv.tm_min = m;
	tmv.tm_sec = sec;
#ifdef _WIN32
	time_t t = _mkgmtime(&tmv);
#else
	time_t t = timegm(&tmv);
#endif
	char buf[48] = "";
	strftime(buf, sizeof(buf), "%m/%d/%Y %I:%M %p", localtime(&t));
	return buf;
}

// ---- backup folders (moved from dojo.cpp) ----
// A clip-folder subfolder that is an F8 backup: <clip>_gen_NN, <clip>_setup_NN (macro clips) or a bare gen_NN from
// older builds. kind / num come back for the reconciler.
bool genFolderKind(const std::string& name, const std::string& base, std::string& kind, int& num)
{
	auto tail2 = [&](const std::string& pre) {
		if (name.size() != pre.size() + 2 || name.compare(0, pre.size(), pre) != 0)
			return false;
		if (!isdigit((unsigned char)name[pre.size()]) || !isdigit((unsigned char)name[pre.size() + 1]))
			return false;
		num = atoi(name.c_str() + pre.size());
		return true;
	};
	if (tail2(base + "_gen_") || tail2("gen_"))
	{
		kind = "gen";
		return true;
	}
	if (tail2(base + "_setup_"))
	{
		kind = "setup";
		return true;
	}
	return false;
}

// Slot number of a state file inside a backup: <game>.state = 0, <game>_N.state = N.
int stateFileSlot(const ghc::filesystem::path& p)
{
	const std::string stem = p.stem().string();
	const size_t us = stem.rfind((char)95);	// underscore
	if (us == std::string::npos || us + 1 >= stem.size())
		return 0;
	for (size_t i = us + 1; i < stem.size(); i++)
		if (!isdigit((unsigned char)stem[i]))
			return 0;
	return atoi(stem.c_str() + us + 1);
}

int archive(const std::string& clipDir, int *filesCopied, u64 *bytesCopied, const char *tag)
{
	ghc::filesystem::path clip(clipDir);
	// Backups carry the clip's identity so they stay traceable if moved/copied out:
	// "<clipName>_gen_NN" (bare "gen_NN" folders from older builds still count for numbering).
	std::string base = clip.filename().string();
	// Numbering (guardrail, 2026-09-03): the next number is one past the HIGHEST ever used for this kind - on disk OR
	// recorded in clip.json. The old first-free-slot rule reused the number of a deleted backup, and the reconciler
	// (matching by name) then grafted the deleted entry's facts onto the new folder.
	int used = 0;
	std::error_code ec;
	for (const auto& dEnt : ghc::filesystem::directory_iterator(clip, ec))
	{
		if (!dEnt.is_directory(ec))
			continue;
		std::string k;
		int n = 0;
		if (genFolderKind(dEnt.path().filename().string(), base, k, n) && k == tag)
			used = std::max(used, n);
	}
	{
		std::ifstream in((clip / "clip.json").string());
		if (in.good())
		{
			try
			{
				nlohmann::json j = nlohmann::json::parse(in, nullptr, true, true);
				if (j.is_object() && j.contains("generations") && j["generations"].is_array())
					for (const auto& e : j["generations"])
						if (e.is_object() && e.value("kind", std::string("gen")) == tag)
							used = std::max(used, e.value("gen", 0));
			}
			catch (...) {}
		}
	}
	int gen = used + 1;
	if (gen > 99)
		return -1;
	char name[192];
	snprintf(name, sizeof(name), "%s_%s_%02d", base.c_str(), tag, gen);
	ghc::filesystem::path genDir = clip / name;
	if (ghc::filesystem::exists(genDir, ec))
		return -1;	// cannot happen after the max rule - belt and braces
	ghc::filesystem::create_directories(genDir, ec);
	int copied = 0;
	u64 bytes = 0;
	// Everything in the clip folder is copied by extension - there is no slot ceiling here, so a
	// backup carries however many of the 100 slots actually exist, their .frame/.png sidecars, the
	// movie itself and clip.json. At ~10-28 MB per state that adds up fast, hence the size report.
	for (const auto& f : ghc::filesystem::directory_iterator(clip, ec))
	{
		if (f.is_directory(ec))
			continue;		// prior gen_NN folders are directories - never nested
		std::string ext = f.path().extension().string();
		if (ext == ".flyr" || ext == ".flyreplay" || ext == ".state" || ext == ".frame"
				|| ext == ".json" || ext == ".png" || ext == ".label" || ext == ".txt" || ext == ".env" || ext == ".wave" || ext == ".map")	// .txt carries a macro clip's macro.txt
		{
			std::error_code sizeEc;
			u64 sz = (u64)ghc::filesystem::file_size(f.path(), sizeEc);
			ghc::filesystem::copy_file(f.path(), genDir / f.path().filename(),
					ghc::filesystem::copy_options::overwrite_existing, ec);
			if (!ec)
			{
				copied++;
				if (!sizeEc)
					bytes += sz;
			}
		}
	}
	NOTICE_LOG(NETWORK, "TAS GEN: state backup %d file(s), %.1f MB -> %s",
			copied, bytes / 1048576.0, genDir.string().c_str());
	if (filesCopied != nullptr)
		*filesCopied = copied;
	if (bytesCopied != nullptr)
		*bytesCopied = bytes;
	return gen;
}

int restore(const std::string& clipDir, const std::string& genName)
{
	ghc::filesystem::path gd = ghc::filesystem::path(clipDir) / genName;
	std::error_code ec;
	if (!ghc::filesystem::is_directory(gd, ec))
		return -1;
	// 1. Live state files the backup does not have keep sequence numbers minted against another movie and confuse the
	//    dead-timeline guard: move them, with their sidecars, to <clip>/.trash/<utc>/ (never delete).
	std::set<std::string> inGen;
	for (const auto& f : ghc::filesystem::directory_iterator(gd, ec))
		if (!f.is_directory(ec))
			inGen.insert(f.path().filename().string());
	int trashed = 0;
	{
		std::vector<ghc::filesystem::path> orphan;
		for (const auto& f : ghc::filesystem::directory_iterator(clipDir, ec))
		{
			if (f.is_directory(ec))
				continue;
			const std::string fn = f.path().filename().string();
			if (fn.find(".state") == std::string::npos || inGen.count(fn))
				continue;	// not a state / sidecar, or the backup carries the same file (it is overwritten below)
			orphan.push_back(f.path());
		}
		if (!orphan.empty())
		{
			std::string stamp = utcNowIso();
			for (char& c : stamp)
				if (c == (char)58)
					c = (char)95;	// colon -> underscore, a folder-safe stamp
			ghc::filesystem::path trash = ghc::filesystem::path(clipDir) / ".trash" / stamp;
			ghc::filesystem::create_directories(trash, ec);
			for (const auto& p : orphan)
			{
				ghc::filesystem::rename(p, trash / p.filename(), ec);
				if (!ec)
					trashed++;
			}
		}
	}
	// 2. Copy everything EXCEPT clip.json - a wholesale copy regressed generations[], tags and notes to backup time.
	int n = 0;
	for (const auto& f : ghc::filesystem::directory_iterator(gd, ec))
		if (!f.is_directory(ec) && f.path().filename().string() != "clip.json")
		{
			ghc::filesystem::copy_file(f.path(), ghc::filesystem::path(clipDir) / f.path().filename(),
					ghc::filesystem::copy_options::overwrite_existing, ec);
			if (!ec)
				n++;
		}
	// 3. MERGE the backup's clip.json: what DESCRIBES the restored movie comes from the backup (stats.frames /
	//    durationSeconds, rewinds, bookmarks, states[], the State 0 pairing); the sequence clock never runs backwards
	//    (stats.rerecords = max); what belongs to the LIVE clip stays (generations[], tags, notes, contents, identity).
	{
		const std::string livePath = clipDir + "/clip.json";
		nlohmann::json live = nlohmann::json::object(), snap = nlohmann::json::object();
		{
			std::ifstream in(livePath);
			if (in.good())
			{
				try { live = nlohmann::json::parse(in, nullptr, true, true); }
				catch (...) { live = nlohmann::json::object(); }
			}
			std::ifstream sin((gd / "clip.json").string());
			if (sin.good())
			{
				try { snap = nlohmann::json::parse(sin, nullptr, true, true); }
				catch (...) { snap = nlohmann::json::object(); }
			}
		}
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
		nlohmann::json rf = nlohmann::json::object();
		rf["generation"] = genName;
		const std::string iso = utcNowIso();
		rf["utc"] = iso;
		rf["local"] = localUsTime(iso);
		rf["filesCopied"] = n;
		rf["trashedStateFiles"] = trashed;
		live["restoredFrom"] = rf;
		write(clipDir, live);
	}
	NOTICE_LOG(NETWORK, "TAS GEN: live replaced with %s (%d files copied, %d live-only state file(s) moved to .trash, clip.json merged)",
			genName.c_str(), n, trashed);
	return n;
}

// Movie length straight from a .flyr: messages of a 12-byte header (body size, seq, cmd); cmd 6 = u32 record size +
// [u32 frame][24 B] records; the loader is last-write-wins per frame, so the length is max frame + 1.
static u32 flyrFrameCount(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f.good())
		return 0;
	std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	const size_t got = buf.size();
	size_t pos = 0;
	u32 maxFrame = 0;
	bool any = false;
	while (pos + 12 <= got)
	{
		u32 bodySize, cmd;
		memcpy(&bodySize, &buf[pos], 4);
		memcpy(&cmd, &buf[pos + 8], 4);
		pos += 12;
		if (bodySize > got - pos)
			break;
		if (cmd == 6 && bodySize >= 4)
		{
			u32 fs;
			memcpy(&fs, &buf[pos], 4);
			if (fs >= 4 && fs <= 256)
				for (size_t o = pos + 4; o + fs <= pos + bodySize; o += fs)
				{
					u32 fr;
					memcpy(&fr, &buf[o], 4);
					if (!any || fr > maxFrame)
						maxFrame = fr;
					any = true;
				}
		}
		pos += bodySize;
	}
	return any ? maxFrame + 1 : 0;
}

// BACKFILL (David, 2026-09-04 - temporary in spirit, permanent in place): backups made before schema 6 recorded no
// facts, and entries the reconciler synthesized from a bare folder have none either. Every F8 copy carries the clip.json
// SNAPSHOT of that moment (ArchiveGeneration writes the stats first, then copies), so the facts can be recovered from
// inside the backup itself: stats.frames -> movieFrames (the movie's actual length as the session saw it - for a macro
// clip that is the macro's rows, the same number a new record carries), stats.rerecords -> rerecords, mode -> mode,
// states[].movieFrame -> slotFrames (and slots when empty). No snapshot stats: the .flyr length stands in. atFrame
// (the playhead when F8 was pressed) was never captured and stays absent - the popup shows a dash for it.
// Only ABSENT fields are filled; a full record is never touched. Entries touched here get backfilled: true.
//
// NOTE FOR THE FUTURE - keep this in step with RecordGeneration: whenever a NEW fact is added to the record there,
// add its derivation here too, so the existing backups catch up on their next reconcile (session open, F8, the
// Generations popup / Rescan). This block is the one place that turns old folders into full records.
static void backfillFromFolder(nlohmann::json& e, const ghc::filesystem::path& genDir)
{
	const bool needFrames = !e.contains("movieFrames");
	const bool needRerec = !e.contains("rerecords");
	const bool needMode = !e.contains("mode");
	const bool needSlotFrames = !e.contains("slotFrames") || !e["slotFrames"].is_array() || e["slotFrames"].empty();
	if (!needFrames && !needRerec && !needMode && !needSlotFrames)
		return;	// a full record
	const nlohmann::json snap = read(genDir.string());	// the clip.json copied INTO the backup at backup time
	const nlohmann::json sst = (snap.contains("stats") && snap["stats"].is_object()) ? snap["stats"] : nlohmann::json::object();
	bool touched = false;
	if (needFrames)
	{
		u32 mf = sst.value("frames", 0u);
		if (mf == 0)
		{	// no snapshot stats (a very old backup): the copied movie file itself
			std::error_code ec;
			for (const auto& f : ghc::filesystem::directory_iterator(genDir, ec))
			{
				const std::string ext = f.path().extension().string();
				if (!f.is_directory(ec) && (ext == ".flyr" || ext == ".flyreplay"))
				{
					mf = flyrFrameCount(f.path().string());
					break;
				}
			}
		}
		if (mf != 0)
		{
			e["movieFrames"] = mf;
			touched = true;
		}
	}
	if (needRerec && sst.contains("rerecords"))
	{
		e["rerecords"] = sst.value("rerecords", 0u);
		touched = true;
	}
	if (needMode && !snap.empty())
	{
		e["mode"] = snap.value("mode", std::string("movie"));	// absent in the snapshot = a Movie clip
		touched = true;
	}
	if (needSlotFrames && snap.contains("states") && snap["states"].is_array())
	{
		nlohmann::json sf = nlohmann::json::array(), sl = nlohmann::json::array();
		for (const auto& st : snap["states"])
			if (st.is_object() && st.contains("slot") && st["slot"].is_number())
			{
				sf.push_back(nlohmann::json::array({ st["slot"].get<int>(), st.value("movieFrame", 0u) }));
				sl.push_back(st["slot"].get<int>());
			}
		if (!sf.empty())
		{
			e["slotFrames"] = sf;
			if (!e.contains("slots") || !e["slots"].is_array() || e["slots"].empty())
				e["slots"] = sl;
			touched = true;
		}
	}
	if (touched)
		e["backfilled"] = true;
}

// (review) clip.json is read by the GUI thread and written by the emu thread (WriteClipStats on a state save): one lock
// around every read / write / reconcile, and an ATOMIC write (tmp + rename) so a reader never sees a torn file.
static std::recursive_mutex clipMutex;

void reconcile(const std::string& clipDir)
{
	if (clipDir.empty())
		return;
	std::lock_guard<std::recursive_mutex> lk(clipMutex);	// the whole read-modify-write
	const std::string path = clipDir + "/clip.json";
	nlohmann::json j = nlohmann::json::object();
	{
		std::ifstream in(path);
		if (in.good())
		{
			try { j = nlohmann::json::parse(in, nullptr, true, true); }
			catch (...)
			{	// (review) fail CLOSED: an existing file that does not parse is left alone, never rebuilt from {}
				NOTICE_LOG(NETWORK, "TAS GEN: reconcile skipped - %s does not parse (left untouched)", path.c_str());
				return;
			}
		}
	}
	if (!j.is_object())
		j = nlohmann::json::object();
	const nlohmann::json before = j;	// write only on a difference (David): every write bumps the library version and the Macros windows rescan on it
	std::vector<nlohmann::json> gens;
	if (j.contains("generations") && j["generations"].is_array())
		for (const auto& e : j["generations"])
			if (e.is_object())
				gens.push_back(e);
	const std::string base = ghc::filesystem::path(clipDir).filename().string();
	std::error_code ec;
	std::set<std::string> onDisk;
	int folders = 0;
	for (const auto& dEnt : ghc::filesystem::directory_iterator(clipDir, ec))
	{
		if (!dEnt.is_directory(ec))
			continue;
		const std::string nm = dEnt.path().filename().string();
		std::string kind;
		int num = 0;
		if (!genFolderKind(nm, base, kind, num))
			continue;
		folders++;
		onDisk.insert(nm);
		int files = 0;
		u64 bytes = 0;
		std::vector<int> slotNums;
		for (const auto& f : ghc::filesystem::directory_iterator(dEnt.path(), ec))
		{
			if (f.is_directory(ec))
				continue;
			files++;
			std::error_code se;
			bytes += (u64)ghc::filesystem::file_size(f.path(), se);
			if (f.path().extension() == ".state")
				slotNums.push_back(stateFileSlot(f.path()));
		}
		std::sort(slotNums.begin(), slotNums.end());
		nlohmann::json slots = nlohmann::json::array();
		for (int sn : slotNums)
			slots.push_back(sn);
		nlohmann::json *found = nullptr;
		for (auto& e : gens)
			if (e.value("name", std::string()) == nm)
			{
				found = &e;
				break;
			}
		if (found == nullptr)
		{	// no record: synthesize one from the folder (a setup from before setups were recorded, or a hand copy)
			nlohmann::json e = nlohmann::json::object();
			e["gen"] = num;
			e["kind"] = kind;
			e["name"] = nm;
			e["files"] = files;
			e["bytes"] = bytes;
			e["slots"] = slots;
			std::error_code tec;
			const auto ft = ghc::filesystem::last_write_time(dEnt.path(), tec);
			if (!tec)
			{
				const std::string iso = utcIso(decltype(ft)::clock::to_time_t(ft));
				e["createdUtc"] = iso;
				e["createdLocal"] = localUsTime(iso);
			}
			e["recovered"] = true;
			e["tags"] = nlohmann::json::array();
			e["notes"] = "";
			e["present"] = true;
			backfillFromFolder(e, dEnt.path());	// the facts the folder can still tell
			gens.push_back(e);
		}
		else
		{
			nlohmann::json& e = *found;
			e["present"] = true;
			e["files"] = files;
			e["bytes"] = bytes;
			if (!e.contains("kind"))
				e["kind"] = kind;
			if (!e.contains("slots") || !e["slots"].is_array() || e["slots"].empty())
				e["slots"] = slots;
			if (!e.contains("tags") || !e["tags"].is_array())
				e["tags"] = nlohmann::json::array();
			if (!e.contains("notes"))
				e["notes"] = "";
			backfillFromFolder(e, dEnt.path());	// absent facts only - a full record is never touched
		}
	}
	int presentCount = 0;
	std::string latest, latestIso;
	for (auto& e : gens)
	{
		const std::string nm = e.value("name", std::string());
		const bool present = !nm.empty() && onDisk.count(nm) != 0;
		e["present"] = present;
		if (!present)
			continue;
		presentCount++;
		const std::string iso = e.value("createdUtc", std::string());
		if (latest.empty() || iso > latestIso)
		{	// ISO-8601 UTC sorts as text
			latest = nm;
			latestIso = iso;
		}
	}
	// ONE generation system (David, 2026-09-04): gens and the older setup folders interleave by date - kind is a legacy
	// folder detail now, "setup" is a tag.
	std::stable_sort(gens.begin(), gens.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
		const std::string ca = a.value("createdUtc", std::string()), cb = b.value("createdUtc", std::string());
		if (ca != cb)
			return ca < cb;
		return a.value("gen", 0) < b.value("gen", 0);
	});
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& e : gens)
		arr.push_back(e);
	j["generations"] = arr;
	j["generationCount"] = (u32)presentCount;
	j["latestGeneration"] = latest;
	if (j.value("schema", 0) < 6)
		j["schema"] = 6;
	if (j == before)
	{	// nothing new on disk: no write, no library bump (a plain row click used to rewrite clip.json and trigger a full macro rescan)
		DEBUG_LOG(NETWORK, "TAS GEN: reconciled %s - unchanged (%d backup folder(s))", base.c_str(), folders);
		return;
	}
	write(clipDir, j);	// through the one writer: bumps the library version the browsers rescan on
	NOTICE_LOG(NETWORK, "TAS GEN: reconciled %s - %d backup folder(s) on disk, %d recorded", base.c_str(), folders, (int)gens.size());
}

int deleteGenerations(const std::string& clipDir, const std::vector<std::string>& genNames)
{
	if (clipDir.empty() || genNames.empty())
		return 0;
	std::lock_guard<std::recursive_mutex> lk(clipMutex);	// the WHOLE op: the folder moves + the clip.json rewrite, one critical section
	std::error_code ec;
	// 1) move each backup folder to <clip>/.trash/<utc>/<genName> - never a hard delete (restore does the same). A record
	//    whose folder is already gone (present:false) just loses its record. If a move FAILS, that name is skipped and its
	//    record is kept - a removed record over a live folder would make reconcile resynthesize it, losing the user's tags.
	std::set<std::string> moved;
	std::string stamp = utcNowIso();
	for (char& c : stamp)
		if (c == (char)58)
			c = (char)95;	// colon -> underscore, a folder-safe stamp
	const ghc::filesystem::path trash = ghc::filesystem::path(clipDir) / ".trash" / stamp;
	for (const std::string& name : genNames)
	{
		if (name.empty())
			continue;
		const ghc::filesystem::path genDir = ghc::filesystem::path(clipDir) / name;
		if (ghc::filesystem::is_directory(genDir, ec))
		{
			ghc::filesystem::create_directories(trash, ec);
			ghc::filesystem::rename(genDir, trash / name, ec);
			if (ec)
			{
				NOTICE_LOG(NETWORK, "TAS GEN: delete - could not move %s to .trash (%s) - kept", name.c_str(), ec.message().c_str());
				continue;
			}
		}
		moved.insert(name);	// folder gone (trashed now, or already absent) -> its record may be removed
	}
	if (moved.empty())
		return 0;
	// 2) drop the records, recompute the present-only count / latest / contents.generationFolders (reconcile's rule)
	nlohmann::json j = read(clipDir);
	if (!j.contains("generations") || !j["generations"].is_array())
		return 0;
	nlohmann::json kept = nlohmann::json::array();
	int removed = 0;
	for (const auto& e : j["generations"])
	{
		if (e.is_object() && moved.count(e.value("name", std::string())) != 0)
		{
			removed++;
			continue;
		}
		kept.push_back(e);
	}
	if (removed == 0)
		return 0;
	int presentCount = 0;
	std::string latest, latestIso;
	for (const auto& e : kept)
	{
		if (!e.value("present", true))
			continue;
		presentCount++;
		const std::string iso = e.value("createdUtc", std::string());
		if (latest.empty() || iso > latestIso)
		{
			latest = e.value("name", std::string());
			latestIso = iso;
		}
	}
	j["generations"] = kept;
	j["generationCount"] = (u32)presentCount;
	j["latestGeneration"] = latest;
	if (j.contains("contents") && j["contents"].is_object())
		j["contents"]["generationFolders"] = presentCount;
	if (j.value("schema", 0) < 6)
		j["schema"] = 6;
	write(clipDir, j);	// atomic (tmp + rename); bumps the library version every reader keys on
	NOTICE_LOG(NETWORK, "TAS GEN: deleted %d generation(s) from %s (moved to .trash/%s), %d present remain",
			removed, ghc::filesystem::path(clipDir).filename().string().c_str(), stamp.c_str(), presentCount);
	return removed;
}

bool deleteGeneration(const std::string& clipDir, const std::string& genName)
{
	std::vector<std::string> one{ genName };
	return deleteGenerations(clipDir, one) > 0;
}

// ---- clip.json (P2: every read-modify-write in the tree goes through these two) ----
nlohmann::json read(const std::string& clipDir)
{
	std::lock_guard<std::recursive_mutex> lk(clipMutex);
	nlohmann::json j = nlohmann::json::object();
	if (clipDir.empty())
		return j;
	std::ifstream in(clipDir + "/clip.json");
	if (in.good())
	{
		try { j = nlohmann::json::parse(in, nullptr, true, true); }
		catch (...) { j = nlohmann::json::object(); }
	}
	if (!j.is_object())
		j = nlohmann::json::object();
	return j;
}

bool write(const std::string& clipDir, const nlohmann::json& j)
{
	if (clipDir.empty())
		return false;
	std::lock_guard<std::recursive_mutex> lk(clipMutex);
	// ATOMIC: dump to clip.json.tmp, then rename over clip.json. A torn read used to parse as {} and a reconcile then
	// rewrote the clip's whole record set from that (review).
	const std::string path = clipDir + "/clip.json", tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out.good())
			return false;
		out << j.dump(2) << std::endl;
		if (!out.good())
			return false;
	}
	std::error_code ec;
	ghc::filesystem::rename(tmp, path, ec);
	if (ec)
	{
		ghc::filesystem::remove(tmp, ec);
		return false;
	}
	bump();
	return true;
}

void parseTags(const char *csv, std::vector<std::string>& out)
{
	out.clear();
	if (csv == nullptr)
		return;
	std::string t;
	for (const char *p = csv; ; p++)
	{
		if (*p == (char)44 || *p == 0)
		{	// comma or end: one tag, trimmed of spaces / tabs
			const size_t a = t.find_first_not_of(" 	");
			const size_t b = t.find_last_not_of(" 	");
			if (a != std::string::npos)
				out.push_back(t.substr(a, b - a + 1));
			t.clear();
			if (*p == 0)
				break;
		}
		else
			t += *p;
	}
}

std::string joinTags(const nlohmann::json& tags)
{
	std::string s;
	if (!tags.is_array())
		return s;
	for (const auto& t : tags)
		if (t.is_string())
			s += (s.empty() ? "" : ", ") + t.get<std::string>();
	return s;
}

static bool tagEq(const std::string& a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i] != 0; i++)
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return false;
	return i == a.size() && b[i] == 0;
}

bool hasTag(const std::vector<std::string>& tags, const char *tag)
{
	for (const auto& t : tags)
		if (tagEq(t, tag))
			return true;
	return false;
}

void toggleTagCsv(char *buf, size_t sz, const char *tag)
{
	std::vector<std::string> have;
	parseTags(buf, have);
	bool removed = false;
	for (size_t i = 0; i < have.size(); i++)
		if (tagEq(have[i], tag))
		{
			have.erase(have.begin() + i);
			removed = true;
			break;
		}
	if (!removed)
		have.push_back(tag);
	std::string csv;
	for (const auto& t : have)
		csv += (csv.empty() ? "" : ", ") + t;
	snprintf(buf, sz, "%s", csv.c_str());
}

bool readTagsNotes(const std::string& clipDir, std::string& tagsCsv, std::string& notes, std::string *created, std::string *mode)
{
	tagsCsv.clear();
	notes.clear();
	if (created != nullptr)
		created->clear();
	if (mode != nullptr)
		mode->clear();
	std::error_code ec;
	if (clipDir.empty() || !ghc::filesystem::exists(ghc::filesystem::path(clipDir) / "clip.json", ec))
		return false;
	const nlohmann::json j = read(clipDir);
	if (j.contains("tags"))
		tagsCsv = joinTags(j["tags"]);
	if (j.contains("notes") && j["notes"].is_string())
		notes = j["notes"].get<std::string>();
	if (created != nullptr && j.contains("created") && j["created"].is_string())
		*created = j["created"].get<std::string>();
	if (mode != nullptr && j.contains("mode") && j["mode"].is_string())
		*mode = j["mode"].get<std::string>();
	return true;
}

bool writeTagsNotes(const std::string& clipDir, const char *tagsCsv, const char *notes)
{
	nlohmann::json j = read(clipDir);
	std::vector<std::string> tv;
	parseTags(tagsCsv, tv);
	nlohmann::json tags = nlohmann::json::array();
	for (const auto& t : tv)
		tags.push_back(t);
	j["tags"] = tags;
	j["notes"] = notes != nullptr ? notes : "";
	return write(clipDir, j);
}

bool setGenerationTagsNotes(const std::string& clipDir, const std::string& genName, const char *tagsCsv, const char *notes)
{
	nlohmann::json j = read(clipDir);
	if (!j.contains("generations") || !j["generations"].is_array())
		return false;
	std::vector<std::string> tv;
	parseTags(tagsCsv, tv);
	nlohmann::json tags = nlohmann::json::array();
	for (const auto& t : tv)
		tags.push_back(t);
	bool hit = false;
	for (auto& e : j["generations"])
		if (e.is_object() && e.value("name", std::string()) == genName)
		{
			e["tags"] = tags;
			e["notes"] = notes != nullptr ? notes : "";
			hit = true;
		}
	return hit && write(clipDir, j);
}

bool appendGeneration(const std::string& clipDir, const nlohmann::json& entry)
{
	nlohmann::json j = read(clipDir);
	nlohmann::json gens = (j.contains("generations") && j["generations"].is_array()) ? j["generations"] : nlohmann::json::array();
	gens.push_back(entry);
	j["generations"] = gens;
	j["generationCount"] = (u32)gens.size();
	j["latestGeneration"] = entry.value("name", std::string());
	if (j.value("schema", 0) < 6)
		j["schema"] = 6;
	return write(clipDir, j);
}

bool renameMeta(const std::string& newClipDir, const std::string& oldName, const std::string& newName)
{
	std::error_code ec;
	if (!ghc::filesystem::exists(ghc::filesystem::path(newClipDir) / "clip.json", ec))
		return false;
	nlohmann::json j = read(newClipDir);
	auto renamed = [&](const std::string& s) -> std::string {
		std::string gkind;
		int gnum = 0;
		if (!genFolderKind(s, oldName, gkind, gnum))
			return s;
		char tn[192];
		snprintf(tn, sizeof(tn), "%s_%s_%02d", newName.c_str(), gkind.c_str(), gnum);
		return std::string(tn);
	};
	if (j.contains("macroFile") && j["macroFile"] == oldName + "_macro.txt")
		j["macroFile"] = newName + "_macro.txt";
	if (j.contains("generations") && j["generations"].is_array())
		for (auto& e : j["generations"])
			if (e.is_object() && e.contains("name") && e["name"].is_string())
				e["name"] = renamed(e["name"].get<std::string>());
	if (j.contains("latestGeneration") && j["latestGeneration"].is_string())
		j["latestGeneration"] = renamed(j["latestGeneration"].get<std::string>());
	// restoredFrom names the backup live came from - renamed with the rest, or the LIVE marker and the "live = " line
	// would point at a folder name that no longer exists after a clip rename (review, 2026-09-04).
	if (j.contains("restoredFrom") && j["restoredFrom"].is_object() && j["restoredFrom"].contains("generation")
			&& j["restoredFrom"]["generation"].is_string())
		j["restoredFrom"]["generation"] = renamed(j["restoredFrom"]["generation"].get<std::string>());
	if (j.contains("contents") && j["contents"].is_object())
	{
		nlohmann::json& ct = j["contents"];
		if (ct.contains("macro") && ct["macro"] == oldName + "_macro.txt")
			ct["macro"] = newName + "_macro.txt";
		if (ct.contains("movie") && ct["movie"].is_string())
		{
			const std::string mv = ct["movie"].get<std::string>();
			const size_t dot = mv.rfind((char)46);
			if (dot != std::string::npos)
				ct["movie"] = newName + mv.substr(dot);	// the .flyr is renamed to <newName>.flyr by the caller
		}
	}
	return write(newClipDir, j);
}

bool seed(const std::string& clipDir, const std::string& game, const std::string& created, const std::string& tagsCsv, const std::string& notes)
{
	nlohmann::json meta = nlohmann::json::object();
	meta["game"] = game;
	meta["created"] = created;
	meta["notes"] = notes;
	std::vector<std::string> tv;
	parseTags(tagsCsv.c_str(), tv);
	nlohmann::json tags = nlohmann::json::array();
	for (const auto& t : tv)
		tags.push_back(t);
	meta["tags"] = tags;
	return write(clipDir, meta);
}

bool loadGenerations(const std::string& clipDir, std::vector<Generation>& out)
{
	out.clear();
	const nlohmann::json j = read(clipDir);
	if (!j.contains("generations") || !j["generations"].is_array())
		return false;
	for (const auto& e : j["generations"])
	{
		if (!e.is_object())
			continue;
		Generation g;
		g.gen = e.value("gen", 0);
		g.kind = e.value("kind", std::string("gen"));
		g.name = e.value("name", std::string());
		g.createdUtc = e.value("createdUtc", std::string());
		g.createdLocal = e.value("createdLocal", std::string());
		g.mode = e.value("mode", std::string());
		g.notes = e.value("notes", std::string());
		g.files = e.value("files", 0);
		g.bytes = e.value("bytes", (u64)0);
		g.atFrame = e.value("atFrame", 0u);
		g.movieFrames = e.value("movieFrames", 0u);
		g.rerecords = e.value("rerecords", 0u);
		g.present = e.value("present", true);
		g.recovered = e.value("recovered", false);
		if (e.contains("tags") && e["tags"].is_array())
			for (const auto& t : e["tags"])
				if (t.is_string())
					g.tags.push_back(t.get<std::string>());
		if (e.contains("slots") && e["slots"].is_array())
			for (const auto& s : e["slots"])
				if (s.is_number())
					g.slots.push_back(s.get<int>());
		if (e.contains("slotFrames") && e["slotFrames"].is_array())
			for (const auto& sf : e["slotFrames"])
				if (sf.is_array() && sf.size() == 2 && sf[0].is_number() && sf[1].is_number())
					g.slotFrames.emplace_back(sf[0].get<int>(), sf[1].get<u32>());
		out.push_back(g);
	}
	return true;
}

}

// The global name every existing caller uses (declared in dojo.h).
bool tasGenFolderKind(const std::string& name, const std::string& base, std::string& kind, int& num)
{
	return tas_clip::genFolderKind(name, base, kind, num);
}
