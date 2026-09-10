#include "roll_library.h"
#include "tasmacro.h"
#include "stdclass.h"
#include "cfg/cfg.h"
#include "log/Log.h"
#include "deps/filesystem.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace roll
{

size_t Sequence::length() const
{
	size_t n = 0;
	for (const auto& l : lanes)
		n = std::max(n, l.size());
	return n;
}

std::string libraryDir()
{
	return get_writable_data_path("snippets");
}

// ---- THE HEADER BLOCK -----------------------------------------------------
//
// Leading '#' lines only. Stopping at the first non-comment line is what makes
// "a header comment block must not shift the combo" true of THIS reader too:
// a '#' note halfway down a combo is the codec's business, not metadata.

static std::string trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) a++;
	while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
	return s.substr(a, b - a);
}

static std::vector<std::string> splitTags(const std::string& v)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : v)
	{
		if (c == ',')
		{
			const std::string t = trim(cur);
			if (!t.empty())
				out.push_back(t);
			cur.clear();
		}
		else
			cur += c;
	}
	const std::string t = trim(cur);
	if (!t.empty())
		out.push_back(t);
	return out;
}

// Consume the leading comment block from `text`, filling name/tags. Returns the
// REST, which is what the codec sees. A header key it does not know is left in
// place rather than dropped, so a newer flycast writing a key this one has
// never heard of does not lose it on a retag.
static std::string takeHeader(const std::string& text, std::string& name,
		std::vector<std::string>& tags, std::string& kept)
{
	std::istringstream in(text);
	std::string line;
	std::string rest;
	bool inHeader = true;
	while (std::getline(in, line))
	{
		std::string l = line;
		if (!l.empty() && l.back() == '\r')
			l.pop_back();
		if (inHeader && !trim(l).empty() && trim(l)[0] == '#')
		{
			const std::string body = trim(trim(l).substr(1));
			const size_t colon = body.find(':');
			const std::string key = colon == std::string::npos ? ""
					: trim(body.substr(0, colon));
			const std::string val = colon == std::string::npos ? ""
					: trim(body.substr(colon + 1));
			if (key == "name")
				name = val;
			else if (key == "tags")
				tags = splitTags(val);
			else
				kept += l + "\n";	// an unknown key survives a retag
			continue;
		}
		// A BLANK LINE IS A FRAME. tas_macro::ToText writes a neutral frame as
		// an empty line, so skipping blanks here as "header decoration" would
		// silently eat a sequence that OPENS on neutral - which a combo
		// starting on a buffered gap does. The header therefore ends at the
		// first line that is not a comment, blank included, and makeHeader
		// writes no separator blank of its own.
		inHeader = false;
		rest += l + "\n";
	}
	return rest;
}

static std::string makeHeader(const std::string& name,
		const std::vector<std::string>& tags, const std::string& kept)
{
	std::string h = "# name: " + name + "\n";
	if (!tags.empty())
	{
		h += "# tags: ";
		for (size_t i = 0; i < tags.size(); i++)
			h += (i != 0 ? ", " : "") + tags[i];
		h += "\n";
	}
	return h + kept;
}

// ---- READ / WRITE ---------------------------------------------------------

//! A macro's frames -> lanes, dropping lanes that never hold anything. The drop
//! is the whole point (see the header): the format cannot tell an absent player
//! from a neutral one, so the safe reading is the only reading.
static std::vector<std::vector<Cell>> lanesOf(const tas_macro::Macro& m)
{
	std::vector<std::vector<Cell>> lanes(2);
	Cell any0 = 0, any1 = 0;
	for (const auto& f : m.frames)
	{
		lanes[0].push_back((Cell)f.p1);
		lanes[1].push_back((Cell)f.p2);
		any0 |= f.p1;
		any1 |= f.p2;
	}
	if (any0 == 0)
		lanes[0].clear();
	if (any1 == 0)
		lanes[1].clear();
	return lanes;
}

static tas_macro::Macro macroOf(const Sequence& s)
{
	tas_macro::Macro m;
	const size_t n = s.length();
	for (size_t i = 0; i < n; i++)
	{
		tas_macro::Frame f;
		if (s.lanes.size() > 0 && i < s.lanes[0].size())
			f.p1 = (u16)s.lanes[0][i];
		if (s.lanes.size() > 1 && i < s.lanes[1].size())
			f.p2 = (u16)s.lanes[1][i];
		m.frames.push_back(f);
	}
	return m;
}

static std::string stemOf(const std::string& path)
{
	return ghc::filesystem::path(path).stem().string();
}

