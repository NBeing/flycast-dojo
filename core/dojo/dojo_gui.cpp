#include "dojo_gui.h"

void DojoGui::gui_display_ggpo_connect()
{
	char netplay_session_txt[128];
	sprintf(netplay_session_txt, "%s Netplay Session - %s ", ICON_FA_BOLT, dojo.game_name.c_str());
	ImGui::OpenPopup(netplay_session_txt);
	if (ImGui::BeginPopupModal(netplay_session_txt, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiInputTextFlags_EnterReturnsTrue))
	{
		static char si[128] = "";
		if (!matched)
		{
			if (ImGui::BeginTabBar("GGPOTabBar", ImGuiTabBarFlags_None))
			{
				if (config::MatchCodeEnable)
				{
					char match_codes_txt[128];
					sprintf(match_codes_txt, " %s Match Codes ", ICON_FA_NETWORK_WIRED);
					if (ImGui::BeginTabItem(match_codes_txt))
					{
						local_tab = false;
						dojo.presence.Close();

						char host_txt[128];
						sprintf(host_txt, "  %s  \nHost", ICON_FA_SATELLITE);
						if (ImGui::Button(host_txt, ScaledVec2(150, 150)))
						{
							cfgSetVirtual("network", "server", "");
							cfgSetVirtual("network", "GGPO", "yes");
							cfgSetVirtual("network", "ActAsServer", "yes");
							dojo.disconnect_toggle = false;
							dojo.hosting = true;
							hosting_opt = true;
							matched = true;
							try
							{
								std::thread t2(&UdpClient::ClientThread, std::ref(client));
								t2.detach();
							}
							catch (std::exception &)
							{
							}
							gui_setState(GuiState::MatchCodeHostWait);
						}
						ImGui::SameLine();
						char join_txt[128];
						sprintf(join_txt, "  %s  \nJoin", ICON_FA_SATELLITE_DISH);
						if (ImGui::Button(join_txt, ScaledVec2(150, 150)))
						{
							cfgSetVirtual("network", "server", "");
							cfgSetVirtual("network", "GGPO", "yes");
							cfgSetVirtual("network", "ActAsServer", "no");
							dojo.disconnect_toggle = false;
							dojo.hosting = false;
							hosting_opt = false;
							matched = true;
							try
							{
								std::thread t2(&UdpClient::ClientThread, std::ref(client));
								t2.detach();
							}
							catch (std::exception &)
							{
							}
							gui_setState(GuiState::MatchCodeGuestWait);
						}
						ImGui::EndTabItem();
					}
				}
				char ip_entry_txt[128];
				sprintf(ip_entry_txt, " %s IP Entry ", ICON_FA_ETHERNET);	
				if (ImGui::BeginTabItem(ip_entry_txt))
				{
					local_tab = true;
					dojo.presence.Close();

					ImGui::InputTextWithHint(" IP", "0.0.0.0", si, IM_ARRAYSIZE(si));
					detect_address = std::string(si);
#ifndef __ANDROID__
					ImGui::SameLine();
					char paste_btn_txt[128];
					sprintf(paste_btn_txt, "%s", ICON_FA_CLIPBOARD);
					if (ImGui::Button(paste_btn_txt))
					{
						char *pasted_txt = SDL_GetClipboardText();
						memcpy(si, pasted_txt, strlen(pasted_txt));
					}
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("Paste");
#endif
					ImGui::EndTabItem();
				}

				if (config::NetBeaconEnable)
				{
					char local_net_txt[128];
					sprintf(local_net_txt, " %s LAN ", ICON_FA_NETWORK_WIRED);
					if (ImGui::BeginTabItem(local_net_txt))
					{
						local_tab = true;
						if (!dojo.presence.beacon_active)
						{
							dojo.presence.beacon_active = true;
							std::thread t3(&NetBeacon::BeaconThread, std::ref(dojo.presence));
							t3.detach();
						}

						if (!dojo.presence.lobby_active)
						{
							dojo.presence.lobby_active = true;
							std::thread t4(&NetBeacon::ListenerThread, std::ref(dojo.presence));
							t4.detach();
						}

						if (ImGui::BeginChild("Beacons", ImVec2(0, 100.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_DragScrolling | ImGuiWindowFlags_NavFlattened))
						{
							for (auto it = dojo.presence.active_beacons.begin(); it != dojo.presence.active_beacons.end(); ++it)
							{
								std::string beacon_msg = it->first;
								if (beacon_msg == config::PlayerName.get() + "_" + dojo.presence.client_seed)
									own_ip = it->second;

								std::string player_name = beacon_msg.substr(0, beacon_msg.find('_'));
								if (dojo.presence.last_seen[it->first.data()] + 5000 > dojo.presence.unix_timestamp() &&
									(beacon_msg != config::PlayerName.get() + "_" + dojo.presence.client_seed))
									if (ImGui::Selectable(player_name.data(), selected_beacon == beacon_msg.data()))
									{
										selected_beacon = beacon_msg;
										detect_address = it->second;
									}
							}
							ImGui::EndChild();
						}

						ImGui::EndTabItem();
					}
				}
			}
		}
		else
		{
			detect_address = config::NetworkServer.get();
			local_tab = true;
		}


		char start_btn_txt[128];
		char cancel_btn_txt[128];
		
		sprintf(start_btn_txt, "%s Start", ICON_FA_CIRCLE_PLAY);
		sprintf(cancel_btn_txt, "%s Cancel", ICON_FA_CIRCLE_XMARK);

		if (local_tab)
		{
			ImGui::SliderInt("", (int *)&current_delay, 0, 20);
			ImGui::SameLine();
			ImGui::Text("Delay");

			if (!matched)
			{
				ImGui::Columns(2, "hosting", false);
				ImGui::RadioButton("Host", &hosting_opt, 1);
				ImGui::NextColumn();
				ImGui::RadioButton("Join", &hosting_opt, 0);
				ImGui::Columns(1, NULL, false);
			}

			float font_size = ImGui::GetFontSize() * (strlen(start_btn_txt) + strlen(cancel_btn_txt)) / 2;
			ImGui::Text(" ");
			ImGui::SameLine(ImGui::GetWindowSize().x / 2 - font_size + (font_size / 2));

			if (ImGui::Button(start_btn_txt))
			{
				if (detect_address == own_ip)
					detect_address = "127.0.0.1";

				if (hosting_opt)
					cfgSetVirtual("network", "ActAsServer", "yes");
				else
					cfgSetVirtual("network", "ActAsServer", "no");

				cfgSetVirtual("network", "GGPO", "yes");
				cfgSetVirtual("network", "Enable", "no");
				cfgSetVirtual("network", "server", detect_address);

				NOTICE_LOG(NETWORK, "CONNECT %s", detect_address.data());
				if (current_delay != config::GGPODelay.get())
					cfgSetVirtual("network", "GGPODelay", std::to_string(current_delay));

				ImGui::CloseCurrentPopup();
				gui_setState(GuiState::Closed);

				gui_start_game(settings.content.path);
			}

			ImGui::SameLine();
		}
		
		if (!local_tab)
		{
			float font_size = ImGui::GetFontSize() * (strlen(cancel_btn_txt)) / 2;
			ImGui::Text(" ");
			ImGui::SameLine(ImGui::GetWindowSize().x / 2 - font_size + (font_size / 2));
		}
		if (ImGui::Button(cancel_btn_txt))
		{
			dojo.presence.Close();
			cfgSetVirtual("network", "GGPO", "no");

			settings.content.path = "";
			ImGui::CloseCurrentPopup();
			gui_setState(GuiState::Main);
		}

		ImGui::EndPopup();
	}
}

void DojoGui::gui_display_disconnected()
{
	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(330 * settings.display.uiScale, 0));

	ImGui::Begin("##disconnected", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Disconnected.");

	if (ImGui::Button("Exit Game"))
		dc_exit();

	ImGui::End();

	error_popup();
}

void DojoGui::gui_display_replay_end()
{
	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(130 * settings.display.uiScale, 0));

	ImGui::Begin("##replay_end", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("End of Replay.");

	char exit_txt[64];
	if (dojo.commandLineStart)
		sprintf(exit_txt, "%s  Exit", ICON_FA_DOOR_OPEN);
	else
		sprintf(exit_txt, "%s  Close Game", ICON_FA_DOOR_OPEN);

	if (ImGui::Button(exit_txt))
	{
		if (!dojo.commandLineStart)
			gui_stop_game();
		else
			dc_exit();
	}

	ImGui::End();

	error_popup();
}

void DojoGui::show_player_name_overlay(bool paused)
{
	// if both player names are defaults, hide overlay
	if (dojo.player_2.length() <= 1 ||
		(strcmp(dojo.player_1.data(), "Player") == 0 &&
		 strcmp(dojo.player_1.data(), dojo.player_2.data()) == 0))
	{
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.557f, 0.268f, 0.965f, 1.f));

	if (dojo.player_1.length() > 1)
	{
		float font_size = ImGui::CalcTextSize(dojo.player_1.data()).x + 10;

		ImGui::SetNextWindowPos(ImVec2((settings.display.width / 4) - ((font_size + 25) / 2), 0));
#if defined(__APPLE__) || defined(__ANDROID__)
		ImGui::SetNextWindowSize(ImVec2(font_size + 30, 42));
#else
		ImGui::SetNextWindowSize(ImVec2(font_size + 30, 35));
#endif
		ImGui::SetNextWindowBgAlpha(0.5f);
		ImGui::Begin("#one", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);

		ImGui::SameLine(
			(ImGui::GetContentRegionAvail().x / 2) -
			font_size + (font_size / 2) + 5);

		ImGui::TextUnformatted(dojo.player_1.c_str());
		if (dojo.ScoreAvailable())
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(std::to_string(dojo.p1_wins).c_str());
		}

		ImGui::End();
	}

	if (dojo.player_2.length() > 1)
	{
		float font_size = ImGui::CalcTextSize(dojo.player_2.data()).x + 10;

		ImGui::SetNextWindowPos(ImVec2(((settings.display.width / 4) * 3) - ((font_size + 25) / 2), 0));
#if defined(__APPLE__) || defined(__ANDROID__)
		ImGui::SetNextWindowSize(ImVec2(font_size + 30, 42));
#else
		ImGui::SetNextWindowSize(ImVec2(font_size + 30, 35));
#endif
		ImGui::SetNextWindowBgAlpha(0.5f);
		ImGui::Begin("#two", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);

		ImGui::SameLine(
			(ImGui::GetContentRegionAvail().x / 2) -
			font_size + (font_size / 2) + 5);

		ImGui::TextUnformatted(dojo.player_2.c_str());
		if (dojo.ScoreAvailable())
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(std::to_string(dojo.p2_wins).c_str());
		}

		ImGui::End();
	}

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void DojoGui::gui_display_match_code_host_wait()
{
	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(330 * settings.display.uiScale, 0));

	ImGui::Begin("##host_wait", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Waiting for opponent to connect...");

	if (!dojo.match_code.empty())
	{
		ImGui::Text("Match Code: %s", dojo.match_code.data());
		ImGui::SameLine();
		ShowHelpMarker("Match Codes not working?\nTry switching to Direct IP in the settings.");
#ifndef __ANDROID__
		char copy_btn_txt[128];
		sprintf(copy_btn_txt, "%s Copy Match Code", ICON_FA_COPY);
		if (ImGui::Button(copy_btn_txt))
		{
			SDL_SetClipboardText(dojo.match_code.data());
		}
		ImGui::SameLine();
#endif
	}
	char cancel_btn_txt[128];
	sprintf(cancel_btn_txt, "%s Cancel", ICON_FA_CIRCLE_XMARK);
	if (ImGui::Button(cancel_btn_txt))
	{
		dojo.disconnect_toggle = true;
		dojo.match_code = "";
		matched = false;
		hosting_opt = true;
		dojo.hosting = false;
		config::GGPOEnable.set(false);
		gui_setState(GuiState::GGPOConnect);
	}

	if (!config::NetworkServer.get().empty())
	{
		gui_setState(GuiState::GGPOConnect);
	}

	ImGui::End();
}

