#include "roll_edit.h"
#include "roll_pattern.h"
#include "dojo.h"
#include "input/gamepad.h"
#include "tasmacro.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstring>

namespace roll
{

size_t rowBytes() { return sizeof(FrameInputs) * 2; }

Row blankRow() { return Row(rowBytes(), 0); }

//! The two spellings a trigger has in this movie format: a byte >= 0x20, or the
//! GGPO-era kcode bit. Reading accepts both; WRITING sets both, so a row this
//! code produces reads as pressed under either convention.
static void readFrame(const Row& r, int player, FrameInputs& out)
{
	memset(&out, 0, sizeof out);
	const size_t off = (size_t)player * sizeof(FrameInputs);
	if (r.size() >= off + sizeof(FrameInputs))
		memcpy(&out, r.data() + off, sizeof(FrameInputs));
}

static void writeFrame(Row& r, int player, const FrameInputs& in)
{
	const size_t off = (size_t)player * sizeof(FrameInputs);
	if (r.size() < off + sizeof(FrameInputs))
		r.resize(off + sizeof(FrameInputs), 0);
	memcpy(r.data() + off, &in, sizeof(FrameInputs));
}

bool rowHas(const Row& r, int player, const Column& c)
{
	FrameInputs f{};
	readFrame(r, player, f);
	return pressed(c, f.kcode, f.triggers.l, f.triggers.r,
			BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT);
}

//! One column into one decoded frame. Factored out because the cell codec
//! applies every column of the profile in a loop, and a second copy of the
//! trigger rule below is exactly the kind of drift this port exists to avoid.
static void writeColumn(FrameInputs& f, const Column& c, bool on)
{
	if (c.trigger < 0)
	{
		if (on) f.kcode |= c.bit;
		else    f.kcode &= ~c.bit;
		return;
	}
	const u32 tbit = c.trigger == 0 ? BTN_TRIGGER_LEFT : BTN_TRIGGER_RIGHT;
	u8& b = c.trigger == 0 ? f.triggers.l : f.triggers.r;
	// BOTH spellings, so the row reads as pressed whichever one a reader
	// checks. Setting only the byte would leave a kcode-reading path seeing
	// an unpressed trigger - which is how half a recording goes missing.
	b = on ? 0xFF : 0x00;
	if (on) f.kcode |= tbit;
	else    f.kcode &= ~tbit;
}

Row rowWith(const Row& r, int player, const Column& c, bool on)
{
	Row out = r;
	if (out.size() < rowBytes())
		out.resize(rowBytes(), 0);
	FrameInputs f{};
	readFrame(out, player, f);
	writeColumn(f, c, on);
	writeFrame(out, player, f);
	return out;
}

int laneCount()
{
	const size_t n = rowBytes() / sizeof(FrameInputs);
	return n < 1 ? 1 : (int)n;
}

Cell cellOf(const Row& r, int lane)
{
	FrameInputs f{};
	readFrame(r, lane, f);
	const Profile& p = profile();
	Cell c = 0;
	for (int i = 0; i < p.count; i++)
		if (pressed(p.cols[i], f.kcode, f.triggers.l, f.triggers.r,
				BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT))
			c |= p.cols[i].canon;
	return c;
}

Row cellInto(const Row& r, int lane, Cell c)
{
	Row out = r;
	if (out.size() < rowBytes())
		out.resize(rowBytes(), 0);
	FrameInputs f{};
	readFrame(out, lane, f);
	// STARTS FROM THE EXISTING FRAME and rewrites only modelled columns, so
	// unmodelled bits survive. Rebuilding from zero would be simpler and would
	// silently drop analog axes.
	const Profile& p = profile();
	for (int i = 0; i < p.count; i++)
		writeColumn(f, p.cols[i], (c & p.cols[i].canon) != 0);
	writeFrame(out, lane, f);
	return out;
}

Edit blankRows(const std::set<u32>& rows)
{
	Edit e;
	for (u32 r : rows)
		e[r] = blankRow();
	return e;
}

Edit setColumn(const std::map<u32, Row>& src, const std::set<u32>& rows,
		int player, const Column& c, bool on)
{
	Edit e;
	for (u32 r : rows)
	{
		auto it = src.find(r);
		// A selected frame with no record still takes the edit: the user asked
		// for this column here, and a hole is not a refusal.
		e[r] = rowWith(it == src.end() ? blankRow() : it->second, player, c, on);
	}
	return e;
}

Edit paintColumn(const std::map<u32, Row>& all, u32 anchor, u32 to,
		int player, const Column& c, bool on, int gap)
{
	// DELEGATED, not reimplemented. `[MEASURED 2026-09-09]` this loop and the
	// fork's tasMashPlace2, tasFillRowsWithMacro and brush stroke are one
	// operation written four times - a periodic payload over a row range at a
	// phase - and the four copies had already drifted (docs/ROLL-EDIT-MODEL.md).
	// A single column is that operation with a one-step pattern.
	//
	// The mask is the column and nothing else, which is what makes this a
	// SET/CLEAR of one input rather than a replace of the whole cell: every
	// other button in the lane is outside the mask and survives.
	return applyPattern(all, anchor, to,
			Pattern::one(player, on ? (Cell)c.canon : 0, (Cell)c.canon), gap);
}

Edit mergeIntoMovie(const std::map<u32, Row>& all, const Edit& e)
{
	Edit out(all.begin(), all.end());
	for (const auto& kv : e)
		out[kv.first] = kv.second;		// the edit wins where it names a frame
	return out;
}

Edit deleteRows(const std::map<u32, Row>& all, const std::set<u32>& rows)
{
	Edit e;
	u32 dst = 0;
	for (const auto& kv : all)
	{
		if (rows.count(kv.first) != 0)
			continue;			// dropped; the tail closes over it
		e[dst++] = kv.second;
	}
	return e;
}

Edit insertBlanks(const std::map<u32, Row>& all, u32 at, u32 count)
{
	Edit e;
	if (count == 0)
		return Edit(all.begin(), all.end());
	for (const auto& kv : all)
		e[kv.first < at ? kv.first : kv.first + count] = kv.second;
	for (u32 i = 0; i < count; i++)
		e[at + i] = blankRow();
	return e;
}

/*
	SELF-TEST. These transforms are arithmetic over a map, so they can be run on
	synthetic rows with no emulator and no movie - which is the whole reason for
	splitting them out of the funnel.

	The claims worth having are the ones about IDENTITY: after a resize, which
	frame is which. A transform that keeps the right CONTENT at the wrong INDEX
	passes any "did it change" check and silently desynchronises every savestate
	anchored past the edit.
*/
void editSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLEDIT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& prof = profile();
	const Column *up = nullptr, *a1 = nullptr;
	for (int i = 0; i < prof.count; i++)
	{
		if (prof.cols[i].canon == tas_macro::CANON_UP) up = &prof.cols[i];
		if (prof.cols[i].canon == tas_macro::CANON_A1) a1 = &prof.cols[i];
	}
	claim("the profile offers a plain-bit and a trigger column", up != nullptr && a1 != nullptr);
	if (up == nullptr || a1 == nullptr) { NOTICE_LOG(RENDERER, "ROLLEDIT SELFTEST: %d passed, %d failed", pass, fail); return; }

