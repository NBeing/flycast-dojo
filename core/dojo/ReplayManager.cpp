/*
	Copyright 2026 flycast-dojo contributors

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "ReplayManager.hpp"
#include "stdclass.h"
#include "dojo/deps/filesystem.hpp"
#include "dojo/deps/json.hpp"

#include <zip.h>

#include <algorithm>
#include <ctime>
#include <fstream>

namespace fs = ghc::filesystem;
using json = nlohmann::json;

namespace replaymgr
{

static bool isReplayFile(const fs::path& p)
{
	const std::string ext = p.extension().string();
	return ext == ".flyr" || ext == ".flyreplay";
}

std::string ReplayInfo::playersLabel() const
{
	if (hostPlayer.empty() && guestPlayer.empty())
		return "";
	if (guestPlayer.empty())
		return hostPlayer;
	return hostPlayer + " vs " + guestPlayer;
}

std::string replaysDir()
{
	const std::string dir = get_writable_data_path("") + "/replays";
	std::error_code ec;
	if (!fs::exists(dir, ec))
		fs::create_directories(dir, ec);
	return dir;
}

std::string metadataPath(const std::string& replayPath)
{
	return replayPath + ".json";
}

// Recovers what it can from the legacy
// "<game>__<iso8601>__<host>__<guest>__.flyr" convention.
//
// Unlike the parser this replaces, a filename that does not fit is not a
// failure: the caller gets the bare filename as a display name and the
// listing stays usable. Renaming a replay must never break the library.
static void deriveFromFilename(const fs::path& p, ReplayInfo& info)
{
	const std::string stem = p.filename().string();
	info.displayName = p.stem().string();

	std::vector<std::string> parts;
	const std::string delim = "__";
	std::string s = stem;
	size_t pos;
	while ((pos = s.find(delim)) != std::string::npos)
	{
		parts.push_back(s.substr(0, pos));
		s.erase(0, pos + delim.length());
	}

	if (parts.size() >= 1)
		info.game = parts[0];
	if (parts.size() >= 2)
		info.date = parts[1];
	if (parts.size() >= 3)
		info.hostPlayer = parts[2];
	if (parts.size() >= 4)
		info.guestPlayer = parts[3];

	// A well-formed legacy name reads better as "game - host vs guest" than as
	// the raw filename.
	if (parts.size() >= 3)
	{
		info.displayName = info.game;
		const std::string players = info.playersLabel();
		if (!players.empty())
			info.displayName += " - " + players;
	}
}

static std::string fileDate(const fs::path& p)
{
	std::error_code ec;
	const auto ftime = fs::last_write_time(p, ec);
	if (ec)
		return "";
	const std::time_t t = decltype(ftime)::clock::to_time_t(ftime);
	std::tm tm_{};
#ifdef _WIN32
	localtime_s(&tm_, &t);
#else
	localtime_r(&t, &tm_);
#endif
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_);
	return std::string(buf);
}

ReplayInfo describe(const std::string& replayPath)
{
	ReplayInfo info;
	info.path = replayPath;

	const fs::path p(replayPath);
	std::error_code ec;
	const std::string meta = metadataPath(replayPath);
	if (fs::exists(meta, ec))
	{
		try {
			std::ifstream in(meta);
			json j;
			in >> j;
			info.displayName = j.value("name", "");
			info.game = j.value("game", "");
			info.date = j.value("date", "");
			info.hostPlayer = j.value("host", "");
			info.guestPlayer = j.value("guest", "");
			info.notes = j.value("notes", "");
			info.hasMetadata = true;
		} catch (const std::exception& e) {
			WARN_LOG(COMMON, "[replay] unreadable metadata %s: %s", meta.c_str(), e.what());
			info.hasMetadata = false;
		}
	}

	// Fill any gap from the filename, so a partial sidecar still lists well.
	if (!info.hasMetadata || info.displayName.empty() || info.game.empty())
	{
		ReplayInfo derived;
		deriveFromFilename(p, derived);
		if (info.displayName.empty())
			info.displayName = derived.displayName;
		if (info.game.empty())
			info.game = derived.game;
		if (info.date.empty())
			info.date = derived.date;
		if (info.hostPlayer.empty())
			info.hostPlayer = derived.hostPlayer;
		if (info.guestPlayer.empty())
			info.guestPlayer = derived.guestPlayer;
	}
	if (info.date.empty())
		info.date = fileDate(p);
	if (info.displayName.empty())
		info.displayName = p.stem().string();

	return info;
}

std::vector<ReplayInfo> list()
{
	std::vector<ReplayInfo> out;
	const std::string dir = replaysDir();
	std::error_code ec;
	if (!fs::exists(dir, ec))
		return out;

	for (const auto& entry : fs::directory_iterator(dir, ec))
	{
		if (ec)
			break;
		if (!entry.is_regular_file(ec) || !isReplayFile(entry.path()))
			continue;
		out.push_back(describe(entry.path().string()));
	}

	// Newest first. Dates are ISO8601 or "YYYY-MM-DD HH:MM", both of which
	// sort correctly as text.
	std::sort(out.begin(), out.end(), [](const ReplayInfo& a, const ReplayInfo& b) {
		return a.date > b.date;
	});
	return out;
}

bool writeMetadata(const ReplayInfo& info)
{
	try {
		json j;
		j["name"] = info.displayName;
		j["game"] = info.game;
		j["date"] = info.date;
		j["host"] = info.hostPlayer;
		j["guest"] = info.guestPlayer;
		j["notes"] = info.notes;

		std::ofstream out(metadataPath(info.path), std::ios::trunc);
		if (!out)
			return false;
		out << j.dump(2);
		return true;
	} catch (const std::exception& e) {
		WARN_LOG(COMMON, "[replay] could not write metadata for %s: %s",
				info.path.c_str(), e.what());
		return false;
	}
}

bool setDisplayName(const std::string& replayPath, const std::string& name)
{
	ReplayInfo info = describe(replayPath);
	info.displayName = name;
	return writeMetadata(info);
}

bool setNotes(const std::string& replayPath, const std::string& notes)
{
	ReplayInfo info = describe(replayPath);
	info.notes = notes;
	return writeMetadata(info);
}

bool remove(const std::string& replayPath)
{
	std::error_code ec;
	const bool ok = fs::remove(replayPath, ec);
	// The sidecar is best-effort: a replay with no metadata is fine, a
	// metadata file with no replay is litter.
	fs::remove(metadataPath(replayPath), ec);
	return ok;
}

// --- Transfer ---------------------------------------------------------------

static bool addFileToZip(zip_t *za, const std::string& path, const std::string& nameInZip)
{
	zip_source_t *src = zip_source_file(za, path.c_str(), 0, -1);
	if (src == nullptr)
		return false;
	if (zip_file_add(za, nameInZip.c_str(), src, ZIP_FL_OVERWRITE) < 0)
	{
		zip_source_free(src);
		return false;
	}
	return true;
}

bool exportBundle(const std::string& replayPath, const std::string& destZip)
{
	std::error_code ec;
	if (!fs::exists(replayPath, ec))
	{
		WARN_LOG(COMMON, "[replay] cannot export missing %s", replayPath.c_str());
		return false;
	}

	// Make sure the metadata exists so the recipient gets the name and players,
	// even for a legacy replay that never had a sidecar.
	const ReplayInfo info = describe(replayPath);
	writeMetadata(info);

	int err = 0;
	zip_t *za = zip_open(destZip.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
	if (za == nullptr)
	{
		WARN_LOG(COMMON, "[replay] could not create bundle %s (zip error %d)", destZip.c_str(), err);
		return false;
	}

	const fs::path p(replayPath);
	bool ok = addFileToZip(za, replayPath, p.filename().string());
	if (ok)
		ok = addFileToZip(za, metadataPath(replayPath), p.filename().string() + ".json");

	if (zip_close(za) < 0)
		ok = false;
	if (!ok)
	{
		WARN_LOG(COMMON, "[replay] failed writing bundle %s", destZip.c_str());
		fs::remove(destZip, ec);
	}
	return ok;
}

static bool extractEntry(zip_t *za, zip_int64_t index, const std::string& destPath)
{
	zip_stat_t st;
	if (zip_stat_index(za, index, 0, &st) != 0)
		return false;
	zip_file_t *zf = zip_fopen_index(za, index, 0);
	if (zf == nullptr)
		return false;

	std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		zip_fclose(zf);
		return false;
	}
	char buf[16384];
	zip_int64_t n;
	while ((n = zip_fread(zf, buf, sizeof(buf))) > 0)
		out.write(buf, (std::streamsize)n);
	zip_fclose(zf);
	return n >= 0;
}

std::string importBundle(const std::string& srcZip)
{
	int err = 0;
	zip_t *za = zip_open(srcZip.c_str(), ZIP_RDONLY, &err);
	if (za == nullptr)
	{
		WARN_LOG(COMMON, "[replay] could not open bundle %s (zip error %d)", srcZip.c_str(), err);
		return "";
	}

	const std::string dir = replaysDir();
	std::string importedReplay;
	std::string importedMeta;

	const zip_int64_t count = zip_get_num_entries(za, 0);
	for (zip_int64_t i = 0; i < count; i++)
	{
		const char *name = zip_get_name(za, i, 0);
		if (name == nullptr)
			continue;
		// Flatten: a bundle is a replay plus its sidecar, never a tree, and
		// honouring embedded paths would let a crafted zip write outside the
		// replays folder.
		const fs::path entryName = fs::path(name).filename();
		const std::string ext = entryName.extension().string();
		const bool isMeta = (ext == ".json");
		if (!isMeta && !isReplayFile(entryName))
			continue;

		// Do not clobber an existing replay of the same name.
		fs::path dest = fs::path(dir) / entryName;
		std::error_code ec;
		int suffix = 1;
		while (fs::exists(dest, ec) && !isMeta)
		{
			dest = fs::path(dir) /
					(entryName.stem().string() + " (" + std::to_string(suffix++) + ")" + ext);
		}
		if (isMeta && !importedReplay.empty())
			dest = fs::path(metadataPath(importedReplay));

		if (!extractEntry(za, i, dest.string()))
		{
			WARN_LOG(COMMON, "[replay] failed extracting %s", name);
			continue;
		}
		if (isMeta)
			importedMeta = dest.string();
		else
			importedReplay = dest.string();
	}
	zip_close(za);

	if (importedReplay.empty())
	{
		WARN_LOG(COMMON, "[replay] %s contained no replay", srcZip.c_str());
		return "";
	}
	// If the sidecar arrived before the replay we could not name it correctly;
	// re-derive so the library at least lists the import.
	if (importedMeta.empty() || importedMeta != metadataPath(importedReplay))
		writeMetadata(describe(importedReplay));

	INFO_LOG(COMMON, "[replay] imported %s", importedReplay.c_str());
	return importedReplay;
}

}
