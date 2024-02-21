#include "dojo.h"

void Dojo::AssignPlayerNames()
{
	hosting = config::ActAsServer;

	if (hosting)
	{
		player_1 = settings.dojo.PlayerName;
		player_2 = settings.dojo.OpponentName;
	}
	else
	{
		player_1 = settings.dojo.OpponentName;
		player_2 = settings.dojo.PlayerName;
	}

	if (config::StreamTxtOutput)
	{
		WriteStringToOut("p1name", player_1);
		WriteStringToOut("p2name", player_2);
	}
}

void Dojo::InitScore()
{
	p1_wins = 0;
	p2_wins = 0;

	if (config::StreamTxtOutput)
	{
		WriteStringToOut("p1wins", std::to_string(dojo.p1_wins));
		WriteStringToOut("p2wins", std::to_string(dojo.p2_wins));
	}
}

void Dojo::RegisterPlayerWin(int player)
{
	if (player == 0)
	{
		NOTICE_LOG(NETWORK, "P1 WIN", p1_wins);
		p1_wins++;

		if (config::StreamTxtOutput)
			WriteStringToOut("p1wins", std::to_string(p1_wins));
	}
	else if (player == 1)
	{
		NOTICE_LOG(NETWORK, "P2 WIN", p2_wins);
		p2_wins++;

		if (config::StreamTxtOutput)
			WriteStringToOut("p2wins", std::to_string(p2_wins));
	}

	last_score_frame = (u32)FrameNumber;
}

bool Dojo::ScoreAvailable()
{
	bool score_available = false;

	std::string score_games[] =
		{
			"AKATSUKI BK AUSF. ACHSE",
			"CAPCOM VS SNK  JAPAN",
			"CAPCOM VS SNK 2  JAPAN",
			"HOKUTO NO KEN",
			"JINGI STORM THE ARCADE",
			"MARVEL VS CAPCOM2  JAPAN",
			"MOERO JUSTICE GAKUEN  JAPAN",
			" POWER SMASH 2 -----------",
			"SAMURAI SPIRITS 6",
			"THE KING OF FIGHTERS XI",
			"The Rumble Fish 2",
			"TOY FIGHTER",
			"VF4 FINAL TUNED JAPAN",
		};

	for (auto it = std::begin(score_games); it != std::end(score_games); ++it)
	{
		if (settings.content.gameId.find(*it) != std::string::npos)
		{
			score_available = true;
		}
	}

	return score_available;
}

void Dojo::FirstToPoll()
{
	if (config::FirstTo > 0 && (p1_wins == config::FirstTo.get() || p2_wins == config::FirstTo.get()))
	{
		u32 cooldown_frames = 1200;
		u32 frame_num = (u32)FrameNumber.load();

		if (frame_num > (last_score_frame + cooldown_frames))
		{
			if (ggpo::active)
				ggpo::stopSession();
			else if (config::GGPOEnable)
				gui_setState(GuiState::Disconnected);
			else
				gui_stop_game();
		}
	}
}

