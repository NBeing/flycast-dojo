#pragma once

#include "emulator.h"
#include "imgui/imgui.h"
#include "rend/gui.h"
#include "rend/gui_util.h"

#ifndef __ANDROID__
#include "sdl/sdl.h"
#endif

#include "dojo.h"
#include "udp_client.h"
#include "net_beacon.h"

class DojoGui
{
public:
	void gui_display_ggpo_connect();
	void gui_display_disconnected();
	void show_player_name_overlay(bool paused);

	void gui_display_match_code_host_wait();
	void gui_display_match_code_guest_wait();

	void settings_dojo_tab();

	void set_imgui_style();

	void gui_display_replay_end();
private:
	// GGPO Connect Screen
	int current_delay = 0;
	std::string selected_beacon = "";
	std::string detect_address = "";
	std::string own_ip = "";
	int hosting_opt = 1;
	bool local_tab = true;
	UdpClient client;

	bool matched = false;
};

extern DojoGui dojo_gui;
