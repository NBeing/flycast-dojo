/*
	Determinism: the one predicate, and the sync manifest.

	WHY THIS EXISTS

	A movie is a recipe, not a video. It stores "press Punch on frame 107" and
	replays by re-running the game. That only reproduces the original if the
	machine, and everything told to the machine, is identical both times.

	Three things break it:

	  1. INITIAL CONDITIONS - BIOS, flash, VMU, the RTC at boot. Pinned by
	     starting from a savestate anchor, not by anything here.
	  2. HOST LEAKING IN     - the RTC read mid-run, an overclock slider, FMA
	     rounding, uninitialised memory.
	  3. CONFIGURATION       - cheats, threaded rendering, region, SOCD
	     resolution. Identical inputs, different settings, different game.

	This module owns (2) and (3).

	THE PREDICATE

	Upstream already solved much of this for rollback netplay: there are ~20
	`if (config::GGPOEnable)` guards across the emulation core, each one a
	decision that some subsystem is not deterministic. Local recording and
	replay is the same problem under a different name, so every one of those
	guards is a candidate for this predicate instead.

	Enumerating the flags at each call site (GGPOEnable || RecordMatches || ...)
	is how it is usually done and it rots: the list is copied, then one copy
	gains a case and the others do not. One predicate, used everywhere, means
	the audit is a grep.

	THE MANIFEST

	BizHawk's SyncSettings idea. Cores there split every option into Settings
	(cosmetic, change freely) and SyncSettings (affects emulation, stored INSIDE
	the movie, changing them invalidates it). That single distinction is what
	makes their movies portable across machines and across years.

	Note the consequence for cheats: a cheat is a memory write at a defined
	point, perfectly deterministic and perfectly recordable. Upstream disables
	cheats under netplay because syncing cheat state between two peers is more
	work than forbidding it - a netplay convenience, not a law. The fix is not
	to ban cheats, it is to record that they were on.

	Values are read and written through the cfg layer rather than through the
	Option objects, because cfgSetVirtual() + Settings::load() is the mechanism
	`-config` launch flags already use, and Option::override() is what upstream's
	own GGPO pins already use. Nothing new had to be invented to apply a
	manifest.

	See SYNC_SETTINGS.md and DETERMINISM.md.
*/
#pragma once
#include <string>
#include <vector>

namespace determinism
{

// ---- the predicate ------------------------------------------------------

// True when this run's result must be byte-reproducible: rollback netplay
// (upstream's original case) or local recording / replay / spectating.
//
// Every `if (config::GGPOEnable)` in the emulation core that exists for
// determinism reasons - as opposed to netplay plumbing like ROM digests -
// should be asking this instead. See DETERMINISM.md for the audit.
bool isDeterministicRun();

// Which case fired, for logs and for the HUD. Never nullptr.
//   "off" | "netplay" | "record" | "replay" | "spectate"
const char *runKind();

// ---- state fingerprint --------------------------------------------------
//
// NOT REIMPLEMENTED HERE. The primitive already exists as the Lua binding
// `flycast.emulator.hashState()` (core/lua/lua.cpp), which emuapi's adapter
// already exposes as `savestate.hash`. An earlier draft of this file added a
// second MD5-based one before noticing; the note survives so nobody adds a
// third.
//
// That it lives in Lua is the better outcome: the anchor assertion built on it
// (scripts/determinism_anchor.lua) is then an emuapi conformance test that runs
// against ANY conforming host, rather than flycast-specific C++.
//
// Caveat worth knowing: hashState uses XXH32, so it is a 32-bit fingerprint.
// Fine for detecting divergence, not a cryptographic identity.

// ---- the sync manifest --------------------------------------------------

struct SyncEntry
{
	std::string key;	// "section.name", e.g. "config.Dreamcast.Broadcast"
	std::string value;	// the value as text, exactly as cfg stores it
};

// Is this option key one whose value changes what the game does?
bool isSyncCritical(const std::string& key);

// Every sync-critical key this build knows about, in a stable order.
const std::vector<std::string>& syncCriticalKeys();

// Read the current value of every sync-critical option.
//
// MUST be called AFTER per-game config has been applied. Settings::load(true)
// overlays a per-game section that can silently change any of these, so a
// manifest captured before it records a value the run will not actually use.
std::vector<SyncEntry> captureManifest();

enum class ApplyStatus
{
	Ok,				// every key matched a known, applied option
	UnknownKey,		// the movie names something this build does not know
	MissingKey,		// this build considers a key sync-critical; movie lacks it
};

// Force the recorded values, via cfgSetVirtual + Option::override, so they win
// over both the saved cfg and any per-game section.
//
// FAILURE POLICY: an unknown or missing key is a REFUSAL, not a warning.
// A determinism failure surfaces thousands of frames later, in front of whoever
// is watching, not in front of the author. `problem` names the offending key.
ApplyStatus applyManifest(const std::vector<SyncEntry>& manifest, std::string& problem);

// Text form, for embedding in a movie header or a clip.json `sync` block.
// One "key=value" per line, sorted, so it diffs usefully.
std::string serializeManifest(const std::vector<SyncEntry>& manifest);
bool parseManifest(const std::string& text, std::vector<SyncEntry>& out, std::string& error);

// ---- anti-drift audit ---------------------------------------------------

// Registered options that appear in NO classification tier. A newly added
// option lands here rather than silently defaulting to "not sync-critical",
// which is the failure mode a bool-on-the-option design has.
//
// Empty is the goal. Logged once at startup.
std::vector<std::string> unclassifiedOptions();

// Classified keys that no longer resolve to a registered option: an option was
// renamed or removed and the table was not updated. Also logged at startup.
std::vector<std::string> staleClassifications();

// Log both of the above at NOTICE. Called once from flycast_init.
void auditClassification();

}
