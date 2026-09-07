#include "tastext.h"
#include "dojo.h"
#include "input/gamepad.h"
#include "log/Log.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tas_text
{

static_assert(sizeof(FrameInputs) == 12, "text codec is written against the 12-byte packet");
static constexpr size_t ENTRY_SIZE = sizeof(FrameInputs) * MAX_PLAYERS;

// ---- the one table everything drives from ----------------------------------------------------
// Column order IS this table's order: bools first, then axes (Appendix B choice - the digital
// columns are what you scan while authoring). The packet stores ~kcode: pressed bits SET, and the
// trigger DIGITAL bits ride in the same field (BTN_TRIGGER_*) while their analog bytes sit in
// triggers.l/r - both are serialized, so nothing is inferred on import.
struct BoolCol
{
	const char *name;	// LogKey name (without the "P<n> " prefix)
	char mnemonic;		// the char written when pressed; '.' when not
	u32 bit;			// bit in FrameInputs.kcode (pressed = set)
};
static const BoolCol BOOL_COLS[] = {
	{ "Up",    'U', DC_DPAD_UP },
	{ "Down",  'D', DC_DPAD_DOWN },
	{ "Left",  'L', DC_DPAD_LEFT },
	{ "Right", 'R', DC_DPAD_RIGHT },
	{ "A",     'A', DC_BTN_A },
	{ "B",     'B', DC_BTN_B },
	{ "X",     'X', DC_BTN_X },
	{ "Y",     'Y', DC_BTN_Y },
	{ "C",     'C', DC_BTN_C },
	{ "Z",     'Z', DC_BTN_Z },
	{ "Start", 'S', DC_BTN_START },
	{ "TrigL", 'l', BTN_TRIGGER_LEFT },
	{ "TrigR", 'r', BTN_TRIGGER_RIGHT },
};
static constexpr int NBOOL = (int)(sizeof(BOOL_COLS) / sizeof(BOOL_COLS[0]));

struct AxisCol
{
	const char *name;
	int offset;			// byte offset inside FrameInputs
};
static const AxisCol AXIS_COLS[] = {
	{ "AnalogX", 4 },	// u.analog.x (neutral 128)
	{ "AnalogY", 5 },	// u.analog.y (neutral 128)
	{ "LT", 10 },		// triggers.l (neutral 0)
	{ "RT", 11 },		// triggers.r (neutral 0)
};
static constexpr int NAXIS = (int)(sizeof(AXIS_COLS) / sizeof(AXIS_COLS[0]));

static u32 namedMask()
{
	u32 m = 0;
	for (const BoolCol& c : BOOL_COLS)
		m |= c.bit;
	return m;
}

// A packet the named columns can express EXACTLY: only named kcode bits, no mouse/keyboard
// payload, and nothing in the union beyond the two analog bytes. Anything else -> @raw.
static bool representable(const FrameInputs& fi)
{
	if ((fi.kcode & ~namedMask()) != 0)
		return false;
	if (fi.mouseButtons != 0 || fi.kbModifiers != 0)
		return false;
	const u8 *b = (const u8 *)&fi;
	if (b[6] != 0 || b[7] != 0 || b[8] != 0 || b[9] != 0)	// union bytes past analog x/y
		return false;
	return true;
}

static std::string logKey()
{
	std::string k = "LogKey:";
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		k += "#";
		for (const BoolCol& c : BOOL_COLS)
			k += "P" + std::to_string(p + 1) + " " + c.name + "|";
		for (const AxisCol& a : AXIS_COLS)
			k += "P" + std::to_string(p + 1) + " " + a.name + "|";
	}
	return k;
}

static std::string frameLine(const u8 *entry)
{
	std::string line = "|";
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		FrameInputs fi;
		memcpy(&fi, entry + p * sizeof(FrameInputs), sizeof(FrameInputs));
		for (const BoolCol& c : BOOL_COLS)
			line += (fi.kcode & c.bit) ? c.mnemonic : '.';
		const u8 *b = (const u8 *)&fi;
		char num[16];
		for (const AxisCol& a : AXIS_COLS)
		{
			snprintf(num, sizeof(num), "%5d,", (int)b[a.offset]);
			line += num;
		}
		line += "|";
	}
	return line;
}

