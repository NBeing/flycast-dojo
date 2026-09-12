#include <array>
#include "spg.h"
#include "cfg/cfg.h"
#include "hw/holly/holly_intc.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_sched.h"
#include "oslib/oslib.h"
#include "hw/maple/maple_if.h"
#include "serialize.h"
#include "network/ggpo.h"
#include "hw/pvr/Renderer_if.h"

#ifdef TEST_AUTOMATION
#include "input/gamepad_device.h"
#endif

//SPG emulation; Scanline/Raster beam registers & interrupts

static u32 clc_pvr_scanline;
static u32 pvr_numscanlines = 512;
static u32 prv_cur_scanline = -1;

#if !defined(NDEBUG) || defined(DEBUGFAST)
static u32 vblk_cnt;
static float last_fps;
#endif

// 27 mhz pixel clock
constexpr int PIXEL_CLOCK = 27 * 1000 * 1000;
static u32 Line_Cycles;
static u32 Frame_Cycles;
int render_end_schid;
int vblank_schid;

static std::array<double, 4> real_times;
static std::array<u64, 4> cpu_cycles;
static u32 cpu_time_idx;
bool SH4FastEnough;
u32 fskip;

static u32 lightgun_line = 0xffff;
static u32 lightgun_hpos;
static bool maple_int_pending;

void CalculateSync()
{
	u32 pixel_clock = PIXEL_CLOCK / (FB_R_CTRL.vclk_div ? 1 : 2);

	// We need to calculate the pixel clock

	pvr_numscanlines = SPG_LOAD.vcount + 1;

	Line_Cycles = (u32)((u64)SH4_MAIN_CLOCK * (u64)(SPG_LOAD.hcount + 1) / (u64)pixel_clock);
	if (SPG_CONTROL.interlace)
		Line_Cycles /= 2;

	Frame_Cycles = pvr_numscanlines * Line_Cycles;
	prv_cur_scanline = 0;
	clc_pvr_scanline = 0;

	sh4_sched_request(vblank_schid, Line_Cycles);
}

static int getNextSpgInterrupt()
{
	if (SPG_HBLANK_INT.hblank_int_mode == 2)
		return Line_Cycles;

	u32 min_scanline = prv_cur_scanline + 1;
	u32 min_active = pvr_numscanlines;

	if (min_scanline <= SPG_VBLANK_INT.vblank_in_interrupt_line_number)
		min_active = std::min(min_active, SPG_VBLANK_INT.vblank_in_interrupt_line_number);

	if (min_scanline <= SPG_VBLANK_INT.vblank_out_interrupt_line_number)
		min_active = std::min(min_active, SPG_VBLANK_INT.vblank_out_interrupt_line_number);

	if (min_scanline <= SPG_VBLANK.vstart)
		min_active = std::min(min_active, SPG_VBLANK.vstart);

	if (min_scanline <= SPG_VBLANK.vbend)
		min_active = std::min(min_active, SPG_VBLANK.vbend);

	if (lightgun_line != 0xffff && min_scanline <= lightgun_line)
		min_active = std::min(min_active, lightgun_line);

	if (SPG_HBLANK_INT.hblank_int_mode == 0 && min_scanline <= SPG_HBLANK_INT.line_comp_val)
		min_active = std::min(min_active, SPG_HBLANK_INT.line_comp_val);

	min_active = std::max(min_active, min_scanline);

	return (min_active - prv_cur_scanline) * Line_Cycles;
}

void rescheduleSPG()
{
	sh4_sched_request(vblank_schid, getNextSpgInterrupt());
}

