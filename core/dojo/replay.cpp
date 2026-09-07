#include "dojo.h"
#include "rend/video_recorder.h"
#include "tastext.h"
#include "oslib/oslib.h"
#include "json.hpp"
#include "tas_clip.h"

void Replay::Init()
{
	// Read the LIVE cfg value, not the config::ReplayFilename Option: the Option caches the value
	// loaded at startup and only refreshes on Settings::load(), which runs AFTER this. The replay
	// browser sets ReplayFilename via cfgSetVirtual just before boot, so the cached Option still
	// held the PREVIOUS session's persisted path - picking any clip silently played the most
	// recently recorded one instead (and pointed F3 at the wrong folder).
	filename = cfgLoadStr("dojo", "ReplayFilename", "");
	if (!LoadReplayFile(filename))
	{
		WARN_LOG(NETWORK, "TAS: replay init FAILED for '%s' - not entering playback", filename.c_str());
		return;
	}

	// TAS: point savestates at this clip's folder so F3/state-0 loads its BASE state and seeks here.
	hostfs::savestateFolderOverride = ghc::filesystem::path(filename).parent_path().string();
	NOTICE_LOG(NETWORK, "TAS: savestate folder -> %s (replay clip)", hostfs::savestateFolderOverride.c_str());
	// Always open a clip on BASE. config::SavestateSlot persists in emu.cfg across sessions and
	// games, so loading a movie would otherwise inherit whatever slot was last used - and F3 on an
	// inherited slot can land on a state that belongs to a different clip, or on one saved at/after
	// this movie's end, which dead-ends playback the instant you seek to it. Slot 0 is the one
	// state a clip is guaranteed to have a sensible meaning for.
	config::SavestateSlot.set(0);
	// The .set alone is NOT enough: Emulator::loadGame re-runs Settings::load(true) mid-boot,
	// which re-reads the Option from cfg and stomped this back to whatever emu.cfg held (the
	// "replay opened on slot 1" regression). Virtual entries win cfg reads, so the reload now
	// re-reads 0.
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", "0");
	NOTICE_LOG(NETWORK, "TAS: savestate slot reset to 0 (BASE) for this clip");
	dojo.BeginClipStats();

	// HEADLESS AUTO-CAPTURE, start side. dojo.cpp stops it when the movie ends;
	// without this it stopped something that never began.
	//
	// The TAS fork starts it in mainui.cpp right after the AutoSeekState seek.
	// That path is not on this branch (AutoSeekState is part of the unported UI
	// layer), so the trigger here is simply "a replay opened" - which is the
	// same intent for a clip played from frame 0, and is the only mode this
	// branch has.
	//
	// Requesting this early is safe by design: requestStart() only stashes the
	// path, and the renderer opens the encoder on the first frame it composites,
	// because that is the only place the framebuffer size is known.
	// If AutoSeekState is configured, the capture is armed AFTER the seek
	// instead (mainui.cpp), so the recording starts at the seek target rather
	// than carrying the boot and the pre-seek stretch. Arming in both places
	// would start it here and then no-op there, silently capturing the wrong
	// span.
	if (cfgLoadBool("dojo", "AutoCapture", false)
			&& cfgLoadInt("dojo", "AutoSeekState", -1) < 0)
	{
		const std::string stem = ghc::filesystem::path(filename).stem().string();
		const std::string out = hostfs::getSavestatePath(0, false).empty()
				? stem + ".avi"
				: (ghc::filesystem::path(hostfs::savestateFolderOverride) / (stem + ".avi")).string();
		videorec::requestStart(out);
		NOTICE_LOG(NETWORK, "TAS: auto-capture armed -> %s", out.c_str());
	}

	// T6 (dojo:TextApply): if a hand-edited text movie sits next to the .flyr, import it and push
	// it through THE FUNNEL - ApplyEdit diffs, applies, appends to the .flyr, and logs the
	// timeline event so states above the edit go stale exactly as they would for a rewind.
	if (cfgLoadBool("dojo", "TextApply", false))
	{
		const std::string editPath = hostfs::savestateFolderOverride + "/movie.edit.tas.txt";
		std::ifstream probe(editPath);
		if (probe.good())
		{
			probe.close();
			std::map<u32, std::vector<u8>> edited;
			std::string err;
			if (tas_text::ImportText(editPath, edited, err))
				dojo.ApplyEdit(edited, "movie.edit.tas.txt");
			else
				NOTICE_LOG(NETWORK, "TAS EDIT: import FAILED - %s", err.c_str());
		}
	}

	// PR3 self-test (dojo:ResizeProbe=lo-hi): delete frames [lo, hi] through the RESIZE
	// funnel right after load - the tail pulls down, the .flyr is rewritten shorter, the
	// guard event lands at lo. Harness: resizeguard.ps1 asserts length, shift, persistence.
	// (Delimiter is '-', NOT ',': the -config CLI splits entries on commas.)
	// One-shot: Init runs twice per session, and a relative delete is NOT idempotent -
	// unguarded it fired twice and took 2N frames (resizeguard caught it).
	{
		static bool resizeProbed = false;
		std::string rp = resizeProbed ? "" : cfgLoadStr("dojo", "ResizeProbe", "");
		resizeProbed = true;
		size_t dash = rp.find('-');
		if (dash != std::string::npos)
		{
			const u32 lo = (u32)atoi(rp.substr(0, dash).c_str());
			const u32 hi = (u32)atoi(rp.substr(dash + 1).c_str());
			if (hi >= lo)
			{
				const u32 n = hi - lo + 1;
				std::map<u32, std::vector<u8>> edited;
				for (const auto& kv : dojo.session_inputs)
				{
					if (kv.first >= lo && kv.first <= hi)
						continue;
					edited[kv.first < lo ? kv.first : kv.first - n] = kv.second;
				}
				NOTICE_LOG(NETWORK, "TAS RESIZE PROBE: deleting [%u, %u] (%u of %u frames)",
						lo, hi, n, (u32)dojo.session_inputs.size());
				dojo.ApplyEditResize(edited, "resize probe");
				NOTICE_LOG(NETWORK, "TAS RESIZE PROBE: movie now %u frames",
						(u32)dojo.session_inputs.size());
			}
		}
	}

	// T5 self-test (dojo:TextRoundTrip): the freshly loaded movie -> text -> back, byte-compared.
	// Runs here because session_inputs is complete and the clip folder is known; the .tas.txt
	// stays next to the .flyr for inspection either way.
	if (cfgLoadBool("dojo", "TextRoundTrip", false))
	{
		std::string game = get_file_basename(settings.content.fileName);
		if (game.empty())	// Init runs before content is set on a cold boot
			game = get_file_basename(cfgLoadStr("dojo", "LastRomPath", ""));
		tas_text::RoundTripSelfTest(dojo.session_inputs, hostfs::savestateFolderOverride, game);
	}

	// A replay session is strictly READ-ONLY: force auto-record off so a shadow recording can't
	// start when play_match goes false (R press / replay end). That shadow CreateReplayFile made a
	// new clip folder and re-pointed savestateFolderOverride at it - so F1/F3 silently used the NEW
	// folder's states instead of the clip's own ("F3 loads today's state" bug). Re-recording will be
	// an explicit feature, not a side effect.
	cfgSetVirtual("dojo", "RecordMatches", "no");
	cfgSetVirtual("dojo", "Transmitting", "no");

	// Stale "Just Play" scratch states in the shared folder must not shadow this clip's states.
	hostfs::wipeScratchSavestates();

	dojo.play_match = true;

	if (ggpo_session)
	{
		config::GGPOEnable = true;
		dojo.frame_number = 9;
	}
	else
	{
		config::GGPOEnable = false;
		dojo.frame_number = 0;
	}
	// Replay boot pause (David, 2026-09-04: "try seeking replays to state0 first then freeze"): arm the OSD step-hold so the
	// boot freezes at power-on; the handoff (gui.cpp) then seeks State 0 if the clip has one, and announces the clip in the
	// States window. Not for a spectate / GGPO stream (the stream drives the frames).
	if (!ggpo_session && !cfgLoadBool("dojo", "Receiving", false))
	{
		dojo.stepping = true;
		dojo.target_step_frame = 0;
		dojo.replay_bootload = true;
		dojo.boot_ready_arm = true;
	}
}

