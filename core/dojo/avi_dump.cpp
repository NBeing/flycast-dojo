#include "avi_dump.h"
#include "cfg/option.h"		// config::RenderResolution
#include "cfg/cfg.h"		// cfgLoadInt
#include "rend/gui.h"		// gui_display_notification
#include "stdclass.h"		// get_writable_data_path
#include "oslib/oslib.h"	// hostfs::savestateFolderOverride (save captures next to the clip)
#include "log/Log.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <vfw.h>
#include <commdlg.h>
#endif

AviDump avi_dump;

static constexpr u32 AVI_AUDIO_RATE = 44100;	// Dreamcast AICA output rate

#ifdef _WIN32
// Bilinear upscale srcRgb (srcW x srcH, RGB, top-down) into dib (targetW x targetH, BGR,
// bottom-up, DWORD-aligned rows) as an uncompressed 24-bit DIB for the VfW codec.
static void upscaleToDib(const u8 *src, int sw, int sh, std::vector<u8>& dib, int tw, int th)
{
	int stride = (tw * 3 + 3) & ~3;
	if (sw == tw && sh == th)
	{
		// Fast path: output res == emulator render res (the default), so no scaling is needed.
		// Direct RGB(top-down) -> BGR(bottom-up) copy - skips the per-pixel bilinear float math and
		// the full-buffer memset (only the DWORD-align padding at each row end is zeroed).
		dib.resize((size_t)stride * th);
		int rowBytes = tw * 3;
		for (int y = 0; y < th; y++)
		{
			const u8 *s = &src[(size_t)y * sw * 3];
			u8 *d = &dib[(size_t)(th - 1 - y) * stride];
			for (int x = 0; x < tw; x++)
			{
				d[x * 3 + 0] = s[x * 3 + 2];
				d[x * 3 + 1] = s[x * 3 + 1];
				d[x * 3 + 2] = s[x * 3 + 0];
			}
			for (int p = rowBytes; p < stride; p++)
				d[p] = 0;
		}
		return;
	}
	dib.assign((size_t)stride * th, 0);
	for (int ty = 0; ty < th; ty++)
	{
		float fy = (ty + 0.5f) * sh / th - 0.5f;
		int y0 = (int)std::floor(fy);
		float wy = fy - y0;
		int y0c = y0 < 0 ? 0 : (y0 >= sh ? sh - 1 : y0);
		int y1 = y0 + 1;
		int y1c = y1 < 0 ? 0 : (y1 >= sh ? sh - 1 : y1);
		u8 *drow = &dib[(size_t)(th - 1 - ty) * stride];	// bottom-up: image top -> last buffer row
		for (int tx = 0; tx < tw; tx++)
		{
			float fx = (tx + 0.5f) * sw / tw - 0.5f;
			int x0 = (int)std::floor(fx);
			float wx = fx - x0;
			int x0c = x0 < 0 ? 0 : (x0 >= sw ? sw - 1 : x0);
			int x1 = x0 + 1;
			int x1c = x1 < 0 ? 0 : (x1 >= sw ? sw - 1 : x1);
			const u8 *p00 = &src[((size_t)y0c * sw + x0c) * 3];
			const u8 *p01 = &src[((size_t)y0c * sw + x1c) * 3];
			const u8 *p10 = &src[((size_t)y1c * sw + x0c) * 3];
			const u8 *p11 = &src[((size_t)y1c * sw + x1c) * 3];
			for (int ch = 0; ch < 3; ch++)
			{
				float top = p00[ch] + (p01[ch] - p00[ch]) * wx;
				float bot = p10[ch] + (p11[ch] - p10[ch]) * wx;
				float val = top + (bot - top) * wy;
				int iv = (int)(val + 0.5f);
				if (iv < 0) iv = 0; else if (iv > 255) iv = 255;
				drow[tx * 3 + (2 - ch)] = (u8)iv;	// RGB source -> BGR dib
			}
		}
	}
}

static std::string codecOptsPath()
{
	return get_writable_data_path("avi_codec.opts");
}

// Serialize the chosen codec (fixed fields + variable format/parms blobs) so F12 can reuse it.
static void saveCodecOpts(const AVICOMPRESSOPTIONS& o)
{
	FILE *f = fopen(codecOptsPath().c_str(), "wb");
	if (f == nullptr)
		return;
	fwrite(&o.fccType, 4, 1, f);
	fwrite(&o.fccHandler, 4, 1, f);
	fwrite(&o.dwKeyFrameEvery, 4, 1, f);
	fwrite(&o.dwQuality, 4, 1, f);
	fwrite(&o.dwBytesPerSecond, 4, 1, f);
	fwrite(&o.dwFlags, 4, 1, f);
	fwrite(&o.dwInterleaveEvery, 4, 1, f);
	fwrite(&o.cbFormat, 4, 1, f);
	if (o.cbFormat != 0 && o.lpFormat != nullptr)
		fwrite(o.lpFormat, 1, o.cbFormat, f);
	fwrite(&o.cbParms, 4, 1, f);
	if (o.cbParms != 0 && o.lpParms != nullptr)
		fwrite(o.lpParms, 1, o.cbParms, f);
	fclose(f);
}

