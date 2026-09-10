#include "roll_profile.h"
#include "roll_host.h"
#include "roll_select.h"
#include "roll_edit.h"
#include "roll_paint.h"
#include "roll_marks.h"
#include "roll_notation.h"
#include "roll_staged.h"
#include "roll_pattern.h"
#include "session.h"
#include "rend/gui.h"
#include "movie.h"
#include "dojo.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "input/gamepad.h"
#include "imgui.h"
#include <cstring>
#include <cmath>
#include <string>
#include "log/LogManager.h"
#include "cfg/cfg.h"
#include "oslib/oslib.h"

/*
	A PIANO ROLL: frames down, inputs across.

	The first vertical slice of the studio port, and deliberately a THIN one -
	it reads the movie and draws it, and edits nothing. The point is to prove
	the two seams carry a real panel before 3,832 lines of David's roll are
	brought over onto them.

	IT NAMES NO GAME AND NO EMULATOR. Columns come from roll::profile(), so the
	labels are MvC2's today and another game's tomorrow without touching this
	file. Savestate questions go through roll::host(), so this draws the same
	whether a slot is a file, a snapshot in memory, or something else entirely.
	The only concessions are reading dojo's frame record and the DC trigger
	bits, which is where a host adapter will eventually sit.

	`[MEASURED 2026-09-09]` docs/PIANO-ROLL-LIFT.md: of 161 real dependencies in
	David's show_piano_roll(), 4 are the emulator and 4 are the game. This file
	uses exactly those two seams and nothing else from either.
*/
namespace roll
{
static bool panelOpen = false;

// How many frames either side of the playhead to draw. Small on purpose: a
// window that scrolls to a selection is edit-tool work, and there are no edit
// tools yet.
static constexpr int SPAN = 24;

/*
	THE HOVER FLAGS A DRAG NEEDS - and a correction, kept because the wrong
	answer is instructive.

	`[CORRECTED 2026-09-09]` this briefly also carried
	ImGuiHoveredFlags_AllowWhenOverlappedByItem, on the theory that the last test
	in ImGui::IsItemHovered() was capping a drag at the row next to its anchor:

	    if ((g.LastItemData.InFlags & ImGuiItemFlags_AllowOverlap) && id != 0)
	        if ((flags & ImGuiHoveredFlags_AllowWhenOverlappedByItem) == 0)
	            if (g.HoveredIdPreviousFrame != g.LastItemData.ID)
	                return false;

	The reading of ImGui was right and the diagnosis was wrong: the flag changed
	the measurement not at all, because the mouse was never moving. What it DID
	change was to turn hover into a raw rectangle test, after which TWO adjacent
	rows claimed the same point every frame and the stroke covered two rows from
	a stationary cursor - which then passed a "the stroke spanned more than one
	row" assertion for entirely the wrong reason.

	The real cause was the harness's environment, not this file: scripts/
	rolltest.sh records that no pointer motion is delivered while a button is
	held under Xvfb + i3, measured on both axes through both XTest and
	XWarpPointer. One flag, and the extend fires for exactly one row per frame.
*/
static constexpr ImGuiHoveredFlags DRAG_HOVER = ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;

//! One player's half of a session_inputs row, or nothing if the row is absent
//! or too short. A hole in the movie is a legitimate state, not an error.
static bool frameAt(u32 frame, int player, FrameInputs& out)
{
	auto it = dojo.session_inputs.find(frame);
	if (it == dojo.session_inputs.end())
		return false;
	const size_t need = sizeof(FrameInputs) * (size_t)(player + 1);
	if (it->second.size() < need)
		return false;
	memcpy(&out, it->second.data() + (size_t)player * sizeof(FrameInputs), sizeof(FrameInputs));
	return true;
}

static void draw()
{
	const Profile& prof = profile();
	Host *h = host();

	if (!movie::authored())
	{
		ImGui::TextDisabled("No movie is open.");
		return;
	}

	// BOOKMARKS FOLLOW THE CLIP. Loaded once per folder and saved whenever they
	// change - which is cheap (a handful of lines) and means the file is never
	// out of date with what is on screen. Compared by VALUE rather than by a
	// "loaded" flag, so opening a second clip in one session reloads.
	{
		static std::string lastClip = "\x01";	// a value no path can be
		if (lastClip != hostfs::savestateFolderOverride)
		{
			lastClip = hostfs::savestateFolderOverride;
			marksLoad();
		}
		static size_t lastMarks = (size_t)-1;
		static u32 lastMarkSig = ~0u;
		u32 sig = 0;
		for (const auto& kv : marks().all())
			sig = sig * 31u + kv.first + (u32)kv.second.size();
		if (marks().count() != lastMarks || sig != lastMarkSig)
		{
			lastMarks = marks().count();
			lastMarkSig = sig;
			marksSave();
		}
	}

	const u32 playhead = dojo.frame_number.load();
	ImGui::Text("%s   frame %u of %u", prof.name, playhead, movie::end());
	// `dojo:RollSelTrace=yes` - the selection as a line a harness can assert on.
	// Logged only when it CHANGES, so a driven test can watch it move rather
	// than polling, and a quiet log means nothing was selected.
	if (cfgLoadBool("dojo", "RollSelTrace", false))
	{
		static size_t lastN = (size_t)-1; static u32 lastLo = ~0u, lastHi = ~0u;
		const Selection& sl = selection();
		if (sl.count() != lastN || sl.lo() != lastLo || sl.hi() != lastHi)
		{
			lastN = sl.count(); lastLo = sl.lo(); lastHi = sl.hi();
			NOTICE_LOG(RENDERER, "ROLL SEL: n=%d lo=%u hi=%u", (int)lastN, lastLo, lastHi);
		}
	}
	// ALWAYS OCCUPIES ITS HEIGHT, drawn or not. A line that appears when you
	// select something moves everything below it - the table included - so the
	// row you were about to click is no longer under the cursor.
	//
	// `[MEASURED 2026-09-10]` that is not theoretical: scripts/rolltest.sh
	// measures where the rows are, selects two, and then found the table had
	// slid a line down, so the paint drag landed outside it and the harness
	// reported the stroke unwired. The fork learned the same thing in its
	// snapshots pane - "It always occupies its height, drawn or not, so picking
	// a snapshot never moves the bar below."
	if (selection().empty())
		ImGui::TextDisabled("no selection");
	else
		ImGui::Text("selected: %d rows, %u..%u", (int)selection().count(),
				selection().lo(), selection().hi());
	if (h == nullptr)
		// Said rather than papered over: with no host there are no savestate
		// markers, and a blank gutter would look like "no states exist".
		ImGui::TextDisabled("No host installed - savestate markers unavailable.");
	ImGui::Separator();

	// ---- INTEGRATION PROBE, dojo:RollEditProbe=yes -----------------------
	//
	// Runs ONCE against the loaded movie: build a transform, push it through the
	// funnel, check the movie changed, undo, check it came back. The unit tests
	// prove the transforms; only this proves the WIRING - and the panel registry
	// taught this tree an hour ago that a seam nothing calls hides its own
	// defects.
	//
	// Safe to run because testrun.sh copies the clip into a temp dir per test,
	// so the movie being edited is a throwaway. Off by default all the same.
	{
		static bool probed = false;
		if (!probed && movie::authored() && cfgLoadBool("dojo", "RollEditProbe", false))
		{
			probed = true;
			const u32 f = movie::end() > 4 ? movie::end() - 4 : 0;
			auto rowOf = [&](u32 fr) -> Row {
				auto it = dojo.session_inputs.find(fr);
				return it == dojo.session_inputs.end() ? Row() : it->second;
			};
			const Row before = rowOf(f);
			const size_t depth = dojo.undo_stack.size();

			const Column& c = profile().cols[0];		// any plain-bit column
			std::map<u32, Row> src; src[f] = before;
			std::map<u32, Row> whole;
			for (const auto& kv : dojo.session_inputs) whole[kv.first] = kv.second;
			Edit e = mergeIntoMovie(whole, setColumn(src, { f }, 0, c, !rowHas(before, 0, c)));
			const s64 first = dojo.ApplyEdit(e, "roll: probe");

			const Row after = rowOf(f);
			const bool changed = after != before;
			const bool grew    = dojo.undo_stack.size() > depth;
			NOTICE_LOG(RENDERER, "ROLL PROBE: apply frame=%u first=%lld changed=%s undo_depth %zu->%zu",
					f, (long long)first, changed ? "yes" : "NO", depth, dojo.undo_stack.size());

			const bool undone = dojo.ApplyUndo();
			const bool restored = rowOf(f) == before;
			NOTICE_LOG(RENDERER, "ROLL PROBE: undo=%s restored=%s  => %s",
					undone ? "yes" : "NO", restored ? "yes" : "NO",
					(changed && grew && undone && restored) ? "PASS" : "FAIL");
		}
	}

	marksProbe();		// dojo:RollMarkProbe - one-shot, reads the marks file back
	anchorProbe();		// dojo:RollAnchorProbe - one-shot, reads the real sidecars

	// ---- STROKE PROBE, dojo:RollPaintProbe=yes ---------------------------
	//
	// WHY THIS EXISTS WHEN scripts/rolltest.sh DRIVES A REAL DRAG.
	//
	// `[CORRECTED 2026-09-09]` it was written because the harness appeared
	// unable to drive a multi-row stroke at all, which turned out to be a defect
	// in core/sdl/sdl.cpp rather than a fact about the display server. The
	// harness drives a real eight-row drag now and asserts the span.
	//
	// It is kept, and not as a duplicate. It needs no window manager, no
	// synthetic input and no timing, so it distinguishes "the STROKE is broken"
	// from "the INPUT never arrived" - the two possibilities that cost a full
	// afternoon to tell apart, because they produce the same commit. It also
	// checks EVERY row of the span rather than the ends, which a drag cannot
	// easily assert. Real movie, real funnel, real undo stack; only ImGui hit
	// testing is absent, and that is exactly the half rolltest covers.
	{
		static bool strokeProbed = false;
		if (!strokeProbed && movie::authored() && cfgLoadBool("dojo", "RollPaintProbe", false))
		{
			strokeProbed = true;
			const u32 f = movie::end() > 8 ? movie::end() - 8 : 0;
			const u32 last = f + 4;
			const Column& c = profile().cols[0];
			std::map<u32, Row> whole;
			for (const auto& kv : dojo.session_inputs) whole[kv.first] = kv.second;

			auto rowOf = [&](u32 fr) -> Row {
				auto it = dojo.session_inputs.find(fr);
				return it == dojo.session_inputs.end() ? Row() : it->second;
			};
			std::map<u32, Row> before;
			for (u32 r = f; r <= last; r++) before[r] = rowOf(r);
			const bool wasOn = rowHas(before[f], 0, c);
			const size_t depth = dojo.undo_stack.size();

			// The GESTURE, not a hand-built map: begin/extendTo/build is exactly
			// what a mouse would drive, so a defect in the stroke's own state
			// machine is reachable from here.
			paint().begin(f, 0, 0, wasOn, /*forceErase*/ false, /*gap*/ 0);
			paint().extendTo(last);
			const bool spanned = paint().lo() == f && paint().hi() == last;
			Edit e = paint().build(whole);
			const s64 first = dojo.ApplyEdit(e, "roll: stroke probe");
			paint().end();

			// EVERY row in the span, not just the ends: a stroke that wrote only
			// its anchor and its last row would satisfy a first/last check.
			int set = 0;
			for (u32 r = f; r <= last; r++)
				if (rowHas(rowOf(r), 0, c) == !wasOn) set++;
			const bool grew = dojo.undo_stack.size() > depth;

			const bool undone = dojo.ApplyUndo();
			int restored = 0;
			for (u32 r = f; r <= last; r++)
				if (rowOf(r) == before[r]) restored++;

			const bool ok = spanned && first == (s64)f && set == 5 && grew
					&& undone && restored == 5;
			NOTICE_LOG(RENDERER, "ROLL PAINTPROBE: %u..%u spanned=%s first=%lld set=%d/5 "
					"undo=%s restored=%d/5  => %s",
					f, last, spanned ? "yes" : "NO", (long long)first, set,
					undone ? "yes" : "NO", restored, ok ? "PASS" : "FAIL");
		}
	}

	// THE WHOLE MOVIE AS A MAP. Both funnels want it - ApplyEdit refuses a map
	// that does not span the movie, ApplyEditResize reads absence as a deletion -
	// and the paint stroke needs it at RELEASE, which happens outside the
	// buttons' scope and often outside the table entirely.
	auto wholeMovie = [&]() {
		std::map<u32, Row> all;
		for (const auto& kv : dojo.session_inputs) all[kv.first] = kv.second;
		return all;
	};

	// ONE GATE, READ ONCE, used by the buttons AND the stroke. Two copies of a
	// permission rule is how a tool ends up with a button that refuses and a
	// drag that does not.
	//
	// `[CORRECTED 2026-09-09]` this asked `!session::readOnly()` and that was
	// WRONG in a way no unit test could see: `dojo:Replay=yes` sets play_match,
	// so mode() is Read, so EVERY edit tool was unreachable in the only mode
	// where a movie exists. The panel drew "edits need a writable session" and
	// looked like a working gate. Two facts settle it:
	//
	//   `session::readOnly()` had exactly ONE caller in the tree - this line.
	//   It answers "the movie drives the guest and the live pad is ignored",
	//   which is about INPUT ROUTING while emulating. The roll rewrites the
	//   TAPE while the machine is STOPPED. Different question.
	//
	//   The tree's own shipped edit customers - TextApply and ResizeProbe in
	//   replay.cpp - push ApplyEdit into a REPLAY with no such check. The
	//   funnel's actual contract is "a loaded movie is editable".
	//
	// The real hazard is A LIVE PEER, whose tape is shared and who desyncs if
	// this side rewrites it - and that is the one case replay.cpp's probes never
	// meet, because they run at load. Rewriting a paused replay is not a hazard;
	// it is the feature, and dojo.cpp calls it re-recording.
	//
	// livePeer(), NOT netplay(). `[MEASURED 2026-09-09]` netplay() answers TRUE
	// for a purely local replay of a clip recorded from a GGPO match, because
	// replay.cpp sets config::GGPOEnable when it loads one - so this gate would
	// refuse every edit on such a clip while telling the user the tape is
	// shared, with nothing on the other end. docs/SESSION-KINDS.md #9.
	const bool paused   = gui_state == GuiState::Paused;
	const bool writable = !session::netplay();
	const bool editable = paused && writable;
	static int paintGap = 0;		// 0 = every row, 1 = every 2nd, 2 = every 3rd
	static int rangeFactor = 2;		// stretch xN / compress /N, shared deliberately
	static char mashText[96] = "236LP";
	static bool mashMerge = false;
	static std::string mashErr;

	// ---- MASH PROBE, dojo:RollMashProbe=yes ------------------------------
	//
	// The panel's mash needs typed text, which the harness cannot supply, so the
	// wiring between the parser and the funnel gets an in-process customer -
	// parse, build a track, apply, check the movie says what the pattern said.
	{
		static bool mashProbed = false;
		if (!mashProbed && movie::authored() && cfgLoadBool("dojo", "RollMashProbe", false))
		{
			mashProbed = true;
			const u32 f = movie::end() > 24 ? movie::end() - 24 : 0;
			std::vector<Cell> cells;
			std::string err;
			const bool parsed = parsePattern("2 8 5", cells, err);	// down, up, neutral

			Pattern pat;
			pat.tracks.resize(1);
			for (Cell c : cells)
				pat.tracks[0].push_back(CellOp{ c, cellAll() });
			Edit e = applyPattern(wholeMovie(), f, f + 5, pat, 0);
			const s64 first = dojo.ApplyEdit(e, "roll: mash probe");

			auto lane = [&](u32 fr) {
				auto it = dojo.session_inputs.find(fr);
				return it == dojo.session_inputs.end() ? (Cell)0 : cellOf(it->second, 0);
			};
			// THE PATTERN TILES: down, up, neutral, down, up, neutral.
			const bool tiled = parsed && cells.size() == 3
					&& lane(f) == cells[0] && lane(f + 1) == cells[1] && lane(f + 2) == cells[2]
					&& lane(f + 3) == cells[0] && lane(f + 4) == cells[1];
			const bool undone = dojo.ApplyUndo();
			NOTICE_LOG(RENDERER, "ROLL MASHPROBE: parsed=%s cells=%d first=%lld tiled=%s undo=%s"
					"  => %s", parsed ? "yes" : "NO", (int)cells.size(), (long long)first,
					tiled ? "yes" : "NO", undone ? "yes" : "NO",
					(parsed && tiled && undone) ? "PASS" : "FAIL");
		}
	}

	// ---- EDITS -----------------------------------------------------------
	//
	// THE GATE IS TWO CONDITIONS AND BOTH ARE STATED, not one silently
	// disabled button. An edit needs the machine STILL (a running movie is
	// being driven frame by frame; rewriting under it is the desync this
	// project exists to avoid) and a session that MAY be written (read-only
	// playback is deliberately not editable).
	//
	// Every button below builds a transform and hands it to the funnel -
	// Dojo::ApplyEdit or ApplyEditResize - which persists, logs the timeline
	// event the dead-timeline guard needs, and captures undo. Nothing here
	// writes session_inputs itself.
	{
		Selection& sel = selection();
		const bool haveSel  = !sel.empty();

		if (!paused)        ImGui::TextDisabled("edits need the movie PAUSED");
		else if (!writable) ImGui::TextDisabled("edits are refused while a peer is connected - the tape is shared");
		else
		{
			// GOVERNS THE DRAG, NOT THE BUTTONS, and says so. A control sitting
			// among widgets it does not affect is worse than an unlabelled one.
			ImGui::SetNextItemWidth(96.f);
			ImGui::Combo("paint every", &paintGap, "row\0" "2nd row\0" "3rd row\0");
			ImGui::SameLine();

			// ALWAYS DRAWN, DISABLED WHEN THERE IS NOTHING TO ACT ON.
			//
			// `[MEASURED 2026-09-10]` these used to be replaced by a one-line
			// "select rows for the buttons", so SELECTING SOMETHING grew the
			// panel by several lines and slid the table down underneath the
			// cursor. scripts/rolltest.sh measured where the rows were, selected
			// two, and then painted outside the table - and reported that the
			// stroke was unwired. A layout that moves when you use it is a bug
			// on its own; being the second one in this file today is what makes
			// it worth a comment.
			//
			// It is also better to look at: a disabled button says what exists,
			// a vanished one says nothing.
			ImGui::BeginDisabled(!haveSel);
			{

			if (ImGui::Button("Blank"))
			{
				// MERGED: the funnel refuses a map that does not span the movie.
				Edit e = mergeIntoMovie(wholeMovie(), blankRows(sel.rows()));
				dojo.ApplyEdit(e, "roll: blank");
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete rows"))
			{
				Resize r = deleteRows(wholeMovie(), sel.rows());
				dojo.ApplyEditResize(r.edit, "roll: delete rows");
				// ONE CALL, AND EXACTLY ONE. The savestate anchors on disk, the
				// bookmarks and the selection all hold row indices and all
				// register as holders (roll_remap.h) - a caller that has to
				// remember three of them is the fork's five hand-called fixups
				// with a shorter list.
				//
				// The selection used to be CLEARED here, because one naming
				// deleted frames now names other frames entirely. True, and the
				// reason the remap exists: deleted rows drop out and survivors
				// move with their content.
				//
				// `[MEASURED 2026-09-10]` an sel.remap() survived beside this for
				// one build. A remap applied TWICE shifts twice - silently, and
				// only for whichever holder the caller happened to remember.
				remapAll(r.remap);
			}
			ImGui::SameLine();
			if (ImGui::Button("Insert blanks"))
			{
				Resize r = insertBlanks(wholeMovie(), sel.lo(), (u32)sel.count());
				dojo.ApplyEditResize(r.edit, "roll: insert blanks");
				remapAll(r.remap);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%d rows)", (int)sel.count());

			// ---- THE RANGE TOOLS ------------------------------------------
			//
			// These take sel.lo()..sel.hi() and NOT the selected set. "What does
			// a gapped selection mean for a stretch" has no good answer, and the
			// fork gives three different ones across its tools; asking for the
			// thing they can do is more honest than refusing a shape.
			//
			// Reverse is the exception and does take the set, because exchanging
			// exactly the named rows IS well defined however scattered they are.
			if (ImGui::Button("Reverse"))
			{
				Edit e = reverseRows(wholeMovie(), sel.rows());
				dojo.ApplyEdit(e, "roll: reverse");
			}
			ImGui::SameLine();
			if (ImGui::Button("Stretch"))
			{
				Resize r = stretchRows(wholeMovie(), sel.lo(), sel.hi(), (u32)rangeFactor);
				dojo.ApplyEditResize(r.edit, "roll: stretch");
				remapAll(r.remap);
			}
			ImGui::SameLine();
			if (ImGui::Button("Compress"))
			{
				Resize r = compressRows(wholeMovie(), sel.lo(), sel.hi(), (u32)rangeFactor);
				dojo.ApplyEditResize(r.edit, "roll: compress");
				remapAll(r.remap);
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.f);
			// ONE FACTOR FOR BOTH, which is what the fork does and is right:
			// stretch x2 then compress /2 is the round trip a user expects, and
			// two separate fields make that a coincidence rather than a rule.
			ImGui::InputInt("x / \xc3\xb7", &rangeFactor);
			rangeFactor = rangeFactor < 2 ? 2 : (rangeFactor > 16 ? 16 : rangeFactor);
			}
			ImGui::EndDisabled();
			// MASH AND BOOKMARKS LIVE OUTSIDE THE SELECTION GATE.
			//
			// Their BUTTONS need a selection and say so individually; the pattern
			// FIELD does not, and having it greyed out until you selected something
			// meant you could not type the pattern you were about to select rows
			// for. Marking a frame needs no selection at all - it takes the
			// playhead - so it has no business behind that gate either.
			// ---- MASH: A PATTERN, WRITTEN OUT ----------------------------
			//
			// The same applyPattern the paint stroke uses, with a longer track.
			// docs/ROLL-EDIT-MODEL.md §2: mash, fill, brush, stamp and paint are
			// ONE function once the payload has a name, and this is that claim
			// cashed in - there is no second loop here, only a different track.
			//
			// MERGE IS THE MASK, not a flag on the write: replacing masks every
			// bit the profile models, merging masks only the bits the pattern
			// carries. That is why an "overdub" cannot clear anything and a
			// "stamp" can.
			{
				ImGui::SetNextItemWidth(140.f);
				ImGui::InputText("##mash", mashText, sizeof(mashText));
				ImGui::SameLine();
				ImGui::BeginDisabled(!haveSel);
				if (ImGui::Button("Mash"))
				{
					std::vector<Cell> cells;
					mashErr.clear();
					if (!parsePattern(mashText, cells, mashErr))
					{
						// The parser names the token it choked on and the panel
						// shows it. A pattern that silently dropped a token
						// would write something nobody typed.
					}
					else if (cells.empty())
						mashErr = "nothing to write";
					else
					{
						Pattern pat;
						pat.tracks.resize(1);
						for (Cell c : cells)
							pat.tracks[0].push_back(CellOp{ c, mashMerge ? c : cellAll() });
						Edit e = applyPattern(wholeMovie(), sel.lo(), sel.hi(), pat, paintGap);
						dojo.ApplyEdit(e, "roll: mash");
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				// ARM THE SAME PATTERN AS A BRUSH, so a drag stamps it instead
				// of setting one column. Same gesture, same phase, same gap -
				// only the payload changes, which is the claim
				// docs/ROLL-EDIT-MODEL.md §2 makes about these being one tool.
				// Not gated on a selection: a brush is armed to be dragged.
				if (ImGui::Button(paint().armed() ? "Unbrush" : "Brush"))
				{
					if (paint().armed())
						paint().arm({}, false);
					else
					{
						std::vector<Cell> cells;
						mashErr.clear();
						if (parsePattern(mashText, cells, mashErr) && !cells.empty())
							paint().arm(cells, mashMerge);
					}
				}
				if (paint().armed())
				{
					ImGui::SameLine();
					ImGui::TextColored(TAS_FOCUS_RING, "brush armed");
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(!haveSel);
				if (ImGui::Button("Fill"))
				{
					// The SAME pattern over the selected ROWS rather than over
					// the range - a different thing only when the selection is
					// gapped, and that is exactly when a user wants it.
					std::vector<Cell> cells;
					mashErr.clear();
					if (parsePattern(mashText, cells, mashErr) && !cells.empty())
					{
						Pattern pat;
						pat.tracks.resize(1);
						for (Cell c : cells)
							pat.tracks[0].push_back(CellOp{ c, mashMerge ? c : cellAll() });
						Edit e = applyPatternToRows(wholeMovie(), sel.rows(), pat);
						dojo.ApplyEdit(e, "roll: fill");
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::Checkbox("merge", &mashMerge);
				if (!mashErr.empty())
				{
					ImGui::SameLine();
					ImGui::TextColored(TAS_P2_COL, "%s", mashErr.c_str());
				}
			}

			// ---- THE STAGED BUFFER ----------------------------------------
			//
			// A SECOND DOCUMENT with its own controls, deliberately. In the fork
			// the same button row edits whichever document is active - "if
			// (stagedActive) stagedTogOp(TXK_SWAP) else tasSwapSelPlayers(...)" -
			// so a button means two things depending on state elsewhere on
			// screen. These are its own buttons and they only ever touch it.
			//
			// The op queue is edited HERE and never by Ctrl+Z. The fork has two
			// undo systems on one key; this has one, and the queue has buttons.
			{
				Staged& st = staged();
				ImGui::BeginDisabled(!haveSel);
				if (ImGui::Button("Stage"))
					st.load(wholeMovie(), sel.rows());
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (st.empty())
					ImGui::TextDisabled("(nothing staged)");
				else
				{
					// BASELINE AND RESULT BOTH, because the difference is the
					// point: 6 -> 2 after a compress says the four are still
					// there to come back.
					ImGui::Text("%d frames (from %d), %d op%s",
							(int)st.size(), (int)st.baseSize(), (int)st.ops().size(),
							st.ops().size() == 1 ? "" : "s");
					ImGui::SameLine();
					if (ImGui::Button("Swap"))     st.push(Step{ Op::SwapLanes });
					ImGui::SameLine();
					if (ImGui::Button("Flip"))     st.push(Step{ Op::Flip });
					ImGui::SameLine();
					if (ImGui::Button("Rev"))      st.push(Step{ Op::Reverse });
					ImGui::SameLine();
					if (ImGui::Button("x N"))      st.push(Step{ Op::Stretch, rangeFactor });
					ImGui::SameLine();
					if (ImGui::Button("/ N"))      st.push(Step{ Op::Compress, rangeFactor });
					ImGui::SameLine();
					ImGui::BeginDisabled(st.ops().empty());
					// UNDO OP, not undo: it pops the recipe and replays from the
					// baseline, so a compress gives its frames back.
					if (ImGui::Button("Undo op"))  st.pop();
					ImGui::SameLine();
					if (ImGui::Button("Clear ops")) st.clearOps();
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::Button("Place"))
					{
						const u32 at = haveSel ? sel.lo() : playhead;
						Edit e = st.place(wholeMovie(), at, mashMerge);
						dojo.ApplyEdit(e, "roll: place staged");
					}
					ImGui::SameLine();
					if (ImGui::Button("Unload"))   st.unload();
				}
			}

			// ---- BOOKMARKS ------------------------------------------------
			//
			// Deliberately NOT gated on a selection: marking where you are is
			// something you do while looking at a frame, not after picking a
			// range. The target is the selection's first row when there is one
			// and the playhead otherwise, which is the same "target" rule the
			// fork applies to its place verbs - stated here once instead of at
			// five call sites.
			{
				const u32 target = haveSel ? sel.lo() : playhead;
				char mb[48];
				snprintf(mb, sizeof(mb), marks().has(target) ? "Unmark %u" : "Mark %u", target);
				if (ImGui::Button(mb))
					marks().toggle(target);
				ImGui::SameLine();
				u32 to = 0;
				ImGui::BeginDisabled(!marks().prev(target, to));
				if (ImGui::Button("<Mark") && marks().prev(target, to))
					sel.press(to, Mods{});		// select it; seeking is the playhead's job
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!marks().next(target, to));
				if (ImGui::Button("Mark>") && marks().next(target, to))
					sel.press(to, Mods{});
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextDisabled("(%d marks)", (int)marks().count());
			}

		}
	}
	ImGui::Separator();

	std::map<u32, std::vector<int>> slotsAt;
	if (h != nullptr)
		h->framesToSlots(slotsAt);

	const int cols = 2 + prof.count;	// frame, states, then the inputs
	if (!ImGui::BeginTable("##roll", cols,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
		return;

	ImGui::TableSetupScrollFreeze(2, 1);
	ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthFixed, 64.f);
	ImGui::TableSetupColumn("st", ImGuiTableColumnFlags_WidthFixed, 40.f);
	for (int c = 0; c < prof.count; c++)
		ImGui::TableSetupColumn(prof.cols[c].label, ImGuiTableColumnFlags_WidthFixed, 28.f);
	ImGui::TableHeadersRow();

	// WHICH COLUMN IS UNDER THE MOUSE, asked ONCE. The row is a single hit
	// target spanning the whole table, so the click alone cannot say whether
	// the user meant the frame gutter or a button cell - but the table knows.
	// Column hover does not vary down a row, so this is per draw, not per row.
	//
	// -1 is the frame or state gutter, or nothing: those SELECT. An input
	// column PAINTS. One button, and geometry decides which gesture it was -
	// exactly how the row already derives its frame from where the mouse is.
	int hoveredCol = -1;
	for (int c = 0; c < prof.count; c++)
		if (ImGui::TableGetColumnFlags(2 + c) & ImGuiTableColumnFlags_IsHovered)
		{
			hoveredCol = c;
			break;
		}

	// `dojo:RollPaintTrace=yes` - the hovered column, logged on CHANGE. Without
	// it a paint test that reports nothing cannot tell "the click missed the
	// table" from "the stroke never began", which are bugs in different files -
	// and the harness clicks at a fixed pixel offset, so which column it lands
	// in is a measurement, not something to assume.
	// Reported AFTER the row loop, because the row half is only known then.
	// `[MEASURED 2026-09-10]` the row belongs here as much as the column: a
	// harness with fixed pixel offsets for its rows broke the moment the panel
	// grew a second button row and the table moved down. Which row a point is
	// over is a measurement, exactly like which column.
	auto traceHover = [&](int col, bool anyRow, u32 row) {
		if (!cfgLoadBool("dojo", "RollPaintTrace", false))
			return;
		static int lastCol = -2;
		static s64 lastRow = -2;
		const s64 r = anyRow ? (s64)row : -1;
		if (col == lastCol && r == lastRow)
			return;
		lastCol = col; lastRow = r;
		NOTICE_LOG(RENDERER, "ROLL HOVER: col=%d(%s) row=%lld", col,
				col >= 0 ? profile().cols[col].label : "gutter", (long long)r);
	};

	// WHICH ROW IS UNDER THE CURSOR - decided ONCE, from geometry, exactly as
	// the column above is. A row is NOT allowed to answer for itself.
	//
	// `[MEASURED 2026-09-09]` because TWO ADJACENT ROWS ANSWER YES to the same
	// pixel. ImGui::Selectable deliberately inflates its hit box so selectables
	// tile with no click-gap - imgui_widgets.cpp, "Selectables are meant to be
	// tightly packed together with no click-gap, so we extend their box to cover
	// spacing between selectable":
	//
	//     const float spacing_U = IM_TRUNC(spacing_y * 0.50f);
	//     bb.Min.y -= spacing_U;
	//     bb.Max.y += (spacing_y - spacing_U);
	//
	// The inflation is ItemSpacing.y, which is the right amount between
	// selectables IN A WINDOW. A TABLE lays its rows out with CellPadding
	// instead, and flycast scales the whole style, so the two stop tiling:
	// IM_TRUNC rounds, the boxes overlap, and both rows contain the point.
	//
	// The symptom was that a stationary press painted TWO frames, and that one
	// press logged two "begin" lines in the same millisecond on consecutive
	// rows. `[CORRECTED 2026-09-09]` that was first blamed on a paused emulator
	// rendering faster than input is polled, so that one click edge was seen by
	// two frames. It was one frame and two rows.
	//
	// THE TIE-BREAK PREFERS AN EXACT HIT, then the nearest centre. Exact means
	// the cursor is inside the row's own rect rather than in the inflation, and
	// those cannot overlap. Falling back to nearest-centre rather than to
	// nothing matters for a drag: in the inflation between two rows there is no
	// exact hit, and a stroke that stalled there would drop frames the user
	// dragged across.
	bool  hoverAny = false, hoverExact = false;
	u32   hoverRow = 0;
	float hoverDist = 0.f;

	const u32 lo = playhead > (u32)SPAN ? playhead - SPAN : 0;
	const u32 hi = std::min(playhead + (u32)SPAN, movie::end());

	for (u32 f = lo; f < hi; f++)
	{
		ImGui::TableNextRow();
		if (selection().has(f))
			ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
					ImGui::GetColorU32(tasCol(TAS_P1_COL, 0.22f)));
		if (f == playhead)
			ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
					ImGui::GetColorU32(tasCol(TAS_FOCUS_RING, 0.16f)));

		ImGui::TableNextColumn();
		// THE ROW IS THE CLICK TARGET, spanning every column: selection is by
		// FRAME, so a hit region narrower than the row would make the model and
		// the gesture disagree about what was picked.
		Selection& sel = selection();
		ImGui::PushID((int)f);
		const bool wasSel = sel.has(f);
		char lbl[32];
		// A MARKED FRAME IS FLAGGED IN THE LABEL ITSELF rather than drawn beside
		// it: the label is the row's click target, so anything drawn separately
		// would be a second hit region for the same row.
		const char *mk = marks().has(f) ? "*" : " ";
		snprintf(lbl, sizeof(lbl), movie::has(f) ? "%s%u" : "%s(%u)", mk, f);
		// A frame with no record is a HOLE, not an end - movie.h exists to keep
		// that distinction, and the roll must show it rather than draw zeroes.
		if (!movie::has(f)) ImGui::PushStyleColor(ImGuiCol_Text,
				ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		ImGui::Selectable(lbl, wasSel,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap);
		if (!movie::has(f)) ImGui::PopStyleColor();

		// OFFERED, NOT ACTED ON. The winner is picked after the table, so a row
		// cannot start a gesture its neighbour is equally entitled to start.
		if (ImGui::IsItemHovered(DRAG_HOVER))
		{
			const ImGuiStyle& st = ImGui::GetStyle();
			const float padU = std::trunc(st.ItemSpacing.y * 0.5f);
			const float padD = st.ItemSpacing.y - padU;
			const float y0 = ImGui::GetItemRectMin().y;
			const float y1 = ImGui::GetItemRectMax().y;
			const float my = ImGui::GetIO().MousePos.y;
			const bool  exact = my >= y0 + padU && my < y1 - padD;
			const float dist  = std::fabs(my - (y0 + y1) * 0.5f);
			if (!hoverAny || (exact && !hoverExact)
					|| (exact == hoverExact && dist < hoverDist))
			{
				hoverAny = true; hoverExact = exact; hoverRow = f; hoverDist = dist;
			}
		}
		ImGui::PopID();

		ImGui::TableNextColumn();
		auto sit = slotsAt.find(f);
		if (sit != slotsAt.end() && !sit->second.empty())
		{
			const int slot = sit->second.front();
			const bool stale = h != nullptr && h->slotStale(slot);
			// Stale means the state no longer belongs to this timeline: still a
			// valid machine, no longer a point on THIS movie.
			if (stale) ImGui::TextColored(TAS_P2_COL, "%d!", slot);
			else       ImGui::Text("%d", slot);
		}

		FrameInputs fi{};
		const bool have = frameAt(f, 0, fi);
		const Paint& pt = paint();
		for (int c = 0; c < prof.count; c++)
		{
			ImGui::TableNextColumn();
			bool on = have && pressed(prof.cols[c], fi.kcode, fi.triggers.l,
					fi.triggers.r, BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT);
			// LIVE PREVIEW, through the SAME predicate the commit uses. touches()
			// is what build() consults, so the grid cannot promise a cell the
			// release will not write - and a gap row keeps the movie's own value
			// because the stroke SKIPS it rather than inverting it.
			//
			// Drawn even where the movie has no record: a stroke past a hole
			// creates the frame, and showing nothing there would hide that.
			const bool prev = pt.active() && c == pt.column() && pt.touches(f);
			if (prev)
				on = pt.writes();
			if (on)
				ImGui::TextColored(prev ? TAS_FOCUS_RING : TAS_P1_COL, "%s", "\xe2\x96\xa0");	// filled square
		}
	}
	ImGui::EndTable();

	traceHover(hoveredCol, hoverAny, hoverRow);

	// ---- THE GESTURE, on the ONE row that won -----------------------------
	//
	// Moved out of the row loop so there is exactly one decision per frame.
	// IsItemClicked is not reachable here, so the press edge is read globally -
	// which is all it ever was: IsItemClicked is "hovered AND clicked this
	// frame", and hoverAny already carries ImGui's hover verdict, including the
	// window test, the clip rect and the blocked-by-active-item rule.
	//
	// The selection highlight is therefore one frame behind a click. It was
	// already inconsistent - rows drawn AFTER the clicked one saw the new
	// selection and rows before it did not - so this trades half a frame of
	// disagreement for a whole frame of honest lag.
	if (hoverAny)
	{
		Selection& sel = selection();
		const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		// !active() KEEPS THE ANCHOR DECIDED ONCE. With one row per frame a
		// second begin should now be unreachable, but the stroke's own rule is
		// that the anchor is chosen once and this is where that is enforced.
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !paint().active())
		{
			// Modifiers read HERE and passed in: the grammar is a pure function
			// of them, which is what lets it be tested without a frame.
			ImGuiIO& io = ImGui::GetIO();
			if (editable && hoveredCol >= 0)
			{
				// SET vs ERASE IS DECIDED AT THE ANCHOR and never asked again,
				// so the cell's CURRENT state is read here, once. Deciding it
				// per row would make a drag flicker between writing and erasing
				// as it crossed existing input.
				FrameInputs a{};
				const bool wasOn = frameAt(hoverRow, 0, a)
						&& pressed(prof.cols[hoveredCol], a.kcode, a.triggers.l,
								a.triggers.r, BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT);
				paint().begin(hoverRow, 0, hoveredCol, wasOn, io.KeyAlt, paintGap);
				if (cfgLoadBool("dojo", "RollPaintTrace", false))
					NOTICE_LOG(RENDERER, "ROLL PAINT: begin frame=%u col=%d(%s) was=%s -> %s gap=%d exact=%s",
							hoverRow, hoveredCol, prof.cols[hoveredCol].label,
							wasOn ? "on" : "off", paint().writes() ? "set" : "clear",
							paintGap, hoverExact ? "yes" : "no");
			}
			else
				sel.press(hoverRow, Mods{ io.KeyShift, io.KeyCtrl, io.KeyAlt });
		}
		else if (paint().active() && down)
		{
			// COLUMN-LOCKED: only the ROW comes from the hover. However far
			// sideways the mouse wanders, the stroke stays in the column and the
			// port it began in.
			paint().extendTo(hoverRow);
			// Logged on CHANGE. "the stroke committed fewer rows than the mouse
			// crossed" has several causes - the far row was never hovered, the
			// table scrolled under the drag, the extend never ran - and they are
			// indistinguishable from the commit alone.
			if (cfgLoadBool("dojo", "RollPaintTrace", false))
			{
				static u32 lastExt = ~0u;
				if (hoverRow != lastExt)
				{
					lastExt = hoverRow;
					NOTICE_LOG(RENDERER, "ROLL PAINT: extend to %u (drawn window %u..%u)",
							hoverRow, lo, hi);
				}
			}
		}
		else if (sel.dragging() && down)
			sel.dragTo(hoverRow);
	}

	// The drag ends wherever the mouse is released, including outside the
	// table - a release the roll never sees would leave it dragging forever.
	if (selection().dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		selection().release();

	// A STROKE COMMITS ON RELEASE, wherever the mouse is - outside the table,
	// outside the window - for the same reason the selection drag does: a
	// release the roll never sees would leave it painting forever.
	if (paint().active() && cfgLoadBool("dojo", "RollPaintTrace", false))
	{
		// ONE LINE PER UI FRAME while a stroke is live. Three different failures
		// look identical from the commit alone - the UI is not redrawing, the
		// pointer is not where xdotool put it, or no row reports hovered - and
		// this separates them: a gap in the timestamps is the first, a static
		// mouse= is the second, row=none is the third.
		const ImGuiIO& io = ImGui::GetIO();
		NOTICE_LOG(RENDERER, "ROLL DRAG: mouse=%.0f,%.0f down=%d stroke=%u..%u",
				io.MousePos.x, io.MousePos.y, (int)ImGui::IsMouseDown(ImGuiMouseButton_Left),
				paint().lo(), paint().hi());
	}
	if (paint().active())
	{
		if (!editable)
			// The gate was lost MID-STROKE: the movie resumed, or the session
			// became read-only. Drop the stroke rather than commit it. The user
			// authored it under a rule that no longer holds, and an edit landing
			// on a running movie is the desync this project exists to avoid.
			paint().end();
		else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			// ONE EDIT FOR THE WHOLE STROKE, not one per row - so one undo step
			// walks the gesture back the way the user made it.
			Edit e = paint().build(wholeMovie());
			const s64 first = dojo.ApplyEdit(e, "roll: paint");
			if (cfgLoadBool("dojo", "RollPaintTrace", false))
				NOTICE_LOG(RENDERER, "ROLL PAINT: commit col=%d %s %u..%u gap=%d first=%lld rows=%zu",
						paint().column(), paint().writes() ? "set" : "clear",
						paint().lo(), paint().hi(), paintGap, (long long)first, e.size());
			paint().end();
		}
	}

	// ONE-SHOT DECODE CHECK. An all-empty grid is what a neutral stretch looks
	// like AND what a broken pressed() looks like; they are not distinguishable
	// by eye. Counts the presses actually decoded over the drawn span, once.
	// `dojo:RollDecodeTrace=yes` - debug-by-instrumentation, the house method.
	// Kept rather than deleted because an empty grid is what a neutral stretch
	// AND a broken decode both look like, and only this told them apart:
	// `[MEASURED 2026-09-09]` 591 of 11520 frames carry input, per column
	// ^:0 v:32 <:8 >:14 LP:0 HP:0 LK:421 HK:0 A1:0 A2:0 ST:116 - LK and ST
	// dominating because DC_BTN_A/START are what menus use, and this profile
	// calls DC_BTN_A "LK".
	//
	// Gated past the seek as well: the first draw is at frame 0, where no input
	// exists yet and "0 presses" says nothing.
	static bool counted = false;
	if (!counted && hi > lo && playhead > 10000
			&& cfgLoadBool("dojo", "RollDecodeTrace", false))
	{
		counted = true;
		int cells = 0, frames = 0;
		for (u32 f = lo; f < hi; f++)
		{
			FrameInputs t{};
			if (!frameAt(f, 0, t)) continue;
			frames++;
			for (int c = 0; c < prof.count; c++)
				if (pressed(prof.cols[c], t.kcode, t.triggers.l, t.triggers.r,
						BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT))
					cells++;
		}
		// Scan the WHOLE movie for the first frame carrying any input, for
		// either player. "48 idle frames" and "the decode is wrong" look
		// identical from one window; this distinguishes them.
		u32 firstAt = 0; u32 firstK = 0; int who = -1; int rowBytes = -1;
		for (const auto& kv : dojo.session_inputs)
		{
			if (rowBytes < 0) rowBytes = (int)kv.second.size();
			FrameInputs a{}, b{};
			const bool ha = frameAt(kv.first, 0, a), hb = frameAt(kv.first, 1, b);
			if (ha && a.kcode != 0) { firstAt = kv.first; firstK = a.kcode; who = 0; break; }
			if (hb && b.kcode != 0) { firstAt = kv.first; firstK = b.kcode; who = 1; break; }
		}
		// END TO END: does pressed() actually map kcode bits onto COLUMNS?
		// A correct frameAt with a wrong bit table still shows an empty grid.
		int active = 0, byCol[16] = {0};
		for (const auto& kv : dojo.session_inputs)
		{
			FrameInputs t{};
			if (!frameAt(kv.first, 0, t)) continue;
			bool any = false;
			for (int c = 0; c < prof.count && c < 16; c++)
				if (pressed(prof.cols[c], t.kcode, t.triggers.l, t.triggers.r,
						BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT)) { byCol[c]++; any = true; }
			if (any) active++;
		}
		std::string hist;
		for (int c = 0; c < prof.count && c < 16; c++)
			hist += std::string(c ? " " : "") + prof.cols[c].label + ":" + std::to_string(byCol[c]);
		NOTICE_LOG(RENDERER, "ROLL DECODE: sizeof=%d row=%d; first input p%d frame %u = 0x%05X; "
				"%d of %u frames have input; per column %s",
				(int)sizeof(FrameInputs), rowBytes, who, firstAt, firstK,
				active, (u32)dojo.session_inputs.size(), hist.c_str());
	}
}

void registerPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "pianoroll", "Piano Roll", &panelOpen, draw,
			panels::Both, /*persist*/ true });
	// A LOOKUP THAT CAN FAIL, rather than trusting the add. A panel that failed
	// to register is invisible in exactly the same way as one that is merely
	// closed, and the registry is the only thing that knows the difference.
	NOTICE_LOG(RENDERER, "ROLL PANEL: registered=%s open=%s cfg=%s",
			panels::find("pianoroll") != nullptr ? "yes" : "NO",
			panelOpen ? "yes" : "no",
			cfgLoadBool("dojo", "Panel.pianoroll", false) ? "yes" : "no");
}

}	// namespace roll
