#ifndef LIBRETRO
#include "types.h"
#include "emulator.h"
#include "hw/mem/addrspace.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "rend/gui.h"
#include "rend/panel.h"
#include "dojo/tas_ui.h"
#include "dojo/session.h"
#include "dojo/dojo_settings.h"
#include "dojo/roll_profile.h"
#include "dojo/roll_select.h"
#include "dojo/roll_edit.h"
#include "dojo/roll_paint.h"
#include "dojo/roll_pattern.h"
#include "dojo/roll_remap.h"
#include "dojo/roll_meta.h"
#include "dojo/roll_marks.h"
#include "dojo/roll_notation.h"
#include "dojo/roll_library.h"
#include "input/hold_repeat.h"
#include "dojo/movie.h"
#include "liveness.h"
#include "dojo/roll_staged.h"
#include "lua/luatier.h"
#include "lua/luawatch.h"
#include "dojo/roll_host.h"
#include "oslib/oslib.h"
#include "debug/gdb_server.h"
#include "archive/rzip.h"
#include "rend/mainui.h"
#include "input/gamepad_device.h"
#include "lua/lua.h"
#include "stdclass.h"
#include "serialize.h"
#include "determinism.h"
#include <algorithm>
#include "cfg/cfg.h"

#include <filesystem>
#include "dojo/dojo.h"
#include "dojo/dojo_gui.h"

int flycast_init(int argc, char* argv[])
{
#if defined(TEST_AUTOMATION)
	setbuf(stdout, 0);
	setbuf(stderr, 0);
	settings.aica.muteAudio = true;
#endif
	if (!addrspace::reserve())
	{
		ERROR_LOG(VMEM, "Failed to alloc mem");
		return -1;
	}
	ParseCommandLine(argc, argv);
	if (cfgLoadInt("naomi", "BoardId", 0) != 0)
	{
		settings.naomi.multiboard = true;
		settings.naomi.slave = true;
	}
	settings.naomi.drivingSimSlave = cfgLoadInt("naomi", "DrivingSimSlave", 0);

	config::Settings::instance().reset();
	LogManager::Shutdown();
	if (!cfgOpen())
	{
		LogManager::Init();
		NOTICE_LOG(BOOT, "Config directory is not set. Starting onboarding");
		gui_open_onboarding();
	}
	else
	{
		LogManager::Init();
		config::Settings::instance().load(false);
	}
	gui_init();
	// AFTER config load (it reads a cfg flag) and BEFORE any frame, which is
	// the window in which a registry is still safe to exercise: registration is
	// additive and nothing has drawn yet.
	panels::selfTest();
	tas_ui::selfTest();
	session::selfTest();
	dojocfg::selfTest();
	roll::selfTest();
	roll::selectionSelfTest();
	roll::editSelfTest();
	roll::paintSelfTest();
	roll::patternSelfTest();
	roll::remapSelfTest();
	roll::metaSelfTest();
	roll::marksSelfTest();
	roll::notationSelfTest();
	roll::librarySelfTest();
	hotkeys::holdRepeatSelfTest();
	movie::movieSelfTest();
	liveness::livenessSelfTest();
	/*
		START THE WATCHDOG HERE, beside the self-tests, because this is after
		the config load (it reads dojo:LoadGraceSeconds) and before any frame.
		The two lambdas are the only coupling the pure unit has to an emulator.
	*/
	liveness::startWatchdog(
			[] { return framesCompleted.load(); },
			[] { return emu.running(); });
	roll::stagedSelfTest();
	luatier::selfTest();
	luawatch::selfTest();
	roll::installHost();
	roll::marksInstall();
	roll::selectionInstall();
	roll::registerPanel();
	roll::registerStatesPanel();
	roll::registerHotkeyPanel();
	os_CreateWindow();
	os_SetupInput();

	if(config::GDB)
		debugger::init(config::GDBPort);
	lua::init();

	if(config::ProfilerEnabled)
		LogManager::GetInstance()->SetEnable(LogTypes::PROFILER, true);

	/*
		`[REMOVED 2026-09-10]` there was a second `dojo.replay.Init()` here.

		docs/SESSION-KINDS.md §4 #10 filed it as "triggered from two places on
		identical conditions". Measured, it ran twice per session - but the calls
		were never redundant: gui_start_game() calls dojo.Reset() and THEN
		Init(), so this one's movie was parsed and thrown away.

		AND IT WAS WORSE THAN WASTED FROM THE UI. Init's own first comment
		explains that the replay browser sets ReplayFilename with cfgSetVirtual
		"just before boot" - which is after this point. So a UI-launched replay
		loaded the PREVIOUS session's persisted clip here, and pointed
		hostfs::savestateFolderOverride at that clip's folder. Dojo::Reset() does
		not clear the override; it READS it. Anything asking about savestates
		between here and gui_start_game - a Lua script from lua::init() just
		above, the States panel drawing on the Main screen - saw the wrong clip.

		Safe to remove because every game start goes through gui_start_game:
		`gameLoader.load()` has exactly one caller, and Init sits above it.
	*/

	return 0;
}

