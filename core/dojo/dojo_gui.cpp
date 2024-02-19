#include "dojo_gui.h"

void DojoGui::gui_display_ggpo_connect()
{
	std::string title = "Connect to GGPO Opponent";
	ImGui::OpenPopup(title.data());
	if (ImGui::BeginPopupModal(title.data(), NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiInputTextFlags_EnterReturnsTrue))
	{
		static char si[128] = "";
		if (!matched)
		{
			if (ImGui::BeginTabBar("GGPOTabBar", ImGuiTabBarFlags_None))
			{
				if (ImGui::BeginTabItem("Match Codes"))
				{
					local_tab = false;
					if (ImGui::Button("Host Game", ScaledVec2(150, 150)))
					{
						config::NetworkServer.set("");
						config::GGPOEnable.set(true);
						config::ActAsServer.set(true);
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
					if (ImGui::Button("Join Game", ScaledVec2(150, 150)))
					{
						config::NetworkServer.set("");
						config::GGPOEnable.set(true);
						config::ActAsServer.set(false);
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

				if (ImGui::BeginTabItem("IP Entry"))
				{
					local_tab = true;
					dojo.presence.beacon_active = false;
					dojo.presence.lobby_active = false;
					ImGui::InputTextWithHint("IP", "0.0.0.0", si, IM_ARRAYSIZE(si));
					detect_address = std::string(si);
#ifndef __ANDROID__
					ImGui::SameLine();
					if (ImGui::Button("Paste"))
					{
						char *pasted_txt = SDL_GetClipboardText();
						memcpy(si, pasted_txt, strlen(pasted_txt));
					}
#endif
					ImGui::EndTabItem();
				}

				if (config::NetBeaconEnable)
				{
					if (ImGui::BeginTabItem("Local Network"))
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
								if (dojo.presence.last_seen[it->first.data()] + 5000 > dojo.presence.unix_timestamp() &&
									(config::PlayerName.get() != it->first || it->first == "Player"))
									if (ImGui::Selectable((it->first).data(), selected_beacon == (it->first).data()))
									{
										selected_beacon = it->first;
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

			if (ImGui::Button("Start"))
			{
				if (hosting_opt)
					config::ActAsServer.set(true);
				else
					config::ActAsServer.set(false);

				config::GGPOEnable.set(true);
				config::NetworkEnable.set(false);
				config::NetworkServer.set(detect_address);

				NOTICE_LOG(NETWORK, "CONNECT %s", detect_address.data());
				if (current_delay != config::GGPODelay.get())
					config::GGPODelay.set(current_delay);

				SaveSettings();

				ImGui::CloseCurrentPopup();

				gui_start_game(settings.content.path);
			}

			ImGui::SameLine();
		}
		if (ImGui::Button("Cancel"))
		{
			config::GGPOEnable.set(false);

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

void DojoGui::show_player_name_overlay(bool paused)
{
	// if both player names are defaults, hide overlay
	if (dojo.player_2.length() <= 1 ||
		strcmp(dojo.player_1.data(), "Player") == 0 &&
			strcmp(dojo.player_1.data(), dojo.player_2.data()) == 0)
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
		if (ImGui::Button("Copy Match Code"))
		{
			SDL_SetClipboardText(dojo.match_code.data());
		}
		ImGui::SameLine();
#endif
	}
	if (ImGui::Button("Cancel"))
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
			if (ImGui::Button("Paste"))
			{
				char *pasted_txt = SDL_GetClipboardText();
				memcpy(mc, pasted_txt, strlen(pasted_txt));
			}
#endif
			if (ImGui::Button("Start Session"))
			{
				dojo.match_code = std::string(mc, strlen(mc));
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
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
