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
	CreateReplayFile();
}

void Replay::AppendToFile(std::string frame, int version)
{
	if (frame.size() == MAPLE_FRAME_SIZE)
	{
		// append frame data to replay file
		std::ofstream fout(filename,
						   std::ios::out | std::ios::binary | std::ios_base::app);

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
				fout.write((const char *)&message[0], message.size());

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
					fout.write((const char *)&message[0], message.size());
				}
			}
		}

		fout.close();
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
	if (!std::filesystem::exists(get_writable_data_path("replays")))
		std::filesystem::create_directory(get_writable_data_path("replays"));

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
		std::filesystem::path(get_writable_data_path("replays")) / replay_name;

	// create replay file itself
	std::ofstream file;
	file.open(replay_path.string());

	filename = replay_path.string();
	cfgSaveStr("dojo", "ReplayFilename", replay_path.string());

	if (version > 0)
		AppendHeaderToFile(rom_name);

	return replay_path.string();
}

void Replay::AppendHeaderToFile(std::string rom_name)
{
	std::ofstream fout(filename,
					   std::ios::out | std::ios::binary | std::ios_base::app);

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

	// spectate_start.AppendString(config::Quark.get());
	// spectate_start.AppendString(config::MatchCode.get());

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

	std::vector<unsigned char> message = spectate_start.Msg();

	fout.write((const char *)spectate_start.Msg().data(), (std::streamsize)(spectate_start.GetSize() + (unsigned int)HEADER_LEN));
	fout.close();
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

void Replay::ProcessBody(unsigned int cmd, unsigned int body_size, const char *buffer, int *offset)
{
	// NOTICE_LOG(NETWORK, "CMD %u", cmd);
	if (cmd == 0)
		return;

	if (cmd == SPECTATE_START)
	{
		unsigned int v = MessageReader::ReadInt((const char *)buffer, offset);
		std::string GameName = MessageReader::ReadString((const char *)buffer, offset);
		std::string PlayerName = MessageReader::ReadString((const char *)buffer, offset);
		std::string OpponentName = MessageReader::ReadString((const char *)buffer, offset);
		// std::string Quark = MessageReader::ReadString((const char*)buffer, offset);
		// std::string MatchCode = MessageReader::ReadString((const char*)buffer, offset);
		unsigned int analog = MessageReader::ReadInt((const char *)buffer, offset);
		unsigned int precise_triggers = MessageReader::ReadInt((const char *)buffer, offset);
		unsigned int ggpo = MessageReader::ReadInt((const char *)buffer, offset);

		version = v;
		dojo.game_name = GameName;
		settings.content.path = dojo.GetEntryPath(dojo.game_name);
		// config::Quark = Quark;
		// config::MatchCode = MatchCode;
		dojo.precise_triggers = (bool)precise_triggers;
		if (ggpo)
			ggpo_session = true;

		NOTICE_LOG(NETWORK, "v %u GameName %s PlayerName %s OpponentName %s analog %d", v, GameName.data(), PlayerName.data(), OpponentName.data(), analog);

		// settings.content.path = GetEntryPath(GameName);
		// std::string entry_path =  GetEntryPath(game_name);
		// settings.content.path = entry_path + ".zip";
		// NOTICE_LOG(NETWORK, "ENTRY %s", settings.content.path.data());

		// if (version >= 3)
		//{
		//	settings.dojo.state_md5 = MessageReader::ReadString((const char*)buffer, offset);
		//	settings.dojo.state_commit = MessageReader::ReadString((const char*)buffer, offset);
		// }

		std::cout << "Replay Version: " << version << std::endl;

		if (version >= 2)
		{
			analog = analog;
			// last_consecutive_common_frame = 0;
			dojo.frame_number = 0;
		}
		else
			analog = 0;

		// std::cout << "REPLAY VERSION " << version << "ANALOG " << analog << std::endl;

		std::cout << "Game: " << GameName << std::endl;

		// if (!received_player_info)
		//{
		//	settings.dojo.PlayerName = PlayerName;
		//	settings.dojo.OpponentName = OpponentName;
		//	AssignPlayerNames();
		//	std::cout << "Player: " << PlayerName << std::endl;
		//	std::cout << "Opponent: " << OpponentName << std::endl;
		// }

		// std::cout << "Quark: " << Quark << std::endl;
		// std::cout << "Match Code: " << MatchCode << std::endl;

		// if (version == 3)
		//{
		//	std::cout << "Savestate MD5: " << settings.dojo.state_md5 << std::endl;
		//	std::cout << "Savestate Commit SHA: " << settings.dojo.state_commit << std::endl;
		// }

		// dojo.receiver_header_read = true;
		// dojo.receiver_start_read = true;
	}
	else if (cmd == PLAYER_INFO)
	{
		auto p1_info = MessageReader::ReadPlayerInfo(buffer, offset);
		auto p2_info = MessageReader::ReadPlayerInfo(buffer, offset);

		auto player_name = p1_info[0];
		auto opponent_name = p2_info[0];

		std::cout << "P1: " << player_name << std::endl;
		std::cout << "P2: " << opponent_name << std::endl;

		settings.dojo.PlayerName = player_name;
		settings.dojo.OpponentName = opponent_name;

		// received_player_info = true;

		// AssignPlayerNames();
	}
	else if (cmd == MAPLE_BUFFER)
	{
		unsigned int frame_size = MessageReader::ReadInt((const char *)buffer, offset);

		// read frames
		while ((unsigned int)*offset < body_size)
		{
			std::string frame = MessageReader::ReadContinuousData((const char *)buffer, offset, frame_size);

			// if (memcmp(frame.data(), { 0 }, FRAME_SIZE) == 0)
			if (memcmp(frame.data(), "00000000000000000000", MAPLE_FRAME_SIZE) == 0)
			{
				// dojo.receiver_ended = true;
			}
			else
			{
				u32 frame_num = GetFrameNumber((u8 *)frame.data());
				// NOTICE_LOG(NETWORK, "FRAME NUM %u", frame_num);
				std::string maple_input(frame.data() + 4, (MAPLE_FRAME_SIZE - 4));
				std::vector<u8> inputs(maple_input.begin(), maple_input.end());

				dojo.session_inputs[frame_num] = inputs;

				/*
				std::cout << "GGPO FRAME " << frame_num << " ";

				for (int i = 0; i < (MAPLE_FRAME_SIZE - 4); i++)
				{
				  std::bitset<8> b(inputs[i]);
				  std::cout << b.to_string();
				}

				std::cout << std::endl;
				*/

				// buffer stream
				/*
				if (dojo.session_inputs.size() == config::RxFrameBuffer.get() &&
					dojo.frame_number < dojo.last_consecutive_common_frame)
					dojo.resume();
					*/
			}
		}
	}
	else if (cmd == PLAYER_WIN)
	{
		// u32 player = MessageReader::ReadPlayerWin(buffer, offset);

		// if (player == 0)
		//	final_p1_wins++;
		// else if (player == 1)
		//	final_p2_wins++;
	}
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
		ProcessBody(cmd, body_size, (const char *)body_buf.data(), &offset);
	}

	replay_loaded = true;
	// if (final_p1_wins > 0 || final_p2_wins > 0)
	//{
	//	std::cout << "Final P1 Score: " << final_p1_wins << std::endl;
	//	std::cout << "Final P2 Score: " << final_p2_wins << std::endl;
	// }
}
