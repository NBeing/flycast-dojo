#include "thumbnail.h"
#include "cfg/cfg.h"
#include "hw/pvr/Renderer_if.h"
#include "log/Log.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

// stb_image_write's implementation is already compiled into the binary (CustomTexture.cpp defines
// STB_IMAGE_WRITE_IMPLEMENTATION), so this only needs the declarations.
#include <stb_image_write.h>

namespace tas_thumb
{

static std::thread writer;
static std::mutex writerMutex;

// A std::thread that is still joinable when its destructor runs calls std::terminate. This guard is
// constructed after `writer`, so it is destroyed *before* it at exit and always joins first - that
// keeps a savestate taken a moment before quitting from turning into a crash on shutdown.
static struct WriterGuard {
	~WriterGuard() { flush(); }
} writerGuard;

// Box-filter downscale of a top-down RGB image. Averaging (rather than nearest) matters here:
// the source is a 3840x2880 render being reduced ~12x, and point sampling turns HUD text and
// sprite edges into aliased noise.
static void downscale(const std::vector<u8>& src, int sw, int sh, std::vector<u8>& dst, int dw, int dh)
{
	dst.assign((size_t)dw * dh * 3, 0);
	for (int y = 0; y < dh; y++)
	{
		int y0 = (int)((int64_t)y * sh / dh);
		int y1 = (int)((int64_t)(y + 1) * sh / dh);
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < dw; x++)
		{
			int x0 = (int)((int64_t)x * sw / dw);
			int x1 = (int)((int64_t)(x + 1) * sw / dw);
			if (x1 <= x0)
				x1 = x0 + 1;
			u32 r = 0, g = 0, b = 0, n = 0;
			for (int sy = y0; sy < y1 && sy < sh; sy++)
			{
				const u8 *row = &src[((size_t)sy * sw) * 3];
				for (int sx = x0; sx < x1 && sx < sw; sx++)
				{
					r += row[sx * 3 + 0];
					g += row[sx * 3 + 1];
					b += row[sx * 3 + 2];
					n++;
				}
			}
			u8 *d = &dst[((size_t)y * dw + x) * 3];
			d[0] = (u8)(r / (n ? n : 1));
			d[1] = (u8)(g / (n ? n : 1));
			d[2] = (u8)(b / (n ? n : 1));
		}
	}
}

void flush()
{
	std::lock_guard<std::mutex> lock(writerMutex);
	if (writer.joinable())
		writer.join();
}

void captureForState(const std::string& statePath)
{
	if (!cfgLoadBool("dojo", "StateThumbnails", true))
		return;
	if (renderer == nullptr || statePath.empty())
		return;

	auto t0 = std::chrono::steady_clock::now();
	std::vector<u8> full;
	int fw = 0, fh = 0;
	if (!renderer->GetLastFrameRGB(full, fw, fh) || fw <= 0 || fh <= 0)
		return;		// renderer has no readback path (GL/Vulkan) or nothing rendered yet

	// Keep the source aspect - Widescreen/Rotate90 change it, and a fixed box would stretch.
	int tw = cfgLoadInt("dojo", "ThumbnailWidth", 320);
	if (tw < 64)
		tw = 64;
	if (tw > fw)
		tw = fw;
	int th = (int)((int64_t)fh * tw / fw);
	if (th < 1)
		th = 1;

	std::vector<u8> small;
	downscale(full, fw, fh, small, tw, th);
	auto t1 = std::chrono::steady_clock::now();

	// PNG encoding is the slow part - hand it to a worker so the render thread returns now.
	{
		std::lock_guard<std::mutex> lock(writerMutex);
		if (writer.joinable())
			writer.join();		// at most one encode in flight; saves are seconds apart
		std::string out = statePath + ".png";
		writer = std::thread([out, small = std::move(small), tw, th]() {
			stbi_write_png(out.c_str(), tw, th, 3, small.data(), tw * 3);
		});
	}
	double readMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
	NOTICE_LOG(COMMON, "TAS thumb: %dx%d -> %dx%d in %.1f ms (png async)", fw, fh, tw, th, readMs);
}

}