	claim("a blank row is the movie's row size", blankRow().size() == rowBytes());
	claim("...and holds nothing pressed", !rowHas(blankRow(), 0, *up));

	// A plain bit round-trips, and only for the player it was written for.
	Row r = rowWith(blankRow(), 0, *up, true);
	claim("a bit column sets", rowHas(r, 0, *up));
	claim("...for that player only", !rowHas(r, 1, *up));
	claim("...and clears again", !rowHas(rowWith(r, 0, *up, false), 0, *up));

	// A TRIGGER must read as pressed under BOTH conventions.
	Row t = rowWith(blankRow(), 0, *a1, true);
	FrameInputs tf{}; readFrame(t, 0, tf);
	claim("a trigger column sets the byte AND the kcode bit",
			tf.triggers.l >= 0x20 && (tf.kcode & BTN_TRIGGER_LEFT) != 0);
	claim("...and clears both", [&]{
		Row c0 = rowWith(t, 0, *a1, false);
		FrameInputs cf{}; readFrame(c0, 0, cf);
		return cf.triggers.l < 0x20 && (cf.kcode & BTN_TRIGGER_LEFT) == 0;
	}());

	// setColumn over a HOLE still produces a row.
	std::map<u32, Row> src;
	src[5] = rowWith(blankRow(), 0, *up, true);
	Edit e = setColumn(src, { 5, 6 }, 0, *up, true);
	claim("setColumn writes a selected frame that had no record",
			e.count(6) == 1 && rowHas(e[6], 0, *up));

	// blankRows clears content but names every selected frame.
	Edit b = blankRows({ 5, 9 });
	claim("blankRows covers exactly the selection", b.size() == 2 && b.count(9) == 1);
	claim("...and each is empty", !rowHas(b[5], 0, *up));

