#pragma once

#include "emulator.h"
#include "imgui/imgui.h"
#include "rend/gui.h"
#include "rend/gui_util.h"

#include "hw/maple/maple_devs.h"
#include "hw/naomi/naomi_cart.h"

#ifndef __ANDROID__
#include "sdl/sdl.h"
#endif

#include "dojo.h"
#include "match_client.h"
#include "net_beacon.h"

class DojoGui
{
public:
	void copy_btn(const char *si, std::string name);
	void paste_btn(char *si, float width, std::string name);
	float paste_btn_width();

	void netplay_body_head(bool local, bool presence = false);
	void netplay_ip_entry_body();
	void netplay_match_code_body();
	void netplay_relay_body();
	void netplay_lan_body();
	void gui_display_netplay_connect();

	void gui_display_disconnected();
	void show_player_name_overlay(bool paused);

	void gui_display_match_code_host_wait();
	void gui_display_match_code_guest_wait();

	void settings_dojo_tab();

	void set_imgui_style();

	void gui_display_replay_end();
	void show_replay_position_overlay(int frame_num);

	void show_last_inputs_overlay();
	void display_btn(std::string btn_str, bool *any_found);
	void display_input_str(std::string input_str, std::string prev_str = "");

	void show_button_check();
	void gui_display_test_game();
	void gui_display_select_platform();

	void gui_display_replays();

	bool test_game_screen = false;

	int current_map_button = 0;
	bool mapping_shown = false;
	bool pending_map = false;
	bool quick_map_settings_call = false;

	bool gui_start = false;

private:
	// GGPO Connect Screen
	int current_delay = 0;
	std::string selected_beacon = "";
	std::string detect_address = "";
	std::string own_ip = "";
	int hosting_opt = 1;
	bool local_tab = true;
	MatchClient client;

	bool matched = false;

	float netplay_popup_width = 430;
	float local_spacer = 40;
};

extern DojoGui dojo_gui;