void Replay::StartRecording()
{
	dojo.play_match = false;
	analog = 2;

	if (config::RecordMatches)
		CreateReplayFile();
	else if (config::Transmitting)
	{
		std::string rom_name = GetRomNamePrefix();
		AppendHeaderToReplay(rom_name);
	}

	dojo.recording_started = true;
}

void Replay::AppendEditedFrames(const std::vector<std::pair<u32, std::vector<u8>>>& frames)
{
	if (filename.empty() || frames.empty())
		return;
	// One self-contained batch in the normal .flyr shape (same as FlushReplay writes), appended.
	// The parser is last-write-wins per frame, so these records override the originals on the
	// next load while the file keeps its full history - edits never rewrite the movie in place.
	MessageWriter mw;
	mw.AppendHeader(0, MAPLE_BUFFER);
	mw.AppendInt(MAPLE_FRAME_SIZE);
	for (const auto& fr : frames)
	{
		unsigned char rec[MAPLE_FRAME_SIZE] = { 0 };
		memcpy(rec, &fr.first, sizeof(u32));
		memcpy(rec + 4, fr.second.data(),
				std::min(fr.second.size(), (size_t)(MAPLE_FRAME_SIZE - 4)));
		mw.AppendContinuousData((const char *)rec, MAPLE_FRAME_SIZE);
	}
	std::vector<unsigned char> message = mw.Msg();
	std::ofstream fout(filename, std::ios::out | std::ios::binary | std::ios_base::app);
	fout.write((const char *)message.data(), message.size());
	fout.close();
	NOTICE_LOG(NETWORK, "TAS EDIT: appended %u frame record(s) to %s", (u32)frames.size(), filename.c_str());
}

