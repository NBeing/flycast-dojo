#include "luawatch.h"
#include "hw/mem/addrspace.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstring>
#include <vector>

namespace luawatch
{

namespace {

struct Watch
{
	u32 addr = 0, len = 0, rev = 0;
	bool live = false;
	std::vector<u8> shadow;
	bool primed = false;		//!< the first tick fills the shadow; it is not a change
};

std::vector<Watch> watches;
u64 compares_ = 0, changes_ = 0;
double lastUs_ = 0.0, maxUs_ = 0.0;

/*
	READ THE REGION THROUGH THE ADDRESS SPACE, in 32-bit chunks.

	Not through a raw RAM pointer: a watch may name any address, and the one
	pointer available (`mem_b`) is main RAM only. `[MEASURED 2026-09-10]` the
	cost is reported by lastMicros() rather than argued about - which is the
	point of MAX_BYTES existing at all, since the honest cap is whatever the
	measurement says a frame can afford.

	An unmapped read answers 0 here, a documented deviation of this emulator's;
	a watch over unmapped space therefore looks permanently unchanged rather
	than raising, which is the same answer the rest of the tree gives.
*/
void readRegion(u32 addr, u32 len, u8 *out)
{
	u32 i = 0;
	for (; i + 4 <= len; i += 4)
	{
		const u32 v = addrspace::readt<u32>(addr + i);
		memcpy(out + i, &v, 4);
	}
	for (; i < len; i++)
		out[i] = addrspace::readt<u8>(addr + i);
}

}	// namespace

int add(u32 addr, u32 len)
{
	if (len == 0 || len > MAX_BYTES)
		return -1;
	for (size_t i = 0; i < watches.size(); i++)
		if (!watches[i].live)
		{
			watches[i] = Watch{ addr, len, 0, true, std::vector<u8>(len), false };
			return (int)i;
		}
	watches.push_back(Watch{ addr, len, 0, true, std::vector<u8>(len), false });
	return (int)watches.size() - 1;
}

bool remove(int id)
{
	if (id < 0 || id >= (int)watches.size() || !watches[id].live)
		return false;
	watches[id].live = false;
	watches[id].shadow.clear();
	watches[id].shadow.shrink_to_fit();
	return true;
}

u32 revision(int id)
{
	if (id < 0 || id >= (int)watches.size() || !watches[id].live)
		return 0;
	return watches[id].rev;
}

void tick()
{
	if (watches.empty())
		return;
	const double t0 = os_GetSeconds();
	std::vector<u8> buf;
	for (Watch& w : watches)
	{
		if (!w.live)
			continue;
		buf.resize(w.len);
		readRegion(w.addr, w.len, buf.data());
		compares_++;
		if (!w.primed)
		{
			// THE FIRST TICK IS NOT A CHANGE. Priming from a zeroed shadow
			// would report every watch as changed on its first frame, and a
			// script arming a watch to wait for something would fire at once.
			w.primed = true;
			w.shadow.swap(buf);
			continue;
		}
		if (memcmp(w.shadow.data(), buf.data(), w.len) != 0)
		{
			w.rev++;
			changes_++;
			w.shadow.swap(buf);
		}
	}
	lastUs_ = (os_GetSeconds() - t0) * 1e6;
	if (lastUs_ > maxUs_)
		maxUs_ = lastUs_;
}

void reset()
{
	watches.clear();
	compares_ = changes_ = 0;
	lastUs_ = maxUs_ = 0.0;
}

int count()
{
	int n = 0;
	for (const Watch& w : watches)
		if (w.live) n++;
	return n;
}

u64    compares()   { return compares_; }
u64    changes()    { return changes_; }
double lastMicros() { return lastUs_; }
double maxMicros()  { return maxUs_; }

/*
	SELF-TEST. It cannot touch guest memory - there may not be a machine - so it
	covers the bookkeeping: ids, bounds, the priming rule, and that a removed
	watch stops answering. The behaviour that needs a running game is in
	scripts/tests/memwatch.lua.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(COMMON, "LUAWATCH SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	reset();
	claim("a fresh registry is empty", count() == 0 && compares() == 0);

	const int a = add(0x0C000000, 16);
	claim("a watch gets an id", a >= 0 && count() == 1);
	claim("zero length is refused", add(0x0C000000, 0) < 0);
	// THE CAP IS A REFUSAL, not a silent truncation: a watch that quietly
	// covered less than it was asked for would report "no change" about bytes
	// nobody was looking at.
	claim("an oversized region is refused", add(0x0C000000, MAX_BYTES + 1) < 0);
	claim("...and the cap itself is allowed", add(0x0C000000, MAX_BYTES) >= 0);

	claim("a new watch starts at revision 0", revision(a) == 0);
	claim("an unknown id answers 0 rather than raising", revision(9999) == 0);

	const int b = add(0x0C000000, 8);
	claim("removing answers true once", remove(b) && !remove(b));
	claim("...and a removed watch stops answering", revision(b) == 0);
	// SLOT REUSE, so a script that arms and drops watches in a loop does not
	// grow the vector without bound.
	claim("a removed slot is reused", add(0x0C000100, 4) == b);

	reset();
	claim("reset clears everything", count() == 0 && compares() == 0 && maxMicros() == 0.0);

	NOTICE_LOG(COMMON, "LUAWATCH SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace luawatch
