#include "ui_text.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

/*
	THE UI TEXT EDITOR - rename anything on screen, and see it change.

	`[PORTED 2026-09-14]` in spirit from the fork's `tasUiTextEditorWindow`
	(211 lines), but NOT line by line, and the differences are the point.

	HIS NEEDS A DEPENDENCY WE DO NOT HAVE. His edit box is
	`ImGuiColorTextEdit` - 5,106 lines of third-party widget, pulled in for
	multi-cursor editing, Ctrl+D-next-occurrence and move-line. That is a
	wonderful editor for the Notepad, which is what it was added for. For
	renaming a button it is an enormous amount of machinery to carry, so this
	uses `InputTextMultiline`, which ImGui already has.

	HIS LISTS A MANIFEST; THIS LISTS WHAT WAS DRAWN. See the long note on
	`uitext::seen()` - a source-scanning Python extractor is a second owner of
	"what strings this UI has", and CLAUDE.md §4 is about exactly that failure.
	The cost is that this list is only ever "strings seen so far", and the panel
	SAYS SO rather than implying it is complete. A count that silently means
	something narrower than it looks is the failure this tree keeps paying for.
*/
namespace roll
{

static bool uiTextOpen = false;
static char filterBuf[128] = "";
static bool editedOnly = false;
static char editBuf[1024] = "";

/*
	THE SELECTION IS A KEY, NEVER A ROW INDEX.

	`[MEASURED 2026-09-14]` the first version held an index into the vector from
	uitext::seen(), and it was wrong within one frame of being used. THE LIST
	GROWS WHILE YOU LOOK AT IT: this panel's own labels are registered as it
	draws them, so clicking a row at index 2 and reading rows[2] on the next
	frame reads a DIFFERENT string - the count went 9 -> 12 between the click
	and the next render.

	That is not a cosmetic drift. The edit box was filled at click time and the
	key was read from the stale index, so the panel showed "Capture selection"
	beside key 4d2d86621ae3904a while key("Capture selection") is
	5c8b6f117cd58bf7 - and Apply would have written the override under the wrong
	key, renaming a string the user never selected.

	core/rend/panel.h states the rule for its own registry: prefer "a lookup
	that FAILS LOUDLY if the id ever changes, rather than an index that silently
	points at whatever moved into its place". Same rule, one panel along.
*/
static std::string selectedKey;

static void draw()
{
	std::vector<uitext::Seen> rows;
	uitext::seen(rows);

	/*
		THE HEADING SAYS WHAT THE NUMBER MEANS. "412 strings" reads as "every
		string in the UI"; it is not, and cannot be - a panel that has never
		been opened has drawn nothing and contributes nothing here. Doctrine
		rule 4: state coverage, because green is not scope.
	*/
	tasText("%u strings drawn so far this session", (unsigned)rows.size());
	tasTextDisabled("Open a panel to add its labels to this list.");

	ImGui::SetNextItemWidth(200.f);
	tasInputTextWithHint("##uifilter", "filter", filterBuf, sizeof(filterBuf));
	ImGui::SameLine();
	tasCheckbox("edited only", &editedOnly);
	ImGui::SameLine();
	int nOver = 0;
	for (const uitext::Seen& s : rows)
		if (s.overridden)
			nOver++;
	if (nOver > 0)
		tasTextColored(TAS_ACTIVE_COL, "%d edited", nOver);
	else
		tasTextDisabled("none edited");

	ImGui::Separator();

	const float availY = ImGui::GetContentRegionAvail().y;
	const float listH = std::max(80.f, availY - 120.f);
	if (ImGui::BeginChild("##uilist", ImVec2(0, listH), true))
	{
		int shown = 0;
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const uitext::Seen& s = rows[i];
			if (editedOnly && !s.overridden)
				continue;
			if (filterBuf[0] != 0 && strstr(s.original.c_str(), filterBuf) == nullptr
					&& strstr(s.replacement.c_str(), filterBuf) == nullptr)
				continue;
			shown++;
			ImGui::PushID(s.key.c_str());
			// THE ORIGINAL IS THE LABEL, so the row reads as the thing you are
			// about to rename. Selectable's own id comes from the KEY pushed
			// above, not from the text, so a string containing "##" cannot
			// break the row and a row keeps its identity as the list grows.
			const bool sel = (s.key == selectedKey);
			if (ImGui::Selectable("##row", sel))
			{
				selectedKey = s.key;
				snprintf(editBuf, sizeof(editBuf), "%s",
						s.overridden ? s.replacement.c_str() : s.original.c_str());
			}
			ImGui::SameLine(0, 0);
			if (s.overridden)
			{
				tasTextColored(TAS_ACTIVE_COL, "%s", s.replacement.c_str());
				ImGui::SameLine();
				tasTextDisabled("(was: %s)", s.original.c_str());
			}
			else
				ImGui::TextUnformatted(s.original.c_str());
			ImGui::PopID();
		}
		if (shown == 0)
			// NOT AN EMPTY BOX. "nothing matches the filter" and "nothing has
			// been drawn yet" are different answers.
			tasTextDisabled(rows.empty() ? "Nothing drawn yet."
					: "No string matches this filter.");
	}
	ImGui::EndChild();

	// LOOK THE SELECTION UP BY KEY. A key that is no longer in the list cannot
	// happen today (the registry only grows within a session) but clearing the
	// selection is the honest response if it ever does, rather than editing
	// whatever sits at a remembered position.
	const uitext::Seen *sel = nullptr;
	for (const uitext::Seen& r : rows)
		if (r.key == selectedKey)
		{
			sel = &r;
			break;
		}
	if (sel == nullptr)
	{
		tasTextDisabled("Pick a string above to rename it.");
		return;
	}
	const uitext::Seen& s = *sel;
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputTextMultiline("##uiedit", editBuf, sizeof(editBuf), ImVec2(0, 48.f));

	/*
		THE FORMAT GUARD, SAID BEFORE IT BITES. An override whose %-codes differ
		from the original's is rendered literally rather than through printf -
		which is what stops a mismatch crashing - but from the user's side that
		looks like "my text appeared with %u in it". Telling them here is the
		difference between a safety net and a mystery.
	*/
	const std::string wantSig = uitext::fmtSignature(s.original.c_str());
	const std::string haveSig = uitext::fmtSignature(editBuf);
	if (wantSig != haveSig)
	{
		tasTextColored(TAS_WRITE, "the %% codes differ from the original");
		tasTextDisabled("original has \"%s\", this has \"%s\" - values will NOT be filled in",
				wantSig.c_str(), haveSig.c_str());
	}

	if (tasButton("Apply"))
	{
		uitext::set(s.key, editBuf);
		NOTICE_LOG(RENDERER, "UI TEXT: %s -> \"%s\"", s.key.c_str(), editBuf);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!s.overridden);
	if (tasButton("Reset"))
	{
		uitext::set(s.key, "");
		snprintf(editBuf, sizeof(editBuf), "%s", s.original.c_str());
		NOTICE_LOG(RENDERER, "UI TEXT: %s reset", s.key.c_str());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	tasTextDisabled("key %s", s.key.c_str());
}

void registerUiTextPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "uitext", "UI Text", &uiTextOpen, draw, panels::Menu,
			/*persist*/ true, /*defW*/ 560.f, /*defH*/ 420.f });
	NOTICE_LOG(RENDERER, "UITEXT PANEL: registered=%s open=%s",
			panels::find("uitext") != nullptr ? "yes" : "NO", uiTextOpen ? "yes" : "no");
}

}	// namespace roll
