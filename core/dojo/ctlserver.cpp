#include "ctlserver.h"
#include "cfg/cfg.h"           // cfgLoadBool / cfgLoadStr (live reads)
#include "cfg/option.h"        // config::SavestateSlot
#include "types.h"             // settings (savestateAllowed replica)
#include "rend/gui.h"          // gui_saveState / gui_loadState / gui_step_frames / gui_state / GuiState
#include "oslib/oslib.h"       // hostfs::savestateFolderOverride / clampSavestateSlot / currentSavestateSlot
#include "dojo/dojo.h"         // dojo.frame_number / play_match / macro_armed
#include "dojo/mvc2.h"         // tas_mvc2::readRam
#include "dojo/tas_clip.h"     // tas_clip::setLocked / bump - the Test Lab "Overwrite + lock" persist path
#include "log/Log.h"           // NOTICE_LOG
#include "deps/filesystem.hpp" // ghc::filesystem::create_directories (lazily make _ctl/)
#include "json.hpp"
#include <fstream>
#include <string>
#include <system_error>
#include <climits>
#include <cctype>              // toupper (case-insensitive set_mode token)

using json = nlohmann::json;

namespace tas_ctl
{
// s_lastHandledSeq sits at this sentinel until the first cmd.json sighting for a ctlDir baselines it to that
// file's current seq (WITHOUT executing) - so a leftover command from a previous session is skipped, and a
// new clip's freshly-numbered (low) seqs are never pre-ignored. Reset to the sentinel on every dir change.
static const int    kSeqUnset       = INT_MIN;
static const double kStepWatchdogSec = 5.0;   // a step that never lands (movie end / stopped guest) fails after this

static std::string s_ctlDir;           // the ctl base dir this state was primed from
static bool        s_ensured = false;   // _ctl/ created for s_ctlDir
static int         s_lastHandledSeq = kSeqUnset;

// ONE outstanding command at a time. A step lands ASYNC (spans frames), so its response is deferred.
static bool   s_pending = false;
static int    s_pendingSeq = 0;
static u32    s_pendingTarget = 0;
static double s_pendingIssuedAt = 0.0;  // os_GetSeconds() at dispatch (watchdog clock; dojo.frame_number is frozen while stopped)
static bool   s_pendingIsAdv = false;   // the in-flight single-step belongs to an advance_until (not a plain `step`)

// advance_until: single-step to the FIRST frame a watched RAM value satisfies a predicate (or give up at max
// frames), then pause + optionally checkpoint. Driven ONE frame at a time through the s_pending step machine
// above (s_pendingIsAdv marks an in-flight step as ours), re-checking the predicate on every landed frame so it
// stops on the FIRST true frame with no overshoot. s_advMax is a FRAME COUNT from s_advStartFrame, not absolute.
static bool s_advancing     = false;
static int  s_advSeq        = 0;
static u32  s_advAddr       = 0;
static int  s_advWidth      = 1;
static int  s_advOp         = 0;
static u32  s_advVal        = 0;
static u32  s_advStartVal   = 0;   // value at the start of the advance (for changed/inc/dec)
static u32  s_advStartFrame = 0;   // frame at the start of the advance (frames_advanced baseline)
static int  s_advMax        = 0;   // max frames to advance before giving up (fired:false)
static int  s_advSaveSlot   = -1;  // checkpoint slot to save on fire, or -1 for none

// The 3-way input mode label (mirrors tasDriver in dojo_gui.cpp): READ = playback, READWRITE = live-signal
// overwrite, WRITE = advance-overwrites. Read-only view of dojo state - safe every frame on this thread.
static const char* modeStr() { return dojo.play_match ? "READ" : (dojo.macro_armed ? "READWRITE" : "WRITE"); }

// File-static in gui.cpp (:1114) so it cannot be called across the TU - replicated verbatim here.
static bool savestateAllowed()
{
	return !settings.content.path.empty() && !settings.network.online && !settings.naomi.multiboard;
}

// Lenient integer arg: the default when the key is missing OR present with a non-integer type, so a
// malformed value can never throw nlohmann's type_error out of tick() (load's slot is checked explicitly).
static int intArg(const json& args, const char* key, int def)
{
	return (args.contains(key) && args[key].is_number_integer()) ? args[key].get<int>() : def;
}

// A JSON addr is either a string ("2C2685A0" or "0x8C2685A0", always hex) or an integer -> u32.
// readRam converts Demul 2C..-> flycast 8C.. itself, so either address space is fine here.
static bool parseAddr(const json& a, u32& out)
{
	if (a.is_number_integer() || a.is_number_unsigned()) { out = (u32)a.get<int64_t>(); return true; }
	if (a.is_string())
	{
		try { out = (u32)std::stoul(a.get<std::string>(), nullptr, 16); return true; } catch (...) { return false; }
	}
	return false;
}

// advance_until predicate. eq/ne/lt/le/gt/ge compare `cur` to the requested `val`; changed/inc/dec compare to
// `startVal` (the value captured when the advance began). Op codes match advOpCode() below.
static bool advPred(u32 cur, int op, u32 val, u32 startVal)
{
	switch (op)
	{
	case 0: return cur == val;          // eq
	case 1: return cur != val;          // ne
	case 2: return cur <  val;          // lt
	case 3: return cur <= val;          // le
	case 4: return cur >  val;          // gt
	case 5: return cur >= val;          // ge
	case 6: return cur != startVal;     // changed
	case 7: return cur >  startVal;     // inc
	case 8: return cur <  startVal;     // dec
	default: return false;
	}
}

// "eq"|"ne"|"lt"|"le"|"gt"|"ge"|"changed"|"inc"|"dec" (case-insensitive) -> advPred op code, else -1.
static int advOpCode(const std::string& s)
{
	std::string t = s;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "eq")      return 0;
	if (t == "ne")      return 1;
	if (t == "lt")      return 2;
	if (t == "le")      return 3;
	if (t == "gt")      return 4;
	if (t == "ge")      return 5;
	if (t == "changed") return 6;
	if (t == "inc")     return 7;
	if (t == "dec")     return 8;
	return -1;
}