	// ---- THE IDENTITY CLAIMS ----
	std::map<u32, Row> all;
	for (u32 i = 0; i < 5; i++)
		all[i] = rowWith(blankRow(), 0, *up, i == 3);	// only frame 3 pressed

	Edit d = deleteRows(all, { 1 });
	claim("deleteRows shortens the movie by the count removed", d.size() == 4);
	// frame 3 held the press; removing ONE row before it moves it to 2.
	claim("...and PULLS THE TAIL UP, so the marked frame renumbers",
			rowHas(d[2], 0, *up) && !rowHas(d[3], 0, *up));

	Edit ins = insertBlanks(all, 1, 2);
	claim("insertBlanks lengthens by the count inserted", ins.size() == 7);
	claim("...pushes the tail down, so the marked frame renumbers",
			rowHas(ins[5], 0, *up) && !rowHas(ins[3], 0, *up));
	claim("...and the inserted frames are blank",
			!rowHas(ins[1], 0, *up) && !rowHas(ins[2], 0, *up));
	claim("...while frames before the point keep their identity",
			ins.count(0) == 1 && !rowHas(ins[0], 0, *up));

	// A resize map must describe the WHOLE movie, or ApplyEditResize reads the
	// gaps as deletions.
	claim("a resize transform returns every surviving frame, not just changed ones",
			d.size() + 1 == all.size() && ins.size() == all.size() + 2);

	// THE FUNNEL'S COVERAGE RULE, learned the hard way: ApplyEdit REFUSES a map
	// that does not span the movie, so a focused transform must be merged first.
	Edit focused = blankRows({ 2 });
	claim("a focused edit names only what it touches", focused.size() == 1);
	Edit merged = mergeIntoMovie(all, focused);
	claim("merged, it covers the whole movie", merged.size() == all.size()
			&& merged.begin()->first == all.begin()->first
			&& merged.rbegin()->first == all.rbegin()->first);
	claim("...the edited frame carries the edit", !rowHas(merged[2], 0, *up));
	claim("...and every other frame is untouched", rowHas(merged[3], 0, *up));

	// ---- MASH ----
	std::map<u32, Row> m;
	for (u32 i = 0; i < 12; i++) m[i] = blankRow();

	Edit p0 = paintColumn(m, 2, 6, 0, *up, true, 0);
	claim("gap 0 paints every row in the range",
			rowHas(p0[2], 0, *up) && rowHas(p0[3], 0, *up) && rowHas(p0[6], 0, *up));
	claim("...and nothing outside it", !rowHas(p0[1], 0, *up) && !rowHas(p0[7], 0, *up));

	Edit p1 = paintColumn(m, 2, 8, 0, *up, true, 1);
	claim("gap 1 paints every other row",
			rowHas(p1[2], 0, *up) && !rowHas(p1[3], 0, *up)
			&& rowHas(p1[4], 0, *up) && rowHas(p1[8], 0, *up));

	// THE DISCRIMINATING CLAIM: the phase follows the ANCHOR, not the range
	// start. THE RANGE LENGTH MUST NOT BE A MULTIPLE OF THE STEP, or the two
	// implementations coincide and the claim proves nothing - the first version
	// of this used anchor=8 to=4 step=2, where anchored fires 8,6,4 and
	// range-start fires 4,6,8: THE SAME ROWS. Sabotaging the phase left it
	// green, which is how the dud was found.
	//
	// anchor=8 to=3 step=2 separates them completely:
	//   anchored     |f-8| % 2 == 0  ->  8, 6, 4
	//   range-start  (f-3) % 2 == 0  ->  3, 5, 7
	Edit down = paintColumn(m, 8, 3, 0, *up, true, 1);
	claim("the gap phase is anchored, not measured from the range start",
			rowHas(down[8], 0, *up) && rowHas(down[6], 0, *up) && rowHas(down[4], 0, *up)
			&& !rowHas(down[7], 0, *up) && !rowHas(down[5], 0, *up)
			&& !rowHas(down[3], 0, *up));
	claim("...and the anchor row always fires, whichever way the drag went",
			rowHas(down[8], 0, *up) && rowHas(paintColumn(m, 8, 13, 0, *up, true, 1)[8], 0, *up));

	Edit past = paintColumn(m, 10, 14, 0, *up, true, 0);
	claim("painting past the end EXTENDS the movie with blanks",
			past.size() == 15 && rowHas(past[14], 0, *up));

	claim("inserting nothing is the movie unchanged",
			insertBlanks(all, 2, 0).size() == all.size());

	NOTICE_LOG(RENDERER, "ROLLEDIT SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
