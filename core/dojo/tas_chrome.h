/*
	The TAS colour language - one canonical, reduced palette.

	PORTED VERBATIM from David's fork (`core/dojo/dojo_gui.cpp:133-165`), where
	it is a file-static block inside a 21,877-line translation unit. It becomes
	a header here because it is the substrate every studio panel reads from, and
	leaving it file-static is precisely the coupling the port is meant to avoid:
	the second panel to need TAS_DIM would either include a .cpp or grow its own
	copy of the numbers.

	`[SOURCE]` the values, the semantics and the comments are his. What changed
	is where they live and that they are `constexpr`-shaped rather than mutable
	file statics. Nothing here reads the emulator, so it has no dependencies
	beyond ImGui - which is why it ports first and alone.

	FADED VARIANTS COME FROM tasCol(), NEVER FROM HAND-SPLATTED NUMBERS. That is
	his rule and it is the reason the palette is worth having as a language
	rather than a list: a row tint that is "TAS_ACCENT at 0.25" stays correct
	when the accent changes.
*/
#pragma once
#include "imgui.h"

//  TAS COLOR LANGUAGE - one canonical, reduced palette (see the "TAS Color Language" reference).
//  10 semantic tokens + 3 chrome. Every TAS window reads from these; raw ImVec4 color literals
//  are being retired onto them. Faded variants come from tasCol(token, alpha), never by hand-
//  splatting the numbers. Values tuned for the dark grid ground (#16161B).
// ============================================================================================
inline const ImVec4 TAS_ACCENT (0.561f, 0.659f, 1.000f, 1.f);	// section headers, selection, highlight
inline const ImVec4 TAS_READ   (0.373f, 0.847f, 0.451f, 1.f);	// READ session / replay / bookmark / ok
inline const ImVec4 TAS_WRITE  (0.859f, 0.227f, 0.180f, 1.f);	// WRITE session / Recording / destructive
inline const ImVec4 TAS_PROTECT(0.290f, 0.549f, 0.941f, 1.f);	// blue - legacy protected tint; today only the full-rate auto-fire color
inline const ImVec4 TAS_ACTIVE (1.000f, 0.820f, 0.251f, 1.f);	// playhead / current frame
inline const ImVec4 TAS_STAGED (1.000f, 0.580f, 0.149f, 1.f);	// staging & queues (Input Sender queue, staged macro)
inline const ImVec4 TAS_P1     (0.373f, 0.780f, 0.961f, 1.f);	// player 1 (cyan-leaning, != accent)
inline const ImVec4 TAS_P2     (1.000f, 0.580f, 0.600f, 1.f);	// player 2 (pink-leaning, != write red)
inline const ImVec4 TAS_DIM    (0.522f, 0.522f, 0.561f, 1.f);	// disabled / secondary text / empty
inline const ImVec4 TAS_TEXT   (0.918f, 0.922f, 0.941f, 1.f);	// primary emphasis text
inline const ImVec4 TAS_BG        (0.086f, 0.086f, 0.106f, 1.f);	// chrome: deepest ground / grid
inline const ImVec4 TAS_PANEL     (0.118f, 0.118f, 0.153f, 1.f);	// chrome: window / row
inline const ImVec4 TAS_PANEL_ALT (0.165f, 0.157f, 0.212f, 1.f);	// chrome: alt-row (the purple)
// Faded variants (row tints, washes) come from here, not hand-splatted numbers.
inline ImVec4 tasCol(const ImVec4 &c, float a) { return ImVec4(c.x, c.y, c.z, a); }
inline ImVec4 tasLit(const ImVec4 &c) { return ImVec4(c.x * 1.18f, c.y * 1.18f, c.z * 1.18f, c.w); }	// button hover shade
inline ImVec4 tasDrk(const ImVec4 &c) { return ImVec4(c.x * 0.78f, c.y * 0.78f, c.z * 0.78f, c.w); }	// button active shade
// Driver-model tokens (red/green, tone = scope; NO blue). BRIGHT = session / R-key level,
// DARK+desaturated = PIANO ROLL sub-state. Hue: green = read/safe, red = write/hot.
inline const ImVec4 TAS_WATCH      = tasLit(TAS_READ);	// bright green - WATCH (session read-only)
inline const ImVec4 TAS_READWRITE = ImVec4(0.960f, 0.553f, 0.192f, 1.f);	// orange - READ-WRITE mode (author; a signal stomps the active cell)
// Legacy names kept as aliases so existing call sites migrate for free - and pick up the new
// P1/P2 shifts + the bookmark->READ unification at once. New code uses the tokens above.
inline const ImVec4 TAS_MODULE_COL   = TAS_ACCENT;	// section headers
inline const ImVec4 TAS_SELECT_COL   = TAS_ACCENT;	// selection tint (was == MODULE by value)
inline const ImVec4 TAS_BOOKMARK_COL = TAS_READ;	// bookmarks fold onto the one green
inline const ImVec4 TAS_ACTIVE_COL   = TAS_ACTIVE;	// playhead gold