void DojoGui::gui_display_match_code_guest_wait()
{
	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(330 * settings.display.uiScale, 0));

	ImGui::Begin("##guest_wait", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	if (config::NetworkServer.get().empty())
	{
		ImGui::OpenPopup("Match Code");
		if (ImGui::BeginPopupModal("Match Code", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiInputTextFlags_EnterReturnsTrue))
		{
			ImGui::Text("Enter Match Code generated by host.");

			static char mc[128] = "";
			ImGui::InputTextWithHint("", "ABC123", mc, IM_ARRAYSIZE(mc), ImGuiInputTextFlags_CharsUppercase);

#ifndef __ANDROID__
			ImGui::SameLine();
			char paste_btn_txt[128];
			sprintf(paste_btn_txt, "%s", ICON_FA_CLIPBOARD);
			if (ImGui::Button(paste_btn_txt))
			{
				char *pasted_txt = SDL_GetClipboardText();
				memcpy(mc, pasted_txt, strlen(pasted_txt));
			}
#endif
			char start_btn_txt[128];
			sprintf(start_btn_txt, "%s Start", ICON_FA_CIRCLE_PLAY);
			if (ImGui::Button(start_btn_txt))
			{
				dojo.match_code = std::string(mc, strlen(mc));
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			char cancel_btn_txt[128];
			sprintf(cancel_btn_txt, "%s Cancel", ICON_FA_CIRCLE_XMARK);
			if (ImGui::Button(cancel_btn_txt))
			{
				dojo.disconnect_toggle = true;
				dojo.match_code = "";
				matched = false;
				hosting_opt = true;
				dojo.hosting = false;
				config::GGPOEnable.set(false);
				gui_setState(GuiState::GGPOConnect);
			}

			ImGui::EndPopup();
		}
	}

	if (!config::NetworkServer.get().empty())
		gui_setState(GuiState::GGPOConnect);

	ImGui::End();
}

void DojoGui::settings_dojo_tab()
{
	if (ImGui::BeginTabItem("Dojo"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding);

		char PlayerName[256] = {0};
		strcpy(PlayerName, config::PlayerName.get().c_str());
		ImGui::InputText("Player Name", PlayerName, sizeof(PlayerName), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
		ImGui::SameLine();
		ShowHelpMarker("Name visible to other players");
		config::PlayerName = std::string(PlayerName, strlen(PlayerName));

		OptionCheckbox("Enable Player Name Overlay", config::PlayerNameOverlay,
					   "Enable overlay showing player names during netplay sessions & replays");

		OptionCheckbox("Output Session Details to Text Files", config::StreamTxtOutput,
					   "Outputs in-game overlay details to external text files (in the 'out' folder). Useful for online streams.");

		if (ImGui::CollapsingHeader("GGPO", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Left Thumbstick:");
			OptionRadioButton<int>("Disabled", config::GGPOAnalogAxes, 0, "Left thumbstick not used");
			ImGui::SameLine();
			OptionRadioButton<int>("Horizontal", config::GGPOAnalogAxes, 1, "Use the left thumbstick horizontal axis only");
			ImGui::SameLine();
			OptionRadioButton<int>("Full", config::GGPOAnalogAxes, 2, "Use the left thumbstick horizontal and vertical axes");

			OptionCheckbox("Enable Chat", config::GGPOChat, "Open the chat window when a chat message is received");
			if (config::GGPOChat)
			{
				OptionCheckbox("Enable Chat Window Timeout", config::GGPOChatTimeoutToggle, "Automatically close chat window after 20 seconds");
				if (config::GGPOChatTimeoutToggle)
				{
					char chatTimeout[256];
					sprintf(chatTimeout, "%d", (int)config::GGPOChatTimeout);
					ImGui::InputText("Chat Window Timeout (seconds)", chatTimeout, sizeof(chatTimeout), ImGuiInputTextFlags_CharsDecimal, nullptr, nullptr);
					ImGui::SameLine();
					ShowHelpMarker("Sets duration that chat window stays open after new message is received.");
					config::GGPOChatTimeout.set(atoi(chatTimeout));
				}
			}
			OptionCheckbox("Network Statistics", config::NetworkStats,
						   "Display network statistics on screen");

			int GGPOPort = config::GGPOPort.get();
			ImGui::InputInt("GGPO Local Port", &GGPOPort);
			ImGui::SameLine();
			ShowHelpMarker("The GGPO port to listen on");
			if (GGPOPort != config::GGPOPort.get())
				config::GGPOPort = GGPOPort;

			std::string PortTitle;
			std::string PortDescription;

			PortTitle = "Handshake Port";
			PortDescription = "The handshake port to listen on";

			char ServerPort[256];
			strcpy(ServerPort, config::DojoServerPort.get().c_str());

			ImGui::InputText(PortTitle.c_str(), ServerPort, sizeof(ServerPort), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
			ImGui::SameLine();
			ShowHelpMarker(PortDescription.c_str());
			config::DojoServerPort = ServerPort;
		}

		if (ImGui::CollapsingHeader("Training", ImGuiTreeNodeFlags_None))
		{
			OptionCheckbox("Automatically Load Netplay Savestate", config::AutoLoadNetState);
			ImGui::SameLine();
			ShowHelpMarker("When available, loads netplay savestate on launch. Typically character or mode select screen");

			OptionCheckbox("Show Input Display", config::ShowTrainingInputDisplay);
			ImGui::SameLine();
			ShowHelpMarker("Shows controller input history in Training Mode\n(Temporarily disabled for Offline Delay > 0)");

			if (config::ShowTrainingInputDisplay)
			{
				OptionCheckbox("Use Numpad Notation", config::UseAnimeInputNotation);
				ImGui::SameLine();
				ShowHelpMarker("Show inputs using Numpad/Anime Notation for Directions");
			}

			OptionCheckbox("Hide Random Input Slot", config::HideRandomInputSlot);
			ImGui::SameLine();
			ShowHelpMarker("Hides input slot is being played for random playback");

			OptionCheckbox("Start Recording on First Input", config::RecordOnFirstInput);
			ImGui::SameLine();
			ShowHelpMarker("Delay dummy recording until the first input is registered");
		}

		if (ImGui::CollapsingHeader("Match Codes##MCHeader", ImGuiTreeNodeFlags_None))
		{
			OptionCheckbox("Enable Match Codes", config::MatchCodeEnable,
						   "Establishes direct connection via public matchmaking relay.\nWorks with most home routers. Activates 'Match Codes' tab in GGPO Connection screen.");

			if (config::MatchCodeEnable)
			{
				char MatchmakingServerAddress[256];

				strcpy(MatchmakingServerAddress, config::MatchmakingServerAddress.get().c_str());
				ImGui::InputText("Matchmaking Service Address", MatchmakingServerAddress, sizeof(MatchmakingServerAddress), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				config::MatchmakingServerAddress = MatchmakingServerAddress;

				char MatchmakingServerPort[256];

				strcpy(MatchmakingServerPort, config::MatchmakingServerPort.get().c_str());
				ImGui::InputText("Matchmaking Service Port", MatchmakingServerPort, sizeof(MatchmakingServerPort), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				config::MatchmakingServerPort = MatchmakingServerPort;
			}
		}

		if (ImGui::CollapsingHeader("Local Network Lobby", ImGuiTreeNodeFlags_None))
		{
			OptionCheckbox("Enable Local Network Lobby", config::NetBeaconEnable,
						   "Broadcasts and listens for peers on local network. Activates 'Local Networks' tab in GGPO Connection screen.");

			if (config::NetBeaconEnable)
			{
				char BeaconMulticastAddress[256];

				strcpy(BeaconMulticastAddress, config::BeaconMulticastAddress.get().c_str());
				ImGui::InputText("Multicast Address", BeaconMulticastAddress, sizeof(BeaconMulticastAddress), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				config::BeaconMulticastAddress = BeaconMulticastAddress;

				char BeaconMulticastPort[256];

				strcpy(BeaconMulticastPort, config::BeaconMulticastPort.get().c_str());
				ImGui::InputText("Multicast Port", BeaconMulticastPort, sizeof(BeaconMulticastPort), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				config::BeaconMulticastPort = BeaconMulticastPort;
			}
		}

		ImGui::PopStyleVar();
		ImGui::EndTabItem();
	}
}

void DojoGui::set_imgui_style()
{
	// Sonic Riders style by Sewer56 from ImThemes
	// https://github.com/Patitotective/ImThemes

	ImGuiStyle &style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	style.DisabledAlpha = 0.6000000238418579f;
	style.WindowPadding = ImVec2(8.0f, 8.0f);
	style.WindowRounding = 0.0f;
	style.WindowBorderSize = 0.0f;
	style.WindowMinSize = ImVec2(32.0f, 32.0f);
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_Left;
	style.ChildRounding = 0.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupRounding = 0.0f;
	style.PopupBorderSize = 0.0f;
	style.FramePadding = ImVec2(4.0f, 3.0f);
	style.FrameRounding = 4.0f;
	style.FrameBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(8.0f, 4.0f);
	style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
	style.CellPadding = ImVec2(4.0f, 2.0f);
	style.IndentSpacing = 21.0f;
	style.ColumnsMinSpacing = 6.0f;
	style.ScrollbarSize = 14.0f;
	style.ScrollbarRounding = 9.0f;
	style.GrabMinSize = 10.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.TabBorderSize = 0.0f;
	style.TabMinWidthForCloseButton = 0.0f;
	style.ColorButtonPosition = ImGuiDir_Right;
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

	style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.729411780834198f, 0.7490196228027344f, 0.7372549176216125f, 1.0f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08627451211214066f, 0.08627451211214066f, 0.08627451211214066f, 0.9399999976158142f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.5f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.4000000059604645f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6700000166893005f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.4666666686534882f, 0.2196078449487686f, 0.2196078449487686f, 0.6700000166893005f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.4666666686534882f, 0.2196078449487686f, 0.2196078449487686f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.4666666686534882f, 0.2196078449487686f, 0.2196078449487686f, 0.6700000166893005f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.3372549116611481f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 1.0f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.4666666686534882f, 0.2196078449487686f, 0.2196078449487686f, 0.6499999761581421f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.6499999761581421f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.5f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6499999761581421f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.0f);
	style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6600000262260437f);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6600000262260437f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.7098039388656616f, 0.3882353007793427f, 0.3882353007793427f, 0.5400000214576721f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6600000262260437f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(0.8392156958580017f, 0.658823549747467f, 0.658823549747467f, 0.6600000262260437f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.09803921729326248f, 0.1490196138620377f, 0.9700000286102295f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1372549086809158f, 0.2588235437870026f, 0.4196078479290009f, 1.0f);
	style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
	style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
	style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
	style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
}

void DojoGui::show_replay_position_overlay(int frame_num)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.557f, 0.268f, 0.965f, 1.f));

	if (dojo.frame_number < dojo.session_inputs.size() ||
		cfgLoadBool("dojo", "Training", false))
	{
		char text_pos[30] = {0};

		if (dojo.play_match)
			sprintf(text_pos, "%u / %u     ", frame_num, dojo.session_inputs.size());
		else if (cfgLoadBool("dojo", "Training", false))
			sprintf(text_pos, "%u     ", frame_num);

		float font_size = ImGui::CalcTextSize(text_pos).x;

		ImGui::SetNextWindowPos(ImVec2(settings.display.width - font_size, settings.display.height - 40));
		ImGui::SetNextWindowSize(ImVec2(font_size, 40));
		ImGui::SetNextWindowBgAlpha(0.5f);
		ImGui::Begin("#pos", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);

		if (dojo.play_match)
			ImGui::Text("%u / %u", frame_num, dojo.session_inputs.size());
		else if (cfgLoadBool("dojo", "Training", false))
			ImGui::Text("%u", frame_num);

		ImGui::End();
	}

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void DojoGui::display_btn(std::string btn_str, bool *any_found)
{
	std::vector<std::string> btns = {"1", "2", "3", "4", "5", "6",
									 "X", "Y", "LT", "A", "B", "RT",
									 "C", "Z", "D", "Start"};

	if (std::any_of(btns.begin(), btns.end(), [btn_str](std::string str)
					{ return btn_str == str; }))
	{
		ImGui::SameLine();
		*any_found = true;
	}

	if (btn_str == "1")
		ImGui::TextColored(ImVec4(255, 0, 0, 1), "%s", ICON_KI_BUTTON_ONE);
	else if (btn_str == "2")
		ImGui::TextColored(ImVec4(0, 175, 255, 1), "%s", ICON_KI_BUTTON_TWO);
	else if (btn_str == "3")
		ImGui::TextColored(ImVec4(255, 255, 255, 1), "%s", ICON_KI_BUTTON_THREE);
	else if (btn_str == "4")
		ImGui::TextColored(ImVec4(255, 255, 0, 1), "%s    ", ICON_KI_BUTTON_FOUR);
	else if (btn_str == "5")
		ImGui::TextColored(ImVec4(0, 175, 0, 1), "%s    ", ICON_KI_BUTTON_FIVE);
	else if (btn_str == "6")
		ImGui::TextColored(ImVec4(255, 0, 175, 1), "%s    ", ICON_KI_BUTTON_SIX);
	else if (btn_str == "X")
		ImGui::TextColored(ImVec4(255, 255, 0, 1), "%s", ICON_KI_BUTTON_X);
	else if (btn_str == "Y")
		ImGui::TextColored(ImVec4(0, 255, 0, 1), "%s", ICON_KI_BUTTON_Y);
	else if (btn_str == "LT")
		ImGui::TextColored(ImVec4(255, 255, 255, 1), "%s", ICON_KI_BUTTON_L);
	else if (btn_str == "A")
		ImGui::TextColored(ImVec4(255, 0, 0, 1), "%s", ICON_KI_BUTTON_A);
	else if (btn_str == "B")
		ImGui::TextColored(ImVec4(0, 175, 255, 1), "%s", ICON_KI_BUTTON_B);
	else if (btn_str == "RT")
		ImGui::TextColored(ImVec4(255, 255, 255, 1), "%s", ICON_KI_BUTTON_R);
	else if (btn_str == "C")
		ImGui::TextColored(ImVec4(255, 255, 255, 1), "%s    ", ICON_KI_BUTTON_C);
	else if (btn_str == "Z")
		ImGui::TextColored(ImVec4(255, 75, 255, 1), "%s", ICON_KI_BUTTON_Z);
	else if (btn_str == "D")
		ImGui::TextColored(ImVec4(0, 0, 255, 1), "%s", ICON_KI_BUTTON_D);
	else if (btn_str == "Start")
		ImGui::Text("%s", ICON_KI_BUTTON_START);
}

void DojoGui::display_input_str(std::string input_str, std::string prev_str)
{
	bool any_found = false;
	std::vector<std::string> arcade_btns = {"1", "2", "3", "4", "5", "6", "C", "Z", "D", "Start"};
	std::vector<std::string> dc_btns = {"X", "Y", "LT", "A", "B", "RT", "C", "Start"};

	std::vector<std::string> buttons = dc_btns;
	if (settings.platform.isArcade())
		buttons = arcade_btns;

	std::string new_btns = "";
	if (prev_str.length() > 0)
	{
		for (int i = 0; i < buttons.size(); i++)
		{
			if (prev_str.find(buttons[i]) != std::string::npos &&
				input_str.find(buttons[i]) != std::string::npos)
				display_btn(buttons[i], &any_found);
			else if (prev_str.find(buttons[i]) == std::string::npos &&
					 input_str.find(buttons[i]) != std::string::npos)
				new_btns.append(buttons[i]);
		}
	}
	else
	{
		new_btns = input_str;
	}

	for (int i = 0; i < buttons.size(); i++)
	{
		if (new_btns.find(buttons[i]) != std::string::npos)
			display_btn(buttons[i], &any_found);
	}

	if (!any_found)
		ImGui::Text("");
}

void DojoGui::show_last_inputs_overlay()
{
	if (config::Training && config::Delay > 0)
		return;

	for (int di = 0; di < 2; di++)
	{
		if (!dojo.displayed_inputs[di].empty())
		{

			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.557f, 0.268f, 0.965f, 1.f));

#if defined(__APPLE__) || defined(__ANDROID__)
			ImGui::SetNextWindowSize(ImVec2(290, ImGui::GetIO().DisplaySize.y - 230));
#else
			ImGui::SetNextWindowSize(ImVec2(210, ImGui::GetIO().DisplaySize.y - 150));
#endif

			if (di == 0)
			{
#if defined(__APPLE__) || defined(__ANDROID__)
				ImGui::SetNextWindowPos(ImVec2(10, 180));
#else
				ImGui::SetNextWindowPos(ImVec2(10, 100));
#endif
				ImGui::SetNextWindowBgAlpha(0.4f);
				ImGui::Begin("#one_input", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
			}
			else if (di == 1)
			{
#if defined(__APPLE__) || defined(__ANDROID__)
				ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 300, 180));
#else
				ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 220, 100));
#endif
				ImGui::SetNextWindowBgAlpha(0.4f);
				ImGui::Begin("#two_input", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
			}

			if (dojo.displayed_inputs[di].size() > 60)
			{
				dojo.displayed_inputs[di].erase(dojo.displayed_inputs[di].begin());
				dojo.displayed_inputs_str[di].erase(dojo.displayed_inputs_str[di].begin());
				dojo.displayed_dirs_str[di].erase(dojo.displayed_dirs_str[di].begin());
				dojo.displayed_dirs[di].erase(dojo.displayed_dirs[di].begin());
				dojo.displayed_inputs_duration[di].erase(dojo.displayed_inputs_duration[di].begin());
				dojo.displayed_num_dirs[di].erase(dojo.displayed_num_dirs[di].begin());
			}

			std::map<u32, std::bitset<18>>::reverse_iterator it = dojo.displayed_inputs[di].rbegin();

			u32 input_frame_num = it->first;
			u32 input_duration = dojo.frame_number - input_frame_num;

			dojo.displayed_inputs_duration[di][input_frame_num] = input_duration;
			if (dojo.displayed_inputs_str[di].size() > 1)
			{
				it++;
				dojo.last_displayed_inputs_str[di] = dojo.displayed_inputs_str[di][it->first];
			}

			for (auto rit = dojo.displayed_inputs[di].rbegin(); rit != dojo.displayed_inputs[di].rend(); ++rit)
			{
				ImGui::Text("%03u", dojo.displayed_inputs_duration[di][rit->first]);
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.594f, 0.806f, 0.912f, 1.f));
				if (config::UseAnimeInputNotation)
				{
					auto num = dojo.displayed_num_dirs[di][rit->first];
					ImGui::Text("%d", num);
				}
				else
				{
					auto num = dojo.displayed_num_dirs[di][rit->first];
					if (num == 1)
						ImGui::Text("%s", ICON_KI_ARROW_BOTTOM_LEFT);
					else if (num == 2)
						ImGui::Text("%s", ICON_KI_ARROW_BOTTOM);
					else if (num == 3)
						ImGui::Text("%s", ICON_KI_ARROW_BOTTOM_RIGHT);
					else if (num == 4)
						ImGui::Text("%s", ICON_KI_ARROW_LEFT);
					else if (num == 6)
						ImGui::Text("%s", ICON_KI_ARROW_RIGHT);
					else if (num == 7)
						ImGui::Text("%s", ICON_KI_ARROW_TOP_LEFT);
					else if (num == 8)
						ImGui::Text("%s", ICON_KI_ARROW_TOP);
					else if (num == 9)
						ImGui::Text("%s", ICON_KI_ARROW_TOP_RIGHT);
				}
				ImGui::PopStyleColor();
				ImGui::SameLine();
				display_input_str(dojo.displayed_inputs_str[di][rit->first], dojo.last_displayed_inputs_str[di]);
				dojo.last_displayed_inputs_str[di] = dojo.displayed_inputs_str[di][rit->first];
			}
			ImGui::End();

			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
		}
	}
}