static std::string rawLine(u32 frame, const std::vector<u8>& entry)
{
	std::string line = "@raw " + std::to_string(frame) + " ";
	char h[4];
	for (u8 byte : entry)
	{
		snprintf(h, sizeof(h), "%02X", byte);
		line += h;
	}
	return line;
}

bool ExportText(const std::map<u32, std::vector<u8>>& inputs, const std::string& path,
		const std::string& gameName, std::string& err)
{
	if (inputs.empty())
	{
		err = "movie is empty";
		return false;
	}
	// Frames must be contiguous from the first key: the text format is positional and a silent
	// gap would shift every later frame. (Recordings are contiguous; anything else is a bug we
	// want to SEE, not paper over.)
	const u32 first = inputs.begin()->first;
	const u32 last = inputs.rbegin()->first;
	if (last - first + 1 != (u32)inputs.size())
	{
		err = "movie frames are not contiguous (" + std::to_string(inputs.size()) + " entries spanning "
				+ std::to_string(first) + ".." + std::to_string(last) + ")";
		return false;
	}

	std::ofstream f(path, std::ios::out | std::ios::trunc | std::ios::binary);
	if (!f.good())
	{
		err = "cannot open " + path;
		return false;
	}
	f << "; flycast-dojo TAS text movie v1\n";
	f << "; game: " << gameName << "  frames: " << inputs.size() << " (" << first << ".." << last << ")\n";
	f << "; '.' = unpressed. bools then axes per player. '|' groups: P1 | P2.\n";
	f << logKey() << "\n";

	// Run-length collapse on identical consecutive entries: ' *N' repeat suffix. The in-memory
	// invariant (entry index == frame number) is untouched - expansion happens at parse.
	u32 fr = first;
	while (fr <= last)
	{
		const std::vector<u8>& entry = inputs.at(fr);
		u32 run = 1;
		while (fr + run <= last && inputs.at(fr + run) == entry)
			run++;
		bool named = entry.size() == ENTRY_SIZE;
		if (named)
			for (int p = 0; p < MAX_PLAYERS && named; p++)
			{
				FrameInputs fi;
				memcpy(&fi, entry.data() + p * sizeof(FrameInputs), sizeof(FrameInputs));
				named = representable(fi);
			}
		if (named)
		{
			f << frameLine(entry.data());
			if (run > 1)
				f << " *" << run;
			f << "\n";
		}
		else
		{
			// exact-bytes escape, with the same repeat collapse as named lines - legacy movies
			// carry constant uninitialized garbage in their dead bytes, so whole idle stretches
			// are identical raw entries.
			f << rawLine(fr, entry);
			if (run > 1)
				f << " *" << run;
			f << "\n";
		}
		fr += run;
	}
	f << "; end\n";
	return f.good();
}

// ---- import ----------------------------------------------------------------------------------

static bool parseLogKey(const std::string& line, std::vector<std::pair<int, int>>& cols, std::string& err)
{
	// Returns, in file column order: {colType, index} where colType 0 = bool (index into
	// BOOL_COLS), 1 = axis (index into AXIS_COLS). Names are validated against the tables -
	// an unknown name is an ERROR (strict, unlike bk2's silent tolerance), because a movie
	// written by a future build with extra columns must not be silently misread by this one.
	cols.clear();
	std::string body = line.substr(7);
	int group = -1;
	size_t pos = 0;
	while (pos < body.size())
	{
		if (body[pos] == '#')
		{
			group++;
			pos++;
			continue;
		}
		size_t bar = body.find('|', pos);
		if (bar == std::string::npos)
			break;
		std::string name = body.substr(pos, bar - pos);
		pos = bar + 1;
		std::string prefix = "P" + std::to_string(group + 1) + " ";
		if (name.rfind(prefix, 0) != 0)
		{
			err = "LogKey name '" + name + "' does not match its group P" + std::to_string(group + 1);
			return false;
		}
		std::string base = name.substr(prefix.size());
		bool found = false;
		for (int i = 0; i < NBOOL && !found; i++)
			if (base == BOOL_COLS[i].name)
			{
				cols.push_back({ 0, i });
				found = true;
			}
		for (int i = 0; i < NAXIS && !found; i++)
			if (base == AXIS_COLS[i].name)
			{
				cols.push_back({ 1, i });
				found = true;
			}
		if (!found)
		{
			err = "LogKey column '" + name + "' is unknown to this build";
			return false;
		}
	}
	if (group + 1 != MAX_PLAYERS)
	{
		err = "LogKey has " + std::to_string(group + 1) + " group(s), expected " + std::to_string(MAX_PLAYERS);
		return false;
	}
	return true;
}

