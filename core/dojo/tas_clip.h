#pragma once
#include "types.h"
#include "deps/filesystem.hpp"
#include "json.hpp"
#include <ctime>
#include <string>
#include <utility>
#include <vector>

// tas_clip (2026-09-03, the generations refactor): everything that touches a CLIP FOLDER and its clip.json, with no
// ImGui and no session state. A clip is a folder under data/replays/<game>/<clip>/ (the movie, the savestates and
// their sidecars, the optional <clip>_macro.txt, audio.env, skip.map, clip.json - see CLIP_SCHEMA.md) plus its F8
// backups, the <clip>_gen_NN / <clip>_setup_NN subfolders ("generations"). Before this module the same clip.json was
// read-modified-written from nine places with two path spellings and two dump formats, and three private
// "is this a backup folder" predicates disagreed (two never saw setups). Everything routes through here now: the two
// pre-boot browsers, the in-session drawer, the rename flow, the replay seed and Dojo's session wrappers.
//
// Rules: functions take a clip DIRECTORY (pre-boot safe, no hostfs::savestateFolderOverride); every write preserves
// fields it does not own (read-modify-write, dump(2)); reconcile() rewrites clip.json and must never run per frame.
namespace tas_clip
{
	// ---- the clip.json date conventions ----
	std::string utcIso(time_t t);						// "2026-08-22T16:37:41Z"
	std::string utcNowIso();
	std::string localUsTime(const std::string& iso);	// "...T16_37_41Z" or "...T16:37:41Z" -> "08/22/2026 09:37 AM" local

	// ---- backup folders ----
	// THE predicate: <clip>_gen_NN, <clip>_setup_NN (macro clips) or a bare gen_NN from older builds; kind / num come back.
	bool genFolderKind(const std::string& name, const std::string& base, std::string& kind, int& num);
	int stateFileSlot(const ghc::filesystem::path& p);	// <game>.state = 0, <game>_N.state = N
	// Copy the live set (by extension) into the next <clip>_<kind>_NN - a number never used before for that kind, on disk
	// or in clip.json. Returns the number, or -1 (limit / clash). Gens are constants once written.
	int archive(const std::string& clipDir, int *filesCopied, u64 *bytesCopied, const char *kind);
	// Replace the live set with a backup: live-only state files go to <clip>/.trash/<utc>/, every backup file but
	// clip.json is copied over, and clip.json is MERGED (the facts of the restored movie from the backup, the live
	// identity / generations / tags / notes kept, rerecords = max, restoredFrom recorded). Returns files copied or -1.
	// Pure folder work: the caller decides whether a restore is allowed (Dojo refuses the clip open in a session).
	int restore(const std::string& clipDir, const std::string& genName);
	// Make clip.json generations[] match the folder: unrecorded backups get an entry synthesized from the folder
	// (recovered: true), entries whose folder is gone stay flagged present: false, files / bytes are recounted.
	void reconcile(const std::string& clipDir);

	// ---- clip.json ----
	nlohmann::json read(const std::string& clipDir);					// {} when absent / unparsable, always an object
	bool write(const std::string& clipDir, const nlohmann::json& j);	// dump(2), trailing newline
	void parseTags(const char *csv, std::vector<std::string>& out);	// "a, b ,c" -> [a, b, c]
	std::string joinTags(const nlohmann::json& tags);					// ["a","b"] -> "a, b"
	bool hasTag(const std::vector<std::string>& tags, const char *tag);	// case-insensitive
	void toggleTagCsv(char *buf, size_t sz, const char *tag);		// add / remove a tag in a CSV edit buffer
	bool readTagsNotes(const std::string& clipDir, std::string& tagsCsv, std::string& notes,
			std::string *created, std::string *mode);					// the browsers' row facts
	bool writeTagsNotes(const std::string& clipDir, const char *tagsCsv, const char *notes);
	bool setGenerationTagsNotes(const std::string& clipDir, const std::string& genName, const char *tagsCsv, const char *notes);
	bool appendGeneration(const std::string& clipDir, const nlohmann::json& entry);	// + generationCount / latestGeneration / schema
	// After a clip folder rename: macroFile, generations[].name, latestGeneration and contents carry the old name.
	bool renameMeta(const std::string& newClipDir, const std::string& oldName, const std::string& newName);
	// Delete F8 backups: MOVE each <clipDir>/<genName> to <clipDir>/.trash/<utc>/ (restore's "never a hard delete" rule),
	// drop its generations[] record, recompute generationCount / latestGeneration / contents.generationFolders (present-only,
	// like reconcile). Survivors keep their numbers. Holds clipMutex across the folder move AND the atomic clip.json rewrite,
	// so the emu thread's WriteClipStats cannot interleave. A record whose folder is already gone just loses its record.
	int deleteGenerations(const std::string& clipDir, const std::vector<std::string>& genNames);	// records removed; one .trash stamp, one write, one bump
	bool deleteGeneration(const std::string& clipDir, const std::string& genName);
	// A new clip's first clip.json (Replay::CreateReplayFile).
	bool seed(const std::string& clipDir, const std::string& game, const std::string& created,
			const std::string& tagsCsv, const std::string& notes);
	// TEST LAB (David, 2026-09-05): the PERMANENT, tagged fixture-state library replays/<game>/_lab - a clip folder with
	// no movie that the Test Lab session (launch menu 6) binds its savestates to. Fixtures are ordinary labeled states
	// copied in from any clip (States row menu > Add to Test Lab): a test loads one, sends inputs, asserts on the combo
	// meters. The folder NAME is what marks the lab, so nothing about it can leak across sessions.
	std::string labDir(const std::string& gameName);					// replays/<game>/_lab (not created here)
	bool labIsActive(const std::string& savestateFolderOverride);	// is the bound savestate folder the lab (or one of its tests)?
	// A TEST is a folder under the lab (David): BASE (slot 0) is its permanent fixture - only Overwrite BASE changes it;
	// slots 1-99 are that test's own outcomes / scratch, so tests never stomp each other. TEST_NN_<name> by default.
	struct LabTest
	{
		std::string name, dir;
		bool hasBase = false;
		int states = 0;
		std::vector<std::string> tags;			// TOP-LEVEL clip.json tags (click-to-edit in the Test Lab window)
		std::string notes;						// TOP-LEVEL clip.json notes (click-to-edit)
		std::string createdUtc, createdLocal;	// clip.json "created" if ISO, else the BASE .state mtime
		std::string modifiedUtc, modifiedLocal;	// newest top-level file mtime in the test folder
	};
	int labTests(const std::string& gameName, std::vector<LabTest>& out);				// sorted by name; count
	std::string labNewTestDir(const std::string& gameName, const std::string& label);	// the next TEST_NN[_label] path (not created)

	struct Generation
	{
		int gen = 0;
		std::string kind, name, createdUtc, createdLocal, mode, notes;
		std::vector<std::string> tags;
		int files = 0;
		u64 bytes = 0;
		u32 atFrame = 0, movieFrames = 0, rerecords = 0;
		std::vector<int> slots;
		std::vector<std::pair<int, u32>> slotFrames;
		bool present = true, recovered = false;
	};
	bool loadGenerations(const std::string& clipDir, std::vector<Generation>& out);	// as recorded (call reconcile first for accuracy)

	// Bumped by every write through this module - a GUI list cached on it knows when to rescan.
	u32 libraryVersion();
	void bump();
}
