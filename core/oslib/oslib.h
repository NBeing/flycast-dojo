#pragma once
#include <vector>
#include "types.h"
#if defined(__SWITCH__)
#include <malloc.h>
#endif

void os_SetWindowText(const char* text);
double os_GetSeconds();

void os_DoEvents();
void os_CreateWindow();
void os_SetupInput();
void os_TermInput();
void os_InstallFaultHandler();
void os_UninstallFaultHandler();
void os_RunInstance(int argc, const char *argv[]);

#ifdef _MSC_VER
#include <intrin.h>
#endif

u32 static inline bitscanrev(u32 v)
{
#ifdef __GNUC__
	return 31-__builtin_clz(v);
#else
	unsigned long rv;
	_BitScanReverse(&rv,v);
	return rv;
#endif
}

namespace hostfs
{
	std::string getVmuPath(const std::string& port);

	std::string getArcadeFlashPath();

	std::string findFlash(const std::string& prefix, const std::string& names);
	std::string getFlashSavePath(const std::string& prefix, const std::string& name);
	std::string findNaomiBios(const std::string& name);

	std::string getSavestatePath(int index, bool writable);

	// TAS: savestate slots 0..99. Slot 0 is BASE (unsuffixed filename, the combo-start bookmark);
	// 1..99 are checkpoints. Fixed, not configurable: config::SavestateSlot persists in emu.cfg
	// and survives across games, so a shrinking maximum would silently clamp to a DIFFERENT slot
	// than the HUD shows - checkpoint loss. Empty slots cost nothing, so there is nothing to tune.
	constexpr int MAX_SAVESTATE_SLOTS = 100;
	int clampSavestateSlot(int slot);	// -> [0, MAX_SAVESTATE_SLOTS-1]
	// How many slots the F2 hotkey walks before wrapping (dojo:SlotCycleCount, 2..MAX). This is a
	// CYCLE limit, not a storage limit: all MAX slots stay addressable from the F4 States window and
	// nothing above the limit is hidden or deleted. Shrinking the storage ceiling instead would
	// silently clamp config::SavestateSlot - which persists across games - onto a DIFFERENT slot
	// than the one a state was saved in, i.e. checkpoint loss.
	int savestateCycleCount();
	int currentSavestateSlot();			// clamped read of config::SavestateSlot (Option has no
										// validation hook, and cfg/CLI/Lua all write it raw)

	// TAS: which slots currently hold a state, by scanning the active folder once (cheaper and
	// drift-proof compared to opening every possible slot file).
	std::vector<bool> scanSavestateSlots();

	// TAS: everything the state browser shows about a slot. Gathered in ONE directory read plus a
	// couple of tiny sidecar reads per OCCUPIED slot, so refreshing it a few times a second while
	// the browser is open costs nothing measurable.
	struct SavestateInfo
	{
		bool exists = false;
		u64 size = 0;			// bytes of the .state file
		s64 mtime = 0;			// time_t the state was written
		u32 movieFrame = 0;		// .state.frame sidecar (0 when absent)
		u32 rerecordSeq = 0;	// sidecar v2: re-record count when saved (0 = old sidecar, unknown)
		bool haveSeq = false;
		u64 prefixHash = 0;		// sidecar v3: hash of movie bytes below the anchor (0 = absent)
		std::string label;		// .state.label sidecar - the user's name for this checkpoint
		bool hasPng = false;	// <state>.png thumbnail (the States window's cards) - its stats gathered HERE, not per frame (review)
		s64 pngMtime = 0;
		u64 pngSize = 0;
	};
	std::vector<SavestateInfo> scanSavestateInfo();

	// The label is a sidecar (<state>.label) rather than a clip.json field on purpose: it travels
	// with F8 backups and clip renames for free, works when there is no clip folder at all, and
	// needs no read-modify-write of a shared file. Empty label = the sidecar is removed.
	std::string loadSavestateLabel(int index);
	void saveSavestateLabel(int index, const std::string& label);

	// TAS: when non-empty, savestates are read/written in this folder (a movie clip's own
	// directory) instead of the shared data path, so a recording's states sit next to its .flyr.
	extern std::string savestateFolderOverride;

	// TAS: "Just Play" savestates in the shared data folder are per-session scratch. Delete them
	// (every slot + .frame/.png sidecars; .state.net untouched) so they never masquerade as a
	// clip's states. Called on game unload/exit and when a record or replay session begins.
	void wipeScratchSavestates();

	// TAS: erase ONE savestate slot from disk - the .state file plus its .frame/.png/.label
	// sidecars (a single-slot cousin of wipeScratchSavestates). Targets the WRITABLE path, so it
	// removes the real file whether the slot lives in a clip's folder (savestateFolderOverride) or
	// the shared data path. Returns true if a .state was removed. The caller owns the UI side
	// effects: bump dojo.savestate_epoch to force a rescan, and clear any lock on the slot.
	bool deleteSavestate(int index);

	std::string getTextureLoadPath(const std::string& gameId);
	std::string getTextureDumpPath();

	std::string getShaderCachePath(const std::string& filename);
}

static inline void *allocAligned(size_t alignment, size_t size)
{
#ifdef _WIN32
	return _aligned_malloc(size, alignment);
#elif defined(__SWITCH__)
   return memalign(alignment, size);
#else
	void *data;
	if (posix_memalign(&data, alignment, size) != 0)
		return nullptr;
	else
		return data;
#endif
}

static inline void freeAligned(void *p)
{
#ifdef _WIN32
	_aligned_free(p);
#else
	free(p);
#endif
}

void registerCrash(const char *directory, const char *path);
void uploadCrashes(const std::string& directory);
