#include "dojo.h"

void Training::SwitchPlayer()
{
	control_player == 0 ? control_player = 1 : control_player = 0;

	if (control_player != 0)
		player_switched = true;
	else
		player_switched = false;

	std::ostringstream NoticeStream;
	NoticeStream << "Controlling Player " << control_player + 1;
	gui_display_notification(NoticeStream.str().data(), 2000);
}

void Training::ToggleRecording(int slot)
{
	std::ostringstream NoticeStream;
	if (recording)
	{
		recording = false;
		recording_started = false;
		NoticeStream << "Stop Recording Slot " << slot + 1 << " Player " << control_player + 1;
	}
	else
	{
		current_record_slot = slot;
		record_slot[slot].clear();
		recorded_slots.insert(slot);
		recording = true;
		if (config::RecordOnFirstInput)
			recording_started = false;
		else
			recording_started = true;
		NoticeStream << "Recording Slot " << slot + 1 << " Player " << control_player + 1;
	}
	gui_display_notification(NoticeStream.str().data(), 2000);
}

void Training::TogglePlayback(int slot)
{
	TogglePlayback(slot, false);
}

void Training::TogglePlayback(int slot, bool hide_slot = false)
{
	std::ostringstream NoticeStream;
	if (playback_loop)
	{
		if (trigger_playback)
		{
			rnd_playback_loop = false;
			trigger_playback = false;
			if (hide_slot)
				NoticeStream << "Stop Loop";
			else
				NoticeStream << "Stop Loop Slot " << slot + 1;
		}
		else
		{
			current_record_slot = slot;
			trigger_playback = true;
			if (hide_slot)
				NoticeStream << "Play Loop";
			else
				NoticeStream << "Play Loop Slot " << slot + 1;
		}
	}
	else
	{
		current_record_slot = slot;
		if (hide_slot)
			NoticeStream << "Play Input";
		else
			NoticeStream << "Play Slot " << slot + 1;
		PlayRecording(slot);
	}
	gui_display_notification(NoticeStream.str().data(), 2000);
}

void Training::ToggleRandomPlayback()
{
	if (recorded_slots.empty())
	{
		gui_display_notification("No Input Slots Recorded", 2000);
		return;
	}

	srand(time(0));
	if (rnd_playback_loop)
	{
		rnd_playback_loop = false;
	}
	else
	{
		if (playback_loop)
			rnd_playback_loop = true;
	}

	if (!playing_input && !playback_loop)
	{
		auto it = recorded_slots.cbegin();
		int rnd = rand() % recorded_slots.size();
		std::advance(it, rnd);
		current_record_slot = *it;
	}

	TogglePlayback(current_record_slot, config::HideRandomInputSlot.get());
}

void Training::SelectRecordSlot()
{
	selected_record_slot = (selected_record_slot + 1) % 3;

	std::ostringstream NoticeStream;
	NoticeStream << "Selected Input Slot " << selected_record_slot + 1;
	gui_display_notification(NoticeStream.str().data(), 2000);
}

void Training::ToggleSelectedRecording()
{
	ToggleRecording(selected_record_slot);
}

void Training::ToggleSelectedPlayback()
{
	TogglePlayback(selected_record_slot, false);
}

void Training::PlayRecording(int slot)
{
	if (!recording && !playing_input)
	{
		playing_input = true;
		u8 to_add[MAPLE_FRAME_SIZE] = {0};
		u32 target_frame = dojo.frame_number + 1 + config::Delay;
		for (std::string frame : record_slot[slot])
		{
			// to_add[0] = (u8)port;
			std::vector<u8> frame_record(MAPLE_FRAME_SIZE, 0);
			memcpy(frame_record.data(), frame.data(), MAPLE_FRAME_SIZE);
			dojo.RecRecordAction(target_frame, frame_record.size(), frame_record.data());
			target_frame++;
		}
		next_playback_frame = target_frame;
		playing_input = false;
	}
}

void Training::Reset()
{
	playback_loop = false;
	playing_input = false;
	trigger_playback = false;
	next_playback_frame = 0;
	control_player = 0;

	player_switched = false;
	current_record_slot = 0;

	recording = false;
	recording_started = false;

	for (int i = 0; i < 3; i++)
	{
		record_slot[i].clear();
	}

	recorded_slots.clear();
}

std::vector<u8> Training::FilterPlayerInput(int player, int size, unsigned char *bits)
{
	int player_input_size = size / MAX_PLAYERS;
	std::vector<u8> out_frame(size, 0);

	int start = player * player_input_size;
	memcpy(out_frame.data() + start, bits + start, player_input_size);

	return out_frame;
}

std::vector<u8> Training::SwapPlayerInputs(int size, unsigned char *bits)
{
	int player_input_size = size / MAX_PLAYERS;
	std::vector<u8> out_frame(size, 0);

	memcpy(out_frame.data() + player_input_size, bits, player_input_size);
	memcpy(out_frame.data(), bits + player_input_size, player_input_size);

	return out_frame;
}
