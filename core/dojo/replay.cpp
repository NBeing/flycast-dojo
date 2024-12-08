#include "dojo.h"

void Replay::Init()
{
	filename = config::ReplayFilename.get();
	if (!LoadReplayFile(filename))
		return;

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

				if (config::RecordMatches)
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
			}
		}
	}
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
	auto replays_dir = std::filesystem::path(get_writable_data_path("replays"));
	auto game_replays_dir = replays_dir / get_game_name();

	if (!std::filesystem::exists(game_replays_dir))
		std::filesystem::create_directories(game_replays_dir);

	// create timestamp string, iso8601 format
	std::string timestamp = currentISO8601TimeUTC();
	std::replace(timestamp.begin(), timestamp.end(), ':', '_');

	std::string replay_name = rom_name + "__" +
							  timestamp + "__" +
							  config::PlayerName.get() + "__" +
							  settings.dojo.OpponentName + "__";

	if (version == 0)
		replay_name.append(".flyreplay");
	else if (version >= 1)
		replay_name.append(".flyr");

	std::filesystem::path replay_path =
		std::filesystem::path(game_replays_dir) / replay_name;

	// create replay file itself
	std::ofstream file;
	file.open(replay_path.string());

	filename = replay_path.string();
	cfgSaveStr("dojo", "ReplayFilename", replay_path.string());

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
	spectate_start.AppendString(config::RelayKey.get());

	u32 analogAxes = analog;
	if (settings.platform.system == DC_PLATFORM_DREAMCAST && config::GGPOEnable)
		analogAxes = (u32)config::GGPOAnalogAxes.get();
	spectate_start.AppendInt(analogAxes);

	u32 precise_triggers = (u32)(!config::GGPOEnable.get());
	spectate_start.AppendInt(precise_triggers);

	u32 ggpo = 0;
	if (config::GGPOEnable)
	{
		ggpo = 1;
		std::cout << "GGPO SESSION DETECTED" << std::endl;
	}
	spectate_start.AppendInt(ggpo);

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
	if (std::filesystem::exists(path))
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

std::string Replay::DownloadReplayJson(std::string game_name)
{
	std::string json_url = "https://skunkworks.match.dojo.ooo/api/v1/replays?player=&game=" + game_name + "&match_code=";
	auto curl = curl_easy_init();

	std::string s;
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, json_url.data());

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 2L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
					curl_easy_strerror(res));
		}

		curl_easy_cleanup(curl);
	}

	remote_replay_json = s;
	return s;
}