bool libraryRead(const std::string& path, Sequence& out, std::string& err)
{
	std::ifstream in(path);
	if (!in)
	{
		err = "cannot open " + path;
		return false;
	}
	std::stringstream ss;
	ss << in.rdbuf();

	out = Sequence{};
	out.file = ghc::filesystem::path(path).filename().string();
	std::string kept;
	const std::string body = takeHeader(ss.str(), out.name, out.tags, kept);
	if (out.name.empty())
		out.name = stemOf(path);	// a headerless archive file names itself

	tas_macro::Macro m;
	tas_macro::FromText(body, m);
	out.lanes = lanesOf(m);
	return true;
}

bool libraryWrite(const std::string& path, const Sequence& s, std::string& err)
{
	const tas_macro::Macro m = macroOf(s);
	const std::string text = makeHeader(s.name, s.tags, "") + tas_macro::ToText(m);
	std::ofstream o(path, std::ios::binary | std::ios::trunc);
	if (!o)
	{
		err = "cannot write " + path;
		return false;
	}
	o << text;
	o.close();
	if (!o)
	{
		err = "write failed: " + path;
		return false;
	}
	return true;
}

bool libraryRetag(const std::string& path, const std::string& name,
		const std::vector<std::string>& tags, std::string& err)
{
	std::ifstream in(path);
	if (!in)
	{
		err = "cannot open " + path;
		return false;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	in.close();

	// The body is carried through UNPARSED. That is the difference between this
	// and libraryWrite: a retag must not be able to damage a combo, so the
	// frames never go near the codec.
	std::string oldName, kept;
	std::vector<std::string> oldTags;
	const std::string body = takeHeader(ss.str(), oldName, oldTags, kept);

	std::ofstream o(path, std::ios::binary | std::ios::trunc);
	if (!o)
	{
		err = "cannot write " + path;
		return false;
	}
	o << makeHeader(name, tags, kept) << body;
	o.close();
	return (bool)o;
}

bool libraryRenameFile(const std::string& dir, const std::string& fromFile,
		const std::string& toFile, std::string& err)
{
	const ghc::filesystem::path from = ghc::filesystem::path(dir) / fromFile;
	const ghc::filesystem::path to   = ghc::filesystem::path(dir) / toFile;
	if (from == to)
		return true;
	std::error_code ec;
	if (ghc::filesystem::exists(to, ec))
	{
		err = toFile + " already exists";	// never silently clobber a combo
		return false;
	}
	ghc::filesystem::rename(from, to, ec);
	if (ec)
	{
		err = ec.message();
		return false;
	}
	return true;
}

bool libraryDelete(const std::string& path, std::string& err)
{
	std::error_code ec;
	if (!ghc::filesystem::remove(path, ec) || ec)
	{
		err = ec ? ec.message() : std::string("not found");
		return false;
	}
	return true;
}

std::vector<Sequence> libraryScan(int *skipped)
{
	return libraryScan(libraryDir(), skipped);
}

std::vector<Sequence> libraryScan(const std::string& dir, int *skipped)
{
	std::vector<Sequence> out;
	int bad = 0;
	std::error_code ec;
	if (ghc::filesystem::is_directory(dir, ec))
	{
		for (const auto& e : ghc::filesystem::directory_iterator(dir, ec))
		{
			if (!e.is_regular_file())
				continue;
			std::string ext = e.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
			if (ext != ".txt")
				continue;
			Sequence s;
			std::string err;
			if (libraryRead(e.path().string(), s, err))
				out.push_back(std::move(s));
			else
				bad++;			// one bad file must not hide the library
		}
	}
	std::sort(out.begin(), out.end(), [](const Sequence& a, const Sequence& b) {
		return a.name < b.name;
	});
	if (skipped != nullptr)
		*skipped = bad;
	return out;
}

// ---- PLACEMENT ------------------------------------------------------------

static Pattern buildPattern(const Sequence& s, bool replace)
{
	Pattern p;
	p.tracks.resize(s.lanes.size());
	for (size_t l = 0; l < s.lanes.size(); l++)
		for (Cell c : s.lanes[l])
			p.tracks[l].push_back(CellOp{ c, replace ? cellAll() : c });
	return p;
}

Pattern patternReplacing(const Sequence& s)   { return buildPattern(s, true); }
Pattern patternOverdubbing(const Sequence& s) { return buildPattern(s, false); }

Sequence sequenceOfRows(const std::map<u32, Row>& all, const std::set<u32>& rows,
		const std::string& name)
{
	Sequence s;
	s.name = name;
	const int lanes = laneCount();
	s.lanes.resize((size_t)lanes);
	std::vector<Cell> any((size_t)lanes, 0);
	for (u32 f : rows)		// a std::set iterates in ascending frame order
	{
		const auto it = all.find(f);
		const Row  r  = it != all.end() ? it->second : blankRow();
		for (int l = 0; l < lanes; l++)
		{
			const Cell c = cellOf(r, l);
			s.lanes[(size_t)l].push_back(c);
			any[(size_t)l] |= c;
		}
	}
	for (int l = 0; l < lanes; l++)
		if (any[(size_t)l] == 0)
			s.lanes[(size_t)l].clear();	// a single-player cut stays single-player
	return s;
}

// ---- SELF-TEST ------------------------------------------------------------
//
// RUN:  flycast --config dojo:PanelSelfTest=yes
// PASS: "ROLLLIB SELFTEST: N passed, 0 failed"
//
// Every fixture here is built to make its failure REACHABLE, because three
// fixtures in this tree already failed to and passed for the wrong reason
// (a compress tested with lo == 0, a reverse tested with no holes, a fill that
// used an ABSENT track where it meant an empty one). The notes below say what
// each fixture would look like if it were vacuous.

void librarySelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLLIB SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	std::error_code ec;
	const ghc::filesystem::path dir =
			ghc::filesystem::temp_directory_path(ec) / "flycast_rolllib_selftest";
	ghc::filesystem::remove_all(dir, ec);
	ghc::filesystem::create_directories(dir, ec);
	claim("the test has a scratch folder", ghc::filesystem::is_directory(dir, ec));
	if (!ghc::filesystem::is_directory(dir, ec))
	{
		NOTICE_LOG(RENDERER, "ROLLLIB SELFTEST: %d passed, %d failed", pass, fail);
		return;
	}
	auto put = [&](const char *file, const std::string& text) {
		const std::string p = (dir / file).string();
		std::ofstream o(p, std::ios::binary | std::ios::trunc);
		o << text;
		return p;
	};
	auto slurp = [&](const std::string& p) {
		std::ifstream i(p, std::ios::binary);
		std::stringstream ss;
		ss << i.rdbuf();
		return ss.str();
	};

	const Profile& prof = profile();
	int lpc = -1, hpc = -1;
	for (int i = 0; i < prof.count; i++)
	{
		if (prof.cols[i].canon == tas_macro::CANON_LP) lpc = i;
		if (prof.cols[i].canon == tas_macro::CANON_HP) hpc = i;
	}
	claim("the profile offers the columns this test needs", lpc >= 0 && hpc >= 0);
	if (lpc < 0 || hpc < 0)
	{
		NOTICE_LOG(RENDERER, "ROLLLIB SELFTEST: %d passed, %d failed", pass, fail);
		return;
	}
	const Column& cLp = prof.cols[lpc];
	const Column& cHp = prof.cols[hpc];

	// ---- the header block -------------------------------------------------
	{
		// The name in the header DIFFERS from the stem, so "took the header"
		// and "took the stem" cannot both be true - a fixture naming the file
		// after its title would pass either way.
		const std::string p = put("bnb.txt", "# name: Ruby Heart bnb\n# tags: combo, midscreen\nZ\nX\n");
		Sequence s;
		std::string err;
		claim("a library file reads", libraryRead(p, s, err));
		claim("the header names the sequence, not the file stem",
				s.name == "Ruby Heart bnb");
		claim("tags parse, comma separated and trimmed",
				s.tags.size() == 2 && s.tags[0] == "combo" && s.tags[1] == "midscreen");
		// Two header lines over two frames: a reader that counted them as
		// frames would answer 4, and one that dropped the first two lines of
		// ANY file would answer 0.
		claim("a header block costs zero frames", s.length() == 2);
	}
	{
		// The control for the claim above. A six-year archive file has no
		// header at all, and must still name itself.
		const std::string p = put("RubyHeartCombo34_P1.txt", "Z\nX\n");
		Sequence s;
		std::string err;
		libraryRead(p, s, err);
		claim("a headerless archive file takes its name from the stem",
				s.name == "RubyHeartCombo34_P1" && s.tags.empty() && s.length() == 2);
	}
	{
		// A NEUTRAL FIRST FRAME. tas_macro writes neutral as an empty line, so
		// a header reader that skipped blanks would return 2 frames here, not
		// 3, and the combo would land one frame early forever.
		const std::string p = put("gap.txt", "# name: gap\n\nZ\n\n");
		Sequence s;
		std::string err;
		libraryRead(p, s, err);
		claim("a sequence that opens on a neutral frame keeps it",
				s.length() == 3 && !s.lanes.empty() && s.lanes[0].size() == 3
				&& s.lanes[0][0] == 0 && s.lanes[0][1] != 0 && s.lanes[0][2] == 0);
	}

	// ---- an all-neutral lane is ABSENT ------------------------------------
	{
		const std::string p1only = put("p1.txt", "Z\nX\n");
		const std::string both   = put("p2.txt", "ZU\nXI\n");
		Sequence a, b;
		std::string err;
		libraryRead(p1only, a, err);
		libraryRead(both, b, err);
		claim("a single-player file leaves the other lane ABSENT",
				a.lanes.size() > 1 && !a.lanes[0].empty() && a.lanes[1].empty());
		// THE CONTROL. Without it, "lane 1 is empty" also passes when the
		// reader never fills lane 1 at all - which is a real way to be wrong.
		claim("a two-player file fills BOTH lanes",
				b.lanes.size() > 1 && !b.lanes[0].empty() && !b.lanes[1].empty());
	}

	// ---- retag leaves the combo alone -------------------------------------
	{
		// The body is deliberately something libraryWrite WOULD change:
		// lowercase letters, a filler dot, an inline note and a stray numeric
		// line. A canonical body would round-trip identically and the claim
		// would hold whether or not the frames were reparsed.
		const std::string body = "z\nx. #wallbounce\n17\nZX\n";
		const std::string p = put("keep.txt", "# name: old\n# tags: a\n# author: dave\n" + body);
		std::string err;
		claim("retag succeeds", libraryRetag(p, "new", { "b", "c" }, err));
		const std::string after = slurp(p);
		claim("retag leaves every frame line byte for byte",
				after.size() >= body.size()
				&& after.compare(after.size() - body.size(), body.size(), body) == 0);
		claim("retag writes the new name and tags",
				after.find("# name: new") != std::string::npos
				&& after.find("# tags: b, c") != std::string::npos
				&& after.find("# name: old") == std::string::npos);
		// A key this build has never heard of must survive, or a newer flycast
		// and an older one cannot share a library.
		claim("retag preserves a header key it does not understand",
				after.find("# author: dave") != std::string::npos);
	}

	// ---- write / read round trip ------------------------------------------
	{
		Sequence s;
		s.name = "written";
		s.tags = { "made", "here" };
		s.lanes.resize(2);
		s.lanes[0] = { (Cell)cLp.canon, 0, (Cell)cHp.canon };
		const std::string p = (dir / "written.txt").string();
		std::string err;
		claim("a sequence writes", libraryWrite(p, s, err));
		Sequence back;
		claim("...and reads back", libraryRead(p, back, err));
		claim("the round trip keeps name, tags, length and the neutral gap",
				back.name == "written" && back.tags.size() == 2
				&& back.length() == 3 && back.lanes[0].size() == 3
				&& back.lanes[0][1] == 0 && back.lanes[1].empty());
	}

	// ---- placement: replace vs overdub ------------------------------------
	{
		// THE DISCRIMINATING FIXTURE. The rows underneath HOLD something, and
		// the sequence has a neutral middle frame. Over blank rows the two
		// modes are pixel-identical and this pair proves nothing.
		std::map<u32, Row> all;
		for (u32 i = 0; i < 8; i++)
			all[i] = cellInto(blankRow(), 0, cellWith(0, cHp, true));

		Sequence s;
		s.name = "gapped";
		s.lanes.resize(2);
		s.lanes[0] = { (Cell)cLp.canon, 0, (Cell)cLp.canon };

		const Edit rep = applyPattern(all, 2, 4, patternReplacing(s), 0);
		const Edit ovr = applyPattern(all, 2, 4, patternOverdubbing(s), 0);
		auto cell = [&](const Edit& e, u32 f) {
			auto it = e.find(f);
			return it == e.end() ? (Cell)0 : cellOf(it->second, 0);
		};
		claim("both modes write the sequence's own frames",
				cellHas(cell(rep, 2), cLp) && cellHas(cell(ovr, 2), cLp));
		claim("REPLACING clears the frame under a neutral step",
				!cellHas(cell(rep, 3), cHp) && !cellHas(cell(rep, 3), cLp));
		claim("OVERDUBBING leaves the frame under a neutral step alone",
				cellHas(cell(ovr, 3), cHp));
		// The other half of the pair: replace must also drop what it lands on
		// where the step is NOT neutral, and overdub must keep it. Without
		// this, a "replace" that only ever cleared neutrals would pass above.
		claim("REPLACING drops what it lands on; OVERDUBBING keeps it",
				!cellHas(cell(rep, 2), cHp) && cellHas(cell(ovr, 2), cHp));
		claim("neither mode touches a row outside the range",
				cellHas(cell(rep, 6), cHp) && cellHas(cell(ovr, 6), cHp));
	}

	// ---- rows -> a sequence ------------------------------------------------
	{
		std::map<u32, Row> all;
		for (u32 i = 0; i < 40; i++)
			all[i] = blankRow();
		all[10] = cellInto(blankRow(), 0, cellWith(0, cLp, true));
		all[30] = cellInto(blankRow(), 0, cellWith(0, cHp, true));
		// A GAPPED selection, and the gap is large: consecutive steps and
		// "steps at their own frame numbers" differ by 20 here, so a reader
		// that kept absolute positions cannot pass.
		const Sequence s = sequenceOfRows(all, { 10, 20, 30 }, "cut");
		claim("a gapped selection yields CONSECUTIVE steps in frame order",
				s.length() == 3 && s.lanes[0].size() == 3
				&& cellHas(s.lanes[0][0], cLp) && s.lanes[0][1] == 0
				&& cellHas(s.lanes[0][2], cHp));
		claim("a single-player cut stays single-player",
				s.lanes.size() > 1 && s.lanes[1].empty());
	}

	// ---- the folder IS the index ------------------------------------------
	{
		// ITS OWN FOLDER. The first run of this claim scanned the scratch
		// directory every other fixture had been writing into, so "the front
		// entry is the one I named first alphabetically" was decided by files
		// belonging to other claims - and it read as a sort defect in the code.
		const ghc::filesystem::path sub = dir / "index";
		ghc::filesystem::create_directories(sub, ec);
		auto putSub = [&](const char *file, const std::string& text) {
			std::ofstream o((sub / file).string(), std::ios::binary | std::ios::trunc);
			o << text;
		};
		// THE TWO ORDERS DISAGREE, deliberately: by filename this is
		// aaa.txt then zzz.txt, whose NAMES are "zzz last" then "aaa first".
		// Sorting by either key is therefore visible in the result.
		putSub("aaa.txt", "# name: zzz last\nX\n");
		putSub("zzz.txt", "# name: aaa first\nZ\n");
		putSub("notes.md", "not a sequence\n");
		int skipped = -1;
		const std::vector<Sequence> lib = libraryScan(sub.string(), &skipped);
		bool sorted = true, sawMd = false, sawDropIn = false;
		for (size_t i = 0; i < lib.size(); i++)
		{
			if (i > 0 && lib[i - 1].name > lib[i].name)
				sorted = false;
			if (lib[i].file == "notes.md")
				sawMd = true;
			if (lib[i].file == "zzz.txt")
				sawDropIn = true;
		}
		// A file dropped into the folder is in the library with no rescan,
		// because there is nothing else that could know about it.
		claim("a dropped-in .txt is in the library", sawDropIn);
		claim("a non-.txt is not", !sawMd);
		// Sorted BY NAME, and the fixture proves it is not sorted by filename:
		// zzz.txt is named "aaa first", so the two orders disagree.
		claim("the library holds exactly the .txt files", lib.size() == 2);
		claim("the library sorts by name, not by filename",
				sorted && lib.size() == 2 && lib.front().name == "aaa first"
				&& lib.back().name == "zzz last");
		claim("a clean folder reports nothing skipped", skipped == 0);
	}

	// ---- rename and delete -------------------------------------------------
	{
		put("a.txt", "# name: a\nZ\n");
		put("b.txt", "# name: b\nX\n");
		std::string err;
		claim("rename REFUSES to clobber an existing file",
				!libraryRenameFile(dir.string(), "a.txt", "b.txt", err) && !err.empty());
		claim("...and the file it would have clobbered is still there",
				ghc::filesystem::exists(dir / "b.txt", ec)
				&& ghc::filesystem::exists(dir / "a.txt", ec));
		claim("rename moves the file",
				libraryRenameFile(dir.string(), "a.txt", "c.txt", err)
				&& ghc::filesystem::exists(dir / "c.txt", ec)
				&& !ghc::filesystem::exists(dir / "a.txt", ec));
		claim("delete removes it",
				libraryDelete((dir / "c.txt").string(), err)
				&& !ghc::filesystem::exists(dir / "c.txt", ec));
		claim("deleting what is not there is refused, not silently fine",
				!libraryDelete((dir / "c.txt").string(), err));
	}

	ghc::filesystem::remove_all(dir, ec);
	NOTICE_LOG(RENDERER, "ROLLLIB SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
