#pragma once
#include "types.h"

/*
	THE COMBO HUNT - find, from a base the machine can reach deterministically, an
	input and a timing where a hit CONNECTS, and say so in a line a harness pins.

	`[2026-09-17]` the audit found NOTHING on this machine that lands a combo: no
	clip, state, macro or manifest records the combo byte >= 1, and David's own
	notes say his PASS "verifies the infrastructure, not that hits connect". So the
	fixture is BUILT, not found - and it is emuapi's own "programmatic exploration"
	loop, verbatim (emuapi/ARCHITECTURE.md): save the base; for each candidate:
	load, press, step, read. Nothing new in shape.

	  base       : slot 0 of the bound clip (the surface tour's), or - preferred - a
	               savestate-free base reached by David's DC-native `fastVS` boot
	               snippet (scripts/fixtures/mvc2/snippets), which sidesteps V48/V49
	               round-trip, RTC and ROM identity at once
	  candidates : David's marker-bracketed `Combo_Dhalsim97_OK` window first (a
	               human-graded, PS2-converted macro - a CANDIDATE, never a fixture
	               until observed here), then single buttons x delay
	  oracle     : tas_mvc2::comboPeak / peekCombo (Combo_Meter_HitsToOpponent, the
	               DC-verified byte), resolved BY NAME through SPREADSHEET.json
	  phase      : MvC2 skips every 4th frame; a combo straddling a skip boundary
	               connects on ONE of four phases (David measured "~1/4 of the
	               time"). A fixture that does not pin its phase is ~75% flaky, so
	               every candidate is swept over the four phases and the winning
	               phase is part of the result.
	  ceremony   : the FST's armFixedSweep shape - pause-for-checkout, WRITE-mode
	               authoring with macro_armed, gui_step_frames, back to Paused - and
	               oracle::machineHash only while stopped.

	RESULT GRAMMAR (the harness pins these, and RECIPE.toml records them):
	  COMBO HUNT: candidate <name> phase=<0-3> d=<delay> peak=<n> hash=<machine> frames=<n>
	  COMBO HUNT RESULT: found=yes|no candidate=<name> phase=<p> d=<d> peak=<n> base=<hash> after=<hash> ...
	Two hashes on purpose: the picture can be identical while the state is not.

	dojo:ComboHunt=<mode> arms it (off by default). A read-only probe: it writes
	nothing outside the bound clip folder and restores the original movie.
*/
namespace roll {
namespace combohunt {

void tick();		//!< mainui.cpp, beside the other probes; no-op unless dojo:ComboHunt
void selfTest();	//!< dojo:PanelSelfTest - the pure parts (candidate table, phase math)

}	// namespace combohunt
}	// namespace roll
