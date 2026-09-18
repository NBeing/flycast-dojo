#include "surface_tour.h"
#include "dojo.h"
#include "roll_host.h"
#include "hotkey_bind.h"
#include "session.h"
#include "tas_branch.h"
#include "tas_clip.h"
#include "thumbnail.h"
#include "branch_export.h"
#include "rend/gui.h"
#include "rend/video_recorder.h"
#include "input/gamepad.h"
#include "input/gamepad_device.h"
#include "input/mapping.h"
#include "input/hold_repeat.h"
#include "oslib/oslib.h"
#include "stdclass.h"
#include <fstream>
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "deps/filesystem.hpp"
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

/*
	THE HOOKS THAT LIVE HERE - the ones whose verbs are already public (the Host,
	gui_*, tas_branch, videorec, bexport). Each drives the REAL verb the button calls,
	then READS BACK the truth and returns that; why() is set before any false. The
	hooks whose verbs are static in a feature's own translation unit are DEFINED
	THERE (roll_panel, snippets_panel, macros_panel, notepad_panel, sender_panel,
	lab_panel, fst) - same contract, same header.

	PRECONDITIONS ARE SET BY THE HOOK, NOT ASSUMED. The runner puts the machine
	Paused + WRITE before the feature phase, but probeTick() (the unit drive) runs a
	hook with no runner in front of it - so a hook that needs Paused pauses, and one
	that needs WRITE clears play_match. Idempotent either way.

	probeTick(): dojo:TourHook=<name>[,<name>...] runs the named hooks IN ORDER once
	the machine is ready and logs one `TOUR HOOK: <name> -> PASS|FAIL (<why>)` per
	name - a comma list because save->load->delete and create->checkout->back are
	only meaningful as sequences.
*/
namespace roll {
namespace surfacetour {
namespace hooks {

// ---- shared -----------------------------------------------------------------------

//! Paused + WRITE, the contract's floor. Forced rather than assumed (see above).
static void ensureAuthoring()
{
	if (gui_state != GuiState::Paused)
		gui_pause_for_checkout();
	dojo.play_match = false;
}

static void setSlot(int slot)
{
	config::SavestateSlot.set(slot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(slot));
}

static bool exists(const std::string& p)
{
	std::error_code ec;
	return !p.empty() && ghc::filesystem::exists(p, ec);
}

static int64_t mtimeOf(const std::string& p)
{
	std::error_code ec;
	if (!exists(p))
		return 0;
	return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
			ghc::filesystem::last_write_time(p, ec).time_since_epoch()).count();
}

// ---- states -----------------------------------------------------------------------

bool statesLabelRoundTrip()
{
	Host *h = host();
	SlotView v0;
	if (h == nullptr || !h->slotView(0, v0) || !v0.exists)
	{
		why("slot 0 does not exist");
		return false;
	}
	const std::string was = v0.label;
	const char *want = "tour label";
	// SABOTAGE "label" (surface_tour.h v2): the restored defect is a sidecar write that
	// silently did nothing - setSlotLabel is skipped and reports success. The read-back
	// through slotView stays HONEST; it is the instrument that must expose the defect.
	const bool wrote = surfacetour::sabotaged("label") ? true : h->setSlotLabel(0, want);
	SlotView v1;
	const bool readBack = h->slotView(0, v1) && v1.label == want;
	const bool restored = h->setSlotLabel(0, was);
	SlotView v2;
	const bool back = h->slotView(0, v2) && v2.label == was;
	if (!(wrote && readBack && restored && back))
	{
		why("wrote=%d readback=%d restored=%d back=%d", wrote, readBack, restored, back);
		return false;
	}
	return true;
}

// ---- the BASE guard (surface_tour.h v3) ----------------------------------------------
//
// Driven through the KEY, not a verb: injectKey presses and releases the bound
// SAVESTATE code on the keyboard device, so the dispatch's guard (gamepad_device.cpp)
// is what is exercised. The truth is read back two ways - the guard's own counters
// (hotkeys::baseHoldStats) AND the slot-0 file's bytes - so a guard that counted a
// block while the file changed, or the reverse, cannot pass.

static u64 fileFnv(const std::string& p)
{
	std::ifstream in(p, std::ios::binary);
	if (!in)
		return 0;
	u64 h = 1469598103934665603ull;
	char buf[65536];
	while (in.read(buf, sizeof(buf)) || in.gcount() > 0)
		for (std::streamsize i = 0; i < in.gcount(); i++)
			h = (h ^ (u8)buf[i]) * 1099511628211ull;
	return h;
}

static const char *const kSlotExts[] = { "", ".frame", ".png", ".label" };

//! Copy slot 0's files aside (<file>.tourbak) so the hold step can restore them:
//! the tour's later steps (branch create, test lab add) build on the harness base.
static void backupSlot0()
{
	const std::string base = hostfs::getSavestatePath(0, false);
	for (const char *ext : kSlotExts)
	{
		std::error_code ec;
		const std::string src = base + ext, dst = base + ext + ".tourbak";
		ghc::filesystem::remove(dst, ec);
		if (exists(src))
			ghc::filesystem::copy_file(src, dst, ec);
	}
}

static void restoreSlot0()
{
	tas_thumb::flush();	// the thumbnail worker may still be writing <state>.png for the hold's save
	const std::string base = hostfs::getSavestatePath(0, false);
	for (const char *ext : kSlotExts)
	{
		std::error_code ec;
		const std::string dst = base + ext, bak = base + ext + ".tourbak";
		ghc::filesystem::remove(dst, ec);
		if (exists(bak))
			ghc::filesystem::rename(bak, dst, ec);
	}
}

static u32 savestateKeyCode()
{
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	if (kbd == nullptr || kbd->get_input_mapping() == nullptr)
		return 0;
	return kbd->get_input_mapping()->get_button_code(0, EMU_BTN_SAVESTATE);
}

bool baseTapBlocked()
{
	ensureAuthoring();
	const std::string path = hostfs::getSavestatePath(0, false);
	if (!exists(path))
	{
		why("slot 0 holds no state - nothing to guard");
		return false;
	}
	const int user = (int)config::SavestateSlot;
	setSlot(0);
	backupSlot0();
	const u64 before = fileFnv(path);
	const int blocked0 = hotkeys::baseHoldStats().blocked, written0 = hotkeys::baseHoldStats().written;
	bool sent;
	if (surfacetour::sabotaged("base"))
	{
		// SABOTAGE "base": the restored defect is "F1 saves on press" - the tap writes.
		// The read-back below (bytes + counters) is the instrument that must expose it.
		gui_saveState();
		sent = true;
	}
	else
	{
		const u32 code = savestateKeyCode();
		if (code == 0) { setSlot(user); why("SAVESTATE is unbound on the keyboard"); return false; }
		sent = surfacetour::injectKey(code);	// press + release: a TAP, well inside BaseHoldMs
	}
	setSlot(user);
	if (!sent)
	{
		restoreSlot0();
		return false;
	}
	const u64 after = fileFnv(path);
	const int blocked = hotkeys::baseHoldStats().blocked - blocked0;
	const int written = hotkeys::baseHoldStats().written - written0;
	// ALWAYS restore: under the `base` arm the tap DID write, and the hold step (its
	// own backup, no needsPrev) and every later slot-0 step must still see David's
	// base - a sabotaged tap must not leak into the control's fixture.
	restoreSlot0();
	if (after != before || blocked != 1 || written != 0)
	{
		why("file %s, blocked=%d written=%d (want unchanged, 1, 0)", after == before ? "unchanged" : "CHANGED", blocked, written);
		return false;
	}
	why("tap BLOCKED, slot 0 bytes unchanged (fnv %016llx)", (unsigned long long)before);
	return true;
}

bool baseHoldWrites()
{
	// A polled hook (the runner's captures-stop shape): the first call presses and
	// returns false; each later call polls; the call that sees the write releases the
	// key, restores slot 0 from its own copy, and returns true. A hook-local
	// deadline releases the key and restores on timeout, so a red step never leaves
	// F1 down or the sandbox's slot 0 overwritten.
	static int phase = 0;			// 0 idle, 1 held, 2 done (one-shot per tour)
	static double pressedAt = 0;
	static u64 before = 0;
	static int written0 = 0, user = 0;
	const std::string path = hostfs::getSavestatePath(0, false);
	const int holdMs = cfgLoadInt("dojo", "BaseHoldMs", 1000);
	if (phase == 2)
		return true;
	if (phase == 0)
	{
		ensureAuthoring();
		if (!exists(path)) { why("slot 0 holds no state"); return false; }
		const u32 code = savestateKeyCode();
		const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
		if (code == 0 || kbd == nullptr) { why("SAVESTATE is unbound on the keyboard"); return false; }
		user = (int)config::SavestateSlot;
		setSlot(0);
		backupSlot0();		// its OWN copy - this step must run (and restore) even when the tap step went red
		before = fileFnv(path);
		written0 = hotkeys::baseHoldStats().written;
		kbd->gamepad_btn_input(code, true);		// press and HOLD - the release comes from the poll
		pressedAt = os_GetSeconds();
		phase = 1;
		why("holding F1 (%d ms to go)", holdMs);
		return false;
	}
	const int written = hotkeys::baseHoldStats().written - written0;
	const double heldMs = (os_GetSeconds() - pressedAt) * 1000.0;
	if (written < 1)
	{
		if (heldMs < holdMs + 2500.0)
		{
			why("holding F1 (%d of %d ms)", (int)heldMs, holdMs);
			return false;
		}
		const u32 code = savestateKeyCode();
		if (rebind::keyboard() != nullptr) rebind::keyboard()->gamepad_btn_input(code, false);
		setSlot(user);
		restoreSlot0();
		phase = 2;	// do not re-press on later polls
		why("held %d ms, the guard never wrote (written=%d)", (int)heldMs, written);
		return false;
	}
	const u64 after = fileFnv(path);
	const u32 code = savestateKeyCode();
	if (rebind::keyboard() != nullptr) rebind::keyboard()->gamepad_btn_input(code, false);
	setSlot(user);
	const bool changed = after != before;
	restoreSlot0();
	phase = 2;
	if (!changed)
	{
		why("the guard counted a write but slot 0's bytes did not change");
		return false;
	}
	why("hold matured at %d ms, slot 0 rewritten (%016llx -> %016llx) then restored", (int)heldMs, (unsigned long long)before, (unsigned long long)after);
	return true;
}

// ---- savestates: the scratch slot -------------------------------------------------

bool saveScratchSlot()
{
	ensureAuthoring();
	const int user = (int)config::SavestateSlot;
	// SABOTAGE "save" (surface_tour.h v2): the restored defect is a save that lands in
	// the WRONG slot (98) while the step claims 99. The file check at 99 below is the
	// honest instrument; it must expose the defect. The stray 98 is removed here so the
	// sandbox never carries a state the tour did not mean to leave.
	const bool wrongSlot = surfacetour::sabotaged("save");
	setSlot(wrongSlot ? kScratchSlot - 1 : kScratchSlot);
	gui_saveState();				// synchronous while Paused: dc_savestate runs before it returns
	setSlot(user);
	if (wrongSlot)
	{
		// `[MEASURED 2026-09-17]` the thumbnail is encoded on a worker thread, so a remove
		// that runs straight after gui_saveState races it and a stray <state>.png lands a
		// moment later (and the branch step then copies it). Drain the worker first.
		tas_thumb::flush();
		const std::string stray = hostfs::getSavestatePath(kScratchSlot - 1, false);
		for (const char *ext : { "", ".frame", ".png", ".label" })
		{
			std::error_code ec;
			ghc::filesystem::remove(stray + ext, ec);
		}
	}
	const std::string path = hostfs::getSavestatePath(kScratchSlot, false);
	if (!exists(path))
	{
		why("no file at %s (gui_saveState refused?)", path.c_str());
		return false;
	}
	return true;
}

bool loadScratchSlot()
{
	ensureAuthoring();
	Host *h = host();
	SlotView v;
	if (h == nullptr || !h->slotView(kScratchSlot, v) || !v.exists)
	{
		why("slot %d does not exist (save first)", kScratchSlot);
		return false;
	}
	const int user = (int)config::SavestateSlot;
	setSlot(kScratchSlot);
	gui_loadState();
	setSlot(user);
	const u32 fr = dojo.frame_number.load();
	if (gui_state != GuiState::Paused || !v.haveFrame || fr != v.frame)
	{
		why("after load: paused=%d frame=%u expected=%u haveFrame=%d",
				gui_state == GuiState::Paused, fr, v.frame, v.haveFrame);
		return false;
	}
	return true;
}

bool deleteScratchSlot()
{
	Host *h = host();
	if (h == nullptr)
	{
		why("no host");
		return false;
	}
	const std::string path = hostfs::getSavestatePath(kScratchSlot, false);
	if (!exists(path))
	{
		why("nothing to delete at %s", path.c_str());
		return false;
	}
	const bool deleted = h->deleteSlot(kScratchSlot);
	if (!deleted || exists(path))
	{
		why("deleteSlot=%d stillExists=%d", deleted, exists(path));
		return false;
	}
	return true;
}

// ---- the F2 cycle, through the LIVE binding ---------------------------------------

static bool cycleSlot(DreamcastKey action, int step)
{
	ensureAuthoring();				// gui_hotkey_allowed() needs Paused (or Closed)
	const std::shared_ptr<GamepadDevice> kbd = rebind::keyboard();
	if (kbd == nullptr || kbd->get_input_mapping() == nullptr)
	{
		why("no keyboard");
		return false;
	}
	const u32 code = kbd->get_input_mapping()->get_button_code(0, action);
	if (code == (u32)-1)
	{
		why("unbound");
		return false;
	}
	if (gui_keyboard_captured())
	{
		why("keyboard captured by a text field");
		return false;
	}
	const int was = hostfs::currentSavestateSlot();
	const int expected = (was + step) % hostfs::MAX_SAVESTATE_SLOTS;
	if (!injectKey(code))
		return false;
	const int now = hostfs::currentSavestateSlot();
	if (now != expected)
	{
		why("slot %d -> %d, expected %d (code %u)", was, now, expected, code);
		return false;
	}
	return true;
}

bool slotNext() { return cycleSlot(EMU_BTN_SAVESTATE_SLOT_NEXT, 1); }
bool slotPrev() { return cycleSlot(EMU_BTN_SAVESTATE_SLOT_PREV, hostfs::MAX_SAVESTATE_SLOTS - 1); }

// ---- the R toggle -------------------------------------------------------------------

static bool setDriver(int which, session::Mode want, const char *name)
{
	gui_set_driver(which);
	if (session::mode() != want)
	{
		why("mode is not %s after gui_set_driver(%d)", name, which);
		return false;
	}
	return true;
}

bool driverRead()      { return setDriver(0, session::Mode::Read,      "READ"); }
bool driverReadWrite() { return setDriver(1, session::Mode::ReadWrite, "READ-WRITE"); }
bool driverWrite()     { return setDriver(2, session::Mode::Write,     "WRITE"); }

// ---- branches ---------------------------------------------------------------------

static std::string g_root;			//!< the clip we forked from
static std::string g_branchDir;		//!< the branch we made

bool branchCreate()
{
	ensureAuthoring();
	const std::string head = hostfs::savestateFolderOverride;
	if (head.empty())
	{
		why("no clip folder bound");
		return false;
	}
	if (tas_branch::isBranchDir(head))
	{
		why("already on a branch (%s) - depth-1 only", head.c_str());
		return false;
	}
	Host *h = host();
	SlotView v;
	if (h == nullptr || !h->slotView(0, v) || !v.exists)
	{
		why("slot 0 does not exist");
		return false;
	}
	g_root = head;
	// READ BACK THE BRANCH COUNT, not just the returned path: a create that silently
	// did nothing and handed back an existing dir would read as success otherwise.
	const std::string bdir = head + "/branches";
	auto countBranches = [&]() {
		size_t n = 0;
		std::error_code ec;
		if (ghc::filesystem::is_directory(bdir, ec))
			for (const auto& e : ghc::filesystem::directory_iterator(bdir, ec))
				if (e.is_directory(ec)) n++;
		return n;
	};
	const size_t nBefore = countBranches();
	// SABOTAGE "branch" (surface_tour.h v2): the restored defect is a create that silently
	// does nothing. The count read-back is the honest instrument that must expose it;
	// checkout/back then SKIP via needsPrev.
	if (surfacetour::sabotaged("branch"))
		g_branchDir.clear();
	else
		g_branchDir = tas_branch::create(head, 0, v.frame, "tour", "tour", "surface tour");
	const size_t nAfter = countBranches();
	if (nAfter <= nBefore || g_branchDir.empty() || !exists(g_branchDir))
	{
		why("branches %zu -> %zu, create returned '%s'", nBefore, nAfter, g_branchDir.c_str());
		return false;
	}
	return true;
}

bool branchCheckout()
{
	ensureAuthoring();
	if (g_branchDir.empty())
	{
		why("no branch created yet");
		return false;
	}
	if (!roll::branch::checkoutFolder(g_branchDir, 0, "tour"))
	{
		why("checkoutFolder refused");
		return false;
	}
	if (hostfs::savestateFolderOverride != g_branchDir)
	{
		why("head is %s, not the branch", hostfs::savestateFolderOverride.c_str());
		return false;
	}
	return true;
}

bool branchBackToMain()
{
	ensureAuthoring();
	const std::string root = !g_root.empty() ? g_root : tas_branch::rootOf(hostfs::savestateFolderOverride);
	if (root.empty())
	{
		why("no root to return to");
		return false;
	}
	if (!roll::branch::checkoutFolder(root, 0, "main"))
	{
		why("checkoutFolder(root) refused");
		return false;
	}
	if (hostfs::savestateFolderOverride != root)
	{
		why("head is %s, not the root", hostfs::savestateFolderOverride.c_str());
		return false;
	}
	return true;
}

// ---- captures -----------------------------------------------------------------------

static std::string g_capPath;

bool capturesStart()
{
	ensureAuthoring();
	const std::string stamp = roll::captures::clipStamp(hostfs::savestateFolderOverride);
	if (stamp.empty())
	{
		why("no clip folder bound");
		return false;
	}
	if (videorec::isRecording() || videorec::startPending())
	{
		why("recorder already busy");
		return false;
	}
	const std::string dir = roll::captures::captureDir(stamp);
	std::error_code ec;
	ghc::filesystem::create_directories(dir, ec);
	g_capPath = dir + "/tour.avi";
	videorec::requestStart(g_capPath);
	// The recorder opens on the renderer's NEXT presented frame, and a hook cannot
	// block for one - so run frames and read back what a single call can: the
	// request was accepted and is armed (or already opened). The runner's dwell
	// lets those frames happen before capturesStop reads the file.
	gui_step_frames(30);
	if (!(videorec::startPending() || videorec::isRecording()))
	{
		why("recorder did not arm (no encoder?)");
		return false;
	}
	return true;
}

static bool g_stopRequested = false;

/*
	POLL-SAFE: the runner (and the unit drive's `+name`) call this every tick until
	it says yes, so the one-shot side effect - asking the recorder to stop - happens
	on the first call only, and every later call just reads the file back. Done =
	the output exists and has bytes (the muxer finalises after the writer drains).
*/
bool capturesStop()
{
	if (!g_stopRequested)
	{
		if (!(videorec::isRecording() || videorec::startPending()))
		{
			why("was not recording");
			return false;
		}
		videorec::requestStop();
		gui_step_frames(5);
		g_stopRequested = true;
	}
	std::error_code ec;
	const uintmax_t bytes = exists(g_capPath) ? (uintmax_t)ghc::filesystem::file_size(g_capPath, ec) : 0;
	if (videorec::isRecording() || bytes == 0)
	{
		why("recording=%d bytes=%llu at %s", videorec::isRecording(), (unsigned long long)bytes, g_capPath.c_str());
		return false;
	}
	g_stopRequested = false;
	return true;
}

// ---- branch export -----------------------------------------------------------------

static double g_exportStart = 0;
static std::string g_exportDir;

bool exportLaunch()
{
	ensureAuthoring();
	if (roll::bexport::active())
	{
		why("export already running");
		return false;
	}
	roll::bexport::refreshCandidates();
	if (roll::bexport::candidates().empty())
	{
		why("no candidates");
		return false;
	}
	std::vector<unsigned char>& ck = roll::bexport::checked();
	for (auto& c : ck) c = 0;
	ck[0] = 1;
	g_exportDir = roll::captures::captureDir(roll::captures::clipStamp(hostfs::savestateFolderOverride));
	g_exportStart = os_GetSeconds();
	if (!roll::bexport::launch())
	{
		why("launch refused");
		return false;
	}
	if (!roll::bexport::active())
	{
		why("launched but not active");
		return false;
	}
	return true;
}

bool exportDone()
{
	if (roll::bexport::active())
	{
		why("still running");
		return false;
	}
	// Done = no longer active AND something landed in the capture folder since launch.
	std::error_code ec;
	if (g_exportDir.empty() || !ghc::filesystem::is_directory(g_exportDir, ec))
	{
		why("no export folder");
		return false;
	}
	const int64_t since = (int64_t)g_exportStart;
	for (const auto& e : ghc::filesystem::recursive_directory_iterator(g_exportDir, ec))
		if (e.is_regular_file(ec) && mtimeOf(e.path().string()) >= since - 1)
			return true;
	why("no output newer than the launch under %s", g_exportDir.c_str());
	return false;
}

// ---- the unit drive -------------------------------------------------------------------

struct Entry { const char *name; bool (*fn)(); };
static const Entry kHooks[] = {
	{ "rollEditFlipUndo",     rollEditFlipUndo },
	{ "rollEditRedoUndo",     rollEditRedoUndo },
	{ "statesLabelRoundTrip", statesLabelRoundTrip },
	{ "saveScratchSlot",      saveScratchSlot },
	{ "loadScratchSlot",      loadScratchSlot },
	{ "deleteScratchSlot",    deleteScratchSlot },
	{ "slotNext",             slotNext },
	{ "slotPrev",             slotPrev },
	{ "driverRead",           driverRead },
	{ "driverReadWrite",      driverReadWrite },
	{ "driverWrite",          driverWrite },
	{ "senderSend",           senderSend },
	{ "senderStop",           senderStop },
	{ "senderSendFrameskip",  senderSendFrameskip },
	{ "notepadAnalyze",       notepadAnalyze },
	{ "snippetsPlace",        snippetsPlace },
	{ "macrosPlace",          macrosPlace },
	{ "branchCreate",         branchCreate },
	{ "branchCheckout",       branchCheckout },
	{ "branchBackToMain",     branchBackToMain },
	{ "labAddTest",           labAddTest },
	{ "labTrashTest",         labTrashTest },
	{ "capturesStart",        capturesStart },
	{ "capturesStop",         capturesStop },
	{ "fstArmSweep",          fstArmSweep },
	{ "fstSweepDone",         fstSweepDone },
	{ "exportLaunch",         exportLaunch },
	{ "exportDone",           exportDone },
};

static const Entry *findHook(const std::string& name)
{
	for (const Entry& e : kHooks)
		if (name == e.name)
			return &e;
	return nullptr;
}

/*
	A SEQUENCER, not a loop: the list runs ONE entry per tick so frames can pass
	between entries (a save, then a load; a branch, then its checkout). An entry
	written `+name` is POLLED - called every tick until it returns true or 150 s
	pass - which is how the runner treats a verify with maxWaitMs, and the only
	honest way to unit-drive the hooks that read completion back (fstSweepDone,
	exportDone, capturesStop) instead of asking them a question they cannot yet
	answer.
*/
static const double kPollSeconds = 150.0;

void probeTick()
{
	static bool done = false, parsed = false;
	static std::vector<std::string> names;
	static size_t idx = 0;
	static double pollStart = 0;
	if (done)
		return;
	const std::string list = cfgLoadStr("dojo", "TourHook", "");
	if (list.empty())
		return;
	// Ready = the same floor the runner uses: frames flowing and slot 0 visible to
	// the host. A hook run against a cold machine would fail for the wrong reason.
	if (dojo.frame_number.load() < 120)
		return;
	{
		SlotView v;
		if (host() == nullptr || !host()->slotView(0, v) || !v.exists)
			return;
	}
	if (!parsed)
	{
		parsed = true;
		// ';' as well as ',': the -config parser cuts a value at the first comma, so a
		// list passed on the command line has to be written with semicolons.
		size_t at = 0;
		while (at <= list.size())
		{
			size_t comma = list.find_first_of(",;", at);
			if (comma == std::string::npos)
				comma = list.size();
			std::string name = list.substr(at, comma - at);
			while (!name.empty() && name.front() == ' ') name.erase(name.begin());
			while (!name.empty() && name.back() == ' ') name.pop_back();
			if (!name.empty())
				names.push_back(name);
			at = comma + 1;
		}
	}
	if (idx >= names.size())
	{
		done = true;
		NOTICE_LOG(RENDERER, "TOUR HOOK: done (%d hook%s)", (int)names.size(), names.size() == 1 ? "" : "s");
		return;
	}
	const bool poll = names[idx][0] == '+';
	const std::string name = poll ? names[idx].substr(1) : names[idx];
	const Entry *e = findHook(name);
	if (e == nullptr)
	{
		NOTICE_LOG(RENDERER, "TOUR HOOK: %s -> FAIL (no such hook)", name.c_str());
		idx++;
		return;
	}
	if (poll && pollStart == 0)
		pollStart = os_GetSeconds();
	why("");
	const bool ok = e->fn();
	if (poll && !ok && os_GetSeconds() - pollStart < kPollSeconds)
		return;					// ask again next tick
	NOTICE_LOG(RENDERER, "TOUR HOOK: %s -> %s (%s%s)", e->name, ok ? "PASS" : "FAIL", lastWhy(),
			(poll && !ok) ? " [poll timed out]" : "");
	pollStart = 0;
	idx++;
}

}	// namespace hooks
}	// namespace surfacetour
}	// namespace roll
