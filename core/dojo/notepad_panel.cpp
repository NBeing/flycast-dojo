#include "TextEditor.h"
#include "surface_tour.h"
#include "ui_text.h"
#include "roll_notation.h"
#include "roll_profile.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "cfg/cfg.h"
#include "stdclass.h"
#include "log/LogManager.h"
#include "imgui.h"
#include "deps/filesystem.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/*
	THE NOTEPAD - author TAS notation as text, with a real editor: a per-line
	frame gutter, inline lint, and syntax coloring.

	`[PORTED 2026-09-15]` from the fork's tasNotepadWindow. The one window that
	genuinely NEEDED a third-party dependency, and the survey confirmed it rather
	than dissolving it (unlike the node editor): its value is the gutter, the
	diagnostic squiggles, playhead-follow and coloring - none of which ImGui's
	InputTextMultiline provides. So core/deps/ImGuiColorTextEdit is vendored
	(5,106 lines, ImGui 1.90.4, zero imgui_internal - a clean public-API widget,
	verified to compile against ours before a line of this window was written).

	TWO DELIBERATE DEPARTURES FROM HIM, both to honour one-owner-per-fact:

	- IT USES OUR NOTATION, not his codec. His Notepad drives a four-dialect
	  ~1,411-line codec (numpad / cardinals / CE letters / PPAD); this tree has
	  roll_notation, ONE dialect built from the profile, and roll_notation.h's
	  own comment says why a second is refused: "Each would be a second way to
	  say the same thing, which is how five dialects happen." So the editor is
	  linted and guttered by parsePattern, the same parser the piano roll uses.

	- IT DOES NOT WRITE THE ROLL. The roll owns applyPattern() - its edit funnel,
	  staging and tracks - and a second path from here would be the §4 defect the
	  whole tree is built to avoid. This is a SCRATCHPAD (his own word): you
	  compose and validate notation here, precisely, before committing it through
	  the roll's own tools. Text persists per session under dojo:NotepadText.

	THE ANALYSIS IS PURE, so the arm can drive it with no widget and no frame:
	one token is one frame (roll_notation's grammar), a line's gutter is the
	frame its first token lands on, and a line that fails to parse gets a
	squiggle carrying parsePattern's own error.
*/
namespace roll {
namespace notepad {

bool setAndAnalyze(const std::string& text, int& totalFrames, int& errorLines, std::string& back);

//! One line's contribution: where it starts in frames, how many it adds, and
//! whether it parsed. `error` empty == the line is clean.
struct LineInfo
{
	u32 startFrame = 0;
	int frames = 0;
	std::string error;
};

struct Analysis
{
	std::vector<LineInfo> lines;
	int totalFrames = 0;
	int errorLines = 0;
};

//! Split on '\n', keeping empty lines (they are real editor lines and get a
//! gutter). A trailing newline does NOT add a phantom final line.
static std::vector<std::string> splitLines(const std::string& text)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : text)
	{
		if (c == '\n') { out.push_back(cur); cur.clear(); }
		else if (c != '\r') cur += c;
	}
	out.push_back(cur);
	return out;
}

/*
	Parse each line on its own, accumulating the frame offset. A blank line is
	zero frames (and clean). A line that fails contributes zero frames to the
	running offset - its author has to fix it before its frames exist, so
	counting a partial parse would put every later line's gutter wrong.
*/
Analysis analyze(const std::string& text)
{
	Analysis a;
	u32 frame = 0;
	for (const std::string& line : splitLines(text))
	{
		LineInfo li;
		li.startFrame = frame;
		// A line of only whitespace is not an error and not a frame.
		bool blank = true;
		for (char c : line)
			if (c != ' ' && c != '\t' && c != ',') { blank = false; break; }
		if (!blank)
		{
			std::vector<Cell> cells;
			std::string err;
			if (parsePattern(line, cells, err))
			{
				li.frames = (int)cells.size();
				frame += (u32)cells.size();
			}
			else
			{
				li.error = err.empty() ? "unrecognised token" : err;
				a.errorLines++;
			}
		}
		a.totalFrames += li.frames;
		a.lines.push_back(li);
	}
	return a;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "NOTEPAD SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	{
		const Analysis a = analyze("");
		claim("empty text is one line, zero frames", a.lines.size() == 1 && a.totalFrames == 0);
	}
	{
		// roll_notation: one token is one frame; 5/-/. are the neutral frame.
		const Analysis a = analyze("2 3 6");
		claim("one line's frames = its token count", a.lines.size() == 1 && a.lines[0].frames == 3);
		claim("...and total frames follows", a.totalFrames == 3);
	}
	{
		const Analysis a = analyze("2 3\n6 2 3\n");	// trailing newline
		claim("a trailing newline does not add a phantom line", a.lines.size() == 3);
		claim("line 0 starts at frame 0", a.lines[0].startFrame == 0);
		claim("line 1's gutter is the frame after line 0's tokens", a.lines[1].startFrame == 2);
		claim("the last (empty) line starts past every token", a.lines[2].startFrame == 5);
		claim("total is every token across every line", a.totalFrames == 5);
	}
	{
		const Analysis a = analyze("2 3\nZZZ\n6");
		claim("a bad line is flagged with an error", !a.lines[1].error.empty() && a.errorLines == 1);
		// THE GUTTER-INTEGRITY CLAIM: a broken line contributes 0 frames, so the
		// line after it is NOT pushed forward by a half-parse. Without this, one
		// typo silently renumbers everything below it.
		claim("a broken line contributes no frames to the lines below it",
				a.lines[2].startFrame == 2);
		claim("...and the clean lines still count", a.totalFrames == 3);
	}
	{
		const Analysis a = analyze("   \n\t,\n5");
		claim("whitespace-only lines are clean and frameless",
				a.errorLines == 0 && a.lines[0].frames == 0 && a.lines[1].frames == 0);
		claim("a neutral token IS a frame", a.lines[2].frames == 1);
	}

	NOTICE_LOG(RENDERER, "NOTEPAD SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The panel.
// ---------------------------------------------------------------------------------------

static bool notepadOpen = false;

static TextEditor& editor()
{
	static TextEditor ed;
	static bool init = false;
	if (!init)
	{
		init = true;
		ed.SetPalette(TextEditor::PaletteId::Dark);
		// Plain text: our notation is NOT PPAD, so his Ppad language def would
		// mis-colour it. Correct gutter + squiggles matter more than colour; a
		// notation-specific language def is a clean later addition.
		ed.SetLanguageDefinition(TextEditor::LanguageDefinitionId::None);
		ed.SetText(cfgLoadStr("dojo", "NotepadText", ""));
	}
	return ed;
}

static void draw()
{
	TextEditor& ed = editor();
	const std::string text = ed.GetText();
	const Analysis a = analyze(text);

	// Header: what the buffer parses to, right now.
	tasTextColored(TAS_ACCENT, "%d frame%s", a.totalFrames, a.totalFrames == 1 ? "" : "s");
	ImGui::SameLine();
	if (a.errorLines > 0)
		tasTextColored(TAS_WRITE, "%d line%s with errors", a.errorLines, a.errorLines == 1 ? "" : "s");
	else
		tasTextColored(TAS_READ, "all lines parse");
	ImGui::SameLine();
	if (tasSmallButton("Save"))
	{
		cfgSaveStr("dojo", "NotepadText", text);
		NOTICE_LOG(RENDERER, "NOTEPAD: saved %d chars, %d frames", (int)text.size(), a.totalFrames);
	}
	ImGui::SameLine();
	tasTextDisabled("scratchpad - validate here, apply through the Piano Roll");

	// Gutter: the frame each line starts on. Diagnostics: a squiggle per bad
	// line, carrying parsePattern's own message. Both rebuilt from the analysis
	// every frame, which is cheap - a Notepad is tens of lines, not thousands.
	std::vector<TextEditor::GutterLabel> gutter;
	std::vector<TextEditor::Diagnostic> diags;
	gutter.reserve(a.lines.size());
	for (int i = 0; i < (int)a.lines.size(); i++)
	{
		const LineInfo& li = a.lines[i];
		TextEditor::GutterLabel g;
		g.mText = li.error.empty() ? ("f" + std::to_string(li.startFrame)) : "!";
		g.mColor = ImGui::GetColorU32(li.error.empty() ? tasCol(TAS_DIM, 0.8f) : TAS_WRITE);
		gutter.push_back(g);
		if (!li.error.empty())
		{
			TextEditor::Diagnostic d;
			d.mLine = i;
			d.mStart = 0;
			d.mEnd = 1000;		// whole line; parsePattern does not report a column
			d.mColor = ImGui::GetColorU32(TAS_WRITE);
			d.mMessage = li.error;
			d.mStyle = TextEditor::DecoStyle::Squiggle;
			diags.push_back(d);
		}
	}
	ed.SetGutterLabels(gutter);
	ed.SetDiagnostics(diags);

	ed.Render("##notepad");
}

}	// namespace notepad

void registerNotepadPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	panels::add({ "notepad", "Notepad", &notepad::notepadOpen, notepad::draw, panels::Menu,
			/*persist*/ true, /*defW*/ 480.f, /*defH*/ 420.f });
	NOTICE_LOG(RENDERER, "NOTEPAD PANEL: registered=%s open=%s",
			panels::find("notepad") != nullptr ? "yes" : "NO", notepad::notepadOpen ? "yes" : "no");
}

