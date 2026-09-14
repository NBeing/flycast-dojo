/*
	UI TEXT OVERRIDES - every label in the studio, editable at runtime.

	`[PORTED 2026-09-14]` from the TAS fork's `uiResolve` layer
	(reference/flycast-rr @ edca8915, ~36 wrapper functions over ~387 lines).
	This is the one genuinely file-wide coupling in his 26,591-line
	`dojo_gui.cpp`: almost every widget call goes through a `tas*` wrapper, so
	adopting or stripping it is an up-front all-or-nothing decision and deciding
	it per window guarantees a mixed file.

	THE CALL SITE IS A RENAME AND NOTHING ELSE:

		ImGui::Text("combo row")   ->   tasText("combo row")
		ImGui::Button("Generate")  ->   tasButton("Generate")

	Same arguments, same return. THE KEY IS A HASH OF THE ENGLISH STRING, not a
	hand-assigned id - there is no per-string bookkeeping to maintain and no way
	for a call site to name the wrong key.

	HIS NAMES ARE KEPT DELIBERATELY. `tasText` rather than `ui::text` is not the
	convention this tree would choose on its own, but the entire value of
	adopting this layer is that his remaining windows paste in unchanged.
	Renaming them keeps the indirection and throws away the reason for it.

	WHAT AN OVERRIDE CANNOT DO, because he thought about it and it is the
	property worth keeping: an override only goes through `printf` when it keeps
	the SAME %-specifiers, in the same order, as the original. Edit
	"Frame %u / %u" into "Frame %s" and you get the literal text, not a crash.
	See `fmtSignature`.

	THE COST, said plainly. The key is a hash of the source string, so CHANGING
	AN ENGLISH LABEL SILENTLY ORPHANS ITS OVERRIDE - the old key simply stops
	matching and the new text appears untranslated. That is inherent to
	string-keyed overrides rather than a defect in this port, and it is the
	quiet direction: nothing errors, a customisation just evaporates.
*/
#pragma once
#include "types.h"
#include "imgui.h"
#include <cstdarg>
#include <cstddef>
#include <string>
#include <vector>

namespace uitext {

/*
	The content key: FNV-1a 64 over the UTF-8 bytes, 16 lowercase hex.

	BYTE-COMPATIBLE WITH HIS `tools/extract_ui_text.py`, deliberately - an
	overrides file written by either fork loads in the other, and his manifest
	keys resolve here unchanged. That compatibility is the reason this is a
	hash of the text rather than something nicer.

	PRIVATE-USE-AREA CODEPOINTS ARE SKIPPED (U+E000..U+F8FF): the Font Awesome
	and Kenney `ICON_*` macros. A Python source scan cannot expand a macro, so a
	manifest string is always icon-free; stripping the glyph at runtime is what
	makes an icon-prefixed control match the icon-free key.
*/
std::string key(const char *s);

/*
	The sequence of printf conversion characters in a format string:
	"%d x %s" -> "ds", with "%%" and width/length modifiers ignored.

	An override may be passed to printf ONLY when its signature equals the
	original's. This is the safety property of the whole layer.
*/
std::string fmtSignature(const char *s);

/*
	Is there anything here a person could translate?

	`ImGui::Text("%s", buf)` becomes `tasText("%s", buf)` under a mechanical
	rename, and "%s" is not a label - nobody overrides it, and every such call
	site would otherwise add a meaningless row to the editor's list. The rule is
	"strip the conversions; is there a letter left?", so "%s" and "%u / %u" are
	out while "Frame %u / %u" stays in.

	NOT IN THE FORK, which registers them all. Its manifest comes from a source
	scan that has the same problem, and its own coverage sweep counts them as
	`g_uiCovDynamic` - gaps it knows about and cannot close.
*/
bool translatable(const char *s);

//! The override for `original`, or nullptr. Records the string as seen.
const char *resolve(const char *original);

//! Load / save `ui_text_overrides.json` in the writable data path.
void load();
void save();

//! Set or clear one override. An empty replacement removes it.
void set(const std::string& k, const std::string& replacement);

//! One string this UI has actually drawn.
struct Seen
{
	std::string key;
	std::string original;
	bool overridden = false;
	std::string replacement;
};

/*
	EVERY STRING THAT HAS PASSED THROUGH THIS LAYER THIS SESSION.

	`[CORRECTED from the fork]` his editor lists strings from a MANIFEST
	produced by `tools/extract_ui_text.py` scanning the source. That is a second
	owner of "what strings this UI draws", and CLAUDE.md §4 records exactly what
	that costs: "Does this host implement this name?" was answered in two places
	and "they agreed for months and parted company the moment a name moved onto
	a method table". The fix there was to DERIVE the answer - `emu.supports()`
	is built from the bindings that actually exist rather than from a declared
	list - and this is the same fix. A registry the wrappers populate cannot
	drift from the wrappers, and needs no build step and no Python.

	WHAT IS GIVEN UP, and it is real: a runtime registry only knows strings that
	have been DRAWN. A panel never opened contributes nothing, so this is
	"strings seen so far", never "all strings", and the editor must say so
	rather than implying completeness. His manifest genuinely knows them all
	statically. The trade is a list that is incomplete and honest against one
	that is complete until it is quietly wrong.
*/
void seen(std::vector<Seen>& out);

//! How many distinct strings have been drawn this session.
size_t seenCount();

//! Gated on `dojo:PanelSelfTest`. One line per claim; no ROM, no frame.
void selfTest();

}	// namespace uitext

// ---------------------------------------------------------------------------------------
// The wrappers. His names, his signatures - so his windows paste in unchanged.
// ---------------------------------------------------------------------------------------

void tasText(const char *fmt, ...);
void tasTextWrapped(const char *fmt, ...);
void tasTextColored(const ImVec4& col, const char *fmt, ...);
void tasTextDisabled(const char *fmt, ...);
void tasTextUnformatted(const char *text, const char *textEnd = nullptr);
void tasTip(const char *fmt, ...);
void tasTipItem(const char *fmt, ...);
bool tasButton(const char *label, const ImVec2& size = ImVec2(0, 0));
bool tasSmallButton(const char *label);
bool tasCheckbox(const char *label, bool *v);
bool tasSelectable(const char *label, bool selected = false,
		ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
bool tasMenuItem(const char *label, const char *shortcut = nullptr,
		bool selected = false, bool enabled = true);
bool tasRadioButton(const char *label, bool active);
bool tasInputTextWithHint(const char *label, const char *hint, char *buf, size_t bufSize,
		ImGuiInputTextFlags flags = 0);
void tasTableSetupColumn(const char *label, ImGuiTableColumnFlags flags = 0,
		float initW = 0.f, ImGuiID userId = 0);