static int spg_line_sched(int tag, int cycles, int jitter, void *arg)
{
	clc_pvr_scanline += cycles + jitter;

	while (clc_pvr_scanline >= Line_Cycles)
	{
		prv_cur_scanline = (prv_cur_scanline + 1) % pvr_numscanlines;
		clc_pvr_scanline -= Line_Cycles;
		
		if (SPG_VBLANK_INT.vblank_in_interrupt_line_number == prv_cur_scanline)
		{
			if (maple_int_pending)
			{
				maple_int_pending = false;
				SB_MDST = 0;
			}
			asic_RaiseInterrupt(holly_SCANINT1);
		}

		if (SPG_VBLANK_INT.vblank_out_interrupt_line_number == prv_cur_scanline)
		{
			maple_vblank();
			asic_RaiseInterrupt(holly_SCANINT2);
		}

		if (SPG_VBLANK.vstart == prv_cur_scanline)
			SPG_STATUS.vsync = 1;

		if (SPG_VBLANK.vbend == prv_cur_scanline)
			SPG_STATUS.vsync = 0;

		SPG_STATUS.scanline = prv_cur_scanline;
		
		switch (SPG_HBLANK_INT.hblank_int_mode)
		{
		case 0:
			if (prv_cur_scanline == SPG_HBLANK_INT.line_comp_val)
				asic_RaiseInterrupt(holly_HBLank);
			break;
		case 2:
			asic_RaiseInterrupt(holly_HBLank);
			break;
		case 1:
			WARN_LOG(PVR, "Unimplemented HBLANK INT mode");
			break;
		default:
			INFO_LOG(PVR, "Invalid HBLANK INT mode");
			break;
		}

		// Vblank
		if (prv_cur_scanline == 0)
		{
			if (SPG_CONTROL.interlace)
				SPG_STATUS.fieldnum = ~SPG_STATUS.fieldnum;
			else
				SPG_STATUS.fieldnum = 0;

			rend_vblank();

			double now = os_GetSeconds() * 1000000.0;
			cpu_time_idx = (cpu_time_idx + 1) % cpu_cycles.size();
			if (cpu_cycles[cpu_time_idx] != 0)
			{
				u32 cycle_span = (u32)(sh4_sched_now64() - cpu_cycles[cpu_time_idx]);
				double time_span = now - real_times[cpu_time_idx];
				double cpu_speed = ((double)cycle_span / time_span) / (SH4_MAIN_CLOCK / 100000000);
				SH4FastEnough = cpu_speed >= 85.0;
			}
			else
				SH4FastEnough = false;
			cpu_cycles[cpu_time_idx] = sh4_sched_now64();
			real_times[cpu_time_idx] = now;

#ifdef TEST_AUTOMATION
			replay_input();
#endif

#if !defined(NDEBUG) || defined(DEBUGFAST)
			vblk_cnt++;
			if ((os_GetSeconds()-last_fps)>2)
			{
				static int Last_FC;
				double ts=os_GetSeconds()-last_fps;
				double spd_fps=(FrameCount-Last_FC)/ts;
				double spd_vbs=vblk_cnt/ts;
				double spd_cpu=spd_vbs*Frame_Cycles;
				spd_cpu/=1000000;	//mrhz kthx
				double fullvbs=(spd_vbs/spd_cpu)*200;

				Last_FC=FrameCount;

				vblk_cnt=0;

				const char* mode=0;
				const char* res=0;

				res = SPG_CONTROL.interlace ? "480i" : "240p";

				if (SPG_CONTROL.isPAL())
					mode = "PAL";
				else if (SPG_CONTROL.isNTSC())
					mode = "NTSC";
				else
				{
					res = SPG_CONTROL.interlace ? "480i" : "480p";
					mode = "VGA";
				}

				double frames_done=spd_cpu/2;
				double mspdf=1/frames_done*1000;

				double full_rps = spd_fps + fskip / ts;

				INFO_LOG(COMMON, "%s/%c - %4.2f - %4.2f - V: %4.2f (%.2f, %s%s%4.2f) R: %4.2f+%4.2f",
					VER_SHORTNAME,'n',mspdf,spd_cpu*100/200,spd_vbs,
					spd_vbs/full_rps,mode,res,fullvbs,
					spd_fps,fskip/ts);
				
				fskip=0;
				last_fps=os_GetSeconds();
			}
#endif
		}
		if (lightgun_line != 0xffff && lightgun_line == prv_cur_scanline)
		{
			maple_int_pending = false;
			SPG_TRIGGER_POS = ((lightgun_line & 0x3FF) << 16) | (lightgun_hpos & 0x3FF);
			SB_MDST = 0;
			lightgun_line = 0xffff;
		}
	}

	return getNextSpgInterrupt();
}

