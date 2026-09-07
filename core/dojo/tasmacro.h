#pragma once
#include "types.h"
#include <string>
#include <vector>

// PR1: the CE-trainer macro codec - the MvC2 Hitbox-View trainer's language, which is also
// the format of the user's 6-year Demul combo archive (RubyHeartCombo34_P1.txt etc.):
//
//   one line = one frame; uppercase letters = keys held that frame; blank / spaces = neutral;
//   stray numeric lines occur in old files and are ignored on read.
//
// Letters are the trainer's PHYSICAL keys, per-player, confirmed by the user 2026-08-24:
//   label:  U  D  L  R  X(LP) Y(HP) L(A1) A(LK) B(HK) R(A2) Start
//   P1   :  W  S  A  D  Z     X     C     V     B     N     M
//   P2   :  T  G  F  H  U     I     O     J     K     L     P
// The alphabets are disjoint, so ONE line carries both players unambiguously - one file per
// combo, stored next to the clip's .flyr. A trailing '.' in the trainer's key strings is a
// filler and is ignored.
//
// In memory a macro is canon bits per player per frame - the same 11-bit canonical set the
// fidelity harness uses (bits 0-3 dirs U/D/L/R, 4 LP, 5 HP, 6 LK, 7 HK, 8 Start, 9 A1,
// 10 A2). Conversion to FrameInputs (kcode bits + trigger bytes, both A1/A2 representations)
// happens at PLACEMENT time, not here.
namespace tas_macro
{
	struct Frame
	{
		u16 p1 = 0;		// canon bits
		u16 p2 = 0;
	};

	struct Macro
	{
		std::vector<Frame> frames;
		int ignoredLines = 0;		// numeric/garbage lines skipped on load
		int unknownChars = 0;		// letters outside both alphabets (counted, skipped)
	};

	// canon bit indices (match Dojo::canonFromPacket)
	enum : u16
	{
		CANON_UP = 1 << 0, CANON_DOWN = 1 << 1, CANON_LEFT = 1 << 2, CANON_RIGHT = 1 << 3,
		CANON_LP = 1 << 4, CANON_HP = 1 << 5, CANON_LK = 1 << 6, CANON_HK = 1 << 7,
		CANON_START = 1 << 8, CANON_A1 = 1 << 9, CANON_A2 = 1 << 10,
	};

	// text <-> macro, no file involved (the clipboard path uses these directly)
	bool FromText(const std::string& text, Macro& out);
	std::string ToText(const Macro& m);

	// file -> macro. Returns false only on unreadable file (err set); malformed content is
	// tolerated the way the trainer tolerated it (skipped + counted), because six-year-old
	// archive files must load.
	bool Load(const std::string& path, Macro& out, std::string& err);

	// macro -> file, canonical letter order (P1 then P2, dirs-buttons-start). Neutral frames
	// are written as empty lines. Round-trips SEMANTICALLY (parse(save(m)) == m), not
	// byte-identically with archive files (their stray spaces/numerics are not preserved).
	bool Save(const std::string& path, const Macro& m, std::string& err);

	// startup probe (dojo:MacroProbe=<file>): load, log a summary + per-letter histogram,
	// save an .echo.txt next to it, reload, compare - NOTICE-logs the verdict. Headless
	// verification of the codec against the real archive before any UI exists.
	void Probe(const std::string& path);
}
