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

#include "cfg/option.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "network/ggpo.h"
#include "rend/gui.h"

#include "message_writer.h"
#include "message_reader.h"

#include "net_beacon.h"
#include "replay.h"

#define MAPLE_FRAME_SIZE 28
#define FRAME_BATCH 120

#include "input/gamepad_device.h"

constexpr int MAX_PLAYERS = 2;

constexpr u32 BTN_TRIGGER_LEFT = DC_BTN_BITMAPPED_LAST << 1;
constexpr u32 BTN_TRIGGER_RIGHT = DC_BTN_BITMAPPED_LAST << 2;

#pragma pack(push, 1)
struct FrameInputs
{
	u32 kcode:20;
	u32 mouseButtons:4;
	u32 kbModifiers:8;

	union {
		struct {
			u8 x;
			u8 y;
		} analog;
		struct {
			s16 x;
			s16 y;
		} absPos;
		struct {
			s16 x;
			s16 y;
			s16 wheel;
		} relPos;
		u8 keys[6];
	} u;
	struct {
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

	void AssignPlayerNames();

	bool hosting;
	std::string player_1;
	std::string player_2;

	std::atomic<u32> frame_number = {0};
	std::map<uint32_t, std::vector<uint8_t>> session_inputs;
	std::map<uint32_t, std::vector<uint8_t>> rec_inputs;

	std::string match_code = "";

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

	void FillDelayFrames();
	void MapleRecordAction(MapleInputState inputState[4]);
	void MapleApplyAction(MapleInputState inputState[4]);
	void PrintInputs(int player, FrameInputs inputs);
	void PrintMapleInputState(MapleInputState inputState[4]);

	bool playback_loop;
	bool playing_input;
	bool trigger_playback;
	u32 next_playback_frame;
	int record_player = 0;

	bool player_switched;
	int current_record_slot = 0;

	bool recording = false;
	bool recording_started = false;
	std::vector<std::string> record_slot[3];
	std::set<int> recorded_slots;

	void TrainingSwitchPlayer();
	void ToggleRecording(int slot);
	void TogglePlayback(int slot);
	void TogglePlayback(int slot, bool hide_slot);
	void ToggleRandomPlayback();
	void PlayRecording(int slot);
	void ResetTraining();

	std::vector<u8> FilterPlayerInput(int player, int size, unsigned char *bits);
	std::vector<u8> SwapPlayerInputs(int size, unsigned char *bits);
};

extern Dojo dojo;
