#pragma once

#include "emulator.h"
#include "imgui/imgui.h"
#include "rend/gui.h"
#include "rend/gui_util.h"
#include "rend/imgui_driver.h"

#include "hw/maple/maple_devs.h"
#include "hw/naomi/naomi_cart.h"

#ifndef __ANDROID__
#include "sdl/sdl.h"
#endif

#include "dojo.h"
#include "net_beacon.h"

class DojoGui
{
public:
	void copy_btn(const char *si, std::string name);
	void paste_btn(char *si, float width, std::string name);
	float paste_btn_width();

	void netplay_body_head(bool local, bool presence = false);
	void netplay_ip_entry_body();
	void netplay_relay_body();
	void netplay_lan_body();
	void gui_display_netplay_connect();
	void gui_display_quick_match();

	void gui_display_delay_select();
	void gui_display_quick_match_guest_wait();

	void gui_display_disconnected();
	void show_player_name_overlay(bool paused);

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

	void invoke_download_save_popup(std::string game_path, bool* net_save_download, bool launch_game);
	void gui_display_savestate_dl();

	bool get_avatar_image(std::string email_sha, ImTextureID& textureId, bool allowLoad);
	bool get_flag_image(std::string country_code, ImTextureID& textureId, bool allowLoad);

	static void AvatarImage(ImTextureID textureId, const std::string& tooltip, ImVec2 size);

	bool net_save_download = false;
	bool test_game_screen = false;

	int current_map_button = 0;
	bool mapping_shown = false;
	bool pending_map = false;

	bool quick_map_settings_call = false;
	bool quick_match_dl_call = false;
	bool delay_select = false;
	bool netplay_session = false;

	bool gui_start = false;

	void show_pause();

	void gui_display_stream_wait();
	bool buffer_captured = false;
private:
	// GGPO Connect Screen
	int current_delay = 0;
	std::string selected_beacon = "";
	std::string detect_address = "";
	std::string own_ip = "";
	int hosting_opt = 1;
	bool local_tab = true;

	bool matched = false;

	float netplay_popup_width = 410;
	float local_spacer = 40;
};

extern DojoGui dojo_gui;
