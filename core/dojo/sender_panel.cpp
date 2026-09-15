#include "ui_text.h"
#include "tas_auto.h"
#include "tasmacro.h"
#include "dojo.h"
#include "roll_profile.h"
#include "roll_notation.h"
#include "roll_host.h"
#include "tas_colors.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <string>
#include <vector>

/*
	THE INPUT SENDER - hold a button, or send a sequence into the running game.

	`[PORTED 2026-09-14]` from the TAS fork's `tasInputSenderWindow` (767 lines),
	but this is a PANEL FOR AN ENGINE WE ALREADY HAD, not a port of his window.

	`[MEASURED 2026-09-14]` WHY IT MATTERS MORE THAN IT LOOKS. `core/dojo/tas_auto.cpp`
	is BYTE-IDENTICAL to his, and `core/dojo/dojo.cpp:2169` consults it every
	single frame - `anyArmed()`, then `overlayCanon()` per player, ORed into the
	guest's input and baked into the movie. The engine is fully wired to the
	emulator.

	And `tas_auto::arm()` - the only function that puts bits into that overlay -
	HAD ZERO CALLERS anywhere in the tree. So `overlayCanon()` always returned 0,
	`anyArmed()` was always false, and the only thing that could ever fire that
	branch was one `playLive()` inside the frameskip-alignment path.

	The hold / auto-fire engine shipped, was documented in detail, ran 60 times a
	second, and could not be armed. That is CLAUDE.md's opening lesson alive in
	the tree: "A clean build is not evidence a feature is wired. SaveStateFrame /
	LoadStateFrame compiled, linked, and were unreachable for several commits."

	So this panel is not a new feature. It is the missing half of one that was
	already paid for.

	WHAT IS DELIBERATELY NOT HIS. His window builds chords by holding Shift and
	clicking a virtual stick-and-buttons cluster, rendered through his
	four-dialect notation codec (`tasNotationCombo`, `tasInputCell`) - the
	~1,411-line island this tree does not have. We have our own smaller
	chokepoint, `roll::parsePattern` / `renderCell`, which the Piano Roll already
	uses, so a sequence is TYPED here rather than clicked. One notation, one
	grammar, one place that knows it.
*/
namespace roll
{
namespace sender
{

/*
	Turn a typed pattern into the per-frame canon words `tas_auto::playLive`
	wants.

	`Cell` IS the canon word. `core/dojo/roll_profile.cpp` builds every column
	from `tas_macro::CANON_*`, so the roll's cell and the engine's input word are
	the same eleven bits and the conversion is a narrowing, not a mapping. That
	is worth asserting rather than assuming - it is exactly the kind of agreement
	that holds until someone adds a twelfth bit.
*/
bool patternToCanon(const std::string& text, std::vector<u16>& out, std::string& err)
{
	out.clear();
	std::vector<Cell> cells;
	if (!parsePattern(text, cells, err))
		return false;
	out.reserve(cells.size());
	for (Cell c : cells)
		out.push_back((u16)c);
	return true;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "SENDER SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	/*
		THE HZ RULE, which the whole hold / auto-fire feature rests on and which
		had NO COVERAGE in this tree. tas_auto.cpp arrived byte-identical from
		the fork and was never asserted here - and a shared engine nobody checks
		is a shared engine whose behaviour is folklore.

		`[SOURCE]` tas_auto.h states it: "hz 0 = off; hz >= 60 = HOLD (pressed
		every frame); hz < 60 = auto-fire (one frame on, 60/hz - 1 off)".
	*/
	claim("hz 0 is off, on every frame",
			!tas_auto::tickOn(0, 0) && !tas_auto::tickOn(0, 1) && !tas_auto::tickOn(0, 37));
	claim("hz 60 is HOLD - pressed on every frame",
			tas_auto::tickOn(60, 0) && tas_auto::tickOn(60, 1) && tas_auto::tickOn(60, 59));
	claim("hz 30 is one frame on, one off",
			tas_auto::tickOn(30, 0) && !tas_auto::tickOn(30, 1)
			&& tas_auto::tickOn(30, 2) && !tas_auto::tickOn(30, 3));
	claim("hz 15 is one frame on, three off",
			tas_auto::tickOn(15, 0) && !tas_auto::tickOn(15, 1)
			&& !tas_auto::tickOn(15, 2) && !tas_auto::tickOn(15, 3)
			&& tas_auto::tickOn(15, 4));
	// THE CONTROL for the three above. A tickOn() that answered true always
	// satisfies "hold", and one that answered on even phases satisfies 30.
	claim("...and 15 is NOT the same pattern as 30",
			tas_auto::tickOn(30, 2) != tas_auto::tickOn(15, 2));

