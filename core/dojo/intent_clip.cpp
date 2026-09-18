/*
	INTENT MODULE: the clip and its copies (Surface Tour v4, 2026-09-18).

	Owns the steps for: branches (module 3), generations (4), the test lab (6),
	captures (9). Registers them from this TU through surfacetour::registerModule -
	the runner's tables are not edited here. See docs/tour-intent-clip.md and
	intent.h for the ceremony every step shares.

	THE RULE (the user, 2026-09-17): "nothing opened starting on your roll tests...
	each feature should be tested with its INTENT." Every feature here is four
	beats: OPEN its window by the hotkey the tour rebound (the human sees it), ACT
	through the feature's own verb, INTENT - run the game and read what the fighter
	did (the combo counter, the machine hash), CLOSE. The combo is David's
	Combo_Dhalsim97 window on the tour clip's BASE (RECIPE.toml: peak 19).

	David's intents, quoted:
	  branches    "fan out the wakeup options into separate movies, the base intact";
	              acceptance: "root .flyr byte-identical after a branch+checkout
	              round-trip; branch .flyr has the new tail" (BRANCHES_ROADMAP.md)
	  generations "generations are immutable"; F8 = "copy the clip's replay + every
	              state + sidecars + clip.json into the next <clip>_gen_NN"
	  test lab    "a test = a folder whose BASE (slot 0) is the permanent fixture...
	              tests never stomp each other"
	  captures    "a capture contains every emulated frame exactly once"

	Module order: "clip" < "roll" < "send" by name, so this module runs first,
	right after "roll: redo the flip + undo" (READ-WRITE authoring on, roll
	pristine). It ends on BASE with the roll restored (intent::end).
*/
#include "surface_tour.h"
#include "intent.h"
#include "dojo.h"
#include "roll_host.h"
#include "tas_branch.h"
#include "tas_clip.h"
#include "tasmacro.h"
#include "rend/gui.h"
#include "rend/panel.h"
#include "rend/video_recorder.h"
#include "input/mapping.h"
#include "oslib/oslib.h"
#include "emulator.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "deps/filesystem.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#undef verify	// types.h's macro; Step::verify is a field (surface_tour.cpp does the same)