void DojoGui::show_button_check()
{
	if (test_game_screen)
	{
		if (strlen(settings.content.path.data()) > 0)
		{
			std::string extension = get_file_extension(settings.content.path);
			if (extension == "chd" || extension == "gdi" || extension == "cdi")
			{
				settings.platform.system = DC_PLATFORM_DREAMCAST;
			}
			else
			{
				int platform = naomi_cart_GetPlatform(settings.content.path.data());
				settings.platform.system = platform;
			}
		}
		GamepadDevice::load_system_mappings();
	}

	const float scaling = settings.display.uiScale;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.335f, 0.155f, 0.770f, 1.000f));

	std::string pause_text;
	pause_text = "Button Check";

	float font_size = ImGui::CalcTextSize(pause_text.c_str()).x;

	ImGui::SetNextWindowPos(ImVec2((settings.display.width / 2) - ((font_size + 40) / 2), 0));
#if defined(__APPLE__) || defined(__ANDROID__)
	ImGui::SetNextWindowSize(ImVec2(font_size + 40, 60));
#else
	ImGui::SetNextWindowSize(ImVec2(font_size + 40, 40));
#endif
	ImGui::SetNextWindowBgAlpha(0.65f);
	ImGui::Begin("#button_check_title", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);

	ImGui::SameLine(
		(ImGui::GetContentRegionAvail().x / 2) -
		font_size + (font_size / 2) + 10);

	ImGui::TextUnformatted(pause_text.c_str());

	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	// int num_players = (ggpo_join_screen || test_game_screen) ? 1 : 2;
	int num_players = 2;
	for (int i = 0; i < num_players; i++)
	{
		if (num_players == 2)
		{
			if (i == 0)
				ImGui::SetNextWindowPos(ImVec2(settings.display.width / 4.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			else
				ImGui::SetNextWindowPos(ImVec2((settings.display.width / 4.f) * 3, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		}
		else
		{
			ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		}

		float font_height = ImGui::CalcTextSize("Test").y;

		if (settings.platform.isArcade())
			ImGui::SetNextWindowSize(ImVec2(130 * scaling, font_height * 18));
		else
			ImGui::SetNextWindowSize(ImVec2(130 * scaling, 0));

		std::string bc_title = "##button_check" + std::to_string(i);
		ImGui::Begin(bc_title.c_str(), NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

		auto areaWidth = ImGui::GetContentRegionAvail().x * 0.5f;

		// if (ggpo_join_screen || test_game_screen)
		//{
		//	std::string player_name = config::PlayerName.get();
		//	ImGui::SetCursorPosX(10.0f + areaWidth - (ImGui::CalcTextSize(player_name.c_str()).x / 2.0f));
		//	ImGui::Text("%s", player_name.c_str());
		// }
		// else
		{
			ImGui::SetCursorPosX(10.0f + areaWidth - (ImGui::CalcTextSize("Player X").x / 2.0f));
			ImGui::Text("Player %d", i + 1);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.594f, 0.806f, 0.912f, 1.f));

		int num = 0;
		if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_DOWN) == 1)
			num = 2;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_LEFT) == 1)
			num = 4;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_RIGHT) == 1)
			num = 6;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_UP) == 1)
			num = 8;

		if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_DOWN) == 1 &&
			dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_LEFT) == 1)
			num = 1;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_DOWN) == 1 &&
				 dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_RIGHT) == 1)
			num = 3;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_UP) == 1 &&
				 dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_LEFT) == 1)
			num = 7;
		else if (dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_UP) == 1 &&
				 dojo.button_check_pressed[i].count(DreamcastKey::DC_DPAD_RIGHT) == 1)
			num = 9;

		ImGui::SetCursorPosX(areaWidth);

		if (num == 1)
			ImGui::Text("%s", ICON_KI_ARROW_BOTTOM_LEFT);
		else if (num == 2)
			ImGui::Text("%s", ICON_KI_ARROW_BOTTOM);
		else if (num == 3)
			ImGui::Text("%s", ICON_KI_ARROW_BOTTOM_RIGHT);
		else if (num == 4)
			ImGui::Text("%s", ICON_KI_ARROW_LEFT);
		else if (num == 5)
			ImGui::Text("    ");
		else if (num == 6)
			ImGui::Text("%s", ICON_KI_ARROW_RIGHT);
		else if (num == 7)
			ImGui::Text("%s", ICON_KI_ARROW_TOP_LEFT);
		else if (num == 8)
			ImGui::Text("%s", ICON_KI_ARROW_TOP);
		else if (num == 9)
			ImGui::Text("%s", ICON_KI_ARROW_TOP_RIGHT);
		else
			ImGui::Text(" ");

		ImGui::PopStyleColor();

		ImGui::SetCursorPosX((areaWidth) * 0.5f);

		if (settings.platform.isArcade())
		{
			// ImGui::Text("%s %s %s", ICON_KI_BUTTON_A, ICON_KI_BUTTON_B, ICON_KI_BUTTON_C);
			// ImGui::Text("%s %s %s", ICON_KI_BUTTON_X, ICON_KI_BUTTON_Y, ICON_KI_BUTTON_Z);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_A) == 1)
				ImGui::TextColored(ImVec4(255, 0, 0, 1), "%s", ICON_KI_BUTTON_A);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_A);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_B) == 1)
				ImGui::TextColored(ImVec4(0, 175, 255, 1), "%s", ICON_KI_BUTTON_B);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_B);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_C) == 1)
				ImGui::TextColored(ImVec4(255, 155, 0, 1), "%s", ICON_KI_BUTTON_C);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_C);

			ImGui::SetCursorPosX((areaWidth) * 0.5f);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_X) == 1)
				ImGui::TextColored(ImVec4(255, 255, 0, 1), "%s", ICON_KI_BUTTON_X);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_X);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_Y) == 1)
				ImGui::TextColored(ImVec4(0, 255, 0, 1), "%s", ICON_KI_BUTTON_Y);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_Y);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_Z) == 1)
				ImGui::TextColored(ImVec4(255, 0, 175, 1), "%s", ICON_KI_BUTTON_Z);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_Z);

			ImGui::SetCursorPosX(areaWidth - ImGui::CalcTextSize("A").x / 2.0f);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_START) == 1)
				ImGui::TextColored(ImVec4(165, 0, 255, 1), "%s", ICON_KI_CARET_TOP);
			else
				ImGui::Text("%s", ICON_KI_CARET_TOP);

			ImGui::Text(" ");

			std::set<int>::reverse_iterator rit;
			for (rit = dojo.button_check_pressed[i].rbegin(); rit != dojo.button_check_pressed[i].rend(); ++rit)
			{
				const char *button_name = GetCurrentGameButtonName((DreamcastKey)*rit);
				if (button_name != nullptr && strlen(button_name) > 0 && button_name != " ")
					ImGui::Text("%s\n", button_name);
			}
		}
		else
		{
			// ImGui::Text("%s %s %s", ICON_KI_BUTTON_X, ICON_KI_BUTTON_Y, ICON_KI_STICK_LEFT_TOP);
			// ImGui::Text("%s %s %s", ICON_KI_BUTTON_A, ICON_KI_BUTTON_B, ICON_KI_STICK_RIGHT_TOP);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_X) == 1)
				ImGui::TextColored(ImVec4(255, 255, 0, 1), "%s", ICON_KI_BUTTON_X);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_X);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_Y) == 1)
				ImGui::TextColored(ImVec4(0, 255, 0, 1), "%s", ICON_KI_BUTTON_Y);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_Y);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_AXIS_LT) == 1)
				ImGui::TextColored(ImVec4(255, 155, 0, 1), "%s", ICON_KI_STICK_LEFT_TOP);
			else
				ImGui::Text("%s", ICON_KI_STICK_LEFT_TOP);

			ImGui::SetCursorPosX((areaWidth) * 0.5f);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_A) == 1)
				ImGui::TextColored(ImVec4(255, 0, 0, 1), "%s", ICON_KI_BUTTON_A);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_A);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_B) == 1)
				ImGui::TextColored(ImVec4(0, 175, 255, 1), "%s", ICON_KI_BUTTON_B);
			else
				ImGui::Text("%s", ICON_KI_BUTTON_B);

			ImGui::SameLine();

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_AXIS_RT) == 1)
				ImGui::TextColored(ImVec4(255, 155, 0, 1), "%s", ICON_KI_STICK_RIGHT_TOP);
			else
				ImGui::Text("%s", ICON_KI_STICK_RIGHT_TOP);

			ImGui::SetCursorPosX(areaWidth - ImGui::CalcTextSize("A").x / 2.0f);

			if (dojo.button_check_pressed[i].count(DreamcastKey::DC_BTN_START) == 1)
				ImGui::TextColored(ImVec4(165, 0, 255, 1), "%s", ICON_KI_CARET_TOP);
			else
				ImGui::Text("%s", ICON_KI_CARET_TOP);
		}
		ImGui::End();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.335f, 0.155f, 0.770f, 1.000f));

	std::string msg_text;
