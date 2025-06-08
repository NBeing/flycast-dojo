#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>
#include <utility>

#include <array>
#include <bitset>
#include <queue>

#include "deps/filesystem.hpp"

#include "cfg/option.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "network/ggpo.h"
#include "rend/gui.h"

#include "message_writer.h"
#include "message_reader.h"

#include "net_beacon.h"
#include "replay.h"
#include "training.h"
#include "relay_client.h"
#include "dojo_file.h"
#include "tcp_client.h"

#define MAPLE_FRAME_SIZE 28
#define FRAME_BATCH 120

#include "input/gamepad_device.h"

constexpr int MAX_PLAYERS = 2;

constexpr u32 BTN_TRIGGER_LEFT = DC_BTN_BITMAPPED_LAST << 1;
constexpr u32 BTN_TRIGGER_RIGHT = DC_BTN_BITMAPPED_LAST << 2;

#pragma pack(push, 1)
struct FrameInputs
{
	u32 kcode : 20;
	u32 mouseButtons : 4;
	u32 kbModifiers : 8;

	union
	{
		struct
		{
			u8 x;
			u8 y;
		} analog;
		struct
		{
			s16 x;
			s16 y;
		} absPos;
		struct
		{
			s16 x;
			s16 y;
			s16 wheel;
		} relPos;
		u8 keys[6];
	} u;
	struct
	{
		u8 l;
		u8 r;
	} triggers;
};
#pragma pack(pop)

class Dojo
{
public:
	NetBeacon presence;
	Replay replay;
	Training training;

	void AssignPlayerNames();

	bool hosting;
	std::string player_1;
	std::string player_2;

	std::atomic<u32> frame_number = {0};
	std::map<uint32_t, std::vector<uint8_t>> session_inputs;
	std::map<uint32_t, std::vector<uint8_t>> rec_inputs;

	u32 last_applied_frame = 0;

	bool play_match = false;
	bool precise_triggers = true;

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

	std::string game_name;
	bool commandLineStart;
	bool disconnect_toggle = false;

	std::string GetEntryPath(std::string entry);

	void PollRecordAction(int frame, int size, unsigned char *bits);
	void RecRecordAction(int frame, int size, unsigned char *bits);
	void GGPORecordAction(int frame, int size, unsigned char *bits);

	bool recording_started = false;

	void FillDelayFrames();
	void MapleRecordAction(MapleInputState inputState[4]);
	void MapleApplyAction(MapleInputState inputState[4]);
	void PrintInputs(int player, FrameInputs inputs);
	void PrintMapleInputState(MapleInputState inputState[4]);

	std::array<std::map<u32, std::bitset<18>>, 2> displayed_inputs;
	std::array<std::map<u32, std::string>, 2> displayed_inputs_str;
	std::map<u32, std::string> last_displayed_inputs_str;
	std::array<std::map<u32, std::string>, 2> displayed_dirs_str;
	std::array<std::map<u32, u32>, 2> displayed_inputs_duration;
	std::array<std::bitset<18>, 2> last_held_input;
	std::array<std::map<u32, std::vector<bool>>, 2> displayed_dirs;
	std::array<std::map<u32, int>, 2> displayed_num_dirs;

	void AddToInputDisplay(MapleInputState inputState[4]);
	void ResetInputDisplay();

	std::set<int> button_check_pressed[2];

	std::string current_gamepad;

	void ProcessBody(unsigned int cmd, unsigned int body_size, const char *buffer, int *offset);

	void SaveRecordSlotsFile();
	void LoadRecordSlotsFile();
	void LoadRecordSlotsFile(std::string filename);

	void Split(std::string const &str, const char delim, std::vector<std::string> &out);
	void Replace(std::string &subject, const std::string &search, const std::string &replace);

	uint64_t UnixTimestamp();

	RelayClient relay_client;
	TcpClient tcp_client;

	bool stepping = false;
	bool buffering = false;
	bool manual_pause = false;
	u32 target_step_frame = 0;

	void ResetPause();
	void Reset();
};

extern Dojo dojo;
