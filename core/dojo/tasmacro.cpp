#include "tasmacro.h"
#include "log/Log.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace tas_macro
{

// letter -> (player, canon bit). Both alphabets in one table; disjoint by construction.
struct KeyDef { char ch; int player; u16 bit; };
static const KeyDef KEYS[] = {
	// P1: W S A D Z X C V B N M
	{ 'W', 0, CANON_UP }, { 'S', 0, CANON_DOWN }, { 'A', 0, CANON_LEFT }, { 'D', 0, CANON_RIGHT },
	{ 'Z', 0, CANON_LP }, { 'X', 0, CANON_HP }, { 'C', 0, CANON_A1 }, { 'V', 0, CANON_LK },
	{ 'B', 0, CANON_HK }, { 'N', 0, CANON_A2 }, { 'M', 0, CANON_START },
	// P2: T G F H U I O J K L P
	{ 'T', 1, CANON_UP }, { 'G', 1, CANON_DOWN }, { 'F', 1, CANON_LEFT }, { 'H', 1, CANON_RIGHT },
	{ 'U', 1, CANON_LP }, { 'I', 1, CANON_HP }, { 'O', 1, CANON_A1 }, { 'J', 1, CANON_LK },
	{ 'K', 1, CANON_HK }, { 'L', 1, CANON_A2 }, { 'P', 1, CANON_START },
};

// serialize order: per player, dirs then attacks then start - stable and readable
static const char P1_ORDER[] = "WSADZXCVBNM";
static const char P2_ORDER[] = "TGFHUIOJKLP";

static const KeyDef *lookup(char c)
{
	for (const KeyDef& k : KEYS)
		if (k.ch == c)
			return &k;
	return nullptr;
}

static u16 bitFor(const char *order, int idx)
{
	const KeyDef *k = lookup(order[idx]);
	return k != nullptr ? k->bit : 0;
}

bool FromText(const std::string& text, Macro& out)
{
	out = Macro{};
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		// '#' opens a comment, to end of line (user: "WC #comment" = frame +
		// note, like a programming language). Without this strip, capitals
		// inside comments would letter-map into REAL inputs. A line whose only
		// content is the comment is an ANNOTATION, not a frame (a header
		// comment block must not shift the combo); ". #note" pins a neutral
		// frame WITH a note - the dot forces the frame.
		const size_t hashAt = line.find('#');
		const bool hadComment = hashAt != std::string::npos;
		if (hadComment)
			line.resize(hashAt);
		Frame f;
		bool sawDigit = false;
		bool sawLetter = false;
		for (char c : line)
		{
			if (c == ' ' || c == '\t' || c == '.')
				continue;			// padding / the trainer's filler dot
			if (c >= '0' && c <= '9')
			{
				sawDigit = true;	// stray numeric lines in old archive files
				continue;
			}
			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			const KeyDef *k = lookup(c);
			if (k == nullptr)
			{
				out.unknownChars++;
				continue;
			}
			sawLetter = true;
			if (k->player == 0)
				f.p1 |= k->bit;
			else
				f.p2 |= k->bit;
		}
		if (sawDigit && !sawLetter)
		{
			// a line that is ONLY a number is trainer noise, not a frame
			out.ignoredLines++;
			continue;
		}
		if (hadComment && !sawLetter && !sawDigit)
		{
			bool anyMark = false;		// '.' counts - it forces a frame
			for (char c : line)
				if (c != ' ' && c != '\t')
				{
					anyMark = true;
					break;
				}
			if (!anyMark)
			{
				out.ignoredLines++;	// comment-only: an annotation, not a frame
				continue;
			}
		}
		out.frames.push_back(f);
	}
	return true;
}

bool Load(const std::string& path, Macro& out, std::string& err)
{
	std::ifstream in(path);
	if (!in)
	{
		err = "cannot open " + path;
		return false;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	return FromText(buf.str(), out);
}

std::string ToText(const Macro& m)
{
	std::string text;
	for (const Frame& f : m.frames)
	{
		for (int i = 0; P1_ORDER[i] != 0; i++)
			if (f.p1 & bitFor(P1_ORDER, i))
				text += P1_ORDER[i];
		for (int i = 0; P2_ORDER[i] != 0; i++)
			if (f.p2 & bitFor(P2_ORDER, i))
				text += P2_ORDER[i];
		text += '\n';
	}
	return text;
}

bool Save(const std::string& path, const Macro& m, std::string& err)
{
	std::ofstream outF(path, std::ios::binary);	// binary: we control the newlines
	if (!outF)
	{
		err = "cannot write " + path;
		return false;
	}
	const std::string text = ToText(m);
	outF.write(text.data(), (std::streamsize)text.size());
	return true;
}

void Probe(const std::string& path)
{
	std::string err;
	Macro m;
	if (!Load(path, m, err))
	{
		NOTICE_LOG(NETWORK, "TAS MACRO probe: LOAD FAILED - %s", err.c_str());
		return;
	}
	// shape summary
	int active = 0;
	u32 firstActive = 0, lastActive = 0;
	bool sawActive = false;
	std::map<char, int> histo;
	for (u32 i = 0; i < m.frames.size(); i++)
	{
		const Frame& f = m.frames[i];
		if (f.p1 == 0 && f.p2 == 0)
			continue;
		active++;
		if (!sawActive)
		{
			firstActive = i;
			sawActive = true;
		}
		lastActive = i;
	}
	// per-letter histogram
	for (const Frame& f : m.frames)
		for (const KeyDef& k : KEYS)
			if (((k.player == 0 ? f.p1 : f.p2) & k.bit) != 0)
				histo[k.ch]++;
	std::ostringstream hs;
	for (const auto& kv : histo)
		hs << kv.first << ":" << kv.second << " ";
	NOTICE_LOG(NETWORK, "TAS MACRO probe: %s", path.c_str());
	NOTICE_LOG(NETWORK, "TAS MACRO: %d frames (%d active, first %u last %u), %d numeric lines ignored, %d unknown chars",
			(int)m.frames.size(), active, firstActive, lastActive, m.ignoredLines, m.unknownChars);
	NOTICE_LOG(NETWORK, "TAS MACRO letters: %s", hs.str().c_str());

	// semantic round-trip: save -> reload -> compare canon sequences
	const std::string echo = path + ".echo.txt";
	if (!Save(echo, m, err))
	{
		NOTICE_LOG(NETWORK, "TAS MACRO probe: echo save failed - %s", err.c_str());
		return;
	}
	Macro m2;
	if (!Load(echo, m2, err))
	{
		NOTICE_LOG(NETWORK, "TAS MACRO probe: echo reload failed - %s", err.c_str());
		return;
	}
	bool same = m.frames.size() == m2.frames.size();
	u32 firstDiff = 0;
	if (same)
		for (u32 i = 0; i < m.frames.size(); i++)
			if (m.frames[i].p1 != m2.frames[i].p1 || m.frames[i].p2 != m2.frames[i].p2)
			{
				same = false;
				firstDiff = i;
				break;
			}
	if (same)
		NOTICE_LOG(NETWORK, "TAS MACRO: semantic round-trip OK (%d frames identical through %s)",
				(int)m.frames.size(), echo.c_str());
	else
		NOTICE_LOG(NETWORK, "TAS MACRO: ROUND-TRIP MISMATCH (sizes %d vs %d, first diff frame %u)",
				(int)m.frames.size(), (int)m2.frames.size(), firstDiff);
}

}	// namespace tas_macro