#if defined(__ANDROID__)
	msg_text = "Press MENU to exit.";
#else
	msg_text = "Press MENU or TAB to exit.";
#endif

	float msg_font_size = ImGui::CalcTextSize(msg_text.c_str()).x;

#if defined(__APPLE__) || defined(__ANDROID__)
	ImGui::SetNextWindowPos(ImVec2((settings.display.width / 2) - ((msg_font_size + 40) / 2), settings.display.height - 60));
	ImGui::SetNextWindowSize(ImVec2(msg_font_size + 40, 60));
#else
	ImGui::SetNextWindowPos(ImVec2((settings.display.width / 2) - ((msg_font_size + 40) / 2), settings.display.height - 40));
	ImGui::SetNextWindowSize(ImVec2(msg_font_size + 40, 40));
#endif
	ImGui::SetNextWindowBgAlpha(0.65f);
	ImGui::Begin("#exit_description", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);

	ImGui::SameLine(
		(ImGui::GetContentRegionAvail().x / 2) -
		msg_font_size + (msg_font_size / 2) + 10);

	ImGui::TextUnformatted(msg_text.c_str());

	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void DojoGui::gui_display_test_game()
{
	const float scaling = settings.display.uiScale;

	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(330 * scaling, 0));

	ImGui::Begin("##test_game", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Columns(2, "buttons", false);

	int displayed_button_count = 0;

	std::string start_btn_txt = "Main Menu";
	if (strlen(settings.content.path.data()) > 0)
		start_btn_txt = "Start Game";
	if (ImGui::Button(start_btn_txt.data(), ImVec2(150 * scaling, 50 * scaling)))
	{
		gui_setState(GuiState::Closed);

		if (strlen(settings.content.path.data()) > 0)
		{
			std::string extension = get_file_extension(settings.content.path);
			// dreamcast games use built-in bios by default
			if (extension == "chd" || extension == "gdi" || extension == "cdi")
			{
				settings.platform.system = DC_PLATFORM_DREAMCAST;
			}
			else
			{
				int platform = naomi_cart_GetPlatform(settings.content.path.data());
				settings.platform.system = platform;
			}

			gui_start_game(settings.content.path);
		}
		else
		{
			cfgSetVirtual("dojo", "TestGame", "no");
			gui_setState(GuiState::Main);
		}
	}

	displayed_button_count++;
	ImGui::NextColumn();

	if (settings.content.path.size() > 0)
	{
		if (ImGui::Button("Start Training", ImVec2(150 * scaling, 50 * scaling)))
		{
			config::Delay = 0;
			cfgSetVirtual("dojo", "Training", "yes");
			gui_setState(GuiState::Closed);

			if (strlen(settings.content.path.data()) > 0)
			{
				std::string extension = get_file_extension(settings.content.path);
				// dreamcast games use built-in bios by default
				if (extension == "chd" || extension == "gdi" || extension == "cdi")
				{
					settings.platform.system = DC_PLATFORM_DREAMCAST;
				}
				else
				{
					int platform = naomi_cart_GetPlatform(settings.content.path.data());
					settings.platform.system = platform;
				}

				gui_start_game(settings.content.path);
			}
			else
			{
				gui_setState(GuiState::Main);
			}
		}

		displayed_button_count++;
		ImGui::NextColumn();
	}

	if (ImGui::Button("Button Check", ScaledVec2(150, 50)) && !settings.network.online)
	{
		test_game_screen = true;
		gui_setState(GuiState::ButtonCheck);
	}

	displayed_button_count++;
	ImGui::NextColumn();

	if (ImGui::Button("Settings", ImVec2(150 * scaling, 50 * scaling)))
	{
		gui_setState(GuiState::Settings);
	}

	displayed_button_count++;

	ImVec2 exit_size;

	if (displayed_button_count % 2 == 0)
	{
		ImGui::Columns(1, nullptr, false);
		exit_size = ScaledVec2(300, 50) + ImVec2(ImGui::GetStyle().ColumnsMinSpacing + ImGui::GetStyle().FramePadding.x * 2 - 1, 0);
	}
	else
	{
		ImGui::NextColumn();
		exit_size = ScaledVec2(150, 50);
	}

	// Exit
	if (ImGui::Button("Exit", exit_size))
	{
		dc_exit();
	}

	/*
	ImGui::NextColumn();

	std::string filename = settings.content.path.substr(settings.content.path.find_last_of("/\\") + 1);
	std::string game_name = get_file_basename(filename);
	std::string net_state_path = get_writable_data_path(game_name + ".state.net");

	bool save_exists = false;
	if(std::filesystem::exists(net_state_path))
		save_exists = true;

	if(!save_exists)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}

	if (ImGui::Button("Delete Savestate", ImVec2(150 * scaling, 50 * scaling)))
	{
		if(std::filesystem::exists(net_state_path))
			std::filesystem::remove(net_state_path);
	}

	if(!save_exists)
	{
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}
	*/

	ImGui::End();
}

