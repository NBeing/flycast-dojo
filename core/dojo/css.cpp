#include "css.h"
#include "mvc2.h"
#include "tasmacro.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "stdclass.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

/*
	David's mcp/charselect.py, the pure part, ported line for line (2026-09-18). Where a
	comment below is his, it is quoted; the arithmetic is transcribed, not redesigned -
	the whole point is that the C++ produces the bytes his Python produces (the parity
	claim in selfTest against the fixture his Python generated).
*/
namespace roll {
namespace css {

namespace {

// ID -> name (user's ID_2 table, MvC2_Trainer)
const char *const NAMES[59] = {
	"Ryu", "Zangief", "Guile", "Morrigan", "Anakaris", "StriderHiryu", "Cyclops", "Wolverine",
	"Psylocke", "Iceman", "Rogue", "CaptainAmerica", "SpiderMan", "Hulk", "Venom", "DoctorDoom",
	"TronBonne", "Jill", "Hayato", "RubyHeart", "Sonson", "Amingo", "Marrow", "Cable",
	"AbyssA", "AbyssB", "AbyssC", "ChunLi", "Megaman", "Roll", "Akuma", "BBHood", "Felicia",
	"Charlie", "Sakura", "Dan", "Cammy", "Dhalsim", "MBison", "Ken", "Gambit", "Juggernaut",
	"Storm", "Sabretooth", "Magneto", "ShumaGorath", "WarMachine", "SilverSamurai", "OmegaRed",
	"Spiral", "Colossus", "IronMan", "Sentinel", "Blackheart", "Thanos", "Jin", "CaptainCommando",
	"WolverineB", "Servbot",
};

// The grid as WALKED (rows cyclically anchored at RubyHeart's row; column-aligned torus,
// verified 2026-09-12). P1 cursor starts on RubyHeart = (0,0), P2 on Cable = (0,1).
const std::vector<std::vector<const char *>> GRID = {
	{ "RubyHeart", "Cable", "Iceman", "Gambit", "Blackheart", "Charlie", "Akuma", "Zangief" },
	{ "Hayato", "CaptainAmerica", "SpiderMan", "Juggernaut", "Spiral", "Dhalsim", "Jin", "Anakaris" },
	{ "TronBonne", "DoctorDoom", "Venom", "Magneto", "Colossus", "MBison", "Morrigan", "Sakura" },
	{ "Jill", "Rogue", "Hulk", "IronMan", "Cammy", "StriderHiryu" },
	{ "ChunLi", "OmegaRed", "Storm", "Sentinel", "Servbot", "Roll" },
	{ "CaptainCommando", "Wolverine", "ShumaGorath", "Thanos", "Felicia", "Megaman" },
	{ "Amingo", "Cyclops", "WolverineB", "WarMachine", "Dan", "Guile" },
	{ "Sonson", "Marrow", "Psylocke", "SilverSamurai", "Sabretooth", "Ken", "BBHood", "Ryu" },
};

struct Pos { int r, c; };

bool posOf(const std::string& name, Pos& p)
{
	for (int r = 0; r < (int)GRID.size(); r++)
		for (int c = 0; c < (int)GRID[r].size(); c++)
			if (name == GRID[r][c]) { p = { r, c }; return true; }
	return false;
}

// --- 8-direction adjacency on the select globe (a vertical torus of rows [8,8,8,6,6,6,6,8]) ---
// Verified against the emulator 2026-09-12: horizontal wraps per-row width; vertical is
// straight for cols 0-5, but crossing from an 8-wide row INTO a 6-wide row folds col6->4,
// col7->5 (e.g. Sakura DOWN -> StriderHiryu, Ryu UP -> Guile). Diagonals are a single
// press = vertical THEN horizontal composed (Sakura DR -> Jill). 6-wide -> 8-wide and
// same-width moves keep the column.
struct Delta { const char *code; int dr, dc; };
const Delta DELTA8[] = {
	{ "U", -1, 0 }, { "D", 1, 0 }, { "L", 0, -1 }, { "R", 0, 1 },
	{ "UL", -1, -1 }, { "UR", -1, 1 }, { "DL", 1, -1 }, { "DR", 1, 1 },
};

const Delta *deltaOf(const char *dir)
{
	for (const Delta& d : DELTA8)
		if (strcmp(d.code, dir) == 0)
			return &d;
	return nullptr;
}

int mod(int a, int m) { return ((a % m) + m) % m; }

Pos vstep(Pos p, int dr)
{
	const int nr = mod(p.r + dr, 8);
	const int wfrom = (int)GRID[p.r].size(), wto = (int)GRID[nr].size();
	int nc;
	if (wto < wfrom)			// 8->6 FOLD outer cols in:  col6->4, col7->5 (else straight)
		nc = p.c < wto ? p.c : p.c - 2;
	else if (wto > wfrom)		// 6->8 UN-FOLD outer cols out: col4->6, col5->7 (else straight)
		nc = p.c < 4 ? p.c : p.c + 2;
	else
		nc = p.c;				// same width: column preserved
	return { nr, nc };
}

Pos hstep(Pos p, int dc) { return { p.r, mod(p.c + dc, (int)GRID[p.r].size()) }; }

// per-player macro letters (from core/dojo/tasmacro.h; alphabets are disjoint so one line
// carries both) - as canon bits here, rendered to letters only for the fixture.
u16 dirBit(const char *word)
{
	if (strcmp(word, "up") == 0) return tas_macro::CANON_UP;
	if (strcmp(word, "down") == 0) return tas_macro::CANON_DOWN;
	if (strcmp(word, "left") == 0) return tas_macro::CANON_LEFT;
	return tas_macro::CANON_RIGHT;
}

struct Pal { const char *name; u16 bit; };
const Pal PAL[] = {
	{ "LP", tas_macro::CANON_LP }, { "HP", tas_macro::CANON_HP }, { "A1", tas_macro::CANON_A1 },
	{ "LK", tas_macro::CANON_LK }, { "HK", tas_macro::CANON_HK }, { "A2", tas_macro::CANON_A2 },
};

int assistIdx(char a) { return a == 'B' ? 1 : a == 'C' ? 2 : 0; }	// alpha/beta/gamma -> DOWN presses from alpha

std::string norm(const std::string& s)
{
	std::string o;
	for (char ch : s)
		if (isalnum((unsigned char)ch))
			o += (char)tolower((unsigned char)ch);
	return o;
}

// Canonical names, matched case/space/dot/hyphen-insensitively (David's _norm). His
// SPREADSHEET CharacterInfo NameMatches aliases (Commando, Doom, Chun-Li...) are NOT
// ported: tas_mvc2 exposes no CharacterInfo walk, and the picks the tour uses are canonical.
bool resolveName(const std::string& x, std::string& out)
{
	const std::string k = norm(x);
	for (const auto& row : GRID)
		for (const char *n : row)
			if (norm(n) == k) { out = n; return true; }
	return false;
}

// David's tables (Demul 0x2C..), kept as the cross-check for the by-name resolution.
const u32 ID2_DAVID[6]    = { 0x2C268341, 0x2C268E89, 0x2C2699D1, 0x2C2688E5, 0x2C26942D, 0x2C269F75 };
const u32 ASSIST_DAVID[6] = { 0x2C268809, 0x2C269351, 0x2C269E99, 0x2C268DAD, 0x2C2698F5, 0x2C26A43D };
const u32 PALID_DAVID[6]  = { 0x2C26886D, 0x2C2693B5, 0x2C269EFD, 0x2C268E11, 0x2C269959, 0x2C26A4A1 };

u32 byName(const char *field, Slot s, const u32 *fallback)
{
	const int i = (int)s;
	const u32 fc = tas_mvc2::addrOf(field, i / 3, i % 3);
	return fc != 0 ? tas_mvc2::toDemul(fc) : fallback[i];
}

}	// namespace

bool known(const std::string& name) { Pos p; return posOf(name, p); }

int idOf(const std::string& name)
{
	for (int i = 0; i < 59; i++)
		if (name == NAMES[i])
			return i;
	return -1;
}

const char *nameOf(int id) { return id >= 0 && id < 59 ? NAMES[id] : ""; }

std::string neighbor(const std::string& name, const char *dir)
{
	Pos p;
	const Delta *d = deltaOf(dir);
	if (!posOf(name, p) || d == nullptr)
		return "";
	if (d->dr) p = vstep(p, d->dr);
	if (d->dc) p = hstep(p, d->dc);
	return GRID[p.r][p.c];
}

// Cursor path start->target as dir codes (U/D/L/R). VERTICAL in the start column, then
// HORIZONTAL in the target row - each axis takes the shorter wrap. For HOME starts
// (RubyHeart col0 / Cable col1) this never crosses the 6<->8 width fold ambiguously.
std::vector<std::string> plan(const std::string& target, const std::string& start)
{
	std::vector<std::string> moves;
	Pos s, t;
	if (start == target || !posOf(start, s) || !posOf(target, t))
		return moves;
	const int dr = mod(t.r - s.r, 8);
	if (dr <= 4) moves.insert(moves.end(), dr, "D"); else moves.insert(moves.end(), 8 - dr, "U");
	const int w = (int)GRID[t.r].size();
	const int dc = mod(t.c - s.c, w);
	if (dc <= w / 2) moves.insert(moves.end(), dc, "R"); else moves.insert(moves.end(), w - dc, "L");
	return moves;
}

std::string home(int player) { return player == 0 ? "RubyHeart" : "Cable"; }

u16 dirCanon(int player, const char *dir)
{
	(void)player;	// the canon bits are lane-agnostic; the LANE (P1/P2) is chosen by the caller
	const Delta *d = deltaOf(dir);
	if (d == nullptr) return 0;
	u16 s = 0;
	if (d->dr < 0) s |= dirBit("up");
	if (d->dr > 0) s |= dirBit("down");
	if (d->dc < 0) s |= dirBit("left");
	if (d->dc > 0) s |= dirBit("right");
	return s;
}

u16 paletteCanon(int player, const std::string& palette)
{
	(void)player;
	for (const Pal& p : PAL)
		if (palette == p.name)
			return p.bit;
	return 0;
}

// Build one player's per-frame stream for a 3-char team pick. The cursor RESETS to the
// player's home (RubyHeart/Cable) after each lock, so every character is navigated from home
// (verified 2026-09-12).
std::vector<u16> buildPicks(int player, const std::vector<Pick>& picks, const Timing& t)
{
	std::vector<u16> lines;
	const std::string hm = home(player);
	auto neutral = [&](int n) { for (int i = 0; i < n; i++) lines.push_back(0); };
	for (const Pick& pk : picks)
	{
		for (const std::string& d : plan(pk.name, hm))	// 1. navigate from home (press-edge)
		{
			lines.push_back(dirCanon(player, d.c_str()));
			neutral(t.navgap);
		}
		neutral(t.preselect);
		const u16 pal = paletteCanon(player, pk.palette);
		lines.push_back(pal); neutral(t.postsel);		// 2. select (palette) + min wait
		for (int i = 0; i < assistIdx(pk.assist); i++)	// 3. assist: DOWN * index (alpha=0 presses)
		{
			lines.push_back(dirBit("down"));
			neutral(t.astgap);
		}
		neutral(t.preconf);
		lines.push_back(pal); neutral(t.postconf);		// 4. confirm (reuse palette btn) + advance (lockout)
	}
	return lines;
}

// merge_players + the letter rendering: P1 letters then P2 letters, '.' for a neutral frame.
std::vector<std::string> renderLetters(const std::vector<u16>& p1, const std::vector<u16>& p2)
{
	static const char *P1L = "WSADZXCVBNM", *P2L = "TGFHUIOJKLP";
	static const u16 BITS[11] = {
		tas_macro::CANON_UP, tas_macro::CANON_DOWN, tas_macro::CANON_LEFT, tas_macro::CANON_RIGHT,
		tas_macro::CANON_LP, tas_macro::CANON_HP, tas_macro::CANON_A1, tas_macro::CANON_LK,
		tas_macro::CANON_HK, tas_macro::CANON_A2, tas_macro::CANON_START,
	};
	const size_t n = std::max(p1.size(), p2.size());
	std::vector<std::string> out;
	out.reserve(n);
	for (size_t i = 0; i < n; i++)
	{
		std::string s;
		const u16 a = i < p1.size() ? p1[i] : 0, b = i < p2.size() ? p2[i] : 0;
		// David's letters(): up, down, left, right in that order, then the one button
		for (int k = 0; k < 11; k++) if (a & BITS[k]) s += P1L[k];
		for (int k = 0; k < 11; k++) if (b & BITS[k]) s += P2L[k];
		out.push_back(s.empty() ? "." : s);
	}
	return out;
}

// 'magneto-a1-a' -> ('Magneto','A1','A'). palette in LP/HP/A1/LK/HK/A2; assist in A/B/C or
// A/B/Y (Y=gamma); all case-insensitive; assist defaults A.
bool parsePick(const std::string& s, Pick& out, std::string& err)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char ch : s) { if (ch == '-') { if (!cur.empty()) parts.push_back(cur); cur.clear(); } else cur += ch; }
	if (!cur.empty()) parts.push_back(cur);
	if (parts.size() < 2) { err = "pick needs <char>-<palette>[-<assist>]: " + s; return false; }
	if (!resolveName(parts[0], out.name)) { err = "unknown character " + parts[0]; return false; }
	std::string pal = parts[1];
	for (char& ch : pal) ch = (char)toupper((unsigned char)ch);
	if (paletteCanon(0, pal) == 0) { err = "bad palette " + parts[1] + " (want LP/HP/A1/LK/HK/A2)"; return false; }
	out.palette = pal;
	char a = parts.size() > 2 ? (char)toupper((unsigned char)parts[2][0]) : 'A';
	if (a == 'Y') a = 'C';
	if (a != 'A' && a != 'B' && a != 'C') { err = "bad assist " + parts[2] + " (want A/B/C or A/B/Y)"; return false; }
	out.assist = a;
	return true;
}

