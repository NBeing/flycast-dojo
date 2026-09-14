#include "input_viz.h"
#include "ui_text.h"
#include "dojo.h"
#include "mvc2.h"
#include "tas_colors.h"
#include "input/gamepad.h"
#include "input/gamepad_device.h"
#include "rend/panel.h"
#include "rend/gui.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

/*
	See input_viz.h for what this is, where it came from and why the decoding
	lives in the header. This file is the panel body and the arm that proves
	the decoding works.
*/
namespace roll {
namespace inputviz {

bool decodeMovieEntry(const u8 *blob, size_t len, u32 k[2], u8 trigL[2], u8 trigR[2])
{
	if (blob == nullptr || len < sizeof(FrameInputs) * 2)
		return false;
	for (int p = 0; p < 2; p++)
	{
		FrameInputs fi;
		// memcpy, not a cast: FrameInputs is #pragma pack(1) and the blob is a
		// std::vector<u8>'s buffer, so a reinterpret_cast here is an unaligned
		// read on the architectures this also builds for.
		memcpy(&fi, blob + p * sizeof(FrameInputs), sizeof(FrameInputs));
		// THE INVERSION. See the header: the movie stores ~live, so pressed is
		// a SET bit there and a CLEAR bit in the shape everything else uses.
		// Without this the rings light for every button that is UP.
		k[p] = ~(u32)fi.kcode;
		trigL[p] = fi.triggers.l;
		trigR[p] = fi.triggers.r;
	}
	return true;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "INPUTVIZ SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- the active-low convention -------------------------------------------------
	claim("a fully released pad reports nothing pressed",
			!pressed(0xFFFFFFFFu, DC_BTN_A) && !pressed(0xFFFFFFFFu, DC_DPAD_UP));
	claim("a CLEAR bit is a PRESSED button",
			pressed(~(u32)DC_BTN_A, DC_BTN_A));
	// THE CONTROL for the two above. A `pressed()` that answered true for
	// everything would satisfy the second claim on its own.
	claim("...and pressing one button does not press its neighbour",
			pressed(~(u32)DC_BTN_A, DC_BTN_A) && !pressed(~(u32)DC_BTN_A, DC_BTN_B));

	// ---- the trigger threshold ------------------------------------------------------
	claim("a trigger below the gate is not pressed", !triggerPressed(0x1F));
	claim("a trigger at the gate is pressed", triggerPressed(0x20));
	claim("a fully held trigger is pressed", triggerPressed(0xFF));

	// ---- decoding a movie entry -----------------------------------------------------
	{
		u32 k[2] = { 0, 0 };
		u8 tl[2] = { 0, 0 }, tr[2] = { 0, 0 };
		claim("a null blob decodes nothing", !decodeMovieEntry(nullptr, 0, k, tl, tr));
		u8 tiny[4] = { 0, 0, 0, 0 };
		claim("a short blob decodes nothing rather than reading past it",
				!decodeMovieEntry(tiny, sizeof(tiny), k, tl, tr));
	}
	{
		/*
			THE CLAIM THIS FILE EXISTS FOR.

			Build a movie entry the way dojo's recorder does - kcode stored as
			~live, so a PRESSED button is a SET bit - and require that what
			comes back out reads as pressed in the active-low shape.

			The negative half is not decoration. `[MEASURED 2026-09-14]`
			dropping the `~` in decodeMovieEntry fails exactly these three and
			nothing else in the file:

			  FAIL  a button the movie recorded as HELD reads as pressed
			  FAIL  ...and a button it did NOT record does not light
			  FAIL  a player who pressed nothing lights nothing

			`[CORRECTED 2026-09-14]` this comment first predicted that only the
			middle one would fail and that the positive claim would survive.
			Wrong, and worth keeping: the prediction was reasoning, the list
			above is the run. That inverted-ring bug shipped in the fork, and on
			screen it looks like a visualizer working hard.
		*/
		u8 blob[sizeof(FrameInputs) * 2];
		memset(blob, 0, sizeof(blob));
		FrameInputs fi[2];
		memset(fi, 0, sizeof(fi));
		// P1 holds LP (DC_BTN_X) and UP; P2 holds nothing. Stored active-HIGH.
		fi[0].kcode = (u32)(DC_BTN_X | DC_DPAD_UP);
		fi[0].triggers.l = 0x40;	// A1 held
		fi[0].triggers.r = 0x00;
		fi[1].kcode = 0;
		memcpy(blob, &fi[0], sizeof(FrameInputs));
		memcpy(blob + sizeof(FrameInputs), &fi[1], sizeof(FrameInputs));

		u32 k[2] = { 0, 0 };
		u8 tl[2] = { 0, 0 }, tr[2] = { 0, 0 };
		const bool got = decodeMovieEntry(blob, sizeof(blob), k, tl, tr);
		claim("a well-formed entry decodes", got);
		claim("a button the movie recorded as HELD reads as pressed",
				got && pressed(k[0], DC_BTN_X) && pressed(k[0], DC_DPAD_UP));
		claim("...and a button it did NOT record does not light (the inverted ring)",
				got && !pressed(k[0], DC_BTN_A) && !pressed(k[0], DC_DPAD_DOWN));
		claim("a player who pressed nothing lights nothing",
				got && !pressed(k[1], DC_BTN_X) && !pressed(k[1], DC_DPAD_UP));
		claim("the triggers come back as stored",
				got && triggerPressed(tl[0]) && !triggerPressed(tr[0]));
	}

	// ---- which source the pads read -------------------------------------------------
	claim("READ reads the movie", readsFromMovie(true, false, false));
	claim("READ-WRITE reads the movie too (the blanking-pad bug)",
			readsFromMovie(false, true, false));
	claim("live WRITE reads the physical pad", !readsFromMovie(false, false, false));
	claim("...but a frozen WRITE reads the movie, because the pad is already consumed",
			readsFromMovie(false, false, true));
	claim("the roll drives the guest in READ and READ-WRITE, not in WRITE",
			rollDrivesGuest(true, false) && rollDrivesGuest(false, true)
			&& !rollDrivesGuest(false, false));

	// ---- which frame the pads sit on ------------------------------------------------
	claim("roll-driven and running, the pad is on the playhead cell",
			padFrame(100, /*rollDriven*/ true, /*frozen*/ false) == 100);
	claim("frozen, the pad is on the row that just RAN, not the one next up",
			padFrame(100, true, true) == 99);
	claim("live WRITE sits on the playhead frame while running",
			padFrame(100, false, false) == 100);
	claim("frame 0 frozen does not wrap to 0xFFFFFFFF",
			padFrame(0, false, true) == 0);

	NOTICE_LOG(RENDERER, "INPUTVIZ SELFTEST: %d passed, %d failed", pass, fail);
}

// ---------------------------------------------------------------------------------------
// The panel body. No Begin/End: core/rend/panel.h owns the window, deliberately.
// ---------------------------------------------------------------------------------------

static bool vizOpen = false;

/*
	One control: where its bit lives in the pad, and where the same control
	lives in the game's own two input bytes.

	`[SOURCE]` the game-side layout is `core/dojo/mvc2.h`:
	  byte B: Up=32 Down=16 Left=8 Right=4 X(LP)=2 Y(HP)=1 Start=128
	  byte A: Ltrig(A1)=128 A(LK)=64 B(HK)=32 Rtrig(A2)=16

	`dcBit == 0` marks the two ANALOG controls: the triggers have no pad bit to
	test, so they are read from the analog byte against the threshold instead.
*/
struct Ctl
{
	const char *label;
	u32 dcBit;
	int flagByte;	//!< 0 = the 'a' byte, 1 = the 'b' byte
	u8 flagBit;
};

// Diamond order: up, left, right, down.
static const Ctl kDpad[4] = {
	{ "^", DC_DPAD_UP, 1, 32 }, { "<", DC_DPAD_LEFT, 1, 8 },
	{ ">", DC_DPAD_RIGHT, 1, 4 }, { "v", DC_DPAD_DOWN, 1, 16 },
};
// MvC2 names rather than Dreamcast ones, because that is what the player calls
// them and this panel is read while playing.
static const Ctl kFace[6] = {
	{ "LP", DC_BTN_X, 1, 2 },  { "HP", DC_BTN_Y, 1, 1 },  { "A1", 0, 0, 128 },
	{ "LK", DC_BTN_A, 0, 64 }, { "HK", DC_BTN_B, 0, 32 }, { "A2", 0, 0, 16 },
};
static const Ctl kStart = { "ST", DC_BTN_START, 1, 128 };

static void draw()
{
	if (settings.content.fileName.empty())
	{
		// SAID, not drawn as two empty pads. "No game" and "a game with nobody
		// pressing anything" are different answers and blank circles give
		// neither - the distinction core/dojo/hotkey_panel.cpp also makes.
		tasTextDisabled("No game loaded - nothing to visualise.");
		return;
	}

	const tas_mvc2::GameState gs = tas_mvc2::read();
	const bool frozen = gui_state == GuiState::Paused;
	const float sc = std::max(0.5f, std::min(2.5f,
			cfgLoadInt("dojo", "OverlayScale", 100) / 100.f)) * 1.10f;

	const bool rollDriven = rollDrivesGuest(dojo.play_match, dojo.macro_armed);
	const bool fromMovie = readsFromMovie(dojo.play_match, dojo.macro_armed, frozen);
	const u32 frame = dojo.frame_number.load();
	const u32 vizFrame = padFrame(frame, rollDriven, frozen);

	// SENT: what the emulator handed the guest this frame.
	u32 sentK[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };	// active LOW: all released
	u8 sentL[2] = { 0, 0 }, sentR[2] = { 0, 0 };
	bool haveMovieRow = false;
	if (fromMovie)
	{
		const auto it = dojo.session_inputs.find(vizFrame);
		if (it != dojo.session_inputs.end())
			haveMovieRow = decodeMovieEntry(it->second.data(), it->second.size(),
					sentK, sentL, sentR);
	}
	if (!fromMovie)
	{
		for (int p = 0; p < 2; p++)
		{
			sentK[p] = kcode[p];
			sentL[p] = (u8)(lt[p] >> 8);
			sentR[p] = (u8)(rt[p] >> 8);
		}
	}

	// ---- header: the frame, and who is driving ------------------------------------
	const u32 mlen = (u32)dojo.session_inputs.size();
	if (mlen > 0)
		tasTextColored(TAS_ACTIVE_COL, "Frame %u / %u", frame, mlen);
	else
		tasTextColored(TAS_ACTIVE_COL, "Frame - / -");
	ImGui::SameLine();
	if (dojo.play_match)
		tasTextColored(TAS_READ, "[READ]");
	else if (dojo.macro_armed)
		tasTextColored(TAS_READWRITE, "[READ-WRITE]");
	else
		tasTextColored(TAS_WRITE, "[WRITE]");

	/*
		WHY A ROW CAN BE MISSING, said out loud rather than shown as a neutral
		pad. During recording the cell the playhead just stepped onto has not
		been authored yet, and in READ a miss means the movie has been scrubbed
		past its end. Both are ordinary; a confident blank pad for either is
		the "an absent binding and a broken feature look identical" failure
		this tree keeps paying for.
	*/
	if (fromMovie && !haveMovieRow)
		tasTextColored(TAS_DIM, "no authored input at frame %u", vizFrame);
	else if (!tas_mvc2::mapValidated())
		tasTextDisabled("game read - during a match only");
	else
		tasTextColored(TAS_DIM, "scene %u  skip %u/%u",
				gs.sceneFrame, gs.skipCount, gs.skipRate);
	ImGui::Separator();

	// ---- the pads -------------------------------------------------------------------
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float cell = 26.f * sc;
	const float gap = 6.f * sc;
	const ImU32 colFill[2] = {
		ImGui::GetColorU32(tasCol(TAS_P1, 200 / 255.f)),
		ImGui::GetColorU32(tasCol(TAS_P2, 200 / 255.f)),
	};
	const ImU32 colRing = ImGui::GetColorU32(tasCol(TAS_TEXT, 235 / 255.f));
	const ImU32 colIdle = ImGui::GetColorU32(tasCol(TAS_DIM, 40 / 255.f));
	const ImU32 colText = ImGui::GetColorU32(TAS_TEXT);

	/*
		RING = SENT, FILL = RECEIVED, and keeping them separate is the point of
		the whole tool. The ring is what the emulator handed over; the fill is
		what the game's own RAM says it took. A ring with no fill is a dropped
		input - which is invisible in every other view in this emulator.
	*/
	auto drawCtl = [&](ImVec2 c, const Ctl& ct, int p, u8 fa, u8 fb) {
		const bool sent = ct.dcBit != 0
				? pressed(sentK[p], ct.dcBit)
				: triggerPressed(ct.flagBit == 128 ? sentL[p] : sentR[p]);
		const bool read = ((ct.flagByte == 0 ? fa : fb) & ct.flagBit) != 0;
		const float r = cell * 0.5f;
		dl->AddCircleFilled(c, r - 2.f * sc, read ? colFill[p] : colIdle);
		if (sent)
			dl->AddCircle(c, r, colRing, 0, 2.5f * sc);
		const ImVec2 ts = ImGui::CalcTextSize(ct.label);
		dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), colText, ct.label);
	};

