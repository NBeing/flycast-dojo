#include "roll_paint.h"
#include "tasmacro.h"
#include "roll_notation.h"
#include <string>
#include "cfg/cfg.h"
#include "log/LogManager.h"

namespace roll
{

static Paint thePaint;
Paint& paint() { return thePaint; }

void Paint::begin(u32 row, int player, int column, bool wasOn, bool forceErase, int gap)
{
	active_ = true;
	player_ = player;
	col_    = column;
	anchor_ = last_ = row;
	// Alt erases outright; otherwise the anchor cell TOGGLES - land on input and
	// the stroke removes it, land on empty and it writes.
	on_     = forceErase ? false : !wasOn;
	step_   = gap < 0 ? 1 : gap + 1;
}

void Paint::extendTo(u32 row)
{
	if (active_)
		last_ = row;		// REPLACED, never accumulated: the stroke can shrink
}

void Paint::end() { active_ = false; }

bool Paint::touches(u32 row) const
{
	if (row < lo() || row > hi())
		return false;
	const u32 d = row >= anchor_ ? row - anchor_ : anchor_ - row;
	return d % (u32)step_ == 0;
}

void Paint::arm(const std::vector<Cell>& cells, bool merge)
{
	brush_.clear();
	for (Cell c : cells)
		// MERGE IS THE MASK: an overdub masks only the bits it carries and so
		// cannot clear anything; a stamp masks every bit the profile models.
		brush_.push_back(CellOp{ c, merge ? c : cellAll() });
}

Edit Paint::build(const std::map<u32, Row>& all) const
{
	if (!active_)
		return Edit(all.begin(), all.end());
	if (!brush_.empty())
	{
		// ARMED: the same applyPattern the mash uses, on this stroke's lane.
		Pattern pat;
		pat.tracks.resize((size_t)player_ + 1);
		pat.tracks[(size_t)player_] = brush_;
		return applyPattern(all, anchor_, last_, pat, step_ - 1);
	}
	// DELEGATED, not reimplemented. roll_edit's paintColumn() is the same rule
	// as a pure function - phase anchored at the anchor, gap rows skipped, whole
	// movie returned - and it was written independently to the same conclusions.
	// Two copies of one rule drift; this class owns the GESTURE (what is active,
	// where it began, whether it writes or erases) and delegates the arithmetic.
	//
	// It is also stricter than the loop this replaced: it pre-fills blanks for
	// every row past the old end, where mine created only the rows the pattern
	// FIRES on - so painting 10->14 at gap 1 left 11 and 13 missing and the
	// movie non-contiguous. ApplyEdit permits extension, so nothing would have
	// refused it.
	return paintColumn(all, anchor_, last_, player_, profile().cols[col_], on_, step_ - 1);
}

/*
	SELF-TEST. Every claim here is a rule a user would notice being broken, and
	three of them are rules I would have got WRONG by guessing rather than
	reading the fork: gap rows are skipped rather than inverted, the gap is
	counted from the anchor rather than the top of the range, and the stroke is
	an extent that can shrink rather than an accumulation.
*/
void paintSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLPAINT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& prof = profile();
	int up = -1, dn = -1;
	for (int i = 0; i < prof.count; i++)
	{
		if (prof.cols[i].canon == tas_macro::CANON_UP)   up = i;
		if (prof.cols[i].canon == tas_macro::CANON_DOWN) dn = i;
	}
	claim("the profile offers two distinct columns", up >= 0 && dn >= 0 && up != dn);
	if (up < 0 || dn < 0) { NOTICE_LOG(RENDERER, "ROLLPAINT SELFTEST: %d passed, %d failed", pass, fail); return; }

	std::map<u32, Row> movie;
	for (u32 i = 0; i < 12; i++)
		movie[i] = blankRow();

	// --- a plain stroke, every row ---
	Paint p;
	p.begin(2, 0, up, /*wasOn*/false, /*forceErase*/false, /*gap*/0);
	p.extendTo(5);
	claim("a stroke spans anchor..last", p.lo() == 2 && p.hi() == 5);
	Edit e = p.build(movie);
	claim("every row in range is painted",
			rowHas(e[2], 0, prof.cols[up]) && rowHas(e[3], 0, prof.cols[up])
			&& rowHas(e[4], 0, prof.cols[up]) && rowHas(e[5], 0, prof.cols[up]));
	claim("...and nothing outside it", !rowHas(e[1], 0, prof.cols[up])
			&& !rowHas(e[6], 0, prof.cols[up]));
	claim("...in that column only", !rowHas(e[3], 0, prof.cols[dn]));
	claim("...for that player only", !rowHas(e[3], 1, prof.cols[up]));
	claim("the edit covers the whole movie", e.size() == movie.size());

	// --- EXTENT, NOT ACCUMULATION: the stroke can shrink ---
	p.extendTo(3);
	Edit shrunk = p.build(movie);
	claim("pulling the stroke back UNPAINTS the rows it left",
			rowHas(shrunk[3], 0, prof.cols[up]) && !rowHas(shrunk[5], 0, prof.cols[up]));
	p.end();

