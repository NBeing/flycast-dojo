// engine_diff - run OUR copyLiveSet and HIS over the same directory, diff the result.
//
//   RUN:   compiled + run by scripts/enginediff.sh; no emulator, no ROM, no display.
//   PASS:  both loop bodies copy the identical set of files, byte-for-byte, and
//          neither copies a non-live-set file or recurses a subdirectory.
//   SELF:  --self-test deliberately drops one extension from theirs() and requires
//          the comparison to CATCH the divergence.
//
// WHY THIS EXISTS. `[MEASURED 2026-09-15]` core/dojo/tas_branch.cpp and
// core/oslib are BYTE-IDENTICAL to reference/flycast-rr, so the branch engine's
// parity is guaranteed by construction. The ONE behavioral divergence is
// tas_clip::copyLiveSet: he factored it behind isLiveSetExt(), this tree
// extracted it inline from archive(). Same intent, different code - and it is
// the file set that makes up a clip, the sort of fact that drifts silently when
// a sidecar extension is added to one fork and not the other. A branch or an F8
// backup would then quietly drop a file.
//
// This is the differential idea aimed where parity is actually INTENDED: the
// engines are meant to match him, so "same as his" is the pass condition here,
// unlike the roll_* modules that deliberately fix his bugs. The bodies below are
// faithful transcriptions of each fork's copyLiveSet; scripts/enginediff.sh
// additionally reads BOTH pinned source files and asserts their extension sets
// match, so a transcription here cannot silently go stale against his fork.
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using u64 = std::uint64_t;

// If set (by --self-test), theirs() pretends ".map" is not a live-set extension -
// the exact shape of "his fork gained/lost an extension and ours did not".
static bool g_sabotageTheirs = false;

// -------------------------------------------------------------------------------------------
// OURS - transcribed from core/dojo/tas_clip.cpp copyLiveSet (inline extension list).
// -------------------------------------------------------------------------------------------
static int copyLiveSet_ours(const std::string& srcDir, const std::string& dstDir, u64 *bytesCopied)
{
	std::error_code ec;
	int copied = 0;
	u64 bytes = 0;
	for (const auto& f : fs::directory_iterator(srcDir, ec))
	{
		if (f.is_directory(ec))
			continue;
		const std::string ext = f.path().extension().string();
		if (ext == ".flyr" || ext == ".flyreplay" || ext == ".state" || ext == ".frame"
				|| ext == ".json" || ext == ".png" || ext == ".label" || ext == ".txt"
				|| ext == ".env" || ext == ".wave" || ext == ".map")
		{
			std::error_code sizeEc;
			const u64 sz = (u64)fs::file_size(f.path(), sizeEc);
			std::error_code cpEc;
			fs::copy_file(f.path(), fs::path(dstDir) / f.path().filename(),
					fs::copy_options::overwrite_existing, cpEc);
			if (!cpEc)
			{
				copied++;
				if (!sizeEc)
					bytes += sz;
			}
		}
	}
	if (bytesCopied != nullptr)
		*bytesCopied = bytes;
	return copied;
}

// -------------------------------------------------------------------------------------------
// THEIRS - transcribed from reference/flycast-rr/core/dojo/tas_clip.cpp (isLiveSetExt + loop).
// -------------------------------------------------------------------------------------------
static bool isLiveSetExt_theirs(const std::string& ext)
{
	if (g_sabotageTheirs && ext == ".map")
		return false;			// --self-test: the injected drift
	return ext == ".flyr" || ext == ".flyreplay" || ext == ".state" || ext == ".frame"
			|| ext == ".json" || ext == ".png" || ext == ".label" || ext == ".txt"
			|| ext == ".env" || ext == ".wave" || ext == ".map";
}
static int copyLiveSet_theirs(const std::string& srcDir, const std::string& dstDir, u64 *bytesCopied)
{
	std::error_code ec;
	int copied = 0;
	u64 bytes = 0;
	for (const auto& f : fs::directory_iterator(fs::path(srcDir), ec))
	{
		if (f.is_directory(ec))
			continue;
		if (!isLiveSetExt_theirs(f.path().extension().string()))
			continue;
		std::error_code sizeEc;
		u64 sz = (u64)fs::file_size(f.path(), sizeEc);
		fs::copy_file(f.path(), fs::path(dstDir) / f.path().filename(),
				fs::copy_options::overwrite_existing, ec);
		if (!ec)
		{
			copied++;
			if (!sizeEc)
				bytes += sz;
		}
	}
	if (bytesCopied != nullptr)
		*bytesCopied = bytes;
	return copied;
}

