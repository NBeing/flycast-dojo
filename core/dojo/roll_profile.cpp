#include "roll_profile.h"
#include "roll_host.h"
#include "input/gamepad.h"
#include "tasmacro.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <set>

/*
	The profile registry, and the one profile that exists today.

	MARVEL VS CAPCOM 2 IS ELEVEN STRINGS AND AN ORDERING. Everything else in
	this table is Dreamcast hardware: DC_DPAD_*, DC_BTN_*, and the two analog
	trigger channels. "LP/HP/LK/HK" are what MvC2 calls four buttons the pad
	already has, and "A1/A2" are what it calls the triggers.

	That is the whole of the game coupling the piano roll has. Another fighting
	game on this hardware is a different set of labels over the same bits; a
	different system is a different bit set under the same structure. Neither
	needs the roll to change.
*/
namespace roll
{

static const Column mvc2Cols[] = {
	{ "^",  DC_DPAD_UP,    -1, tas_macro::CANON_UP    },
	{ "v",  DC_DPAD_DOWN,  -1, tas_macro::CANON_DOWN  },
	{ "<",  DC_DPAD_LEFT,  -1, tas_macro::CANON_LEFT  },
	{ ">",  DC_DPAD_RIGHT, -1, tas_macro::CANON_RIGHT },
	{ "LP", DC_BTN_X,      -1, tas_macro::CANON_LP    },
	{ "HP", DC_BTN_Y,      -1, tas_macro::CANON_HP    },
	{ "LK", DC_BTN_A,      -1, tas_macro::CANON_LK    },
	{ "HK", DC_BTN_B,      -1, tas_macro::CANON_HK    },
	// The trigger channel has no plain bit: a trigger reads as a byte, and the
	// GGPO-era kcode carried it as a bit. Both spellings mean pressed, which is
	// why `bit` is 0 here and `trigger` names the channel instead.
	{ "A1", 0,              0, tas_macro::CANON_A1    },
	{ "A2", 0,              1, tas_macro::CANON_A2    },
	{ "ST", DC_BTN_START,  -1, tas_macro::CANON_START },
};

// The stick, and what it cannot do. A Dreamcast pad fact, not a Marvel one -
// it sits here because this is the layer that owns the bit vocabulary, and the
// next profile on the same hardware will repeat it rather than inherit it
// silently.
static const Opposed mvc2Opposed[] = {
	{ tas_macro::CANON_UP,   tas_macro::CANON_DOWN  },
	{ tas_macro::CANON_LEFT, tas_macro::CANON_RIGHT },
};

static const Profile mvc2{ "Marvel vs Capcom 2", mvc2Cols, (int)std::size(mvc2Cols),
		tas_macro::CANON_UP | tas_macro::CANON_DOWN
			| tas_macro::CANON_LEFT | tas_macro::CANON_RIGHT,
		mvc2Opposed, (int)std::size(mvc2Opposed),
		tas_macro::CANON_UP, tas_macro::CANON_DOWN,
		tas_macro::CANON_LEFT, tas_macro::CANON_RIGHT };

// Defaults to the only profile there is. When a second one exists this becomes
// a lookup, and the DEFAULT should probably become a plain Dreamcast pad with
// hardware names - a roll that mislabels buttons is better than one that
// refuses to draw.
static const Profile *current = &mvc2;

bool cellHas(Cell c, const Column& col) { return (c & col.canon) != 0; }

Cell cellWith(Cell c, const Column& col, bool on)
{
	return on ? (c | col.canon) : (c & ~(Cell)col.canon);
}

Cell cellAll()
{
	const Profile& p = profile();
	Cell m = 0;
	for (int i = 0; i < p.count; i++)
		m |= p.cols[i].canon;
	return m;
}

//! Opposed inputs cancel: BOTH go, rather than one winning. A pad that reported
//! left and right at once would be a broken pad, and picking a winner here
//! would invent an input the user never gave.
static Cell socdClean(Cell c)
{
	const Profile& p = profile();
	for (int i = 0; i < p.opposedCount; i++)
	{
		const Cell pair = (Cell)p.opposed[i].a | (Cell)p.opposed[i].b;
		if ((c & pair) == pair)
			c &= ~pair;
	}
	return c;
}

Cell cellApply(Cell have, Cell bits, Cell mask)
{
	const Profile& p = profile();
	Cell out = (have & ~mask) | (bits & mask);

	// A DIRECTION REPLACES, A BUTTON ACCUMULATES, and the difference is decided
	// by whether this write INTRODUCED a direction - not by whether the result
	// has one. Painting LEFT onto a frame holding RIGHT must give LEFT, and
	// CLEARING left must not resurrect right.
	const Cell incoming = bits & mask & p.dirs;
	if (incoming != 0)
		out = (out & ~p.dirs) | socdClean(incoming);
	else
		out = (out & ~p.dirs) | socdClean(out & p.dirs);
	return out;
}

bool pressed(const Column& c, u32 kcode, u8 trigL, u8 trigR, u32 trigLBit, u32 trigRBit)
{
	if (c.trigger < 0)
		return (kcode & c.bit) != 0;
	// A trigger reads as a BYTE, and the GGPO-era kcode also carried it as a
	// bit. Both spellings mean pressed; accepting only one silently loses half
	// the recordings.
	const u8  b    = c.trigger == 0 ? trigL : trigR;
	const u32 tbit = c.trigger == 0 ? trigLBit : trigRBit;
	return b >= 0x20 || (kcode & tbit) != 0;
}

const Profile& profile()             { return *current; }
void setProfile(const Profile& p)    { current = &p; }

static Host *theHost = nullptr;
Host *host()                         { return theHost; }
void setHost(Host *h)                { theHost = h; }

/*
	SELF-TEST. A profile is DATA, and the failure modes of data are duplicates,
	holes and a mislabelled column - none of which the compiler can see.

	The claim that matters is the LAST one. Every other check would still pass if
	the table were a verbatim copy of the game layer into the system layer; only
	asking whether a column can be reached by its canonical id proves the two
	layers are actually separable, which is the entire reason this file exists.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLL SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const Profile& p = profile();
	claim("a profile is in force", p.cols != nullptr && p.count > 0);

	bool labelled = true;
	for (int i = 0; i < p.count; i++)
		if (p.cols[i].label == nullptr || p.cols[i].label[0] == '\0') labelled = false;
	claim("every column has a label", labelled);

	// A duplicate canon id silently merges two columns everywhere text notation
	// or a macro is involved - the roll would still draw both and edit one.
	std::set<u16> canon;
	bool uniqueCanon = true;
	for (int i = 0; i < p.count; i++)
		if (!canon.insert(p.cols[i].canon).second) uniqueCanon = false;
	claim("canonical ids are unique", uniqueCanon);

	// Every column is EITHER a plain bit or a trigger channel, never both and
	// never neither - a column that is neither can never be pressed.
	bool addressable = true;
	for (int i = 0; i < p.count; i++)
	{
		const bool isBit = p.cols[i].bit != 0;
		const bool isTrig = p.cols[i].trigger >= 0;
		if (isBit == isTrig) addressable = false;
	}
	claim("every column is a bit XOR a trigger channel", addressable);

	// THE SEAM ITSELF: a column is reachable by canonical id without knowing any
	// label. This is what lets a different game supply different names.
	int found = -1;
	for (int i = 0; i < p.count; i++)
		if (p.cols[i].canon == tas_macro::CANON_START) found = i;
	claim("a column is reachable by canon id, with no label knowledge", found >= 0);

	// The host is separately installable, and its absence is a legible state
	// rather than a crash. Written first as claim(..., true) - a check that
	// cannot fail, which is worth less than no check at all because it reports
	// a pass. It now exercises the registry.
	Host *saved = host();
	setHost(nullptr);
	const bool absentIsNull = host() == nullptr;
	struct Probe : Host {
		bool slotStale(int) const override { return false; }
		u32  slotFrame(int) const override { return 7; }
		void framesToSlots(std::map<u32, std::vector<int>>&) const override {}
	} probe;
	setHost(&probe);
	const bool installed = host() == &probe && host()->slotFrame(0) == 7;
	setHost(saved);
	claim("no host reads as absent, and an installed one answers",
			absentIsNull && installed && host() == saved);

	NOTICE_LOG(RENDERER, "ROLL SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
