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
#include "oslib/oslib.h"
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

// ONE EDIT BUFFER AND A ROW, not a buffer per slot. A hundred slots would be a
// hundred buffers kept in step with a scan that changes underneath them; one
// buffer can only ever be stale about the row it is on.
static int  editRow = -1;
static char editBuf[80] = {};

// TWO STEPS AND A DEADLINE, for the one irreversible thing this panel can do.
// A modal would be the other answer and is worse here: it stops the frame, and
// this panel is drawn while a movie may be running. Arming a specific slot and
// letting it disarm itself means a stray click cannot delete anything, and a
// deliberate one takes two.
// The generations pane's own click-to-edit cell. Separate from the wall's
// because they are different tables with different rows, and one shared row
// index between two lists is how a rename lands on the wrong thing.
static int  genEditRow = -1;
static int  genEditCol = 0;			// 0 = tags, 1 = notes
static char genEditBuf[512] = {};

static int    armedDelete = -1;
static double armedAt = 0.0;
static constexpr double ARM_SECONDS = 4.0;

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

	if (!ImGui::BeginTable("##states", 7,
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
	ImGui::TableSetupColumn("");
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
		if (!v.exists)
		{
			// Nothing to name. The host refuses it too, so this is agreement
			// rather than the UI guessing at the rule.
		}
		else if (editRow == i)
		{
			ImGui::SetNextItemWidth(-1.f);
			ImGui::PushID(i);
			// Committed on Enter or on losing focus, NOT per keystroke: a
			// sidecar write per character would be a file write per character.
			if (ImGui::InputText("##label", editBuf, sizeof(editBuf),
					ImGuiInputTextFlags_EnterReturnsTrue)
				|| ImGui::IsItemDeactivatedAfterEdit())
			{
				if (!h->setSlotLabel(i, editBuf))
					// SAID, not swallowed. The host refuses when there is no
					// clip folder, and a rename that silently did nothing would
					// look exactly like one that worked until the next refresh.
					ImGui::TextDisabled("refused");
				editRow = -1;
			}
			else if (ImGui::IsItemDeactivated())
				editRow = -1;		// Escape, or clicked away without editing
			ImGui::PopID();
		}
		else
		{
			ImGui::PushID(i);
			if (v.label.empty()) ImGui::TextDisabled("(unnamed)");
			else                 ImGui::TextUnformatted(v.label.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				editRow = i;
				snprintf(editBuf, sizeof(editBuf), "%s", v.label.c_str());
			}
			ImGui::PopID();
		}

		// ---- delete, armed ----
		ImGui::TableNextColumn();
		if (v.exists)
		{
			ImGui::PushID(i);
			const bool armed = armedDelete == i
					&& os_GetSeconds() - armedAt < ARM_SECONDS;
			if (!armed)
			{
				if (ImGui::SmallButton("x"))
				{
					armedDelete = i;
					armedAt = os_GetSeconds();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Delete this state and its sidecars");
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, TAS_WRITE);
				if (ImGui::SmallButton("really?"))
				{
					// SAID, not swallowed - the host refuses with no clip
					// folder, and a delete that quietly did nothing would look
					// like one that worked until the next rescan.
					if (!h->deleteSlot(i))
						NOTICE_LOG(RENDERER, "STATES: delete of slot %d was REFUSED", i);
					armedDelete = -1;
				}
				ImGui::PopStyleColor();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndTable();

	/*
		LABEL PROBE, `dojo:StatesLabelProbe=yes`. One shot.

		Naming a slot is the first WRITE in the host interface, and the only way
		to know it landed is to make the round trip the UI makes: write, force a
		rescan, read back through slotView(). Reading back the string we just
		passed in would prove nothing - that is the cache agreeing with itself,
		the mistake that cost an afternoon on the drag trace.

		Safe under scripts/rolltest.sh, which copies the clip and its savestates
		into a temp directory per run. Off by default: it renames a real slot.
	*/
	{
		static bool probed = false;
		if (!probed && occupied > 0 && cfgLoadBool("dojo", "StatesLabelProbe", false))
		{
			probed = true;
			int slot = -1;
			SlotView v0;
			for (int i = 0; i < n; i++)
				if (h->slotView(i, v0) && v0.exists) { slot = i; break; }

			const std::string was = v0.label;
			const char *want = "probe label";
			const bool wrote = h->setSlotLabel(slot, want);

			SlotView v1;
			const bool readBack = h->slotView(slot, v1) && v1.label == want;

			// Put it back, and check THAT too - an empty label must remove the
			// sidecar rather than leave an empty one, which is the one branch of
			// saveSavestateLabel a happy path never exercises.
			const bool restored = h->setSlotLabel(slot, was);
			SlotView v2;
			const bool back = h->slotView(slot, v2) && v2.label == was;

			NOTICE_LOG(RENDERER, "STATES LABELPROBE: slot=%d wrote=%s readback=%s"
					" restored=%s back=%s (was \"%s\")  => %s",
					slot, wrote ? "yes" : "NO", readBack ? "yes" : "NO",
					restored ? "yes" : "NO", back ? "yes" : "NO", was.c_str(),
					(wrote && readBack && restored && back) ? "PASS" : "FAIL");
		}
	}

	/*
		DELETE PROBE, `dojo:StatesDeleteProbe=yes`. One shot, and DESTRUCTIVE.

		It removes a real savestate, so it runs only under scripts/statestest.sh,
		which copies the clip and its states into a temp directory - and in a
		SECOND launch, after the read and label checks have had their turn on an
		intact one.
	*/
	{
		static bool delProbed = false;
		if (!delProbed && occupied > 0 && cfgLoadBool("dojo", "StatesDeleteProbe", false))
		{
			delProbed = true;
			int slot = -1;
			SlotView v0;
			for (int i = 0; i < n; i++)
				if (h->slotView(i, v0) && v0.exists) { slot = i; break; }

			const bool deleted = h->deleteSlot(slot);
			SlotView v1;
			// READ BACK THROUGH THE SCAN, so this measures the disk rather than
			// the return value agreeing with itself.
			const bool gone = h->slotView(slot, v1) && !v1.exists;
			// And the notice, which is the one thing that distinguishes a
			// deletion from a stranding for anything drawing markers.
			const bool noticed = h->staleNoticePhase() >= 0 && h->staleNoticeWasDeletion();
			const bool refusesEmpty = !h->deleteSlot(slot);	// now empty: must refuse

			NOTICE_LOG(RENDERER, "STATES DELETEPROBE: slot=%d deleted=%s gone=%s notice=%s"
					" refuses-empty=%s  => %s", slot, deleted ? "yes" : "NO",
					gone ? "yes" : "NO", noticed ? "yes" : "NO",
					refusesEmpty ? "yes" : "NO",
					(deleted && gone && noticed && refusesEmpty) ? "PASS" : "FAIL");
		}
	}

	// ---- GENERATIONS: snapshots of the whole slot set ---------------------
	//
	// `[MEASURED 2026-09-09]` docs/STATES-LIFT.md: this pane is what the fork's
	// show_states_snapshots delegates to, and it has ZERO game-specific symbols.
	// The whole port cost is the host interface above.
	//
	// THE `#` COLUMN IS A RUNNING COUNT IN DISPLAY ORDER and not the host's
	// stored number, which is the fork's own hard-won lesson: its folder numbers
	// restart per kind, so a list of eight ended with "06" and its comment reads
	// "8 or 6 backups?". The stored number sits beside its kind where it means
	// something.
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Generations", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int gn = h->snapshotCount();
		if (gn == 0)
			ImGui::TextDisabled("No snapshots of this slot set yet.");
		else if (ImGui::BeginTable("##gens", 7, ImGuiTableFlags_Borders
				| ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
				| ImGuiTableFlags_ScrollY, ImVec2(0, 160.f)))
		{
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn("#");
			ImGui::TableSetupColumn("kind");
			ImGui::TableSetupColumn("created");
			ImGui::TableSetupColumn("files");
			ImGui::TableSetupColumn("MB");
			ImGui::TableSetupColumn("at frame");
			ImGui::TableSetupColumn("tags / notes", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int i = 0; i < gn; i++)
			{
				SnapshotView g;
				if (!h->snapshotView(i, g))
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%d", i + 1);
				if (!g.onDisk)
				{
					ImGui::SameLine();
					// RECORDED BUT ABSENT is a real state and says so: the
					// alternative is a row that looks fine and restores nothing.
					ImGui::TextColored(TAS_P2_COL, "!");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("recorded, but its files are not on disk");
				}
				else if (g.synthesized && ImGui::IsItemHovered())
					ImGui::SetTooltip("record rebuilt from the folder");

				ImGui::TableNextColumn();
				ImGui::Text("%s %02d", g.kindLabel.c_str(), g.ordinal);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(g.createdLocal.empty() ? "-" : g.createdLocal.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%d", g.files);
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", (double)g.bytes / 1048576.0);
				ImGui::TableNextColumn();
				if (g.haveFrame) ImGui::Text("%u", g.atFrame);
				else             ImGui::TextDisabled("-");

				ImGui::TableNextColumn();
				ImGui::PushID(i);
				if (genEditRow == i)
				{
					ImGui::SetNextItemWidth(-1.f);
					if (ImGui::InputText("##ge", genEditBuf, sizeof(genEditBuf),
							ImGuiInputTextFlags_EnterReturnsTrue)
						|| ImGui::IsItemDeactivatedAfterEdit())
					{
						const bool ok = genEditCol == 0
								? h->setSnapshotTags(g.id, genEditBuf)
								: h->setSnapshotNotes(g.id, genEditBuf);
						if (!ok)
							NOTICE_LOG(RENDERER, "STATES: annotating snapshot %d was REFUSED", i);
						genEditRow = -1;
					}
					else if (ImGui::IsItemDeactivated())
						genEditRow = -1;
				}
				else
				{
					const std::string shown = g.tags.empty() && g.notes.empty()
							? std::string("(untagged)")
							: g.tags + (g.notes.empty() ? "" : "  -  " + g.notes);
					if (g.tags.empty() && g.notes.empty()) ImGui::TextDisabled("%s", shown.c_str());
					else                                   ImGui::TextUnformatted(shown.c_str());
					if (ImGui::IsItemHovered())
						ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						genEditRow = i;
						// Click edits TAGS; the notes are reachable from the
						// same cell with Ctrl, rather than stealing a column
						// from a pane that is already seven wide.
						genEditCol = ImGui::GetIO().KeyCtrl ? 1 : 0;
						snprintf(genEditBuf, sizeof(genEditBuf), "%s",
								genEditCol == 0 ? g.tags.c_str() : g.notes.c_str());
					}
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
			ImGui::TextDisabled("click to tag, Ctrl+click to annotate");
		}

		/*
			GENERATIONS PROBE, `dojo:StatesGenProbe=yes`. One shot, and it MAKES
			a snapshot, so it only belongs in a harness working on a copy.

			Counting what is already there would be vacuous - the test clip has
			no generations - so this creates one and watches the count move,
			which is the only version of the claim that can fail.
		*/
		static bool genProbed = false;
		if (!genProbed && cfgLoadBool("dojo", "StatesGenProbe", false)
				&& !hostfs::savestateFolderOverride.empty())
		{
			genProbed = true;
			const int before = h->snapshotCount();
			int files = 0;
			u64 bytes = 0;
			const int num = dojo.ArchiveClipDir(hostfs::savestateFolderOverride,
					&files, &bytes, "gen");
			// COPYING AND RECORDING ARE TWO STEPS, and the first alone is
			// invisible. `[MEASURED 2026-09-10]` the probe called only
			// ArchiveClipDir and reported "archived=1 files=4 count 0 -> 0" -
			// four files on disk and nothing in the library, which is exactly
			// what a half-finished F8 would leave behind. Dojo::ArchiveGeneration
			// pairs them and this follows it.
			if (num >= 0)
			{
				dojo.RecordGeneration(num, files, bytes, "gen");
				dojo.ReconcileGenerations(hostfs::savestateFolderOverride);
			}
			const int after = h->snapshotCount();

			// Tag the new one and read it back THROUGH the host, so this
			// measures clip.json rather than the string handed in.
			bool tagged = false, readBack = false;
			SnapshotView g;
			if (after > before && h->snapshotView(after - 1, g))
			{
				tagged = h->setSnapshotTags(g.id, "probe, auto");
				SnapshotView g2;
				readBack = h->snapshotView(after - 1, g2)
						&& g2.tags.find("probe") != std::string::npos;
			}
			NOTICE_LOG(RENDERER, "STATES GENPROBE: archived=%d files=%d count %d -> %d"
					" tagged=%s readback=%s  => %s", num, files, before, after,
					tagged ? "yes" : "NO", readBack ? "yes" : "NO",
					(num >= 0 && after == before + 1 && tagged && readBack) ? "PASS" : "FAIL");
		}
	}

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
