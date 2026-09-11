#include "input/hotkeys.h"
#include "input/gamepad_device.h"
#include "input/mapping.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "tas_colors.h"
#include "imgui.h"
#include "log/LogManager.h"
#include <string>

/*
	THE HOTKEY CHEAT SHEET - what each TAS action is, and what it is bound to
	RIGHT NOW.

	`[PORTED 2026-09-10]` from the TAS fork, where it is the item a survey of
	its eighteen hotkeys rated cheapest-for-the-value: ~165 lines and
	self-contained, depending only on the registry and a binding lookup.

	THE KEY IS LOOKED UP LIVE, never stored, and the fork says why:

		"The KEY shown in the panel is looked up LIVE from the keyboard device's
		 input mapping (single source of truth), so this display can never drift
		 from reality - and a future remap UI edits the same mapping."

	That is the same rule core/rend/panel.h and docs/HOTKEYS.md are both built
	on: one owner per fact. A cheat sheet holding its own copy of the bindings
	is a cheat sheet that lies the first time somebody rebinds anything, and it
	lies QUIETLY - which is the failure mode this tree keeps paying for.

	WHAT IS NOT HERE. The fork's version also does drag-to-reorder persisted in
	`dojo:HotkeyOrder`, a pinned NoInputs overlay arm, a Shift-to-peek, pad
	chips with per-device tooltips, and a hardcoded F11 row. Those are real UX
	and they are deliberately not in this first cut: the panel registry already
	gives docking, persistence and a View-menu entry, and the rest should land
	when there is something to reorder - five rows sort themselves.
*/
namespace roll
{

static bool hotkeysOpen = false;

//! The keyboard, if there is one. Nothing else can name a scancode.
static std::shared_ptr<GamepadDevice> keyboardDevice()
{
	for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
	{
		const std::shared_ptr<GamepadDevice> g = GamepadDevice::GetGamepad(i);
		// `[MEASURED 2026-09-10]` "is it a keyboard" cannot be asked by NAME:
		// this machine's keyboard enumerates as "Kinesis Freestyle2 PC - KB800"
		// and a second one as "Keyboard". What actually distinguishes them for
		// this purpose is whether the device can NAME a scancode, so that is
		// what gets asked - F1 is 58 and every real keyboard names it.
		if (g && g->get_input_mapping() != nullptr && g->get_button_name(58) != nullptr)
			return g;
	}
	return nullptr;
}

static void draw()
{
	const std::shared_ptr<GamepadDevice> kb = keyboardDevice();
	/*
		WHICH KEYBOARD THIS PANEL IS READING, said once and again whenever it
		changes. Not per frame.

		"The panel opened" and "the panel has anything to show" are different
		claims, and from outside the process they look identical - which is the
		gap that has cost this tree the most time. A harness can now assert the
		second. It is also the support answer for an empty cheat sheet: this
		machine enumerates one keyboard that can name scancodes and one that
		cannot, and which one you get decides what the panel says.
	*/
	static std::string lastDev = "\x01";
	const std::string devNow = kb != nullptr ? kb->name() : std::string("(none)");
	if (devNow != lastDev)
	{
		lastDev = devNow;
		NOTICE_LOG(RENDERER, "HOTKEY PANEL: reading bindings from [%s], %d actions",
				devNow.c_str(), hotkeys::count());
	}
	if (kb == nullptr)
	{
		// SAID, not drawn as an empty list. "No keyboard" and "nothing bound"
		// are different answers and an empty table gives neither.
		ImGui::TextDisabled("No keyboard that can name its keys - nothing to show.");
		return;
	}
	const std::shared_ptr<InputMapping> map = kb->get_input_mapping();

	if (!ImGui::BeginTable("##hotkeys", 2,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		return;
	for (int i = 0; i < hotkeys::count(); i++)
	{
		const hotkeys::Action& a = hotkeys::all()[i];
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(a.label);
		ImGui::TableSetColumnIndex(1);

		const u32 code = map->get_button_code(0, a.id);
		if (code == (u32)-1)
		{
			// UNBOUND IS A REAL ANSWER and it is the common one here: these
			// ship with no default key, because every key the fork defaults
			// them to is already a training binding in this tree.
			ImGui::TextDisabled("unbound");
			continue;
		}
		const char *name = kb->get_button_name(code);
		if (name != nullptr)
			ImGui::TextColored(TAS_FOCUS_RING, "%s", name);
		else
			// The raw code, rather than nothing: a key this device cannot name
			// is still bound, and hiding that would make it look unbound.
			ImGui::TextDisabled("[%u]", code);
	}
	ImGui::EndTable();
	ImGui::Separator();
	ImGui::TextDisabled("Rebind under Settings > Controls, then Map on a device.");
}

void registerHotkeyPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "hotkeys", "Hotkeys", &hotkeysOpen, draw, panels::Both, /*persist*/ true });
	NOTICE_LOG(RENDERER, "HOTKEY PANEL: registered=%s open=%s",
			panels::find("hotkeys") != nullptr ? "yes" : "NO", hotkeysOpen ? "yes" : "no");
}

}	// namespace roll
