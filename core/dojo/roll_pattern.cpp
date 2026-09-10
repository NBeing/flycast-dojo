#include "roll_pattern.h"
#include "tasmacro.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <algorithm>

namespace roll
{

size_t Pattern::period() const
{
	size_t n = 0;
	for (const auto& t : tracks)
		n = std::max(n, t.size());
	return n;
}

Pattern Pattern::one(int lane, Cell bits, Cell mask)
{
	Pattern p;
	if (lane < 0)
		return p;
	p.tracks.resize((size_t)lane + 1);
	p.tracks[(size_t)lane].push_back(CellOp{ bits, mask });
	return p;
}

Edit applyPattern(const std::map<u32, Row>& all, u32 anchor, u32 to,
		const Pattern& p, int gap)
{
	Edit out(all.begin(), all.end());
	if (p.empty())
		return out;					// nothing to write; the movie is returned intact

	const u32 lo   = std::min(anchor, to);
	const u32 hi   = std::max(anchor, to);
	const u32 step = (u32)(gap < 0 ? 0 : gap) + 1;

	// Extend with blanks FIRST if the range runs past the end, so the rows the
	// pattern wants to touch exist to be touched - and so the movie stays
	// CONTIGUOUS. Creating only the rows the pattern fires on would leave holes
	// between them, which nothing downstream would refuse.
	const u32 end = all.empty() ? 0 : all.rbegin()->first;
	for (u32 f = end + 1; f <= hi && !all.empty(); f++)
		if (out.find(f) == out.end())
			out[f] = blankRow();

	// The lowest firing row in range. The pattern's step 0 lands HERE rather
	// than on the anchor, which is what makes an upward drag write the pattern
	// forwards in time while keeping the phase anchored (see the header).
	const u32 firstFired = anchor - ((anchor - lo) / step) * step;

	const int lanes = laneCount();
	for (u32 f = firstFired; f <= hi; f += step)
	{
		if (f < lo)
			continue;
		const size_t bk = (size_t)((f - firstFired) / step);
		auto it = out.find(f);
		Row row = it == out.end() ? blankRow() : it->second;

		for (size_t lane = 0; lane < p.tracks.size() && (int)lane < lanes; lane++)
		{
			const std::vector<CellOp>& track = p.tracks[lane];
			if (track.empty())
				continue;			// this lane is not part of the pattern
			const CellOp& op = track[bk % track.size()];
			if (op.mask == 0)
				continue;			// a step that writes nothing, deliberately
			const Cell have = cellOf(row, (int)lane);
			row = cellInto(row, (int)lane, cellApply(have, op.bits, op.mask));
		}
		out[f] = row;
	}
	return out;
}

/*
	SELF-TEST. Every claim is a rule a user would notice being broken, and four
	of them are rules the fork gets WRONG - so a port that passed by copying
	would fail here, which is the point of writing them down.
*/
void patternSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLPATTERN SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& prof = profile();
	int up = -1, dn = -1, lf = -1, rt = -1, lp = -1, hp = -1;
	for (int i = 0; i < prof.count; i++)
	{
		switch (prof.cols[i].canon)
		{
		case tas_macro::CANON_UP:    up = i; break;
		case tas_macro::CANON_DOWN:  dn = i; break;
		case tas_macro::CANON_LEFT:  lf = i; break;
		case tas_macro::CANON_RIGHT: rt = i; break;
		case tas_macro::CANON_LP:    lp = i; break;
		case tas_macro::CANON_HP:    hp = i; break;
		default: break;
		}
	}
	claim("the profile offers the columns this test needs",
			up >= 0 && dn >= 0 && lf >= 0 && rt >= 0 && lp >= 0 && hp >= 0);
	if (up < 0 || dn < 0 || lf < 0 || rt < 0 || lp < 0 || hp < 0)
	{
		NOTICE_LOG(RENDERER, "ROLLPATTERN SELFTEST: %d passed, %d failed", pass, fail);
		return;
	}
	const Column& cUp = prof.cols[up];
	const Column& cDn = prof.cols[dn];
	const Column& cLf = prof.cols[lf];
	const Column& cRt = prof.cols[rt];
	const Column& cLp = prof.cols[lp];
	const Column& cHp = prof.cols[hp];

	auto fresh = [&]() {
		std::map<u32, Row> m;
		for (u32 i = 0; i < 16; i++)
			m[i] = blankRow();
		return m;
	};
	auto has = [&](const Edit& e, u32 f, int lane, const Column& c) {
		auto it = e.find(f);
		return it != e.end() && cellHas(cellOf(it->second, lane), c);
	};

