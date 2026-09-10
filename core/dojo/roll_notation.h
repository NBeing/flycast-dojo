#pragma once
#include "roll_profile.h"
#include <string>
#include <vector>

/*
	TEXT TO A SEQUENCE OF CELLS - the third profile chokepoint.

	`[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md §1 named three places where
	a game gets into the roll: the column table, the canon/packet mapping, and
	THE NOTATION. The fork has FIVE dialects of the same eleven bits - a numpad
	parser, a CE-letter parser with a hardcoded keyboard alphabet
	("P1: WSADZXCVBNM, P2: TGFHUIOJKLP"), and three renderers - and its numpad
	parser also encodes reading conventions like "MP" meaning LP.

	This is ONE dialect, and it is built from the PROFILE rather than from a
	table of its own: buttons are matched against the profile's own column
	labels, and the numpad is derived from the four direction bits the profile
	names. A second game with different labels needs no change here; a different
	control layout needs only its four directions.

	THE GRAMMAR, which is small on purpose:

	    tokens are separated by whitespace or commas
	    ONE TOKEN IS ONE FRAME - this is a piano roll, not a combo notation,
	      and a token that expanded to several frames would make the text and
	      the rows disagree about length
	    a token is an optional numpad digit followed by zero or more names
	    5, - and . are the neutral frame
	    names are the profile's labels, matched case-insensitively
	    _ repeats the previous frame, so "236LP _ _" is a three-frame hold

	Deliberately NOT supported: charge notation, motion shorthand that spans
	frames, and the CE alphabet. Each would be a second way to say the same
	thing, which is how five dialects happen.
*/
namespace roll
{

//! Parse `text` into one cell per frame. False on the first bad token, with
//! `err` naming it - a pattern that silently dropped a token would write
//! something the user did not type.
bool parsePattern(const std::string& text, std::vector<Cell>& out, std::string& err);

//! A cell back to a token, in the profile's own labels. Round-trips through
//! parsePattern; the whitespace does not survive and does not matter.
std::string renderCell(Cell c);

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void notationSelfTest();

}	// namespace roll
