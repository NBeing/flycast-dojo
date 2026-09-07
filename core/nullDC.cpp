#ifndef LIBRETRO
#include "types.h"
#include "emulator.h"
#include "hw/mem/addrspace.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "log/LogManager.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "debug/gdb_server.h"
#include "archive/rzip.h"
#include "rend/mainui.h"
#include "input/gamepad_device.h"
#include "lua/lua.h"
#include "stdclass.h"
#include "serialize.h"
#include "determinism.h"
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
	os_CreateWindow();
	os_SetupInput();

	if(config::GDB)
		debugger::init(config::GDBPort);
	lua::init();

	if(config::ProfilerEnabled)
		LogManager::GetInstance()->SetEnable(LogTypes::PROFILER, true);

	if (cfgLoadBool("dojo", "Replay", false))
		dojo.replay.Init();

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
	gui_cancel_load();
	lua::term();
	emu.term();
	gui_term();
	os_TermInput();
}

void dc_savestate(int index)
{
	if (settings.network.online)
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