namespace roll {
namespace surfacetour {

namespace {

// The tour's chords for the four windows (surface_tour.cpp KEYS[], the same codes
// the rebind phase bound them to; still bound while the feature phase runs).
const u32 CTRL = InputMapping::KEY_MOD_CTRL, ALT = InputMapping::KEY_MOD_ALT, SHIFT = InputMapping::KEY_MOD_SHIFT;
const u32 KEY_BRANCHES = ALT  | 60;		// Alt+F3
const u32 KEY_STATES   = CTRL | 60;		// Ctrl+F3
const u32 KEY_TESTLAB  = ALT  | 58;		// Alt+F1
const u32 KEY_CAPTURES = CTRL | 64;		// Ctrl+F7
const u32 KEY_GEN_ARCHIVE = SHIFT | 65;	// Shift+F8: btn_gen_archive's default (keyboard_device.h)

const u16 EXPECT_PEAK = 19;				// scripts/fixtures/mvc2/RECIPE.toml [result] combo_peak, measured 2026-09-17

struct State
{
	std::string root;					// the main clip dir at begin
	std::string branchDir, branchId;
	u64 mainFlyrHash = 0;
	std::string genName;
	u32 afterHash = 0;					// the machine after the combo landed on main (the RECIPE's `after`)
	std::string labDir;
	bool labBound = false;
	std::string capPath;
	u32 capFrom = 0;
	u64 capWrittenAtFrom = 0;
	u32 capTo = 0;
	bool capStopRequested = false;
	bool capDupArmed = false;
	std::string capWas;
} st;

bool panelOpen(const char *id)
{
	const panels::Panel *p = panels::find(id);
	return p != nullptr && p->open != nullptr && *p->open;
}

bool exists(const std::string& p)
{
	std::error_code ec;
	return !p.empty() && ghc::filesystem::exists(p, ec);
}

//! FNV-1a 64 over a file's bytes; 0 for a missing file.
u64 fileHash(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.good())
		return 0;
	u64 h = 1469598103934665603ull;
	char buf[65536];
	while (in.read(buf, sizeof(buf)) || in.gcount() > 0)
	{
		const std::streamsize n = in.gcount();
		for (std::streamsize i = 0; i < n; i++)
			h = (h ^ (u8)buf[i]) * 1099511628211ull;
	}
	return h;
}

//! The clip's movie file, if any.
std::string flyrIn(const std::string& dir)
{
	std::error_code ec;
	if (!ghc::filesystem::is_directory(dir, ec))
		return "";
	for (const auto& e : ghc::filesystem::directory_iterator(dir, ec))
	{
		const std::string ext = e.path().extension().string();
		if (ext == ".flyr" || ext == ".flyreplay")
			return e.path().string();
	}
	return "";
}

//! <clip>_gen_NN subfolders of dir, by name.
std::vector<std::string> gensIn(const std::string& dir)
{
	std::vector<std::string> out;
	std::error_code ec;
	if (!ghc::filesystem::is_directory(dir, ec))
		return out;
	for (const auto& e : ghc::filesystem::directory_iterator(dir, ec))
		if (e.is_directory(ec) && e.path().filename().string().find("_gen_") != std::string::npos)
			out.push_back(e.path().filename().string());
	return out;
}

std::string gameOf(const std::string& clipDir)
{
	// replays/<game>/<clip> - the lab is keyed on <game>.
	return ghc::filesystem::path(clipDir).parent_path().filename().string();
}

// ---- step makers ---------------------------------------------------------------------

Step openStep(const char *name, u32 code, const char *panel)
{
	Step s;
	s.name = name;
	s.act = [code, panel] { why(""); if (panelOpen(panel)) return true; return injectKey(code); };
	s.verify = [panel] { if (panelOpen(panel)) return true; why("%s not open", panel); return false; };
	s.maxWaitMs = 2000;
	return s;
}

Step closeStep(const char *name, u32 code, const char *panel)
{
	Step s;
	s.name = name;
	s.act = [code, panel] { why(""); if (!panelOpen(panel)) return true; return injectKey(code); };
	s.verify = [panel] { if (!panelOpen(panel)) return true; why("%s still open", panel); return false; };
	s.maxWaitMs = 2000;
	return s;
}

//! Run the placed roll to the stop frame and read the P1 combo peak while stopped.
Step runStep(const char *name, u16 expectPeak, u32 *hashOut = nullptr)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.needsPrev = true;
	s.maxWaitMs = 60000;
	s.act = [] { why(""); if (intent::runToStop()) return true; why("%s", intent::lastWhy()); return false; };
	s.verify = [expectPeak, hashOut] {
		if (!intent::settled()) return false;
		const u16 p = intent::peak(0);
		if (hashOut != nullptr) *hashOut = intent::machineHash();
		if (p == expectPeak) { why("peak %u at frame %u, hash %08X", (unsigned)p, dojo.frame_number.load(), intent::machineHash()); return true; }
		why("peak %u (want %u) at frame %u", (unsigned)p, (unsigned)expectPeak, dojo.frame_number.load());
		return false;
	};
	return s;
}

Step reloadStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 15000;
	s.act = [] { why(""); if (intent::reloadBase()) return true; why("%s", intent::lastWhy()); return false; };
	s.verify = [] { if (!intent::settled()) return false; why("frame %u hash %08X", dojo.frame_number.load(), intent::machineHash()); return true; };
	return s;
}

Step placeStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.needsPrev = true;
	s.act = [] { why(""); if (intent::placeCombo(0, 0)) { why("%s at t0=%u, %u rows", intent::comboName(), intent::t0(), intent::comboLen()); return true; } why("%s", intent::lastWhy()); return false; };
	return s;
}

