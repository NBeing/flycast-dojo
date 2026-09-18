/*
	THE FRAME SKIP TEST - sweep a timing, re-run it, and say which one worked.

	`[PORTED 2026-09-14]` from the TAS fork's Frame Skip Test
	(reference/flycast-rr @ edca8915: a ~380-line runner in 13 functions, a
	~60-line state struct and a 353-line window). Second panel ported, chosen
	because its runner is already a standalone state machine behind a public
	step/running boundary, so the logic separates from the UI cleanly - the
	highest UI density of any window in his file, which is another way of
	saying the least tangled logic.

	WHAT IT ACTUALLY DOES, since the name undersells it. MvC2 latches inputs
	every frame but a match skips every 4th, so a combo that works depends on
	WHICH of the four skip phases its first frame lands in - the "works 1 in 4"
	problem. The test inserts k neutral frames at a chosen row to shift the
	whole tail by k, sweeps k over a range, and re-runs each variant from the
	same savestate. Two placements, one mechanism:

	  P = the selection's first row       walk the four skip phases
	  P = a row INSIDE the selection      the gap test: HP, wait k, HK

	Optionally a second axis sweeps a frame-skip pin at a different row, so the
	sweep is Nb frame-skip phases (outer) x Na combo timings (inner), flattened
	to one index n.

	WHY THE MODEL IS IN A HEADER. Everything below is integer arithmetic and one
	map transformation, and all of it is wrong in ways that look right: a shift
	applied when the insert row is after the selection rather than at-or-before
	it, an axis flattened with the outer and inner swapped, two inserts at the
	same row composed twice. None of that needs an emulator to be wrong and none
	of it needs one to be checked, so it lives out here where selfTest() drives
	it with no ImGui, no ROM and no savestate.
*/
#pragma once
#include "types.h"
#include <map>
#include <vector>

namespace roll {
namespace fst {

//! A movie, in the shape `dojo.session_inputs` keeps it: row -> packed inputs.
using Movie = std::map<u32, std::vector<u8>>;

/*
	The sweep's parameters, and every question you can ask about a variant.

	FLATTENED, a INNER and b OUTER. `n` runs 0..Ntot()-1 with the combo axis
	moving fastest, so consecutive variants differ by one combo frame and the
	frame-skip pin only moves once per row of the sweep. That ordering is not
	cosmetic: the runner saves an outcome savestate per variant into slots 1..N
	in `n` order, so a reader scrubbing those slots walks the combo timings in
	sequence rather than interleaved.
*/
struct Sweep
{
	u32 selLo = 0, selHi = 0;	//!< the captured Piano Roll selection
	u32 P = 0;					//!< COMBO insert row
	int k0 = 0, k1 = 3;			//!< COMBO blanks range (a)
	bool fkOn = false;			//!< the optional second axis
	u32 fkRow = 0;				//!< FRAME SKIP pin row - may sit BEFORE the combo row
	int fk0 = 0, fk1 = 3;		//!< FRAME SKIP blanks range (b) - four phases by default

	int Na() const { return k1 >= k0 ? k1 - k0 + 1 : 1; }
	int Nb() const { return fkOn ? (fk1 >= fk0 ? fk1 - fk0 + 1 : 1) : 1; }
	int Ntot() const { return Na() * Nb(); }

	int aOf(int n) const { return k0 + (n % Na()); }
	int bOf(int n) const { return fkOn ? (fk0 + (n / Na())) : 0; }

	/*
		WHERE THE SELECTION LANDS for variant n, with both inserts applied.

		AT-OR-BEFORE, NOT BEFORE. An insert at exactly the row you are asking
		about pushes that row down - the new blank frames take its place and it
		moves after them. Writing `<` here instead of `<=` puts the reported
		sequence row one short for the commonest case of all, P == selLo, which
		is the plain skip-phase test.
	*/
	u32 seqRowOf(int n) const
	{
		u32 r = selLo;
		if (fkOn && fkRow <= selLo)
			r += (u32)bOf(n);
		if (P <= selLo)
			r += (u32)aOf(n);
		return r;
	}

	u32 endRowOf(int n) const
	{
		u32 r = selHi;
		if (fkOn && fkRow <= selHi)
			r += (u32)bOf(n);
		if (P <= selHi)
			r += (u32)aOf(n);
		return r;
	}
};

/*
	Put a sweep into a state the rest of this can trust: ranges the right way
	round, the combo row inside the selection, the pin at or before it.

	A FUNCTION RATHER THAN CHECKS SCATTERED THROUGH THE UI, which is where the
	fork does it - inside `fstGenerate`, so the window can hold and display an
	invalid sweep right up until the moment somebody presses the button. Here
	the normalisation is the thing being tested.
*/
void normalize(Sweep& s);

/*
	Build variant `n` from the ORIGINAL movie - never from the previous variant.

	EVERY VARIANT IS BUILT FROM THE SNAPSHOT, which is the rule that makes the
	sweep mean anything: a variant composed on top of its predecessor
	accumulates every earlier insert, so variant 3 of a 0..3 sweep would carry
	six extra frames rather than three, and the result table would still look
	perfectly plausible.

	Both inserts are PURE INSERTS composed in original coordinates - the tail
	shifts with them, "as if a frame were removed" - and two inserts landing on
	the same row merge into one run rather than being applied twice.

	Returns false only for an out-of-range `n`.
*/
bool bakeEdit(const Movie& original, const Sweep& s, int n, Movie& out);

// Arm and start a k0..k1 sweep at P = selLo over [selLo, selHi] from slot 0 - armFixedSweep's
// body with the rows chosen by the caller (the Surface Tour's intent module sweeps the
// placed combo window). Same ceremony, same generate()/runStart(); false with st.why set.
bool armSweepOver(const char *who, u32 selLo, u32 selHi, int k0, int k1, int settle, std::string& why);
// Variant n's peaks after a sweep; false when n did not run.
bool resultOf(int n, u16& peak1, u16& peak2);

/*
	DRIVE THE SWEEP. Called once per frame from mainui, outside the ImGui frame
	and outside the emulation loop - the same point gui_loadState() and
	gui_open_step() are called from, which is what makes reloading a state and
	stepping to a stop frame safe here.

	NOT A REGISTRY HOOK, deliberately. core/rend/panel.h refuses a per-frame
	`tick` field and says why: a sweep runs with its window shut, and giving the
	registry a hook for that would make it a scheduler. "A panel that needs to
	run while closed is not a panel with an extra field; it is a feature with a
	panel attached, and the feature keeps its own tick." This is that tick.
*/
void tick();

//! Take the live Piano Roll selection into the test, and show the panel.
bool captureSelection();

//! Gated on `dojo:PanelSelfTest`. One line per claim; no ROM, no frame.
void selfTest();

}	// namespace fst

//! Registers the Frame Skip Test panel with the registry. Idempotent.
void registerFrameSkipTestPanel();

//! True while the automated sweep is driving. `gui.cpp` gates the base-save
//! re-link on this, so it is declared where both can see it.
bool frameSkipTestRunning();


}	// namespace roll
