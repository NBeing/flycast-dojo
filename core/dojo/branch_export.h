/*
	PER-BRANCH VIDEO EXPORT - main and every branch, each captured start to
	finish into captures/<clip>/<tag>.avi.

	`[PORTED 2026-09-14]` from the fork's bx* state machine (~230 lines behind
	gui_branch_export_*). It reuses the replay machinery: for each item, pause,
	check out its folder (lands on BASE), put the session into replay playback,
	arm a NAMED capture, resume, and let the movie run to its end - where the
	replay-end hook in dojo.cpp stops the recorder, because `dojo:AutoCapture`
	is set for the duration. A per-frame tick pumped from mainui walks the
	queue; a stall guard and a watchdog keep a stuck item from hanging the run.

	WHAT IS PURE, and therefore under the arm: the filename sanitiser and the
	candidate list (main first, labels from tags > name > id, de-duplicated).
	Everything else moves the emulator and is verified by running it.
*/
#pragma once
#include "types.h"
#include "deps/json/json.hpp"
#include <string>
#include <vector>

namespace roll {
namespace bexport {

struct Item
{
	std::string dir;	//!< the clip folder (root, or a branch)
	std::string name;	//!< filename-safe, unique within the run
};

//! Keep [A-Za-z0-9-_], spaces -> '_', drop the rest; empty -> "branch"; cap 60.
std::string sanitize(const std::string& s);

/*
	main first, then every branch in `branches` (the root clip's `branches[]`
	array), each named by its tags, else its name, else its id, sanitised and
	de-duplicated with _2, _3 ... so two branches tagged alike cannot collide
	on disk. Pure: takes the JSON rather than reading the folder.
*/
std::vector<Item> buildCandidates(const std::string& rootDir, const nlohmann::json& branches);

//! Rebuild the offered list from the bound clip. Empty when no clip is bound.
void refreshCandidates();
const std::vector<Item>& candidates();
std::vector<unsigned char>& checked();		//!< parallel to candidates(); 1 = export it

//! Start the run over the checked candidates. False if nothing is checked,
//! no clip is bound, or a run is already active.
bool launch();
void cancel();
bool active();
std::string progress();					//!< "Exporting 2/3: name", or empty

//! Pumped once per frame from mainui, like fst::tick. No-op when inactive.
void tick();

void selfTest();

}	// namespace bexport
}	// namespace roll