bool Replay::RewriteReplayFile()
{
	if (filename.empty())
		return false;
	std::ofstream fout(filename, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!fout)
		return false;
	if (!file_header.empty())
		fout.write((const char *)file_header.data(), file_header.size());
	else
	{
		// no captured header (shouldn't happen for an attached file) - regenerate
		std::vector<u8> header = GenHeader(GetRomNamePrefix());
		fout.write((const char *)header.data(), header.size());
	}
	MessageWriter mw;
	u32 inBatch = 0;
	auto openBatch = [&]()
	{
		mw = MessageWriter();
		mw.AppendHeader(0, MAPLE_BUFFER);
		mw.AppendInt(MAPLE_FRAME_SIZE);
		inBatch = 0;
	};
	openBatch();
	u32 total = 0;
	for (const auto& kv : dojo.session_inputs)
	{
		unsigned char rec[MAPLE_FRAME_SIZE] = { 0 };
		memcpy(rec, &kv.first, sizeof(u32));
		memcpy(rec + 4, kv.second.data(),
				std::min(kv.second.size(), (size_t)(MAPLE_FRAME_SIZE - 4)));
		mw.AppendContinuousData((const char *)rec, MAPLE_FRAME_SIZE);
		total++;
		if (++inBatch == FRAME_BATCH)
		{
			std::vector<unsigned char> msg = mw.Msg();
			fout.write((const char *)msg.data(), msg.size());
			openBatch();
		}
	}
	if (inBatch > 0)
	{
		std::vector<unsigned char> msg = mw.Msg();
		fout.write((const char *)msg.data(), msg.size());
	}
	fout.close();
	// streaming writer state: the next recorded frame starts a clean batch
	replay_msg = MessageWriter();
	replay_frame_count = 0;
	NOTICE_LOG(NETWORK, "TAS EDIT: rewrote %s from scratch (%u frames) - length change persisted",
			filename.c_str(), total);
	return true;
}