//! A segment's own ceremony: snapshot the roll, reload BASE. Each segment begins and ends
//! on its own so an ARM in one segment cannot make the next segment's steps vacuous
//! (`[MEASURED 2026-09-18]` under branch-leak, gen's "damage" cleared a roll the leak had
//! already cleared - VACUOUS, which the harness's G4 refuses under an arm).
Step beginStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 15000;
	s.act = [] { why(""); if (!intent::begin("clip")) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [] { if (!intent::settled()) return false; why("BASE slot0@%u hash %08X", intent::baseFrame(), intent::baseHash()); return true; };
	return s;
}

Step endStep(const char *name)
{
	Step s;
	s.name = name;
	s.kind = Kind::Record;
	s.maxWaitMs = 15000;
	s.act = [] { why(""); if (!intent::end()) { why("%s", intent::lastWhy()); return false; } return true; };
	s.verify = [] {
		if (!intent::settled()) return false;
		const u32 h = intent::machineHash();
		if (h != intent::baseHash()) { why("hash %08X != BASE %08X", h, intent::baseHash()); return false; }
		why("BASE hash %08X, roll restored", h);
		return true;
	};
	return s;
}

// ---- the module ----------------------------------------------------------------------

void addSteps(std::vector<Step>& out)
{
	auto add = [&](Step s) { out.push_back(std::move(s)); };

	// ===== 3. BRANCHES =====================================================================
	{
		Step s;
		s.name = "branch intent: begin (roll snapshot, BASE)";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			st = State();
			st.root = hostfs::savestateFolderOverride;
			if (tas_branch::isBranchDir(st.root)) { why("already on a branch"); return false; }
			if (!intent::begin("clip")) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] { if (!intent::settled()) return false; why("BASE slot0@%u hash %08X", intent::baseFrame(), intent::baseHash()); return true; };
		add(s);
	}
	add(openStep("branch intent: open branches (Alt+F3)", KEY_BRANCHES, "branches"));
	add(placeStep("branch intent: place the combo on main"));
	add(runStep("branch intent: run main - the combo lands (peak 19)", EXPECT_PEAK, &st.afterHash));
	add(reloadStep("branch intent: reload BASE"));
	{
		Step s;
		s.name = "branch intent: create a branch from slot 0";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			std::error_code ec;
			std::vector<std::string> before;
			const std::string bdir = st.root + "/branches";
			if (ghc::filesystem::is_directory(bdir, ec))
				for (const auto& e : ghc::filesystem::directory_iterator(bdir, ec)) before.push_back(e.path().string());
			if (!hooks::branchCreate())
				return false;
			for (const auto& e : ghc::filesystem::directory_iterator(bdir, ec))
			{
				bool seen = false;
				for (const std::string& b : before) if (b == e.path().string()) seen = true;
				if (!seen) { st.branchDir = e.path().string(); st.branchId = e.path().filename().string(); }
			}
			if (st.branchDir.empty()) { why("no new branch dir under %s", bdir.c_str()); return false; }
			why("%s", st.branchId.c_str());
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "branch intent: checkout the branch";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			// ARM branch-leak: the checkout never happens, so the clear below lands on MAIN -
			// the restored defect is "a branch edit that leaks into main". Every later
			// read-back is honest about it: main's second run reads peak 0.
			if (sabotaged("branch-leak")) { why("SABOTAGE branch-leak: checkout skipped"); return true; }
			return hooks::branchCheckout();
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			// main's .flyr is final once the checkout flushed the live set; hash it here.
			st.mainFlyrHash = fileHash(flyrIn(st.root));
			if (sabotaged("branch-leak")) return true;
			if (hostfs::savestateFolderOverride != st.branchDir) { why("head is %s", hostfs::savestateFolderOverride.c_str()); return false; }
			why("head = %s, main .flyr %016llx", st.branchId.c_str(), (unsigned long long)st.mainFlyrHash);
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "branch intent: clear the combo on the branch";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] { why(""); if (intent::clearCombo()) return true; why("%s", intent::lastWhy()); return false; };
		add(s);
	}
	add(runStep("branch intent: run the branch - no combo (peak 0)", 0));
	{
		Step s;
		s.name = "branch intent: back to main";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 15000;
		s.act = [] { why(""); return hooks::branchBackToMain(); };
		s.verify = [] {
			if (!intent::settled()) return false;
			if (hostfs::savestateFolderOverride != st.root) { why("head is %s", hostfs::savestateFolderOverride.c_str()); return false; }
			why("head = main, frame %u", dojo.frame_number.load());
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "branch intent: root .flyr byte-identical, branch .flyr differs";
		// David's acceptance is the ROUND-TRIP alone: hashed here, before main runs again -
		// `[MEASURED 2026-09-18]` a run in READ-WRITE appends its tail to the .flyr
		// (FlushReplay), so measuring after the run reads a rewrite the branch never did.
		s.needsPrev = true;
		s.act = [] {
			why("");
			const u64 root = fileHash(flyrIn(st.root)), br = fileHash(flyrIn(st.branchDir));
			if (root != st.mainFlyrHash) { why("root .flyr %016llx != %016llx before", (unsigned long long)root, (unsigned long long)st.mainFlyrHash); return false; }
			if (br == 0 || br == root) { why("branch .flyr %016llx (root %016llx)", (unsigned long long)br, (unsigned long long)root); return false; }
			why("root %016llx unchanged, branch %016llx", (unsigned long long)root, (unsigned long long)br);
			return true;
		};
		add(s);
	}
	{
		// needsPrev OFF on purpose (arm rule 6, TEST-PLAN §5.2): under `branch-leak` the .flyr
		// identity step above reddens first (main WAS edited - correctly), and this target must
		// still RUN to be judged; "back to main" put the machine on BASE either way.
		Step s = runStep("branch intent: run main again - the combo still lands (peak 19)", EXPECT_PEAK);
		s.needsPrev = false;
		add(s);
	}
	add(reloadStep("branch intent: reload BASE"));
	{
		Step s;
		s.name = "branch intent: delete the tour branch";
		s.optional = true;
		s.act = [] {
			why("");
			if (st.branchId.empty()) { why("no branch to delete"); return false; }
			if (!tas_branch::remove(st.root, st.branchId)) { why("remove refused"); return false; }
			if (exists(st.branchDir)) { why("%s still exists", st.branchDir.c_str()); return false; }
			return true;
		};
		add(s);
	}
	add(closeStep("branch intent: close branches", KEY_BRANCHES, "branches"));
	add(endStep("branch intent: end segment (roll restored, BASE)"));

	// ===== 4. GENERATIONS ==================================================================
	add(openStep("gen intent: open states (Ctrl+F3)", KEY_STATES, "states"));
	add(beginStep("gen intent: begin segment (roll snapshot, BASE)"));
	add(placeStep("gen intent: place the combo on main"));
	{
		Step s;
		s.name = "gen intent: Shift+F8 archives a generation";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 5000;
		s.act = [] {
			why("");
			const std::vector<std::string> before = gensIn(st.root);
			if (!injectKey(KEY_GEN_ARCHIVE)) { why("no keyboard"); return false; }
			st.genName.clear();
			for (const std::string& g : gensIn(st.root))
			{
				bool seen = false;
				for (const std::string& b : before) if (b == g) seen = true;
				if (!seen) st.genName = g;
			}
			return true;
		};
		s.verify = [] {
			if (st.genName.empty())
			{
				// the archive runs on the key press; a slow copy shows up a tick later
				const std::vector<std::string> now = gensIn(st.root);
				if (now.empty()) { why("no <clip>_gen_NN under %s", st.root.c_str()); return false; }
				st.genName = now.back();
			}
			const std::string flyr = flyrIn(st.root + "/" + st.genName);
			if (flyr.empty()) { why("%s has no .flyr", st.genName.c_str()); return false; }
			why("%s (.flyr %016llx)", st.genName.c_str(), (unsigned long long)fileHash(flyr));
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "gen intent: damage - clear the combo on main";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] { why(""); if (intent::clearCombo()) return true; why("%s", intent::lastWhy()); return false; };
		add(s);
	}
	add(runStep("gen intent: run main - damaged (peak 0)", 0));
	add(reloadStep("gen intent: reload BASE"));
	{
		Step s;
		s.name = "gen intent: restore the generation";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			// ARM gen-noop: the restore copies nothing - the restored defect is a restore
			// that only SAYS it restored. The run below must read the damage (peak 0).
			if (!sabotaged("gen-noop"))
			{
				const int n = tas_clip::restore(st.root, st.genName);
				if (n < 0) { why("tas_clip::restore(%s) failed", st.genName.c_str()); return false; }
				why("%d file(s) copied back", n);
			}
			else why("SABOTAGE gen-noop: restore skipped");
			// the live roll is the in-memory copy: re-attach the restored movie, then BASE
			const std::string flyr = flyrIn(st.root);
			if (flyr.empty()) { why("no .flyr on main after the restore"); return false; }
			dojo.replay.AttachFile(flyr);
			dojo.stale_tail_from = ~0u;
			dojo.savestate_epoch++;
			if (!intent::reloadBase()) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] { if (!intent::settled()) return false; return true; };
		add(s);
	}
	{
		Step s = runStep("gen intent: run main - the combo is back (peak 19, same hash)", EXPECT_PEAK);
		Step inner = s;
		s.verify = [inner] {
			if (!inner.verify()) return false;
			const u32 h = intent::machineHash();
			if (h != st.afterHash) { why("hash %08X != %08X after the first run", h, st.afterHash); return false; }
			why("peak 19, hash %08X == the pre-damage hash", h);
			return true;
		};
		add(s);
	}
	add(reloadStep("gen intent: reload BASE"));
	add(closeStep("gen intent: close states", KEY_STATES, "states"));
	add(endStep("gen intent: end segment (roll restored, BASE)"));

	// ===== 6. TEST LAB =====================================================================
	add(openStep("lab intent: open test lab (Alt+F1)", KEY_TESTLAB, "testlab"));
	add(beginStep("lab intent: begin segment (roll snapshot, BASE)"));
	{
		Step s;
		s.name = "lab intent: add a test from slot 0 (BASE is the fixture)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.act = [] {
			why("");
			std::vector<tas_clip::LabTest> before, after;
			const std::string game = gameOf(st.root);
			tas_clip::labTests(game, before);
			if (!hooks::labAddTest())
				return false;
			tas_clip::labTests(game, after);
			st.labDir.clear();
			for (const tas_clip::LabTest& t : after)
			{
				bool seen = false;
				for (const tas_clip::LabTest& b : before) if (b.dir == t.dir) seen = true;
				if (!seen) st.labDir = t.dir;
			}
			if (st.labDir.empty()) { why("new test not found in the library"); return false; }
			why("%s", ghc::filesystem::path(st.labDir).filename().string().c_str());
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "lab intent: bind the test - its BASE loads";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			if (!roll::branch::checkoutFolder(st.labDir, 0, "lab")) { why("checkoutFolder(test) refused"); return false; }
			st.labBound = true;
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			if (hostfs::savestateFolderOverride != st.labDir) { why("head is %s", hostfs::savestateFolderOverride.c_str()); return false; }
			if (dojo.frame_number.load() != intent::baseFrame()) { why("test BASE at frame %u, not %u", dojo.frame_number.load(), intent::baseFrame()); return false; }
			why("test BASE at frame %u, hash %08X", dojo.frame_number.load(), intent::machineHash());
			return true;
		};
		add(s);
	}
	add(placeStep("lab intent: place the combo on the test's roll"));
	add(runStep("lab intent: run the test - the combo lands on its BASE (peak 19)", EXPECT_PEAK));
	// results.jsonl / peakP1: UNMEASURED - this tree's lab is BASE-only by design (lab_panel.cpp:
	// "A test created here is BASE-only; record its sequence afterwards"): no roll->macro
	// writer, no lab runner. Not a step - a step that only SKIPs is decorative (G7).
	{
		Step s;
		s.name = "lab intent: back to main";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.optional = true;
		s.act = [] {
			why("");
			if (!st.labBound) { why("test was not bound"); return false; }
			if (!roll::branch::checkoutFolder(st.root, 0, "main")) { why("checkoutFolder(root) refused"); return false; }
			st.labBound = false;
			return true;
		};
		s.verify = [] { if (!intent::settled()) return false; if (hostfs::savestateFolderOverride != st.root) { why("head is %s", hostfs::savestateFolderOverride.c_str()); return false; } return true; };
		add(s);
	}
	{
		Step s;
		s.name = "lab intent: trash the tour test";
		s.optional = true;
		s.act = [] { why(""); return hooks::labTrashTest(); };
		add(s);
	}
	add(closeStep("lab intent: close test lab", KEY_TESTLAB, "testlab"));
	add(endStep("lab intent: end segment (roll restored, BASE)"));

	// ===== 9. CAPTURES =====================================================================
	add(openStep("capture intent: open captures (Ctrl+F7)", KEY_CAPTURES, "captures"));
	add(beginStep("capture intent: begin segment (roll snapshot, BASE)"));
	{
		Step s;
		s.name = "capture intent: start the recorder";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			if (dojo.frame_number.load() != intent::baseFrame()) { why("not on BASE (frame %u)", dojo.frame_number.load()); return false; }
			const std::string stamp = roll::captures::clipStamp(hostfs::savestateFolderOverride);
			if (stamp.empty()) { why("no clip stamp"); return false; }
			if (videorec::isRecording() || videorec::startPending()) { why("recorder busy"); return false; }
			const std::string dir = roll::captures::captureDir(stamp);
			std::error_code ec;
			ghc::filesystem::create_directories(dir, ec);
			st.capPath = dir + "/intent.avi";
			// ARM capture-dup: keep every present, including the paused duplicates - the
			// pcsx2-rr bug dojo:CapturePausedFrames=no exists to prevent. The count below
			// must then exceed the frames emulated.
			st.capDupArmed = sabotaged("capture-dup");
			if (st.capDupArmed)
			{
				st.capWas = cfgLoadStr("dojo", "CapturePausedFrames", "");
				cfgSetVirtual("dojo", "CapturePausedFrames", "yes");
			}
			st.capStopRequested = false;
			videorec::requestStart(st.capPath);
			// The recorder opens on the renderer's NEXT presented frame: run a few so it is
			// open BEFORE the baseline is taken (`[MEASURED 2026-09-18]` 1546 written for 1553
			// emulated when the baseline was the request frame - the first presents are gone).
			gui_step_frames(8);
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			if (!videorec::isRecording()) { why("recorder did not open (frame %u)", dojo.frame_number.load()); return false; }
			st.capFrom = dojo.frame_number.load();
			st.capWrittenAtFrom = videorec::framesWritten();
			why("recording from frame %u (%llu frame(s) already written)", st.capFrom, (unsigned long long)st.capWrittenAtFrom);
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "capture intent: record the combo (every emulated frame once)";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 120000;
		s.act = [] {
			why("");
			if (!videorec::isRecording()) { why("not recording"); return false; }
			if (!intent::runToStop()) { why("%s", intent::lastWhy()); return false; }
			// Real time, not fast-forward: the recorder captures PRESENTED frames, and
			// fast-forward is allowed to skip presents. ~26 s for the window.
			settings.input.fastForwardMode = false;
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			why("ran %u -> %u, recorder %s", st.capFrom, dojo.frame_number.load(), videorec::isRecording() ? "recording" : "not recording");
			return true;
		};
		add(s);
	}
	{
		Step s;
		s.name = "capture intent: stop - frames written == frames emulated, 0 paused duplicates";
		s.kind = Kind::Record;
		s.needsPrev = true;
		s.maxWaitMs = 20000;
		s.act = [] {
			why("");
			if (!(videorec::isRecording() || videorec::startPending())) { why("was not recording"); return false; }
			// The emulated count is pinned HERE: the stop is applied on the next present, and the
			// frames stepped to reach that present are not the recording's (`[MEASURED 2026-09-18]`
			// counting them read 1540 written for 1545 emulated - the 5 stepped below).
			st.capTo = dojo.frame_number.load();
			videorec::requestStop();
			gui_step_frames(5);		// one more present so the stop request is applied
			st.capStopRequested = true;
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			if (videorec::isRecording()) return false;
			std::error_code ec;
			const uintmax_t bytes = exists(st.capPath) ? (uintmax_t)ghc::filesystem::file_size(st.capPath, ec) : 0;
			if (bytes == 0) { why("no bytes at %s", st.capPath.c_str()); return false; }
			const u64 written = videorec::framesWritten() - st.capWrittenAtFrom;
			const u64 dups = videorec::pausedDuplicatesSkipped();
			const u32 emulated = st.capTo - st.capFrom;
			if (st.capDupArmed)
			{
				cfgSetVirtual("dojo", "CapturePausedFrames", st.capWas);
				st.capDupArmed = false;
			}
			// The stop lands on the NEXT present, which may write one more frame than the pinned
			// count: exactly-once means written in [emulated, emulated + 1].
			const bool ok = written >= emulated && written <= (u64)emulated + 1;
			why("%llu frames written for %u emulated (%llu paused duplicates skipped), %llu bytes",
					(unsigned long long)written, emulated, (unsigned long long)dups, (unsigned long long)bytes);
			return ok;
		};
		add(s);
	}
	add(closeStep("capture intent: close captures", KEY_CAPTURES, "captures"));

	// ===== END: the roll restored, BASE ======================================================
	{
		Step s;
		s.name = "clip intent: end (restore the roll, BASE)";
		s.kind = Kind::Record;
		s.maxWaitMs = 15000;
		s.act = [] {
			why("");
			if (st.labBound) { roll::branch::checkoutFolder(st.root, 0, "main"); st.labBound = false; }
			if (!intent::end()) { why("%s", intent::lastWhy()); return false; }
			return true;
		};
		s.verify = [] {
			if (!intent::settled()) return false;
			const u32 h = intent::machineHash();
			if (h != intent::baseHash()) { why("hash %08X != BASE %08X", h, intent::baseHash()); return false; }
			why("BASE hash %08X, roll restored", h);
			return true;
		};
		add(s);
	}
}