void Dojo::UpdateScore()
{
	uint32_t cooldown_frames = 600;

	u32 frame_num = FrameNumber.load();
	if (frame_num < (last_score_frame + cooldown_frames))
		return;

	if (ScoreAvailable())
	{
		uint32_t detected_p1_wins;
		uint32_t detected_p2_wins;

		if (settings.content.gameId == "MOERO JUSTICE GAKUEN  JAPAN" ||
			settings.content.gameId == "SAMURAI SPIRITS 6" ||
			settings.content.gameId == "VF4 FINAL TUNED JAPAN")
		{
			uint32_t winning_streak;
			uint32_t champion_player;

			if (settings.content.gameId == "MOERO JUSTICE GAKUEN  JAPAN")
			{
				champion_player = ReadMem8_nommu(0x8C2EED95);
				winning_streak = ReadMem8_nommu(0x8C2EED96);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}
			else if (settings.content.gameId == "SAMURAI SPIRITS 6")
			{
				champion_player = ReadMem8_nommu(0x8C2F7CF0);
				winning_streak = ReadMem8_nommu(0x8C2F7D18);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}
			else if (settings.content.gameId == "VF4 FINAL TUNED JAPAN")
			{
				champion_player = ReadMem8_nommu(0x8C2925D4);
				winning_streak = ReadMem8_nommu(0x8C2925DC);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}

			if (champion_player == 0)
			{
				detected_p1_wins = winning_streak;
				detected_p2_wins = 0;
			}
			else if (champion_player == 1)
			{
				detected_p2_wins = winning_streak;
				detected_p1_wins = 0;
			}
			// double ko, samsptk
			else if (champion_player == 255)
			{
				champion_player = 0;
				winning_streak = 0;

				detected_p1_wins = 0;
				detected_p2_wins = 0;
			}

			if (champion_player != 255)
			{
				if (current_p1_wins + 1 == detected_p1_wins)
				{
					RegisterPlayerWin(0);
				}

				if (current_p2_wins + 1 == detected_p2_wins)
				{
					RegisterPlayerWin(1);
				}
			}

			current_p1_wins = detected_p1_wins;
			current_p2_wins = detected_p2_wins;

			FirstToPoll();

			return;
		}

		if (settings.content.gameId == "AKATSUKI BK AUSF. ACHSE")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C19609C);
			detected_p2_wins = ReadMem8_nommu(0x8C196388);
		}
		else if (settings.content.gameId == "CAPCOM VS SNK  JAPAN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C2357A3);
			detected_p2_wins = ReadMem8_nommu(0x8C235DAF);
		}
		else if (settings.content.gameId == "CAPCOM VS SNK 2  JAPAN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C241D3F);
			detected_p2_wins = ReadMem8_nommu(0x8C24230B);
		}
		else if (settings.content.gameId == "HOKUTO NO KEN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C4939BE);
			detected_p2_wins = ReadMem8_nommu(0x8C4939CA);
		}
		else if (settings.content.gameId == "JINGI STORM THE ARCADE")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C1C996C);
			detected_p2_wins = ReadMem8_nommu(0x8C1C9970);
		}
		else if (settings.content.gameId == " POWER SMASH 2 -----------")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C23DE6C);
			detected_p2_wins = ReadMem8_nommu(0x8C23DEE4);
		}
		else if (settings.content.gameId == "THE KING OF FIGHTERS XI")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C27CBB8);
			detected_p2_wins = ReadMem8_nommu(0x8C27CDB0);
		}
		else if (settings.content.gameId == "The Rumble Fish 2")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C3A59B0);
			detected_p2_wins = ReadMem8_nommu(0x8C3A59B4);
		}
		else if (settings.content.gameId == "TOY FIGHTER")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C0F8BCC);
			detected_p2_wins = ReadMem8_nommu(0x8C0F8BCD);
		}
		else if (settings.content.gameId == "MARVEL VS CAPCOM2  JAPAN")
		{
			uint32_t in_match = ReadMem8_nommu(0x8C2F836C);
			if (in_match == 0)
				return;
			detected_p1_wins = ReadMem8_nommu(0x8C2F83D4);
			detected_p2_wins = ReadMem8_nommu(0x8C2F83D5);
		}

		if (current_p1_wins + 1 == detected_p1_wins)
		{
			RegisterPlayerWin(0);
		}

		if (current_p2_wins + 1 == detected_p2_wins)
		{
			RegisterPlayerWin(1);
		}

		current_p1_wins = detected_p1_wins;
		current_p2_wins = detected_p2_wins;

		FirstToPoll();
	}
}

void Dojo::WriteStringToOut(std::string name, std::string contents)
{
#ifndef __ANDROID__
	auto dir_name = get_writable_config_path("out/");
	if (!std::filesystem::exists(dir_name))
		std::filesystem::create_directory(dir_name);

	std::string path = dir_name + name + ".txt";
	std::ofstream fout(path);
	fout << contents;
	fout.close();
#endif
}

std::string Dojo::GetTrainingLua()
{
	if (settings.content.gameId == "T1249M")
			return get_readonly_config_path("training/cvs2.lua");
	else if (settings.content.gameId == "T1212N")
			return get_readonly_config_path("training/mvsc2.lua");
	else
	{
		// look up by game file name in training folder
		std::string lua_file = settings.content.path;
		size_t lastindex = lua_file.find_last_of('/');
#ifdef _WIN32
		size_t lastindex2 = lua_file.find_last_of('\\');
		if (lastindex == std::string::npos)
			lastindex = lastindex2;
		else if (lastindex2 != std::string::npos)
			lastindex = std::max(lastindex, lastindex2);
#endif
		if (lastindex != std::string::npos)
			lua_file = lua_file.substr(lastindex + 1);
		lastindex = lua_file.find_last_of('.');
		if (lastindex != std::string::npos)
			lua_file = lua_file.substr(0, lastindex);

		lua_file = lua_file + ".lua";
		auto lua_path = get_readonly_data_path("training/" + lua_file);

		if (std::filesystem::exists(lua_path))
			return lua_path;
	}

	return "";
}
