#pragma once
#include "types.h"
#include <functional>
#include <string>
#include <vector>

/*
	THE SURFACE TOUR - a self-driving, human-watchable test of the whole TAS surface.

	`[2026-09-17]` the journey suite proves each feature's infrastructure; nothing walked
	the surface the way a person does. The tour does: load the game, load David's
	savestate, REBIND every window's hotkey through the real rebind engine, open and
	close every window WITH those hotkeys, then exercise each feature one by one -
	paced 1 s per click and 2 s per record action, narrated on screen, and scored so a
	harness (scripts/surfacetourtest.sh) reads the same run headless.

	IN-PROCESS INPUT ONLY. A key is "pressed" by calling the keyboard device's own
	GamepadDevice::gamepad_btn_input(code, pressed) - the exact entry SDL uses - so
	the rebind and the hotkey take the genuine path, headless AND on the user's real
	screen, and nothing is ever synthesised on a real X display.

	THIS HEADER IS THE CONTRACT between the three tracks that build it and is FROZEN
	after the scaffold: the runner (surface_tour.cpp) owns the step machine, the
	narration and the panel hotkeys; the hooks (surface_tour_hooks.cpp and one hook
	compiled into each feature's own translation unit, because its verbs are static
	there) own "drive the real verb, then read back the truth". A change here is a
	coordinator commit, never a track's.
*/
namespace roll {
namespace surfacetour {

//! Click = a UI gesture, dwell dojo:TourBpmMs (1000). Record = an action that changes
//! machine or disk state, dwell dojo:TourRecordMs (2000). The human keeps up either way.
enum class Kind : u8 { Click, Record };

struct Step
{
	const char *name;                //!< narrated verbatim in the banner and the log line
	Kind kind = Kind::Click;
	std::function<bool()> begin;     //!< optional, at t=0 (e.g. rebind::arm); false => FAIL
	std::function<bool()> act;       //!< at t=TourArmMs - "the click"; false => FAIL now
	std::function<bool()> verify;    //!< from t=dwell, polled each tick until true or maxWaitMs
	int  maxWaitMs = 0;              //!< 0 = one verify call at dwell
	bool optional = false;           //!< a missing precondition => SKIP, never FAIL
	bool needsPrev = false;          //!< SKIP when the previous step did not PASS
};

//! The reason for the current FAIL/SKIP - printed as "(why)" and shown in the banner.
void why(const char *fmt, ...);

// ---- the runner (surface_tour.cpp) -------------------------------------------------------
void tick();                          //!< mainui.cpp, beside rebind::probeTick(); no-op unless dojo:SurfaceTour
void selfTest();                      //!< dojo:PanelSelfTest - the pure step machine, no frame, no devices
bool injectKey(u32 code);             //!< keyboard()->gamepad_btn_input(code,true) then (code,false); false = no keyboard
bool focusClearRequested();           //!< the banner body clears ImGui focus when true (one frame)
int  current();                       //!< index of the step in flight; -1 when idle or done
int  total();
const char *stepName(int i);
int  verdict(int i);                  //!< -1 pending, 0 FAIL, 1 PASS, 2 SKIP
const char *lastWhy();
void registerSurfaceTourPanel();      //!< nullDC.cpp, after registerTimelinePanel()

// ---- per-feature hooks (surface_tour_hooks.cpp + one per feature TU) ------------------
// Contract: each runs Paused, in WRITE (dojo.play_match == false), with slot 0 loaded and
// a clip bound. It drives the REAL verb the UI button calls, then READS BACK the truth
// (state, file, mapping) and returns that; it calls why() before returning false.
namespace hooks {
constexpr int kScratchSlot = 99;
bool rollEditFlipUndo();                          // roll_panel.cpp
bool statesLabelRoundTrip();                      // surface_tour_hooks.cpp (Host::setSlotLabel/slotView)
bool saveScratchSlot();
bool loadScratchSlot();
bool deleteScratchSlot();                         // surface_tour_hooks.cpp
bool slotNext();
bool slotPrev();                                  // surface_tour_hooks.cpp (inject the LIVE SLOT_NEXT/PREV code)
bool driverRead();
bool driverReadWrite();
bool driverWrite();                               // surface_tour_hooks.cpp (gui_set_driver + session::mode())
bool senderSend();
bool senderStop();                                // sender_panel.cpp (patternToCanon -> tas_auto::playLive)
bool notepadAnalyze();                            // notepad_panel.cpp (editor().SetText + analyze)
bool snippetsPlace();                             // snippets_panel.cpp (the probe body, seeded)
bool macrosPlace();                               // macros_panel.cpp   (the probe body, seeded)
bool branchCreate();
bool branchCheckout();
bool branchBackToMain();                          // surface_tour_hooks.cpp
bool labAddTest();
bool labTrashTest();                              // lab_panel.cpp
bool capturesStart();
bool capturesStop();                              // surface_tour_hooks.cpp (videorec + gui_step_frames)
bool fstArmSweep();
bool fstSweepDone();                              // fst.cpp (armFixedSweep() shared with fstProbe)
bool exportLaunch();
bool exportDone();                                // surface_tour_hooks.cpp (bexport)
//! dojo:TourHook=<name> - drive ONE hook once the machine is ready, log
//! `TOUR HOOK: <name> -> PASS|FAIL (<why>)`. The unit drive for a track working alone.
void probeTick();
}

}	// namespace surfacetour

// Declared here so no track has to edit another's translation unit to reach these: both
// already exist with external linkage in their own files (lab_panel.cpp, sender_panel.cpp).
namespace lab    { bool addTestFromSlot(int slot); }
namespace sender { bool patternToCanon(const std::string& text, std::vector<u16>& out, std::string& err); }

}	// namespace roll