/*
	SURFACE TOUR HOOK - type into the real editor and read the analysis back: one
	good three-frame line, one line that cannot parse, so the verdict has both a
	gutter and a squiggle to be right about. Contract: surface_tour.h.
*/
/*
	INTENT MODULE ENTRY (intent_send.cpp, 2026-09-18): put `text` into the real editor,
	analyze it the way the panel does, hand the editor's own text back. The round-trip
	law the module asserts (parse(render(rows)) == rows) is checked by the caller.
*/
bool notepad::setAndAnalyze(const std::string& text, int& totalFrames, int& errorLines, std::string& back)
{
	TextEditor& ed = notepad::editor();
	ed.SetText(text);
	back = ed.GetText();
	const notepad::Analysis a = notepad::analyze(back);
	totalFrames = a.totalFrames;
	errorLines = a.errorLines;
	return true;
}

bool surfacetour::hooks::notepadAnalyze()
{
	TextEditor& ed = notepad::editor();
	ed.SetText("5LP _ _\nBOGUS\n");
	const notepad::Analysis a = notepad::analyze(ed.GetText());
	if (a.errorLines != 1 || a.totalFrames != 3)
	{
		surfacetour::why("errorLines=%d totalFrames=%d (want 1 and 3)", a.errorLines, a.totalFrames);
		return false;
	}
	return true;
}

}	// namespace roll
