#include "types.h"
#include "sh4_if.h"
#include "sh4_sched.h"
#include "cfg/cfg.h"
#include "serialize.h"

#include <algorithm>
#include <vector>

//sh4 scheduler

/*

	register handler
	request callback at time

	single fire events only

	sh4_sched_register(id)
	sh4_sched_request(id, in_cycles)

	sh4_sched_now()

*/
struct sched_list
{
	sh4_sched_callback* cb;
	void *arg;
	int tag;
	int start;
	int end;
};

/*
	WHICH EVENT IS CURRENTLY INSIDE ITS OWN CALLBACK, or -1.

	`[MEASURED 2026-09-11]` This exists for one reason: handle_cb clears an
	event's `end` BEFORE invoking it and re-arms only after, so for the whole
	duration of a callback that event serializes as DISABLED. A savestate
	written from inside a callback therefore records a machine with that timer
	switched off - and for the SPG that is fatal and self-sealing, because the
	only thing that re-arms the vblank event is the vblank event firing.

	Read only by spg_RepairSchedule, to tell the two cases apart. Nothing about a
	RUNNING machine changes.
*/
static int sh4_sched_in_cb = -1;

static u64 sh4_sched_ffb;
static std::vector<sched_list> sch_list;
static int sh4_sched_next_id = -1;

static u32 sh4_sched_now();

static u32 sh4_sched_remaining(const sched_list& sched, u32 reference)
{
	if (sched.end != -1)
		return sched.end - reference;
	else
		return -1;
}

void sh4_sched_ffts()
{
	u32 diff = -1;
	int slot = -1;

	u32 now = sh4_sched_now();
	for (const sched_list& sched : sch_list)
	{
		u32 remaining = sh4_sched_remaining(sched, now);
		if (remaining < diff)
		{
			slot = &sched - &sch_list[0];
			diff = remaining;
		}
	}

	sh4_sched_ffb -= Sh4cntx.sh4_sched_next;

	sh4_sched_next_id = slot;
	if (slot != -1)
		Sh4cntx.sh4_sched_next = diff;
	else
		Sh4cntx.sh4_sched_next = SH4_MAIN_CLOCK;

	sh4_sched_ffb += Sh4cntx.sh4_sched_next;
}

int sh4_sched_register(int tag, sh4_sched_callback* ssc, void *arg)
{
	sched_list t{ ssc, arg, tag, -1, -1};
	for (sched_list& sched : sch_list)
		if (sched.cb == nullptr)
		{
			sched = t;
			return &sched - &sch_list[0];
		}

	sch_list.push_back(t);

	return sch_list.size() - 1;
}

void sh4_sched_unregister(int id)
{
	if (id == -1)
		return;
	verify(id < (int)sch_list.size());
	if (id == (int)sch_list.size() - 1)
		sch_list.resize(sch_list.size() - 1);
	else
	{
		sch_list[id].cb = nullptr;
		sch_list[id].end = -1;
	}
	sh4_sched_ffts();
}

/*
	Return current cycle count, in 32 bits (wraps after 21 dreamcast seconds)
*/
static u32 sh4_sched_now()
{
	return sh4_sched_ffb - Sh4cntx.sh4_sched_next;
}

/*
	Return current cycle count, in 64 bits (effectively never wraps)
*/
u64 sh4_sched_now64()
{
	return sh4_sched_ffb - Sh4cntx.sh4_sched_next;
}

void sh4_sched_request(int id, int cycles)
{
	verify(cycles == -1 || (cycles >= 0 && cycles <= SH4_MAIN_CLOCK));

	sched_list& sched = sch_list[id];
	sched.start = sh4_sched_now();

	if (cycles == -1)
	{
		sched.end = -1;
	}
	else
	{
		sched.end = sched.start + cycles;
		if (sched.end == -1)
			sched.end++;
	}

	sh4_sched_ffts();
}

bool sh4_sched_in_callback(int id)
{
	return id != -1 && id == sh4_sched_in_cb;
}

bool sh4_sched_is_scheduled(int id)
{
	return sch_list[id].end != -1;
}

/* Returns how much time has passed for this callback */
static int sh4_sched_elapsed(sched_list& sched)
{
	if (sched.end != -1)
	{
		int rv = sh4_sched_now() - sched.start;
		sched.start = sh4_sched_now();
		return rv;
	}
	else
		return -1;
}