// Responses are SEQ-NAMED files: <respDir>/<seq>.json - NEVER a single shared resp.json. A polling client only
// ever opens its OWN seq's file, written once via tmp+rename to a target that never pre-exists, so the client's
// read can never collide with a rename over a file it holds open (the Windows sharing-violation that silently
// dropped a response). The previous response is pruned once the next is written (the client already consumed it,
// because the protocol is synchronous: it waits for resp(N) before sending cmd(N+1)).
static std::string s_prevResp;
static void writeResp(const std::string& respDir, int seq, bool ok, const json& extra, const std::string& error)
{
	json j = json::object();
	j["seq"]   = seq;
	j["ok"]    = ok;
	j["frame"] = (u32)dojo.frame_number.load();
	j["mode"]  = modeStr();
	if (extra.is_object())
		for (auto it = extra.begin(); it != extra.end(); ++it)
			j[it.key()] = it.value();
	if (error.empty()) j["error"] = nullptr; else j["error"] = error;
	const std::string path = respDir + "/" + std::to_string(seq) + ".json";
	const std::string tmp  = path + ".tmp";
	{
		std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
		if (!o.good())
			return;
		o << j.dump(2) << std::endl;
		if (!o.good())
			return;
	}
	std::error_code ec;
	ghc::filesystem::rename(tmp, path, ec);        // fresh per-seq target: never overwrites an open file
	if (ec) { ghc::filesystem::remove(tmp, ec); return; }
	if (!s_prevResp.empty() && s_prevResp != path) // prune the previous response (already consumed by the client)
		ghc::filesystem::remove(s_prevResp, ec);
	s_prevResp = path;
}

