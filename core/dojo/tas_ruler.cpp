#include "tas_ruler.h"
#include "mvc2.h"
#include "emulator.h"
#include "log/Log.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>

namespace tas_ruler
{

static const char MAGIC[8] = { 'T', 'A', 'S', 'S', 'K', 'I', 'P', '1' };

static std::mutex mtx;					// guards store / seen (the emu loop writes once per frame, the GUI reads)
static std::vector<SkipSample> store;	// index = movie frame
static u32 seen = 0;
static u32 version_ = 0;
static u32 polls = 0;

void onPoll(u32 frame)
{
	if (!emu.running())
		return;
	const tas_mvc2::GameState gs = tas_mvc2::read();	// rate + count (a handful of byte reads)
	SkipSample s;
	s.seen = 1;
	s.skip = tas_mvc2::readSkipToggle() == 255 ? 1 : 0;
	s.count = gs.skipCount;
	s.rate = gs.skipRate;
	u32 nowSeen;
	{
		std::lock_guard<std::mutex> lk(mtx);
		if (store.size() <= frame)
			store.resize(frame + 1);
		if (!store[frame].seen)
			seen++;
		store[frame] = s;
		nowSeen = seen;
		version_++;
	}
	polls++;
	if (polls <= 3 || polls % 600 == 0)	// instrumentation: the measured cadence, throttled
		NOTICE_LOG(NETWORK, "TAS SKIPMAP: frame %u rate %u count %u %s (%u frames seen)",
				frame, s.rate, s.count, s.skip ? "SKIP" : "-", nowSeen);
}

// The frame the roll is paused ON has no poll yet (its poll runs when it executes), but the Timeline / Input Viz
// SKIP badge already shows the RAM at the pause point - the very bytes that poll will read. Peek them from the GUI
// thread and file them under that frame, so the playhead row agrees with the badge (David: the discrepancy); the
// poll that follows overwrites the slot with the same reading.
void onPausePeek(u32 frame)
{
	u8 rate, count, toggle;
	if (!tas_mvc2::peekSkip(rate, count, toggle))
		return;
	SkipSample s;
	s.seen = 1;
	s.skip = toggle == 255 ? 1 : 0;
	s.count = count;
	s.rate = rate;
	std::lock_guard<std::mutex> lk(mtx);
	if (store.size() <= frame)
		store.resize(frame + 1);
	if (!store[frame].seen)
		seen++;
	store[frame] = s;
	version_++;
}

void eraseFrom(u32 frame)
{
	std::lock_guard<std::mutex> lk(mtx);
	if (frame >= store.size())
		return;
	for (size_t i = frame; i < store.size(); i++)
		if (store[i].seen)
			seen--;
	store.resize(frame);
	version_++;
}

void reset()
{
	std::lock_guard<std::mutex> lk(mtx);
	store.clear();
	seen = 0;
	polls = 0;
	version_++;
}

void snapshot(u32 lo, u32 hi, std::vector<SkipSample>& out)
{
	out.clear();
	if (hi <= lo)
		return;
	out.resize(hi - lo);
	std::lock_guard<std::mutex> lk(mtx);
	const u32 end = std::min<u32>(hi, (u32)store.size());
	for (u32 f = lo; f < end; f++)
		out[f - lo] = store[f];
}

u32 seenFrames()
{
	return seen;
}

u32 version()
{
	return version_;
}

// ---- persistence: TASSKIP1 | u32 count | count x { u32 frame, u8 skip, u8 count, u8 rate } ----

bool saveClip(const std::string& dir)
{
	if (dir.empty() || seen == 0)
		return false;
	std::vector<std::pair<u32, SkipSample>> rows;
	{
		std::lock_guard<std::mutex> lk(mtx);
		for (u32 f = 0; f < (u32)store.size(); f++)
			if (store[f].seen)
				rows.emplace_back(f, store[f]);
	}
	const std::string path = dir + "/skip.map";
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.good())
	{
		NOTICE_LOG(NETWORK, "TAS SKIPMAP: FAILED to save %s", path.c_str());
		return false;
	}
	out.write(MAGIC, 8);
	const u32 n = (u32)rows.size();
	out.write((const char *)&n, 4);
	for (const auto& kv : rows)
	{
		out.write((const char *)&kv.first, 4);
		out.write((const char *)&kv.second.skip, 1);
		out.write((const char *)&kv.second.count, 1);
		out.write((const char *)&kv.second.rate, 1);
	}
	NOTICE_LOG(NETWORK, "TAS SKIPMAP: saved %u frame(s) -> %s", n, path.c_str());
	return out.good();
}

bool loadClip(const std::string& dir)
{
	if (dir.empty())
		return false;
	const std::string path = dir + "/skip.map";
	std::ifstream in(path, std::ios::binary);
	if (!in.good())
		return false;
	char magic[8];
	in.read(magic, 8);
	if (!in.good() || memcmp(magic, MAGIC, 8) != 0)
		return false;
	u32 n = 0;
	in.read((char *)&n, 4);
	if (!in.good() || n > 4000000)
		return false;
	std::vector<std::pair<u32, SkipSample>> rows;
	rows.reserve(n);
	for (u32 i = 0; i < n; i++)
	{
		u32 f;
		SkipSample s;
		s.seen = 1;
		in.read((char *)&f, 4);
		in.read((char *)&s.skip, 1);
		in.read((char *)&s.count, 1);
		in.read((char *)&s.rate, 1);
		if (!in.good())
			break;
		rows.emplace_back(f, s);
	}
	{
		std::lock_guard<std::mutex> lk(mtx);
		store.clear();
		seen = 0;
		for (const auto& kv : rows)
		{
			if (store.size() <= kv.first)
				store.resize(kv.first + 1);
			if (!store[kv.first].seen)
				seen++;
			store[kv.first] = kv.second;
		}
		version_++;
	}
	NOTICE_LOG(NETWORK, "TAS SKIPMAP: loaded %u frame(s) from %s", (u32)rows.size(), path.c_str());
	return true;
}

}