// -------------------------------------------------------------------------------------------

static int g_pass = 0, g_fail = 0;
static void claim(const char *what, bool ok)
{
	(ok ? g_pass : g_fail)++;
	printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
}

static void write(const fs::path& p, const std::string& content)
{
	std::ofstream f(p, std::ios::binary);
	f << content;
}

// The file set of a directory: name -> contents. Names only at top level (both
// loops copy only the top level), so a plain map is the whole comparison.
static std::vector<std::pair<std::string, std::string>> treeOf(const fs::path& dir)
{
	std::vector<std::pair<std::string, std::string>> out;
	std::error_code ec;
	for (const auto& f : fs::directory_iterator(dir, ec))
	{
		if (f.is_directory(ec))
			continue;
		std::ifstream in(f.path(), std::ios::binary);
		std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		out.push_back({ f.path().filename().string(), body });
	}
	std::sort(out.begin(), out.end());
	return out;
}

int main(int argc, char **argv)
{
	if (argc > 1 && std::string(argv[1]) == "--self-test")
		g_sabotageTheirs = true;

	std::error_code ec;
	const fs::path root = fs::temp_directory_path(ec) / "flycast-enginediff";
	fs::remove_all(root, ec);
	const fs::path src = root / "src", da = root / "ours", db = root / "theirs";
	fs::create_directories(src, ec);
	fs::create_directories(da, ec);
	fs::create_directories(db, ec);

	// LIVE SET: one file of every extension the copier must take. Distinct
	// contents so a byte comparison is meaningful.
	const char *live[] = { "clip.json", "movie.flyr", "old.flyreplay", "s0.state",
			"s0.state.frame", "s0.state.png", "s0.state.label", "audio.wave",
			"skip.map", "clip_macro.txt", "notation.env" };
	for (const char *n : live)
		write(src / n, std::string("body-of-") + n);
	// NON live set: must never be copied. .STATE is uppercase - the compare is
	// case-sensitive, so it is NOT a match, which is the control on a copier
	// that lowercased or matched loosely.
	const char *junk[] = { "scratch.tmp", "run.log", "README", ".hidden", "S0.STATE" };
	for (const char *n : junk)
		write(src / n, std::string("junk-") + n);
	// A SUBDIRECTORY holding a would-be live file: must be skipped whole, not
	// recursed. This is the rule that caps branch depth at 1.
	fs::create_directories(src / "branches", ec);
	write(src / "branches" / "nested.state", "should-never-be-copied");

	u64 ba = 0, bb = 0;
	const int na = copyLiveSet_ours(src.string(), da.string(), &ba);
	const int nb = copyLiveSet_theirs(src.string(), db.string(), &bb);
	const auto ta = treeOf(da), tb = treeOf(db);

	const size_t nLive = sizeof(live) / sizeof(live[0]);
	claim("ours copies every live-set file and no more", na == (int)nLive);
	// THE DIFFERENTIAL: the two loop bodies produce the identical tree. Under
	// --self-test theirs() drops .map, so this is the claim that must go red.
	claim("ours and theirs copy the IDENTICAL file set, byte-for-byte", ta == tb);
	claim("...and they copy the same number of files", na == nb);
	claim("...and account the same byte total", ba == bb);

	// NON-VACUITY: a copier that took everything, or nothing, would satisfy
	// "ours == theirs" trivially. Pin the actual set on our side.
	std::set<std::string> names;
	for (const auto& kv : ta)
		names.insert(kv.first);
	claim("a subdirectory is skipped whole, not recursed",
			names.count("nested.state") == 0);
	claim("junk extensions are not copied",
			names.count("scratch.tmp") == 0 && names.count("run.log") == 0
			&& names.count("README") == 0 && names.count(".hidden") == 0);
	claim("the match is case-sensitive (S0.STATE is not a .state)",
			names.count("S0.STATE") == 0);
	claim("a copied file's bytes are the source's bytes",
			!ta.empty() && ta[0].second == std::string("body-of-") + ta[0].first);

	fs::remove_all(root, ec);
	printf("  %d passed, %d failed%s\n", g_pass, g_fail,
			g_sabotageTheirs ? "  (--self-test: a caught divergence is EXPECTED)" : "");
	// In --self-test the divergence claim SHOULD fail; the wrapper inverts the
	// exit, so here we still return nonzero-on-fail and let it judge.
	return g_fail == 0 ? 0 : 1;
}
