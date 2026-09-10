#include "roll_staged.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <algorithm>

namespace roll
{

namespace { Staged theStaged; }

Staged& staged() { return theStaged; }

void Staged::load(const std::map<u32, Row>& all, const std::set<u32>& rows)
{
	base_.clear();
	ops_.clear();
	const int lanes = laneCount();
	for (u32 r : rows)			// ascending: a std::set is frame order
	{
		Frame f((size_t)lanes, 0);
		auto it = all.find(r);
		// A HOLE READS AS NEUTRAL rather than being skipped, the same rule
		// reverseRows follows: skipping would silently shorten the clip and the
		// user would find out at the movie.
		if (it != all.end())
			for (int l = 0; l < lanes; l++)
				f[(size_t)l] = cellOf(it->second, l);
		base_.push_back(std::move(f));
	}
	now_ = base_;
}

void Staged::unload()
{
	base_.clear();
	now_.clear();
	ops_.clear();
}

void Staged::push(Step s)
{
	if (s.k < 2)
		s.k = 2;
	ops_.push_back(s);
	recompute();
}

void Staged::pop()
{
	if (ops_.empty())
		return;
	ops_.pop_back();
	recompute();
}

void Staged::clearOps()
{
	ops_.clear();
	recompute();
}

void Staged::recompute()
{
	// REPLAYED FROM THE BASELINE, always, never applied incrementally. That is
	// the one design decision in this file: it costs a full pass per edit of the
	// queue and it is what makes Compress reversible, because the frames a
	// compress drops were never removed from base_.
	Clip c = base_;
	const Profile& p = profile();
	for (const Step& s : ops_)
	{
		switch (s.op)
		{
		case Op::SwapLanes:
			for (Frame& f : c)
				if (f.size() >= 2) std::swap(f[0], f[1]);
			break;
		case Op::CopyLaneUp:
			for (Frame& f : c)
				if (f.size() >= 2) f[1] = f[0];
			break;
		case Op::CopyLaneDown:
			for (Frame& f : c)
				if (f.size() >= 2) f[0] = f[1];
			break;
		case Op::Flip:
			// THE ONLY OP THAT ASKS THE PROFILE, because mirroring is a question
			// about what LEFT means. Up and down are untouched: a mirror is
			// left-right, and flipping them too would be a rotation.
			for (Frame& f : c)
				for (Cell& v : f)
				{
					const Cell lr = v & (p.left | p.right);
					if (lr != 0 && lr != (p.left | p.right))
						v ^= (p.left | p.right);
				}
			break;
		case Op::Reverse:
			std::reverse(c.begin(), c.end());
			break;
		case Op::Stretch:
		{
			Clip out;
			out.reserve(c.size() * (size_t)s.k);
			for (const Frame& f : c)
				for (int i = 0; i < s.k; i++)
					out.push_back(f);
			c.swap(out);
			break;
		}
		case Op::Compress:
		{
			Clip out;
			for (size_t i = 0; i < c.size(); i += (size_t)s.k)
				out.push_back(c[i]);
			c.swap(out);
			break;
		}
		}
	}
	now_.swap(c);
}

Edit Staged::place(const std::map<u32, Row>& all, u32 at, bool merge) const
{
	if (now_.empty())
		return Edit(all.begin(), all.end());
	// BUILT AS A PATTERN and handed to applyPattern - the sixth customer of that
	// one function. A staged clip is a pattern whose period is its whole length,
	// applied over exactly as many rows, with no gap.
	const int lanes = laneCount();
	Pattern pat;
	pat.tracks.resize((size_t)lanes);
	for (int l = 0; l < lanes; l++)
		for (const Frame& f : now_)
		{
			const Cell c = (size_t)l < f.size() ? f[(size_t)l] : 0;
			pat.tracks[(size_t)l].push_back(CellOp{ c, merge ? c : cellAll() });
		}
	return applyPattern(all, at, at + (u32)now_.size() - 1, pat, 0);
}

/*
	SELF-TEST. The claim that matters is the one an undo stack could not make:
	that a LOSSY op is reversible because the baseline still has what it dropped.
*/
void stagedSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLSTAGED SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& p = profile();
	int lp = -1, lf = -1, rt = -1;
	for (int i = 0; i < p.count; i++)
	{
		if (p.cols[i].canon == p.left)  lf = i;
		if (p.cols[i].canon == p.right) rt = i;
		if (std::string(p.cols[i].label) == "LP") lp = i;
	}
	claim("the profile offers the columns this test needs", lp >= 0 && lf >= 0 && rt >= 0);
	if (lp < 0 || lf < 0 || rt < 0)
	{
		NOTICE_LOG(RENDERER, "ROLLSTAGED SELFTEST: %d passed, %d failed", pass, fail);
		return;
	}

