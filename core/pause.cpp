#include "pause.h"
#include "emulator.h"
#include "log/LogManager.h"

namespace pausing
{
static u32 g_mask = 0;
// THE ARBITER ONLY RESTARTS WHAT IT STOPPED.
//
// This is what keeps it from fighting the gui_state machine, which also calls
// emu.stop()/emu.start() (gui_open_pause, gui_open_settings). If the machine was
// already stopped when the first reason arrived, the arbiter records the reason
// and touches nothing; dropping that reason then also touches nothing, so
// closing a dialog cannot resume a machine the menu stopped - or vice versa.
static bool g_weStopped = false;

static void apply(u32 old)
{
	if (g_mask == old)
		return;
	if (old == 0 && g_mask != 0)
	{
		if (emu.running())
		{
			try {
				emu.stop();
				g_weStopped = true;
			} catch (const FlycastException& e) {
				ERROR_LOG(COMMON, "pause: emu.stop() failed: %s", e.what());
				g_weStopped = false;
			}
		}
	}
	else if (old != 0 && g_mask == 0 && g_weStopped)
	{
		g_weStopped = false;
		try {
			emu.start();
		} catch (const FlycastException& e) {
			ERROR_LOG(COMMON, "pause: emu.start() failed: %s", e.what());
		}
	}
	NOTICE_LOG(COMMON, "pause: mask %02x -> %02x%s", old, g_mask,
			g_weStopped ? " (arbiter owns the stop)" : "");
}

void set(u32 reason)   { u32 old = g_mask; g_mask |= reason;  apply(old); }
void clear(u32 reason) { u32 old = g_mask; g_mask &= ~reason; apply(old); }
u32  mask()            { return g_mask; }

void resetAll()
{
	g_mask = 0;
	g_weStopped = false;
}
}
