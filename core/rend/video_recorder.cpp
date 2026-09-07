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
static std::condition_variable spaceCond;
static std::deque<std::vector<u8>> queue;
static std::deque<std::vector<s16>> audioQueue;
static std::thread writerThread;

static FILE *pipeFile;
static FILE *pcmFile;
// Paths: video is encoded to a temp file and muxed with the captured PCM into
// recPath when the capture stops.
static std::string videoTmpPath;
static std::string pcmTmpPath;
static bool blockOnFull;
static int recFps = 60;

// Written by the writer thread only.
static u64 videoFramesWritten;
static u64 audioFramesWritten;
static u64 pendingDuplicates;
static std::vector<u8> lastFrame;
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

// Inserts a suffix before the extension, so the temp video keeps the final
// file's container ("out.avi" -> "out.video.avi") and ffmpeg can stream-copy
// it during the mux instead of re-encoding.
static std::string insertSuffix(const std::string& path, const char *suffix)
{
	const size_t slash = path.find_last_of("/\\");
	const size_t dot = path.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return path + suffix;
	return path.substr(0, dot) + suffix + path.substr(dot);
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

static bool writeFrameToPipe(const std::vector<u8>& frame)
{
	if (pipeFile == nullptr || frame.empty())
		return false;
	if (std::fwrite(frame.data(), 1, frame.size(), pipeFile) != frame.size())
	{
		ERROR_LOG(COMMON, "[rec] write to encoder failed, stopping capture");
		// Let the render thread observe this and shut down cleanly.
		recording = false;
		requestStop();
		return false;
	}
	videoFramesWritten++;
	written++;
	return true;
}

// Keeps the audio track anchored to the video timeline.
//
// Audio is only produced while the emulator is running, so any stretch with no
// emulation - sitting in the menu, paused, fast-forwarding - leaves a hole. If
// those holes are not filled, everything after them plays early.
//
// Only large gaps are filled. Audio and video legitimately lead and lag each
// other by a few milliseconds, and padding that jitter would insert clicks and
// accumulate drift.
static void padAudioToVideo()
{
	if (pcmFile == nullptr || audioFramesWritten == 0)
		return;
	const u64 expected = videoFramesWritten * (u64)AudioSampleRate / (u64)std::max(1, recFps);
	const u64 tolerance = AudioSampleRate / 4;	// 250 ms
	if (audioFramesWritten + tolerance >= expected)
		return;

	u64 missing = expected - audioFramesWritten;
	static const std::vector<s16> silence(2048 * AudioChannels, 0);
	while (missing > 0)
	{
		const u64 chunk = std::min<u64>(missing, 2048);
		std::fwrite(silence.data(), sizeof(s16) * AudioChannels, chunk, pcmFile);
		audioFramesWritten += chunk;
		missing -= chunk;
	}
}

static void drainAudioLocked(std::deque<std::vector<s16>>& pending)
{
	for (const auto& chunk : pending)
	{
		if (pcmFile == nullptr)
			break;
		const size_t frames = chunk.size() / AudioChannels;
		std::fwrite(chunk.data(), sizeof(s16) * AudioChannels, frames, pcmFile);
		audioFramesWritten += frames;
	}
	pending.clear();
}

static void writerMain()
{
	while (true)
	{
		std::vector<u8> frame;
		std::deque<std::vector<s16>> audioBatch;
		u64 duplicates = 0;
		bool haveFrame = false;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cond.wait(lock, [] {
				return !queue.empty() || !audioQueue.empty() || !writerRunning;
			});
			if (queue.empty() && audioQueue.empty() && !writerRunning)
				break;
			audioBatch.swap(audioQueue);
			if (!queue.empty())
			{
				frame = std::move(queue.front());
				queue.pop_front();
				haveFrame = true;
				duplicates = pendingDuplicates;
				pendingDuplicates = 0;
			}
		}
		spaceCond.notify_all();

		drainAudioLocked(audioBatch);

		if (haveFrame)
		{
			// Repeat the previous frame for every slot whose contents were
			// dropped, so the file keeps one frame per emulated frame.
			for (u64 i = 0; i < duplicates && !lastFrame.empty(); i++)
				writeFrameToPipe(lastFrame);
			if (writeFrameToPipe(frame))
				lastFrame = std::move(frame);
			padAudioToVideo();
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

	// Video is encoded to a temp file and muxed with the captured audio when
	// the capture stops; ffmpeg cannot take two raw streams on one stdin.
	videoTmpPath = insertSuffix(path, ".video");
	pcmTmpPath = insertSuffix(path, ".audio") + ".pcm";
	blockOnFull = cfgLoadBool("record", "blockonfull", false);
	recFps = cfgLoadInt("record", "fps", 60);

	const std::string cmd = buildCommand(videoTmpPath, width, height, format, flipVertically);
	INFO_LOG(COMMON, "[rec] %s", cmd.c_str());

	pipeFile = REC_POPEN(cmd.c_str(), REC_MODE);
	if (pipeFile == nullptr)
	{
		ERROR_LOG(COMMON, "[rec] could not launch encoder - is ffmpeg on PATH?");
		gui_display_notification("Recording failed: ffmpeg not found", 5000);
		return false;
	}

	pcmFile = std::fopen(pcmTmpPath.c_str(), "wb");
	if (pcmFile == nullptr)
		WARN_LOG(COMMON, "[rec] could not open %s - recording will be silent", pcmTmpPath.c_str());

	recWidth = width;
	recHeight = height;
	recFormat = format;
	recPath = path;
	written = 0;
	dropped = 0;
	videoFramesWritten = 0;
	audioFramesWritten = 0;
	pendingDuplicates = 0;
	lastFrame.clear();
	recording = true;
	writerRunning = true;
	writerThread = std::thread(writerMain);

	INFO_LOG(COMMON, "[rec] recording %dx%d to %s", width, height, path.c_str());
	gui_display_notification("Recording started", 2000);
	return true;
}

// Combines the encoded video with the captured PCM into the final file. With
// no audio, the temp video simply becomes the output.
static void muxOutput(u64 audioFrames)
{
	if (videoTmpPath.empty())
		return;

	auto promoteVideoOnly = [] {
		std::remove(recPath.c_str());
		if (std::rename(videoTmpPath.c_str(), recPath.c_str()) != 0)
		{
			ERROR_LOG(COMMON, "[rec] could not move %s to %s", videoTmpPath.c_str(), recPath.c_str());
			recPath = videoTmpPath;	// report where the data actually is
		}
		std::remove(pcmTmpPath.c_str());
	};

	if (audioFrames == 0)
	{
		INFO_LOG(COMMON, "[rec] no audio captured, writing video only");
		promoteVideoOnly();
		return;
	}

	const std::string exe = cfgLoadStr("record", "ffmpeg", "ffmpeg");
	// pcm_s16le is lossless and always valid in AVI; AAC in AVI is not.
	const std::string acodec = cfgLoadStr("record", "acodec", "pcm_s16le");

	char cmd[1536];
	// -c:v copy: the video is already encoded, so this is a remux, not a
	// re-encode. -shortest trims whichever track overran.
	snprintf(cmd, sizeof(cmd),
			"\"%s\" -hide_banner -loglevel error -y -i \"%s\" "
			"-f s16le -ar %d -ac %d -i \"%s\" "
			"-c:v copy -c:a %s -shortest \"%s\"",
			exe.c_str(), videoTmpPath.c_str(),
			AudioSampleRate, AudioChannels, pcmTmpPath.c_str(),
			acodec.c_str(), recPath.c_str());
	INFO_LOG(COMMON, "[rec] mux: %s", cmd);

	const int rc = std::system(cmd);
	if (rc != 0)
	{
		WARN_LOG(COMMON, "[rec] mux failed (%d), keeping video without audio", rc);
		promoteVideoOnly();
		return;
	}
	std::remove(videoTmpPath.c_str());
	std::remove(pcmTmpPath.c_str());
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
	spaceCond.notify_all();
	if (writerThread.joinable())
		writerThread.join();

	if (pipeFile != nullptr)
	{
		REC_PCLOSE(pipeFile);
		pipeFile = nullptr;
	}
	const u64 audioFrames = audioFramesWritten;
	if (pcmFile != nullptr)
	{
		std::fclose(pcmFile);
		pcmFile = nullptr;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
		audioQueue.clear();
	}
	lastFrame.clear();

	muxOutput(audioFrames);

	INFO_LOG(COMMON, "[rec] stopped: %llu frames written, %llu dropped, %llu audio frames -> %s",
			(unsigned long long)written.load(), (unsigned long long)dropped.load(),
			(unsigned long long)audioFrames, recPath.c_str());

	char msg[256];
	snprintf(msg, sizeof(msg), "Recording saved (%llu frames%s%s)",
			(unsigned long long)written.load(),
			audioFrames > 0 ? ", with audio" : ", silent",
			dropped > 0 ? ", frames repeated" : "");
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
		std::unique_lock<std::mutex> lock(mutex);
		if (queue.size() >= MaxQueuedFrames)
		{
			if (blockOnFull)
			{
				// Rerecording mode: keep every frame's real contents, even if
				// that means stalling the render thread.
				spaceCond.wait(lock, [] {
					return queue.size() < MaxQueuedFrames || !recording;
				});
				if (!recording)
					return;
			}
			else
			{
				// Encoder is behind. Drop this frame's contents but keep its
				// slot: the writer repeats the previous frame in its place, so
				// the file stays one frame per emulated frame and the audio
				// track stays aligned. Stalling the render thread here would
				// perturb rollback frame pacing.
				dropped++;
				pendingDuplicates++;
				return;
			}
		}
		queue.push_back(std::move(rgb));
	}
	cond.notify_one();
}

void submitAudio(const void *interleavedStereoS16, int frameCount)
{
	if (!recording || interleavedStereoS16 == nullptr || frameCount <= 0)
		return;
	const s16 *src = (const s16 *)interleavedStereoS16;
	std::vector<s16> chunk(src, src + (size_t)frameCount * AudioChannels);
	{
		std::lock_guard<std::mutex> lock(mutex);
		// Audio is tiny next to video (~172 KB/s). A bound this generous only
		// trips if the writer thread has genuinely stalled, in which case
		// dropping audio is preferable to unbounded growth.
		if (audioQueue.size() >= 256)
			return;
		audioQueue.push_back(std::move(chunk));
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
