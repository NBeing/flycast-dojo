#pragma once
#include "types.h"
#include <string>
#include <vector>

/*
	THE INTENT HELPER - one ceremony for every intent module of the Surface Tour.

	`[2026-09-17]` the user, having watched the 77-step tour: "we don't have any real
	meat to these tests. each feature should be tested with its INTENT." The intent of
	every studio feature is a GAME outcome - a hit that lands, a branch whose fighter
	does something main's does not - and the only honest way to ask the game is the
	combo hunt's ceremony (combohunt.cpp, itself the FST's): pause for checkout,
	WRITE-authoring, snapshot the roll, reload BASE, bake an edit through the funnel,
	run to a stop frame under fast-forward, read the peak WHILE STOPPED, restore.

	This file is that ceremony lifted out of the hunt so that three module TUs
	(intent_roll.cpp, intent_clip.cpp, intent_send.cpp) share ONE implementation of
	it instead of three drifting copies (the lesson of arms.lua / serve_arms.py).
	Nothing here is new behaviour: every function is a hunt step with a name.

	THE FIXTURE it stands on: the tour clip's slot 0 (in-match) as BASE, and David's
	Combo_Dhalsim97 window as THE COMBO - `dojo:IntentMacro=<file>` (the harness
	stages scripts/fixtures/mvc2/candidates/Combo_Dhalsim97_pcsx2_macro.txt), window
	from the file's own CLIP markers or `dojo:IntentWindow=a-b`. Measured on this
	base: peak 19 on all four phases, after=64FF89AB at phase 0 (RECIPE.toml).

	Every module step that moves the machine must end with end(): the snapshot roll
	restored through the same funnel and BASE reloaded, so the tour's convergence
	gate can pair the module's last mover with `load slot 0` (the same hash), and
	the steps after the module see the roll they expect.

	ASYNC: a load or a run stops the machine on the emulator thread. begin(),
	runToStop(), reloadBase() and end() ARM; settled() is the poll (use it as the
	step's verify with a maxWaitMs). Facts (baseFrame/baseHash) are read the first
	time settled() is true after begin().
*/
namespace roll {
namespace intent {

bool ready(std::string& why);		//!< frame >= 120, slot 0 exists, a clip folder is bound
bool begin(const char *who);		//!< the ceremony: pause, WRITE-authoring, snapshot the roll, reload BASE (arms an async stop)
bool settled();						//!< polled: the machine is stopped (oracle::machineStopped); records base facts once
u32  baseFrame();
u32  baseHash();
u32  machineHash();					//!< oracle::machineHash() - valid only while settled()

bool comboLoaded(std::string& why);	//!< dojo:IntentMacro parsed; the window rows are cached
u32  comboLen();					//!< rows in the window
const char *comboName();

bool placeCombo(int phase, int d);	//!< bake from the SNAPSHOT: phase pin at base+1, the window blanked, the rows at t0
bool clearCombo();					//!< bake from the SNAPSHOT: the same window BLANK (the "kill the combo" edit)
u32  t0();							//!< first row of the placed combo (baseFrame + 1 + phase + d)
u32  stopFrame();					//!< t0 + comboLen + 60 run-out

bool runToStop();					//!< step to stopFrame() under fast-forward; poll settled()
u16  peak(int player);				//!< Combo_Meter_Value PEAK since the last runToStop (0 = P1)
//! The peak THE FIXTURE lands (dojo:IntentPeak, staged by the harness from RECIPE.toml's [combo]/[result]
//! combo_peak). `[2026-09-18]` three TUs carried a literal 19 - the harness base's number - and the number
//! sat in a dozen step NAMES the arm tables match on; a fixture change should be one config, not a rewrite.
u16  pinnedPeak();
bool reloadBase();					//!< gui_loadState(0) around the user's slot; poll settled()
bool end();							//!< restore the snapshot roll through the funnel + reloadBase(); poll settled()
const char *lastWhy();

void selfTest();					//!< the pure parts: the marker parse, the window parse, t0 arithmetic

}	// namespace intent
}	// namespace roll
