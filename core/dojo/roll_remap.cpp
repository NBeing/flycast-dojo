#include "roll_remap.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <algorithm>

namespace roll
{

Remap Remap::identity()
{
	Remap m;
	m.identity_ = true;
	return m;
}

Remap Remap::inserted(u32 at, u32 count, u32 movieRows)
{
	Remap m;
	if (count == 0)
		return identity();
	// TWO SPANS, and the first one is the point: rows BELOW the insert do not
	// move. Writing one span over the whole movie with a single delta is the
	// fork's bug, and it is a shorter, more obvious-looking piece of code.
	if (at > 0)
		m.spans_.push_back(Span{ 0, std::min(at, movieRows), 0 });
	if (movieRows > at)
		m.spans_.push_back(Span{ at, movieRows - at, (s64)count });
	return m;
}

Remap Remap::deleted(const std::set<u32>& rows, u32 movieRows)
{
	Remap m;
	if (rows.empty())
		return identity();

	// One span per SURVIVING RUN. `removed` cannot change inside a run - a run
	// ends precisely where a deletion is - so the delta captured when the run
	// closes is the count of deletions strictly below where it opened.
	u32 removed = 0, runStart = 0;
	bool inRun = false;
	for (u32 r = 0; r < movieRows; r++)
	{
		if (rows.count(r) != 0)
		{
			if (inRun)
			{
				m.spans_.push_back(Span{ runStart, r - runStart, -(s64)removed });
				inRun = false;
			}
			removed++;
		}
		else if (!inRun)
		{
			runStart = r;
			inRun = true;
		}
	}
	if (inRun)
		m.spans_.push_back(Span{ runStart, movieRows - runStart, -(s64)removed });
	return m;
}

bool Remap::at(u32 row, u32& out) const
{
	if (identity_)
	{
		out = row;
		return true;
	}
	// Spans are sorted and disjoint: the candidate is the last one starting at
	// or below `row`.
	auto it = std::upper_bound(spans_.begin(), spans_.end(), row,
			[](u32 v, const Span& s) { return v < s.from; });
	if (it == spans_.begin())
		return false;
	--it;
	if (row >= it->from + it->count)
		return false;			// inside a deleted run, or past the end
	out = (u32)((s64)row + it->delta);
	return true;
}

void Remap::applyTo(std::set<u32>& rows) const
{
	if (identity_)
		return;
	std::set<u32> next;
	for (u32 r : rows)
	{
		u32 to = 0;
		if (at(r, to))
			next.insert(next.end(), to);
	}
	rows.swap(next);
}

/*
	SELF-TEST. The claims that matter are the NEGATIVE ones - a remap that moved
	everything by the right amount would pass every positive check here and still
	be the fork's bug, because the fork's bug is moving rows that should not have
	moved.
*/
void remapSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLREMAP SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	auto to = [](const Remap& m, u32 r) -> s64 {
		u32 o = 0;
		return m.at(r, o) ? (s64)o : -1;
	};

	{
		const Remap m = Remap::identity();
		claim("identity moves nothing", m.isIdentity() && to(m, 0) == 0 && to(m, 900) == 900);
	}

	// ---- insert ----------------------------------------------------------
	{
		const Remap m = Remap::inserted(5, 3, 20);
		claim("insert moves rows at and above the point", to(m, 5) == 8 && to(m, 19) == 22);
		// THE CLAIM THE FORK FAILS. Two of its five selection-shift sites move
		// these rows, so a selection at 10..20 slides forward when you paste at
		// frame 500.
		claim("insert leaves rows BELOW the point exactly where they were",
				to(m, 0) == 0 && to(m, 4) == 4);
		claim("the boundary row is the first that moves", to(m, 4) == 4 && to(m, 5) == 8);
	}
	{
		const Remap m = Remap::inserted(0, 2, 10);
		claim("inserting at 0 moves the whole movie", to(m, 0) == 2 && to(m, 9) == 11);
	}
	claim("inserting nothing is identity", Remap::inserted(7, 0, 10).isIdentity());

	// ---- delete ----------------------------------------------------------
	{
		std::set<u32> gone{ 2, 3 };
		const Remap m = Remap::deleted(gone, 8);
		claim("delete leaves rows below alone", to(m, 0) == 0 && to(m, 1) == 1);
		claim("delete pulls the tail up by the count removed", to(m, 4) == 2 && to(m, 7) == 5);
		// A DELETED ROW HAS NO IMAGE. Answering "unchanged" here is what makes a
		// selection keep naming frames that now hold something else, which is
		// worse than naming nothing.
		claim("a deleted row does not exist afterwards", to(m, 2) == -1 && to(m, 3) == -1);
	}
	{
		std::set<u32> gone{ 2, 5 };
		const Remap m = Remap::deleted(gone, 8);
		claim("scattered deletes shift each run by its own count",
				to(m, 1) == 1 && to(m, 3) == 2 && to(m, 4) == 3 && to(m, 6) == 4 && to(m, 7) == 5);
		claim("one span per surviving run", m.spans() == 3);
	}
	{
		std::set<u32> gone{ 0, 1, 2 };
		const Remap m = Remap::deleted(gone, 3);
		claim("deleting everything leaves nothing", to(m, 0) == -1 && to(m, 2) == -1);
	}
	claim("deleting nothing is identity", Remap::deleted({}, 10).isIdentity());

	// ---- applied to a selection -----------------------------------------
	{
		std::set<u32> sel{ 1, 2, 4, 7 };
		Remap::deleted({ 2, 3 }, 8).applyTo(sel);
		claim("a selection drops deleted rows and shifts survivors",
				sel == std::set<u32>({ 1, 2, 5 }));
	}
	{
		std::set<u32> sel{ 10, 11, 12 };
		Remap::inserted(500, 4, 600).applyTo(sel);
		claim("a selection below an insert does not move",
				sel == std::set<u32>({ 10, 11, 12 }));
	}
	{
		std::set<u32> sel{ 3, 4 };
		Remap::identity().applyTo(sel);
		claim("identity leaves a selection untouched", sel == std::set<u32>({ 3, 4 }));
	}
	{
		std::set<u32> sel{ 2, 3 };
		Remap::deleted({ 2, 3 }, 8).applyTo(sel);
		claim("a selection entirely inside a deletion becomes empty", sel.empty());
	}

	NOTICE_LOG(RENDERER, "ROLLREMAP SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