	// ---- the cell rules, which is where the game lives -------------------
	claim("a button accumulates",
			cellHas(cellApply(cellWith(0, cLp, true), cellWith(0, cHp, true), cHp.canon), cLp)
			&& cellHas(cellApply(cellWith(0, cLp, true), cellWith(0, cHp, true), cHp.canon), cHp));
	{
		const Cell had = cellWith(0, cRt, true);
		const Cell now = cellApply(had, cellWith(0, cLf, true), cLf.canon);
		claim("a direction REPLACES: painting left over right gives left, not both",
				cellHas(now, cLf) && !cellHas(now, cRt));
	}
	{
		const Cell had = cellWith(cellWith(0, cRt, true), cLp, true);
		const Cell now = cellApply(had, 0, cRt.canon);
		claim("clearing a direction does not resurrect its opposite",
				!cellHas(now, cRt) && !cellHas(now, cLf) && cellHas(now, cLp));
	}
	{
		const Cell both = cellWith(cellWith(0, cUp, true), cDn, true);
		const Cell now  = cellApply(0, both, cellAll());
		claim("an opposed pair cannot survive: up+down cancels",
				!cellHas(now, cUp) && !cellHas(now, cDn));
	}
	claim("a masked write leaves bits outside the mask alone",
			cellHas(cellApply(cellWith(0, cLp, true), 0, cHp.canon), cLp));

	// ---- the codec ------------------------------------------------------
	{
		const Row r = cellInto(blankRow(), 0, cellWith(cellWith(0, cLp, true), cUp, true));
		claim("a cell round-trips through a row",
				cellOf(r, 0) == cellWith(cellWith(0, cLp, true), cUp, true));
		claim("writing lane 0 leaves lane 1 neutral", cellOf(r, 1) == 0);
		claim("a row holds more than one lane", laneCount() >= 2);
	}

	// ---- the pattern ----------------------------------------------------
	{
		Edit e = applyPattern(fresh(), 4, 8, Pattern::one(0, cLp.canon, cLp.canon), 0);
		bool all = true;
		for (u32 f = 4; f <= 8; f++) all = all && has(e, f, 0, cLp);
		claim("gap 0 writes every row in the range", all && !has(e, 3, 0, cLp));
		claim("the whole movie comes back", e.size() == 16);
	}
	{
		Edit e = applyPattern(fresh(), 4, 10, Pattern::one(0, cLp.canon, cLp.canon), 1);
		claim("gap 1 fires every other row from the anchor",
				has(e, 4, 0, cLp) && !has(e, 5, 0, cLp) && has(e, 6, 0, cLp));
	}
	{
		// A gap row that ALREADY held input must keep it. The fork's
		// replace-mode brush writes neutral here.
		std::map<u32, Row> m = fresh();
		m[5] = cellInto(blankRow(), 0, cellWith(0, cHp, true));
		Edit e = applyPattern(m, 4, 10, Pattern::one(0, cLp.canon, cLp.canon), 1);
		claim("a gap row is left ALONE, not cleared", has(e, 5, 0, cHp));
	}
	{
		Edit dn2 = applyPattern(fresh(), 4, 10, Pattern::one(0, cLp.canon, cLp.canon), 1);
		Edit up2 = applyPattern(fresh(), 10, 4, Pattern::one(0, cLp.canon, cLp.canon), 1);
		bool same = true;
		for (u32 f = 4; f <= 10; f++)
			same = same && (has(dn2, f, 0, cLp) == has(up2, f, 0, cLp));
		claim("dragging up and dragging down fire the same rows", !same ? false : true);
	}
	{
		// THE FORK'S BUG. A two-step pattern dragged UPWARD must still read
		// forwards in time: the lower row gets step 0.
		Pattern p;
		p.tracks.resize(1);
		p.tracks[0].push_back(CellOp{ cLp.canon, cellAll() });
		p.tracks[0].push_back(CellOp{ cHp.canon, cellAll() });
		Edit e = applyPattern(fresh(), 9, 6, p, 0);		// dragged UP, 9 -> 6
		claim("a pattern advances in FRAME order however the drag went",
				has(e, 6, 0, cLp) && has(e, 7, 0, cHp) && has(e, 8, 0, cLp) && has(e, 9, 0, cHp));
	}
	{
		Pattern p;
		p.tracks.resize(2);
		p.tracks[1].push_back(CellOp{ cLp.canon, cLp.canon });	// lane 0 track empty
		std::map<u32, Row> m = fresh();
		m[5] = cellInto(blankRow(), 0, cellWith(0, cHp, true));
		Edit e = applyPattern(m, 4, 8, p, 0);
		claim("an EMPTY track leaves that lane alone rather than writing neutral",
				has(e, 5, 0, cHp) && has(e, 5, 1, cLp));
	}
	{
		std::map<u32, Row> m;
		for (u32 i = 0; i < 6; i++) m[i] = blankRow();
		Edit e = applyPattern(m, 4, 9, Pattern::one(0, cLp.canon, cLp.canon), 1);
		bool contiguous = true;
		for (u32 f = 0; f <= 9; f++) contiguous = contiguous && e.count(f) == 1;
		claim("a stroke past the end extends the movie CONTIGUOUSLY", contiguous);
	}
	{
		Edit e = applyPattern(fresh(), 4, 8, Pattern(), 0);
		claim("an empty pattern changes nothing", e.size() == 16 && !has(e, 4, 0, cLp));
	}

	NOTICE_LOG(RENDERER, "ROLLPATTERN SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
