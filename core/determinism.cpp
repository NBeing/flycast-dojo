/*
	Determinism: the one predicate, and the sync manifest. See determinism.h
	for the reasoning and SYNC_SETTINGS.md for the classification argument.
*/
#include "determinism.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/Log.h"
#include "emulator.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace determinism
{

// ---------------------------------------------------------------- predicate

bool isDeterministicRun()
{
	// settings.network.online is NOT redundant with GGPOEnable: a dojo session
	// can be online without GGPO. Any online session is by definition one that
	// must reproduce on two machines at once.
	//
	// config::Transmitting and config::Receiving are DELIBERATELY ABSENT, and
	// an earlier version of this function had them, which was a bug.
	//
	//   Option<bool> Transmitting("Transmitting", true, "dojo");   <- TRUE
	//
	// Transmitting defaults to true on every flycast-dojo base. It means "will
	// upload replays if a session happens", not "a session is happening", so
	// including it made this predicate return true in every plain single-player
	// launch - which silently pinned the SH4 clock, forced EmulateFramebuffer
	// off and turned on the savestate verify probe for everyone, always.
	//
	// Upstream's own RTC condition in aica_if.cpp still has both flags and so is
	// effectively unconditional. That is survivable for the RTC (a frozen clock
	// is merely wrong, not slow) and is NOT survivable for a predicate that
	// removes a user-facing feature, which is why this one does not copy it.
	//
	// Caught by a negative test: overclock to 250 with recording off and watch
	// whether the pin fires. It did.
	return settings.network.online
		|| config::GGPOEnable
		|| config::RecordMatches
		|| config::Replay;
}

const char *runKind()
{
	if (config::GGPOEnable || settings.network.online) return "netplay";
	if (config::Replay)			return "replay";
	if (config::RecordMatches)	return "record";
	return "off";
}

// ----------------------------------------------------- the classification
//
// Tiers follow SYNC_SETTINGS.md. Only CERTAIN and HIGH are enabled; the
// "needs measurement" set is listed at the bottom, deliberately inert, so the
// argument for each is visible in the same place as the decision.
//
// PRESENT ON THIS BASE (it was not on the older one): "config.Sh4Clock", the
// 100-300 MHz overclock slider flyinghead added in 48acb03b8. The older
// video-recording base had no such option at all - SH4_MAIN_CLOCK is a
// compile-time #define there, so the clock was pinned by construction and the
// pin was a no-op. Here the slider is real, upstream's guard (f8d5517b8)
// covers GGPO only, and emulator.cpp widens it to the predicate.

static const std::vector<std::string>& classifiedKeys()
{
	static const std::vector<std::string> keys = {
		// -- CERTAIN: guest-visible machine configuration --------------------
		"config.Dreamcast.Broadcast",		// NTSC/PAL: 60Hz vs 50Hz. The whole timeline.
		"config.Dreamcast.Region",			// games branch on it; BIOS differs
		"config.Dreamcast.Language",		// games read it
		"config.Dreamcast.Cable",			// VGA vs composite selects a video mode
		"config.UseReios",					// HLE BIOS vs real BIOS - different boot code
		"config.Dynarec.Enabled",			// interpreter and JIT are not cycle-identical
		"config.ForceFreePlay",				// patches NAOMI settings
		"config.aica.DSPEnabled",			// AICA DSP state is serialized
		"config.Dreamcast.RamMod32MB",		// 32 MB RAM mod - changes the MEMORY MAP.
											// Not present on the older base at all.
		"network.BattleCable",				// adds hardware to the machine
		"network.MultiboardSlaves",			// Naomi multiboard: more machines
		"config.PerGameVmu",				// selects which VMU/flash the machine boots
											// with, i.e. its initial conditions

		// NOT CLASSIFIED HERE, and they were on the older video-recording base:
		//   Dreamcast.FullMMU, Dreamcast.ForceWindowsCE, Dynarec.idleskip
		//     - removed upstream between the two bases
		//   SOCDResolution, input.EnableDiagonalCorrection, dojo.ForceRealBios
		//     - blueminder master-line additions that dojo-7 never had
		// The startup audit found all six as "stale" on the first run against
		// this base, which is exactly what that check exists for.

		// -- CERTAIN: upstream already guards these for GGPO -----------------
		// If flyinghead disabled it for rollback, it is nondeterministic, and
		// local record/replay is the same problem under another name.
		"config.rend.ThreadedRendering",	// guarded at emulator.cpp:951
		"config.rend.EmulateFramebuffer",	// 51758b965 "ggpo: disable full framebuffer emulation"
		"config.Sh4Clock",					// 48acb03b8 overclock slider; f8d5517b8 pinned it
											// for GGPO only. Rescales every recompiled block's
											// guest_cycles (decoder.cpp), so record at 200 and
											// replay at 250 and the guest gets a different
											// cycle budget per frame.

		// -- CERTAIN: input transformed before the guest sees it -------------
		// These change the button state actually delivered, so a movie recorded
		// under one setting is a different movie under another.
		"input.MouseSensitivity",			// scales mouse deltas

		// -- HIGH CONFIDENCE -------------------------------------------------
		"config.pvr.AutoSkipFrame",			// skips on MEASURED performance: a wall clock
		"config.ta.skip",					// skips TA processing
		"config.rend.RenderToTextureBuffer",// RTT results land in guest-visible VRAM
		"config.rend.WidescreenGameHacks",	// patches guest memory for some titles
		"config.FastGDRomLoad",				// changes GD-ROM timing; games race it
		"config.Debug.SerialConsoleEnabled",// touches SCIF - a SCIF timer reschedule was
											// one of the two real desyncs David found
		"network.EmulateBBA",				// adds a device to the machine
	};
	return keys;
}

// Deliberately NOT classified yet - each is plausibly host-side pacing and
// plausibly not, and the honest way to settle it is measurement rather than
// argument: record a clip, replay with the option flipped, compare state
// hashes. That test needs the savestate anchor, so these wait for it.
static const std::vector<std::string>& needsMeasurement()
{
	static const std::vector<std::string> keys = {
		"config.rend.DelayFrameSwapping",
		"config.rend.FixedFrequency",
		"config.rend.FixedFrequencyThreadSleep",
		"config.pvr.MaxThreads",
		"config.aica.LimitFPS",
		"config.rend.DupeFrames",
	};
	return keys;
}

const std::vector<std::string>& syncCriticalKeys() { return classifiedKeys(); }

bool isSyncCritical(const std::string& key)
{
	const auto& k = classifiedKeys();
	return std::find(k.begin(), k.end(), key) != k.end();
}

// ------------------------------------------------------------- key splitting

static bool splitKey(const std::string& key, std::string& section, std::string& name)
{
	// Section is the first dot-separated token; everything after it is the
	// option name, which itself routinely contains dots ("Dreamcast.Broadcast").
	const size_t dot = key.find('.');
	if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size())
		return false;
	section = key.substr(0, dot);
	name = key.substr(dot + 1);
	return true;
}

