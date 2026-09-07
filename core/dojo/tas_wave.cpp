#include "tas_wave.h"
#include "dojo.h"			// dojo.frame_number - the movie frame the emulator loop is running
#include "log/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>

namespace tas_wave
{

static constexpr u32 SAMPLES_PER_FRAME = 736;	// 44100 Hz / 59.94 Hz - the bucket ruler, not a hard limit
static constexpr u32 MAX_SCRATCH = 2048;		// the pre-movie boot sits at frame 0 for thousands of frames - cap it
static constexpr u32 SNAP_FRAMES = 300;			// per-state snapshot: the ~5 s before the state
static const char MAGIC[8] = { 'T', 'A', 'S', 'W', 'A', 'V', 'E', '1' };

static std::mutex mtx;				// guards store / measured (the emu loop writes once per frame, the GUI reads)
static std::vector<FrameEnv> store;	// index = movie frame
static u32 measured = 0;
static u32 version_ = 0;	// bumps on every store change (publish / stale / reset / load) - the GUI lane caches on it

// The frame in progress - emulator loop only, no lock.
static u32 scratchFrame = 0;
static u32 scratchN = 0;
static u32 scratchPeak[BUCKETS];
static u64 scratchSq[BUCKETS];
static u32 scratchCnt[BUCKETS];
static u32 published = 0;

static void scratchClear(u32 frame)
{
	scratchFrame = frame;
	scratchN = 0;
	memset(scratchPeak, 0, sizeof(scratchPeak));
	memset(scratchSq, 0, sizeof(scratchSq));
	memset(scratchCnt, 0, sizeof(scratchCnt));
}

// Fold the scratch into the store as scratchFrame's envelope and move the scratch to the next frame.
static void publish()
{
	if (scratchN == 0)
		return;
	FrameEnv e;
	e.flag = 1;
	u32 framePeak = 0;
	for (int b = 0; b < BUCKETS; b++)
	{
		e.peak[b] = (u8)std::min<u32>(255, scratchPeak[b] >> 7);	// |s16| 0..32767 -> 0..255
		const u32 rms = scratchCnt[b] ? (u32)std::sqrt((double)scratchSq[b] / scratchCnt[b]) : 0;
		e.rms[b] = (u8)std::min<u32>(255, rms >> 7);
		framePeak = std::max(framePeak, (u32)e.peak[b]);
	}
	u32 nowMeasured;
	{
		std::lock_guard<std::mutex> lk(mtx);
		if (store.size() <= scratchFrame)
			store.resize(scratchFrame + 1);
		if (store[scratchFrame].flag == 0)
			measured++;
		store[scratchFrame] = e;
		nowMeasured = measured;
		version_++;
	}
	published++;
	if (published <= 3 || published % 600 == 0)	// instrumentation: the measured behavior, throttled
		NOTICE_LOG(AUDIO, "TAS WAVE: frame %u - %u samples, peak %u/255 (%u frames measured)",
				scratchFrame, scratchN, framePeak, nowMeasured);
	scratchClear(scratchFrame + 1);
}

void onSample(s16 l, s16 r)
{
	const u32 f = dojo.frame_number.load(std::memory_order_relaxed);
	if (f != scratchFrame)
		scratchClear(f);	// the counter moved without onFrameEnd (seek / netplay path): the scratch belongs to a frame no longer running
	if (scratchN >= MAX_SCRATCH)
		return;
	const int b = std::min(BUCKETS - 1, (int)(scratchN * BUCKETS / SAMPLES_PER_FRAME));
	const s32 m = ((s32)l + (s32)r) / 2;
	const u32 a = (u32)(m < 0 ? -m : m);
	if (a > scratchPeak[b])
		scratchPeak[b] = a;
	scratchSq[b] += (u64)a * a;
	scratchCnt[b]++;
	scratchN++;
}

void onFrameEnd(u32 frame)
{
	if (scratchFrame != frame)
	{	// desynced (should not happen): drop rather than file this audio under the wrong frame
		scratchClear(frame + 1);
		return;
	}
	publish();
}

void onStateLoad(u32 frame)
{
	{
		std::lock_guard<std::mutex> lk(mtx);
		for (size_t i = frame; i < store.size(); i++)
			if (store[i].flag == 1)
				store[i].flag = 2;
		version_++;
	}
	scratchClear(frame);	// the emulator is stopped during a load - the scratch is idle
}

void markStaleFrom(u32 frame)
{
	std::lock_guard<std::mutex> lk(mtx);
	for (size_t i = frame; i < store.size(); i++)
		if (store[i].flag == 1)
			store[i].flag = 2;
	version_++;
}

void reset()
{
	{
		std::lock_guard<std::mutex> lk(mtx);
		store.clear();
		measured = 0;
		version_++;
	}
	published = 0;
	scratchClear(0);
}

void snapshot(u32 lo, u32 hi, std::vector<FrameEnv>& out)
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

u32 measuredFrames()
{
	return measured;
}

u32 version()
{
	return version_;
}

// ---- persistence: TASWAVE1 | u32 count | count x { u32 frame, u8 flag, u8 peak[16], u8 rms[16] } ----

static bool writeRange(const std::string& path, u32 lo, u32 hi)
{
	std::vector<std::pair<u32, FrameEnv>> rows;
	{
		std::lock_guard<std::mutex> lk(mtx);
		const u32 end = std::min<u32>(hi, (u32)store.size());
		for (u32 f = lo; f < end; f++)
			if (store[f].flag != 0)
				rows.emplace_back(f, store[f]);
	}
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.good())
		return false;
	out.write(MAGIC, 8);
	const u32 n = (u32)rows.size();
	out.write((const char *)&n, 4);
	for (const auto& kv : rows)
	{
		out.write((const char *)&kv.first, 4);
		out.write((const char *)&kv.second.flag, 1);
		out.write((const char *)kv.second.peak, BUCKETS);
		out.write((const char *)kv.second.rms, BUCKETS);
	}
	return out.good();
}

