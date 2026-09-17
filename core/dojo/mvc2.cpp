#include "mvc2.h"
#include "dojo.h"
#include "cfg/cfg.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "log/Log.h"
#include "stdclass.h"
#include "json.hpp"
#include <xxhash.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
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
// `[CORRECTED 2026-09-15, from dev's 0915 tree]` the DISPLAYED combo counter =
// per-point-character "hits to opponent" (a BYTE), which RESETS when a combo
// drops - the true HIT/FAIL signal. The old 0x289642 "Combo_Meter_Value" is a
// running TOTAL: it keeps climbing across a reset (150 on a clip whose screen
// showed 11->reset->15), so a peak of it could never express "k landed, k+1
// dropped" and the FST oracle read as vacuous. Confirmed by his address sweep
// 2026-09-12. P1_A / P2_A = the point character's slot; read as a BYTE.
static constexpr u32 P1_COMBO      = BASE + 0x2685A0;	// P1_A_Combo_Meter_HitsToOpponent (trainer 0x2C2685A0)
static constexpr u32 P2_COMBO      = BASE + 0x268B44;	// P2_A_Combo_Meter_HitsToOpponent (trainer 0x2C268B44)

static bool validated = false;
static bool reported = false;
static std::atomic<u16> comboLast1{0}, comboLast2{0}, comboPeak1{0}, comboPeak2{0};	// written on the emulator loop, read by the GUI
static std::mutex seriesMutex;
static std::vector<ComboSample> series;		// the on-change series, emulator loop -> GUI (copied out)

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

/*
	THE COMBO ORACLE'S ADDRESSES, RESOLVED BY NAME. `[2026-09-17]` the dictionary
	below owns the vocabulary; the constants above are the fallback (no JSON found)
	and the selfTest's cross-check (the two must agree, or the probe is reading a
	different byte than every note in this file describes). Resolved once, logged
	once - which path won is part of the record.
*/
static const u32 *comboAddrs()
{
	static u32 addrs[2] = { 0, 0 };
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		const u32 a = addrOf("Combo_Meter_HitsToOpponent", 0, 0);
		const u32 b = addrOf("Combo_Meter_HitsToOpponent", 1, 0);
		if (a != 0 && b != 0)
		{
			addrs[0] = a;
			addrs[1] = b;
			NOTICE_LOG(NETWORK, "TAS MVC2: combo oracle by NAME - Combo_Meter_HitsToOpponent P1_A=0x%08X P2_A=0x%08X%s (%s)",
					a, b, (a == P1_COMBO && b == P2_COMBO) ? "" : " - DIFFERS FROM THE HARDCODED CONSTANTS",
					dictionaryPath().c_str());
		}
		else
		{
			addrs[0] = P1_COMBO;
			addrs[1] = P2_COMBO;
			NOTICE_LOG(NETWORK, "TAS MVC2: combo oracle by CONSTANT - the field dictionary did not resolve it (%s)",
					dictionaryPath().empty() ? "no SPREADSHEET.json found" : dictionaryPath().c_str());
		}
	}
	return addrs;
}

bool peekCombo(u16& p1, u16& p2)
{
	if (settings.content.path.empty())
		return false;
	const u32 *c = comboAddrs();
	p1 = ReadMem8_nommu(c[0]);	// a BYTE counter; a u16 read leaks the neighbour field
	p2 = ReadMem8_nommu(c[1]);
	return true;
}

void comboPoll()
{
	const u32 *c = comboAddrs();
	const u16 a = ReadMem8_nommu(c[0]);	// BYTE - see the address note above
	const u16 b = ReadMem8_nommu(c[1]);
	comboLast1.store(a, std::memory_order_relaxed);
	comboLast2.store(b, std::memory_order_relaxed);
	if (a > comboPeak1.load(std::memory_order_relaxed))
		comboPeak1.store(a, std::memory_order_relaxed);
	if (b > comboPeak2.load(std::memory_order_relaxed))
		comboPeak2.store(b, std::memory_order_relaxed);
	// dojo:ComboProbe - log the counter on every CHANGE, so a real (resetting)
	// combo is visible and the corrected address/width can be confirmed by
	// measurement, not just against dev's tree. Edge-triggered, off by default.
	static u16 pa = 0xffff, pb = 0xffff;
	if (a != pa || b != pb)
	{
		if (cfgLoadBool("dojo", "ComboProbe", false))
			NOTICE_LOG(NETWORK, "COMBO PROBE: P1=%u P2=%u", (unsigned)a, (unsigned)b);
		pa = a;
		pb = b;
		// The series (see mvc2.h): the same edge, frame-stamped, kept.
		const std::lock_guard<std::mutex> lock(seriesMutex);
		if (series.size() < 4096)
			series.push_back({ dojo.frame_number.load(), a, b });
	}
}

