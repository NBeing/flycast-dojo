/*
	THE INPUT VISUALIZER - what was SENT this frame, and what the game READ.

	`[PORTED 2026-09-14]` from the TAS fork's `show_input_visualizer`
	(reference/flycast-rr, pinned at edca8915, ~374 lines). Chosen as the first
	studio panel to port on a measurement rather than a preference: of the 32
	windows in his 26,591-line `dojo_gui.cpp` it has the smallest transitive
	closure of any real window (728 lines over 12 helpers), and - the part that
	decided it - EVERY data symbol it needs already exists in this tree.
	`tas_macro`, `tas_mvc2` and `tas_va2` are byte-identical between the forks,
	`FrameInputs` is byte-identical, and `core/dojo/tas_colors.h` already
	carries every palette token his version reads. It needs no new third-party
	dependency, which four of his other windows do.

	WHY THE DECODE IS IN A HEADER AND NOT INSIDE THE PANEL. The interesting part
	of this tool is not the drawing, it is the CONVENTION: three different
	representations of "a button is pressed" meet here and two of them are
	inverted relative to each other. A panel body cannot be tested - it needs a
	frame - so the conversion lives out here where `selfTest()` can drive it
	with no ImGui, no emulator and no ROM.
*/
#pragma once
#include "types.h"
#include <cstddef>

namespace roll {
namespace inputviz {

/*
	THE THREE CONVENTIONS, written down because getting them confused is the
	defect this tool exists to make visible and is also the defect it shipped:

	  kcode[]            ACTIVE LOW.  bit CLEAR = pressed.  (the pad, live)
	  FrameInputs.kcode  ACTIVE HIGH. bit SET   = pressed.  (the movie, stored
	                     as ~live by dojo's recorder)
	  the game's RAM     ACTIVE HIGH, and a DIFFERENT bit layout entirely -
	                     tas_mvc2::GameInputs, two bytes per player.

	`[SOURCE]` the fork's own comment on the line that fixes it:
	"session_inputs stores ~live (pressed=SET); invert back to the active-LOW
	 (pressed=CLEAR) shape sentK expects - the movie/paused ring was lit for
	 RELEASED buttons".

	So a movie-sourced frame that is NOT re-inverted lights every button that is
	up, which looks like a working visualizer showing a busy player. It is the
	precise shape of bug a green screenshot cannot catch, and it is why the
	inversion has a claim of its own below rather than being a `~` in a loop.
*/

//! Active-LOW test: in `kcode` convention a CLEAR bit means the button is down.
inline bool pressed(u32 kcodeActiveLow, u32 dcBit)
{
	return (kcodeActiveLow & dcBit) == 0;
}

/*
	A trigger is ANALOG in the packet and DIGITAL in the game, so the panel has
	to pick a threshold the game would agree with. 0x20 of 0xFF is the fork's,
	kept rather than retuned: this value decides whether a partially-held
	trigger shows as A1/A2 pressed, and changing it changes what the tool says
	about inputs somebody already recorded.
*/
inline bool triggerPressed(u8 analog)
{
	return analog >= 0x20;
}

/*
	Decode one `dojo.session_inputs` entry - two packed `FrameInputs`, P1 then
	P2 - into the ACTIVE-LOW shape the rest of this panel speaks.

	Returns false and touches nothing when the blob is absent or short, which is
	a real and frequent case rather than an error: during recording the frame
	the playhead just stepped onto has no movie entry yet. The caller decides
	what to show instead; conflating "no entry" with "all buttons released"
	would draw a confident neutral pad for a frame nobody has authored.
*/
bool decodeMovieEntry(const u8 *blob, size_t len, u32 k[2], u8 trigL[2], u8 trigR[2]);

/*
	WHICH SOURCE the pads should read, given the session's mode.

	`[SOURCE]` the fork carries two `[CORRECTED]` bugs here and both are ported
	with it, because both are invisible until you are frame-advancing:

	  1. "The first version fell back to live kcode whenever the current frame
	      had no movie entry yet - which during recording is true on every
	      just-stepped-to frame, so the ring flapped between movie and pad once
	      per step (user-visible as blinking while frame-advancing)."
	  2. `macro_armed` was missing from the gate, "so the pad blanked to the
	      hands-off pad while a macro advanced in READ-WRITE".

	The rule that survives both: the ROLL drives the guest in READ
	(`play_match`) and READ-WRITE (`macro_armed`), so those read the movie.
	Only live WRITE reads the physical pad. While frozen everything reads the
	movie, because the pad has already been consumed and zeroed.
*/
inline bool readsFromMovie(bool playMatch, bool macroArmed, bool frozen)
{
	return playMatch || macroArmed || frozen;
}

//! True when the roll - not the physical pad - is authoring the guest's input.
inline bool rollDrivesGuest(bool playMatch, bool macroArmed)
{
	return playMatch || macroArmed;
}

/*
	WHICH FRAME the pads sit on.

	Not always the playhead, and the difference is the whole reason this is a
	function. While frozen the movie counter has ALREADY advanced past the row
	that just ran, so the frame whose input the guest actually consumed is
	`frame - 1`. When the roll is driving, the playhead cell is the authored
	one and is what the roll cursor and the time table are both showing, so the
	pad must agree with them or the three disagree on screen by one row.
*/
inline u32 padFrame(u32 frame, bool rollDriven, bool frozen)
{
	if (rollDriven && !frozen)
		return frame;
	if (frozen && frame > 0)
		return frame - 1;
	return frame;
}

//! Gated on `dojo:PanelSelfTest`. Prints one line per claim; no ROM, no frame.
void selfTest();

}	// namespace inputviz

//! Registers the Input Visualizer with the panel registry. Idempotent.
void registerInputVizPanel();

}	// namespace roll
