/*
	Copyright 2026 flycast-dojo contributors

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "video_recorder.h"
#include "cfg/cfg.h"
#include "stdclass.h"
#include "rend/gui.h"

#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

#ifdef _WIN32
#define REC_POPEN  _popen
#define REC_PCLOSE _pclose
// Binary mode matters on Windows: without it the CRT translates 0x0A bytes
// inside the pixel data into CRLF and corrupts every frame.
#define REC_MODE   "wb"
#else
#define REC_POPEN  popen
#define REC_PCLOSE pclose
#define REC_MODE   "w"
#endif

namespace videorec
{

// Bound the queue so a slow encoder costs dropped frames rather than
// unbounded memory. 6 frames at 1080p RGB24 is ~37 MB.
static constexpr size_t MaxQueuedFrames = 6;

static std::mutex mutex;
static std::condition_variable cond;
static std::deque<std::vector<u8>> queue;
static std::thread writerThread;

static FILE *pipeFile;
static std::atomic<bool> recording{false};
static std::atomic<bool> writerRunning{false};
static std::atomic<u64> written{0};
static std::atomic<u64> dropped{0};

static int recWidth;
static int recHeight;
static PixelFormat recFormat = PixelFormat::RGB24;
static std::string recPath;

static std::mutex requestMutex;
static bool pendingStart;
static bool pendingStop;
static std::string pendingPath;

// ---------------------------------------------------------------------------

static std::string defaultOutputPath()
{
	char name[128];
	std::time_t now = std::time(nullptr);
	std::tm tm_{};
#ifdef _WIN32
	localtime_s(&tm_, &now);
#else
	localtime_r(&now, &tm_);
#endif
	std::strftime(name, sizeof(name), "flycast-%Y%m%d-%H%M%S.avi", &tm_);
	return get_writable_data_path(name);
}

int bytesPerPixel(PixelFormat format)
{
	return format == PixelFormat::RGB24 ? 3 : 4;
}

static const char *ffmpegPixFmt(PixelFormat format)
{
	switch (format)
	{
	case PixelFormat::RGBA32: return "rgba";
	case PixelFormat::BGRA32: return "bgra";
	case PixelFormat::RGB24:
	default:                  return "rgb24";
	}
}

static std::string buildCommand(const std::string& path, int width, int height,
		PixelFormat format, bool flipVertically)
{
	// All of these are overridable in emu.cfg under [record] so a capture can
	// be retargeted (different codec, container, or an ffmpeg off $PATH)
	// without a rebuild.
	const std::string exe     = cfgLoadStr("record", "ffmpeg", "ffmpeg");
	const std::string codec   = cfgLoadStr("record", "codec", "mjpeg");
	const std::string quality = cfgLoadStr("record", "quality", "3");
	const int fps             = cfgLoadInt("record", "fps", 60);

	char cmd[1024];
	// vflip is only needed where the framebuffer origin is bottom-left (OpenGL);
	// D3D and Vulkan already hand over top-down rows.
	snprintf(cmd, sizeof(cmd),
			"\"%s\" -hide_banner -loglevel error -y "
			"-f rawvideo -pix_fmt %s -video_size %dx%d -framerate %d -i - "
			"%s-c:v %s -q:v %s \"%s\"",
			exe.c_str(), ffmpegPixFmt(format), width, height, fps,
			flipVertically ? "-vf vflip " : "",
			codec.c_str(), quality.c_str(), path.c_str());
	return std::string(cmd);
}

static void writerMain()
{
	while (true)
	{
		std::vector<u8> frame;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cond.wait(lock, [] { return !queue.empty() || !writerRunning; });
			if (queue.empty() && !writerRunning)
				break;
			frame = std::move(queue.front());
			queue.pop_front();
		}
		if (pipeFile != nullptr && !frame.empty())
		{
			if (std::fwrite(frame.data(), 1, frame.size(), pipeFile) != frame.size())
			{
				ERROR_LOG(COMMON, "[rec] write to encoder failed, stopping capture");
				// Let the render thread observe this and shut down cleanly.
				recording = false;
				requestStop();
			}
			else
				written++;
		}
	}
}

// ---------------------------------------------------------------------------

void requestStart(const std::string& path)
{
	std::lock_guard<std::mutex> lock(requestMutex);
	pendingPath = path;
	pendingStart = true;
	pendingStop = false;
}

void requestStop()
{
	std::lock_guard<std::mutex> lock(requestMutex);
	pendingStop = true;
	pendingStart = false;
}

void toggle()
{
	if (isRecording())
		requestStop();
	else
		requestStart("");
}

bool startPending()
{
	std::lock_guard<std::mutex> lock(requestMutex);
	return pendingStart;
}

bool stopPending()
{
	std::lock_guard<std::mutex> lock(requestMutex);
	return pendingStop;
}

bool start(int width, int height, PixelFormat format, bool flipVertically)
{
	std::string path;
	{
		std::lock_guard<std::mutex> lock(requestMutex);
		pendingStart = false;
		path = pendingPath;
		pendingPath.clear();
	}
	if (recording)
		return true;
	if (width <= 0 || height <= 0)
	{
		ERROR_LOG(COMMON, "[rec] refusing to record a %dx%d framebuffer", width, height);
		return false;
	}
	if (path.empty())
		path = defaultOutputPath();

	const std::string cmd = buildCommand(path, width, height, format, flipVertically);
	INFO_LOG(COMMON, "[rec] %s", cmd.c_str());

	pipeFile = REC_POPEN(cmd.c_str(), REC_MODE);
	if (pipeFile == nullptr)
	{
		ERROR_LOG(COMMON, "[rec] could not launch encoder - is ffmpeg on PATH?");
		gui_display_notification("Recording failed: ffmpeg not found", 5000);
		return false;
	}

	recWidth = width;
	recHeight = height;
	recFormat = format;
	recPath = path;
	written = 0;
	dropped = 0;
	recording = true;
	writerRunning = true;
	writerThread = std::thread(writerMain);

	INFO_LOG(COMMON, "[rec] recording %dx%d to %s", width, height, path.c_str());
	gui_display_notification("Recording started", 2000);
	return true;
}

void stop()
{
	{
		std::lock_guard<std::mutex> lock(requestMutex);
		pendingStop = false;
	}
	if (!recording && !writerRunning)
		return;

	recording = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		writerRunning = false;
	}
	cond.notify_all();
	if (writerThread.joinable())
		writerThread.join();

	if (pipeFile != nullptr)
	{
		REC_PCLOSE(pipeFile);
		pipeFile = nullptr;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
	}

	INFO_LOG(COMMON, "[rec] stopped: %llu frames written, %llu dropped -> %s",
			(unsigned long long)written.load(), (unsigned long long)dropped.load(),
			recPath.c_str());

	char msg[256];
	snprintf(msg, sizeof(msg), "Recording saved (%llu frames%s)",
			(unsigned long long)written.load(),
			dropped > 0 ? ", frames dropped" : "");
	gui_display_notification(msg, 4000);
}

bool isRecording() { return recording; }
int width() { return recWidth; }
int height() { return recHeight; }
size_t frameBytes() { return (size_t)recWidth * recHeight * bytesPerPixel(recFormat); }

void submitFrame(std::vector<u8>&& rgb)
{
	if (!recording)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (queue.size() >= MaxQueuedFrames)
		{
			// Encoder is behind. Drop the newest frame rather than stalling the
			// render thread - frame pacing matters more than a complete capture,
			// especially during a rollback session.
			dropped++;
			return;
		}
		queue.push_back(std::move(rgb));
	}
	cond.notify_one();
}

u64 framesWritten() { return written; }
u64 framesDropped() { return dropped; }
const std::string& outputPath() { return recPath; }

std::string status()
{
	if (!recording)
		return "Not recording";
	char buf[256];
	snprintf(buf, sizeof(buf), "Recording %dx%d - %llu frames, %llu dropped",
			recWidth, recHeight,
			(unsigned long long)written.load(), (unsigned long long)dropped.load());
	return std::string(buf);
}

}