bool ImportText(const std::string& path, std::map<u32, std::vector<u8>>& out, std::string& err)
{
	std::ifstream f(path, std::ios::in | std::ios::binary);
	if (!f.good())
	{
		err = "cannot open " + path;
		return false;
	}
	out.clear();
	std::vector<std::pair<int, int>> cols;	// file column order, from the LogKey
	bool haveKey = false;
	u32 cursor = 0;
	int lineNo = 0;
	std::string line;
	while (std::getline(f, line))
	{
		lineNo++;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		auto fail = [&](const std::string& what) {
			err = "line " + std::to_string(lineNo) + ": " + what;
			return false;
		};

		if (line.rfind("LogKey:", 0) == 0)
		{
			if (!parseLogKey(line, cols, err))
				return fail(err);
			haveKey = true;
			continue;
		}
		if (line.rfind("@raw ", 0) == 0)
		{
			std::istringstream ss(line.substr(5));
			u32 frame = 0;
			std::string hex, rep;
			u32 repeat = 1;
			ss >> frame >> hex >> rep;
			if (rep.rfind("*", 0) == 0)
			{
				long r = atol(rep.c_str() + 1);
				if (r < 1 || r > 1000000)
					return fail("bad @raw repeat count");
				repeat = (u32)r;
			}
			else if (!rep.empty())
				return fail("unexpected token after @raw hex");
			if (frame != cursor)
				return fail("@raw frame " + std::to_string(frame) + " but positional cursor is at "
						+ std::to_string(cursor));
			if (hex.size() < 2 || (hex.size() % 2) != 0)
				return fail("@raw hex payload malformed");
			std::vector<u8> entry(hex.size() / 2);
			for (size_t i = 0; i < entry.size(); i++)
			{
				unsigned v = 0;
				if (sscanf(hex.c_str() + i * 2, "%2X", &v) != 1)
					return fail("@raw hex payload malformed");
				entry[i] = (u8)v;
			}
			for (u32 i = 0; i < repeat; i++)
				out[cursor++] = entry;
			continue;
		}
		if (line.empty() || line[0] != '|')
			continue;		// comments, directives-to-come, anything else: ignored by design

		if (!haveKey)
			return fail("frame line before LogKey");

		// repeat suffix
		u32 repeat = 1;
		{
			size_t star = line.rfind(" *");
			if (star != std::string::npos && star > line.rfind('|'))
			{
				long r = atol(line.c_str() + star + 2);
				if (r < 1 || r > 1000000)
					return fail("bad repeat count");
				repeat = (u32)r;
				line = line.substr(0, star);
			}
		}

		// STRICT group walk - the pipe count must be exactly right (bk2 skips pipes and silently
		// misassigns on malformed lines; that is the one behaviour we refuse to copy).
		std::vector<u8> entry(ENTRY_SIZE, 0);
		size_t pos = 0;
		size_t col = 0;
		for (int p = 0; p < MAX_PLAYERS; p++)
		{
			if (pos >= line.size() || line[pos] != '|')
				return fail("expected '|' opening P" + std::to_string(p + 1) + " group");
			pos++;
			FrameInputs fi;
			memset(&fi, 0, sizeof(fi));
			u8 *bytes = (u8 *)&fi;
			// this player's slice of the LogKey columns, in order
			for (int i = 0; i < NBOOL + NAXIS; i++, col++)
			{
				if (col >= cols.size())
					return fail("more cells than LogKey columns");
				if (cols[col].first == 0)
				{
					if (pos >= line.size())
						return fail("line ends inside P" + std::to_string(p + 1) + " bools");
					const char ch = line[pos++];
					if (ch == '|')
						return fail("too few bool cells for P" + std::to_string(p + 1));
					if (ch != '.')
						fi.kcode |= BOOL_COLS[cols[col].second].bit;
				}
				else
				{
					size_t comma = line.find(',', pos);
					if (comma == std::string::npos)
						return fail("axis value missing ',' terminator");
					long v = atol(line.substr(pos, comma - pos).c_str());
					if (v < 0 || v > 255)
						return fail("axis value out of range 0..255");
					bytes[AXIS_COLS[cols[col].second].offset] = (u8)v;
					pos = comma + 1;
				}
			}
			memcpy(entry.data() + p * sizeof(FrameInputs), &fi, sizeof(fi));
		}
		if (pos >= line.size() || line[pos] != '|')
			return fail("expected closing '|'");
		if (pos + 1 != line.size())
			return fail("trailing characters after closing '|'");

		for (u32 i = 0; i < repeat; i++)
		{
			out[cursor] = entry;
			cursor++;
		}
	}
	if (out.empty())
	{
		err = "no frames parsed";
		return false;
	}
	return true;
}

