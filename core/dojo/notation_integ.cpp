#include "dojo.h"
#include "roll_host.h"
#include "roll_notation.h"
#include "roll_profile.h"
#include "tasmacro.h"
#include "input/gamepad.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <string>
#include <vector>

/*
	NOTATION INTEGRATION - the translation framework delivers the input it names.

	roll_notation is a TRANSLATION FRAMEWORK: the Profile is the language (a game's
	labels over canonical bits), Cell is the interlingua (canon), parsePattern is
	the decoder and renderCell the encoder. notationSelfTest proves text<->Cell
	round-trips. But that is only HALF the loop: the OTHER half is Cell(canon) ->
	the hardware bits the guest actually receives, and those two halves live in two
	files that agree only by hand -

	  roll_profile.cpp : declares  LP -> { DC_BTN_X, CANON_LP }
	  dojo.cpp         : tasWriteCanonIntoFrame maps canon bit 4 -> DC_BTN_X

	Nothing tested that they still agree. Swap one entry and typing "LK" silently
	presses HK - the mistranslation a translations framework exists to prevent.

	This closes the loop through THREE independent components: for every column the
	profile names, parsePattern(label) -> canon (the notation decoder) -> the REAL
	packet encoder (Dojo::InjectInput -> tasWriteCanonIntoFrame) -> the frame the
	guest consumes -> and the profile's OWN pressed() reads back EXACTLY that column
	and no other. Parser, encoder and profile are three separate authors of the same
	fact; if they agree, the translation is faithful end to end.

	A read-only probe (dojo:NotationProbe=yes|scramble), off by default. scramble
	corrupts the canon between decode and encode, so a faithful build must then read
	back the WRONG button - the sabotage that proves the check detects mistranslation.
*/
namespace roll {
namespace notation {

static const u32 PROBE_FRAME = 500000;	//!< a frame far past any real movie

//! Read back the P1 packet InjectInput wrote at PROBE_FRAME.
static bool readFrame(FrameInputs& out)
{
	const auto it = dojo.session_inputs.find(PROBE_FRAME);
	if (it == dojo.session_inputs.end() || it->second.size() < sizeof(FrameInputs))
		return false;
	out = *(const FrameInputs *)it->second.data();
	return true;
}

void probeTick()
{
	static bool done = false;
	static int waited = 0;
	if (done)
		return;
	const std::string mode = cfgLoadStr("dojo", "NotationProbe", "");
	if (mode.empty() || mode == "no")
		return;
	if (++waited < 60)			// let the dojo object and profile settle
		return;
	done = true;
	const bool scramble = (mode == "scramble");
	const Profile& prof = profile();

	int checked = 0, faithful = 0;
	std::string bad;
	for (int i = 0; i < prof.count; i++)
	{
		const Column& c = prof.cols[i];
		std::vector<Cell> cells;
		std::string err;
		// DECODE: the notation parser turns the label into canon.
		if (!parsePattern(c.label, cells, err) || cells.size() != 1)
		{
			bad += std::string(c.label) + "(parse) ";
			continue;
		}
		u16 canon = (u16)cells[0];
		// The sabotage: corrupt the canon between decode and encode. A faithful
		// build then reads back the wrong button (or a bleed), so faithful drops.
		if (scramble)
			canon = (u16)(canon ^ (u16)tas_macro::CANON_LP);

		// ENCODE: the REAL packet path the Input Sender / macros run.
		dojo.InjectInput(PROBE_FRAME, canon, 0, 1);
		FrameInputs fi;
		if (!readFrame(fi))
		{
			bad += std::string(c.label) + "(noinj) ";
			continue;
		}
		checked++;

		// INTERPRET: the profile's own reader must see EXACTLY this column.
		const bool self = pressed(c, fi.kcode, fi.triggers.l, fi.triggers.r,
				BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT);
		bool othersClean = true;
		for (int j = 0; j < prof.count; j++)
		{
			if (j == i)
				continue;
			// A column that shares this one's canon is not a bleed (none do in
			// MvC2, but a profile could alias); compare by canon, not identity.
			if (prof.cols[j].canon == c.canon)
				continue;
			if (pressed(prof.cols[j], fi.kcode, fi.triggers.l, fi.triggers.r,
					BTN_TRIGGER_LEFT, BTN_TRIGGER_RIGHT))
			{
				othersClean = false;
				break;
			}
		}
		if (self && othersClean)
			faithful++;
		else
			bad += std::string(c.label) + (self ? "(bleed)" : "(absent)") + " ";
	}

	NOTICE_LOG(RENDERER, "NOTATION PROBE RESULT: profile=\"%s\" checked=%d faithful=%d scramble=%s bad=[%s]",
			prof.name, checked, faithful, scramble ? "yes" : "no", bad.c_str());
}

}	// namespace notation
}	// namespace roll