void read_lightgun_position(int x, int y)
{
	static u8 flip;
	maple_int_pending = true;
	if (y < 0 || y >= 480 || x < 0 || x >= 640)
	{
		// Off screen
		lightgun_line = 0xffff;
	}
	else
	{
		lightgun_line = y / (SPG_CONTROL.interlace ? 2 : 1) + SPG_VBLANK_INT.vblank_out_interrupt_line_number;
		// For some reason returning the same position twice makes it register off screen
		lightgun_hpos = (x + 286) ^ flip;
		flip ^= 1;
	}
}

bool spg_Init()
{
	render_end_schid = sh4_sched_register(0, &rend_end_render);
	vblank_schid = sh4_sched_register(0, &spg_line_sched);

	return true;
}

void spg_Term()
{
	sh4_sched_unregister(render_end_schid);
	render_end_schid = -1;
	sh4_sched_unregister(vblank_schid);
	vblank_schid = -1;
}

void spg_Reset(bool hard)
{
	CalculateSync();

	SH4FastEnough = false;
	cpu_time_idx = 0;
	cpu_cycles.fill(0);
	real_times.fill(0.0);
	maple_int_pending = false;
	lightgun_line = 0xffff;
	lightgun_hpos = 0;
}

void scheduleRenderDone(TA_context *cntx)
{
	int cycles = 4096;
	if (cntx != nullptr)
	{
		if (settings.platform.isNaomi2()) {
			cycles = 1500000;
		}
		else
		{
			int size = 0;
			for (TA_context *c = cntx; c != nullptr; c = c->nextContext)
				size += c->tad.thd_data - c->tad.thd_root;
			cycles = std::min(450000 + size * 100, 1500000);
		}
	}
	sh4_sched_request(render_end_schid, cycles);
}

void spg_Serialize(Serializer& ser)
{
	if (cfgLoadBool("dojo", "SpgTrace", false))
		NOTICE_LOG(SAVESTATE, "SPGTRACE save:    clc_pvr_scanline=%u prv_cur_scanline=%u"
				" Line_Cycles=%u Frame_Cycles=%u maple_int_pending=%d",
				clc_pvr_scanline, prv_cur_scanline, Line_Cycles, Frame_Cycles,
				(int)maple_int_pending);
	ser << clc_pvr_scanline;
	ser << maple_int_pending;
	ser << pvr_numscanlines;
	ser << prv_cur_scanline;
	ser << Line_Cycles;
	ser << Frame_Cycles;
	ser << lightgun_line;
	ser << lightgun_hpos;
}
void spg_Deserialize(Deserializer& deser)
{
	if (deser.version() < Deserializer::V30)
		deser.skip<u32>(); // in_vblank
	deser >> clc_pvr_scanline;
	if (deser.version() >= Deserializer::V12)
	{
		deser >> maple_int_pending;
		if (deser.version() >= Deserializer::V14)
		{
			deser >> pvr_numscanlines;
			deser >> prv_cur_scanline;
			deser >> Line_Cycles;
			deser >> Frame_Cycles;
			deser >> lightgun_line;
			deser >> lightgun_hpos;
		}
	}
	if (deser.version() < Deserializer::V14)
		CalculateSync();
	// TEMPORARY DIAGNOSTIC (pool drift hunt, 2026-09-07): the first byte to
	// differ between two restores of ONE blob is the first byte of the spg
	// block, i.e. clc_pvr_scanline, and the difference ALTERNATES with restore
	// parity (members 1,3,5 agree; 2,4 agree and differ from them). Print the
	// SPG's whole phase on every restore so the alternation can be attributed
	// to a value rather than inferred from a hash.
	if (cfgLoadBool("dojo", "SpgTrace", false))
		NOTICE_LOG(SAVESTATE, "SPGTRACE restore: clc_pvr_scanline=%u prv_cur_scanline=%u"
				" numscanlines=%u Line_Cycles=%u Frame_Cycles=%u maple_int_pending=%d",
				clc_pvr_scanline, prv_cur_scanline, pvr_numscanlines,
				Line_Cycles, Frame_Cycles, (int)maple_int_pending);

}

