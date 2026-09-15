/*
	`[PORTED 2026-09-14]` VERBATIM from reference/flycast-rr @ edca8915 - both
	files byte-identical to his, and deliberately so. `[MEASURED 2026-09-14]` a
	closure survey of all 633 lines found ZERO that depend on anything this tree
	lacks: core/oslib/oslib.{h,cpp} are byte-identical between the forks, 11 of
	the 12 tas_clip:: entry points were already declared here, and the module
	has no ImGui, no gui_*, no config:: - it is a pure engine. The whole gap was
	~39 lines OUTSIDE it: tas_clip::copyLiveSet (extracted from our archive()),
	Dojo::FlushLiveClip, and prefixHash mirrored into clip.json's states[].

	TWO THINGS THE SURVEY FOUND, recorded here rather than patched in, because
	the file below is his and a diff against the pin should stay empty:

	- THE DEPTH-1 INVARIANT IS ENFORCED BY THE CALLER, NOT HERE. create() will
	  happily make <clip>/branches/<id>/branches/<id2>/ if handed a branch dir;
	  his UI guards it with `canCreate = s.exists && !onBranch`. Any UI wired to
	  this module must carry that guard itself.
	- "pre-boot safe" (below) holds for read/write/list/rootOf but NOT for
	  create() or revalidateForks(): both compute the fork anchor from
	  hostfs::scanSavestateInfo(), which reads the CURRENTLY BOUND folder, not
	  the clipDir argument. Correct only because the caller passes the live head.
*/
#pragma once
#include "types.h"
#include "deps/filesystem.hpp"
#include "json.hpp"
#include <set>
#include <string>

// tas_branch (branches feature, M2 - the persistence layer). A BRANCH is a full independent copy of a clip
// (movie + states + sidecars) forking from a chosen frame, nested at <clip>/branches/<id>/. Every per-folder
// mechanism (slots, F8 generations, stats, wave/ruler, thumbnails, the single .flyr writer) works UNCHANGED inside
// a branch because a branch IS just another clip folder. main = the root clip's live set.
//
// Decisions (BRANCHES_ROADMAP.md):
//   - Topology = depth-1 star: every branch forks from main; no branch-of-branch (branches[] stays flat).
//   - create copies EVERYTHING (copyLiveSet as-is) for now - correctness first; fork-state-only is a later
//     optimization that slots into copyLiveSet's filter.
//   - Folder id = "<YYYY-MM-DDTHH_MM_SSZ>_state_<N>_<NN>" (dev 2026-09-06): a UNIQUE UTC timestamp (the clip-folder
//     convention, colons->underscores) pins WHEN the fork happened (a state can be re-saved/updated) and keeps every
//     folder distinct; <N> is the source slot, <NN> a per-state sequence. Folder-safe, so it can never collide with
//     genFolderKind (<clip>_gen_NN) and reconcile won't mistake it for a generation.
//   - The human name / tags / notes live in the ROOT clip.json branches[] entry (keyed by id), NOT in the folder
//     name - so branches are enumerable and taggable in a for-each pattern (the wakeup-mixup options).
//
// No ImGui here (pre-boot safe): functions take a clip DIRECTORY, not hostfs::savestateFolderOverride.
namespace tas_branch
{
	// <clip>/branches (the parent of every branch folder for this clip).
	std::string branchesDir(const std::string& clipDir);

	// Fork the live clip into a new branch folder. Persists the current clip first (Dojo::FlushLiveClip - hazard #1,
	// flush-before-copy, or the branch loses up to ~119 unflushed movie frames), then copyLiveSet into
	// branches/<id>/, then REGISTERS the branch in the root clip.json branches[] (id/name/fromSlot/atFrame/
	// createdAt/modifiedAt/files/bytes/tags/notes) and stamps the branch's own clip.json with its lineage. name = the
	// display name (usually the source state's label; empty -> the id is used). tagsCsv ("a, b, c") + notes are what
	// the USER cares about (dev) - everything else on disk is machine-friendly. fromSlot/atFrame = the fork point.
	// The copy is the whole live set regardless of recording mode. Returns the new branch DIRECTORY, or "" on failure.
	// Everything happens Paused (the caller's responsibility - the emu thread owns the roll while running).
	std::string create(const std::string& clipDir, int fromSlot, u32 atFrame, const std::string& name,
			const std::string& tagsCsv = "", const std::string& notes = "");

	// Delete a branch: move branches/<id>/ to <clip>/.trash/<utc>/<id> (never a hard delete - matches restore()) and
	// drop its branches[] entry from the root clip.json. id = the folder id (the branches[] "id"). Returns true on the
	// move. Refuses if id doesn't resolve to a folder directly under branches/.
	bool remove(const std::string& clipDir, const std::string& id);

