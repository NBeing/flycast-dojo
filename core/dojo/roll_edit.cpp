#include "roll_edit.h"
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

Row rowWith(const Row& r, int player, const Column& c, bool on)
{
	Row out = r;
	if (out.size() < rowBytes())
		out.resize(rowBytes(), 0);
	FrameInputs f{};
	readFrame(out, player, f);

	if (c.trigger < 0)
	{
		if (on) f.kcode |= c.bit;
		else    f.kcode &= ~c.bit;
	}
	else
	{
		const u32 tbit = c.trigger == 0 ? BTN_TRIGGER_LEFT : BTN_TRIGGER_RIGHT;
		u8& b = c.trigger == 0 ? f.triggers.l : f.triggers.r;
		// BOTH spellings, so the row reads as pressed whichever one a reader
		// checks. Setting only the byte would leave a kcode-reading path seeing
		// an unpressed trigger - which is how half a recording goes missing.
		b = on ? 0xFF : 0x00;
		if (on) f.kcode |= tbit;
		else    f.kcode &= ~tbit;
	}
	writeFrame(out, player, f);
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

	claim("inserting nothing is the movie unchanged",
			insertBlanks(all, 2, 0).size() == all.size());

	NOTICE_LOG(RENDERER, "ROLLEDIT SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
