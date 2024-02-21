#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>


#include "cfg/option.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "network/ggpo.h"
#include "rend/gui.h"

#include "net_beacon.h"

class Dojo
{
public:
	void AssignPlayerNames();

	bool hosting;
	std::string player_1;
	std::string player_2;

	std::atomic<u32> FrameNumber = {0};

	void InitScore();
	void RegisterPlayerWin(int player);
	bool ScoreAvailable();
	void UpdateScore();
	void FirstToPoll();

	uint32_t p1_wins = 0;
	uint32_t p2_wins = 0;

	uint32_t current_p1_wins = 0;
	uint32_t current_p2_wins = 0;

	uint32_t last_score_frame = 0;

	void WriteStringToOut(std::string name, std::string contents);

	std::string GetTrainingLua();

	std::string match_code = "";

	int StartSession();
	bool disconnect_toggle = false;

	NetBeacon presence;

	std::string game_name;
};

extern Dojo dojo;