static bool loadCodecOpts(AVICOMPRESSOPTIONS& o, void*& fmt, void*& parms)
{
	FILE *f = fopen(codecOptsPath().c_str(), "rb");
	if (f == nullptr)
		return false;
	memset(&o, 0, sizeof(o));
	fmt = nullptr;
	parms = nullptr;
	bool ok = fread(&o.fccType, 4, 1, f) == 1
			&& fread(&o.fccHandler, 4, 1, f) == 1
			&& fread(&o.dwKeyFrameEvery, 4, 1, f) == 1
			&& fread(&o.dwQuality, 4, 1, f) == 1
			&& fread(&o.dwBytesPerSecond, 4, 1, f) == 1
			&& fread(&o.dwFlags, 4, 1, f) == 1
			&& fread(&o.dwInterleaveEvery, 4, 1, f) == 1
			&& fread(&o.cbFormat, 4, 1, f) == 1;
	if (ok && o.cbFormat != 0)
	{
		fmt = malloc(o.cbFormat);
		ok = fmt != nullptr && fread(fmt, 1, o.cbFormat, f) == o.cbFormat;
		o.lpFormat = fmt;
	}
	ok = ok && fread(&o.cbParms, 4, 1, f) == 1;
	if (ok && o.cbParms != 0)
	{
		parms = malloc(o.cbParms);
		ok = parms != nullptr && fread(parms, 1, o.cbParms, f) == o.cbParms;
		o.lpParms = parms;
	}
	fclose(f);
	if (!ok)
	{
		free(fmt);
		free(parms);
		fmt = nullptr;
		parms = nullptr;
	}
	return ok;
}

static void writeWav(const std::wstring& path, const std::vector<s16>& buf)
{
	FILE *f = _wfopen(path.c_str(), L"wb");
	if (f == nullptr)
		return;
	u32 dataBytes = (u32)(buf.size() * sizeof(s16));
	auto w32 = [&](u32 v) { u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) }; fwrite(b, 1, 4, f); };
	auto w16 = [&](u16 v) { u8 b[2] = { (u8)v, (u8)(v >> 8) }; fwrite(b, 1, 2, f); };
	fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2);			// PCM, stereo
	w32(AVI_AUDIO_RATE); w32(AVI_AUDIO_RATE * 4); w16(4); w16(16);	// byteRate, blockAlign, bits
	fwrite("data", 1, 4, f); w32(dataBytes);
	fwrite(buf.data(), sizeof(s16), buf.size(), f);
	fclose(f);
	NOTICE_LOG(COMMON, "AVI: wrote %u audio frames to WAV", (u32)(buf.size() / 2));
}
static std::wstring toWide(const std::string& s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

// Where the Save-As dialog should start, and what it should call the file: the ACTIVE CLIP's
// folder, named after the clip (so a capture lands next to its movie/states with a matching
// name, and the browser's rename can carry it along). Empty dir = fall back to the cwd.
static void defaultCaptureTarget(std::wstring& dir, std::wstring& stem)
{
	dir.clear();
	stem = L"combo";
	if (hostfs::savestateFolderOverride.empty())
		return;
	dir = toWide(hostfs::savestateFolderOverride);
	size_t slash = dir.find_last_of(L"\\/");
	std::wstring name = slash == std::wstring::npos ? dir : dir.substr(slash + 1);
	if (!name.empty())
		stem = name;
}

// Locate ffmpeg.exe: bundled next to flycast.exe first (the shipped configuration), then PATH.
static std::wstring findFfmpegExe()
{
	wchar_t exePath[MAX_PATH] = L"";
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	std::wstring dir(exePath);
	size_t slash = dir.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
	{
		std::wstring bundled = dir.substr(0, slash + 1) + L"ffmpeg.exe";
		if (GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES)
			return bundled;
	}
	wchar_t found[MAX_PATH] = L"";
	if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, found, nullptr) > 0)
		return found;
	return L"";
}
#endif	// _WIN32

