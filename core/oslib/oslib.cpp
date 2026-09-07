/*
	Copyright 2021 flyinghead

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
#include "oslib.h"
#include "stdclass.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "nowide/fstream.hpp"
#include "storage.h"
#include "dojo/deps/filesystem.hpp"	// directory scan for savestate slots
#include <algorithm>
#include <fstream>
#include <iterator>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace hostfs
{

std::string getVmuPath(const std::string& port)
{
	if (port == "A1" && config::PerGameVmu && !settings.content.path.empty())
		return get_game_save_prefix() + "_vmu_save_A1.bin";

	char tempy[512];
	sprintf(tempy, "vmu_save_%s.bin", port.c_str());
	// VMU saves used to be stored in .reicast, not in .reicast/data
	std::string apath = get_writable_config_path(tempy);
	if (!file_exists(apath))
		apath = get_writable_data_path(tempy);
	return apath;
}

std::string getArcadeFlashPath()
{
	std::string nvmemSuffix = cfgLoadStr("net", "nvmem", "");
	return get_game_save_prefix() + nvmemSuffix;
}

std::string findFlash(const std::string& prefix, const std::string& names)
{
	const size_t npos = std::string::npos;
	size_t start = 0;
	while (start < names.size())
	{
		size_t semicolon = names.find(';', start);
		std::string name = names.substr(start, semicolon == npos ? semicolon : semicolon - start);

		size_t percent = name.find('%');
		if (percent != npos)
			name = name.replace(percent, 1, prefix);

		std::string fullpath = get_readonly_data_path(name);
		if (file_exists(fullpath))
			return fullpath;
		for (const auto& path : config::ContentPath.get())
		{
			fullpath = path + "/" + name;
			if (file_exists(fullpath))
				return fullpath;
		}

		start = semicolon;
		if (start != npos)
			start++;
	}
	return "";

}

std::string getFlashSavePath(const std::string& prefix, const std::string& name)
{
	return get_writable_data_path(prefix + name);
}

std::string findNaomiBios(const std::string& name)
{
	std::string fullpath = get_readonly_data_path(name);
	if (file_exists(fullpath))
		return fullpath;
	for (const auto& path : config::ContentPath.get())
	{
		try {
			fullpath = hostfs::storage().getSubPath(path, name);
			hostfs::storage().getFileInfo(fullpath);
			return fullpath;
		} catch (const hostfs::StorageException& e) {
		}
	}
	return "";
}

std::string savestateFolderOverride;

int clampSavestateSlot(int slot)
{
	if (slot < 0)
		return 0;
	return slot >= MAX_SAVESTATE_SLOTS ? MAX_SAVESTATE_SLOTS - 1 : slot;
}

int currentSavestateSlot()
{
	return clampSavestateSlot((int)config::SavestateSlot);
}

int savestateCycleCount()
{
	int n = cfgLoadInt("dojo", "SlotCycleCount", MAX_SAVESTATE_SLOTS);
	if (n < 2)
		n = 2;
	return n > MAX_SAVESTATE_SLOTS ? MAX_SAVESTATE_SLOTS : n;
}

// The folder savestates are read/written in right now: a movie clip's own directory while a
// recording or replay is active, otherwise the shared data path.
static std::string savestateDir()
{
	if (!savestateFolderOverride.empty())
		return savestateFolderOverride;
	if (settings.content.fileName.empty())
		return std::string();
	std::string p = get_writable_data_path(get_file_basename(settings.content.fileName) + ".state");
	size_t slash = p.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

// <base>.state = slot 0 (BASE has NO suffix); <base>_N.state = slot N. -1 = not a state file.
static int slotFromStateName(const std::string& name, const std::string& base)
{
	if (name.rfind(base, 0) != 0 || name.size() < base.size() + 6)
		return -1;
	std::string rest = name.substr(base.size());
	if (rest == ".state")
		return 0;
	if (rest.size() < 8 || rest[0] != '_' || rest.compare(rest.size() - 6, 6, ".state") != 0)
		return -1;
	std::string digits = rest.substr(1, rest.size() - 7);
	if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
		return -1;
	int slot = atoi(digits.c_str());
	return (slot > 0 && slot < MAX_SAVESTATE_SLOTS) ? slot : -1;
}

// One directory read instead of one file open per slot: cheaper than the old 10-slot probe even
// at 100 slots, and it can never drift out of sync with the slot count.
std::vector<bool> scanSavestateSlots()
{
	std::vector<bool> used(MAX_SAVESTATE_SLOTS, false);
	const std::string dir = savestateDir();
	if (dir.empty())
		return used;
	const std::string base = get_file_basename(settings.content.fileName);
	std::error_code ec;
	for (const auto& f : ghc::filesystem::directory_iterator(dir, ec))
	{
		if (f.is_directory(ec))
			continue;
		int slot = slotFromStateName(f.path().filename().string(), base);
		if (slot >= 0)
			used[slot] = true;
	}
	return used;
}

std::vector<SavestateInfo> scanSavestateInfo()
{
	std::vector<SavestateInfo> info(MAX_SAVESTATE_SLOTS);
	const std::string dir = savestateDir();
	if (dir.empty())
		return info;
	const std::string base = get_file_basename(settings.content.fileName);
	std::error_code ec;
	for (const auto& f : ghc::filesystem::directory_iterator(dir, ec))
	{
		if (f.is_directory(ec))
			continue;
		const std::string fname = f.path().filename().string();
		if (fname.size() > 4 && fname.compare(fname.size() - 4, 4, ".png") == 0)
		{	// <state>.png thumbnail: its size / mtime ride along so the States window never stats a card per frame (review)
			const int ps = slotFromStateName(fname.substr(0, fname.size() - 4), base);
			if (ps >= 0)
			{
				std::error_code pec;
				info[ps].hasPng = true;
				info[ps].pngSize = (u64)f.file_size(pec);
				auto pt = f.last_write_time(pec);
				if (!pec)
					info[ps].pngMtime = (s64)decltype(pt)::clock::to_time_t(pt);
			}
			continue;
		}
		int slot = slotFromStateName(fname, base);
		if (slot < 0)
			continue;
		SavestateInfo& si = info[slot];
		si.exists = true;
		std::error_code sec;
		si.size = (u64)ghc::filesystem::file_size(f.path(), sec);
		auto ft = ghc::filesystem::last_write_time(f.path(), sec);
		if (!sec)
			si.mtime = (s64)decltype(ft)::clock::to_time_t(ft);
	}
	// Sidecars only for slots that actually exist - at most 100 four-byte reads.
	for (int i = 0; i < MAX_SAVESTATE_SLOTS; i++)
	{
		if (!info[i].exists)
			continue;
		const std::string path = getSavestatePath(i, false);
		std::ifstream fr(path + ".frame", std::ios::binary);
		u32 fn = 0;
		if (fr.good() && fr.read((char *)&fn, sizeof(fn)))
		{
			info[i].movieFrame = fn;
			u32 seq = 0;		// sidecar v2 second field; absent in old 4-byte files
			if (fr.read((char *)&seq, sizeof(seq)))
			{
				info[i].rerecordSeq = seq;
				info[i].haveSeq = true;
				u32 mlen = 0;		// v2 third field, not needed here
				if (fr.read((char *)&mlen, sizeof(mlen)))
				{
					u64 ph = 0;		// sidecar v3: prefix hash (content revalidation)
					if (fr.read((char *)&ph, sizeof(ph)))
						info[i].prefixHash = ph;
				}
			}
		}
		info[i].label = loadSavestateLabel(i);
	}
	return info;
}

std::string loadSavestateLabel(int index)
{
	std::ifstream f(getSavestatePath(index, false) + ".label", std::ios::binary);
	if (!f.good())
		return std::string();
	std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	// One line, bounded: it renders on a thumbnail card, not in a text editor.
	s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
	size_t nl = s.find('\n');
	if (nl != std::string::npos)
		s.resize(nl);
	if (s.size() > 64)
		s.resize(64);
	return s;
}

void saveSavestateLabel(int index, const std::string& label)
{
	const std::string path = getSavestatePath(index, true) + ".label";
	if (label.empty())
	{
		std::error_code ec;
		ghc::filesystem::remove(path, ec);		// no label = no sidecar, not an empty file
		return;
	}
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (f.good())
		f << label.substr(0, 64);
}

std::string getSavestatePath(int index, bool writable)
{
	std::string state_file = get_file_basename(settings.content.fileName);

	char index_str[8] = "";
	if (index > 0) // When index is 0, use same name before multiple states is added
		sprintf(index_str, "_%d", clampSavestateSlot(index));

	state_file = state_file + index_str + ".state";
	if (index == -1)
		state_file += ".net";
	// TAS: keep a clip's savestates next to its .flyr while a movie folder is active.
	if (!savestateFolderOverride.empty())
		return savestateFolderOverride + "/" + state_file;
	if (writable)
		return get_writable_data_path(state_file);
	else
		return get_readonly_data_path(state_file);
}

void wipeScratchSavestates()
{
	if (settings.content.fileName.empty())
		return;
	std::string base = get_file_basename(settings.content.fileName);
	int removed = 0;
	for (int i = 0; i < MAX_SAVESTATE_SLOTS; i++)
	{
		char idx[8] = "";
		if (i > 0)
			snprintf(idx, sizeof(idx), "_%d", i);
		std::string state = get_writable_data_path(base + idx + ".state");
		if (std::remove(state.c_str()) == 0)
			removed++;
		std::remove((state + ".frame").c_str());
		std::remove((state + ".png").c_str());		// thumbnail sidecar (when present)
		std::remove((state + ".label").c_str());	// slot label sidecar (when present)
		std::remove((state + ".wave").c_str());	// TAS waveform snapshot sidecar (when present)
	}
	if (removed > 0)
		NOTICE_LOG(SAVESTATE, "TAS: wiped %d scratch savestate(s) from the shared data folder", removed);
}

// TAS: erase ONE savestate slot from disk - the .state file plus its .frame/.png/.label sidecars.
// Models gui_purge_stale_tick's per-slot removal (rend/gui.cpp) and wipeScratchSavestates' sidecar
// set, for a single index. Uses the WRITABLE path so it removes the real file whether the slot lives
// in a clip's folder (savestateFolderOverride active) or the shared data path. Returns true if a
// .state file was removed. The UI-side follow-up (bump dojo.savestate_epoch to rescan, clear any
// lock on the slot, toast) is the caller's job - oslib stays free of dojo/gui deps.
bool deleteSavestate(int index)
{
	const std::string base = getSavestatePath(index, true);
	std::error_code ec;
	const bool removed = ghc::filesystem::remove(base, ec) && !ec;
	ghc::filesystem::remove(base + ".frame", ec);	// movie-frame sidecar
	ghc::filesystem::remove(base + ".png", ec);		// thumbnail sidecar
	ghc::filesystem::remove(base + ".label", ec);	// slot-label sidecar
	ghc::filesystem::remove(base + ".wave", ec);	// waveform snapshot sidecar
	if (removed)
		NOTICE_LOG(SAVESTATE, "TAS: deleted savestate slot %d (%s + sidecars)", index, base.c_str());
	return removed;
}

std::string getShaderCachePath(const std::string& filename)
{
	return get_writable_data_path(filename);
}

std::string getTextureLoadPath(const std::string& gameId)
{
	if (gameId.length() > 0)
		return get_readonly_data_path("textures/" + gameId) + "/";
	else
		return "";
}

std::string getTextureDumpPath()
{
	return get_writable_data_path("texdump/");
}

}

#ifdef USE_BREAKPAD

#include "rend/boxart/http_client.h"
#include "version.h"
#include "log/InMemoryListener.h"
#include "wsi/context.h"

#define FLYCAST_CRASH_LIST "flycast-crashes.txt"

void registerCrash(const char *directory, const char *path)
{
	char list[256];
	// Register .dmp in crash list
	snprintf(list, sizeof(list), "%s/%s", directory, FLYCAST_CRASH_LIST);
	FILE *f = nowide::fopen(list, "at");
	if (f != nullptr)
	{
		fprintf(f, "%s\n", path);
		fclose(f);
	}
	// Save last log lines
	InMemoryListener *listener = InMemoryListener::getInstance();
	if (listener != nullptr)
	{
		strncpy(list, path, sizeof(list) - 1);
		list[sizeof(list) - 1] = '\0';
		char *p = strrchr(list, '.');
		if (p != nullptr && (p - list) < (int)sizeof(list) - 4)
		{
			strcpy(p + 1, "log");
			FILE *f = nowide::fopen(list, "wt");
			if (f != nullptr)
			{
				std::vector<std::string> log = listener->getLog();
				for (const auto& line : log)
					fprintf(f, "%s", line.c_str());
				fprintf(f, "Version: %s\n", GIT_VERSION);
				fprintf(f, "Renderer: %d\n", (int)config::RendererType.get());
				GraphicsContext *gctx = GraphicsContext::Instance();
				if (gctx != nullptr)
					fprintf(f, "GPU: %s %s\n", gctx->getDriverName().c_str(), gctx->getDriverVersion().c_str());
				fprintf(f, "Game: %s\n", settings.content.gameId.c_str());
				fclose(f);
			}
		}
	}
}

void uploadCrashes(const std::string& directory)
{
	FILE *f = nowide::fopen((directory + "/" FLYCAST_CRASH_LIST).c_str(), "rt");
	if (f == nullptr)
		return;
	http::init();
	char line[256];
	bool uploadFailure = false;
	while (fgets(line, sizeof(line), f) != nullptr)
	{
		char *p = line + strlen(line) - 1;
		if (*p == '\n')
			*p = '\0';
		if (file_exists(line))
		{
			std::string dmpfile(line);
			std::string logfile = get_file_basename(dmpfile) + ".log";
#ifdef SENTRY_UPLOAD
			if (config::UploadCrashLogs)
			{
				NOTICE_LOG(COMMON, "Uploading minidump %s", line);
				std::string version = std::string(GIT_VERSION);
				if (file_exists(logfile))
				{
					nowide::ifstream ifs(logfile);
					if (ifs.is_open())
					{
						std::string line;
						while (std::getline(ifs, line))
							if (line.substr(0, 9) == "Version: ")
							{
								version = line.substr(9);
								break;
							}
					}
				}
				std::vector<http::PostField> fields;
				fields.emplace_back("upload_file_minidump", dmpfile, "application/octet-stream");
				fields.emplace_back("sentry[release]", version);
				if (file_exists(logfile))
					fields.emplace_back("flycast_log", logfile, "text/plain");
				// TODO config, gpu/driver, ...
				int rc = http::post(SENTRY_UPLOAD, fields);
				if (rc >= 200 && rc < 300) {
					nowide::remove(dmpfile.c_str());
					nowide::remove(logfile.c_str());
				}
				else
				{
					WARN_LOG(COMMON, "Upload failed: HTTP error %d", rc);
					uploadFailure = true;
				}
			}
			else
#endif
			{
				nowide::remove(dmpfile.c_str());
				nowide::remove(logfile.c_str());
			}
		}
	}
	http::term();
	fclose(f);
	if (!uploadFailure)
		nowide::remove((directory + "/" FLYCAST_CRASH_LIST).c_str());
}

#else

void registerCrash(const char *directory, const char *path) {}
void uploadCrashes(const std::string& directory) {}

#endif