void RoundTripSelfTest(const std::map<u32, std::vector<u8>>& inputs, const std::string& clipDir,
		const std::string& gameName)
{
	const std::string path = clipDir + "/movie.tas.txt";
	std::string err;
	if (!ExportText(inputs, path, gameName, err))
	{
		NOTICE_LOG(NETWORK, "TAS TEXT: export FAILED - %s", err.c_str());
		return;
	}
	std::map<u32, std::vector<u8>> back;
	if (!ImportText(path, back, err))
	{
		NOTICE_LOG(NETWORK, "TAS TEXT: import FAILED - %s", err.c_str());
		return;
	}
	if (back.size() != inputs.size())
	{
		NOTICE_LOG(NETWORK, "TAS TEXT: round-trip FAILED - frame count %u vs %u",
				(u32)back.size(), (u32)inputs.size());
		return;
	}
	for (const auto& kv : inputs)
	{
		auto it = back.find(kv.first);
		if (it == back.end() || it->second != kv.second)
		{
			// name the first differing byte - the debugging handle
			std::string detail = "entry missing";
			if (it != back.end())
				for (size_t i = 0; i < kv.second.size() && i < it->second.size(); i++)
					if (kv.second[i] != it->second[i])
					{
						char b[64];
						snprintf(b, sizeof(b), "byte %u: %02X vs %02X", (u32)i,
								kv.second[i], it->second[i]);
						detail = b;
						break;
					}
			NOTICE_LOG(NETWORK, "TAS TEXT: round-trip FAILED at frame %u (%s)", kv.first, detail.c_str());
			return;
		}
	}
	NOTICE_LOG(NETWORK, "TAS TEXT: round-trip OK - %u frames byte-identical through %s",
			(u32)inputs.size(), path.c_str());

	// Edit check: if a hand-edited copy exists next to the export, import it and report exactly
	// which frames differ from the live movie. This proves import-on-edited-text WITHOUT touching
	// the live movie - applying the edit is T6's funnel, not the codec's business.
	const std::string editPath = clipDir + "/movie.edit.tas.txt";
	std::ifstream probe(editPath);
	if (probe.good())
	{
		probe.close();
		std::map<u32, std::vector<u8>> edited;
		if (!ImportText(editPath, edited, err))
		{
			NOTICE_LOG(NETWORK, "TAS TEXT EDIT: import FAILED - %s", err.c_str());
			return;
		}
		u32 differing = 0;
		u32 firstDiff = 0;
		bool haveFirst = false;
		for (const auto& kv : inputs)
		{
			auto it = edited.find(kv.first);
			if (it == edited.end() || it->second != kv.second)
			{
				differing++;
				if (!haveFirst)
				{
					haveFirst = true;
					firstDiff = kv.first;
				}
			}
		}
		differing += (u32)(edited.size() > inputs.size() ? edited.size() - inputs.size() : 0);
		if (differing == 0)
			NOTICE_LOG(NETWORK, "TAS TEXT EDIT: %s is identical to the live movie", editPath.c_str());
		else
			NOTICE_LOG(NETWORK, "TAS TEXT EDIT: %u frame(s) differ, first at frame %u (%u frames total)",
					differing, firstDiff, (u32)edited.size());
	}
}

}