void dc_exit()
{
	try {
		emu.stop();
	} catch (...) { }
	mainui_stop();
}

void SaveSettings()
{
	config::Settings::instance().save();
	GamepadDevice::SaveMaplePorts();

#ifdef __ANDROID__
	void SaveAndroidSettings();
	SaveAndroidSettings();
#endif
}

void flycast_term()
{
	// BEFORE anything it might log through. A detached thread that outlives
	// LogManager crashes on the way out, and an exit crash reads as a bug in
	// whatever ran last rather than in the watchdog.
	liveness::stopWatchdog();
	gui_cancel_load();
	lua::term();
	emu.term();
	gui_term();
	os_TermInput();
}

void dc_savestate(int index)
{
	// THE SAME QUESTION THE UI GATE AND THE AUTO-SAVE GATE ASK, and until
	// 2026-09-10 all three spelled it differently (docs/SESSION-KINDS.md §4 #6).
	// This one said `network.online`, which is not set until the handshake, so
	// a GGPO session mid-setup could still write a state.
	//
	// LOADING IS DELIBERATELY NOT GUARDED: emulator.cpp does dc_loadstate(-1)
	// to bring in the synchronised net state, which is exactly how a netplay
	// session starts. Saving is what would desync someone else; loading is how
	// you agree with them.
	if (session::rollbackLive())
		return;

	Serializer ser;
	dc_serialize(ser);

	void *data = malloc(ser.size());
	if (data == nullptr)
	{
		WARN_LOG(SAVESTATE, "Failed to save state - could not malloc %d bytes", (int)ser.size());
		gui_display_notification("Save state failed - memory full", 2000);
    	return;
	}

	ser = Serializer(data, ser.size());
	dc_serialize(ser);

	std::string filename = hostfs::getSavestatePath(index, true);
#if 0
	FILE *f = nowide::fopen(filename.c_str(), "wb");

	if ( f == NULL )
	{
		WARN_LOG(SAVESTATE, "Failed to save state - could not open %s for writing", filename.c_str());
		gui_display_notification("Cannot open save file", 2000);
		free(data);
    	return;
	}

	std::fwrite(data, 1, ser.size(), f);
	std::fclose(f);
#else
	RZipFile zipFile;
	if (!zipFile.Open(filename, true))
	{
		WARN_LOG(SAVESTATE, "Failed to save state - could not open %s for writing", filename.c_str());
		gui_display_notification("Cannot open save file", 2000);
		free(data);
    	return;
	}
	if (zipFile.Write(data, ser.size()) != ser.size())
	{
		WARN_LOG(SAVESTATE, "Failed to save state - error writing %s", filename.c_str());
		gui_display_notification("Error saving state", 2000);
		zipFile.Close();
		free(data);
    	return;
	}
	zipFile.Close();
#endif

	free(data);
	// TAS: record the movie frame this state was made at, as a .frame sidecar.
	// Without it a state has no position in the movie and a seek has nothing to
	// land on. SaveStateFrame/LoadStateFrame came across in dojo.cpp but their
	// only callers live here, so they sat defined and unreachable.
	dojo.SaveStateFrame(filename);
	// `[CORRECTED 2026-09-10]` and the epoch, which is what every slot view
	// watches to know its scan is out of date. dojo.h describes this field as
	// "bumped on every savestate write" and it was bumped in exactly two places,
	// neither of them a savestate write (docs/STATES-LIFT.md G2). The States
	// wall and the roll's gutter therefore showed a new state only when their
	// half-second safety tick came round - which is what that tick was added to
	// cover, so the fallback was hiding the defect it was written for.
	dojo.savestate_epoch++;
	NOTICE_LOG(SAVESTATE, "Saved state to %s size %d", filename.c_str(), (int)ser.size());
	gui_display_notification("State saved", 1000);
}

void dc_loadstate(int index)
{
	dc_loadstate(index, "");
}

void dc_loadstate(std::string filename)
{
	dc_loadstate(0, filename);
}