// ----------------------------------------------------------------- manifest

std::vector<SyncEntry> captureManifest()
{
	std::vector<SyncEntry> out;
	for (const std::string& key : classifiedKeys())
	{
		std::string section, name;
		if (!splitKey(key, section, name))
		{
			WARN_LOG(COMMON, "determinism: malformed sync key '%s'", key.c_str());
			continue;
		}
		// Read through cfg rather than the Option object: cfg is where the
		// per-game overlay and any -config virtual entry have already landed,
		// so this records the value the run will actually use.
		out.push_back({ key, cfgLoadStr(section, name, std::string()) });
	}
	return out;
}

ApplyStatus applyManifest(const std::vector<SyncEntry>& manifest, std::string& problem)
{
	problem.clear();

	// 1. Every key the movie names must be one we understand. A key we do not
	//    know means the movie came from a newer build that classified something
	//    this one does not - playing anyway desyncs with no explanation attached.
	for (const SyncEntry& e : manifest)
	{
		if (!isSyncCritical(e.key))
		{
			problem = e.key;
			return ApplyStatus::UnknownKey;
		}
	}

	// 2. Every key we consider sync-critical must be present. An older movie
	//    predates a classification, so its value is unknown rather than default.
	std::map<std::string, std::string> byKey;
	for (const SyncEntry& e : manifest)
		byKey[e.key] = e.value;
	for (const std::string& key : classifiedKeys())
	{
		if (byKey.find(key) == byKey.end())
		{
			problem = key;
			return ApplyStatus::MissingKey;
		}
	}

	// 3. Force them. cfgSetVirtual wins over both the saved cfg and any
	//    per-game section, and is what -config launch flags already use; the
	//    caller reloads Settings so the Option objects pick the values up.
	for (const SyncEntry& e : manifest)
	{
		std::string section, name;
		if (splitKey(e.key, section, name))
			cfgSetVirtual(section, name, e.value);
	}
	NOTICE_LOG(COMMON, "determinism: applied %d sync settings from the movie",
			(int)manifest.size());
	return ApplyStatus::Ok;
}