static void handle_cb(sched_list& sched)
{
	int remain = sched.end - sched.start;
	int elapsd = sh4_sched_elapsed(sched);
	int jitter = elapsd - remain;

	sched.end = -1;
	// SAVED AND RESTORED rather than assigned, because a callback that reaches
	// code which ticks the scheduler would otherwise clear the marker for the
	// event still on the stack beneath it. The guard also survives an exception
	// thrown THROUGH the callback, which dc_savestate can do.
	struct InCb {
		int prev;
		explicit InCb(int id) : prev(sh4_sched_in_cb) { sh4_sched_in_cb = id; }
		~InCb() { sh4_sched_in_cb = prev; }
	} inCb((int)(&sched - &sch_list[0]));
	int re_sch = sched.cb(sched.tag, remain, jitter, sched.arg);

	if (re_sch > 0)
		sh4_sched_request(&sched - &sch_list[0], std::max(0, re_sch - jitter));
}

void sh4_sched_tick(int cycles)
{
	if (Sh4cntx.sh4_sched_next >= 0)
		return;

	u32 fztime = sh4_sched_now() - cycles;
	if (sh4_sched_next_id != -1)
	{
		for (sched_list& sched : sch_list)
		{
			int remaining = sh4_sched_remaining(sched, fztime);
			if (remaining >= 0 && remaining <= (int)cycles)
				handle_cb(sched);
		}
	}
	sh4_sched_ffts();
}

void sh4_sched_reset(bool hard)
{
	if (hard)
	{
		sh4_sched_ffb = 0;
		sh4_sched_next_id = -1;
		for (sched_list& sched : sch_list)
			sched.start = sched.end = -1;
		Sh4cntx.sh4_sched_next = 0;
	}
}

void sh4_sched_serialize(Serializer& ser, int id)
{
	ser << sch_list[id].tag;
	ser << sch_list[id].start;
	/*
		WRITTEN AS-IS, INCLUDING THE -1 AN IN-FLIGHT CALLBACK LEAVES BEHIND.

		`[MEASURED 2026-09-11]` the obvious repair - substitute "due now" for an
		event inside its own callback, so it fires on restore - was built, and
		it is worse than it looks. An event whose deadline is NOW is selected by
		sh4_sched_ffts on the next pass, which rewrites Sh4cntx.sh4_sched_next
		and sh4_sched_ffb; both are serialized, so a restored machine no longer
		hashes equal to the one that was saved. scripts/tests/slots.lua caught
		it immediately - "the machine is unchanged across the round trip",
		523635580 -> 1031346583 - and that invariant is the floor under state
		hashes, anchors and every determinism comparison in this tree. Not worth
		trading for a fix the load side can make on its own.

		The repair is in spg_RepairSchedule (core/hw/pvr/spg.cpp), which also
		rescues the states already written this way.
	*/
	ser << sch_list[id].end;
}

void sh4_sched_deserialize(Deserializer& deser, int id)
{
	deser >> sch_list[id].tag;
	deser >> sch_list[id].start;
	deser >> sch_list[id].end;
	if (cfgLoadBool("dojo", "SchedTrace", false))
		NOTICE_LOG(SAVESTATE, "SCHEDTRACE dser id=%d end=%d", id, sch_list[id].end);
}

// FIXME modules should save their scheduling data so that it doesn't depend on their scheduler id
namespace aica
{
// hw/aica/aica.cpp
extern int aica_schid;
extern int rtc_schid;
// hw/aica/aica_if.cpp
extern int dma_sched_id;
}
// hw/gdrom/gdromv3.cpp
extern int gdrom_schid;
// hw/maple/maple_if.cpp
extern int maple_schid;
// hw/pvr/spg.cpp
extern int render_end_schid;
extern int vblank_schid;
// hw/sh4/modules/tmu.cpp
extern int tmu_sched[3];

void sh4_sched_serialize(Serializer& ser)
{
	ser << sh4_sched_ffb;

	sh4_sched_serialize(ser, aica::aica_schid);
	sh4_sched_serialize(ser, aica::rtc_schid);
	sh4_sched_serialize(ser, gdrom_schid);
	sh4_sched_serialize(ser, maple_schid);
	sh4_sched_serialize(ser, aica::dma_sched_id);
	for (int id : tmu_sched)
		sh4_sched_serialize(ser, id);
	sh4_sched_serialize(ser, render_end_schid);
	sh4_sched_serialize(ser, vblank_schid);
}

void sh4_sched_deserialize(Deserializer& deser)
{
	deser >> sh4_sched_ffb;

	if (deser.version() >= Deserializer::V19 && deser.version() <= Deserializer::V31)
		deser.skip<u32>();		// sh4_sched_next_id

	sh4_sched_deserialize(deser, aica::aica_schid);
	sh4_sched_deserialize(deser, aica::rtc_schid);
	sh4_sched_deserialize(deser, gdrom_schid);
	sh4_sched_deserialize(deser, maple_schid);
	sh4_sched_deserialize(deser, aica::dma_sched_id);
	for (int id : tmu_sched)
		sh4_sched_deserialize(deser, id);
	sh4_sched_deserialize(deser, render_end_schid);
	sh4_sched_deserialize(deser, vblank_schid);
}
