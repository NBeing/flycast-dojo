#include "roll_profile.h"
#include "roll_host.h"
#include "movie.h"
#include "dojo.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "input/gamepad.h"
#include "imgui.h"
#include <cstring>
#include <string>
#include "log/LogManager.h"
#include "cfg/cfg.h"

/*
	A PIANO ROLL: frames down, inputs across.

	The first vertical slice of the studio port, and deliberately a THIN one -
	it reads the movie and draws it, and edits nothing. The point is to prove
	the two seams carry a real panel before 3,832 lines of David's roll are
	brought over onto them.

	IT NAMES NO GAME AND NO EMULATOR. Columns come from roll::profile(), so the
	labels are MvC2's today and another game's tomorrow without touching this
	file. Savestate questions go through roll::host(), so this draws the same
	whether a slot is a file, a snapshot in memory, or something else entirely.
	The only concessions are reading dojo's frame record and the DC trigger
	bits, which is where a host adapter will eventually sit.

	`[MEASURED 2026-09-09]` docs/PIANO-ROLL-LIFT.md: of 161 real dependencies in
	David's show_piano_roll(), 4 are the emulator and 4 are the game. This file
	uses exactly those two seams and nothing else from either.
*/
namespace roll
{
static bool panelOpen = false;

// How many frames either side of the playhead to draw. Small on purpose: a
// window that scrolls to a selection is edit-tool work, and there are no edit
// tools yet.
static constexpr int SPAN = 24;

//! One player's half of a session_inputs row, or nothing if the row is absent
//! or too short. A hole in the movie is a legitimate state, not an error.
static bool frameAt(u32 frame, int player, FrameInputs& out)
{
	auto it = dojo.session_inputs.find(frame);
	if (it == dojo.session_inputs.end())
		return false;
	const size_t need = sizeof(FrameInputs) * (size_t)(player + 1);
	if (it->second.size() < need)
		return false;
	memcpy(&out, it->second.data() + (size_t)player * sizeof(FrameInputs), sizeof(FrameInputs));
	return true;
}

static void draw()
{
	const Profile& prof = profile();
	Host *h = host();

	if (!movie::authored())
	{
		ImGui::TextDisabled("No movie is open.");
		return;
	}

	const u32 playhead = dojo.frame_number.load();
	ImGui::Text("%s   frame %u of %u", prof.name, playhead, movie::end());
	if (h == nullptr)
		// Said rather than papered over: with no host there are no savestate
		// markers, and a blank gutter would look like "no states exist".
		ImGui::TextDisabled("No host installed - savestate markers unavailable.");
	ImGui::Separator();

	std::map<u32, std::vector<int>> slotsAt;
	if (h != nullptr)
		h->framesToSlots(slotsAt);

	const int cols = 2 + prof.count;	// frame, states, then the inputs
	if (!ImGui::BeginTable("##roll", cols,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
		return;

	ImGui::TableSetupScrollFreeze(2, 1);
	ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthFixed, 64.f);
	ImGui::TableSetupColumn("st", ImGuiTableColumnFlags_WidthFixed, 40.f);
	for (int c = 0; c < prof.count; c++)
		ImGui::TableSetupColumn(prof.cols[c].label, ImGuiTableColumnFlags_WidthFixed, 28.f);
	ImGui::TableHeadersRow();

	const u32 lo = playhead > (u32)SPAN ? playhead - SPAN : 0;
	const u32 hi = std::min(playhead + (u32)SPAN, movie::end());

	for (u32 f = lo; f < hi; f++)
	{
		ImGui::TableNextRow();
		if (f == playhead)
			ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
					ImGui::GetColorU32(tasCol(TAS_FOCUS_RING, 0.16f)));

		ImGui::TableNextColumn();
		// A frame with no record is a HOLE, not an end - movie.h exists to keep
		// that distinction, and the roll must show it rather than draw zeroes.
		if (movie::has(f)) ImGui::Text("%u", f);
		else               ImGui::TextDisabled("%u", f);

		ImGui::TableNextColumn();
		auto sit = slotsAt.find(f);
		if (sit != slotsAt.end() && !sit->second.empty())
		{
			const int slot = sit->second.front();
			const bool stale = h != nullptr && h->slotStale(slot);
			// Stale means the state no longer belongs to this timeline: still a
			// valid machine, no longer a point on THIS movie.
			if (stale) ImGui::TextColored(TAS_P2_COL, "%d!", slot);
			else       ImGui::Text("%d", slot);
		}

		FrameInputs fi{};
		const bool have = frameAt(f, 0, fi);
		for (int c = 0; c < prof.count; c++)
		{
			ImGui::TableNextColumn();
			if (!have)
				continue;
			if (pressed(prof.cols[c], fi.kcode, fi.triggers.l, fi.triggers.r,
					BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT))
				ImGui::TextColored(TAS_P1_COL, "%s", "\xe2\x96\xa0");	// filled square
		}
	}
	ImGui::EndTable();

	// ONE-SHOT DECODE CHECK. An all-empty grid is what a neutral stretch looks
	// like AND what a broken pressed() looks like; they are not distinguishable
	// by eye. Counts the presses actually decoded over the drawn span, once.
	// `dojo:RollDecodeTrace=yes` - debug-by-instrumentation, the house method.
	// Kept rather than deleted because an empty grid is what a neutral stretch
	// AND a broken decode both look like, and only this told them apart:
	// `[MEASURED 2026-09-09]` 591 of 11520 frames carry input, per column
	// ^:0 v:32 <:8 >:14 LP:0 HP:0 LK:421 HK:0 A1:0 A2:0 ST:116 - LK and ST
	// dominating because DC_BTN_A/START are what menus use, and this profile
	// calls DC_BTN_A "LK".
	//
	// Gated past the seek as well: the first draw is at frame 0, where no input
	// exists yet and "0 presses" says nothing.
	static bool counted = false;
	if (!counted && hi > lo && playhead > 10000
			&& cfgLoadBool("dojo", "RollDecodeTrace", false))
	{
		counted = true;
		int cells = 0, frames = 0;
		for (u32 f = lo; f < hi; f++)
		{
			FrameInputs t{};
			if (!frameAt(f, 0, t)) continue;
			frames++;
			for (int c = 0; c < prof.count; c++)
				if (pressed(prof.cols[c], t.kcode, t.triggers.l, t.triggers.r,
						BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT))
					cells++;
		}
		// Scan the WHOLE movie for the first frame carrying any input, for
		// either player. "48 idle frames" and "the decode is wrong" look
		// identical from one window; this distinguishes them.
		u32 firstAt = 0; u32 firstK = 0; int who = -1; int rowBytes = -1;
		for (const auto& kv : dojo.session_inputs)
		{
			if (rowBytes < 0) rowBytes = (int)kv.second.size();
			FrameInputs a{}, b{};
			const bool ha = frameAt(kv.first, 0, a), hb = frameAt(kv.first, 1, b);
			if (ha && a.kcode != 0) { firstAt = kv.first; firstK = a.kcode; who = 0; break; }
			if (hb && b.kcode != 0) { firstAt = kv.first; firstK = b.kcode; who = 1; break; }
		}
		// END TO END: does pressed() actually map kcode bits onto COLUMNS?
		// A correct frameAt with a wrong bit table still shows an empty grid.
		int active = 0, byCol[16] = {0};
		for (const auto& kv : dojo.session_inputs)
		{
			FrameInputs t{};
			if (!frameAt(kv.first, 0, t)) continue;
			bool any = false;
			for (int c = 0; c < prof.count && c < 16; c++)
				if (pressed(prof.cols[c], t.kcode, t.triggers.l, t.triggers.r,
						BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT)) { byCol[c]++; any = true; }
			if (any) active++;
		}
		std::string hist;
		for (int c = 0; c < prof.count && c < 16; c++)
			hist += std::string(c ? " " : "") + prof.cols[c].label + ":" + std::to_string(byCol[c]);
		NOTICE_LOG(RENDERER, "ROLL DECODE: sizeof=%d row=%d; first input p%d frame %u = 0x%05X; "
				"%d of %u frames have input; per column %s",
				(int)sizeof(FrameInputs), rowBytes, who, firstAt, firstK,
				active, (u32)dojo.session_inputs.size(), hist.c_str());
	}
}

void registerPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "pianoroll", "Piano Roll", &panelOpen, draw,
			panels::Both, /*persist*/ true });
	// A LOOKUP THAT CAN FAIL, rather than trusting the add. A panel that failed
	// to register is invisible in exactly the same way as one that is merely
	// closed, and the registry is the only thing that knows the difference.
	NOTICE_LOG(RENDERER, "ROLL PANEL: registered=%s open=%s cfg=%s",
			panels::find("pianoroll") != nullptr ? "yes" : "NO",
			panelOpen ? "yes" : "no",
			cfgLoadBool("dojo", "Panel.pianoroll", false) ? "yes" : "no");
}

}	// namespace roll
