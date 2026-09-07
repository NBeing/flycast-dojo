#include "mvc2.h"
#include "cfg/cfg.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "log/Log.h"
#include <algorithm>
#include <atomic>
#include <vector>

namespace tas_mvc2
{

// flycast guest address = 0x8C000000 + trainer offset (see mvc2.h header comment)
static constexpr u32 BASE          = 0x8C000000;
static constexpr u32 P1_FLAGS_A    = BASE + 0x2681DC;
static constexpr u32 P1_FLAGS_B    = BASE + 0x2681DD;
static constexpr u32 P2_FLAGS_A    = BASE + 0x2681F0;
static constexpr u32 P2_FLAGS_B    = BASE + 0x2681F1;
static constexpr u32 SKIP_RATE     = BASE + 0x289620;
static constexpr u32 SKIP_COUNT    = BASE + 0x289621;
static constexpr u32 SKIP_TOGGLE   = BASE + 0x289622;	// 255 on the reset/skip frame ("0 0 0 255")
static constexpr u32 SCENE_FRAME   = BASE + 0x1F9D80;
static constexpr u32 TOTAL_FRAMES  = BASE + 0x3496B0;
static constexpr u32 P1_COMBO      = BASE + 0x289642;	// combo meter, u16 LE (David, 2026-09-05; trainer 0x2C289642)
static constexpr u32 P2_COMBO      = BASE + 0x289640;	// (trainer 0x2C289640)

static bool validated = false;
static bool reported = false;
static std::atomic<u16> comboLast1{0}, comboLast2{0}, comboPeak1{0}, comboPeak2{0};	// written on the emulator loop, read by the GUI

bool mapValidated()
{
	return validated;
}

u8 readSkipToggle()
{
	if (!emu.running())
		return 0;
	return ReadMem8_nommu(SKIP_TOGGLE);
}

bool peekSkip(u8& rate, u8& count, u8& toggle)
{
	if (settings.content.path.empty())
		return false;
	rate = ReadMem8_nommu(SKIP_RATE);
	count = ReadMem8_nommu(SKIP_COUNT);
	toggle = ReadMem8_nommu(SKIP_TOGGLE);
	return true;
}

bool peekCombo(u16& p1, u16& p2)
{
	if (settings.content.path.empty())
		return false;
	p1 = ReadMem16_nommu(P1_COMBO);
	p2 = ReadMem16_nommu(P2_COMBO);
	return true;
}

void comboPoll()
{
	const u16 a = ReadMem16_nommu(P1_COMBO);
	const u16 b = ReadMem16_nommu(P2_COMBO);
	comboLast1.store(a, std::memory_order_relaxed);
	comboLast2.store(b, std::memory_order_relaxed);
	if (a > comboPeak1.load(std::memory_order_relaxed))
		comboPeak1.store(a, std::memory_order_relaxed);
	if (b > comboPeak2.load(std::memory_order_relaxed))
		comboPeak2.store(b, std::memory_order_relaxed);
}

void comboPeakReset()
{
	comboPeak1.store(0, std::memory_order_relaxed);
	comboPeak2.store(0, std::memory_order_relaxed);
}

u16 comboLast(int player)
{
	return player == 0 ? comboLast1.load(std::memory_order_relaxed) : comboLast2.load(std::memory_order_relaxed);
}

u16 comboPeak(int player)
{
	return player == 0 ? comboPeak1.load(std::memory_order_relaxed) : comboPeak2.load(std::memory_order_relaxed);
}

GameState read()
{
	GameState gs;
	if (!emu.running())
		return gs;

	gs.skipRate = ReadMem8_nommu(SKIP_RATE);
	gs.skipCount = ReadMem8_nommu(SKIP_COUNT);
	gs.in.p1a = ReadMem8_nommu(P1_FLAGS_A);
	gs.in.p1b = ReadMem8_nommu(P1_FLAGS_B);
	gs.in.p2a = ReadMem8_nommu(P2_FLAGS_A);
	gs.in.p2b = ReadMem8_nommu(P2_FLAGS_B);
	gs.sceneFrame = ReadMem32_nommu(SCENE_FRAME);
	gs.totalFrames = ReadMem32_nommu(TOTAL_FRAMES);
	gs.comboP1 = ReadMem16_nommu(P1_COMBO);
	gs.comboP2 = ReadMem16_nommu(P2_COMBO);

	// The roadmap's T1 checkpoint: the map is only trusted once skipRate reads a value the game
	// can actually hold (6 normal / 4 turbo / 2 turbo2). Reported ONCE either way, so the log
	// carries a clear verdict without becoming a ticker.
	if (!validated && (gs.skipRate == 6 || gs.skipRate == 4 || gs.skipRate == 2)
			&& gs.skipCount <= gs.skipRate)
	{
		validated = true;
		NOTICE_LOG(NETWORK, "TAS MVC2: address map VALID (frameskip %u/%u, scene frame %u) gameId='%s'",
				gs.skipCount, gs.skipRate, gs.sceneFrame, settings.content.gameId.c_str());
	}
	else if (!validated && !reported)
	{
		// Clock the timeout on OUR OWN read count, never on totalFrames - that address belongs to
		// the very map being doubted, and a garbage read there fired this report 10 s early.
		static u32 readsWhileInvalid = 0;
		if (++readsWhileInvalid > 60 * 30)
		{
			reported = true;
			// Measured on this rip: the skip pair holds 0,0 outside matches and 6,N during one
			// (attract demo fights included). So "not yet" here usually means "no fight yet",
			// not "wrong offsets" - the scene counter at 0x1F9D80 already proved the map.
			NOTICE_LOG(NETWORK, "TAS MVC2: skip system not seen live yet after 30 s (rate reads %u)"
					" - it only runs during a match; validation will latch in the first fight",
					gs.skipRate);
		}
	}

	// Cadence burst: the first ~3 s of LIVE skip activity, one line per frame - the exact
	// counter/toggle pattern (the user's "1 2 3 X" / "0 0 0 255" recollection) measured rather
	// than remembered. From the user's own cheat tables: cycle value u16 @0x289600, toggle
	// @0x289622, alongside the rate/timer pair.
	if (cfgLoadBool("dojo", "MemTrace", false) && gs.skipRate != 0)
	{
		static int burst = 0;
		if (burst < 180)
		{
			burst++;
			NOTICE_LOG(NETWORK, "TAS SKIP: rate=%u count=%u toggle=%u cycle=%u scene=%u",
					gs.skipRate, gs.skipCount, ReadMem8_nommu(BASE + 0x289622),
					ReadMem16_nommu(BASE + 0x289600), gs.sceneFrame);
		}
	}

	// Change-driven memory trace (dojo:MemTrace) - same philosophy as the input tracer: silence
	// means "nothing changed", a line names exactly what did.
	if (cfgLoadBool("dojo", "MemTrace", false))
	{
		static GameInputs last;
		static u8 lastRate = 0xFF;
		if (gs.in.p1a != last.p1a || gs.in.p1b != last.p1b
				|| gs.in.p2a != last.p2a || gs.in.p2b != last.p2b || gs.skipRate != lastRate)
		{
			// NETWORK, not INPUT, deliberately: the user mutes INPUT to kill hotkey chatter, and
			// that mute silently ate this trace for a whole debugging session. Memory truth
			// belongs with the other TAS session traces.
			NOTICE_LOG(NETWORK, "TAS MEM: P1 a=%02X b=%02X  P2 a=%02X b=%02X  skip %u/%u  scene=%u",
					gs.in.p1a, gs.in.p1b, gs.in.p2a, gs.in.p2b,
					gs.skipCount, gs.skipRate, gs.sceneFrame);
			last = gs.in;
			lastRate = gs.skipRate;
		}
	}
	// ---- offset hunt (dojo:MemHunt=yes) ------------------------------------------------------
	// Finds the frame counters empirically: a byte that changes EVERY frame is a counter, and the
	// skip COUNT is exactly that. Full-RAM snapshot diffing, intersected over 120 frames, leaves a
	// handful of survivors; the skip RATE is then a neighbour that sits constant at 6. Seeding
	// waits ~25 s so the GAME's RAM is being scanned, not the BIOS boot's (the first attempt
	// seeded at 0.5 s and found nothing but boot-era garbage).
	if (cfgLoadBool("dojo", "MemHunt", false))
	{
		static std::vector<u8> prev;
		static std::vector<u8> everyFrame;		// 1 = changed on every observed frame
		static int calls = 0, diffs = 0;
		static bool done = false;
		const u32 huntSize = 16 * 1024 * 1024;	// DC main RAM (RAM_SIZE is a global macro)
		u8 *ram = GetMemPtr(BASE, huntSize);
		calls++;
		// Always-on 1 Hz raw probe of the trainer's skip pair - answers "is the skip system just
		// idle outside matches?" without another hunt. 0x1F9D80 already proved the map is NOT
		// relocated (it changes every frame, exactly as the trainer documents).
		if (ram != nullptr && calls % 60 == 0)
		{
			NOTICE_LOG(NETWORK, "TAS HUNT: probe skip@0x289620=%u,%u  scene=%u  total=%u",
					ram[0x289620], ram[0x289621],
					*(u32 *)(ram + 0x1F9D80), *(u32 *)(ram + 0x3496B0));
			// The trainer's input-flag neighbourhood, dumped raw: measured DEAD on this rip
			// (never changed through a whole replayed match), so either the layout moved a
			// little - visible here - or the real flags live elsewhere and need a correlation
			// hunt against the movie's own inputs.
			char hex[3 * 64 + 8];
			int w = 0;
			for (u32 i = 0x2681C0; i < 0x268200 && w < (int)sizeof(hex) - 4; i++)
				w += snprintf(hex + w, sizeof(hex) - w, "%02X ", ram[i]);
			NOTICE_LOG(NETWORK, "TAS HUNT: flags window 2681C0..2681FF: %s", hex);
		}
		if (ram != nullptr && !done && calls >= 60 * 25)
		{
			if (prev.empty())
			{
				prev.assign(ram, ram + huntSize);
				everyFrame.assign(huntSize, 1);
				NOTICE_LOG(NETWORK, "TAS HUNT: seeded at read %d - diffing every frame now", calls);
			}
			else
			{
				for (u32 i = 0; i < huntSize; i++)
					if (everyFrame[i] && ram[i] == prev[i])
						everyFrame[i] = 0;
				memcpy(prev.data(), ram, huntSize);
				diffs++;
			}
			if (diffs == 120)
			{
				done = true;
				int hits = 0;
				for (u32 i = 0; i < huntSize && hits < 40; i++)
				{
					if (!everyFrame[i])
						continue;
					hits++;
					// A cycling byte whose neighbour holds 2/4/6 = the rate/count pair.
					const int rb = (i > 0 && (ram[i - 1] == 6 || ram[i - 1] == 4 || ram[i - 1] == 2)) ? -1
								 : ((ram[i + 1] == 6 || ram[i + 1] == 4 || ram[i + 1] == 2) ? 1 : 0);
					NOTICE_LOG(NETWORK, "TAS HUNT: every-frame byte at offset 0x%06X value %u%s",
							i, ram[i],
							rb == -1 ? "  <- neighbour[-1] holds a RATE value (count/rate pair, rate first)"
							: rb == 1 ? "  <- neighbour[+1] holds a RATE value" : "");
				}
				NOTICE_LOG(NETWORK, "TAS HUNT: done - %d byte(s) changed on all 120 frames", hits);
			}
		}
	}
	return gs;
}

}
