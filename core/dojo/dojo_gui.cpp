#include "dojo_gui.h"

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