	// Six frames: 0 has LEFT on lane 0, 3 has LP on lane 1, the rest neutral.
	std::map<u32, Row> movie;
	for (u32 i = 0; i < 8; i++)
		movie[i] = blankRow();
	movie[0] = cellInto(blankRow(), 0, p.cols[lf].canon);
	movie[3] = cellInto(blankRow(), 1, p.cols[lp].canon);

	Staged s;
	s.load(movie, { 0, 1, 2, 3, 4, 5 });
	claim("loading takes the named rows", s.size() == 6 && s.baseSize() == 6);
	claim("...and the movie is untouched by loading", movie[0] == cellInto(blankRow(), 0, p.cols[lf].canon));
	claim("a cell survives the trip in", s.clip()[0][0] == (Cell)p.cols[lf].canon
			&& s.clip()[3][1] == (Cell)p.cols[lp].canon);

	// ---- THE CLAIM AN UNDO STACK COULD NOT MAKE ----
	s.push(Step{ Op::Compress, 3 });
	claim("compress drops frames", s.size() == 2);
	s.pop();
	claim("...and popping the op BRINGS THEM BACK, because the baseline kept them",
			s.size() == 6 && s.clip()[3][1] == (Cell)p.cols[lp].canon);

	s.push(Step{ Op::Stretch, 2 });
	claim("stretch holds each frame k times", s.size() == 12
			&& s.clip()[0][0] == s.clip()[1][0]);
	s.push(Step{ Op::Compress, 2 });
	claim("a queue composes in order", s.size() == 6);
	s.clearOps();
	claim("clearing the queue returns the baseline", s.size() == 6 && s.ops().empty());

	s.push(Step{ Op::SwapLanes });
	claim("swap exchanges the lanes",
			s.clip()[0][1] == (Cell)p.cols[lf].canon && s.clip()[3][0] == (Cell)p.cols[lp].canon);
	s.pop();

	s.push(Step{ Op::Flip });
	claim("flip mirrors left to right", s.clip()[0][0] == (Cell)p.cols[rt].canon);
	s.push(Step{ Op::Flip });
	claim("...and flipping twice is where it started", s.clip()[0][0] == (Cell)p.cols[lf].canon);
	s.clearOps();

	s.push(Step{ Op::Reverse });
	claim("reverse turns the clip around", s.clip()[5][0] == (Cell)p.cols[lf].canon
			&& s.clip()[2][1] == (Cell)p.cols[lp].canon);
	s.clearOps();

	// ---- placing it ----
	{
		Edit e = s.place(movie, 2, /*merge*/ false);
		claim("placing writes the clip at the target", e.size() == 8
				&& cellOf(e[2], 0) == (Cell)p.cols[lf].canon);
		claim("...and the movie handed in is still untouched",
				cellOf(movie[2], 0) == 0);
	}
	{
		// MERGE MASKS ONLY WHAT THE CLIP CARRIES, so a NEUTRAL staged frame
		// cannot clear what is already there - and REPLACE can. A pair, because
		// either alone is satisfied by a place that does nothing at all.
		//
		// The clip's frame 1 is neutral and lands on movie frame 3 when placed
		// at 2, so frame 3 is the cell that discriminates.
		std::map<u32, Row> m2 = movie;
		m2[3] = cellInto(blankRow(), 0, p.cols[lp].canon);
		claim("a MERGED place leaves a cell its neutral frame does not name",
				cellOf(s.place(m2, 2, true)[3], 0) == (Cell)p.cols[lp].canon);
		claim("...and a REPLACING place clears it",
				cellOf(s.place(m2, 2, false)[3], 0) == 0);
	}

	s.unload();
	claim("unloading empties it", s.empty() && s.ops().empty());

	NOTICE_LOG(RENDERER, "ROLLSTAGED SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
