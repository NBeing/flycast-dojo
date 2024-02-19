#include "dojo_gui.h"

void DojoGui::gui_display_ggpo_connect()
{
	std::string title = "Connect to GGPO Opponent";
	ImGui::OpenPopup(title.data());
	if (ImGui::BeginPopupModal(title.data(), NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiInputTextFlags_EnterReturnsTrue))
	{
		static char si[128] = "";
		if (ImGui::BeginTabBar("GGPOTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("IP Entry"))
			{
				presence.beacon_active = false;
				presence.lobby_active = false;
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
					if (!presence.beacon_active)
					{
						presence.beacon_active = true;
						std::thread t3(&NetBeacon::BeaconThread, std::ref(presence));
						t3.detach();
					}

					if (!presence.lobby_active)
					{
						presence.lobby_active = true;
						std::thread t4(&NetBeacon::ListenerThread, std::ref(presence));
						t4.detach();
					}

					if (ImGui::BeginChild("Beacons", ImVec2(0, 100.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_DragScrolling | ImGuiWindowFlags_NavFlattened))
					{
						for (auto it = presence.active_beacons.begin(); it != presence.active_beacons.end(); ++it)
						{
							if (presence.last_seen[it->first.data()] + 5000 > presence.unix_timestamp() &&
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

		ImGui::SliderInt("", (int *)&current_delay, 0, 20);
		ImGui::SameLine();
		ImGui::Text("Delay");

		ImGui::Columns(2, "hosting", false);
		ImGui::RadioButton("Host", &hosting, 1);
		ImGui::NextColumn();
		ImGui::RadioButton("Join", &hosting, 0);
		ImGui::Columns(1, NULL, false);

		if (ImGui::Button("Start"))
		{
			if (hosting)
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
		if (ImGui::Button("Cancel"))
		{
			config::GGPOEnable.set(false);
			SaveSettings();

			settings.content.path = "";
			ImGui::CloseCurrentPopup();
			gui_setState(GuiState::Main);
		}

		ImGui::SameLine(0, 128.0f + ImGui::CalcTextSize("IP").x + ImGui::CalcTextSize("Paste").x - ImGui::CalcTextSize("Start").x);

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