std::string serializeManifest(const std::vector<SyncEntry>& manifest)
{
	std::vector<SyncEntry> sorted = manifest;
	std::sort(sorted.begin(), sorted.end(),
			[](const SyncEntry& a, const SyncEntry& b) { return a.key < b.key; });

	std::string out;
	for (const SyncEntry& e : sorted)
	{
		out += e.key;
		out += '=';
		out += e.value;
		out += '\n';
	}
	return out;
}

bool parseManifest(const std::string& text, std::vector<SyncEntry>& out, std::string& error)
{
	out.clear();
	error.clear();

	std::istringstream in(text);
	std::string line;
	int lineNo = 0;
	while (std::getline(in, line))
	{
		lineNo++;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;

		const size_t eq = line.find('=');
		if (eq == std::string::npos || eq == 0)
		{
			// Strict, like the text movie codec: a malformed line names its
			// number rather than being silently skipped into a wrong result.
			error = "line " + std::to_string(lineNo) + ": expected key=value";
			return false;
		}
		out.push_back({ line.substr(0, eq), line.substr(eq + 1) });
	}
	return true;
}

// -------------------------------------------------------------- audit

std::vector<std::string> unclassifiedOptions()
{
	std::vector<std::string> out;
	for (const config::BaseOption *o : config::Settings::instance().allOptions())
	{
		const std::string key = o->optionKey();
		if (key.empty())
			continue;
		if (isSyncCritical(key))
			continue;
		const auto& m = needsMeasurement();
		if (std::find(m.begin(), m.end(), key) != m.end())
			continue;
		out.push_back(key);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<std::string> staleClassifications()
{
	std::vector<std::string> live;
	for (const config::BaseOption *o : config::Settings::instance().allOptions())
	{
		const std::string key = o->optionKey();
		if (!key.empty())
			live.push_back(key);
	}
	std::sort(live.begin(), live.end());

	std::vector<std::string> out;
	for (const std::string& key : classifiedKeys())
		if (!std::binary_search(live.begin(), live.end(), key))
			out.push_back(key);
	return out;
}

void auditClassification()
{
	const std::vector<std::string> stale = staleClassifications();
	for (const std::string& key : stale)
		WARN_LOG(COMMON, "determinism: classified key '%s' matches no registered option "
				"- renamed or removed?", key.c_str());

	const std::vector<std::string> unclassified = unclassifiedOptions();
	NOTICE_LOG(COMMON, "determinism: %d sync-critical, %d unclassified, %d stale",
			(int)classifiedKeys().size(), (int)unclassified.size(), (int)stale.size());
}

}