bool AviDump::startInteractive(int tw, int th, int f, bool forcePickCodec)
{
#ifdef _WIN32
	if (recording)
		return false;

	// Backend selection (dojo:CaptureEncoder): "cfhd"/"prores" pipe raw frames into a spawned
	// ffmpeg.exe that encodes the final .mov live - needs NO installed codec (the shareable
	// path). "vfw" is the classic system-codec (Lagarith etc.) route below. Falls back to VfW
	// with a notice when no ffmpeg.exe is found.
	std::string encoderSel = cfgLoadStr("dojo", "CaptureEncoder", "prores");
	// Automated (headless) capture: no dialogs. Forces the ffmpeg backend (VfW needs its modal
	// dialogs) and auto-names the output captures\<replay-stem>.mov relative to the working dir.
	bool autoCap = cfgLoadBool("dojo", "AutoCapture", false);
	if (autoCap && encoderSel == "vfw")
		encoderSel = "prores";
	useFfmpeg = false;
	if (encoderSel != "vfw")
	{
		ffmpegExe = findFfmpegExe();
		if (ffmpegExe.empty())
			gui_display_notification("ffmpeg not found - using the system (VfW) codec instead", 4000);
		else
		{
			captureEnc = (encoderSel == "prores" || encoderSel == "prores_ks") ? "prores" : "cfhd";

			if (autoCap)
			{
				std::string rf = cfgLoadStr("dojo", "ReplayFilename", "");
				size_t sl = rf.find_last_of("/\\");
				std::string base = sl == std::string::npos ? rf : rf.substr(sl + 1);
				size_t ext = base.rfind(".flyr");
				if (ext != std::string::npos)
					base.resize(ext);
				if (base.empty())
					base = "capture";
				CreateDirectoryW(L"captures", nullptr);
				finalPath = L"captures\\" + std::wstring(base.begin(), base.end()) + L".mov";
				NOTICE_LOG(COMMON, "AVI: auto-capture -> %s", base.c_str());
			}
			else
			{
				std::wstring defDir, defStem;
				defaultCaptureTarget(defDir, defStem);
				wchar_t mpath[MAX_PATH] = L"";
				// FULL path in lpstrFile so the dialog prefills BOTH folder and name. Built by
				// CONCATENATION, not swprintf: under MinGW ANSI stdio, %s in a WIDE printf takes a
				// NARROW string, so it read the UTF-16 path byte-wise and stopped at each first NUL
				// byte - which is exactly how the dialog came up saying "C2.mov".
				{
					std::wstring full = defDir.empty() ? defStem + L".mov"
							: defDir + L"\\" + defStem + L".mov";
					wcsncpy(mpath, full.c_str(), MAX_PATH - 1);
				}
				OPENFILENAMEW mofn = {};
				mofn.lStructSize = sizeof(mofn);
				mofn.lpstrFilter = L"QuickTime movie\0*.mov\0All files\0*.*\0";
				mofn.lpstrFile = mpath;
				mofn.nMaxFile = MAX_PATH;
				mofn.lpstrTitle = L"Save combo capture as";
				mofn.lpstrDefExt = L"mov";
				mofn.lpstrInitialDir = defDir.empty() ? nullptr : defDir.c_str();
				mofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
				if (!GetSaveFileNameW(&mofn))
					return false;			// user cancelled the file dialog
				finalPath = mpath;
			}
			videoTmpPath = finalPath + L".video.mov";
			aviPath = videoTmpPath;			// input of the post-capture stream-copy mux
			wavPath = finalPath;
			size_t mdot = wavPath.rfind(L'.');
			if (mdot != std::wstring::npos && wavPath.size() - mdot <= 5)
				wavPath.resize(mdot);
			wavPath += L".wav";

			useFfmpeg = true;
			targetW = tw;
			targetH = th;
			fps = f;
			frameIndex = 0;
			ffProc = nullptr;
			ffStdin = nullptr;
			recording = true;

			{
				std::lock_guard<std::mutex> lock(audioMutex);
				audioBuf.clear();
				audioBuf.reserve((size_t)AVI_AUDIO_RATE * 2 * 10);
			}
			audioOn.store(true);

			writerStop = false;
			frameQueue.clear();
			writerThread = std::thread(&AviDump::writerLoop, this);

			NOTICE_LOG(COMMON, "AVI(ffmpeg): recording %dx%d @ %d fps, direct %s encode", tw, th, f, captureEnc.c_str());
			char msg[128];
			snprintf(msg, sizeof(msg), "Recording %dx%d + audio (%s)", tw, th, captureEnc.c_str());
			gui_display_notification(msg, 3000);
			return true;
		}
	}
	finalPath.clear();

	std::wstring vfwDir, vfwStem;
	defaultCaptureTarget(vfwDir, vfwStem);
	wchar_t path[MAX_PATH] = L"";
	// Concatenation, not swprintf - same MinGW %s-is-narrow pitfall as the ffmpeg dialog above.
	{
		std::wstring full = vfwDir.empty() ? vfwStem + L".avi"
				: vfwDir + L"\\" + vfwStem + L".avi";
		wcsncpy(path, full.c_str(), MAX_PATH - 1);
	}
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = L"AVI files\0*.avi\0All files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"Save combo capture as";
	ofn.lpstrDefExt = L"avi";
	ofn.lpstrInitialDir = vfwDir.empty() ? nullptr : vfwDir.c_str();
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&ofn))
		return false;					// user cancelled the file dialog

	AVIFileInit();
	PAVIFILE pf = nullptr;
	if (AVIFileOpenW(&pf, path, OF_WRITE | OF_CREATE, nullptr) != 0)
	{
		AVIFileExit();
		gui_display_notification("AVI: could not create the output file", 3500);
		return false;
	}

	AVISTREAMINFOW si = {};
	si.fccType = streamtypeVIDEO;
	si.dwScale = 1;
	si.dwRate = (DWORD)f;
	si.dwSuggestedBufferSize = (DWORD)(tw * th * 3);
	si.rcFrame.right = tw;
	si.rcFrame.bottom = th;
	wcscpy(si.szName, L"Video");
	PAVISTREAM ps = nullptr;
	if (AVIFileCreateStreamW(pf, &ps, &si) != 0)
	{
		AVIFileRelease(pf);
		AVIFileExit();
		gui_display_notification("AVI: could not create the video stream", 3500);
		return false;
	}

	// Codec: reuse the remembered choice unless forced (Shift+F12) or none saved yet.
	AVICOMPRESSOPTIONS opts;
	memset(&opts, 0, sizeof(opts));
	void *fmtBlob = nullptr;
	void *parmsBlob = nullptr;
	bool usedDialog = false;
	if (forcePickCodec || !loadCodecOpts(opts, fmtBlob, parmsBlob))
	{
		AVICOMPRESSOPTIONS *aopts[1] = { &opts };
		if (!AVISaveOptions(nullptr, 0, 1, &ps, aopts))	// native "(VfW) Lagarith..." picker
		{
			AVISaveOptionsFree(1, aopts);
			AVIStreamRelease(ps);
			AVIFileRelease(pf);
			AVIFileExit();
			return false;				// user cancelled the codec dialog
		}
		usedDialog = true;
		saveCodecOpts(opts);			// remember it for next time
	}

	PAVISTREAM psc = nullptr;
	HRESULT hr = AVIMakeCompressedStream(&psc, ps, &opts, nullptr);
	if (usedDialog)
	{
		AVICOMPRESSOPTIONS *aopts[1] = { &opts };
		AVISaveOptionsFree(1, aopts);
	}
	if (hr != AVIERR_OK)
	{
		free(fmtBlob);
		free(parmsBlob);
		AVIStreamRelease(ps);
		AVIFileRelease(pf);
		AVIFileExit();
		gui_display_notification("AVI: codec initialization failed", 3500);
		return false;
	}

	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = tw;
	bi.biHeight = th;					// positive = bottom-up DIB
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = (DWORD)(((tw * 3 + 3) & ~3) * th);
	if (AVIStreamSetFormat(psc, 0, &bi, sizeof(bi)) != AVIERR_OK)
	{
		free(fmtBlob);
		free(parmsBlob);
		AVIStreamRelease(psc);
		AVIStreamRelease(ps);
		AVIFileRelease(pf);
		AVIFileExit();
		gui_display_notification("AVI: codec rejected 24-bit RGB input", 4000);
		return false;
	}

	pfile = pf;
	psVideo = ps;
	psCompressed = psc;
	loadedFormat = fmtBlob;
	loadedParms = parmsBlob;
	targetW = tw;
	targetH = th;
	fps = f;
	frameIndex = 0;
	recording = true;

	// Arm the synced WAV: same path with .wav in place of the extension.
	wavPath = path;
	size_t dot = wavPath.rfind(L'.');
	if (dot != std::wstring::npos && wavPath.size() - dot <= 5)
		wavPath.resize(dot);
	wavPath += L".wav";
	aviPath = path;
	{
		std::lock_guard<std::mutex> lock(audioMutex);
		audioBuf.clear();
		audioBuf.reserve((size_t)AVI_AUDIO_RATE * 2 * 10);
	}
	audioOn.store(true);

	// Spin up the async writer: frames queue here and are converted + compressed off the render
	// thread, so capture speed degrades only to the codec's own throughput, not below it.
	writerStop = false;
	frameQueue.clear();
	writerThread = std::thread(&AviDump::writerLoop, this);

	NOTICE_LOG(COMMON, "AVI(VfW): recording %dx%d @ %d fps%s", tw, th, f, usedDialog ? " (codec picked)" : " (saved codec)");
	char msg[128];
	snprintf(msg, sizeof(msg), "Recording %dx%d + audio", tw, th);
	gui_display_notification(msg, 3000);
	return true;
