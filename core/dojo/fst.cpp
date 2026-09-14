#include "fst.h"
#include "dojo.h"
#include "mvc2.h"
#include "tas_ruler.h"
#include "tas_clip.h"
#include "tas_colors.h"
#include "roll_host.h"
#include "roll_select.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <algorithm>
#include <fstream>
#include <string>

/*
	See fst.h for what the Frame Skip Test is and why the model lives in the
	header. This file is the model's implementation and the arm that proves it.
*/
namespace roll {
namespace fst {

void normalize(Sweep& s)
{
	if (s.k1 < s.k0)
		std::swap(s.k0, s.k1);
	if (s.fk1 < s.fk0)
		std::swap(s.fk0, s.fk1);
	if (s.selHi < s.selLo)
		std::swap(s.selLo, s.selHi);
	// The combo row must sit INSIDE the selection: P == selLo is the skip-phase
	// test, a row within is the gap test, and a row outside is neither.
	if (s.P < s.selLo || s.P > s.selHi)
		s.P = s.selLo;
	// The pin may sit BEFORE the combo row - pin the phase at row 100 and sweep
	// the combo at 200 - but never after the selection starts, or it would
	// shift rows the sweep has already accounted for.
	if (s.fkRow > s.selLo)
		s.fkRow = s.selLo;
}

bool bakeEdit(const Movie& original, const Sweep& s, int n, Movie& out)
{
	if (n < 0 || n >= s.Ntot())
		return false;

	const int a = s.aOf(n), b = s.bOf(n);
	struct Ins { u32 pos; int cnt; };
	std::vector<Ins> ins;
	if (a > 0)
		ins.push_back({ s.P, a });
	if (s.fkOn && b > 0)
	{
		// TWO INSERTS ON ONE ROW ARE ONE RUN. Applied separately they would
		// each shift the other's start, and the second run would land a frames
		// further down than the row the user pinned.
		if (a > 0 && s.fkRow == s.P)
			ins[0].cnt += b;
		else
			ins.push_back({ s.fkRow, b });
	}

	out.clear();
	// Existing rows move down by every insert at or before them.
	for (const auto& kv : original)
	{
		u32 shift = 0;
		for (const auto& x : ins)
			if (x.pos <= kv.first)
				shift += (u32)x.cnt;
		out[kv.first + shift] = kv.second;
	}
	// Then the blank runs, each at its own shifted start.
	for (const auto& x : ins)
	{
		u32 start = x.pos;
		for (const auto& y : ins)
			if (y.pos < x.pos)
				start += (u32)y.cnt;
		for (int q = 0; q < x.cnt; q++)
			out[start + (u32)q].assign(sizeof(FrameInputs) * 2, 0);
	}
	return true;
}

// ---------------------------------------------------------------------------------------

static Movie rowsOf(std::initializer_list<u32> rows, u8 tag)
{
	Movie m;
	for (u32 r : rows)
		m[r] = std::vector<u8>(sizeof(FrameInputs) * 2, (u8)(tag + r));
	return m;
}

static bool isBlank(const Movie& m, u32 row)
{
	const auto it = m.find(row);
	if (it == m.end() || it->second.size() != sizeof(FrameInputs) * 2)
		return false;
	for (u8 v : it->second)
		if (v != 0)
			return false;
	return true;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "FST SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- the sweep's shape ----------------------------------------------------------
	{
		Sweep s;
		s.k0 = 0; s.k1 = 3; s.fkOn = false;
		claim("one axis sweeps k0..k1 inclusive", s.Na() == 4 && s.Nb() == 1 && s.Ntot() == 4);
		claim("a single-value range is one variant, not zero",
				[&]{ Sweep t; t.k0 = 2; t.k1 = 2; return t.Ntot() == 1; }());
		s.fkOn = true; s.fk0 = 0; s.fk1 = 3;
		claim("two axes multiply", s.Na() == 4 && s.Nb() == 4 && s.Ntot() == 16);
		// THE FLATTENING, and the direction matters - the runner saves an
		// outcome state per n, so a swapped inner/outer interleaves them.
		claim("the COMBO axis is the inner one",
				s.aOf(0) == 0 && s.aOf(1) == 1 && s.aOf(3) == 3 && s.aOf(4) == 0);
		claim("...and the FRAME SKIP axis is the outer one",
				s.bOf(0) == 0 && s.bOf(3) == 0 && s.bOf(4) == 1 && s.bOf(15) == 3);
		claim("with the second axis off, b is always 0",
				[&]{ Sweep t; t.k0 = 0; t.k1 = 3; t.fkOn = false;
				     return t.bOf(0) == 0 && t.bOf(3) == 0; }());
	}

	// ---- where the selection lands --------------------------------------------------
	{
		Sweep s;
		s.selLo = 100; s.selHi = 110; s.P = 100; s.k0 = 0; s.k1 = 3; s.fkOn = false;
		claim("k=0 leaves the selection where it was",
				s.seqRowOf(0) == 100 && s.endRowOf(0) == 110);
		// AT-OR-BEFORE. P == selLo is the commonest case in the whole tool and
		// is exactly the one a `<` instead of `<=` gets wrong.
		claim("an insert AT the selection's first row pushes it down",
				s.seqRowOf(2) == 102 && s.endRowOf(2) == 112);
		{
			Sweep g = s;
			g.P = 105;	// the gap test: insert INSIDE the selection
			claim("an insert inside the selection moves the END but not the START",
					g.seqRowOf(2) == 100 && g.endRowOf(2) == 112);
		}
		{
			Sweep p = s;
			p.fkOn = true; p.fk0 = 1; p.fk1 = 1; p.fkRow = 50;	// pinned well before
			claim("a pin before the selection shifts both ends",
					p.seqRowOf(0) == 101 && p.endRowOf(0) == 111);
		}
		{
			Sweep after = s;
			after.P = 200;	// deliberately outside, un-normalised
			claim("an insert AFTER the selection shifts neither end",
					after.seqRowOf(2) == 100 && after.endRowOf(2) == 110);
		}
	}

	// ---- normalisation ---------------------------------------------------------------
	{
		Sweep s;
		s.k0 = 5; s.k1 = 1;
		s.selLo = 20; s.selHi = 10;
		s.P = 999; s.fkRow = 999;
		normalize(s);
		claim("a reversed range is put the right way round", s.k0 == 1 && s.k1 == 5);
		claim("a reversed selection is put the right way round",
				s.selLo == 10 && s.selHi == 20);
		claim("a combo row outside the selection falls back to its first row", s.P == 10);
		claim("a pin after the selection start is pulled back to it", s.fkRow == 10);
		{
			Sweep in;
			in.selLo = 10; in.selHi = 20; in.P = 15; in.fkRow = 5;
			normalize(in);
			claim("...but a legal combo row and an earlier pin are left alone",
					in.P == 15 && in.fkRow == 5);
		}
	}

	// ---- baking a variant -------------------------------------------------------------
	{
		const Movie orig = rowsOf({ 0, 5, 10, 11, 20 }, 1);
		Sweep s;
		s.selLo = 10; s.selHi = 11; s.P = 10; s.k0 = 0; s.k1 = 3; s.fkOn = false;
		Movie out;

		claim("an out-of-range variant bakes nothing", !bakeEdit(orig, s, 99, out));
		claim("k=0 is the original, unchanged",
				bakeEdit(orig, s, 0, out) && out == orig);

		// n=2 -> a=2: two blanks at row 10, everything from 10 up moves by 2.
		claim("a variant bakes", bakeEdit(orig, s, 2, out));
		claim("rows before the insert do not move",
				out.count(0) == 1 && out.count(5) == 1 && out.at(5) == orig.at(5));
		claim("rows at or after the insert move down by k",
				out.count(12) == 1 && out.at(12) == orig.at(10)
				&& out.count(13) == 1 && out.at(13) == orig.at(11)
				&& out.count(22) == 1 && out.at(22) == orig.at(20));
		claim("the inserted rows are BLANK", isBlank(out, 10) && isBlank(out, 11));
		// THE CONTROL for the two above. A bake that blanked everything, or one
		// that moved nothing, satisfies half of this on its own.
		claim("...and nothing else was blanked",
				!isBlank(out, 5) && !isBlank(out, 12) && !isBlank(out, 22));
		claim("the row count grows by exactly k",
				out.size() == orig.size() + 2);
		claim("EVERY variant is built from the ORIGINAL, never the last one",
				bakeEdit(orig, s, 3, out) && out.size() == orig.size() + 3
				&& out.count(13) == 1 && out.at(13) == orig.at(10));
	}

	// ---- two inserts ------------------------------------------------------------------
	{
		const Movie orig = rowsOf({ 0, 50, 100, 101 }, 1);
		Sweep s;
		s.selLo = 100; s.selHi = 101; s.P = 100;
		s.k0 = 2; s.k1 = 2;
		s.fkOn = true; s.fk0 = 3; s.fk1 = 3; s.fkRow = 50;
		Movie out;
		claim("two inserts at different rows both apply", bakeEdit(orig, s, 0, out));
		claim("...the earlier one shifts the later one's landing row",
				out.count(105) == 1 && out.at(105) == orig.at(100));
		claim("...each run is blank at its own row",
				isBlank(out, 50) && isBlank(out, 52) && isBlank(out, 103) && isBlank(out, 104));
		claim("...and the total grows by both counts",
				out.size() == orig.size() + 5);
	}
	{
		// THE MERGE. Both inserts on one row must produce ONE run of a+b, not
		// two runs applied in sequence - which would put the second b frames
		// further down than the row that was pinned.
		const Movie orig = rowsOf({ 0, 100, 101 }, 1);
		Sweep s;
		s.selLo = 100; s.selHi = 101; s.P = 100;
		s.k0 = 2; s.k1 = 2;
		s.fkOn = true; s.fk0 = 3; s.fk1 = 3; s.fkRow = 100;	// SAME row as P
		Movie out;
		claim("two inserts on the SAME row merge into one run", bakeEdit(orig, s, 0, out));
		claim("...of exactly a+b blanks",
				isBlank(out, 100) && isBlank(out, 101) && isBlank(out, 102)
				&& isBlank(out, 103) && isBlank(out, 104) && !isBlank(out, 105));
		claim("...with the tail shifted once, by a+b",
				out.count(105) == 1 && out.at(105) == orig.at(100)
				&& out.size() == orig.size() + 5);
	}

	NOTICE_LOG(RENDERER, "FST SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace fst
}	// namespace roll

// =======================================================================================
// THE RUNNER, and the panel.
//
// The model above is pure and tested. Everything below touches the emulator - savestates,
// the edit funnel, the pause arbiter - and is driven by a state machine rather than a loop,
// because each phase has to hand control back and let frames actually happen.
// =======================================================================================

namespace roll {
namespace fst {

/*
	What one variant's run reached. The combo meters are polled on the emulator
	loop and peak-tracked since the bake, so this is "the best it got", not a
	sample at the stop frame - a combo that lands and drops before the settle
	window ends still counts.
*/
struct Result
{
	bool ran = false;
	u16 peak1 = 0, peak2 = 0;
	u32 endFrame = 0;
	int outcomeSlot = -1;		//!< the savestate saved for eyeball review
	bool haveSkip = false;
	u8 skipSkip = 0, skipCount = 0, skipRate = 0;
	int a = 0, b = 0;
	u32 seqRow = 0;
};

static struct State
{
	Sweep sweep;
	Movie original;				//!< the roll before any variant
	std::map<int, Result> result;
	std::string why;			//!< why the last refusal refused
	int baseSlot = -1;
	u32 baseFrame = 0;
	int cur = -1;				//!< the baked variant (-1 = the original is in the roll)
	int settle = 60;			//!< frames to keep running past the last row before judging
	int runN = 0;
	int runPhase = 0;			//!< 0 bake, 1 running, 2 snap, 3 restore + results
	u32 stopFrame = 0;
	double startedAt = 0;
	bool haveSel = false;
	bool generated = false;
	bool running = false;
	bool fastForward = false;
	bool panelOpen = false;
} st;

static std::string varDesc(int n)
{
	char b[56];
	if (st.sweep.fkOn)
		snprintf(b, sizeof(b), "combo +%d, frame-skip +%d", st.sweep.aOf(n), st.sweep.bOf(n));
	else
		snprintf(b, sizeof(b), "%d blank%s", st.sweep.aOf(n), st.sweep.aOf(n) == 1 ? "" : "s");
	return b;
}

/*
	SLOT QUESTIONS GO THROUGH roll::host(), not through hostfs.

	`[CORRECTED from the fork]` his runner calls hostfs::scanSavestateInfo() and
	gui_slot_stale() directly. core/dojo/roll_host.h exists precisely to stop
	that: "a roll that calls gui_* cannot be given a different machine, and
	cannot be tested without one". A slot is an index here, and whether it is a
	file is the host's business.

	The one thing the interface has no verb for is "write a state into slot n",
	so saving still goes through config::SavestateSlot + gui_saveState() below.
	That is a real gap in Host and is called out rather than papered over.
*/
static bool slotExists(int slot)
{
	SlotView v;
	return slot >= 0 && host() != nullptr && host()->slotView(slot, v) && v.exists;
}

static u32 slotFrameOf(int slot)
{
	SlotView v;
	if (slot < 0 || host() == nullptr || !host()->slotView(slot, v) || !v.exists)
		return 0;
	return v.frame;
}

//! Take the live Piano Roll selection into the test.
bool captureSelection()
{
	if (selection().empty())
	{
		st.why = "Select rows in the Piano Roll first";
		return false;
	}
	st.sweep.selLo = selection().lo();
	st.sweep.selHi = selection().hi();
	st.sweep.P = st.sweep.selLo;
	st.sweep.fkRow = st.sweep.selLo;
	normalize(st.sweep);
	st.haveSel = true;
	st.baseSlot = (int)config::SavestateSlot;
	st.baseFrame = slotFrameOf(st.baseSlot);
	st.generated = false;
	st.cur = -1;
	st.why.clear();
	st.panelOpen = true;
	panels::open("frameskiptest");
	NOTICE_LOG(NETWORK, "TAS FST: captured rows %u..%u, base slot %d @ %u",
			st.sweep.selLo, st.sweep.selHi, st.baseSlot, st.baseFrame);
	return true;
}

//! Snapshot the ORIGINAL roll and arm the sweep. Paused only.
static bool generate()
{
	if (st.running) { st.why = "A run is in progress"; return false; }
	if (!st.haveSel) { st.why = "Capture a Piano Roll selection first"; return false; }
	if (st.cur >= 0) { st.why = "Restore the current variant first"; return false; }
	if (gui_state != GuiState::Paused) { st.why = "Pause first"; return false; }
	if (dojo.play_match) { st.why = "READ - press R to author"; return false; }
	normalize(st.sweep);
	st.baseSlot = (int)config::SavestateSlot;
	st.baseFrame = slotFrameOf(st.baseSlot);
	if (!slotExists(st.baseSlot))
	{
		st.why = "Select a saved state slot first (F2 / States), then Generate";
		return false;
	}
	if (st.sweep.Ntot() > 99)
	{
		st.why = "Too many variants (combo x frame-skip > 99) - narrow a range";
		return false;
	}
	st.original = dojo.session_inputs;
	st.result.clear();
	st.generated = true;
	st.cur = -1;
	st.why.clear();
	NOTICE_LOG(NETWORK, "TAS FST: armed %d variant(s) - combo +%d..+%d @row %u, "
			"frame-skip %s +%d..+%d @row %u, base slot %d @ %u",
			st.sweep.Ntot(), st.sweep.k0, st.sweep.k1, st.sweep.P,
			st.sweep.fkOn ? "ON" : "off", st.sweep.fk0, st.sweep.fk1, st.sweep.fkRow,
			st.baseSlot, st.baseFrame);
	return true;
}

//! Reload the base, then write variant n built from the ORIGINAL.
static bool bake(int n)
{
	if (!st.generated) { st.why = "Generate first"; return false; }
	if (gui_state != GuiState::Paused) { st.why = "Pause first"; return false; }
	if (dojo.play_match) { st.why = "READ - press R to author"; return false; }
	if (n < 0 || n >= st.sweep.Ntot()) return false;

	// The base slot is reloaded around the USER's slot, which is restored after
	// - a sweep must not silently move where F1 would save.
	const int userSlot = (int)config::SavestateSlot;
	config::SavestateSlot.set(st.baseSlot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(st.baseSlot));
	gui_loadState();
	config::SavestateSlot.set(userSlot);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(userSlot));
	if (gui_state != GuiState::Paused) { st.why = "The base state did not load"; return false; }

	Movie edited;
	if (!bakeEdit(st.original, st.sweep, n, edited))
		return false;

	const s64 first = dojo.ApplyEditResize(edited, "frame skip test");
	if (first < 0 && edited != dojo.session_inputs)
	{
		st.why = "The edit funnel refused the variant (locked rows at or above an insert row?)";
		return false;
	}
	dojo.stale_tail_from = ~0u;
	dojo.macro_armed = true;
	st.cur = n;
	tas_mvc2::comboPeakReset();

	Result r;
	r.a = st.sweep.aOf(n);
	r.b = st.sweep.bOf(n);
	r.seqRow = st.sweep.seqRowOf(n);
	st.result[n] = r;
	st.why.clear();
	NOTICE_LOG(NETWORK, "TAS FST: baked n=%d (%s) seq@%u (first changed row %lld)",
			n, varDesc(n).c_str(), r.seqRow, (long long)first);
	return true;
}

static bool restore()
{
	if (!st.generated || st.cur < 0) { st.why = "Nothing to restore"; return false; }
	if (gui_state != GuiState::Paused) { st.why = "Pause first"; return false; }
	dojo.ApplyEditResize(st.original, "frame skip test restore");
	dojo.stale_tail_from = ~0u;
	st.cur = -1;
	st.why.clear();
	gui_display_notification("Frame Skip Test: original roll restored", 2500);
	NOTICE_LOG(NETWORK, "TAS FST: original restored (%u rows)", (u32)st.original.size());
	return true;
}

static void runAbort(const char *why)
{
	settings.input.fastForwardMode = false;
	if (gui_state == GuiState::Closed)
		gui_open_pause();
	st.running = false;
	st.runPhase = 0;
	char m[176];
	snprintf(m, sizeof(m), "Frame Skip Test run stopped: %s", why);
	gui_display_notification(m, 3500);
	NOTICE_LOG(NETWORK, "TAS FST RUN: stopped - %s", why);
}

static bool runStart()
{
	if (!st.generated) { st.why = "Generate first"; return false; }
	if (gui_state != GuiState::Paused) { st.why = "Pause first"; return false; }
	if (dojo.play_match) { st.why = "READ - press R to author"; return false; }
	if (st.baseSlot < 0) { st.why = "No base state"; return false; }
	if (st.settle < 0) st.settle = 0;
	st.running = true;
	st.runN = 0;
	st.runPhase = 0;
	st.startedAt = os_GetSeconds();
	st.why.clear();
	NOTICE_LOG(NETWORK, "TAS FST RUN: start %d variant(s), settle %d, base slot %d @ %u",
			st.sweep.Ntot(), st.settle, st.baseSlot, st.baseFrame);
	return true;
}

/*
	Write the sweep and its outcomes beside the clip's savestates.

	BOTH A SNAPSHOT AND A LOG. results.json is the latest run, results.jsonl
	appends - so a second sweep over the same selection does not erase the first
	one's numbers, which is the whole point of running a sweep twice.
*/
static void writeResults()
{
	if (hostfs::savestateFolderOverride.empty())
		return;
	nlohmann::json j;
	j["schema"] = 2;
	j["createdUtc"] = tas_clip::utcNowIso();
	j["base"] = { { "slot", st.baseSlot }, { "frame", st.baseFrame } };
	j["combo"] = { { "row", st.sweep.P }, { "from", st.sweep.k0 }, { "to", st.sweep.k1 } };
	j["frameSkip"] = { { "on", st.sweep.fkOn }, { "row", st.sweep.fkRow },
			{ "from", st.sweep.fk0 }, { "to", st.sweep.fk1 } };
	j["selection"] = { st.sweep.selLo, st.sweep.selHi };
	j["settle"] = st.settle;
	nlohmann::json runs = nlohmann::json::array();
	for (int n = 0; n < st.sweep.Ntot(); n++)
	{
		const auto it = st.result.find(n);
		if (it == st.result.end() || !it->second.ran)
			continue;
		const Result& r = it->second;
		nlohmann::json run;
		run["combo"] = r.a;
		run["frameSkip"] = r.b;
		run["seqRow"] = r.seqRow;
		run["endFrame"] = r.endFrame;
		run["peakP1"] = r.peak1;
		run["peakP2"] = r.peak2;
		run["outcomeSlot"] = r.outcomeSlot;
		if (r.haveSkip)
			run["skipAtStart"] = { { "skip", r.skipSkip }, { "count", r.skipCount },
					{ "rate", r.skipRate } };
		runs.push_back(run);
	}
	j["runs"] = runs;
	const std::string dir = hostfs::savestateFolderOverride;
	{
		std::ofstream out(dir + "/results.json", std::ios::binary | std::ios::trunc);
		if (out.good()) out << j.dump(2);
	}
	{
		std::ofstream log(dir + "/results.jsonl", std::ios::binary | std::ios::app);
		if (log.good()) log << j.dump() << "\n";
	}
	NOTICE_LOG(NETWORK, "TAS FST RUN: results -> %s/results.json (%u runs)",
			dir.c_str(), (u32)runs.size());
}

void tick()
{
	if (!st.running)
		return;
	const double now = os_GetSeconds();
	switch (st.runPhase)
	{
	case 0:		// bake the next variant and set it running to its stop frame
		if (gui_state != GuiState::Paused)
		{
			if (now - st.startedAt > 5.0)
				runAbort("not paused");
			return;
		}
		if (!bake(st.runN))
		{
			runAbort(st.why.empty() ? "the bake failed" : st.why.c_str());
			return;
		}
		st.stopFrame = st.sweep.endRowOf(st.runN) + 1 + (u32)st.settle;
		{
			const u32 fr = dojo.frame_number.load();
			gui_step_frames(st.stopFrame > fr ? (int)(st.stopFrame - fr) : 1);
		}
		settings.input.fastForwardMode = st.fastForward;
		st.runPhase = 1;
		st.startedAt = now;
		return;

	case 1:		// wait for it to reach the stop frame
		if (gui_state == GuiState::Paused)
		{
			if (dojo.frame_number.load() >= st.stopFrame)
				st.runPhase = 2;
			else if (now - st.startedAt > 1.0)
				runAbort("the run did not start (Training off?)");
		}
		else if (now - st.startedAt > 120.0)
			runAbort("timeout");
		return;

	case 2:		// record what it reached, and save an outcome state to look at
	{
		settings.input.fastForwardMode = false;
		Result& r = st.result[st.runN];
		r.ran = true;
		r.endFrame = dojo.frame_number.load();
		r.peak1 = std::max(r.peak1, tas_mvc2::comboPeak(0));
		r.peak2 = std::max(r.peak2, tas_mvc2::comboPeak(1));
		{
			// WHICH SKIP PHASE THE SEQUENCE ACTUALLY STARTED IN. Without this
			// the table says a variant worked and cannot say why.
			std::vector<tas_ruler::SkipSample> sm;
			tas_ruler::snapshot(r.seqRow, r.seqRow + 1, sm);
			if (!sm.empty() && sm[0].seen)
			{
				r.haveSkip = true;
				r.skipSkip = sm[0].skip;
				r.skipCount = sm[0].count;
				r.skipRate = sm[0].rate;
			}
		}
		const int slot = std::min(st.runN + 1, 99);
		const int userSlot = (int)config::SavestateSlot;
		config::SavestateSlot.set(slot);
		cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(slot));
		gui_saveState();
		{
			char lbl[72];
			snprintf(lbl, sizeof(lbl), "%s  P1 %u / P2 %u  @%u", varDesc(st.runN).c_str(),
					(unsigned)r.peak1, (unsigned)r.peak2, r.endFrame);
			if (host() != nullptr)
				host()->setSlotLabel(slot, lbl);
		}
		config::SavestateSlot.set(userSlot);
		cfgSetVirtual("config", "Dreamcast.SavestateSlot", std::to_string(userSlot));
		r.outcomeSlot = slot;
		NOTICE_LOG(NETWORK, "TAS FST RUN: n=%d (%s) done @%u peak P1 %u P2 %u -> slot %d",
				st.runN, varDesc(st.runN).c_str(), r.endFrame,
				(unsigned)r.peak1, (unsigned)r.peak2, slot);
		st.runN++;
		st.runPhase = st.runN >= st.sweep.Ntot() ? 3 : 0;
		st.startedAt = now;
		return;
	}

	case 3:		// put the original back and write the numbers out
		if (gui_state != GuiState::Paused)
			return;
		restore();
		writeResults();
		st.running = false;
		st.runPhase = 0;
		dojo.savestate_epoch++;
		{
			int ran = 0, bestN = -1;
			u16 best = 0;
			for (const auto& kv : st.result)
				if (kv.second.ran)
				{
					ran++;
					if (bestN < 0 || kv.second.peak1 > best)
					{
						bestN = kv.first;
						best = kv.second.peak1;
					}
				}
			char combo[80] = "";
			if (best > 0 && bestN >= 0)
				snprintf(combo, sizeof(combo), " - best P1 combo %u at %s",
						(unsigned)best, varDesc(bestN).c_str());
			char m[224];
			snprintf(m, sizeof(m), "Frame Skip Test: %d variant%s run%s. Outcomes in slots "
					"1-%d for review; results.json written",
					ran, ran == 1 ? "" : "s", combo, std::min(st.sweep.Ntot(), 99));
			gui_display_notification(m, 6000);
		}
		return;
	}
}

// ---------------------------------------------------------------------------------------
// The panel. No Begin/End - core/rend/panel.h owns the window.
// ---------------------------------------------------------------------------------------

static void drawPanel()
{
	Sweep& s = st.sweep;

	if (!st.haveSel)
	{
		// SAID, not shown as an empty form. A window full of disabled spinners
		// does not tell you the one thing you need to do next.
		ImGui::TextDisabled("No selection captured.");
		ImGui::TextWrapped("Select rows in the Piano Roll, then press Capture.");
		if (ImGui::Button("Capture selection"))
			captureSelection();
		if (!st.why.empty())
			ImGui::TextColored(TAS_WRITE, "%s", st.why.c_str());
		return;
	}

	ImGui::TextColored(TAS_ACCENT, "rows %u..%u", s.selLo, s.selHi);
	ImGui::SameLine();
	if (ImGui::SmallButton("Re-capture"))
		captureSelection();

	ImGui::BeginDisabled(st.running);
	int p = (int)s.P;
	if (ImGui::DragInt("combo row", &p, 1.f, (int)s.selLo, (int)s.selHi))
	{
		s.P = (u32)p;
		normalize(s);
	}
	ImGui::DragIntRange2("blanks", &s.k0, &s.k1, 1.f, 0, 30);
	ImGui::Checkbox("sweep frame skip too", &s.fkOn);
	if (s.fkOn)
	{
		int fr = (int)s.fkRow;
		if (ImGui::DragInt("pin row", &fr, 1.f, 0, (int)s.selLo))
		{
			s.fkRow = (u32)fr;
			normalize(s);
		}
		ImGui::DragIntRange2("pin blanks", &s.fk0, &s.fk1, 1.f, 0, 30);
	}
	ImGui::DragInt("settle", &st.settle, 1.f, 0, 600);
	ImGui::Checkbox("fast-forward the run", &st.fastForward);
	ImGui::EndDisabled();

	/*
		THE VARIANT COUNT IS SHOWN BEFORE THE BUTTON, not discovered by pressing
		it. A 4 x 4 sweep with a 60-frame settle is sixteen reloads and several
		thousand emulated frames, and the fork only tells you the number after
		it refuses at 99.
	*/
	ImGui::Separator();
	ImGui::TextColored(TAS_ACTIVE_COL, "%d variant%s", s.Ntot(), s.Ntot() == 1 ? "" : "s");
	ImGui::SameLine();
	ImGui::TextDisabled("base slot %d @ %u", st.baseSlot, st.baseFrame);

	ImGui::BeginDisabled(st.running || gui_state != GuiState::Paused);
	if (ImGui::Button(st.generated ? "Re-generate" : "Generate"))
		generate();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!st.generated || st.running);
	if (ImGui::Button("Run all"))
		runStart();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(st.cur < 0 || st.running);
	if (ImGui::Button("Restore"))
		restore();
	ImGui::EndDisabled();

	if (st.running)
	{
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
			runAbort("stopped by hand");
		ImGui::TextColored(TAS_STAGED, "running variant %d / %d  (phase %d)",
				st.runN + 1, s.Ntot(), st.runPhase);
	}
	else if (gui_state != GuiState::Paused)
		ImGui::TextDisabled("Pause to generate or run.");

	if (!st.why.empty())
		ImGui::TextColored(TAS_WRITE, "%s", st.why.c_str());

	// ---- results -------------------------------------------------------------------
	if (st.result.empty())
		return;
	ImGui::Separator();
	if (!ImGui::BeginTable("##fstres", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("variant");
	ImGui::TableSetupColumn("P1");
	ImGui::TableSetupColumn("P2");
	ImGui::TableSetupColumn("row");
	ImGui::TableSetupColumn("skip");
	ImGui::TableHeadersRow();
	// THE BEST ONE IS MARKED, because a column of numbers is not an answer -
	// the question this tool is asked is "which timing worked".
	u16 best = 0;
	for (const auto& kv : st.result)
		if (kv.second.ran)
			best = std::max(best, kv.second.peak1);
	for (const auto& kv : st.result)
	{
		const Result& r = kv.second;
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		const bool win = r.ran && best > 0 && r.peak1 == best;
		ImGui::TextColored(win ? TAS_ACTIVE_COL : (kv.first == st.cur ? TAS_ACCENT : TAS_TEXT),
				"%s", varDesc(kv.first).c_str());
		ImGui::TableSetColumnIndex(1);
		if (r.ran) ImGui::TextColored(win ? TAS_ACTIVE_COL : TAS_TEXT, "%u", (unsigned)r.peak1);
		else ImGui::TextDisabled("-");
		ImGui::TableSetColumnIndex(2);
		if (r.ran) ImGui::Text("%u", (unsigned)r.peak2); else ImGui::TextDisabled("-");
		ImGui::TableSetColumnIndex(3);
		ImGui::TextDisabled("%u", r.seqRow);
		ImGui::TableSetColumnIndex(4);
		if (r.haveSkip)
			ImGui::TextDisabled("%u/%u", (unsigned)r.skipCount, (unsigned)r.skipRate);
		else
			// NOT BLANK. "the ruler never saw this frame" and "it saw it and the
			// rate was 0" are different, and an empty cell gives neither.
			ImGui::TextDisabled("?");
	}
	ImGui::EndTable();
}

}	// namespace fst

bool frameSkipTestRunning() { return fst::st.running; }

void registerFrameSkipTestPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	/*
		MENU ONLY, not Both. The Input Visualizer is a thing you watch while the
		game runs; this is a thing you operate while PAUSED - every action it
		offers refuses unless gui_state is Paused. Drawing it over live gameplay
		would be a panel that can only ever say "Pause to generate or run."
	*/
	panels::add({ "frameskiptest", "Frame Skip Test", &fst::st.panelOpen, fst::drawPanel,
			panels::Menu, /*persist*/ true, /*defW*/ 420.f, /*defH*/ 380.f });
	NOTICE_LOG(RENDERER, "FST PANEL: registered=%s open=%s",
			panels::find("frameskiptest") != nullptr ? "yes" : "NO",
			fst::st.panelOpen ? "yes" : "no");
}

}	// namespace roll