void Replay::AppendToReplay(std::string frame, int version)
{
	if (frame.size() == MAPLE_FRAME_SIZE)
	{
		if (version >= 2)
		{
			if (replay_frame_count == 0)
			{
				replay_msg = MessageWriter();
				replay_msg.AppendHeader(0, MAPLE_BUFFER);
				replay_msg.AppendInt(MAPLE_FRAME_SIZE);
			}

			replay_msg.AppendContinuousData(frame.data(), MAPLE_FRAME_SIZE);
			replay_frame_count++;

			if (replay_frame_count % FRAME_BATCH == 0)
			{
				std::vector<unsigned char> message = replay_msg.Msg();
				std::string msg((const char *)&message[0], message.size());

				// "a file is attached" - not RecordMatches: replay sessions run with
				// RecordMatches=no, and R only flips play_match, so hand-recorded frames
				// in a replay-turned-write session batched here and were thrown away
				// (user lost a character-setup section to exactly this).
				if (!filename.empty())
				{
					std::ofstream fout(filename,
									   std::ios::out | std::ios::binary | std::ios_base::app);
					fout.write(msg.c_str(), msg.size());
					fout.close();
				}

				if (config::Transmitting)
					dojo.tcp_client.outgoing_msgs.push(msg);

				replay_msg = MessageWriter();
				replay_msg.AppendHeader(0, MAPLE_BUFFER);
				replay_msg.AppendInt(MAPLE_FRAME_SIZE);
			}

			if (memcmp(frame.data(), "0000000000000000", MAPLE_FRAME_SIZE) == 0)
			{
				// send remaining frames
				if (replay_frame_count % FRAME_BATCH > 0)
				{
					std::vector<unsigned char> message = replay_msg.Msg();
					std::string msg((const char *)message.data(), message.size());

					if (!filename.empty())	// attached file, not the mode flag
					{
						std::ofstream fout(filename,
										   std::ios::out | std::ios::binary | std::ios_base::app);
						fout.write(msg.data(), msg.size());
						fout.close();
					}

					if (config::Transmitting)
						dojo.tcp_client.outgoing_msgs.push(msg);
				}
			}
		}
	}
}

// Frames are written to the .flyr in batches of FRAME_BATCH (120). The only partial-flush path in
// AppendToReplay is guarded by a memcmp against the ASCII string "0000000000000000", which a real
// frame (it starts with a little-endian frame number) can never equal - so every recording ended
// up truncated on a 120-frame boundary, silently losing up to 119 frames (~2 s) off the tail.
// This flushes whatever is pending; call it when the recording session ends.
void Replay::FlushReplay()
{
	if (filename.empty() || replay_frame_count == 0 || (replay_frame_count % FRAME_BATCH) == 0)
		return;
	u32 pending = replay_frame_count % FRAME_BATCH;
	std::vector<unsigned char> message = replay_msg.Msg();
	std::string msg((const char *)message.data(), message.size());

	if (!filename.empty())	// attached file, not the mode flag (see AppendToReplay)
	{
		std::ofstream fout(filename, std::ios::out | std::ios::binary | std::ios_base::app);
		fout.write(msg.data(), msg.size());
		fout.close();
	}
	if (config::Transmitting)
		dojo.tcp_client.outgoing_msgs.push(msg);

	replay_msg = MessageWriter();
	replay_msg.AppendHeader(0, MAPLE_BUFFER);
	replay_msg.AppendInt(MAPLE_FRAME_SIZE);
	replay_frame_count = 0;
	NOTICE_LOG(NETWORK, "TAS: flushed %u trailing frame(s) to the replay", pending);
}

std::string currentISO8601TimeUTC()
{
	auto now = std::chrono::system_clock::now();
	auto itt = std::chrono::system_clock::to_time_t(now);
#ifndef _MSC_VER
	char buf[128] = {0};
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&itt));
	return buf;
#else
	std::ostringstream ss;
	ss << std::put_time(gmtime(&itt), "%FT%TZ");
	return ss.str();
#endif
}

std::string Replay::GetRomNamePrefix()
{
	std::string state_file = settings.content.path;
	return GetRomNamePrefix(state_file);
}

