#pragma once

#include "dojo.h"

class Training
{
public:
	bool playback_loop;
	bool playing_input;
	bool trigger_playback;
	u32 next_playback_frame;
	int control_player = 0;

	bool player_switched;
	int current_record_slot = 0;

	bool recording = false;
	bool recording_started = false;
	std::vector<std::string> record_slot[3];
	std::set<int> recorded_slots;

	void SwitchPlayer();
	void ToggleRecording(int slot);
	void TogglePlayback(int slot);
	void TogglePlayback(int slot, bool hide_slot);
	void ToggleRandomPlayback();
	void PlayRecording(int slot);
	void ResetTraining();

	std::vector<u8> FilterPlayerInput(int player, int size, unsigned char *bits);
	std::vector<u8> SwapPlayerInputs(int size, unsigned char *bits);

};

extern Training training;