void DojoGui::gui_display_select_platform()
{
	const float scaling = settings.display.uiScale;

	ImGui::SetNextWindowPos(ImVec2(settings.display.width / 2.f, settings.display.height / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(330 * scaling, 0));

	ImGui::Begin("Choose Platform", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Columns(2, "buttons", false);

	if (ImGui::Button("Dreamcast", ImVec2(150 * scaling, 50 * scaling)))
	{
		settings.platform.system = DC_PLATFORM_DREAMCAST;
		dojo_gui.quick_map_settings_call = true;
		gui_setState(GuiState::QuickPlayerSelect);
	}

	ImGui::NextColumn();

	if (ImGui::Button("Arcade", ImVec2(150 * scaling, 50 * scaling)))
	{
		settings.platform.system = DC_PLATFORM_NAOMI;
		dojo_gui.quick_map_settings_call = true;
		gui_setState(GuiState::QuickPlayerSelect);
	}

	ImGui::NextColumn();

	ImGui::Columns(1, nullptr, false);
	ImVec2 cancel_size = ScaledVec2(300, 50) + ImVec2(ImGui::GetStyle().ColumnsMinSpacing + ImGui::GetStyle().FramePadding.x * 2 - 1, 0);

	if (ImGui::Button("Cancel", cancel_size))
	{
		gui_setState(GuiState::Settings);
	}

	ImGui::End();
}

void DojoGui::gui_display_replays()
{
	char replays_txt[128];
	sprintf(replays_txt, "%s Replays - %s ", ICON_FA_FILM, dojo.game_name.c_str());
	ImGui::OpenPopup(replays_txt);
	ImGui::SetNextWindowSize(ImVec2(400, 300));
	if (ImGui::BeginPopupModal(replays_txt, NULL, ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (ImGui::BeginTabBar("GGPOTabBar", ImGuiTabBarFlags_None))
		{
			bool is_selected;

			char local_txt[128];
			sprintf(local_txt, " %s Local ", ICON_FA_HARD_DRIVE);
			if (ImGui::BeginTabItem(local_txt))
			{
				auto replays_dir = std::filesystem::path(get_writable_data_path("replays"));
				auto game_replays_dir = replays_dir / get_game_name();

				if (!std::filesystem::exists(game_replays_dir))
					std::filesystem::create_directories(game_replays_dir);

				if (ImGui::BeginChild("Replays##LocalReplays", ImVec2(0, 200.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_DragScrolling | ImGuiWindowFlags_NavFlattened))
				{
					for (const auto& entry : std::filesystem::directory_iterator(game_replays_dir))
					{
						std::string filename = entry.path().filename().string();
						if (ImGui::Selectable(filename.data(), &is_selected))
						{
							config::Replay = true;
							config::ReplayFilename = entry.path().string();
							ImGui::CloseCurrentPopup();
							gui_setState(GuiState::Main);
						}
					}
					ImGui::EndChild();
				}

				ImGui::EndTabItem();
			}

			char close_btn_txt[128];
			sprintf(close_btn_txt, "%s Close", ICON_FA_CIRCLE_XMARK);

			if (ImGui::Button(close_btn_txt))
			{
				settings.content.path = "";
				dojo.game_name = "";
				ImGui::CloseCurrentPopup();
				gui_setState(GuiState::Main);
			}
		}

		ImGui::EndPopup();
	}
}
