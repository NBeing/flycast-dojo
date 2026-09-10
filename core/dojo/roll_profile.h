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

/*
	A CELL: one lane's input on one frame, as a bag of canonical bits.

	OPAQUE TO EVERY TOOL. `[MEASURED 2026-09-09]` docs/ROLL-EDIT-MODEL.md - the
	fork's 37 edit tools split into ~25 generic row/column operations, three
	profile chokepoints, and ~9 generic bodies with ONE profile-shaped payload
	threaded through them. That payload is always this: a per-lane word. Because
	the type system never named it, mash, fill, brush and paint are the same loop
	written four times, drifted four ways.

	So the tools take Cells and never look inside. Only this file does.
*/
using Cell = u32;

//! Inputs a stick cannot report together. Data, not a rule in code, because
//! "up and down cancel" is a fact about a control, not about arithmetic.
struct Opposed { u16 a, b; };

struct Profile
{
	const char   *name;	// for the UI and for saying which profile is loaded
	const Column *cols;
	int           count;

	// THE DIRECTION GROUP, which behaves differently from buttons and has to,
	// because a stick reports ONE direction while buttons accumulate. Writing a
	// direction REPLACES whatever direction was there; writing a button ORs.
	// Kept as data so a second profile - another pad, another system - states
	// its own grouping instead of inheriting a Dreamcast's.
	Cell            dirs = 0;
	const Opposed  *opposed = nullptr;
	int             opposedCount = 0;
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

// ---- CELLS ----------------------------------------------------------------

//! Is this column set in this cell?
bool cellHas(Cell c, const Column& col);

//! One column set or cleared, nothing else touched.
Cell cellWith(Cell c, const Column& col, bool on);

/*
	THE WRITE RULE, and the only place in the roll that knows what an input
	MEANS. `bits` within `mask` replace what `have` held; everything outside
	`mask` is untouched. That single primitive covers all four uses the fork
	spells four ways:

	  paint one column on     mask = that column,  bits = that column
	  paint one column off    mask = that column,  bits = 0
	  stamp, replacing        mask = every bit,    bits = the pattern frame
	  overdub / merge         mask = the pattern frame's bits, bits = the same

	MERGE IS NOT A FLAG HERE. It is what a mask of only the bits being added
	means. Making it a flag is how the fork ended up with a brush that ZEROES
	the rows it skips in one mode and leaves them alone in the other - the same
	gesture, opposite destructiveness, decided by a switch in another panel.

	Directions do not accumulate: if the write introduces one, it wins outright,
	and an opposed pair cannot survive whatever produced it.
*/
Cell cellApply(Cell have, Cell bits, Cell mask);

//! Every canon bit this profile models. The mask a full replace uses - and the
//! bits a codec may overwrite, so anything the profile does NOT model survives
//! a round trip.
Cell cellAll();

const Profile& profile();
void setProfile(const Profile& p);

// Runs under dojo:PanelSelfTest, like the other seams in this tree.
void selfTest();

// Registers the Piano Roll panel with the registry. Idempotent.
void registerPanel();

}	// namespace roll