	// The root clip's branches[] as recorded (an empty array when none / no clip.json). The read side of the
	// for-each pattern - the future Branches list / tagging UI iterates this.
	nlohmann::json list(const std::string& clipDir);

	// The ROOT clip of a folder: if dir is a branch (<clip>/branches/<id>), the <clip>; otherwise dir itself. The
	// authority for the node graph + tags is always the root clip.json, so callers resolve it from wherever HEAD is.
	std::string rootOf(const std::string& dir);
	bool isBranchDir(const std::string& dir);	// true when dir sits directly under a "branches" folder

	// Make sure clipDir's clip.json carries a "main" node (id/kind/name/tags/color). Idempotent - stamps it only when
	// absent (a clip seeded before this feature). Called on create so main is a node the moment the first fork exists.
	void ensureMainNode(const std::string& clipDir);

	// The slots that are branch fork points for the CURRENT head (dev: light-green = "a branch exists here", the
	// jump-off AND merge point). On main = every branches[].fromSlot; on a branch = its own fromSlot. CACHED on
	// (headDir, tas_clip version) so the per-frame Timeline / States draws never parse clip.json. headDir =
	// hostfs::savestateFolderOverride. Returns a reference to a GUI-thread-local cache (do not retain across frames).
	const std::set<int>& forkSlots(const std::string& headDir);

	// After savestate `savedSlot` was (over)written in the current head, re-check the branches that fork from it: does
	// the current state still match the branch's remembered fork anchor (frame + prefixHash)? Sets/clears forkMismatch
	// on each affected branch (on MAIN: every branch forking from savedSlot; on a BRANCH: that branch), so a later
	// merge refuses / warns when the jump-off point moved. Returns how many branches changed their flag. headDir =
	// hostfs::savestateFolderOverride. (dev: "keep a record in the json that tells main the jump-off no longer matches".)
	int revalidateForks(const std::string& headDir, int savedSlot);

	// Switch the live session INTO a branch (or back to main): Dojo::SwitchClipFolder(dir) + load a positioning state.
	// Paused only; the GUI caller (dojo_gui) owns gui_loadState + the piano-roll selection reset, so checkout itself is
	// coordinated there (gui_branch_checkout). This header documents the model; there is no ImGui-free checkout().

	// ---- MERGE readiness (the gate; non-destructive) ----
	// Can a branch's tail splice back into main RIGHT NOW? Compares the branch's fork anchor to main's CURRENT
	// state[fromSlot] (frame + prefixHash, read from the ROOT clip.json - so it works from any head). Ok = the fork
	// point is still frame-aligned AND prefix-identical, so the splice is safe; every other verdict names why not.
	enum class MergeVerdict { Ok, NoAnchor, MainMissing, FrameMismatch, PrefixDiverged };
	struct MergeCheck
	{
		MergeVerdict verdict = MergeVerdict::NoAnchor;
		int slot = -1;			// the fork slot (branch's fromSlot)
		u32 forkFrame = 0;		// the branch's remembered fork frame
		u32 mainFrame = 0;		// main's current state[slot] frame
	};
	MergeCheck mergeStatus(const std::string& rootDir, const std::string& branchId);
	const char *mergeVerdictText(MergeVerdict v);	// a short human reason, for tooltips / menu labels

	// Merge a branch's ENTIRE live set (states + macro + movie + sidecars) INTO main (dev's semantics), behind a
	// tagged F8 backup. Gate first (mergeStatus == Ok, else -1). Steps: F8-backup main tagged "merge from <branch>"
	// (undo); trash main's orphan states; copy the branch's files over main EXCEPT clip.json and the macro .txt
	// (the branch has two - the stale fork-time copy and its real one); bring the branch's REAL macro (its clip.json
	// macroFile) in under MAIN's macro name; MERGE clip.json (adopt movie facts + states, keep main's identity /
	// branches[] / node / macroFile, record mergedFrom). Returns files copied, or -1. mainDir = the root clip. The
	// GUI must FlushLiveClip main BEFORE (so the backup captures main's current) and re-attach + reload AFTER (the
	// files changed under the live session) - see gui_branch_merge. Paused only (the caller's responsibility).
	int merge(const std::string& mainDir, const std::string& branchId);

	// Set (or clear, if hex is empty) a branch's display color ("#RRGGBB") in the root clip.json branches[] entry and
	// its own node, and bump modifiedAt. For the node-graph's per-branch colour coding (the wakeup options). (dev)
	void setColor(const std::string& rootDir, const std::string& id, const std::string& hex);

	// Set a branch's tags (CSV "a, b, c") + notes and bump modifiedAt - the Branch Properties panel's write side. id
	// "main" edits the root clip's node tags/notes; any other id edits that branches[] entry (+ the branch's own node).
	void setTagsNotes(const std::string& rootDir, const std::string& id, const std::string& tagsCsv, const std::string& notes);
}