std::string Replay::GetRomNamePrefix(std::string state_file)
{
	// shamelessly stolen from nullDC.cpp#get_savestate_file_path()
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

std::string Replay::CreateReplayFile()
{
	std::string rom_name = GetRomNamePrefix();
	return CreateReplayFile(rom_name);
}

std::string Replay::CreateReplayFile(std::string rom_name, int version)
{
	auto replays_dir = ghc::filesystem::path(get_writable_data_path("replays"));
	auto game_replays_dir = replays_dir / get_game_name();

	// create timestamp string, iso8601 format
	std::string timestamp = currentISO8601TimeUTC();
	std::replace(timestamp.begin(), timestamp.end(), ':', '_');

	// TAS: one timestamped folder per recording, so the movie and its savestates live together.
	auto clip_dir = game_replays_dir / timestamp;
	if (!ghc::filesystem::exists(clip_dir))
		ghc::filesystem::create_directories(clip_dir);

	std::string replay_name = rom_name + "__" +
							  timestamp + "__" +
							  config::PlayerName.get() + "__" +
							  settings.dojo.OpponentName + "__";

	if (version == 0)
		replay_name.append(".flyreplay");
	else if (version >= 1)
		replay_name.append(".flyr");

	ghc::filesystem::path replay_path = clip_dir / replay_name;

	// create replay file itself
	std::ofstream file;
	file.open(replay_path.string());

	filename = replay_path.string();
	cfgSaveStr("dojo", "ReplayFilename", replay_path.string());

	// TAS: point F1/F3 savestates into this clip folder so they sit next to the .flyr.
	hostfs::savestateFolderOverride = clip_dir.string();
	NOTICE_LOG(NETWORK, "TAS: savestate folder -> %s (new recording)", hostfs::savestateFolderOverride.c_str());
	// Same reason as playback: a fresh clip has no states at all, so the slot inherited from the
	// last session points at nothing. Start every clip on BASE.
	config::SavestateSlot.set(0);
	// The .set alone is NOT enough: Emulator::loadGame re-runs Settings::load(true) mid-boot,
	// which re-reads the Option from cfg and stomped this back to whatever emu.cfg held (the
	// "replay opened on slot 1" regression). Virtual entries win cfg reads, so the reload now
	// re-reads 0.
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", "0");
	NOTICE_LOG(NETWORK, "TAS: savestate slot reset to 0 (BASE) for this clip");
	dojo.BeginClipStats();

	// TAS: per-clip metadata seed. The startup prompt may have staged tags/notes for this
	// session (dojo:PendingTags/PendingNotes, virtual); they land here and are then cleared.
	// The file lives IN the clip folder so it travels/dies with the clip - no central index.
	tas_clip::seed(clip_dir.string(), rom_name, timestamp, cfgLoadStr("dojo", "PendingTags", ""), cfgLoadStr("dojo", "PendingNotes", ""));	// P2: one writer
	cfgSetVirtual("dojo", "PendingTags", "");
	cfgSetVirtual("dojo", "PendingNotes", "");
	// Stale "Just Play" scratch states in the shared folder must not shadow this clip's states.
	hostfs::wipeScratchSavestates();

	if (version > 0)
		AppendHeaderToReplay(rom_name);

	return replay_path.string();
}

std::vector<u8> Replay::GenHeader(std::string rom_name)
{
	MessageWriter spectate_start;

	spectate_start.AppendHeader(1, SPECTATE_START);

	// version
	u32 version = 4;
	spectate_start.AppendInt(version);
	if (rom_name == "")
		spectate_start.AppendString(GetRomNamePrefix());
	else
		spectate_start.AppendString(rom_name);
	spectate_start.AppendString(config::PlayerName.get());
	spectate_start.AppendString(settings.dojo.OpponentName);

	spectate_start.AppendString(config::Quark.get());
	std::string server_key = cfgLoadStr("dojo", "RelayServer", "") + "#" + cfgLoadStr("dojo", "RelayKey", "");
	spectate_start.AppendString(server_key);

	u32 analogAxes = analog;
	if (settings.platform.system == DC_PLATFORM_DREAMCAST && config::GGPOEnable)
		analogAxes = (u32)config::GGPOAnalogAxes.get();
	spectate_start.AppendInt(analogAxes);

	u32 precise_triggers = (u32)(!config::GGPOEnable.get());
	spectate_start.AppendInt(precise_triggers);

	u32 ggpo = 0;
	if (config::GGPOEnable)
		ggpo = 1;
	spectate_start.AppendInt(ggpo);

	spectate_start.AppendString(settings.dojo.P1CountryCode);
	spectate_start.AppendString(settings.dojo.P2CountryCode);

	// if (version == 3)
	//{
	// spectate_start.AppendString(settings.dojo.state_md5);
	// spectate_start.AppendString(settings.dojo.state_commit);
	//}

	std::vector<u8> message = spectate_start.Msg();

	return message;
}

void Replay::AppendHeaderToReplay(std::string rom_name)
{
	std::vector<u8> message = GenHeader(rom_name);
	file_header = message;		// a later rewrite must reproduce these exact bytes
	std::string msg((const char *)message.data(), message.size());

	if (config::RecordMatches)
	{
		std::ofstream fout(filename,
						   std::ios::out | std::ios::binary | std::ios_base::app);

		fout.write(msg.data(), msg.size());
		fout.close();
	}

	if (config::Transmitting)
		dojo.tcp_client.outgoing_msgs.push(msg);
}

bool Replay::LoadReplayFile(std::string path)
{
	NOTICE_LOG(NETWORK, "LOAD REPLAY FILE %s", path.data());
	if (ghc::filesystem::exists(path))
	{
		LoadReplayFileV1(path);
		return true;
	}
	else
	{
		cfgSetVirtual("dojo", "Replay", "no");
		return false;
	}
}

u32 Replay::GetFrameNumber(u8 *data)
{
	return (int)(*(u32 *)(data));
}

void Replay::LoadReplayFileV1(std::string path)
{
	// add string in increments of FRAME_SIZE to net_inputs
	std::ifstream fin(path,
					  std::ios::in | std::ios::binary);

	char header_buf[HEADER_LEN] = {0};
	std::vector<unsigned char> body_buf;
	int offset = 0;

	// NOTICE_LOG(NETWORK, "BEFORE LOOP");
	// read messages until file ends
	while (fin)
	{
		// NOTICE_LOG(NETWORK, "BEGINNING LOOP");
		//  read header
		memset((void *)header_buf, 0, HEADER_LEN);
		fin.read(header_buf, HEADER_LEN);

		unsigned int body_size = HeaderReader::GetSize((unsigned char *)header_buf);
		// unsigned int seq = HeaderReader::GetSeq((unsigned char *)header_buf);
		HeaderReader::GetSeq((unsigned char *)header_buf);
		unsigned int cmd = HeaderReader::GetCmd((unsigned char *)header_buf);

		// NOTICE_LOG(NETWORK, "READ HEADER %u", cmd);
		//  read body
		body_buf.resize(body_size);
		fin.read((char *)body_buf.data(), body_size);

		offset = 0;

		if (cmd == SPECTATE_START)
		{
			// keep the raw header message for RewriteReplayFile (see file_header)
			file_header.assign(header_buf, header_buf + HEADER_LEN);
			file_header.insert(file_header.end(), body_buf.begin(), body_buf.end());
		}

		// NOTICE_LOG(NETWORK, "PROCESS BODY");
		dojo.ProcessBody(cmd, body_size, (const char *)body_buf.data(), &offset);
	}

	replay_loaded = true;
	// if (final_p1_wins > 0 || final_p2_wins > 0)
	//{
	//	std::cout << "Final P1 Score: " << final_p1_wins << std::endl;
	//	std::cout << "Final P2 Score: " << final_p2_wins << std::endl;
	// }
}

size_t CurlWrite_CallbackFunc_StdString(void *contents, size_t size, size_t nmemb, std::string *s)
{
	size_t newLength = size * nmemb;
	try
	{
		s->append((char *)contents, newLength);
	}
	catch (std::bad_alloc &e)
	{
		// handle memory problem
		return 0;
	}
	return newLength;
}

std::string encodeURIComponent(std::string const &value)
{
	std::ostringstream oss;
	oss << std::hex;
	for (auto c : value)
	{
		int uc = static_cast<unsigned char>(c);
		if (((0x30 <= uc) && (uc <= 0x39)) || ((0x41 <= uc) && (uc <= 0x5A)) || ((0x61 <= uc) && (uc <= 0x7A)))
		{
			oss << c;
			continue;
		}
		switch (c)
		{
		case '-':
			oss << c;
			break;
		case '_':
			oss << c;
			break;
		case '.':
			oss << c;
			break;
		case '!':
			oss << c;
			break;
		case '~':
			oss << c;
			break;
		case '*':
			oss << c;
			break;
		case '\'':
			oss << c;
			break;
		case '(':
			oss << c;
			break;
		case ')':
			oss << c;
			break;
		default:
			oss << std::uppercase << '%' << std::setw(2) << uc << std::nouppercase;
			break;
		}
	}
	return oss.str();
}

std::string Replay::DownloadReplayJson(std::string game_name)
{
	if (dojo.replay.remote_replay_json.empty())
		dojo.replay.remote_replay_json = "{}";

	std::string json_url = "https://skunkworks.match.dojo.ooo/api/v1/replays?player=&game=" + encodeURIComponent(game_name) + "&match_code=";
	std::string json_filename = game_name + "_replays.json";
	dojo_file.DownloadFile(json_url, "cache", json_filename, "");

	std::string replay_json_path = get_writable_data_path("cache") + "\\" + json_filename;
	std::ifstream file(replay_json_path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return "{}";

    std::string s{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

	remote_replay_json = s;
	return s;
}