const ArmSpec ARMS[] = {
	{ "branch-leak", "branch intent: run main again - the combo still lands (peak 19)", "branch intent: create a branch from slot 0" },
	{ "gen-noop",    "gen intent: run main - the combo is back (peak 19, same hash)",     "gen intent: Shift+F8 archives a generation" },
	{ "capture-dup", "capture intent: stop - frames written == frames emulated, 0 paused duplicates", "capture intent: start the recorder" },
};

// machine: 0 unchanged, 1 moved, 2 any; movie: 0 unchanged, 1 moved, 2 any.
const ExpectDecl EXPECTS[] = {
	{ "branch intent: begin",                 1, 0, true,  "mover/converge" },
	{ "branch intent: open",                  0, 0, false, "ui" },
	{ "branch intent: place",                 0, 1, false, "movie-mover" },
	{ "branch intent: run",                   1, 0, true,  "mover/converge" },	// every combo run from BASE lands on the RECIPE's after-hash - a run that did not would be the finding
	{ "branch intent: reload BASE",           1, 0, true,  "mover/converge" },
	{ "branch intent: create",                0, 0, false, "ui" },
	{ "branch intent: checkout",              2, 2, false, "any" },		// the branch's slot 0 is main's copy; under the arm nothing moves
	{ "branch intent: clear",                 0, 1, false, "movie-mover" },
	{ "branch intent: back to main",          1, 2, true,  "mover/converge" },
	{ "branch intent: root .flyr",            0, 0, false, "ui" },
	{ "branch intent: delete",                0, 0, false, "ui" },
	{ "branch intent: close",                 0, 0, false, "ui" },
	{ "branch intent: end",                   2, 2, false, "any" },		// BASE may already be loaded; the roll may already be the original
	{ "gen intent: open",                     0, 0, false, "ui" },
	{ "gen intent: begin",                    2, 2, false, "any" },
	{ "gen intent: place",                    0, 1, false, "movie-mover" },
	{ "gen intent: end",                      2, 2, false, "any" },
	{ "gen intent: Shift+F8",                 0, 0, false, "ui" },
	{ "gen intent: damage",                   0, 1, false, "movie-mover" },
	{ "gen intent: run",                      1, 0, true,  "mover/converge" },
	{ "gen intent: reload BASE",              1, 0, true,  "mover/converge" },
	{ "gen intent: restore",                  2, 2, false, "any" },		// re-attach + BASE reload; movie back to the archived bytes (arm: unchanged)
	{ "gen intent: close",                    0, 0, false, "ui" },
	{ "lab intent: open",                     0, 0, false, "ui" },
	{ "lab intent: begin",                    2, 2, false, "any" },
	{ "lab intent: end",                      2, 2, false, "any" },
	{ "lab intent: add",                      0, 0, false, "ui" },
	{ "lab intent: bind",                     2, 2, false, "any" },		// a clip switch: the roll is the test's (empty), BASE loads
	{ "lab intent: place",                    0, 1, false, "movie-mover" },
	{ "lab intent: run",                      1, 0, true,  "mover/converge" },
	{ "lab intent: back to main",             1, 2, true,  "mover/converge" },
	{ "lab intent: trash",                    0, 0, false, "ui" },
	{ "lab intent: close",                    0, 0, false, "ui" },
	{ "capture intent: open",                 0, 0, false, "ui" },
	{ "capture intent: begin",                2, 2, false, "any" },
	{ "capture intent: start",                1, 0, false, "mover" },		// steps 8 frames so the encoder opens
	// `[MEASURED 2026-09-18]` G5b: the recording run is the module's only run that starts
	// from the recorder's own start offset (9936) with fast-forward OFF - its end machine
	// 76A1511D is unique by construction. A plain mover, not a convergence.
	{ "capture intent: record",               1, 0, false, "mover" },
	{ "capture intent: stop",                 1, 0, false, "mover" },		// the stop's own present steps the machine
	{ "capture intent: close",                0, 0, false, "ui" },
	// `[MEASURED 2026-09-18]` movie=Moved here read VACUOUS on a green run: the capture
	// segment before this end never edits the roll (it records the placed combo, the
	// restore then finds the original already in place), so the movie honestly does
	// not move. The machine DOES (the capture run -> BASE), and converges on load slot 0.
	{ "clip intent: end",                     1, 2, true,  "mover/converge" },
};

const Module MOD = { "clip", addSteps, ARMS, (int)(sizeof(ARMS) / sizeof(ARMS[0])), EXPECTS, (int)(sizeof(EXPECTS) / sizeof(EXPECTS[0])) };
const bool REGISTERED = (registerModule(&MOD), true);

}	// namespace

}	// namespace surfacetour
}	// namespace roll
