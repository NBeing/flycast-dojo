#include "input/hotkeys.h"
#include "hotkey_bind.h"
#include "ui_text.h"
#include "input/gamepad_device.h"
#include "input/mapping.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "tas_colors.h"
#include "imgui.h"
#include "log/LogManager.h"
#include <string>
#include <vector>

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

	`[LANDED 2026-09-14]` IT REBINDS NOW. The line above about "a future remap UI"
	was written when this was read-only; `core/dojo/hotkey_bind.{h,cpp}` is that
	engine, and it writes to exactly the mapping this table reads - so the cheat
	sheet updates the instant a binding changes, because it was never holding a
	copy.

	WHAT IS STILL NOT HERE. The fork also does drag-to-reorder persisted in
	`dojo:HotkeyOrder`, a pinned NoInputs overlay arm, a Shift-to-peek, and a
	hardcoded F11 row. Copy-bindings-between-pads is out for a concrete reason
	rather than taste: it needs `InputMapping::get_button_codes` /
	`get_axis_codes`, plural getters this tree does not have, because an action
	with two inputs must copy as two rather than as the first one found.
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

static int deviceSel = 0;		//!< 0 = the keyboard, 1.. = pads()[n-1]

//! The device the table is showing and editing. Null when there is nothing.
static std::shared_ptr<GamepadDevice> currentDevice()
{
	const std::vector<std::shared_ptr<GamepadDevice>> p = rebind::pads();
	if (deviceSel <= 0)
		return rebind::keyboard();
	const int i = deviceSel - 1;
	return i < (int)p.size() ? p[i] : rebind::keyboard();
}

static void draw()
{
	// THE DETECTION IS ADVANCED HERE, not from a global tick. A rebind cannot
	// start without this panel open, so the panel owning the clock keeps the
	// feature in one place - unlike the Frame Skip Test, whose sweep runs with
	// its window shut and therefore needs its own tick in mainui.
	rebind::tick();

	/*
		ESCAPE CANCELS. Read as an ImGui key rather than from the device,
		because while a PAD is armed the keyboard is deliberately still live
		and its Escape never reaches the detector. When the keyboard itself is
		armed, hotkey_bind filters Escape on the way in.
	*/
	if (rebind::active() && ImGui::IsKeyPressed(ImGuiKey_Escape))
		rebind::cancel();

	const std::vector<std::shared_ptr<GamepadDevice>> pads = rebind::pads();
	const std::shared_ptr<GamepadDevice> kb = rebind::keyboard();

	if (kb == nullptr && pads.empty())
	{
		// SAID, not drawn as an empty table. "No device" and "nothing bound"
		// are different answers.
		tasTextDisabled("No keyboard or pad that can be mapped - nothing to show.");
		return;
	}

	// ---- which device -------------------------------------------------------------
	if (kb != nullptr)
	{
		if (ImGui::RadioButton(kb->name().c_str(), deviceSel == 0))
			deviceSel = 0;
	}
	for (int i = 0; i < (int)pads.size(); i++)
	{
		if (kb != nullptr || i > 0)
			ImGui::SameLine();
		if (ImGui::RadioButton(pads[i]->name().c_str(), deviceSel == i + 1))
			deviceSel = i + 1;
	}

	const std::shared_ptr<GamepadDevice> dev = currentDevice();
	if (dev == nullptr)
	{
		tasTextDisabled("That device went away.");
		return;
	}
	/*
		WHICH DEVICE THIS PANEL IS READING, said once and again on change.
		"The panel opened" and "the panel has anything to show" are different
		claims and look identical from outside the process.
	*/
	static std::string lastDev = "\x01";
	if (dev->name() != lastDev)
	{
		lastDev = dev->name();
		NOTICE_LOG(RENDERER, "HOTKEY PANEL: reading bindings from [%s], %d actions",
				lastDev.c_str(), hotkeys::count());
	}

	if (rebind::active())
		tasTextColored(TAS_ACTIVE_COL, "press a key or button...  %.0f s  (Escape cancels)",
				rebind::remaining());
	else
		tasTextDisabled("Click a binding to change it.");
	ImGui::Separator();

	if (!ImGui::BeginTable("##hotkeys", 3,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		return;
	tasTableSetupColumn("action");
	tasTableSetupColumn("bound to");
	tasTableSetupColumn("##clear", ImGuiTableColumnFlags_WidthFixed, 60.f);
	ImGui::TableHeadersRow();

	for (int i = 0; i < hotkeys::count(); i++)
	{
		const hotkeys::Action& a = hotkeys::all()[i];
		const bool waiting = rebind::active() && rebind::pendingKey() == a.id;
		ImGui::PushID(i);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		tasTextUnformatted(a.label);

		ImGui::TableSetColumnIndex(1);
		const std::string bound = rebind::bindingName(dev, a.id);
		/*
			THE WHOLE CELL IS THE BUTTON, so there is no hunting for a small
			target, and its id comes from PushID rather than from the label -
			a binding named the same as another row cannot collide with it.
		*/
		if (waiting)
			ImGui::TextColored(TAS_ACTIVE_COL, "[ press ]");
		else if (ImGui::Selectable(bound.empty() ? "##unbound" : bound.c_str(), false))
			rebind::arm(a.id, dev);
		if (!waiting && bound.empty())
		{
			/*
				UNBOUND IS A REAL ANSWER and must be drawn as one, not as an
				empty cell - an empty cell reads as "this row failed to load".

				`[CORRECTED 2026-09-14]` this comment used to say the TAS
				actions "ship with no default key". They do not.
				`core/input/keyboard_device.h` defaults at least
				EMU_BTN_SLOT_PICKER (Shift+F4) and EMU_BTN_PIANO_ROLL
				(Shift+F5), and a fresh config shows six actions bound. The old
				claim was inherited from when this panel was written and nothing
				re-checked it - which is what the provenance marks are for.
			*/
			ImGui::SameLine(0, 0);
			tasTextDisabled("unbound");
		}

		ImGui::TableSetColumnIndex(2);
		ImGui::BeginDisabled(bound.empty() || rebind::active());
		if (tasSmallButton("clear"))
			rebind::clearBinding(a.id, dev);
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	ImGui::EndTable();
	ImGui::Separator();
	tasTextDisabled("Bindings are saved to this device's mapping file.");
}

void registerHotkeyPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	/*
		A SIZE NOW, because the content changed. `[MEASURED 2026-09-14]` this
		registration passed no defW/defH - "leave it to ImGui" - which was fine
		for the two-column read-only table it was written for. Turning it into a
		rebind UI added device radio buttons, a status line and a third column,
		and the first screenshot showed the window mostly off the left edge and
		too narrow to read a single row.

		core/rend/panel.h makes this per-panel rather than a blanket default
		precisely so a content change carries its own size: "a window size is a
		fact about ONE panel", and the last time one number was shared it fixed
		the States window and broke the Piano Roll's test.
	*/
	panels::add({ "hotkeys", "Hotkeys", &hotkeysOpen, draw, panels::Both,
			/*persist*/ true, /*defW*/ 460.f, /*defH*/ 300.f });
	NOTICE_LOG(RENDERER, "HOTKEY PANEL: registered=%s open=%s",
			panels::find("hotkeys") != nullptr ? "yes" : "NO", hotkeysOpen ? "yes" : "no");
}

}	// namespace roll
