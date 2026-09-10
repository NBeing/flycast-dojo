#include "roll_marks.h"
#include "roll_meta.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstdlib>

namespace roll
{

namespace {
Marks theMarks;
// Inside a roll_meta payload, so these must differ from its own separators
// (\x1e between providers, \x1f between key and payload). A label is user text
// and may contain anything printable, which is why the separators are control
// characters rather than a comma and a colon.
const char REC = '\x1d';
const char FLD = '\x1c';
}

Marks& marks() { return theMarks; }

const char *Marks::labelAt(u32 frame) const
{
	auto it = at_.find(frame);
	return it == at_.end() ? nullptr : it->second.c_str();
}

void Marks::set(u32 frame, const std::string& label)
{
	at_[frame] = label;
}

void Marks::toggle(u32 frame)
{
	auto it = at_.find(frame);
	if (it == at_.end())
		at_[frame] = std::string();
	else
		at_.erase(it);
}

bool Marks::next(u32 from, u32& out) const
{
	auto it = at_.upper_bound(from);
	if (it == at_.end())
		return false;
	out = it->first;
	return true;
}

bool Marks::prev(u32 from, u32& out) const
{
	auto it = at_.lower_bound(from);
	if (it == at_.begin())
		return false;
	--it;
	out = it->first;
	return true;
}

void Marks::remap(const Remap& m)
{
	if (m.isIdentity())
		return;
	std::map<u32, std::string> next;
	for (const auto& kv : at_)
	{
		u32 to = 0;
		if (m.at(kv.first, to))
			next[to] = kv.second;		// a deleted frame takes its mark with it
	}
	at_.swap(next);
}

std::string Marks::serialise() const
{
	std::string out;
	for (const auto& kv : at_)
	{
		if (!out.empty())
			out += REC;
		out += std::to_string(kv.first);
		out += FLD;
		out += kv.second;
	}
	return out;
}

void Marks::load(const std::string& blob)
{
	at_.clear();
	size_t pos = 0;
	while (pos <= blob.size() && !blob.empty())
	{
		const size_t rec = blob.find(REC, pos);
		const std::string one = blob.substr(pos, rec == std::string::npos
				? std::string::npos : rec - pos);
		pos = rec == std::string::npos ? blob.size() + 1 : rec + 1;
		const size_t fld = one.find(FLD);
		if (fld == std::string::npos)
			continue;
		at_[(u32)strtoul(one.substr(0, fld).c_str(), nullptr, 10)] = one.substr(fld + 1);
	}
}

void marksInstall()
{
	remapRegister([](const Remap& m) { theMarks.remap(m); });
	metaRegister("marks",
			[]() { return theMarks.serialise(); },
			[](const std::string& blob) { theMarks.load(blob); });
	metaInstall();
}

/*
	SELF-TEST. The remap claims are the point; the rest is bookkeeping that would
	be obvious if it broke.
*/
void marksSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLMARKS SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	Marks m;
	m.toggle(10);
	claim("toggle adds an unnamed mark", m.has(10) && m.labelAt(10) != nullptr
			&& m.labelAt(10)[0] == 0);
	// AN EMPTY LABEL IS NOT THE ABSENCE OF A MARK, which is why labelAt returns
	// a pointer rather than a string.
	claim("...and an unmarked frame answers nullptr, not an empty label",
			m.labelAt(11) == nullptr);
	m.toggle(10);
	claim("toggle removes it again", !m.has(10) && m.count() == 0);

	m.set(10, "start"); m.set(40, "combo"); m.set(90, "");
	claim("three marks, one unnamed", m.count() == 3);

	u32 to = 0;
	claim("next finds the mark STRICTLY after", m.next(10, to) && to == 40);
	claim("prev finds the mark STRICTLY before", m.prev(40, to) && to == 10);
	claim("next past the last answers false", !m.next(90, to));
	claim("prev before the first answers false", !m.prev(10, to));
	claim("next from between marks lands on the next one", m.next(11, to) && to == 40);

	// ---- THE REMAP, which is why this module exists ----
	{
		Marks k = m;
		k.remap(Remap::inserted(20, 5, 200));
		claim("an insert moves marks at and above it", k.has(45) && k.has(95));
		// THE CLAIM THE FORK FAILS at two of its five hand-written sites.
		claim("...and leaves marks below it alone", k.has(10) && !k.has(15));
		claim("labels travel with their frame",
				k.labelAt(45) != nullptr && std::string(k.labelAt(45)) == "combo");
	}
	{
		Marks k = m;
		k.remap(Remap::deleted({ 40 }, 200));
		claim("a mark on a deleted frame is DROPPED, not slid onto its neighbour",
				k.count() == 2 && !k.has(39) && !k.has(40));
		claim("...while marks after it pull up with the tail", k.has(89));
		claim("...and marks before it do not move", k.has(10));
	}
	{
		Marks k = m;
		k.remap(Remap::identity());
		claim("identity leaves them alone", k.count() == 3 && k.has(40));
	}

	// ---- the blob, which rides the undo stack ----
	{
		Marks k;
		k.set(7, "a label with ; : , = and spaces");
		Marks j;
		j.load(k.serialise());
		claim("a label with punctuation survives the round trip",
				j.labelAt(7) != nullptr
				&& std::string(j.labelAt(7)) == "a label with ; : , = and spaces");
		Marks e;
		e.set(1, "x");
		e.load("");
		claim("loading an empty blob clears rather than keeping stale marks", e.count() == 0);
	}

	NOTICE_LOG(RENDERER, "ROLLMARKS SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
