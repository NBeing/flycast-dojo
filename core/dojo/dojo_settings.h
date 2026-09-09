/*
	THE OWNER OF `dojo:` CONFIG. One table, one read path, one writer.

	"The current value of a setting" is answered three ways in this tree and
	they do not agree:

	  config::X       an Option object that CACHES the value read at startup
	  cfgLoad*        a live read of the store
	  cfgSetVirtual   a shadow entry that wins every later read and is never
	                  written to disk (ini.cpp: get_entry checks the virtual
	                  section first and returns; save() iterates only the
	                  regular sections)

	`[MEASURED 2026-09-08]` 184 `cfg*("dojo", ...)` call sites across 57 keys.
	38 of those keys are cfg-only - the TAS-native set, and exactly the set the
	studio port grows. This owns those.

	THE 19 DUAL-MECHANISM KEYS ARE DELIBERATELY ABSENT. Every one is a netplay
	key with an `Option` that `determinism.cpp`, `aica_if.cpp` and
	`Settings::load` read. Absorbing them would mean either deleting the Option
	- which breaks `determinism::isDeterministicRun()` - or writing through to
	it, at which point this is a FOURTH mechanism rather than a replacement for
	three. The cfg-only 38 are the whole win and carry none of that.

	`launchable` IS NOT DECORATION - IT IS WHAT MAKES set() CORRECT.

	A `-config` flag writes the shadow, and the shadow beats every later
	persisted write for the life of the process (`cl.cpp`'s help text says
	otherwise and is wrong). So a UI toggle on a key a launch flag may set must
	write BOTH, or it looks broken to the person pressing it.

	But writing the shadow UNCONDITIONALLY is the opposite bug: the virtual
	entry wins every later read and `save()` never emits it, so the value on
	disk becomes invisible and the key is unpersistable for the rest of the
	process. `[MEASURED 2026-09-08]` `core/dojo/tas_ui.cpp` shipped exactly that
	a few hours before this file existed, on a per-panel zoom key that no launch
	flag has ever set - "write both" copied as a rule rather than as a decision.

	The table decides. Not the caller, and not a habit.
*/
#pragma once
#include "types.h"
#include <string>

namespace dojocfg
{

/*
	THE TABLE. Name, type, default and launchability, stated ONCE.

	`Key` is generated from it, so a misspelled key is a COMPILE ERROR rather
	than a silent fallback to a default that reads like a deliberate setting -
	which is the present failure mode: `cfgLoadBool("dojo", "MacrMode", false)`
	compiles, runs, and answers false forever.

	Defaults are spelled as strings so one macro can carry three types. The
	cost is that a default is written here rather than shared with a constant;
	SlotCycleCount's 100 must track hostfs::MAX_SAVESTATE_SLOTS, and that is
	noted rather than hidden.

	`VerifyState` is ABSENT ON PURPOSE. Its default is not a literal - it is
	`determinism::isDeterministicRun()`, computed per launch - and a static
	table cannot hold it. Forcing it in would mean either freezing the
	computation or adding a callback column for one key.
*/
#define DOJO_KEYS(_)                                                                  \
	/*  key                type  default     launchable */                            \
	_(AutoCapture,        Bool, "no",       true)                                     \
	_(AutoPlay,           Bool, "no",       true)                                     \
	_(AutoSeekState,      Int,  "-1",       true)                                     \
	_(AviHeight,          Int,  "0",        true)                                     \
	_(AviWidth,           Int,  "0",        true)                                     \
	_(CaptureEncoder,     Str,  "prores",   true)                                     \
	_(CaptureLog,         Bool, "yes",      true)                                     \
	_(CineFormQuality,    Str,  "film1",    true)                                     \
	_(DelaySelect,        Bool, "no",       true)                                     \
	_(DockGameViewport,   Bool, "yes",      true)                                     \
	_(FidelityDump,       Bool, "no",       true)                                     \
	_(FrameskipOffset,    Int,  "1",        true)                                     \
	_(GamePanel,          Bool, "yes",      true)                                     \
	_(KeepAviWav,         Int,  "0",        true)                                     \
	_(LastRomPath,        Str,  "",         false)  /* written on boot, never a flag */ \
	_(MacroMode,          Bool, "no",       true)                                     \
	_(MacroProbe,         Str,  "",         true)                                     \
	_(MemHunt,            Bool, "no",       true)                                     \
	_(MemTrace,           Bool, "no",       true)                                     \
	_(MouseAsController,  Bool, "no",       true)                                     \
	_(OnEnterFile,        Str,  "",         true)                                     \
	_(PanelSelfTest,      Bool, "no",       true)                                     \
	_(PendingNotes,       Str,  "",         false) /* UI-authored clip metadata */     \
	_(PendingTags,        Str,  "",         false) /* ditto */                        \
	_(PlayMacro,          Bool, "no",       true)                                     \
	_(PostEncode,         Str,  "cfhd",     true)                                     \
	_(ProResProfile,      Int,  "1",        true)                                     \
	_(ProResQscale,       Int,  "13",       true)                                     \
	_(ResizeProbe,        Str,  "",         true)                                     \
	_(SlotCycleCount,     Int,  "100",      true)  /* == hostfs::MAX_SAVESTATE_SLOTS */ \
	_(SpgTrace,           Bool, "no",       true)                                     \
	_(StateMapLog,        Bool, "no",       true)                                     \
	_(TextApply,          Bool, "no",       true)                                     \
	_(TextRoundTrip,      Bool, "no",       true)                                     \
	_(UiIni,              Bool, "yes",      true)                                     \
	_(VerifyInputs,       Bool, "no",       true)                                     \
	_(ViewportTrace,      Bool, "no",       true)

enum class Key
{
#define DOJO_KEY_ENUM(name, type, def, launch) name,
	DOJO_KEYS(DOJO_KEY_ENUM)
#undef DOJO_KEY_ENUM
	Count,
};

//! LIVE reads, virtual-aware because that is what the store does. The default
//! comes from the table, never from the call site - which is what stops two
//! call sites disagreeing about what "off" means.
bool        getBool(Key k);
int         getInt (Key k);
std::string getStr (Key k);

//! THE WRITER, and the reason this namespace exists. Persists always; writes
//! the shadow as well only for a `launchable` key. See the header comment for
//! why both directions are bugs.
void set(Key k, bool v);
void set(Key k, int v);
void set(Key k, const std::string& v);

//! Is this key's value currently coming from the shadow? For a settings UI
//! that should be able to SAY "overridden from the command line" rather than
//! showing a control that appears not to work.
bool isOverridden(Key k);

//! The key's name as it appears in emu.cfg. For logs; never for building a
//! second lookup.
const char *name(Key k);

//! Gated on `dojo:PanelSelfTest`, alongside the panel registry's.
void selfTest();

}	// namespace dojocfg