	// --- GAP: skipped, not inverted, counted from the anchor ---
	//
	// THE DATA HAS TO DISCRIMINATE, and the first version of this block did not.
	// `[MEASURED 2026-09-09]` a sabotage that INVERTED gap rows instead of
	// skipping them left the test at 16/16: the stroke erased, so inverting a
	// gap row WROTE it - and the movie already had every row written, so the
	// bug was invisible. A claim is only worth its sabotage.
	//
	// Fixed by making the gap rows differ from what an inversion would write:
	// the stroke WRITES, so an inversion would CLEAR them, and they start set.
	std::map<u32, Row> mixed;
	for (u32 i = 0; i < 12; i++)
		mixed[i] = rowWith(blankRow(), 0, prof.cols[up], true);	// all set

	Paint g;
	g.begin(4, 0, up, /*wasOn*/false, false, /*gap*/1);	// WRITES, over set rows
	g.extendTo(8);
	claim("the anchor decides the direction, and here it writes", g.writes());
	Edit ge = g.build(mixed);
	claim("every 2nd row from the anchor is written",
			rowHas(ge[4], 0, prof.cols[up]) && rowHas(ge[6], 0, prof.cols[up])
			&& rowHas(ge[8], 0, prof.cols[up]));
	claim("GAP ROWS ARE LEFT ALONE, not inverted",
			rowHas(ge[5], 0, prof.cols[up]) && rowHas(ge[7], 0, prof.cols[up]));

	// ...and an ERASING stroke leaves set gap rows set, which is the same rule
	// from the other side - neither direction may touch a gap row.
	Paint g2;
	g2.begin(4, 0, up, /*wasOn*/true, false, /*gap*/1);	// ERASES
	g2.extendTo(8);
	Edit g2e = g2.build(mixed);
	claim("an erasing stroke clears only its own rows",
			!rowHas(g2e[4], 0, prof.cols[up]) && !rowHas(g2e[6], 0, prof.cols[up]));
	claim("...and leaves the gap rows set", rowHas(g2e[5], 0, prof.cols[up]));
	g.end(); g2.end();

	// COUNTED FROM THE ANCHOR, NOT FROM lo(). The first version of this claim
	// used anchor 8 -> last 4, where lo() IS 4 and the range is five rows long -
	// so both formulas picked the same rows and a sabotage counting from lo()
	// also passed 16/16. An ODD offset makes them disagree: from the anchor 8,6,4
	// fire; from lo() it would be 3,5,7.
	Paint u2;
	u2.begin(8, 0, up, false, false, /*gap*/1);
	u2.extendTo(3);
	claim("dragging upward anchors the pattern at the ANCHOR",
			u2.touches(8) && u2.touches(6) && u2.touches(4));
	claim("...and NOT at the bottom of the range",
			!u2.touches(3) && !u2.touches(5) && !u2.touches(7));
	u2.end();

	// --- Alt forces erase even on an empty cell ---
	Paint a;
	a.begin(1, 0, up, /*wasOn*/false, /*forceErase*/true, 0);
	claim("Alt erases even when the anchor cell was empty", !a.writes());
	a.end();

	// --- past the end, the stroke CREATES frames ---
	Paint x;
	x.begin(10, 0, up, false, false, 0);
	x.extendTo(14);
	Edit xe = x.build(movie);
	claim("painting past the movie end creates the frames",
			xe.count(14) == 1 && rowHas(xe[14], 0, prof.cols[up]));
	x.end();

	// --- an inactive stroke changes nothing ---
	Paint idle;
	claim("an inactive stroke is the movie unchanged",
			idle.build(movie).size() == movie.size()
			&& !rowHas(idle.build(movie)[3], 0, prof.cols[up]));

	// ---- ARMED: the brush ------------------------------------------------
	{
		std::vector<Cell> cells;
		std::string err;
		parsePattern("2 8", cells, err);		// down, up
		Paint p;
		p.arm(cells, false);
		claim("arming makes the stroke a brush", p.armed());
		p.begin(4, 0, up, false, false, 0);
		p.extendTo(7);
		Edit e = p.build(movie);
		auto lane0 = [&](u32 f) {
			auto it = e.find(f);
			return it == e.end() ? (Cell)0 : cellOf(it->second, 0);
		};
		// FRAME ORDER, tiled, exactly as the mash writes it - and NOT decided by
		// what the anchor cell held.
		claim("an armed stroke stamps the pattern in frame order",
				lane0(4) == cells[0] && lane0(5) == cells[1]
				&& lane0(6) == cells[0] && lane0(7) == cells[1]);
		p.arm({}, false);
		claim("arming nothing disarms", !p.armed());
	}

	NOTICE_LOG(RENDERER, "ROLLPAINT SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
