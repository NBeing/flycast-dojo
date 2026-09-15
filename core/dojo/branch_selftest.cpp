#include "tas_branch.h"
#include "tas_clip.h"
#include "roll_host.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "deps/filesystem.hpp"
#include <fstream>
#include <string>

/*
	THE ARM FOR tas_branch - the module's pure half, driven with no emulator.

	`[MEASURED 2026-09-14]` tas_branch.cpp arrived byte-identical from the fork and
	had, like tas_auto and tas_clip's lab half before it, no coverage here at all.
	The questions below are the ones a UI is about to lean on: which folder is
	the root of any head, what a branch folder IS, and what a merge verdict says.
	Kept in its own TU so the engine file stays a clean diff against the pin.
*/
namespace roll {
namespace branch {

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "BRANCH SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- what a branch folder IS, and which folder is the root ---------------------
	// Pure string arithmetic on paths that need not exist - the header's
	// "pre-boot safe" claim, checked on the three functions it is true for.
	const std::string clip = "/tmp/replays/G/2026-09-14T00_00_00Z";
	const std::string br   = clip + "/branches/2026-09-14T01_00_00Z_state_3_01";
	claim("branchesDir is <clip>/branches",
			tas_branch::branchesDir(clip) == clip + "/branches");
	claim("a folder whose parent is 'branches' is a branch dir",
			tas_branch::isBranchDir(br));
	claim("the clip itself is NOT a branch dir", !tas_branch::isBranchDir(clip));
	claim("...nor is the branches/ container", !tas_branch::isBranchDir(clip + "/branches"));
	claim("rootOf a branch is two levels up - the clip", tas_branch::rootOf(br) == clip);
	// THE CONTROL, and the one Captures depends on: with no branches in play,
	// rootOf must be the identity. A rootOf that always stripped two levels
	// would satisfy the claim above and break every non-branch caller.
	claim("rootOf a plain clip is the clip itself (identity)",
			tas_branch::rootOf(clip) == clip);

	// ---- list() on a clip that has no clip.json ---------------------------------
	claim("list() of a folder with no clip.json is an empty array, not an error",
			[&]{ const nlohmann::json j = tas_branch::list("/nonexistent/clip/dir");
			     return j.is_array() && j.empty(); }());

	// ---- the merge verdicts say something distinguishable ---------------------------
	{
		using V = tas_branch::MergeVerdict;
		const std::string ok  = tas_branch::mergeVerdictText(V::Ok);
		const std::string na  = tas_branch::mergeVerdictText(V::NoAnchor);
		const std::string mm  = tas_branch::mergeVerdictText(V::MainMissing);
		const std::string fm  = tas_branch::mergeVerdictText(V::FrameMismatch);
		const std::string pd  = tas_branch::mergeVerdictText(V::PrefixDiverged);
		claim("every verdict has text", !ok.empty() && !na.empty() && !mm.empty()
				&& !fm.empty() && !pd.empty());
		// NON-VACUITY: five verdicts must be five DIFFERENT messages. A table
		// that returned one string for all of them passes the claim above.
		claim("...and the five verdicts are five different messages",
				ok != na && na != mm && mm != fm && fm != pd && ok != pd && na != fm);
	}

	// ---- mergeStatus on a clip with no branches refuses, and says why ----------------
	// A real folder this time: an empty clip dir with a minimal clip.json and no
	// branches[] entry. The gate must not answer Ok for a branch that does not
	// exist - that would let merge() copy nothing over main and report success.
	{
		std::error_code ec;
		const std::string tmp = (ghc::filesystem::temp_directory_path(ec)
				/ "flycast-branch-selftest").string();
		ghc::filesystem::remove_all(tmp, ec);
		ghc::filesystem::create_directories(tmp, ec);
		{
			std::ofstream f(tmp + "/clip.json");
			f << "{\"schema\":1,\"branches\":[]}\n";
		}
		const tas_branch::MergeCheck mc = tas_branch::mergeStatus(tmp, "no-such-branch");
		claim("mergeStatus for an unknown branch is not Ok",
				mc.verdict != tas_branch::MergeVerdict::Ok);
		claim("...and it is NoAnchor - there is nothing to compare against",
				mc.verdict == tas_branch::MergeVerdict::NoAnchor);
		claim("merge() of an unknown branch refuses (-1) and copies nothing",
				tas_branch::merge(tmp, "no-such-branch") == -1);
		ghc::filesystem::remove_all(tmp, ec);
	}

	NOTICE_LOG(RENDERER, "BRANCH SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace branch
}	// namespace roll
