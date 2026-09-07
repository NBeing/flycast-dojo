#pragma once
#include <cstdint>
#include <string>
#include <vector>

// VA2 / "ASCII Pad V PRO" program notation - David's 2009 combo-transcript dialect (the Variable.Atmosphere.2
// spreadsheet; spec: notation_reference/va2-notation-grammar.html, visual reference: the "V PRO Program Lab").
//
// A program is a list of STEPS separated by '/'. ONE step is ONE pad mask held for N frames - the trailing count,
// default 1 - so "lp99" is one step = 99 frames of LP. That is the programmable pad's own model (one entry = mask +
// count) and the reason this is not the notepad's frame-per-line language:
//
//   n / n65          neutral for 1 / 65 frames                6 / 6-29 / 8_51   direction, 1 frame / held 29 / 51
//   lp hp lk hk      buttons (pp = LP+HP, kk = LK+HK)         hk140 / 2hk29     button(s) held 140 / down+HK x29
//   236 / 236hk12    motion: ONE frame per leading digit, the LAST digit carries the buttons + the hold (14 frames)
//   2369-4           motion whose final direction is held 4   4+lp+lk+start37   '+' fuses atoms onto the same step
//   a1 a2 assist1    assists                                  thc / taunt / start / st
//   [lp/hp]*360      repeat group (720 frames)                lp*3              = lp/lp/lp (a repeat, never a hold)
//   Magneto-B, ZangiefB11, PsyrockA, assist-a, tag-in magneto85   an assist call (name, optional slot letter a/b/y with
//                    or without the dash, optional count) whose A1/A2 slot is UNKNOWABLE from the text (the team order
//                    at record time) -> refused unless the policy says A1 / A2, or ASSUMED A1 and flagged (paste)
//
// '/' delineates steps (David writes one long '/'-joined line). Spaces, tabs, newlines and CRs are dropped as
// separators (they never add a step: runs collapse), '#' starts a comment, quotes are dropped, everything is
// case-insensitive, ',' joins like '+'. Typos the corpus contains ("6hp23lp10", "236hkn22", "6hk-60", "kkk") are read
// by best guess and REPORTED as notices - the preview is the user's confirmation, never a silent repair.
// Single-pad: nothing in the text names a player; the importer chooses P1/P2.
namespace tas_va2
{
	using u16 = std::uint16_t;
	using u32 = std::uint32_t;

	// canon bits - MUST match tas_macro::CANON_* (dojo_gui.cpp static_asserts it at the import site)
	enum : u16
	{
		UP = 1 << 0, DOWN = 1 << 1, LEFT = 1 << 2, RIGHT = 1 << 3,
		LP = 1 << 4, HP = 1 << 5, LK = 1 << 6, HK = 1 << 7,
		START = 1 << 8, A1 = 1 << 9, A2 = 1 << 10,
	};

	// What to do with a named / slot-ambiguous assist call: ASK = refuse (err names the token); A1 / A2 = every such
	// token is that slot (a tag-in becomes LP+LK for A1, HP+HK for A2 - the MvC2 partner-tag chords);
	// ASSUME_A1 = the paste path: A1, but flagged on the step's own src ("(A1 assumed - check)") + a notice, so a
	// Ctrl+V never drops a combo and never guesses silently.
	enum AssistPolicy { ASSIST_ASK = 0, ASSIST_A1 = 1, ASSIST_A2 = 2, ASSIST_ASSUME_A1 = 3 };

	struct Step
	{
		std::vector<u16> frames;	// fully expanded canon masks: a motion's 1-frame prefix digits, then the held tail
		std::string src;			// the step's source text ("" for the repeated copies of a repeat group)
	};

	struct Result
	{
		std::vector<Step> steps;
		std::vector<std::string> notices;	// non-fatal things the user should see before importing (best-guess reads,
											// policy resolutions, collapsed empty steps, repeat expansions)
		std::string errSrc;					// on failure: the step text the error is about (display form, lowercase) - the
											// notepad lint puts its squiggle on it
		u32 totalFrames() const;
	};

	// Parse a whole program. false = err names the offending step/token and out.steps is empty.
	bool Parse(const std::string& text, AssistPolicy policy, Result& out, std::string& err);

	// The steps flattened to per-frame masks, in order.
	std::vector<u16> Expand(const Result& r);

	// Cheap "is this va2 at all?" for the Notepad's Ctrl+V auto-convert: true when the text (comments stripped) has a
	// step separator / repeat / hold character ("/ [ ] * - _") or a letter glued to a digit ("2lp14", "n65", "a1").
	// CE letters, a lone "N", dots and the trainer's stray numeric lines all say false.
	bool LooksLike(const std::string& text);

	// The other direction: per-frame masks -> program text. Identical consecutive frames become one step with its count
	// ("2lp14"), >= 2 single-frame direction taps before a directed step fold into a motion ("236hk12"), a held bare
	// direction is "9_30", the count rides the last button word ("4lp+lk+start37"), assists come last ("+a1"), an
	// assist-only step repeats with *N ("6+a1*11"). EXACT: Expand(Parse(Fold(f))) == f (SOCD pairs cancel to neutral
	// on that axis - the only lossy case). stepsPerLine > 0 breaks the line every N steps (newlines are separators).
	std::string Fold(const std::vector<u16>& frames, int stepsPerLine = 0);

	// one step (mask held count frames) in the same spelling Fold uses ("3hp10", "9_30", "n", "6+a1*11")
	std::string StepText(u16 mask, u32 count = 1);

	// Built-in check of the grammar against the spec's worked examples (+ the Program Lab's "Combo I1" preset).
	// Returns "" when everything passes, else one line per failure. Headless: no emulator, no GUI.
	std::string SelfTest();
}
