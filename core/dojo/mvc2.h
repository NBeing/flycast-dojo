#pragma once
#include "types.h"

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
		u16 comboP1 = 0;	// the combo (hit) meters - David, 2026-09-05: trainer 0x2C289642 / 0x2C289640 -> 0x8C289642 / 0x8C289640
		u16 comboP2 = 0;	// read as little-endian u16 (the pair sits 2 bytes apart); confirm the width against the CT if a value looks wrong
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
}