	// ---- the pattern conversion --------------------------------------------------------
	{
		std::vector<u16> out;
		std::string err;
		claim("an empty pattern sends nothing",
				patternToCanon("", out, err) && out.empty());
		claim("one token is one frame",
				patternToCanon("2 3 6", out, err) && out.size() == 3);
		claim("a neutral token is a zero frame",
				patternToCanon("5", out, err) && out.size() == 1 && out[0] == 0);
		claim("a button reaches the engine as its CANON bit",
				patternToCanon("LP", out, err) && out.size() == 1
				&& out[0] == tas_macro::CANON_LP);
		claim("a direction and a button combine in one frame",
				patternToCanon("2LP", out, err) && out.size() == 1
				&& out[0] == (tas_macro::CANON_DOWN | tas_macro::CANON_LP));
		claim("a bad token is refused, and named",
				!patternToCanon("LP ZZZ", out, err) && !err.empty());
	}

	/*
		THE AGREEMENT THIS PANEL DEPENDS ON: the roll's Cell and the engine's
		canon word are the same bits. Asserted rather than assumed, because
		nothing in the type system says so - Cell is a u32 and the engine takes
		a u16, and a silent narrowing is what would hide a twelfth bit.
	*/
	{
		const Profile& p = profile();
		u16 all = 0;
		for (int i = 0; i < p.count; i++)
			all |= p.cols[i].canon;
		claim("every profile column fits in the engine's 11 canon bits",
				(all & ~((1u << tas_auto::CANON_BITS) - 1)) == 0);
		claim("the profile's directions ARE the canon directions",
				p.up == tas_macro::CANON_UP && p.down == tas_macro::CANON_DOWN
				&& p.left == tas_macro::CANON_LEFT && p.right == tas_macro::CANON_RIGHT);
	}