/*
	A LOADED MACHINE WITH ITS RASTER SWITCHED OFF IS NOT A MACHINE.

	`[MEASURED 2026-09-11]` a state in this tree's own test data restores with
	sch_list[vblank_schid].end == -1. The emulator then runs the SH4 flat out -
	3,468,966,592 cycles in three seconds - and completes ZERO frames, because
	spg_line_sched is what advances the scanline and only spg_line_sched can
	re-arm spg_line_sched. Every fidelity check passes: the bytes round-trip
	perfectly, and what they faithfully describe is a machine that cannot run.

	CALLED FROM dc_loadstate, NOT FROM spg_Deserialize. `[MEASURED 2026-09-11]`
	it WAS in spg_Deserialize first, and did nothing at all - no log line, which
	is what said so. `[SOURCE]` dc_deserialize runs pvr::deserialize before
	sh4::deserialize, and the scheduler table arrives with the latter, so the
	repair ran against a value that had not been loaded yet and was then
	overwritten by the -1 it existed to remove. A fix-up for the whole machine
	belongs where the whole machine has finished loading.

	SAFE TO REPAIR UNCONDITIONALLY, and that is worth stating rather than
	assuming. `[SOURCE]` spg_line_sched has exactly one exit, `return
	getNextSpgInterrupt()`, which is always positive, and no site in the tree
	calls sh4_sched_request(vblank_schid, -1). There is no legitimate resting
	state in which a Dreamcast's raster is descheduled, so this cannot mask a
	correct one.

	LOUD, because a silent repair would hide the fixture that produced it - and
	those fixtures are on people's disks, inside clips already recorded.
	core/hw/sh4/sh4_sched.cpp stops NEW ones being written; this rescues the old.
*/
// Whether the LAST load needed the repair. Read by verifyLoadedStateIdempotent,
// which would otherwise report the repair as a serializer defect - see there.
static bool scheduleRepaired = false;

bool spg_ScheduleWasRepaired()
{
	return scheduleRepaired;
}

void spg_RepairSchedule()
{
	scheduleRepaired = false;
	if (sh4_sched_is_scheduled(vblank_schid))
		return;
	/*
		DESCHEDULED AND ABOUT TO RE-ARM ITSELF IS NOT DESCHEDULED.

		`[MEASURED 2026-09-11]` scripts/tests/slots.lua saves AND loads from
		inside a Lua vblank hook, which runs inside spg_line_sched, which runs
		inside handle_cb - so its state file carries the -1 and its machine
		never wedges, because handle_cb re-arms the event the moment the
		callback returns. Repairing there changes a machine that was fine and
		broke that test's round-trip identity.

		The wedge needs the OTHER case: a load from the UI or the deferred
		point, outside any callback, where nothing will ever re-arm it.
	*/
	if (sh4_sched_in_callback(vblank_schid))
		return;
	scheduleRepaired = true;
	WARN_LOG(SAVESTATE, "SPG REPAIR: this state was saved from inside a scheduler "
			"callback - the raster restored DESCHEDULED and the machine would have "
			"completed no frames. Re-arming it (core/hw/sh4/sh4_sched.cpp)");
	sh4_sched_request(vblank_schid, getNextSpgInterrupt());
}
