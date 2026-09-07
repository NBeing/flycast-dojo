#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <deque>
#include <thread>
#include <condition_variable>

// Windows Video-for-Windows (VfW) AVI exporter for TAS combo capture.
// Uses codecs installed on the machine (Lagarith, UtVideo, x264vfw, ...) via the native
// AVISaveOptions codec picker, plus a native Save-As dialog for the output path - both are
// real modal Win32 dialogs, so they work during gameplay (unlike ImGui windows, which only
// run while flycast's own menu is open). The chosen codec is remembered between runs; pass
// forcePickCodec (Shift+F12) to re-open the picker.
//
// Video: captured from the renderer at the game's native framebuffer resolution and upscaled
// here to the chosen target output resolution (small window still exports a large video).
// Audio: every emulated stereo sample (tapped from WriteSample) is buffered and written to a
// synced .wav next to the .avi, matching an "AVI -> editor, WAV -> editor" pipeline.
class AviDump
{
public:
	// Pop the native Save-As (+ codec picker if needed) and open the file at target res.
	// Returns false if the user cancelled or setup failed.
	bool startInteractive(int targetWidth, int targetHeight, int fps, bool forcePickCodec);
	void stop();
	bool isRecording() const { return recording; }
	int videoFrames() const { return frameIndex; }

	// Source pixel formats the renderers can hand over. 32-bit formats are passed to ffmpeg
	// UNCONVERTED (-pix_fmt bgra/rgba) - the renderer just row-copies its mapped surface, no
	// per-pixel loop on the render thread. The VfW backend converts on the writer thread.
	enum PixFmt { FMT_RGB24 = 0, FMT_BGRA32, FMT_RGBA32 };

	// src: srcW*srcH*bpp bytes, top-down. Queued for the writer thread, which encodes it off the
	// render thread. BLOCKS when the queue is full (frames are never dropped - the video must
	// contain every emulated frame exactly once), so worst case degrades to the codec's own
	// speed, never below it.
	void addVideoFrame(const u8 *src, int srcW, int srcH, int fmt = FMT_RGB24);
	// Called per emulated stereo sample from WriteSample (emu/audio thread); buffered for the WAV.
	void writeAudioSample(s16 left, s16 right);
	// After stop(): if dojo:PostEncode is set (cfhd/prores/...) and ffmpeg is found (next to the exe
	// or on PATH), launch it to mux+encode the .avi + .wav into a synced .mov. No-op otherwise.
	void runPostEncode();

private:
	void writerLoop();				// worker: pops queued frames, converts + AVIStreamWrites them
	bool spawnFfmpeg(int srcW, int srcH, int fmt);	// ffmpeg backend: start the encoder (writer thread)

	// ffmpeg-direct backend (dojo:CaptureEncoder = cfhd | prores): raw RGB frames are piped into a
	// spawned ffmpeg.exe that encodes the final codec live - no installed VfW codec needed (the
	// shareable path), no DIB conversion, encoder multithreaded in its own process. The synced WAV
	// is stream-copy muxed in at stop. "vfw" selects the classic system-codec path instead.
	bool useFfmpeg = false;
	std::string captureEnc;			// effective encoder for this capture (cfhd/prores)
	std::wstring ffmpegExe;			// resolved ffmpeg path (bundled first, then PATH)
	std::wstring videoTmpPath;		// video-only intermediate (.video.mov)
	std::wstring finalPath;			// the user's chosen output .mov
	void *ffProc = nullptr;			// HANDLE of the encoder process
	void *ffStdin = nullptr;		// HANDLE of its stdin pipe (write end)

	bool recording = false;
	int targetW = 0, targetH = 0, fps = 60;
	int frameIndex = 0;				// owned by the writer thread while recording; read after join
	std::vector<u8> dib;			// target-res BGR, bottom-up, DWORD-aligned scratch (writer thread)

	// async video queue: render thread pushes raw RGB frames, writer thread compresses + writes.
	// Bound keeps peak memory sane (4K RGB frame = ~33 MB; 5 in flight = ~166 MB worst case).
	struct QueuedFrame
	{
		std::vector<u8> rgb;
		int w = 0, h = 0;
		int fmt = FMT_RGB24;
	};
	static constexpr size_t MAX_QUEUED_FRAMES = 5;
	std::thread writerThread;
	std::mutex qMutex;
	std::condition_variable qCv;
	std::deque<QueuedFrame> frameQueue;
	std::vector<std::vector<u8>> freeBufs;	// recycled frame buffers (avoid 33 MB allocs per frame)
	bool writerStop = false;

	// audio -> WAV: buffered on the audio thread, flushed to disk at stop()
	std::atomic<bool> audioOn{false};
	std::mutex audioMutex;
	std::vector<s16> audioBuf;		// interleaved L,R
	std::wstring wavPath;
	std::wstring aviPath;			// kept for the optional ffmpeg post-encode

	// VfW handles (void* so the header stays free of <vfw.h>); valid only on Windows.
	void *pfile = nullptr;			// PAVIFILE
	void *psVideo = nullptr;		// PAVISTREAM (uncompressed source)
	void *psCompressed = nullptr;	// PAVISTREAM (compressed via the chosen codec)
	void *loadedFormat = nullptr;	// malloc'd codec blobs when reusing a saved codec; freed at stop
	void *loadedParms = nullptr;
};

extern AviDump avi_dump;

// Hotkey entry point: start recording (native dialogs) or stop if already recording.
// forcePickCodec re-opens the codec picker instead of reusing the remembered codec.
void avi_toggle_recording(bool forcePickCodec = false);
