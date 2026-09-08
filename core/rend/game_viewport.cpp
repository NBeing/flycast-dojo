#include "game_viewport.h"
#include "rend/transform_matrix.h"	// getDCFramebufferAspectRatio
#include "cfg/option.h"
#include <atomic>
#include <cmath>

namespace rend
{

/*
	Packed into one atomic so a reader never sees half of an update. The UI
	writes this from the render thread; the Lua API reads it from the emulation
	thread. Four uint16s cover any window a Dreamcast emulator is presented in
	(65535px), and the content area is never negative, so the packing is lossless
	in practice - and a torn read would only ever be one frame of a stale
	rectangle anyway, which is why this needs no lock.
*/
static std::atomic<uint64_t> packedContentArea{0};	// 0 = no reservation

static inline uint64_t pack(int x, int y, int w, int h)
{
	auto clamp16 = [](int v) -> uint64_t {
		if (v < 0) v = 0;
		if (v > 65535) v = 65535;
		return (uint64_t)v;
	};
	return (clamp16(x) << 48) | (clamp16(y) << 32) | (clamp16(w) << 16) | clamp16(h);
}

void setContentArea(int x, int y, int w, int h)
{
	packedContentArea.store(w > 0 && h > 0 ? pack(x, y, w, h) : 0, std::memory_order_relaxed);
}

ViewportRect contentArea()
{
	const int winW = settings.display.width;
	const int winH = settings.display.height;

	const uint64_t p = packedContentArea.load(std::memory_order_relaxed);
	if (p == 0)
		return { 0, 0, winW, winH };

	ViewportRect r {
		(int)((p >> 48) & 0xFFFF),
		(int)((p >> 32) & 0xFFFF),
		(int)((p >> 16) & 0xFFFF),
		(int)(p & 0xFFFF),
	};

	// The publisher and the window can disagree for a frame after a resize.
	// Clip rather than trust: a viewport outside the window would scissor the
	// picture away entirely, which looks like a renderer bug.
	if (r.x >= winW || r.y >= winH)
		return { 0, 0, winW, winH };
	if (r.x + r.w > winW)
		r.w = winW - r.x;
	if (r.y + r.h > winH)
		r.h = winH - r.y;
	if (r.w <= 0 || r.h <= 0)
		return { 0, 0, winW, winH };
	return r;
}

ViewportRect gameViewport(float pictureAspectRatio)
{
	ViewportRect area = contentArea();
	if (area.h <= 0 || pictureAspectRatio <= 0.f)
		return area;

	const float areaAR = (float)area.w / area.h;

	int dx = 0;
	int dy = 0;
	if (pictureAspectRatio > areaAR)
		dy = (int)roundf(area.h * (1 - areaAR / pictureAspectRatio) / 2.f);
	else
		dx = (int)roundf(area.w * (1 - pictureAspectRatio / areaAR) / 2.f);

	return { area.x + dx, area.y + dy, area.w - dx * 2, area.h - dy * 2 };
}

ViewportRect gameViewport()
{
	return gameViewport(getDCFramebufferAspectRatio());
}

}	// namespace rend
