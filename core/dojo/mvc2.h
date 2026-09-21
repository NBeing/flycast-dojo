#pragma once
#include "types.h"
#include <string>
#include <vector>

// T1 of the scripted-input roadmap: the MvC2 (Dreamcast) memory probe.
//
// Reads the game's OWN view of the world out of guest RAM - what inputs it latched, where its
// frame-skip cycle stands, what its frame counters say. Offsets come from the Demul-CE trainer
// (MvC2_Trainer_Script.CT, DC version - the same game as NoBGM_VMU.cdi); Demul mapped guest RAM
// at host 0x2C000000, flycast's guest sees it at SH4 0x8C000000, so:
//
//     flycast address = 0x8C000000 + trainer offset
//
// Everything here is read-only and cheap (a handful of byte reads); the input visualizer (T2)
// renders it and the fidelity harness (T3) asserts against it.
namespace tas_mvc2
{
	// The game's latched input flags, one byte pair per player. Bit layout (trainer-documented):
	//   byte B: Up=32 Down=16 Left=8 Right=4 X(LP)=2 Y(HP)=1 Start=128
	//   byte A: Ltrig(A1)=128 A(LK)=64 B(HK)=32 Rtrig(A2)=16
	struct GameInputs
	{
		u8 p1a = 0, p1b = 0;
		u8 p2a = 0, p2b = 0;
	};

	struct GameState
	{
		GameInputs in;
		u8 skipRate = 0;	// 6 normal, 4 turbo, 2 turbo2
		u8 skipCount = 0;	// counts down; at 0 resets to rate and the game runs an extra logic frame
		u32 sceneFrame = 0;	// resets between scenes
		u32 totalFrames = 0;	// since boot
		u16 comboP1 = 0;	// GameState mirror of the hit meter (unused; the ORACLE is comboPoll/comboPeek)
		u16 comboP2 = 0;	// `[CORRECTED 2026-09-20]` the ORACLE is Combo_Meter_Value, u16 per player (P1 0x268B50 /
							// P2 0x2685AC): the on-screen HIT number, measured hit for hit against David's video; it
							// resets on a drop. The point character's HitsToOpponent byte (2026-09-15) excluded assist
							// and partner hits - see the note in mvc2.cpp.
	};

	// True once the address map has proven itself on this session: skipRate read a legal value
	// (2/4/6). Until then every reader shows "probing" rather than garbage - the roadmap's first
	// checkpoint is exactly this latch going true with rate=6 at normal speed.
	bool mapValidated();

	// One snapshot of everything above. Safe to call every frame; also drives the validation
	// latch and, with dojo:MemTrace=yes, a change-driven TAS MEM log line.
	GameState read();

	// The frame-skip TOGGLE byte (0x8C289622): reads 255 on the reset/skip frame (the "0 0 0 255" cadence),
	// 0 otherwise or when not running. Wait-for-Frameskip anchors a held live Send to this.
	u8 readSkipToggle();

	// The skip trio (rate, count, toggle) read even while PAUSED - read()/readSkipToggle() return 0 when the emulator
	// is not running. Only for a loaded, stopped game (the piano roll ruler peeks the frame it is paused on so that
	// row shows what the Timeline / Input Viz SKIP badge shows). False = no game loaded.
	bool peekSkip(u8& rate, u8& count, u8& toggle);

	// The COMBO METERS (David, 2026-09-05), the first outcome fact a test can assert on - the seed of the automated
	// 'did it combo?' verdict. peekCombo reads them even while PAUSED (a loaded game; false = none loaded). comboPoll runs
	// once per maple poll on the emulator loop and keeps the LAST value and the PEAK since comboPeakReset() (the Frame
	// Skip Test resets it on every bake, so the peak measures that variant's run).
	bool peekCombo(u16& p1, u16& p2);
	void comboPoll();
	void comboPeakReset();
	u16 comboLast(int player);	// 0 = P1, 1 = P2
	u16 comboPeak(int player);

	// THE COMBO SERIES - David's comboSample (0915 testrun.cpp: `{frame, p1, p2}` per sampled
	// frame into manifest.json) lifted onto the emulator loop: comboPoll appends a sample on
	// every CHANGE of either meter since comboSeriesReset(), frame-stamped. On change rather
	// than every frame, so it is bounded (4096) and a 1500-frame run stays a few dozen rows;
	// a reader that wants "the meter at frame f" holds the last sample at or before f. Take it
	// while the machine is stopped (a copy) - the hunt writes it per candidate.
	struct ComboSample { u32 frame; u16 p1, p2; };
	void comboSeriesReset();
	std::vector<ComboSample> comboSeriesTake();

	// Generic work-RAM read (Demul 2C.. or flycast 8C.. address; width 1/2/4), for the
	// control server's `read` verb. `[PORTED 2026-09-15]` from dev's 0915 tree.
	u32 readRam(u32 addr, int width);
	// Same, but IsOnRam-checked: false (out untouched) for a non-RAM / out-of-range address.
	bool readRamSafe(u32 addr, int width, u32& out);

