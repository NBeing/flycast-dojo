#pragma once

#include "emulator.h"
#include "imgui/imgui.h"
#include "rend/gui.h"

#ifndef __ANDROID__
#include "sdl/sdl.h"
#endif

#include "dojo.h"
#include "net_beacon.h"

class DojoGui
{
public:
	void gui_display_ggpo_connect();
	void gui_display_disconnected();
	void show_player_name_overlay(bool paused);

private:
	// GGPO Connect Screen
	int current_delay = 0;
	std::string selected_beacon = "";
	std::string detect_address = "";
	int hosting = 1;
	NetBeacon presence;
};

extern DojoGui dojo_gui;