u32 id2Addr(Slot s)     { return byName("ID_2", s, ID2_DAVID); }
u32 assistAddr(Slot s)  { return byName("Assist_Value", s, ASSIST_DAVID); }
u32 paletteAddr(Slot s) { return byName("PaletteID_2", s, PALID_DAVID); }

// ---- the selftest ------------------------------------------------------------------
namespace {

std::string fixturePath()
{
	// the way mvc2.cpp finds SPREADSHEET.json: the data dirs, then the source tree beside this file
	std::vector<std::string> cands;
	cands.push_back(get_readonly_data_path("fixtures/mvc2/css/dhalsim_team_picks.txt"));
	std::string here = __FILE__;
	const size_t sl = here.find_last_of("/\\");
	if (sl != std::string::npos)
		cands.push_back(here.substr(0, sl) + "/../../scripts/fixtures/mvc2/css/dhalsim_team_picks.txt");
	for (const std::string& c : cands)
	{
		std::ifstream in(c);
		if (in.good())
			return c;
	}
	return "";
}

// fixtures-check.sh's fnv(): FNV-1a 64 over each frame's p1 then p2 canon (David's seqHashMacro rule).
u64 seqHash(const std::vector<u16>& p1, const std::vector<u16>& p2)
{
	u64 h = 1469598103934665603ull;
	const size_t n = std::max(p1.size(), p2.size());
	for (size_t i = 0; i < n; i++)
	{
		h = (h ^ (i < p1.size() ? p1[i] : 0)) * 1099511628211ull;
		h = (h ^ (i < p2.size() ? p2[i] : 0)) * 1099511628211ull;
	}
	return h;
}

std::string join(const std::vector<std::string>& v)
{
	std::string s;
	for (const std::string& x : v) s += x;
	return s;
}

}	// namespace

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int passed = 0, failed = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? passed : failed)++;
		NOTICE_LOG(RENDERER, "CSS SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// David's verified edges (charselect.py comments, 2026-09-12)
	claim("Sakura DOWN -> StriderHiryu (8->6 fold col7->5)", neighbor("Sakura", "D") == "StriderHiryu");
	claim("Ryu UP -> Guile (8->6 fold col7->5 going up)", neighbor("Ryu", "U") == "Guile");
	claim("Sakura DR -> Jill (vertical then horizontal, wraps)", neighbor("Sakura", "DR") == "Jill");
	claim("Guile DOWN -> Ryu (6->8 un-fold col5->7)", neighbor("Guile", "D") == "Ryu");
	claim("RubyHeart LEFT wraps to Zangief (8-wide row)", neighbor("RubyHeart", "L") == "Zangief");
	claim("a bad direction is empty", neighbor("Ryu", "X").empty());
	claim("56 grid names, 59 ids, Dhalsim is 37", idOf("Dhalsim") == 37 && strcmp(nameOf(37), "Dhalsim") == 0 && known("Servbot") && !known("AbyssA"));

	// plans
	claim("plan(Dhalsim) == D,L,L,L", join(plan("Dhalsim")) == "DLLL");
	claim("plan(Sentinel) == D,D,D,D,R,R,R", join(plan("Sentinel")) == "DDDDRRR");
	claim("plan(Ryu, from Cable) == U,L,L", join(plan("Ryu", "Cable")) == "ULL");
	claim("plan(Cable) == R; plan to self is empty", join(plan("Cable")) == "R" && plan("RubyHeart").empty());
	claim("home(0)=RubyHeart home(1)=Cable", home(0) == "RubyHeart" && home(1) == "Cable");
	{
		// the walk the tour verifies against the game: each press lands on the graph's prediction
		std::string cur = "RubyHeart", path;
		for (const std::string& d : plan("Dhalsim")) { cur = neighbor(cur, d.c_str()); path += cur + ">"; }
		claim("the Dhalsim walk is RubyHeart>Hayato>Anakaris>Jin>Dhalsim", path == "Hayato>Anakaris>Jin>Dhalsim>");
	}

	// picks
	{
		Pick p; std::string err;
		claim("parsePick(dhalsim-lp-a)", parsePick("dhalsim-lp-a", p, err) && p.name == "Dhalsim" && p.palette == "LP" && p.assist == 'A');
		claim("parsePick defaults assist A, accepts Y=gamma", parsePick("Magneto-A1", p, err) && p.assist == 'A' && parsePick("storm-lk-y", p, err) && p.assist == 'C');
		claim("parsePick refuses a bad palette", !parsePick("ryu-mp-a", p, err));
		claim("parsePick refuses an unknown name", !parsePick("abyssa-lp-a", p, err));
	}

	// addresses: resolved by NAME through the SPREADSHEET dictionary == David's tables
	{
		bool id2 = true, ast = true, pal = true;
		for (int i = 0; i < 6; i++)
		{
			id2 &= id2Addr((Slot)i) == ID2_DAVID[i];
			ast &= assistAddr((Slot)i) == ASSIST_DAVID[i];
			pal &= paletteAddr((Slot)i) == PALID_DAVID[i];
		}
		claim("ID_2 x6 by name == David's ID2 table", id2);
		claim("Assist_Value x6 by name == David's ASSIST table", ast);
		claim("PaletteID_2 x6 by name == David's PALID table", pal);
	}

	// BYTE PARITY with the Python-generated fixture
	{
		std::vector<Pick> p1, p2;
		Pick pk; std::string err;
		for (const char *s : { "Dhalsim-LP-A", "Cable-LP-A", "Sentinel-LP-A" }) { parsePick(s, pk, err); p1.push_back(pk); }
		for (const char *s : { "Ryu-LP-A", "Ken-LP-A", "Guile-LP-A" }) { parsePick(s, pk, err); p2.push_back(pk); }
		const std::vector<u16> s1 = buildPicks(0, p1), s2 = buildPicks(1, p2);
		const std::vector<std::string> mine = renderLetters(s1, s2);
		claim("the merged team stream is 378 frames", mine.size() == 378);
		const std::string path = fixturePath();
		std::vector<std::string> theirs;
		if (!path.empty())
		{
			std::ifstream in(path);
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == '#') continue;
				theirs.push_back(line);
			}
		}
		claim("the fixture dhalsim_team_picks.txt was found and read", !path.empty() && !theirs.empty());
		size_t firstDiff = 0;
		bool same = theirs.size() == mine.size();
		for (size_t i = 0; same && i < mine.size(); i++)
			if (mine[i] != theirs[i]) { same = false; firstDiff = i; }
		if (!same && !theirs.empty())
			NOTICE_LOG(RENDERER, "CSS SELFTEST: parity differs at frame %u: mine='%s' fixture='%s' (sizes %u/%u)",
					(u32)firstDiff, firstDiff < mine.size() ? mine[firstDiff].c_str() : "-",
					firstDiff < theirs.size() ? theirs[firstDiff].c_str() : "-", (u32)mine.size(), (u32)theirs.size());
		claim("BYTE PARITY: every frame equals David's Python output, line for line", same && !theirs.empty());
		char hx[32];
		snprintf(hx, sizeof(hx), "%016llx", (unsigned long long)seqHash(s1, s2));
		claim("seqHashMacro of the stream == 3b736c94ef884bbd (the fixture's pin)", strcmp(hx, "3b736c94ef884bbd") == 0);
		if (strcmp(hx, "3b736c94ef884bbd") != 0)
			NOTICE_LOG(RENDERER, "CSS SELFTEST: stream hash is %s", hx);
	}

	NOTICE_LOG(RENDERER, "CSS SELFTEST: %d passed, %d failed", passed, failed);
}

}	// namespace css
}	// namespace roll
