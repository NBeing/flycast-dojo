#include "roll_select.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <algorithm>

namespace roll
{

static Selection theSelection;
Selection& selection() { return theSelection; }

void Selection::clear()
{
	rows_.clear();
	base_.clear();
	anchor_   = ~0u;
	dragging_ = false;
}

void Selection::press(u32 row, Mods m)
{
	if (m.alt)
	{
		// Alt CLEARS and arms nothing: it is the escape from a selection, not a
		// way to start one. Ctrl+Alt therefore cancel each other, which is what
		// the fork's tooltip promises.
		clear();
		return;
	}

	if (m.shift && anchor_ != ~0u)
	{
		// A range REPLACES, unless Ctrl says add. The snapshot decides which,
		// and dragTo() does the work - so a Shift-click and a Shift-drag of one
		// row cannot disagree.
		base_ = m.ctrl ? rows_ : std::set<u32>();
		dragging_ = true;
		dragTo(row);
		return;
	}

	if (m.ctrl)
	{
		if (rows_.erase(row) != 0)
		{
			// Toggling a row OFF anchors here but starts no drag: dragging from
			// a row you just removed would immediately put it back.
			anchor_   = row;
			dragging_ = false;
			base_.clear();
		}
		else
		{
			base_     = rows_;		// keep what is already selected
			anchor_   = row;
			dragging_ = true;
			dragTo(row);
		}
		return;
	}

	// Plain, and the Shift-with-no-anchor fallthrough.
	base_.clear();
	anchor_   = row;
	dragging_ = true;
	dragTo(row);
}

void Selection::dragTo(u32 row)
{
	if (!dragging_ || anchor_ == ~0u)
		return;
	rows_ = base_;
	const u32 a = std::min(anchor_, row);
	const u32 b = std::max(anchor_, row);
	for (u32 r = a; r <= b; r++)
		rows_.insert(rows_.end(), r);
}

void Selection::release()
{
	dragging_ = false;
	base_.clear();
}

/*
	SELF-TEST. The grammar is five branches and a drag, and every one of them is
	a rule a user will notice being wrong - so each gets a claim rather than a
	round-trip that any of them could satisfy.
*/
void selectionSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLSEL SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	Selection s;
	const Mods plain{}, shift{true,false,false}, ctrl{false,true,false},
			ctrlShift{true,true,false}, alt{false,false,true};

	claim("a fresh selection is empty and has no anchor",
			s.empty() && s.anchor() == ~0u && s.lo() == ~0u);

	s.press(10, plain); s.release();
	claim("a plain press selects one row and anchors it",
			s.count() == 1 && s.has(10) && s.anchor() == 10);

	s.press(20, plain); s.release();
	claim("a plain press REPLACES rather than adding",
			s.count() == 1 && s.has(20) && !s.has(10));

	// Shift ranges from the anchor, and REPLACES.
	s.press(25, shift); s.release();
	claim("Shift takes the range from the anchor, replacing",
			s.count() == 6 && s.has(20) && s.has(25) && !s.has(19));

	// Ctrl toggles one row without disturbing the rest.
	s.press(30, ctrl); s.release();
	claim("Ctrl ADDS a disjoint row", s.count() == 7 && s.has(30) && s.has(20));
	claim("...so the set is NOT contiguous between lo and hi",
			s.lo() == 20 && s.hi() == 30 && !s.has(27));
	s.press(30, ctrl); s.release();
	claim("Ctrl again REMOVES it", s.count() == 6 && !s.has(30));

	// Ctrl+Shift ranges from the anchor and ADDS.
	s.clear();
	s.press(5, plain); s.release();
	s.press(8, plain); s.release();		// anchor now 8, selection {8}
	s.press(3, ctrl);  s.release();		// {3,8}, anchor 3
	s.press(6, ctrlShift); s.release();
	claim("Ctrl+Shift ranges from the anchor and ADDS",
			s.has(8) && s.has(3) && s.has(4) && s.has(5) && s.has(6));

	// Alt clears and arms nothing.
	s.press(4, alt);
	claim("Alt clears, and leaves no anchor to range from",
			s.empty() && s.anchor() == ~0u && !s.dragging());

	// A DRAG REPAINTS FROM THE SNAPSHOT, which is what lets it shrink.
	s.clear();
	s.press(50, plain);				// dragging, base empty
	s.dragTo(55);
	claim("a drag grows from the anchor", s.count() == 6 && s.has(55));
	s.dragTo(52);
	claim("...and SHRINKS when the mouse comes back",
			s.count() == 3 && s.has(52) && !s.has(55));
	s.release();
	claim("release ends the drag", !s.dragging());

	// The same drag, additive: Ctrl kept the earlier rows in the snapshot.
	s.clear();
	s.press(1, plain); s.release();
	s.press(9, ctrl);				// base = {1}, anchor 9, dragging
	s.dragTo(11);
	claim("a Ctrl drag keeps what was already selected",
			s.has(1) && s.has(9) && s.has(10) && s.has(11));
	s.release();

	// Shift with NO anchor must not be a dead click.
	Selection t;
	t.press(7, shift); t.release();
	claim("Shift with no anchor behaves as a plain press",
			t.count() == 1 && t.has(7) && t.anchor() == 7);

	// dragTo without a press does nothing rather than inventing an anchor.
	Selection u;
	u.dragTo(3);
	claim("dragTo outside a drag is a no-op", u.empty());

	NOTICE_LOG(RENDERER, "ROLLSEL SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
