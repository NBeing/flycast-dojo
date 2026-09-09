#pragma once
#include "types.h"

/*
	WHAT AN INPUT COLUMN IS, and what a GAME calls it.

	The piano roll is a grid of frames by inputs. Which inputs exist, and what
	they are named, is the only part of it that belongs to a particular game -
	so it lives here, as DATA, and nothing else in the roll may hardcode it.

	`[MEASURED 2026-09-09]` this is a small seam. Lifting David's
	show_piano_roll() into its own translation unit produced 161 real
	dependencies, of which exactly FOUR are the game coupling: the column table,
	its length, the canonical-id table, and the packet conversion. The other 157
	are the roll's own edit tools, selection, bookmarks and chrome.

	TWO LAYERS, because they change for different reasons:

	  the SYSTEM layer   which bits the hardware has (a Dreamcast pad)
	  the GAME layer     what this game calls them, and in what order

	Marvel vs Capcom 2's "LP/HP/LK/HK" and "A1/A2" are names over plain DC bits -
	DC_BTN_X, DC_BTN_Y, the trigger channels. Another game on the same hardware
	is a different set of names over the same bits; another system is a different
	bit set under the same structure. Neither requires touching the roll.
*/
namespace roll
{

struct Column
{
	const char *label;	// what this input is CALLED here - the game layer
	u32         bit;	// host input bit; 0 when the input is not a plain bit
	int         trigger;	// analog trigger channel: -1 none, 0 left, 1 right
	u16         canon;	// canonical id, for text notation and macros
};

struct Profile
{
	const char   *name;	// for the UI and for saying which profile is loaded
	const Column *cols;
	int           count;
};

// The profile in force. A host sets it once at startup; the roll only reads it.
// Deliberately a function pair rather than a global: a game switch has to be a
// call site somebody can find.
/*
	Is this column pressed, given one frame's raw input?

	Takes PLAIN INTEGERS, not the movie's frame-record struct: the profile
	interprets its own fields and must not depend on how a particular host
	stores a frame. The trigger duality is the host's convention, passed in.
*/
bool pressed(const Column& c, u32 kcode, u8 trigL, u8 trigR, u32 trigLBit, u32 trigRBit);

const Profile& profile();
void setProfile(const Profile& p);

// Runs under dojo:PanelSelfTest, like the other seams in this tree.
void selfTest();

// Registers the Piano Roll panel with the registry. Idempotent.
void registerPanel();

}	// namespace roll