#else
	(void)tw; (void)th; (void)f; (void)forcePickCodec;
	return false;
#endif
}

void AviDump::addVideoFrame(const u8 *src, int sw, int sh, int fmt)
{
#ifdef _WIN32
	if (!recording || src == nullptr || sw <= 0 || sh <= 0)
		return;
	size_t bpp = fmt == FMT_RGB24 ? 3 : 4;
	std::unique_lock<std::mutex> lk(qMutex);
	// Bounded queue: BLOCK rather than drop when the codec falls behind - the video must contain
	// every emulated frame exactly once (a dropped frame would silently desync it).
	qCv.wait(lk, [this] { return frameQueue.size() < MAX_QUEUED_FRAMES || writerStop; });
	if (writerStop)
		return;
	QueuedFrame f;
	if (!freeBufs.empty())
	{
		f.rgb = std::move(freeBufs.back());
		freeBufs.pop_back();
	}
	f.rgb.assign(src, src + (size_t)sw * sh * bpp);
	f.w = sw;
	f.h = sh;
	f.fmt = fmt;
	frameQueue.push_back(std::move(f));
	lk.unlock();
	qCv.notify_all();
#else
	(void)src; (void)sw; (void)sh; (void)fmt;
#endif
}

// ffmpeg backend: start the encoder process with its stdin as our frame pipe. Called from the
// writer thread on the first frame (the source dimensions are only known then). ffmpeg's stderr
// goes to <output>.capture.log for debugging.
bool AviDump::spawnFfmpeg(int srcW, int srcH, int fmt)
{
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE readEnd = nullptr, writeEnd = nullptr;
	if (!CreatePipe(&readEnd, &writeEnd, &sa, 4 * 1024 * 1024))
		return false;
	SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);	// keep our end out of the child

	// take the renderer's native pixel layout as-is - no conversion anywhere on our side
	const wchar_t *pixFmt = fmt == FMT_BGRA32 ? L"bgra" : fmt == FMT_RGBA32 ? L"rgba" : L"rgb24";
	wchar_t buf[96];
	std::wstring cmd = L"\"" + ffmpegExe + L"\" -y -f rawvideo -pix_fmt ";
	cmd += pixFmt;
	swprintf(buf, 96, L" -s %dx%d -r %d -i -", srcW, srcH, fps);
	cmd += buf;
	if (srcW != targetW || srcH != targetH)
	{
		swprintf(buf, 96, L" -vf scale=%d:%d", targetW, targetH);
		cmd += buf;
	}
	if (captureEnc == "prores")
	{
		int profile = cfgLoadInt("dojo", "ProResProfile", 1);
		if (profile < 0) profile = 0;
		if (profile > 3) profile = 3;
		int qscale = cfgLoadInt("dojo", "ProResQscale", 13);
		if (qscale < 2) qscale = 2;
		if (qscale > 31) qscale = 31;
		swprintf(buf, 96, L" -c:v prores_ks -profile:v %d -qscale:v %d -vendor apl0", profile, qscale);
		cmd += buf;
	}
	else
	{
		// ffmpeg's cfhd encoder DEFAULTS to its maximum tier (film3 = film-scan mastering,
		// ~2 Gbps at 4K/60 - a 2.8 GB 11-second clip). film1 is visually excellent for game
		// footage at a fraction of the size; tune via dojo:CineFormQuality.
		std::string q = cfgLoadStr("dojo", "CineFormQuality", "film1");
		if (q != "low" && q != "medium" && q != "high" && q != "film1" && q != "film2" && q != "film3")
			q = "film1";
		cmd += L" -c:v cfhd -quality " + std::wstring(q.begin(), q.end());
	}
	cmd += L" -pix_fmt yuv422p10le \"" + videoTmpPath + L"\"";

	// dojo:CaptureLog (default on): ffmpeg's stderr -> <output>.capture.log. Off sends it to NUL -
	// the handle must still be valid because STARTF_USESTDHANDLES hands it to the child.
	HANDLE logH = cfgLoadBool("dojo", "CaptureLog", true)
			? CreateFileW((finalPath + L".capture.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
					&sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
			: CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL, nullptr);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = readEnd;
	si.hStdOutput = logH != INVALID_HANDLE_VALUE ? logH : nullptr;
	si.hStdError = logH != INVALID_HANDLE_VALUE ? logH : nullptr;
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> cl(cmd.begin(), cmd.end());
	cl.push_back(L'\0');
	BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi);
	CloseHandle(readEnd);
	if (logH != INVALID_HANDLE_VALUE)
		CloseHandle(logH);
	if (!ok)
	{
		CloseHandle(writeEnd);
		WARN_LOG(COMMON, "AVI(ffmpeg): CreateProcess failed (%lu)", (unsigned long)GetLastError());
		return false;
	}
	CloseHandle(pi.hThread);
	ffProc = pi.hProcess;
	ffStdin = writeEnd;
	NOTICE_LOG(COMMON, "AVI(ffmpeg): %s encoder started (source %dx%d)", captureEnc.c_str(), srcW, srcH);
	return true;