void comboPeakReset()
{
	comboPeak1.store(0, std::memory_order_relaxed);
	comboPeak2.store(0, std::memory_order_relaxed);
}

void comboSeriesReset()
{
	const std::lock_guard<std::mutex> lock(seriesMutex);
	series.clear();
}

std::vector<ComboSample> comboSeriesTake()
{
	const std::lock_guard<std::mutex> lock(seriesMutex);
	return series;
}

u16 comboLast(int player)
{
	return player == 0 ? comboLast1.load(std::memory_order_relaxed) : comboLast2.load(std::memory_order_relaxed);
}

u16 comboPeak(int player)
{
	return player == 0 ? comboPeak1.load(std::memory_order_relaxed) : comboPeak2.load(std::memory_order_relaxed);
}

// `[PORTED 2026-09-15]` from dev's 0915 tree - the control server's `read` verb.
u32 readRam(u32 addr, int width)
{
	if (settings.content.path.empty())
		return 0;
	if ((addr >> 24) == 0x2C)
		addr += 0x60000000u;		// Demul -> flycast (same bytes, cached mirror)
	switch (width)
	{
	case 1:  return ReadMem8_nommu(addr);
	case 2:  return ReadMem16_nommu(addr);
	default: return ReadMem32_nommu(addr);
	}
}

