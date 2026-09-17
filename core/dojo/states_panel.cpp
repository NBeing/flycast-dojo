#include "roll_host.h"
#include "ui_text.h"
#include "roll_marks.h"
#include "movie.h"
#include "dojo.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "rend/gui_util.h"
#include "rend/imgui_driver.h"
#include "imgui.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "oslib/oslib.h"
#include "deps/filesystem.hpp"
#include <ctime>
#include <chrono>
#include <map>
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
	no thumbnail DISPLAY in this grid yet - but `[CORRECTED 2026-09-15]`
	thumbnails ARE written now: tas_thumb (core/dojo/thumbnail.cpp) writes
	<state>.png on every save, and a GL GetLastFrameRGB readback was added
	(core/rend/gles/gles.h) so it works here, not only DX9/DX11. Showing them in
	this grid is the remaining half (branch_panel's drawThumb is the pattern to
	reuse); no rename (saveSavestateLabel has no
	callers, G7); no delete (deleteSavestate has none either, G8); no
	generations pane. Reading is the whole of this slice.
*/
namespace roll
{

static bool statesOpen = false;

/*
	A slot's thumbnail as an ImGui image, or nothing when the slot has none.

	`[PORTED 2026-09-16]` branch_panel's drawThumb, the pattern its own comment
	pointed here to: cached on the imguiDriver texture cache keyed by the host's
	opaque handle, reloaded only when the file behind it changes. The handle comes
	from host()->slotThumbnail(); this panel never builds a path, so a host that
	keeps states elsewhere simply returns a key its own loader understands.
*/
static void drawSlotThumb(const std::string& handle, float maxW, float maxH)
{
	if (imguiDriver == nullptr || handle.empty())
		return;
	std::error_code ec;
	if (!ghc::filesystem::exists(handle, ec))
		return;
	const int64_t mtime = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
			ghc::filesystem::last_write_time(handle, ec).time_since_epoch()).count();
	const uintmax_t fsize = (uintmax_t)ghc::filesystem::file_size(handle, ec);
	struct Meta { int64_t mtime; uintmax_t size; int w; int h; };
	static std::map<std::string, Meta> meta;
	ImTextureID id = imguiDriver->getTexture(handle);
	auto it = meta.find(handle);
	if (!(id != ImTextureID() && it != meta.end() && it->second.mtime == mtime && it->second.size == fsize))
	{
		int w = 0, h = 0;
		u8 *data = loadImage(handle, w, h);
		if (data == nullptr)
			return;
		id = imguiDriver->updateTexture(handle, data, w, h);
		free(data);
		if (id == ImTextureID())
			return;
		meta[handle] = Meta{ mtime, fsize, w, h };
		it = meta.find(handle);
	}
	const int w = it->second.w, h = it->second.h;
	if (w <= 0 || h <= 0)
		return;
	float sc = std::min(maxW / (float)w, maxH / (float)h);
	if (sc > 1.f)
		sc = 1.f;
	ImGui::Image(id, ImVec2(w * sc, h * sc));
}

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
/*
	IS THE GENERATIONS TABLE EVEN BEGUN?

	`[MEASURED 2026-09-13]` a clipped table returns false from BeginTable and its
	entire body is skipped, which from outside is indistinguishable from "the
	rows are not there" - and that is exactly how a whole pane stayed unreachable
	without any test noticing. Kept rather than deleted: "did this draw at all"
	is the question a panel trace should answer FIRST, before any question about
	what it drew.
*/
static bool traceGensBegin(bool ok)
{
	if (cfgLoadBool("dojo", "StatesTrace", false))
	{
		static int last = -1;
		if (last != (int)ok) { last = (int)ok; NOTICE_LOG(RENDERER, "STATES GENS: BeginTable=%s", ok ? "yes" : "NO"); }
	}
	return ok;
}

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
		tasTextDisabled("No host installed - there is nothing to ask about slots.");
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

	tasText("%d of %d slots hold a state", occupied, n);
	if (stale != 0)
	{
		ImGui::SameLine();
		tasTextColored(TAS_P2_COL, "  %d stale", stale);
	}
	if (unjudged != 0)
	{
		ImGui::SameLine();
		// NOT FOLDED INTO "clean", deliberately, and the phrasing follows the
		// tree's own vocabulary: a state that MIGHT be dead is not a state that
		// is fine. These predate re-record sequencing and cannot be judged.
		tasTextDisabled("  %d unjudged", unjudged);
	}
	ImGui::SameLine();
	tasCheckbox("show empty", &showEmpty);

	if (!movie::authored())
		tasTextDisabled("No movie open - a slot's frame has nothing to be a frame OF.");

	ImGui::Separator();

	/*
		THE WALL MUST LEAVE ROOM FOR WHAT IS UNDER IT.

		`[MEASURED 2026-09-13]` this passed no size, and a ScrollY table with no
		size FILLS THE REMAINING HEIGHT. So the wall took the whole window and
		everything after it - the separator, the Generations header, its table -
		was clipped away. `BeginTable("##gens", ...)` returned false on every
		frame, its body never ran, and the generations pane was UNREACHABLE for
		any user with a non-trivial number of slots. It is 100 here.

		Nothing reported it because nothing clicked it: scripts/statestest.sh
		reads traces and drives no input, so it saw the counts it asked for and
		never noticed the pane they describe was off screen. It took
		scripts/statesuitest.sh - which has to aim a real mouse at a real cell -
		to fail, and it failed by being unable to find the cell at all.

		Reserved only when the window is tall enough to be worth splitting;
		below that the wall keeps the space it has, because half of two panes is
		worse than one.
	*/
	const float availY = ImGui::GetContentRegionAvail().y;
	// PROPORTIONAL, NOT A FIXED RESERVE. `[MEASURED 2026-09-13]` a fixed 240 px
	// reserve applied only above a threshold, and the docked panel is shorter
	// than that threshold - so the fix did nothing and the pane stayed
	// unreachable. A fraction always splits, and the cap keeps a tall window
	// from giving the generations pane more room than it can use.
	const float reserve = std::min(240.f, availY * 0.45f);
	const ImVec2 wallSize(0.f, availY > 120.f ? -reserve : 0.f);
	if (cfgLoadBool("dojo", "StatesTrace", false))
	{
		static int lastH = -1;
		if (lastH != (int)availY)
		{
			lastH = (int)availY;
			NOTICE_LOG(RENDERER, "STATES LAYOUT: avail=%d wall=%d reserve=%d",
					(int)availY, (int)wallSize.y, (int)reserve);
		}
	}
	if (!ImGui::BeginTable("##states", 8,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_SizingFixedFit, wallSize))
		return;
	ImGui::TableSetupScrollFreeze(1, 1);
	tasTableSetupColumn("slot");
	tasTableSetupColumn("frame");
	tasTableSetupColumn("state");
	tasTableSetupColumn("size");
	tasTableSetupColumn("saved");
	tasTableSetupColumn("preview");
	tasTableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
	tasTableSetupColumn("");
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
		if (i == 0) tasText("BASE");
		else        tasText("%d", i);

		ImGui::TableNextColumn();
		if (!v.exists)
			tasTextDisabled("-");
		else if (!v.haveFrame)
			// A state with no sidecar has no position in the movie. Drawing 0
			// would put it on the first row, which is a lie about where it is.
			tasTextDisabled("(none)");
		else
		{
			const bool here = movie::authored() && v.frame == playhead;
			tasTextColored(here ? TAS_FOCUS_RING : ImGui::GetStyle().Colors[ImGuiCol_Text],
					"%u", v.frame);
			if (marks().has(v.frame))
			{
				ImGui::SameLine();
				tasTextDisabled("*");	// a bookmark sits on the same frame
			}
		}

		ImGui::TableNextColumn();
		if (!v.exists)          tasTextDisabled("empty");
		else if (v.stale)       tasTextColored(TAS_P2_COL, "stale");
		else if (!v.judged)     tasTextDisabled("unjudged");
		else                    tasText("clean");

		ImGui::TableNextColumn();
		if (v.exists) tasText("%.1f MB", (double)v.bytes / 1048576.0);
		else          tasTextDisabled("-");

		ImGui::TableNextColumn();
		tasTextUnformatted(v.exists ? whenText(v.mtime).c_str() : "-");

		// PREVIEW - the state's own frame, written beside it on save. An occupied
		// slot with no thumbnail (GL/Vulkan gave no readback, or an older save)
		// draws a dash, not a broken cell, so "no image" reads as a fact.
		ImGui::TableNextColumn();
		if (v.exists)
		{
			const std::string thumb = h->slotThumbnail(i);
			if (!thumb.empty())
				drawSlotThumb(thumb, 128.f, 72.f);
			else
				tasTextDisabled("-");
		}
		else
			tasTextDisabled("-");

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
					tasTextDisabled("refused");
				editRow = -1;
			}
			else if (ImGui::IsItemDeactivated())
				editRow = -1;		// Escape, or clicked away without editing
			ImGui::PopID();
		}
		else
		{
			ImGui::PushID(i);
			if (v.label.empty()) tasTextDisabled("(unnamed)");
			else                 tasTextUnformatted(v.label.c_str());
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
				if (tasSmallButton("x"))
				{
					armedDelete = i;
					armedAt = os_GetSeconds();
				}
				if (ImGui::IsItemHovered())
					tasTip("Delete this state and its sidecars");
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, TAS_WRITE);
				if (tasSmallButton("really?"))
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
		THUMB PROBE, `dojo:StatesThumbProbe=yes`. Proves the DISPLAY WIRING: the
		panel gets a usable thumbnail handle for an occupied slot through the host
		(never a path it built itself). POLLS rather than one-shots, because its
		vehicle (thumbtest's FST sweep) writes the images progressively - so it
		waits for the first occupied slot whose slotThumbnail() is non-empty, then
		verifies that handle points at an image that exists. The DRAWING itself is
		drawSlotThumb, byte-identical to branch_panel's proven drawThumb.
	*/
	{
		static bool thumbProbed = false;
		if (!thumbProbed && cfgLoadBool("dojo", "StatesThumbProbe", false))
		{
			int slot = -1;
			std::string thumb;
			for (int i = 0; i < n; i++)
			{
				SlotView vt;
				if (h->slotView(i, vt) && vt.exists)
				{
					const std::string t = h->slotThumbnail(i);
					if (!t.empty()) { slot = i; thumb = t; break; }
				}
			}
			if (slot >= 0)		// else: no thumbnail written yet - poll next draw
			{
				thumbProbed = true;
				std::error_code ec;
				const bool exists = ghc::filesystem::exists(thumb, ec);
				const bool isPng  = thumb.size() > 4 && thumb.compare(thumb.size() - 4, 4, ".png") == 0;
				NOTICE_LOG(RENDERER, "STATES THUMBPROBE: slot=%d exists=%s png=%s => %s (%s)",
						slot, exists ? "yes" : "NO", isPng ? "yes" : "NO",
						(exists && isPng) ? "PASS" : "FAIL", thumb.c_str());
			}
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
		if (cfgLoadBool("dojo", "StatesTrace", false))
		{
			static int lastGn = -2;
			if (lastGn != gn) { lastGn = gn; NOTICE_LOG(RENDERER, "STATES GENS: count=%d", gn); }
		}
		if (gn == 0)
			tasTextDisabled("No snapshots of this slot set yet.");
		else if (traceGensBegin(ImGui::BeginTable("##gens", 7, ImGuiTableFlags_Borders
				| ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
				| ImGuiTableFlags_ScrollY, ImVec2(0, 160.f))))
		{
			ImGui::TableSetupScrollFreeze(1, 1);
			tasTableSetupColumn("#");
			tasTableSetupColumn("kind");
			tasTableSetupColumn("created");
			tasTableSetupColumn("files");
			tasTableSetupColumn("MB");
			tasTableSetupColumn("at frame");
			tasTableSetupColumn("tags / notes", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int i = 0; i < gn; i++)
			{
				SnapshotView g;
				if (!h->snapshotView(i, g))
				{
					if (cfgLoadBool("dojo", "StatesTrace", false))
					{
						static int lastSkip = -2;
						if (lastSkip != i) { lastSkip = i; NOTICE_LOG(RENDERER, "STATES GENS: row %d has no view - skipped", i); }
					}
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				tasText("%d", i + 1);
				if (!g.onDisk)
				{
					ImGui::SameLine();
					// RECORDED BUT ABSENT is a real state and says so: the
					// alternative is a row that looks fine and restores nothing.
					tasTextColored(TAS_P2_COL, "!");
					if (ImGui::IsItemHovered())
						tasTip("recorded, but its files are not on disk");
				}
				else if (g.synthesized && ImGui::IsItemHovered())
					tasTip("record rebuilt from the folder");

				ImGui::TableNextColumn();
				tasText("%s %02d", g.kindLabel.c_str(), g.ordinal);
				ImGui::TableNextColumn();
				tasTextUnformatted(g.createdLocal.empty() ? "-" : g.createdLocal.c_str());
				ImGui::TableNextColumn();
				tasText("%d", g.files);
				ImGui::TableNextColumn();
				tasText("%.1f", (double)g.bytes / 1048576.0);
				ImGui::TableNextColumn();
				if (g.haveFrame) tasText("%u", g.atFrame);
				else             tasTextDisabled("-");

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
					if (g.tags.empty() && g.notes.empty()) tasTextDisabled("%s", shown.c_str());
					else                                   tasTextUnformatted(shown.c_str());
					if (ImGui::IsItemHovered())
					{
						ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
						// DID THE MOUSE EVER ARRIVE? Separates "the harness
						// aimed at the wrong pixel" from "the click was not
						// delivered", which produce the same silence.
						if (cfgLoadBool("dojo", "StatesTrace", false))
						{
							static bool said = false;
							if (!said) { said = true;
								const ImVec2 m = ImGui::GetIO().MousePos;
								NOTICE_LOG(RENDERER, "STATES CELL: hovered at %d,%d", (int)m.x, (int)m.y); }
						}
					}
					/*
						WHERE THIS CELL IS, for a harness that has to click it.

						`dojo:StatesTrace`, first row only, logged on change. A
						test cannot guess: the pane is DOCKED, so its origin
						moves with the layout, and a click computed from a
						constant lands on the wrong cell - or on nothing - and
						reports the feature broken. Published relative to the
						main viewport, because that is the origin an OS window
						position is added to.

						The CENTRE rather than a corner, so a pixel of rounding
						or a window-manager title bar does not decide the
						result.
					*/
					if (i == 0 && cfgLoadBool("dojo", "StatesTrace", false))
					{
						const ImVec2 a = ImGui::GetItemRectMin();
						const ImVec2 b = ImGui::GetItemRectMax();
						const ImVec2 o = ImGui::GetMainViewport()->Pos;
						char sig[96];
						snprintf(sig, sizeof(sig), "gen row=0 x=%d y=%d",
								(int)((a.x + b.x) * 0.5f - o.x),
								(int)((a.y + b.y) * 0.5f - o.y));
						static std::string lastCell;
						if (lastCell != sig)
						{
							lastCell = sig;
							NOTICE_LOG(RENDERER, "STATES CELL: %s", sig);
						}
					}
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						genEditRow = i;
						// Click edits TAGS; the notes are reachable from the
						// same cell with Ctrl, rather than stealing a column
						// from a pane that is already seven wide.
						genEditCol = ImGui::GetIO().KeyCtrl ? 1 : 0;
						// WHICH GESTURE WAS IT. One cell, two destinations,
						// chosen by a modifier - so the only way to tell a
						// working Ctrl+click from a plain one that happens to
						// land in the same place is to say which was decided.
						if (cfgLoadBool("dojo", "StatesTrace", false))
							NOTICE_LOG(RENDERER, "STATES EDIT: gen row=%d col=%d (%s)",
									genEditRow, genEditCol,
									genEditCol == 1 ? "notes" : "tags");
						snprintf(genEditBuf, sizeof(genEditBuf), "%s",
								genEditCol == 0 ? g.tags.c_str() : g.notes.c_str());
					}
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
			tasTextDisabled("click to tag, Ctrl+click to annotate");
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
	// 760x520 so the wall AND the generations pane under it both fit. Docked
	// beside the game with no default this opened at 68 pixels of content
	// height and the generations pane was unreachable - see draw().
	panels::add({ "states", "States", &statesOpen, draw, panels::Both, /*persist*/ true,
			/*defW*/ 760.f, /*defH*/ 520.f });
	NOTICE_LOG(RENDERER, "STATES PANEL: registered=%s open=%s",
			panels::find("states") != nullptr ? "yes" : "NO", statesOpen ? "yes" : "no");
}

}	// namespace roll