void tick()
{
	// (1) hard no-op when disabled - zero perturbation to normal runs / headless tests.
	if (!cfgLoadBool("dojo", "ControlServer", false))
		return;

	// (2) where the channel lives: explicit dojo:CtlDir, else the loaded clip folder.
	std::string ctlDir = cfgLoadStr("dojo", "CtlDir", "");
	if (ctlDir.empty())
		ctlDir = hostfs::savestateFolderOverride;
	if (ctlDir.empty())
		return;

	const std::string base     = ctlDir + "/_ctl";
	const std::string cmdPath  = base + "/cmd.json";
	const std::string respDir  = base + "/resp";      // per-seq response files live here (<respDir>/<seq>.json)

	// dir changed (a different clip / CtlDir) -> re-baseline, the clip.json hot-reload pattern.
	if (ctlDir != s_ctlDir)
	{
		// (10) a command was still in flight when the channel moved: fail it to the OLD resp path so its client
		// unblocks instead of hanging forever on a completion that will never be written to the old file. An
		// advance_until owns s_advSeq (its steps set s_pending but reuse s_advSeq, not s_pendingSeq), so abort on
		// that; a plain step (or nothing in flight) uses s_pendingSeq. s_advancing can also be set with no step in
		// flight (dispatched, first check pending) - abort that too.
		if (!s_ctlDir.empty())
		{
			if (s_pending)
				writeResp(s_ctlDir + "/_ctl/resp", s_pendingIsAdv ? s_advSeq : s_pendingSeq, false, json::object(), "aborted: dir changed");
			else if (s_advancing)
				writeResp(s_ctlDir + "/_ctl/resp", s_advSeq, false, json::object(), "aborted: dir changed");
		}

		s_ctlDir         = ctlDir;
		s_ensured        = false;
		s_pending        = false;
		s_pendingIsAdv   = false;
		s_advancing      = false;
		s_lastHandledSeq = kSeqUnset;   // (2) a new clip's freshly-numbered (low) seqs must not be pre-ignored
	}

	// (3) lazily create <ctlDir>/_ctl/ + resp/ once. On failure, leave s_ensured false so it retries next frame.
	if (!s_ensured)
	{
		std::error_code ec;
		ghc::filesystem::remove_all(respDir, ec);           // clear any stale per-seq resp files from a prior session
		ghc::filesystem::create_directories(respDir, ec);   // (re)create base + resp/ in one shot
		if (ec)
		{
			// (9) a transient create failure must not permanently wedge the channel - retry, don't latch true.
			NOTICE_LOG(INPUT, "CTL: create_directories(%s) failed: %s", respDir.c_str(), ec.message().c_str());
			return;
		}
		s_ensured = true;
		s_prevResp.clear();
		// per-seq resp files replace the old single resp.json; publish a readiness marker the launcher polls for.
		{ std::ofstream r(base + "/ready", std::ios::binary | std::ios::trunc); r << "1"; }
	}

	// (4) in-flight step: landed, movie-ended, or wedged? A step can NEVER land once the movie ends (ReplayEnd)
	// or the guest stops and never restarts; without a terminal check + watchdog, s_pending would latch true
	// forever and swallow every later command. Timed on os_GetSeconds() - dojo.frame_number is FROZEN while the
	// machine is stopped, which is exactly the wedge case.
	if (s_pending)
	{
		if (gui_state == GuiState::ReplayEnd)
		{
			// an in-flight advance_until step can never land now: report it not-fired + why, and clear the advance.
			if (s_pendingIsAdv)
			{
				json extra = json::object();
				extra["fired"]           = false;
				extra["reason"]          = "movie ended";
				extra["frames_advanced"] = (u32)(dojo.frame_number.load() - s_advStartFrame);
				writeResp(respDir, s_advSeq, true, extra, "");
				NOTICE_LOG(INPUT, "CTL: advance_until seq=%d aborted - movie ended at frame %u", s_advSeq, (u32)dojo.frame_number.load());
				s_advancing = false;
				s_pending = false;
				s_pendingIsAdv = false;
				return;
			}
			json extra = json::object();
			extra["frame"] = (u32)dojo.frame_number.load();
			writeResp(respDir, s_pendingSeq, false, extra, "movie ended");
			NOTICE_LOG(INPUT, "CTL: step seq=%d aborted - movie ended at frame %u", s_pendingSeq, (u32)dojo.frame_number.load());
			s_pending = false;
			return;
		}
		const bool landed = (gui_state == GuiState::Paused && dojo.frame_number.load() >= s_pendingTarget);
		if (landed)
		{
			// an advance_until single-step landed: clear the step flags and FALL THROUGH (no resp, no return) to the
			// s_advancing block below, which re-checks the predicate on THIS just-landed frame.
			if (s_pendingIsAdv)
			{
				s_pending = false;
				s_pendingIsAdv = false;
			}
			else
			{
				writeResp(respDir, s_pendingSeq, true, json::object(), "");
				NOTICE_LOG(INPUT, "CTL: step seq=%d landed -> frame %u", s_pendingSeq, (u32)dojo.frame_number.load());
				s_pending = false;
				return;
			}
		}
		else if (os_GetSeconds() - s_pendingIssuedAt > kStepWatchdogSec)
		{
			if (s_pendingIsAdv)
			{
				json extra = json::object();
				extra["fired"]           = false;
				extra["reason"]          = "step did not advance";
				extra["frames_advanced"] = (u32)(dojo.frame_number.load() - s_advStartFrame);
				writeResp(respDir, s_advSeq, true, extra, "");
				NOTICE_LOG(INPUT, "CTL: advance_until seq=%d watchdog - did not advance within %.0fs", s_advSeq, kStepWatchdogSec);
				s_advancing = false;
				s_pending = false;
				s_pendingIsAdv = false;
				return;
			}
			writeResp(respDir, s_pendingSeq, false, json::object(), "step did not advance");
			NOTICE_LOG(INPUT, "CTL: step seq=%d watchdog - did not advance within %.0fs", s_pendingSeq, kStepWatchdogSec);
			s_pending = false;
			return;
		}
		else
		{
			return;     // one outstanding command at a time - not landed yet
		}
		// only reached when an advance_until single-step just landed: fall through to the s_advancing block below.
	}

	// (4b) advance_until in progress: check the watched value on the current (just-landed, or the start) frame.
	// The predicate is checked BEFORE issuing each step INCLUDING the start frame, so an already-true absolute
	// condition fires with frames_advanced==0, and every landed frame is re-checked - stopping on the FIRST true
	// frame with no overshoot. Skipped while s_pending is set, so still one outstanding command at a time.
	if (s_advancing)
	{
		u32 cur = 0;
		if (!tas_mvc2::readRamSafe(s_advAddr, s_advWidth, cur))
		{
			writeResp(respDir, s_advSeq, false, json::object(), "address not in work RAM");
			s_advancing = false;
			return;
		}

		if (advPred(cur, s_advOp, s_advVal, s_advStartVal))          // FIRED - stop on this exact frame
		{
			if (gui_state != GuiState::Paused) gui_open_pause();     // ensure paused (normally already, after a landed step)
			int saved = -1;
			if (s_advSaveSlot >= 0 && savestateAllowed())
			{
				const int prev = hostfs::currentSavestateSlot();
				config::SavestateSlot.set(hostfs::clampSavestateSlot(s_advSaveSlot));
				gui_saveState();
				config::SavestateSlot.set(prev);
				saved = s_advSaveSlot;
			}
			json extra = json::object();
			extra["fired"]           = true;
			extra["value"]           = cur;
			extra["frames_advanced"] = (u32)(dojo.frame_number.load() - s_advStartFrame);
			if (saved >= 0) extra["saved_slot"] = saved;
			writeResp(respDir, s_advSeq, true, extra, "");
			NOTICE_LOG(INPUT, "CTL: advance_until seq=%d fired at frame %u (value %u, +%u frames)", s_advSeq,
				(u32)dojo.frame_number.load(), cur, (u32)(dojo.frame_number.load() - s_advStartFrame));
			s_advancing = false;
			return;
		}
		if (dojo.frame_number.load() - s_advStartFrame >= (u32)s_advMax)   // EXHAUSTED - never met within max frames
		{
			json extra = json::object();
			extra["fired"]           = false;
			extra["value"]           = cur;
			extra["frames_advanced"] = (u32)(dojo.frame_number.load() - s_advStartFrame);
			writeResp(respDir, s_advSeq, true, extra, "");
			NOTICE_LOG(INPUT, "CTL: advance_until seq=%d exhausted at frame %u (+%u frames, never true)", s_advSeq,
				(u32)dojo.frame_number.load(), (u32)(dojo.frame_number.load() - s_advStartFrame));
			s_advancing = false;
			return;
		}
		// advance ONE more frame, then re-check on the next landing (exact single-frame granularity)
		s_pendingTarget = dojo.frame_number.load() + 1;
		gui_step_frames(1);
		s_pending = true;
		s_pendingIsAdv = true;
		s_pendingIssuedAt = os_GetSeconds();
		return;
	}

	// (5) read + parse cmd.json EVERY frame (a tiny opt-in file), deduping by seq ONLY - never by mtime: two
	// commands written inside one wall-clock second share an st_mtime (whole seconds on MinGW), so mtime-gating
	// dropped the second and hung the synchronous client forever. A cheap existence check keeps a missing file quiet.
	std::error_code ec;
	if (!ghc::filesystem::exists(cmdPath, ec))
		return;

	json j;
	{
		std::ifstream f(cmdPath);
		if (!f.good())
			return;
		try { j = json::parse(f, nullptr, true, true); }    // throw-on-error + allow comments
		catch (...) { NOTICE_LOG(INPUT, "CTL: cmd.json parse failed"); return; }
	}

	// (6) sequence: ignore stale / already-handled commands.
	if (!j.contains("seq") || !j["seq"].is_number_integer())
	{
		NOTICE_LOG(INPUT, "CTL: cmd has no integer seq - ignored");
		return;
	}
	const int seq = j["seq"].get<int>();

	// (2) first sighting of cmd.json for this dir: baseline s_lastHandledSeq on its current seq WITHOUT executing,
	// so a command left over from a previous session is skipped rather than replayed on boot.
	if (s_lastHandledSeq == kSeqUnset)
	{
		s_lastHandledSeq = seq;
		return;
	}
	if (seq <= s_lastHandledSeq)
		return;

	// (4-major) read the verb DEFENSIVELY (a present-but-not-string "verb" would throw nlohmann's type_error), and
	// wrap the WHOLE dispatch in try/catch so no malformed field can throw out of tick() and crash the emulator.
	std::string verb = (j.contains("verb") && j["verb"].is_string()) ? j["verb"].get<std::string>() : std::string();
	const json args = j.value("args", json::object());

	try
	{
		// (7) dispatch.
		if (verb == "query")
		{
			json extra = json::object();
			extra["paused"] = (gui_state == GuiState::Paused);
			writeResp(respDir, seq, true, extra, "");
		}
		else if (verb == "read")
		{
			u32 addr = 0;
			if (!args.contains("addr") || !parseAddr(args["addr"], addr))
				writeResp(respDir, seq, false, json::object(), "read requires numeric or hex addr");
			else
			{
				const int width = intArg(args, "width", 1);
				u32 v = 0;
				// (5) guarded read: a client address outside work RAM (MMIO / register) can side-effect and
				// desync a replay - readRamSafe refuses it and reads nothing.
				if (!tas_mvc2::readRamSafe(addr, width, v))
					writeResp(respDir, seq, false, json::object(), "address not in work RAM");
				else
				{
					json extra = json::object();
					extra["value"] = v;
					writeResp(respDir, seq, true, extra, "");
				}
			}
		}
		else if (verb == "save")
		{
			if (!savestateAllowed())
				writeResp(respDir, seq, false, json::object(), "savestate not allowed");
			else
			{
				// (8) don't leave the interactive user's active slot changed: save it, use the requested slot, restore.
				const int prevSlot = hostfs::currentSavestateSlot();
				const int slot = hostfs::clampSavestateSlot(intArg(args, "slot", prevSlot));
				config::SavestateSlot.set(slot);
				gui_saveState();
				config::SavestateSlot.set(prevSlot);
				json extra = json::object();
				extra["saved_slot"] = slot;
				writeResp(respDir, seq, true, extra, "");
			}
		}
		else if (verb == "load")
		{
			if (!args.contains("slot") || !args["slot"].is_number_integer())
				writeResp(respDir, seq, false, json::object(), "load requires slot");
			// (6) gui_loadState()/F3 in a recording session (!play_match) REWINDS + re-records the movie instead
			// of loading - destructive. Only load in READ (play_match); refuse otherwise.
			else if (!dojo.play_match)
				writeResp(respDir, seq, false, json::object(), "load only in READ mode (would rewind the movie)");
			else
			{
				// (8) restore the interactive user's active slot after the load.
				const int prevSlot = hostfs::currentSavestateSlot();
				const int slot = hostfs::clampSavestateSlot(args["slot"].get<int>());
				config::SavestateSlot.set(slot);
				gui_loadState();
				config::SavestateSlot.set(prevSlot);
				writeResp(respDir, seq, true, json::object(), "");
			}
		}
		else if (verb == "step")
		{
			int n = intArg(args, "n", 1);
			if (n < 1) n = 1;
			s_pendingTarget = dojo.frame_number.load() + (u32)n;
			gui_step_frames(n);
			s_pending = true;
			s_pendingSeq = seq;
			s_pendingIssuedAt = os_GetSeconds();    // (3) watchdog clock: stamp the dispatch time
			// resp is written on landing / abort / timeout in step (4) - NOT here.
		}
		else if (verb == "advance_until")
		{
			// single-step until a watched RAM value satisfies op vs val (or startVal, for changed/inc/dec),
			// stopping on the FIRST true frame, or give up after `max` frames. Arms the s_advancing state
			// machine; the resp is written by the (4b) block on fire / exhaust / abort - NOT here.
			u32 addr = 0;
			const int op = advOpCode((args.contains("op") && args["op"].is_string()) ? args["op"].get<std::string>() : std::string());
			if (!args.contains("addr") || !parseAddr(args["addr"], addr))
				writeResp(respDir, seq, false, json::object(), "advance_until requires addr");
			else if (op < 0)
				writeResp(respDir, seq, false, json::object(), "bad op (eq|ne|lt|le|gt|ge|changed|inc|dec)");
			else
			{
				const int width = intArg(args, "width", 1);
				const int maxf  = intArg(args, "max", 0);
				u32 startVal = 0;
				if (maxf <= 0)
					writeResp(respDir, seq, false, json::object(), "advance_until requires max>0");
				// (5) guarded read: a non-work-RAM address can side-effect and desync a replay - refuse it.
				else if (!tas_mvc2::readRamSafe(addr, width, startVal))
					writeResp(respDir, seq, false, json::object(), "address not in work RAM");
				else
				{
					if (gui_state != GuiState::Paused) gui_open_pause();   // baseline paused before we single-step
					s_advancing     = true;
					s_advSeq        = seq;
					s_advAddr       = addr;
					s_advWidth      = width;
					s_advOp         = op;
					s_advVal        = (u32)intArg(args, "val", 0);
					s_advStartVal   = startVal;
					s_advStartFrame = dojo.frame_number.load();
					s_advMax        = maxf;
					s_advSaveSlot   = intArg(args, "save", -1);
					// NO resp now - written on fire/exhaust/abort by the (4b) s_advancing block. (seq is still
					// marked handled below, so it is not re-dispatched while the advance runs.)
				}
			}
		}
		else if (verb == "pause")
		{
			// ensure PAUSED, idempotent. gui_open_pause() is a TOGGLE (Closed/running -> pause,
			// Paused -> resume), so call it ONLY when NOT already Paused - never toggle a paused
			// machine back to running. Pausing stops the emulator (emu.stop); it does NOT advance
			// dojo.frame_number, so a pause never perturbs the movie.
			if (gui_state != GuiState::Paused)
				gui_open_pause();
			json extra = json::object();
			extra["paused"] = (gui_state == GuiState::Paused);
			writeResp(respDir, seq, true, extra, "");
		}
		else if (verb == "resume")
		{
			// ensure RUNNING, idempotent. Only toggle when Paused. gui_open_pause() resumes (emu.start)
			// and returns immediately - the guest advances on LATER frames, so resume itself neither
			// skips nor doubles a frame.
			if (gui_state == GuiState::Paused)
				gui_open_pause();
			json extra = json::object();
			extra["paused"] = (gui_state == GuiState::Paused);
			writeResp(respDir, seq, true, extra, "");
		}
		else if (verb == "set_mode")
		{
			// {"mode":"READ"|"READWRITE"|"WRITE"} (case-insensitive). READWRITE/WRITE are pure flag flips
			// (play_match/macro_armed/divergence_open) - no frame change. READ routes through
			// gui_enter_readonly, which at a movie frontier may seek BASE (slot 0) - existing R-hotkey
			// behavior, not fought here.
			std::string m = (args.contains("mode") && args["mode"].is_string()) ? args["mode"].get<std::string>() : std::string();
			for (char& c : m) c = (char)std::toupper((unsigned char)c);
			const int which = (m == "READ") ? 0 : (m == "READWRITE") ? 1 : (m == "WRITE") ? 2 : -1;
			if (which < 0)
				writeResp(respDir, seq, false, json::object(), "mode must be READ|READWRITE|WRITE");
			else
			{
				gui_set_driver(which);
				json extra = json::object();
				extra["mode"] = modeStr();   // re-read the 3-way label AFTER the switch to confirm it took
				writeResp(respDir, seq, true, extra, "");
			}
		}
		else if (verb == "input")
		{
			// args: p1 / p2 = canon u16 bitmasks (the client encodes button names -> canon); frame (default = the
			// NEXT stepped frame, dojo.frame_number); hold = consecutive frames (default 1). Writing session_inputs is
			// only safe while the emulator is PAUSED (the emu thread owns it while running), so ensure paused first.
			int hold = intArg(args, "hold", 1); if (hold < 1) hold = 1;
			const u16 p1 = (u16)intArg(args, "p1", 0);
			const u16 p2 = (u16)intArg(args, "p2", 0);
			if (gui_state != GuiState::Paused) gui_open_pause();
			const u32 frame = (args.contains("frame") && args["frame"].is_number_integer())
							? (u32)intArg(args, "frame", 0) : dojo.frame_number.load();
			dojo.InjectInput(frame, p1, p2, (u32)hold);
			json extra = json::object();
			extra["frame"] = frame; extra["hold"] = hold; extra["p1"] = p1; extra["p2"] = p2;
			writeResp(respDir, seq, true, extra, "");
		}
		else if (verb == "write_macro")
		{
			// Persist the current session_inputs roll to <clip-folder>_macro.txt - the control-plane equivalent
			// of the Test Lab's red "Overwrite" button. WriteMacroFile refuses when macro_locked (the FINALIZED
			// guard), so force past it for this ONE write, exactly like the GUI's Overwrite. session_inputs is
			// owned by the emu thread while running, so ensure PAUSED first (same rule as `input`). Optional
			// "lock":true finalizes the test (setLocked + macro_locked) = "Overwrite + lock"; default false
			// leaves it editable so the roll can keep being appended.
			if (gui_state != GuiState::Paused) gui_open_pause();
			const bool lock = (args.contains("lock") && args["lock"].is_boolean()) && args["lock"].get<bool>();
			const bool prevForce = dojo.macro_force_write;
			dojo.macro_force_write = true;
			const u32 n = dojo.WriteMacroFile();
			dojo.macro_force_write = prevForce;
			if (lock && !hostfs::savestateFolderOverride.empty())
			{
				tas_clip::setLocked(hostfs::savestateFolderOverride, true);
				dojo.macro_locked = true;
			}
			tas_clip::bump();
			json extra = json::object();
			extra["frames_written"] = n;
			extra["locked"]         = lock;
			extra["macro_dir"]      = hostfs::savestateFolderOverride;
			// n==0 means nothing to write (empty roll / no clip folder) - surface it as a failure so the caller notices.
			writeResp(respDir, seq, n > 0, extra, n > 0 ? "" : "nothing written (empty roll or no clip folder)");
		}
		else
		{
			writeResp(respDir, seq, false, json::object(), "unknown verb: " + verb);
		}
	}
	catch (const std::exception& e)
	{
		// (4-major) any nlohmann / logic exception becomes a clean failure resp for this seq, never a crash.
		writeResp(respDir, seq, false, json::object(), e.what());
		NOTICE_LOG(INPUT, "CTL: verb=%s seq=%d threw: %s", verb.c_str(), seq, e.what());
	}

	s_lastHandledSeq = seq;      // even for step (prevents re-dispatch while it lands)
	NOTICE_LOG(INPUT, "CTL: verb=%s seq=%d -> frame %u", verb.c_str(), seq, (u32)dojo.frame_number.load());
}
}