// Verify a just-loaded savestate round-trips: re-serialize the machine and
// compare against the blob it was loaded from. If deserialize is the exact
// inverse of serialize the two are byte-identical, and the first differing
// offset names the subsystem whose state did not survive.
//
// This is the bug class that makes a savestate-seek replay drift while
// play-from-frame-0 stays correct, so it hides from the obvious test. Two real
// desyncs were found this way in the TAS fork - a SCIF timer reschedule and
// AICA envelope side-effects clobbering restored state on load.
//
// It is also the PREREQUISITE for any state fingerprint: until save->load->save
// is byte-stable, a hash compares noise and every anchor assertion built on it
// is meaningless.
//
// Only reads the machine, so it does not perturb the state it just loaded.
// Force with -config dojo:VerifyState=yes|no.
static void verifyLoadedStateIdempotent(const void *blobA, size_t sizeA)
{
	// Is the machine actually STOPPED? dojo-7's dc_loadstate does not call
	// emu.stop() (the older base did), so a re-serialize here can be racing a
	// running emulator - in which case a "difference" is the machine advancing,
	// not a restore bug. Report it rather than let it masquerade.
	if (emu.running())
		WARN_LOG(SAVESTATE, "STATE VERIFY: emulator is RUNNING during the probe - "
				"any diff below may be the machine advancing, not a restore bug");
	// The sizing pass over-reserves (TA contexts reserve their maximum), so the
	// comparison uses the REAL byte count the second pass writes, never this.
	Serializer sizer;
	dc_serialize(sizer);
	const size_t bufSize = sizer.size();

	std::vector<u8> reser(bufSize);
	Serializer ser(reser.data(), bufSize);
	dc_serialize(ser);
	const size_t sizeB = ser.size();

	const size_t n = sizeA < sizeB ? sizeA : sizeB;
	size_t off = 0;
	while (off < n && ((const u8 *)blobA)[off] == reser[off])
		off++;

	if (sizeA == sizeB && off == n)
	{
		NOTICE_LOG(SAVESTATE, "STATE VERIFY: idempotent OK (%llu bytes round-trip)",
				(unsigned long long)sizeA);
	}
	else
	{
		WARN_LOG(SAVESTATE, "STATE VERIFY: NOT idempotent - first diff at offset %llu "
				"(re-serialized %llu vs loaded %llu bytes)",
				(unsigned long long)off, (unsigned long long)sizeB, (unsigned long long)sizeA);
		// Dump the neighbourhood of the first difference. "Offset N" alone
		// only narrows it to a subsystem (that is what SERMAP is for); the
		// BYTES say which field, and whether the delta looks like a counter,
		// a pointer or a timestamp.
		{
			const size_t from = off > 32 ? off - 32 : 0;
			const size_t to   = std::min(off + 48, n);
			std::string a, b, d;
			char t[8];
			for (size_t i = from; i < to; i++)
			{
				snprintf(t, sizeof(t), "%02x", ((const u8 *)blobA)[i]);   a += t;
				snprintf(t, sizeof(t), "%02x", reser[i]);                 b += t;
				d += (((const u8 *)blobA)[i] == reser[i]) ? ".." : "^^";
				if ((i - from) % 4 == 3) { a += ' '; b += ' '; d += ' '; }
			}
			WARN_LOG(SAVESTATE, "STATE VERIFY: window [%llu..%llu), first diff at +%llu",
					(unsigned long long)from, (unsigned long long)to,
					(unsigned long long)(off - from));
			WARN_LOG(SAVESTATE, "STATE VERIFY:  loaded %s", a.c_str());
			WARN_LOG(SAVESTATE, "STATE VERIFY:  reser  %s", b.c_str());
			WARN_LOG(SAVESTATE, "STATE VERIFY:  diff   %s", d.c_str());
		}
		char msg[96];
		snprintf(msg, sizeof(msg), "State verify FAIL: differs at byte %llu",
				(unsigned long long)off);
		gui_display_notification(msg, 5000);
	}
}

