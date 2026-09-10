#include "roll_notation.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cctype>

namespace roll
{

namespace {

char lower(char c) { return (char)std::tolower((unsigned char)c); }

bool sameLabel(const char *label, const std::string& s, size_t at, size_t len)
{
	size_t i = 0;
	for (; i < len && label[i] != 0; i++)
		if (lower(label[i]) != lower(s[at + i]))
			return false;
	return i == len && label[i] == 0;
}

//! The numpad, derived from whichever four bits the profile calls directions.
//! 5 is neutral; 0 is not a direction and is rejected by the caller.
Cell numpad(int digit)
{
	const Profile& p = profile();
	switch (digit)
	{
	case 1: return p.down | p.left;
	case 2: return p.down;
	case 3: return p.down | p.right;
	case 4: return p.left;
	case 5: return 0;
	case 6: return p.right;
	case 7: return p.up | p.left;
	case 8: return p.up;
	case 9: return p.up | p.right;
	default: return 0;
	}
}

}	// namespace

bool parsePattern(const std::string& text, std::vector<Cell>& out, std::string& err)
{
	out.clear();
	err.clear();
	size_t i = 0;
	while (i < text.size())
	{
		if (std::isspace((unsigned char)text[i]) || text[i] == ',')
		{
			i++;
			continue;
		}
		const size_t tokStart = i;
		Cell cell = 0;
		bool any = false;

		if (text[i] == '_')
		{
			// A HOLD, not a repeat count: it copies the previous frame, so a
			// held button and a re-press are different things in the text the
			// way they are different things to the game.
			if (out.empty())
			{
				err = "'_' has no previous frame to hold";
				return false;
			}
			out.push_back(out.back());
			i++;
			continue;
		}
		if (text[i] == '-' || text[i] == '.')
		{
			out.push_back(0);
			i++;
			continue;
		}
		// EVERY DIGIT IN A TOKEN IS ITS OWN FRAME, and the names attach to the
		// LAST of them. "236LP" is three frames with the button on the third,
		// which is how anyone who plays this game reads it.
		//
		// `[MEASURED 2026-09-10]` the first grammar here said one token was one
		// frame full stop, and its own self-test contradicted it on the first
		// interesting case - "236LP" was written expecting a single
		// down-forward frame. The notation is older than the parser and wins.
		std::vector<int> digits;
		while (i < text.size() && std::isdigit((unsigned char)text[i]))
		{
			const int d = text[i] - '0';
			if (d == 0)
			{
				err = "0 is not a direction; 5 is neutral";
				return false;
			}
			digits.push_back(d);
			any = true;
			i++;
		}
		for (size_t k = 0; k + 1 < digits.size(); k++)
			out.push_back(numpad(digits[k]));	// the motion, one frame each
		if (!digits.empty())
			cell |= numpad(digits.back());
		// Then zero or more names, longest match first so "HP" never reads as
		// an "H" that does not exist followed by a "P".
		while (i < text.size() && !std::isspace((unsigned char)text[i]) && text[i] != ',')
		{
			const Profile& p = profile();
			int best = -1;
			size_t bestLen = 0;
			for (int c = 0; c < p.count; c++)
			{
				const size_t len = std::string(p.cols[c].label).size();
				if (len <= bestLen || i + len > text.size())
					continue;
				if (sameLabel(p.cols[c].label, text, i, len))
				{
					best = c;
					bestLen = len;
				}
			}
			if (best < 0)
			{
				err = "unknown input '" + text.substr(tokStart,
						text.find_first_of(" \t\r\n,", tokStart) - tokStart) + "'";
				return false;
			}
			cell |= p.cols[best].canon;
			any = true;
			i += bestLen;
		}
		if (!any)
		{
			err = "empty token";
			return false;
		}
		out.push_back(cell);
	}
	return true;
}

std::string renderCell(Cell c)
{
	const Profile& p = profile();
	if (c == 0)
		return "5";
	// The direction as a numpad digit first, then the buttons in the profile's
	// own order - which is what makes this the inverse of the parser rather
	// than a second opinion about what a cell means.
	std::string out;
	for (int d = 1; d <= 9; d++)
		if (d != 5 && (c & p.dirs) == numpad(d) && numpad(d) != 0)
		{
			out += (char)('0' + d);
			break;
		}
	for (int i = 0; i < p.count; i++)
		if ((p.cols[i].canon & p.dirs) == 0 && (c & p.cols[i].canon) != 0)
			out += p.cols[i].label;
	return out.empty() ? "5" : out;
}

void notationSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLNOTATION SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& p = profile();
	std::vector<Cell> v;
	std::string err;

	claim("an empty pattern parses to nothing", parsePattern("", v, err) && v.empty());

	claim("one token is one frame", parsePattern("2 3 6", v, err) && v.size() == 3);
	claim("...and the numpad comes from the profile's own directions",
			v[0] == p.down && v[1] == (p.down | p.right) && v[2] == p.right);

	claim("5, - and . are all the neutral frame",
			parsePattern("5 - .", v, err) && v.size() == 3
			&& v[0] == 0 && v[1] == 0 && v[2] == 0);

	// A MOTION IS FRAMES, WITH THE BUTTON ON THE LAST ONE - the reading anyone
	// who plays the game already has, and the one the first grammar here got
	// wrong.
	claim("a motion is one frame per digit", parsePattern("236LPHP", v, err) && v.size() == 3);
	{
		Cell want = p.right;
		for (int i = 0; i < p.count; i++)
			if (std::string(p.cols[i].label) == "LP" || std::string(p.cols[i].label) == "HP")
				want |= p.cols[i].canon;
		claim("...with every button on the LAST frame and none on the others",
				v.size() == 3 && v[0] == p.down && v[1] == (p.down | p.right) && v[2] == want);
	}
	claim("a lone direction is still one frame", parsePattern("6", v, err) && v.size() == 1);

	claim("names are case-insensitive", parsePattern("lp", v, err) && parsePattern("LP", v, err));

	// LONGEST MATCH. With labels LP and HP present, a parser matching shortest
	// first would read "HP" as an unknown "H".
	claim("the longest label wins", parsePattern("HP", v, err) && v.size() == 1);

	claim("_ holds the previous frame",
			parsePattern("2LK _ _", v, err) && v.size() == 3 && v[0] == v[1] && v[1] == v[2]);
	claim("_ with no previous frame is an ERROR, not a neutral",
			!parsePattern("_ 2", v, err) && !err.empty());

	// A BAD TOKEN STOPS THE PARSE. Dropping it silently would write a pattern
	// the user did not type, and they would find out at the movie.
	claim("an unknown input is refused and named",
			!parsePattern("2 ZZ 6", v, err) && err.find("ZZ") != std::string::npos);
	claim("0 is refused, because 5 is the neutral", !parsePattern("0", v, err));

	claim("commas separate as well as spaces",
			parsePattern("2,3,6", v, err) && v.size() == 3);

	// ---- the round trip ----
	{
		std::vector<Cell> a, b;
		std::string e;
		const char *src = "2 236LP 5 4HK";
		parsePattern(src, a, e);
		std::string txt;
		for (Cell c : a) txt += renderCell(c) + " ";
		parsePattern(txt, b, e);
		// SIX frames, not four tokens: 236LP is three of them. Counting tokens
		// here would be the old grammar sneaking back in through the test.
		claim("a pattern survives render and re-parse", a == b && a.size() == 6);
	}

	NOTICE_LOG(RENDERER, "ROLLNOTATION SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