#else
	(void)srcW; (void)srcH;
	return false;
#endif
}

// Worker: convert + compress + write queued frames. VfW use is per-thread initialized
// (AVIFileInit/AVIFileExit are ref-counted); only this thread touches psCompressed/dib while
// recording. Keeps draining after stop is requested so every queued frame lands in the file.
void AviDump::writerLoop()
{
#ifdef _WIN32
	if (!useFfmpeg)
		AVIFileInit();
	long long accScaleUs = 0, accWriteUs = 0;
	auto winStart = std::chrono::steady_clock::now();
	int winFrames = 0;
	bool loggedSrc = false;
	for (;;)
	{
		QueuedFrame f;
		size_t depth = 0;
		{
			std::unique_lock<std::mutex> lk(qMutex);
			qCv.wait(lk, [this] { return !frameQueue.empty() || writerStop; });
			if (frameQueue.empty())
				break;		// stop requested and fully drained
			f = std::move(frameQueue.front());
			frameQueue.pop_front();
			depth = frameQueue.size();
		}
		qCv.notify_all();	// wake a producer blocked on a full queue

		if (!loggedSrc)
		{
			loggedSrc = true;
			NOTICE_LOG(COMMON, "AVI: capture SOURCE is %dx%d (emulator render), output %dx%d", f.w, f.h, targetW, targetH);
			if (useFfmpeg && !spawnFfmpeg(f.w, f.h, f.fmt))
			{
				gui_display_notification("Capture encoder failed to start - capture aborted", 5000);
				std::lock_guard<std::mutex> lk(qMutex);
				writerStop = true;
				frameQueue.clear();
				break;
			}
		}
		auto t0 = std::chrono::steady_clock::now();
		auto t1 = t0;
		if (useFfmpeg)
		{
			// raw rgb24 straight down the pipe; blocks on backpressure when the encoder is behind
			DWORD wrote = 0;
			if (!WriteFile((HANDLE)ffStdin, f.rgb.data(), (DWORD)f.rgb.size(), &wrote, nullptr)
					|| wrote != (DWORD)f.rgb.size())
			{
				WARN_LOG(COMMON, "AVI(ffmpeg): pipe write failed - encoder died? capture aborted");
				gui_display_notification("Capture encoder died - capture aborted", 5000);
				std::lock_guard<std::mutex> lk(qMutex);
				writerStop = true;
				frameQueue.clear();
				break;
			}
		}
		else
		{
			// VfW wants 24-bit RGB; 32-bit renderer frames are converted HERE (writer thread),
			// keeping the render thread free of per-pixel work for both backends.
			const u8 *rgbSrc = f.rgb.data();
			static std::vector<u8> rgbScratch;
			if (f.fmt != FMT_RGB24)
			{
				rgbScratch.resize((size_t)f.w * f.h * 3);
				const bool bgra = f.fmt == FMT_BGRA32;
				for (size_t p = 0, n = (size_t)f.w * f.h; p < n; p++)
				{
					const u8 *s = &f.rgb[p * 4];
					u8 *d = &rgbScratch[p * 3];
					if (bgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
					else      { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
				}
				rgbSrc = rgbScratch.data();
			}
			upscaleToDib(rgbSrc, f.w, f.h, dib, targetW, targetH);
			t1 = std::chrono::steady_clock::now();
			LONG written = 0;
			AVIStreamWrite((PAVISTREAM)psCompressed, frameIndex, 1, dib.data(), (LONG)dib.size(),
					AVIIF_KEYFRAME, nullptr, &written);
		}
		auto t2 = std::chrono::steady_clock::now();
		frameIndex++;
		winFrames++;

		{
			std::lock_guard<std::mutex> lk(qMutex);
			freeBufs.push_back(std::move(f.rgb));	// recycle the ~33 MB buffer
		}

		accScaleUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
		accWriteUs += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
		double winSec = std::chrono::duration<double>(t2 - winStart).count();
		if (winSec >= 2.0)
		{
			NOTICE_LOG(COMMON, "AVI: %.1f fps written (convert %.1f ms, codec %.1f ms, queue %d/%d)",
					winFrames / winSec, accScaleUs / (winFrames * 1000.0), accWriteUs / (winFrames * 1000.0),
					(int)depth, (int)MAX_QUEUED_FRAMES);
			accScaleUs = accWriteUs = 0;
			winFrames = 0;
			winStart = t2;
		}
	}
	if (useFfmpeg)
	{
		// close the pipe so the encoder flushes and finalizes the container, then wait for it
		if (ffStdin)
		{
			CloseHandle((HANDLE)ffStdin);
			ffStdin = nullptr;
		}
		if (ffProc)
		{
			NOTICE_LOG(COMMON, "AVI(ffmpeg): waiting for the encoder to finish...");
			WaitForSingleObject((HANDLE)ffProc, 120000);
			DWORD code = 1;
			GetExitCodeProcess((HANDLE)ffProc, &code);
			CloseHandle((HANDLE)ffProc);
			ffProc = nullptr;
			NOTICE_LOG(COMMON, "AVI(ffmpeg): encoder exited %lu", (unsigned long)code);
		}
	}
	else
		AVIFileExit();
#endif
}

void AviDump::writeAudioSample(s16 left, s16 right)
{
	if (!audioOn.load(std::memory_order_relaxed))
		return;
	std::lock_guard<std::mutex> lock(audioMutex);
	if (!audioOn.load(std::memory_order_relaxed))
		return;
	audioBuf.push_back(left);
	audioBuf.push_back(right);
}

void AviDump::stop()
{
#ifdef _WIN32
	if (!recording)
		return;
	recording = false;

	// Flush the writer: let it drain every queued frame, then join BEFORE touching the VfW handles.
	{
		std::lock_guard<std::mutex> lk(qMutex);
		writerStop = true;
	}
	qCv.notify_all();
	if (writerThread.joinable())
		writerThread.join();
	{
		std::lock_guard<std::mutex> lk(qMutex);
		frameQueue.clear();
		freeBufs.clear();
		freeBufs.shrink_to_fit();
	}

	// finalize the WAV (audio thread is no longer appending once audioOn is false)
	audioOn.store(false);
	{
		std::lock_guard<std::mutex> lock(audioMutex);
		if (!audioBuf.empty() && !wavPath.empty())
			writeWav(wavPath, audioBuf);
		audioBuf.clear();
		audioBuf.shrink_to_fit();
	}

	if (psCompressed) { AVIStreamRelease((PAVISTREAM)psCompressed); psCompressed = nullptr; }
	if (psVideo)      { AVIStreamRelease((PAVISTREAM)psVideo); psVideo = nullptr; }
	if (pfile)        { AVIFileRelease((PAVIFILE)pfile); pfile = nullptr; }
	if (!useFfmpeg)
		AVIFileExit();		// pairs with startInteractive's AVIFileInit (VfW backend only)

	free(loadedFormat);
	free(loadedParms);
	loadedFormat = nullptr;
	loadedParms = nullptr;

	NOTICE_LOG(COMMON, "AVI(VfW): stopped, %d frames", frameIndex);
#endif
}

void AviDump::runPostEncode()
{
#ifdef _WIN32
	// ffmpeg-direct captures: the video is ALREADY encoded (cfhd/prores, live) - this step just
	// stream-copy muxes the synced WAV into the user's chosen .mov (fast, no re-encode).
	// VfW captures: default ON (CineForm) so F12-stop yields a single synced .mov; override with
	// dojo:PostEncode=prores or =none (keep plain .avi + .wav). Intermediates are removed after a
	// successful mux unless dojo:KeepAviWav=1.
	std::string vcodec;
	if (useFfmpeg)
	{
		if (aviPath.empty())
			return;
		vcodec = "copy";
	}
	else
	{
		std::string codec = cfgLoadStr("dojo", "PostEncode", "cfhd");
		if (codec.empty() || codec == "none" || aviPath.empty())
			return;
		vcodec = (codec == "prores" || codec == "prores_ks") ? "prores_ks"
				: (codec == "cfhd" || codec == "cineform") ? "cfhd" : codec;
	}

	// Prefer an ffmpeg.exe bundled next to flycast.exe; otherwise rely on PATH.
	wchar_t exePath[MAX_PATH] = L"";
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	std::wstring ffmpeg = L"ffmpeg";
	std::wstring dir(exePath);
	size_t slash = dir.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
	{
		std::wstring bundled = dir.substr(0, slash + 1) + L"ffmpeg.exe";
		if (GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES)
			ffmpeg = bundled;
	}

	std::wstring mov;
	if (useFfmpeg && !finalPath.empty())
		mov = finalPath;		// the user's chosen .mov (input is the .video.mov intermediate)
	else
	{
		mov = aviPath;
		size_t d = mov.rfind(L'.');
		if (d != std::wstring::npos && mov.size() - d <= 5)
			mov.resize(d);
		mov += L".mov";
	}

	std::wstring vcodecW(vcodec.begin(), vcodec.end());
	std::wstring vargs;
	if (vcodec == "prores_ks")
	{
		// ProRes size is driven mainly by qscale (higher = smaller/lighter). profile is the tier tag:
		// 0 Proxy, 1 LT, 2 Standard 422, 3 HQ. Defaults dial the old HQ/9 (~1 Gbps at 4K, chuggy
		// playback) down to a lighter, smoother file. Tune: dojo:ProResProfile / dojo:ProResQscale.
		int profile = cfgLoadInt("dojo", "ProResProfile", 1);
		if (profile < 0) profile = 0;
		if (profile > 5) profile = 5;
		int qscale = cfgLoadInt("dojo", "ProResQscale", 13);
		if (qscale < 2) qscale = 2;
		if (qscale > 31) qscale = 31;
		char pbuf[256];
		snprintf(pbuf, sizeof(pbuf),
				"-vf \"scale=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v prores_ks -profile:v %d -qscale:v %d"
				" -video_track_timescale 600 -vendor apl0 -pix_fmt yuv422p10le", profile, qscale);
		std::string a(pbuf);
		vargs.assign(a.begin(), a.end());
	}
	else if (vcodec == "cfhd")
	{
		std::string q = cfgLoadStr("dojo", "CineFormQuality", "film1");
		if (q != "low" && q != "medium" && q != "high" && q != "film1" && q != "film2" && q != "film3")
			q = "film1";
		vargs = L"-c:v cfhd -quality " + std::wstring(q.begin(), q.end()) + L" -pix_fmt yuv422p10le";
	}
	else
		vargs = L"-c:v " + vcodecW;
	std::wstring cmd = L"\"" + ffmpeg + L"\" -y -i \"" + aviPath + L"\" -i \"" + wavPath
			+ L"\" " + vargs + L" -c:a pcm_s16le -shortest \"" + mov + L"\"";

	// Basename of the .mov for the on-screen messages (output filenames are ASCII here).
	std::wstring baseW = mov;
	size_t bs = baseW.find_last_of(L"\\/");
	if (bs != std::wstring::npos)
		baseW = baseW.substr(bs + 1);
	std::string movName(baseW.begin(), baseW.end());

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(L'\0');
	if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi))
	{
		CloseHandle(pi.hThread);
		NOTICE_LOG(COMMON, "AVI: encoding + muxing video+audio -> %s (ffmpeg -c:v %s, background)...",
				movName.c_str(), vcodec.c_str());
		char m[160];
		snprintf(m, sizeof(m), "Encoding + muxing -> %s  (ffmpeg, running in background)", movName.c_str());
		gui_display_notification(m, 600000);	// held until the completion notice below replaces it

		// Wait for ffmpeg on a detached thread so the completion signal never blocks the emulator.
		// gui_display_notification is mutex-guarded, so calling it from this thread is safe. After a
		// successful mux the .avi + .wav are redundant (the .mov has both, synced) so they're removed
		// unless dojo:KeepAviWav=1 - only ever on ffmpeg success AND a real .mov present on disk.
		HANDLE hProc = pi.hProcess;
		std::wstring movFull = mov, aviCopy = aviPath, wavCopy = wavPath;
		bool keep = cfgLoadInt("dojo", "KeepAviWav", 0) != 0;
		std::thread([hProc, movName, movFull, aviCopy, wavCopy, keep]() {
			WaitForSingleObject(hProc, INFINITE);
			DWORD code = 1;
			GetExitCodeProcess(hProc, &code);
			CloseHandle(hProc);
			char msg[160];
			bool ok = (code == 0) && (GetFileAttributesW(movFull.c_str()) != INVALID_FILE_ATTRIBUTES);
			if (ok)
			{
				if (!keep)
				{
					DeleteFileW(aviCopy.c_str());
					DeleteFileW(wavCopy.c_str());
				}
				NOTICE_LOG(COMMON, "AVI: %s ready (video + audio synced)%s", movName.c_str(),
						keep ? "" : "; removed intermediate .avi + .wav");
				snprintf(msg, sizeof(msg), "%s ready - synced%s", movName.c_str(),
						keep ? "" : " (.avi/.wav removed)");
			}
			else
			{
				WARN_LOG(COMMON, "AVI: ffmpeg exited %lu; kept .avi + .wav", (unsigned long)code);
				snprintf(msg, sizeof(msg), "Encode failed (ffmpeg %lu) - kept .avi + .wav", (unsigned long)code);
			}
			gui_display_notification(msg, 10000);
		}).detach();
	}
	else
	{
		WARN_LOG(COMMON, "AVI: ffmpeg not found; kept .avi + .wav");
		gui_display_notification("ffmpeg not found - kept .avi + .wav", 5000);
	}
#endif
}

void avi_toggle_recording(bool forcePickCodec)
{
	if (avi_dump.isRecording())
	{
		int n = avi_dump.videoFrames();
		avi_dump.stop();
		avi_dump.runPostEncode();
		char msg[96];
		snprintf(msg, sizeof(msg), "Recording stopped (%d frames)", n);
		gui_display_notification(msg, 3000);
		return;
	}

	// Target output resolution: defaults to the internal-resolution setting (config::RenderResolution
	// is the height, e.g. 2880 for 6x), width from a 4:3 aspect. Override via dojo:AviWidth/AviHeight.
	int th = cfgLoadInt("dojo", "AviHeight", 0);
	if (th <= 0)
		th = (int)config::RenderResolution;	// 0/unset = the emulator's internal render height
	else if (th < 240)
		th = 480;
	int tw = cfgLoadInt("dojo", "AviWidth", 0);
	if (tw <= 0)
		tw = th * 4 / 3;

	if (!avi_dump.startInteractive(tw, th, 60, forcePickCodec))
		gui_display_notification("Recording cancelled", 2000);
}