void dc_loadstate(int index, std::string filename)
{
	u32 total_size = 0;
	FILE *f = nullptr;

	if (filename.empty())
		filename = hostfs::getSavestatePath(index, false);
	RZipFile zipFile;
	if (zipFile.Open(filename, false))
	{
		total_size = (u32)zipFile.Size();
		if (index == -1 && config::GGPOEnable)
		{
			f = zipFile.rawFile();
			long pos = std::ftell(f);
			MD5Sum().add(f)
					.getDigest(settings.network.md5.savestate);
			std::fseek(f, pos, SEEK_SET);
			f = nullptr;
		}
	}
	else
	{
		f = nowide::fopen(filename.c_str(), "rb");

		if ( f == NULL )
		{
			// `[MEASURED 2026-09-10]` THIS RETURNS, IT DOES NOT THROW - and the
			// same is true of the malloc and I/O failures below. gui_loadState()
			// wraps this call in a try/catch, so a caller reading that function
			// would reasonably expect a failed load to surface as an exception.
			// It does not: the machine is simply left alone, and the only trace
			// is this line plus a notification that is gone in two seconds.
			//
			// That is why "the load did nothing" is a reachable state at all,
			// and it cost a while to rule out while chasing an unrelated
			// question (scripts/tests/deferredslot.lua's [OPEN] note).
			WARN_LOG(SAVESTATE, "Failed to load state - could not open %s for reading", filename.c_str());
			gui_display_notification("Save state not found", 2000);
			return;
		}
		if (index == -1 && config::GGPOEnable)
			MD5Sum().add(f)
					.getDigest(settings.network.md5.savestate);
		std::fseek(f, 0, SEEK_END);
		total_size = (u32)std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
	}
	void *data = malloc(total_size);
	if (data == nullptr)
	{
		WARN_LOG(SAVESTATE, "Failed to load state - could not malloc %d bytes", total_size);
		gui_display_notification("Failed to load state - memory full", 2000);
		if (f != nullptr)
			std::fclose(f);
		else
			zipFile.Close();
		return;
	}

	size_t read_size;
	if (f == nullptr)
	{
		read_size = zipFile.Read(data, total_size);
		zipFile.Close();
	}
	else
	{
		read_size = fread(data, 1, total_size, f);
		std::fclose(f);
	}
	if (read_size != total_size)
	{
		WARN_LOG(SAVESTATE, "Failed to load state - I/O error");
		gui_display_notification("Failed to load state - I/O error", 2000);
		free(data);
		return;
	}

	try {
		Deserializer deser(data, total_size);
		dc_loadstate(deser);
		// TAS: seek the movie to this state's frame (read-only replay). The
		// other half of the .frame sidecar written in dc_savestate. Before the
		// verify probe, so a failed seek is visible ahead of a hash mismatch.
		dojo.LoadStateFrame(filename);
		/*
			ARM THE LIVENESS WATCH. Here rather than in gui_loadState, because
			this is the one place EVERY load passes through - the hotkey, the
			auto-seek, the Lua bindings, the States wall and the replay seek all
			arrive at dc_loadstate. Arming at a caller would cover that caller.
		*/
		liveness::stateWatch().arm(framesCompleted.load(), os_GetSeconds());
		// Never breaking sync is the point, so this defaults on wherever the
		// run has to be reproducible rather than being something to remember.
		if (cfgLoadBool("dojo", "VerifyState", determinism::isDeterministicRun()))
			verifyLoadedStateIdempotent(data, total_size);
	    NOTICE_LOG(SAVESTATE, "Loaded state ver %d from %s size %d", deser.version(), filename.c_str(), total_size);
		if (deser.size() != total_size)
			WARN_LOG(SAVESTATE, "Savestate size %d but only %d bytes used", total_size, (int)deser.size());
	} catch (const Deserializer::Exception& e) {
		ERROR_LOG(SAVESTATE, "%s", e.what());
	}

	free(data);
	EventManager::event(Event::LoadState);
}

std::string get_savestate_file_path(int index, bool writable)
{
	std::string state_file = settings.content.path;
	size_t lastindex = state_file.find_last_of('/');
#ifdef _WIN32
	size_t lastindex2 = state_file.find_last_of('\\');
	if (lastindex == std::string::npos)
		lastindex = lastindex2;
	else if (lastindex2 != std::string::npos)
		lastindex = std::max(lastindex, lastindex2);
#endif
	if (lastindex != std::string::npos)
		state_file = state_file.substr(lastindex + 1);
	lastindex = state_file.find_last_of('.');
	if (lastindex != std::string::npos)
		state_file = state_file.substr(0, lastindex);

	char index_str[4] = "";
	if (index != 0) // When index is 0, use same name before multiple states is added
		sprintf(index_str, "_%d", index);

	state_file = state_file + index_str + ".state";
	if (writable)
		return get_writable_data_path(state_file);
	else
		return get_readonly_data_path(state_file);
}

std::string get_game_name()
{
	std::string state_file = settings.content.path;
	size_t lastindex = state_file.find_last_of('/');
#ifdef _WIN32
	size_t lastindex2 = state_file.find_last_of('\\');
	if (lastindex == std::string::npos)
		lastindex = lastindex2;
	else if (lastindex2 != std::string::npos)
		lastindex = std::max(lastindex, lastindex2);
#endif
	if (lastindex != std::string::npos)
		state_file = state_file.substr(lastindex + 1);
	lastindex = state_file.find_last_of('.');
	if (lastindex != std::string::npos)
		state_file = state_file.substr(0, lastindex);

	return state_file;
}

std::string get_net_savestate_file_path(bool writable)
{
	std::string path = get_savestate_file_path(0, writable);
	path.append(".net");
	return path;
}

#endif