	auto drawPlayer = [&](int p, u8 fa, u8 fb) {
		ImGui::BeginGroup();
		tasTextColored(p == 1 ? TAS_P2_COL : TAS_P1_COL, "P%d", p + 1);
		const ImVec2 o = ImGui::GetCursorScreenPos();
		const float dx = o.x + cell * 1.5f, dy = o.y + cell * 1.5f;
		drawCtl(ImVec2(dx, dy - cell), kDpad[0], p, fa, fb);
		drawCtl(ImVec2(dx - cell, dy), kDpad[1], p, fa, fb);
		drawCtl(ImVec2(dx + cell, dy), kDpad[2], p, fa, fb);
		drawCtl(ImVec2(dx, dy + cell), kDpad[3], p, fa, fb);
		const float fx = dx + cell * 2.6f, fy = dy - cell * 0.55f;
		for (int i = 0; i < 6; i++)
			drawCtl(ImVec2(fx + (i % 3) * (cell + gap), fy + (i / 3) * (cell + gap)),
					kFace[i], p, fa, fb);
		drawCtl(ImVec2(fx + cell + gap, fy + 2.1f * (cell + gap)), kStart, p, fa, fb);
		// The group has no laid-out widgets - everything above went to the draw
		// list - so it needs a Dummy or the window sizes to the "P1" label.
		ImGui::Dummy(ImVec2(cell * 6.6f + gap * 2, cell * 4.4f));
		ImGui::EndGroup();
	};