bool readFile(const std::string& path, std::vector<std::pair<u32, FrameEnv>>& out)
{
	out.clear();
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
	out.reserve(n);
	for (u32 i = 0; i < n; i++)
	{
		u32 f;
		FrameEnv e;
		in.read((char *)&f, 4);
		in.read((char *)&e.flag, 1);
		in.read((char *)e.peak, BUCKETS);
		in.read((char *)e.rms, BUCKETS);
		if (!in.good())
			break;
		out.emplace_back(f, e);
	}
	std::sort(out.begin(), out.end(), [](const std::pair<u32, FrameEnv>& a, const std::pair<u32, FrameEnv>& b) { return a.first < b.first; });
	return true;
}

bool saveClip(const std::string& dir)
{
	if (dir.empty() || measured == 0)
		return false;
	const bool ok = writeRange(dir + "/audio.env", 0, ~0u);
	NOTICE_LOG(AUDIO, "TAS WAVE: %s %u measured frame(s) -> %s/audio.env", ok ? "saved" : "FAILED to save", measured, dir.c_str());
	return ok;
}

bool loadClip(const std::string& dir)
{
	std::vector<std::pair<u32, FrameEnv>> rows;
	if (dir.empty() || !readFile(dir + "/audio.env", rows))
		return false;
	{
		std::lock_guard<std::mutex> lk(mtx);
		store.clear();
		measured = 0;
		version_++;
		for (const auto& kv : rows)
		{
			if (kv.second.flag == 0)
				continue;
			if (store.size() <= kv.first)
				store.resize(kv.first + 1);
			if (store[kv.first].flag == 0)
				measured++;
			store[kv.first] = kv.second;
		}
	}
	NOTICE_LOG(AUDIO, "TAS WAVE: loaded %u frame(s) from %s/audio.env", (u32)rows.size(), dir.c_str());
	return true;
}

bool writeStateSnapshot(const std::string& statePath, u32 frame)
{
	if (statePath.empty() || measured == 0)
		return false;
	const u32 lo = frame > SNAP_FRAMES ? frame - SNAP_FRAMES : 0;
	return writeRange(statePath + ".wave", lo, frame + 1);
}

}