bool readRamSafe(u32 addr, int width, u32& out)
{
	if (settings.content.path.empty())
		return false;
	if ((addr >> 24) == 0x2C)
		addr += 0x60000000u;		// Demul -> flycast (same bytes, cached mirror)
	if (!IsOnRam(addr))
		return false;			// not work RAM: never touch it
	switch (width)
	{
	case 1:  out = ReadMem8_nommu(addr); break;
	case 2:  out = ReadMem16_nommu(addr); break;
	default: out = ReadMem32_nommu(addr); break;
	}
	return true;
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

// ---------------------------------------------------------------------------------------
// THE FIELD DICTIONARY (see mvc2.h). One parse, one map, resolved by name from then on.
// ---------------------------------------------------------------------------------------

namespace {

struct Dictionary
{
	std::map<std::string, Field> fields;
	std::string path;			// where it loaded from; "" = nowhere
	size_t bytes = 0;
	u32 hash = 0;				// XXH32 of the file bytes - the selfTest's identity pin
	std::string why;			// why it did not load
};

u32 parseHex(const nlohmann::json& j)
{
	if (!j.is_string())
		return 0;
	const std::string s = j.get<std::string>();
	if (s.empty())
		return 0;
	return (u32)strtoul(s.c_str(), nullptr, 16);
}

int widthOf(const std::string& type)
{
	if (type == "2 Bytes" || type == "Word")
		return 2;
	if (type == "4 Bytes" || type == "Float" || type == "Pointer")
		return 4;
	return 1;					// "Byte", "" and anything unnamed: read the smallest unit
}

std::string strOf(const nlohmann::json& j, const char *key)
{
	return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : std::string();
}

/*
	WHERE THE FILE IS. dojo:Mvc2Data names it outright; otherwise the data dirs
	(a shipped copy under mvc2_data/), then the source tree beside this file -
	the dev tree is where every harness runs, and none of the data dirs is it.
*/
std::string locate(std::string& tried)
{
	std::vector<std::string> cands;
	const std::string cfgPath = cfgLoadStr("dojo", "Mvc2Data", "");
	if (!cfgPath.empty())
		cands.push_back(cfgPath);
	cands.push_back(get_readonly_data_path("mvc2_data/SPREADSHEET.json"));
	{
		std::string here = __FILE__;
		const size_t sl = here.find_last_of("/\\");
		if (sl != std::string::npos)
			cands.push_back(here.substr(0, sl) + "/mvc2_data/SPREADSHEET.json");
	}
	for (const std::string& c : cands)
	{
		if (file_exists(c))
			return c;
		tried += (tried.empty() ? "" : ", ") + c;
	}
	return "";
}

const Dictionary& dict()
{
	static Dictionary d;
	static bool loaded = false;
	if (loaded)
		return d;
	loaded = true;
	std::string tried;
	d.path = locate(tried);
	if (d.path.empty())
	{
		d.why = "not found (tried " + tried + ")";
		NOTICE_LOG(NETWORK, "TAS MVC2: field dictionary %s", d.why.c_str());
		return d;
	}
	std::ifstream in(d.path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	d.bytes = text.size();
	d.hash = (u32)XXH32(text.data(), text.size(), 0);
	nlohmann::json root;
	try {
		root = nlohmann::json::parse(text);
	} catch (const std::exception& e) {
		d.why = std::string("parse error: ") + e.what();
		NOTICE_LOG(NETWORK, "TAS MVC2: field dictionary %s (%s)", d.why.c_str(), d.path.c_str());
		d.path.clear();
		return d;
	}
	const nlohmann::json& sheet = root.contains("SPREADSHEET") ? root["SPREADSHEET"] : root;
	if (!sheet.is_object())
	{
		d.why = "no SPREADSHEET object";
		d.path.clear();
		return d;
	}
	static const char *P[2] = { "P1", "P2" };
	static const char *S[3] = { "A", "B", "C" };
	for (auto git = sheet.begin(); git != sheet.end(); ++git)
	{
		const std::string group = git.key();
		if (!git.value().is_object())
			continue;
		// CharacterInfo / StagesInfo / InputsInfo are lookup tables, not addresses; they
		// carry no Type/address keys, so the address scan below simply finds nothing.
		for (auto fit = git.value().begin(); fit != git.value().end(); ++fit)
		{
			const nlohmann::json& e = fit.value();
			if (!e.is_object())
				continue;
			Field f;
			f.name = fit.key();
			f.group = group;
			f.type = strOf(e, "Type");
			f.width = widthOf(f.type);
			f.note1 = strOf(e, "Note1");
			f.note2 = strOf(e, "Note2");
			f.offset = e.contains("hexOffset") ? parseHex(e["hexOffset"]) : 0;
			for (int p = 0; p < 2; p++)
			{
				for (int s = 0; s < 3; s++)
				{
					const std::string k = std::string(P[p]) + "_" + S[s] + "_" + f.name;
					if (e.contains(k))
					{
						f.demul[p][s] = parseHex(e[k]);
						f.perSlot = f.perSlot || f.demul[p][s] != 0;
					}
				}
				const std::string kp = std::string(P[p]) + "_" + f.name;
				if (e.contains(kp))
				{
					f.demulPlayer[p] = parseHex(e[kp]);
					f.perPlayer = f.perPlayer || f.demulPlayer[p] != 0;
				}
			}
			if (e.contains("Address"))
				f.demulSystem = parseHex(e["Address"]);
			if (!f.perSlot && !f.perPlayer && f.demulSystem == 0)
				continue;		// a lookup-table row, not an address
			d.fields[f.name] = f;
		}
	}
	NOTICE_LOG(NETWORK, "TAS MVC2: field dictionary loaded - %u fields, %u bytes, xxh32 %08X, from %s",
			(u32)d.fields.size(), (u32)d.bytes, d.hash, d.path.c_str());
	return d;
}

}	// namespace

u32 toFlycast(u32 demulAddr)
{
	return (demulAddr >> 24) == 0x2C ? demulAddr + 0x60000000u : demulAddr;
}

u32 toDemul(u32 flycastAddr)
{
	return (flycastAddr >> 24) == 0x8C ? flycastAddr - 0x60000000u : flycastAddr;
}

bool field(const char *name, Field& out)
{
	if (name == nullptr)
		return false;
	const Dictionary& d = dict();
	const auto it = d.fields.find(name);
	if (it == d.fields.end())
		return false;
	out = it->second;
	return true;
}

u32 addrOf(const char *name, int player, int slot)
{
	Field f;
	if (!field(name, f))
		return 0;
	if (f.perSlot)
	{
		if (player < 0 || player > 1 || slot < 0 || slot > 2)
			return 0;
		return toFlycast(f.demul[player][slot]);
	}
	if (f.perPlayer)
	{
		if (player < 0 || player > 1)
			return 0;
		return toFlycast(f.demulPlayer[player]);
	}
	return toFlycast(f.demulSystem);
}

/*
	Note2 holds enums as lines of "<n>: <label>" (CRLF or LF - the sheet has both).
	Ranges like "0-99" and prose are simply not matched.
*/
bool enumLabel(const char *name, u32 value, std::string& label)
{
	Field f;
	if (!field(name, f) || f.note2.empty())
		return false;
	std::stringstream ss(f.note2);
	std::string line;
	while (std::getline(ss, line))
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		const size_t c = line.find(':');
		if (c == std::string::npos || c == 0)
			continue;
		char *end = nullptr;
		const unsigned long n = strtoul(line.c_str(), &end, 10);
		if (end == line.c_str() || (size_t)(end - line.c_str()) != c)
			continue;		// not "<number>:"
		if (n != value)
			continue;
		size_t b = c + 1;
		while (b < line.size() && line[b] == ' ')
			b++;
		label = line.substr(b);
		return true;
	}
	return false;
}

u32 charBase(int player, int slot)
{
	return addrOf("Base", player, slot);
}

bool isPoint(int player, int slot)
{
	const u32 a = addrOf("Is_Point", player, slot);
	u32 v = 0;
	return a != 0 && readRamSafe(a, 1, v) && v != 0;
}

int fieldCount()
{
	return (int)dict().fields.size();
}

const std::string& dictionaryPath()
{
	return dict().path;
}

/*
	SPREADSHEET SELFTEST - the dictionary without an emulator (a digit-free name: selftest.sh counts `[A-Z]+ SELFTEST`). What would have been wrong
	silently: a name resolving to a different byte than the constant this file's
	notes describe, a width read as u16 (the neighbour-field leak fixed 2026-09-15),
	an unknown name resolving to SOMETHING, the sheet quietly swapped for another.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "SPREADSHEET SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	const Dictionary& d = dict();
	claim("SPREADSHEET.json loaded (a path, >100 fields)", !d.path.empty() && d.fields.size() > 100);
	// IDENTITY: David's file, byte for byte (md5 23c1827fc4fe3b04313ee8c944565b20 out of
	// tree; in tree the pin is its size and XXH32, measured on that md5's bytes).
	claim("the sheet is 302178 bytes", d.bytes == 302178);
	claim("the sheet's XXH32 is the pinned 0x26AAE67D", d.hash == 0x26AAE67Du);	// [MEASURED 2026-09-17]
	Field f;
	claim("Combo_Meter_HitsToOpponent is a field", field("Combo_Meter_HitsToOpponent", f));
	claim("...a Byte (width 1 - a u16 read leaks the neighbour)", f.width == 1 && f.type == "Byte");
	claim("...at block offset 0x260", f.offset == 0x260);
	claim("...P1_A resolves to the hardcoded 0x8C2685A0 (Demul 0x2C2685A0)",
			addrOf("Combo_Meter_HitsToOpponent", 0, 0) == P1_COMBO && toDemul(P1_COMBO) == 0x2C2685A0u);
	claim("...P2_A resolves to the hardcoded 0x8C268B44 (Demul 0x2C268B44)",
			addrOf("Combo_Meter_HitsToOpponent", 1, 0) == P2_COMBO && toDemul(P2_COMBO) == 0x2C268B44u);
	claim("Base + hexOffset == the flattened P1_A_ key (the sheet is self-consistent)",
			charBase(0, 0) != 0 && charBase(0, 0) + f.offset == addrOf("Combo_Meter_HitsToOpponent", 0, 0));
	claim("...and for P2_C", charBase(1, 2) != 0 && charBase(1, 2) + f.offset == addrOf("Combo_Meter_HitsToOpponent", 1, 2));
	claim("an unknown name is not a field", !field("No_Such_Field_Anywhere", f));
	claim("...and resolves to 0, never to a neighbour", addrOf("No_Such_Field_Anywhere", 0, 0) == 0);
	claim("an out-of-range slot resolves to 0", addrOf("Combo_Meter_HitsToOpponent", 0, 3) == 0
			&& addrOf("Combo_Meter_HitsToOpponent", 2, 0) == 0);
	claim("a system address resolves with no player (A_2D_Game_Timer -> 0x8C289630)",
			addrOf("A_2D_Game_Timer") == 0x8C289630u);
	claim("a per-player address ignores the slot (P2 Assist_Flag -> 0x8C289633)",
			addrOf("Assist_Flag", 1, 2) == 0x8C289633u);
	// EVERY constant this file hardcodes has a name in the sheet; each pair must agree, or
	// one of the two vocabularies is describing a different game.
	claim("Frame_Skip_Rate is the hardcoded SKIP_RATE", addrOf("Frame_Skip_Rate") == SKIP_RATE);
	claim("Frame_Skip_Counter is the hardcoded SKIP_COUNT", addrOf("Frame_Skip_Counter") == SKIP_COUNT);
	claim("Frame_Skip_Toggle is the hardcoded SKIP_TOGGLE", addrOf("Frame_Skip_Toggle") == SKIP_TOGGLE);
	claim("Frame_Counter is the hardcoded SCENE_FRAME", addrOf("Frame_Counter") == SCENE_FRAME);
	claim("Total_Frames is the hardcoded TOTAL_FRAMES", addrOf("Total_Frames") == TOTAL_FRAMES);
	std::string label;
	claim("Note2 enums: ID_2 13 is Hulk", enumLabel("ID_2", 13, label) && label == "Hulk");
	claim("...and 14 is Venom", enumLabel("ID_2", 14, label) && label == "Venom");
	claim("...and 255 is nobody", !enumLabel("ID_2", 255, label));
	claim("Is_Point is a per-slot Byte", field("Is_Point", f) && f.perSlot && f.width == 1);
	claim("toFlycast/toDemul round-trip and leave other spaces alone",
			toFlycast(0x2C2685A0u) == 0x8C2685A0u && toDemul(0x8C2685A0u) == 0x2C2685A0u && toFlycast(0x0C001000u) == 0x0C001000u);
	NOTICE_LOG(RENDERER, "SPREADSHEET SELFTEST: %d passed, %d failed", pass, fail);
}

}