	drawPlayer(0, gs.in.p1a, gs.in.p1b);
	ImGui::SameLine(0, 14.f * sc);
	drawPlayer(1, gs.in.p2a, gs.in.p2b);
}

}	// namespace inputviz

void registerInputVizPanel()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	/*
		ONE PANEL, BOTH STREAMS. The fork draws this as two windows chosen
		inside the draw function - a dockable "Input Viz" with the studio shell
		up, and a pinned NoDecoration|NoInputs "#input_viz" overlay with it down.
		core/rend/panel.h names this exact panel when it says not to register
		the overlay arm separately: one feature would get two ids, two menu rows
		and two persistence keys, and the user could close half of itself.

		So the costume is the registry's business and the body draws the same
		thing either way. What that gives up, honestly: the overlay arm's
		per-panel alpha and its click-through. Both are window flags, and a
		panel that set its own would be choosing furniture the host owns.
	*/
	/*
		THE DEFAULT SIZE IS DERIVED, NOT COPIED. `[MEASURED 2026-09-14]` the
		fork's 330x300 was ported verbatim and the first screenshot showed P2's
		six face buttons and Start clipped off the right edge - P1 complete, P2
		reduced to a d-pad.

		Nothing structural caught it. imgui.ini had [Window][Input Viz], the
		registry reported registered=yes, and all 22 claims passed, over a panel
		drawing half its content. Only looking at the picture found it, which is
		this tree's 2026-08-09 black-AVI lesson at a smaller scale.

		The number is arithmetic on the layout rather than taste: each player
		reserves cell*6.6 + gap*2, the two sit either side of a 14*sc gap, and
		cell/gap are 26/6 scaled by the same sc the body uses. At the default
		OverlayScale that is ~435, so 470 leaves room for the window padding and
		a scrollbar. core/rend/panel.h's defW/defH are per-panel for exactly this
		reason: a blanket size fixed the States window and broke the Piano Roll.
	*/
	panels::add({ "inputviz", "Input Viz", &inputviz::vizOpen, inputviz::draw,
			panels::Both, /*persist*/ true, /*defW*/ 470.f, /*defH*/ 300.f });
	NOTICE_LOG(RENDERER, "INPUTVIZ PANEL: registered=%s open=%s",
			panels::find("inputviz") != nullptr ? "yes" : "NO",
			inputviz::vizOpen ? "yes" : "no");
}

}	// namespace roll