	// ---- THE FIELD DICTIONARY: David's SPREADSHEET.json, the oracle's NAMES --------------------
	//
	// `[2026-09-17]` core/dojo/mvc2_data/SPREADSHEET.json (302178 bytes, md5
	// 23c1827fc4fe3b04313ee8c944565b20) is his DC-verified address vocabulary: per-character
	// fields as `Base + hexOffset` in six blocks (P1/P2 x A/B/C), per-player fields (P1_/P2_),
	// and system addresses - Demul-mapped (0x2C......), which is +0x60000000 from flycast's
	// 0x8C...... . Every address a probe asserts on should be resolved BY NAME here, so a
	// test reads "Combo_Meter_Value" and not a constant nobody can check; the
	// constants above stay as the FALLBACK and as the selfTest's cross-check.
	//
	// Loaded at first use from, in order: dojo:Mvc2Data (a file path), the data dirs
	// (mvc2_data/SPREADSHEET.json), then the source tree beside this file. Logged once.
	struct Field
	{
		std::string name, group, type, note1, note2;
		u32 offset = 0;			// hexOffset inside a character block (per-slot fields only)
		int width = 1;			// Byte 1; "2 Bytes"/Word 2; "4 Bytes"/Float/Pointer 4
		bool perSlot = false;	// P1_A_..P2_C_ keys present (a character-block field)
		bool perPlayer = false;	// P1_/P2_ keys present (Player1And2Addresses)
		u32 demul[2][3] = {};	// [player][slot] Demul address, 0 = absent
		u32 demulPlayer[2] = {};
		u32 demulSystem = 0;	// SystemMemoryAddresses "Address"
	};
	bool field(const char *name, Field& out);				// false = no such name
	// The FLYCAST address (0x8C......) of `name` for player 0/1 and slot 0..2 (A/B/C);
	// per-player fields ignore slot, system fields ignore both. 0 = unresolvable.
	u32 addrOf(const char *name, int player = 0, int slot = 0);
	u32 toFlycast(u32 demulAddr);							// 0x2C.. -> 0x8C.. (others unchanged)
	u32 toDemul(u32 flycastAddr);							// the inverse
	// Note2 enums ("13: Hulk"): the label for `value`, false when the field has no such entry.
	bool enumLabel(const char *name, u32 value, std::string& label);
	u32 charBase(int player, int slot);						// addrOf("Base", ...) - the block start
	bool isPoint(int player, int slot);						// reads Is_Point (a loaded game; false otherwise)
	int fieldCount();										// 0 = the dictionary did not load
	const std::string& dictionaryPath();					// where it loaded from ("" = nowhere)
	void selfTest();										// SPREADSHEET SELFTEST - dojo:PanelSelfTest

	// ---- THE STATE MACHINE EVALUATOR (ported from David's tas_mvc2, 2026-09-18) --------------
	//
	// A state is an ALL-conditions-AND predicate over per-character fields, defined as DATA in
	// mvc2_data/states_general.json (24 states; e.g. Being_Hit = Knockdown_State==32 &&
	// Hitstop2>0), each field resolved BY NAME through the dictionary above. A state naming an
	// unknown field is logged and SKIPPED at load - never silently always-false. Located like
	// SPREADSHEET.json: dojo:Mvc2States, the data dirs, then the source tree beside this file.
	struct StateInfo { std::string name, description; };
	void loadStates();											// lazy on first use; logs "MVC2 states: loaded N (M skipped)"
	const std::vector<StateInfo>& stateList();
	int  stateIndex(const char *name);							// -1 = no such state
	int  pointSlot(int player);									// the slot whose Is_Point reads 0; A when none does
	// Evaluate every loaded state for `player`'s POINT character NOW: out[i] (1/0) matches stateList()[i].
	void evalPointStates(int player, std::vector<u8>& out);
	// One state for one player now; `thresholdOverride` >= 0 replaces the FIRST condition's
	// threshold (a sabotage arm's instrument - never used by a feature).
	bool evalState(int stateIdx, int player, int thresholdOverride = -1);

	// THE SAMPLER: evaluate ONE state for both players on every maple poll (inside comboPoll,
	// the emulator loop) between arm and take - how a tour step asks "did P2 enter Being_Hit
	// during the combo, and did P1 before the first hit?" without a per-frame callback.
	struct StateSample
	{
		int polls = 0;
		int activeP1 = 0, activeP2 = 0;					// polls with the state active
		int activeP1BeforeFirstHit = 0;					// P1 active while P2's combo meter was still 0
		int condP2[4] = { 0, 0, 0, 0 };					// polls on which condition i (up to 4) held for P2 alone - names the miss
		int pointP1 = -1, pointP2 = -1;					// the slots evaluated
		u32 firstActiveP2Frame = 0, firstHitFrame = 0;	// 0 = never
	};
	void statesSampleArm(int stateIdx, int thresholdOverride = -1);
	StateSample statesSampleTake();								// disarms
}