	NOTICE_LOG(RENDERER, "SENDER SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The panel.
// ---------------------------------------------------------------------------------------

static bool senderOpen = false;
static int armHz = 60;					//!< what a NEW arm uses. 60 = hold.
static char patternBuf[256] = "";
static std::string patternErr;

//! The Hz values worth offering: 60 is hold, the rest divide 60 cleanly.
static const int kRates[] = { 60, 30, 20, 15, 12, 10 };

static void drawHoldGrid()
{
	const Profile& p = profile();

	tasText("hold / auto-fire");
	ImGui::SameLine();
	tasTextDisabled("armed inputs are ORed into the guest every frame");

	ImGui::SetNextItemWidth(150.f);
	if (ImGui::BeginCombo("##hz", armHz >= 60 ? "hold" : [&]{
			static char b[24];
			snprintf(b, sizeof(b), "%d Hz", armHz);
			return (const char *)b; }()))
	{
		for (int hz : kRates)
		{
			char label[24];
			if (hz >= 60)
				snprintf(label, sizeof(label), "hold");
			else
				snprintf(label, sizeof(label), "%d Hz", hz);
			if (ImGui::Selectable(label, armHz == hz))
				armHz = hz;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	tasTextDisabled("the rate a NEW arm uses");

	if (!ImGui::BeginTable("##hold", p.count + 1,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
		return;
	tasTableSetupColumn("");
	for (int i = 0; i < p.count; i++)
		tasTableSetupColumn(p.cols[i].label);
	ImGui::TableHeadersRow();

	for (int pl = 0; pl < 2; pl++)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextColored(pl == 1 ? TAS_P2_COL : TAS_P1_COL, "P%d", pl + 1);
		for (int i = 0; i < p.count; i++)
		{
			ImGui::TableSetColumnIndex(i + 1);
			// The canon bit's INDEX is what the engine arms, not the mask -
			// tas_auto::arm takes a bit number 0..10.
			int bit = -1;
			for (int b = 0; b < tas_auto::CANON_BITS; b++)
				if (p.cols[i].canon == (1u << b))
				{
					bit = b;
					break;
				}
			if (bit < 0)
			{
				// A column whose canon is not a single bit cannot be armed.
				// Said, not silently skipped.
				tasTextDisabled("-");
				continue;
			}
			ImGui::PushID(pl * 64 + i);
			const int hz = tas_auto::hzOf(pl, bit);
			const bool on = hz != 0;
			/*
				THE CELL SHOWS ITS RATE, not just on/off. "armed" and "armed at
				10 Hz" are different facts, and a checkbox can only carry the
				first - which is how a turbo left on at the wrong rate becomes
				a mystery desync.
			*/
			char lbl[16];
			if (!on)
				snprintf(lbl, sizeof(lbl), "  ");
			else if (hz >= 60)
				snprintf(lbl, sizeof(lbl), "hold");
			else
				snprintf(lbl, sizeof(lbl), "%d", hz);
			if (on)
				ImGui::PushStyleColor(ImGuiCol_Button,
						ImGui::GetColorU32(tasCol(hz >= 60 ? TAS_ACTIVE : TAS_STAGED, 0.65f)));
			if (ImGui::Button(lbl, ImVec2(34.f, 0)))
				tas_auto::arm(pl, bit, on ? 0 : armHz);		// click toggles
			if (on)
				ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}
	ImGui::EndTable();

	ImGui::BeginDisabled(!tas_auto::anyArmed());
	if (tasButton("clear all"))
	{
		tas_auto::clearAll();
		NOTICE_LOG(RENDERER, "INPUT SENDER: all arms cleared");
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (tas_auto::anyArmed())
		tasTextColored(TAS_ACTIVE_COL, "armed");
	else
		// NOT BLANK. "nothing armed" is the answer that used to be
		// indistinguishable from "this panel does not work".
		tasTextDisabled("nothing armed");
}

static void drawSend()
{
	ImGui::Separator();
	tasText("send a sequence");
	ImGui::SameLine();
	tasTextDisabled("one token per frame, same grammar as the Piano Roll");

	ImGui::SetNextItemWidth(-120.f);
	tasInputTextWithHint("##pattern", "236LP HP _ _", patternBuf, sizeof(patternBuf));
	ImGui::SameLine();

	const bool live = tas_auto::liveActive();
	ImGui::BeginDisabled(live || patternBuf[0] == 0 || dojo.play_match);
	if (tasButton("Send"))
	{
		std::vector<u16> p1;
		patternErr.clear();
		if (!patternToCanon(patternBuf, p1, patternErr))
			NOTICE_LOG(RENDERER, "INPUT SENDER: refused - %s", patternErr.c_str());
		else if (p1.empty())
			patternErr = "nothing to send";
		else
		{
			// P2 gets an empty sequence: this panel sends for P1 only, which is
			// what a one-box pattern can say. Two-player chords are the Piano
			// Roll's job, where there are two lanes to put them in.
			const std::vector<u16> p2;
			tas_auto::playLive(p1, p2, dojo.frame_number.load() + 1);
			NOTICE_LOG(RENDERER, "INPUT SENDER: sent %u frame(s) at %u",
					(unsigned)p1.size(), (unsigned)dojo.frame_number.load() + 1);
		}
	}
	ImGui::EndDisabled();

	if (dojo.play_match)
		// SAID, not shown as a dead button. Sending authors input, which is
		// meaningless while a replay drives the guest.
		tasTextColored(TAS_READ, "READ - playback drives the guest; nothing to send into");
	else if (live)
	{
		tasTextColored(TAS_STAGED, "sending - %u frame(s) left",
				(unsigned)tas_auto::liveRemaining(dojo.frame_number.load()));
		ImGui::SameLine();
		if (tasButton("Stop"))
		{
			tas_auto::stopLive();
			NOTICE_LOG(RENDERER, "INPUT SENDER: send stopped by hand");
		}
	}
	else if (!patternErr.empty())
		tasTextColored(TAS_WRITE, "%s", patternErr.c_str());
}

static void draw()
{
	if (settings.content.fileName.empty())
	{
		tasTextDisabled("No game loaded - nothing to send into.");
		return;
	}
	drawHoldGrid();
	drawSend();
}

}	// namespace sender

void registerSenderPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	/*
		BOTH STREAMS. Unlike the Frame Skip Test, this is operated WHILE the game
		runs - holding a button for the other character is the whole point, and a
		panel that vanished when you unpaused would be useless for it.
	*/
	panels::add({ "sender", "Input Sender", &sender::senderOpen, sender::draw,
			panels::Both, /*persist*/ true, /*defW*/ 560.f, /*defH*/ 260.f });
	NOTICE_LOG(RENDERER, "INPUT SENDER: registered=%s open=%s",
			panels::find("sender") != nullptr ? "yes" : "NO",
			sender::senderOpen ? "yes" : "no");
}

}	// namespace roll
