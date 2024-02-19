#pragma once

#include <iostream>
#include <string>

#include "cfg/option.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "network/ggpo.h"
#include "rend/gui.h"

class Dojo
{
public:
	void AssignPlayerNames();

	bool hosting;
	std::string player_1;
	std::string player_2;

	std::atomic<u32> FrameNumber = {0};

	void RegisterPlayerWin(int player);
	bool ScoreAvailable();
	void UpdateScore();
	void FirstToPoll();

	uint32_t p1_wins = 0;
	uint32_t p2_wins = 0;

	uint32_t current_p1_wins = 0;
	uint32_t current_p2_wins = 0;

	uint32_t last_score_frame = 0;
};

extern Dojo dojo;
