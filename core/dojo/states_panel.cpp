#include "roll_host.h"
#include "roll_marks.h"
#include "movie.h"
#include "dojo.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "imgui.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <ctime>
#include <string>

/*
	THE STATES WINDOW - the slot wall.

	`[MEASURED 2026-09-09]` docs/STATES-LIFT.md. Two corrections shaped this file
	before a line of it was written:

	  The fork's `show_states_snapshots` is TEN LINES that delegate; it is the
	  generations pane docked UNDER the wall, not the wall. The wall itself lives
	  in its core/rend/gui.cpp, and has not been lifted.

	  The pane that WAS lifted has 131 real dependencies and **zero**
	  game-specific symbols. So unlike the piano roll, there is no game to
	  divorce here - the whole cost of this port is the host interface.

	IT NAMES NO EMULATOR AND NO FILE. Everything comes through roll::Host, which
	answers in slots and movie frames. There is deliberately no path on screen:
	the fork had to invent a `#` column because its folder numbers restart per
	kind, so the number shown and the number stored disagreed - a comment in its
	own source says "8 or 6 backups?".

	WHAT IT DOES NOT DO YET, said out loud rather than left to be discovered:
	no thumbnails (nothing in this tree writes one, and GetLastFrameRGB is
	DX9/DX11 only - STATES-LIFT G6); no rename (saveSavestateLabel has no
	callers, G7); no delete (deleteSavestate has none either, G8); no
	generations pane. Reading is the whole of this slice.
*/
namespace roll
{

static bool statesOpen = false;

//! Only slots that hold something, unless the user asks for the empties. A
//! hundred cells of which one is occupied is a worse view than one row.
static bool showEmpty = false;

static std::string whenText(s64 mtime)
{
	if (mtime <= 0)
		return "-";
	const std::time_t t = (std::time_t)mtime;
	std::tm tmv{};
#ifdef _WIN32
	localtime_s(&tmv, &t);
#else
	localtime_r(&t, &tmv);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
	return buf;
}

static void draw()
{
	Host *h = host();
	if (h == nullptr)
	{
		// Said rather than drawn as an empty wall: "no host" and "no states"
		// look identical, and this tree shipped a null host for a day.
		ImGui::TextDisabled("No host installed - there is nothing to ask about slots.");
		return;
	}

	const int n = h->slotCount();
	int occupied = 0, stale = 0, unjudged = 0;
	for (int i = 0; i < n; i++)
	{
		SlotView v;
		if (!h->slotView(i, v) || !v.exists)
			continue;
		occupied++;
		if (v.stale) stale++;
		if (!v.judged) unjudged++;
	}

	ImGui::Text("%d of %d slots hold a state", occupied, n);
	if (stale != 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(TAS_P2_COL, "  %d stale", stale);
	}
	if (unjudged != 0)
	{
		ImGui::SameLine();
		// NOT FOLDED INTO "clean", deliberately, and the phrasing follows the
		// tree's own vocabulary: a state that MIGHT be dead is not a state that
		// is fine. These predate re-record sequencing and cannot be judged.
		ImGui::TextDisabled("  %d unjudged", unjudged);
	}
	ImGui::SameLine();
	ImGui::Checkbox("show empty", &showEmpty);

	if (!movie::authored())
		ImGui::TextDisabled("No movie open - a slot's frame has nothing to be a frame OF.");

	ImGui::Separator();

	if (!ImGui::BeginTable("##states", 6,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_SizingFixedFit))
		return;
	ImGui::TableSetupScrollFreeze(1, 1);
	ImGui::TableSetupColumn("slot");
	ImGui::TableSetupColumn("frame");
	ImGui::TableSetupColumn("state");
	ImGui::TableSetupColumn("size");
	ImGui::TableSetupColumn("saved");
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableHeadersRow();

	const u32 playhead = dojo.frame_number.load();
	for (int i = 0; i < n; i++)
	{
		SlotView v;
		if (!h->slotView(i, v))
			continue;
		if (!v.exists && !showEmpty)
			continue;

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		// Slot 0 is BASE - the unsuffixed file, the power-on bookmark - and it
		// is labelled rather than left as a bare zero, because flycast's own
		// pause menu numbers slots from ONE and would show this same file as
		// "1" (docs/STATES-LIFT.md G11). Naming it sidesteps the collision.
		if (i == 0) ImGui::Text("BASE");
		else        ImGui::Text("%d", i);

		ImGui::TableNextColumn();
		if (!v.exists)
			ImGui::TextDisabled("-");
		else if (!v.haveFrame)
			// A state with no sidecar has no position in the movie. Drawing 0
			// would put it on the first row, which is a lie about where it is.
			ImGui::TextDisabled("(none)");
		else
		{
			const bool here = movie::authored() && v.frame == playhead;
			ImGui::TextColored(here ? TAS_FOCUS_RING : ImGui::GetStyle().Colors[ImGuiCol_Text],
					"%u", v.frame);
			if (marks().has(v.frame))
			{
				ImGui::SameLine();
				ImGui::TextDisabled("*");	// a bookmark sits on the same frame
			}
		}

		ImGui::TableNextColumn();
		if (!v.exists)          ImGui::TextDisabled("empty");
		else if (v.stale)       ImGui::TextColored(TAS_P2_COL, "stale");
		else if (!v.judged)     ImGui::TextDisabled("unjudged");
		else                    ImGui::Text("clean");

		ImGui::TableNextColumn();
		if (v.exists) ImGui::Text("%.1f MB", (double)v.bytes / 1048576.0);
		else          ImGui::TextDisabled("-");

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(v.exists ? whenText(v.mtime).c_str() : "-");

		ImGui::TableNextColumn();
		if (v.exists && !v.label.empty())
			ImGui::TextUnformatted(v.label.c_str());
		else if (v.exists)
			ImGui::TextDisabled("(unnamed)");
	}
	ImGui::EndTable();

	// `dojo:StatesTrace=yes` - the wall as one line, logged on CHANGE. An empty
	// wall is what "no states" and "the host answered nothing" both look like.
	if (cfgLoadBool("dojo", "StatesTrace", false))
	{
		static std::string last;
		char sig[128];
		snprintf(sig, sizeof(sig), "n=%d occupied=%d stale=%d unjudged=%d", n, occupied,
				stale, unjudged);
		if (last != sig)
		{
			last = sig;
			NOTICE_LOG(RENDERER, "STATES: %s", sig);
		}
	}
}

void registerStatesPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "states", "States", &statesOpen, draw, panels::Both, /*persist*/ true });
	NOTICE_LOG(RENDERER, "STATES PANEL: registered=%s open=%s",
			panels::find("states") != nullptr ? "yes" : "NO", statesOpen ? "yes" : "no");
}

}	// namespace roll
