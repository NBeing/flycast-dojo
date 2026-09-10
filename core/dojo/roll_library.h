#pragma once
#include "roll_edit.h"
#include "roll_pattern.h"
#include "roll_profile.h"
#include <map>
#include <set>
#include <string>
#include <vector>

/*
	THE SEQUENCE LIBRARY - saved input sequences you can stamp anywhere.

	A sequence is a combo, a mash pattern, a menu-navigation snippet: a short
	run of frames worth keeping and re-using, as opposed to the movie, which is
	the one long run you are editing. `docs/PIANO-ROLL-LIFT.md` counts six
	symbols for it in the fork; almost all of the weight there is the browser.

	WHAT IT ACTUALLY IS: a folder of .txt files. That is the whole index.

	`[SOURCE]` the fork keeps a JSON index BESIDE the folder - `root["snippets"]`
	in dojo_gui.cpp, with entries carrying the name, the file, the tags and a
	stored mash pattern - and then needs a "Rescan data/snippets/ - dropped-in
	.txt self-register" button in the UI. That button IS the defect: two things
	claim to know what sequences exist, so a file copied into the folder is
	invisible until the user thinks to press it, and an entry whose .txt was
	deleted elsewhere lingers (the fork has an explicit "The .txt is gone from
	data/snippets/" error for exactly that). CLAUDE.md §4: one owner per fact.

	So the metadata lives IN the file, in a leading comment block:

	    # name: Ruby Heart bnb
	    # tags: combo, midscreen
	    WC
	    X
	    ...

	THIS COSTS NOTHING, because the codec already does the work. `[SOURCE]`
	tas_macro::FromText: "'#' opens a comment, to end of line... A line whose
	only content is the comment is an ANNOTATION, not a frame (a header comment
	block must not shift the combo)". A header block therefore reads as zero
	frames in a codec written before this file existed, and a six-year archive
	file with no header loads with its stem as the name and no tags - which is
	the right default rather than a migration.

	AN ALL-NEUTRAL LANE IS ABSENT, NOT NEUTRAL, and that is a fact about the
	format rather than a choice: a CE file that never mentions a P2 letter and
	one whose P2 is deliberately neutral are the SAME BYTES. Reading them as
	present is how the fork ended up with a Fill that wipes the other player on
	every row it touches (docs/ROLL-EDIT-MODEL.md §4). An empty track in a
	Pattern already means "leave that lane alone", so the two agree.
*/
namespace roll
{

struct Sequence
{
	std::string               name;	// display name; the file stem when the header omits it
	std::string               file;	// basename on disk; empty until it is saved
	std::vector<std::string>  tags;
	// One track per lane, in lane order. AN EMPTY LANE IS ABSENT - see above.
	std::vector<std::vector<Cell>> lanes;

	size_t length() const;			//!< longest lane
	bool   empty() const { return length() == 0; }
};

//! Where the library lives. `data/snippets`, deliberately the fork's own folder
//! name, so a user's existing library and a shared .txt both land unchanged.
std::string libraryDir();

//! Everything the folder holds, sorted by name. THE FOLDER IS THE INDEX, so
//! there is nothing to rescan and nothing to fall out of step with.
//! An unreadable individual file is skipped and counted in `skipped`, because
//! one bad .txt must not hide the rest of a library.
std::vector<Sequence> libraryScan(int *skipped = nullptr);

//! The same, over a named folder - which is what makes it testable without
//! writing into the user's real library.
std::vector<Sequence> libraryScan(const std::string& dir, int *skipped = nullptr);

//! One file -> a sequence. False only when the file cannot be read.
bool libraryRead(const std::string& path, Sequence& out, std::string& err);

/*
	A sequence -> one file, header block then frames.

	REWRITES THE BODY, so inline notes ("WC #wallbounce") do not survive - a
	property inherited from tas_macro::Save, which round-trips semantically and
	not byte-identically. Use libraryRetag for a metadata-only change; it is
	written to leave every non-header line exactly as it was.
*/
bool libraryWrite(const std::string& path, const Sequence& s, std::string& err);

//! Name and tags only. Every line that is not part of the leading header block
//! is copied through byte for byte, so renaming a combo cannot damage it.
bool libraryRetag(const std::string& path, const std::string& name,
		const std::vector<std::string>& tags, std::string& err);

//! Move the .txt. The name in the header is NOT touched: a file's name and its
//! title are different facts, and the fork conflates them (its rename moves the
//! file AND rewrites the entry, so two combos may not share a title).
bool libraryRenameFile(const std::string& dir, const std::string& fromFile,
		const std::string& toFile, std::string& err);

bool libraryDelete(const std::string& path, std::string& err);

/*
	A STORED SEQUENCE AT PLACEMENT TIME, and the two ways to mean it.

	There is no merge FLAG here either - the two functions differ only in the
	mask they build, which is the same rule roll_pattern.h states one level
	down. What falls out of it is worth reading twice:

	  REPLACING  mask = cellAll(), so a neutral frame in the sequence CLEARS
	             the frame it lands on. A combo's gaps are part of the combo.
	  OVERDUBBING mask = each step's own bits, so a neutral frame touches
	             nothing and the input already under the gap survives.

	Both are wanted, neither is a mode set somewhere else, and a sequence does
	not record which it "is" - the gesture that places it decides.
*/
Pattern patternReplacing(const Sequence& s);
Pattern patternOverdubbing(const Sequence& s);

/*
	Rows -> a sequence: "new snippet from the selection".

	Takes a SET, and reads it in ascending frame order, so a gapped selection
	yields consecutive steps - the same reading applyPatternToRows uses, and for
	the same reason. Lanes that are neutral across every selected row come back
	EMPTY, so a snippet cut from a single-player stretch stays single-player.
*/
Sequence sequenceOfRows(const std::map<u32, Row>& all, const std::set<u32>& rows,
		const std::string& name);

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void librarySelfTest();

}	// namespace roll
