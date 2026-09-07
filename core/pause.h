#pragma once
#include "types.h"

// Pause arbiter. "Stopped" is several INDEPENDENT facts, not one.
//
// [SOURCE] fbneo-rr src/burner/win32/run.cpp, whose comment names the bugs a
// single flag causes: "the old single-flag code let a closing dialog silently
// resume a GDB-stopped target, and let GDB's 'continue' cancel a user pause."
// flycast had started down the same road by hand - gui.cpp read
// `dojo.manual_pause || dojo.buffering || dojo.stepping`, an OR of reasons with
// no owner, and lua.cpp carried four copies of a save-and-restore ritual around
// operations that merely needed the machine still.
//
// Each owner sets and clears only ITS OWN reason, so no owner can cancel
// another's pause.
//
// WHAT THIS DOES NOT DO: it does not decide what is on screen. flycast's
// gui_state answers that (Closed / Paused / Commands / Loading ...), and the
// two ImGui frame streams key off it. This answers only "is the machine
// stopped, and who wants it stopped". Keeping the two apart is the point: the
// bug that prompted the arbiter was a Lua tool pausing itself into the MENU,
// where it could no longer draw - a presentation question this mask has no
// opinion about.
namespace pausing
{
	enum : u32
	{
		USER   = 1 << 0,	// explicit: the pause key, the menu, Lua emu.pause
		STEP   = 1 << 1,	// frame advance asked to land stopped
		LUA    = 1 << 2,	// a script asked for it, and only a script may release it
		MODAL  = 1 << 3,	// an operation needs the machine still (savestates, ...)
	};

	void set(u32 reason);
	void clear(u32 reason);
	u32  mask();
	inline bool active(u32 reason) { return (mask() & reason) != 0; }
	// Game load/exit: forget every reason WITHOUT side effects. A reason that
	// outlives the machine it referred to would stop the next one.
	void resetAll();

	// RAII for the ritual this replaces:
	//     bool restart = false;
	//     if (gui_state == GuiState::Closed) { gui_open_settings(); restart = true; }
	//     ...
	//     if (restart) gui_open_settings();
	// which restores by re-calling a TOGGLE, so anything that changed state in
	// between gets clobbered - and which is wrong on every early return.
	struct Scoped
	{
		u32 reason;
		bool wasSet;
		explicit Scoped(u32 r = MODAL) : reason(r), wasSet(active(r)) { set(r); }
		~Scoped() { if (!wasSet) clear(reason); }
		Scoped(const Scoped&) = delete;
		Scoped& operator=(const Scoped&) = delete;
	};
}
