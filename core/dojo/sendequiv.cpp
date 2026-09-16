#include "dojo.h"
#include "roll_host.h"
#include "rend/gui.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "input/gamepad.h"
#include "serialize.h"
#include "log/LogManager.h"
#include <xxhash.h>
#include <map>
#include <vector>
#include <string>

/*
	JOURNEY 3 - RECORD vs SEND EQUIVALENCE, by STATE HASH (user, 2026-09-15).

	The tool authors a movie two ways: you RECORD inputs by playing them, or you
	SEND them (the Input Sender -> Dojo::InjectInput). This proves the machine
	cannot tell the two apart: the same input, authored either way, walks the same
	state path - and a one-frame misalignment is detected.

	THE OBVIOUS TEST IS A TRAP. Both a byte-compare of the movie AND a naive
	"author both ways, diff" are circular, because a correct InjectInput MUST
	produce the recorded bytes - that is what equivalence MEANS - so any correct
	build makes the two byte-identical and the comparison proves nothing about
	whether the input was placed right or ever reached the guest.

	THE ORACLE AND THE CODE UNDER TEST SHARE NO CODE. Three arms run from ONE base
	state, each via the FST's proven bake-then-step path (where a stepped WRITE
	session consumes edited session_inputs into the guest):

	  - NEUTRAL  : the window is empty. Its state hash is the baseline.
	  - RECORD   : the window holds A pressed, written with the raw DC_BTN_A bit -
	               this arm NEVER calls InjectInput, so it is the independent
	               reference for "what a recording of A stores".
	  - SEND     : the window is authored by Dojo::InjectInput(frame, canonA) - the
	               full canon -> tasWriteCanonIntoFrame contract, the code the
	               Input Sender runs.

	The claims (scripts/sendequivtest.sh owns them):
	  - SEND == RECORD  : InjectInput lands the record-equivalent state. Non-circular
	                      because RECORD is built without InjectInput.
	  - RECORD != NEUTRAL: the A press actually did something - non-vacuity.
	  - (sabotage) SEND injected one frame late != RECORD: frame alignment matters
	                      and is observable. The --self-test arm sets SendEquivProbe=shift.

	Observed by STATE HASH, not movie bytes: a hash match also proves the injected
	input REPLAYED (reached the guest at the right frame), which bytes cannot show.
	This is the DAG-safe vocabulary - true of any correct engine, folder or tree.

	A read-only probe otherwise: off unless dojo:SendEquivProbe is set.
*/
namespace roll {
namespace sendequiv {

// The canon action code for the A (jab) button: canon bit 6 -> DC_BTN_A, per
// tasWriteCanonIntoFrame. A single well-defined button keeps the fixture legible.
static const u16 CANON_A = (u16)(1 << 6);

using Movie = std::map<u32, std::vector<u8>>;

enum class Kind { Neutral, Record, Send };

// One movie row (both players), as each arm authors the window. NEUTRAL is all
// zero; RECORD sets P1's DC_BTN_A directly (no InjectInput); SEND is left to
// InjectInput at runtime, so its authored form here is NEUTRAL too and the inject
// overwrites it. Pure - selfTest() checks the RECORD form carries A and nothing
// else, and that NEUTRAL carries nothing.
static std::vector<u8> windowRow(Kind k)
{
	std::vector<u8> row(sizeof(FrameInputs) * MAX_PLAYERS, 0);
	if (k == Kind::Record)
		((FrameInputs *)row.data())->kcode |= DC_BTN_A;	// P1, the raw bit - the reference
	return row;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "SENDEQUIV SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	const std::vector<u8> neu = windowRow(Kind::Neutral);
	const std::vector<u8> rec = windowRow(Kind::Record);
	claim("a row is both players wide", neu.size() == sizeof(FrameInputs) * MAX_PLAYERS);
	bool neuEmpty = true;
	for (u8 v : neu) if (v != 0) neuEmpty = false;
	claim("the NEUTRAL window is empty", neuEmpty);
	const FrameInputs *p1 = (const FrameInputs *)rec.data();
	const FrameInputs *p2 = (const FrameInputs *)(rec.data() + sizeof(FrameInputs));
	claim("the RECORD form presses A on P1", (p1->kcode & DC_BTN_A) != 0);
	claim("...and only A (no other button)", (p1->kcode & ~(u32)DC_BTN_A) == 0);
	claim("...and leaves P2 neutral", p2->kcode == 0);
	// The reference must NOT be the send path: CANON_A is the A bit, distinct from
	// the raw DC_BTN_A value, so the two arms genuinely encode by different routes.
	claim("canon A and the raw A bit are different numbers", (u32)CANON_A != (u32)DC_BTN_A);
	NOTICE_LOG(RENDERER, "SENDEQUIV SELFTEST: %d passed, %d failed", pass, fail);
}

// --------------------------------------------------------------------------------------
// THE PROBE. dojo:SendEquivProbe = yes | shift. A multi-phase state machine, like
// the FST's - each arm reloads the base, authors the window, steps to a settle
// frame, and hashes the machine. It cannot be a loop: a step hands control back
// so frames actually happen.
// --------------------------------------------------------------------------------------

static struct State
{
	bool armed = false;
	bool done = false;
	bool shift = false;			//!< the sabotage: SEND injects one frame late
	Movie original;
	u32 F0 = 0, F1 = 0;			//!< the authored window [F0, F1)
	u32 stopFrame = 0;
	int settle = 6;
	int arm = 0;				//!< 0 neutral, 1 record, 2 send
	int phase = 0;				//!< 0 load+author+step, 1 wait, 2 hash
	double startedAt = 0;
	u32 hash[3] = { 0, 0, 0 };
} st;

static u32 hashNow()
{
	Serializer sizer;
	dc_serialize(sizer);
	std::vector<u8> buf(sizer.size());
	Serializer ser(buf.data(), buf.size());
	dc_serialize(ser);
	// Same-build A/B/N compare, so the exact trailer the lua hash adds is not
	// needed - only that this is a consistent function of the machine.
	return (u32)XXH32(buf.data(), ser.size(), 0);
}

static u32 baseFrameOf(int slot)
{
	SlotView v;
	if (host() != nullptr && host()->slotView(slot, v) && v.exists && v.haveFrame)
		return v.frame;
	return 0;
}

static bool slotExists(int slot)
{
	SlotView v;
	return host() != nullptr && host()->slotView(slot, v) && v.exists;
}

// Build the window movie for an arm and put it in session_inputs. For SEND the
// window is authored by InjectInput AFTER the neutral resize, so the code the
// Input Sender runs is what places the bytes.
static void authorArm(int arm)
{
	Movie m = st.original;
	const Kind k = arm == 1 ? Kind::Record : Kind::Neutral;	// SEND resizes neutral, then injects
	for (u32 f = st.F0; f < st.F1; f++)
		m[f] = windowRow(k);
	dojo.ApplyEditResize(m, "sendequiv");
	dojo.stale_tail_from = ~0u;
	dojo.macro_armed = true;
	if (arm == 2)
	{
		const u32 base = st.shift ? st.F0 + 1 : st.F0;	// the sabotage lands the run one frame late
		for (u32 f = st.F0; f < st.F1; f++)
			dojo.InjectInput(base + (f - st.F0), CANON_A, 0, 1);
	}
}

static void arm()
{
	if (st.armed || st.done)
		return;
	const std::string mode = cfgLoadStr("dojo", "SendEquivProbe", "");
	if (mode.empty() || mode == "no")
		return;
	if (dojo.frame_number.load() < 120)
		return;
	if (!slotExists(0))
		return;
	st.shift = (mode == "shift");
	config::SavestateSlot.set(0);
	cfgSetVirtual("config", "Dreamcast.SavestateSlot", "0");
	gui_pause_for_checkout();
	dojo.play_match = false;			// WRITE - a stepped WRITE session consumes session_inputs
	const u32 bf = baseFrameOf(0);
	st.original = dojo.session_inputs;
	st.F0 = bf + 5;
	st.F1 = st.F0 + 8;					// an 8-frame press
	st.stopFrame = st.F1 + (u32)st.settle;
	st.arm = 0;
	st.phase = 0;
	st.armed = true;
	st.startedAt = os_GetSeconds();
	NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: armed window [%u,%u) stop=%u base=slot0@%u shift=%s",
			st.F0, st.F1, st.stopFrame, bf, st.shift ? "yes" : "no");
}

void tick()
{
	arm();
	if (!st.armed || st.done)
		return;
	const double now = os_GetSeconds();
	switch (st.phase)
	{
	case 0:		// reload base, author this arm's window, step to the settle frame
		if (gui_state != GuiState::Paused)
		{
			if (now - st.startedAt > 5.0)
			{
				NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: aborted - not paused");
				st.done = true;
			}
			return;
		}
		gui_loadState();		// slot 0, set above and kept
		if (gui_state != GuiState::Paused)
		{
			NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: aborted - base state did not load");
			st.done = true;
			return;
		}
		authorArm(st.arm);
		{
			const u32 fr = dojo.frame_number.load();
			gui_step_frames(st.stopFrame > fr ? (int)(st.stopFrame - fr) : 1);
		}
		st.phase = 1;
		st.startedAt = now;
		return;

	case 1:		// wait for the step to reach the settle frame
		if (gui_state == GuiState::Paused)
		{
			if (dojo.frame_number.load() >= st.stopFrame)
				st.phase = 2;
			else if (now - st.startedAt > 2.0)
			{
				NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: aborted - the step did not advance (steppable?)");
				st.done = true;
			}
		}
		else if (now - st.startedAt > 120.0)
		{
			NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: aborted - timeout");
			st.done = true;
		}
		return;

	case 2:		// hash the machine and move to the next arm
		st.hash[st.arm] = hashNow();
		NOTICE_LOG(NETWORK, "SENDEQUIV PROBE: arm %d (%s) hash=%08x @%u",
				st.arm, st.arm == 0 ? "neutral" : st.arm == 1 ? "record" : "send",
				st.hash[st.arm], dojo.frame_number.load());
		st.arm++;
		if (st.arm < 3)
		{
			st.phase = 0;
			st.startedAt = now;
			return;
		}
		{
			const bool match = st.hash[1] == st.hash[2];		// send == record
			const bool moved = st.hash[1] != st.hash[0];		// the A press did something
			NOTICE_LOG(NETWORK, "SENDEQUIV RESULT: neutral=%08x record=%08x send=%08x "
					"match=%s moved=%s shift=%s",
					st.hash[0], st.hash[1], st.hash[2],
					match ? "yes" : "no", moved ? "yes" : "no", st.shift ? "yes" : "no");
		}
		// Put the original movie back - a probe must not leave the window edited.
		dojo.ApplyEditResize(st.original, "sendequiv restore");
		dojo.stale_tail_from = ~0u;
		st.done = true;
		return;
	}
}

}	// namespace sendequiv
}	// namespace roll
