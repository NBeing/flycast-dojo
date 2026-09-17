#pragma once
#include "types.h"

/*
	THE ORACLE - the machine as a number, and the movie as a number.

	`[2026-09-17]` ONE HEADER for what SIX private fingerprints did before it:
	sendequiv.cpp's hashNow (static), lua.cpp's hashState (static, Lua-only), the
	netplay MD5 in nullDC.cpp, verifyLoadedStateIdempotent's byte compare, fsttest's
	sha256-of-a-state-file, and oracle_probe.lua's FNV over guest RAM. Not one of
	them was declared anywhere a C++ step could call. core/determinism.h:76-88 is
	the tree's own scar - "an earlier draft added a second MD5-based one before
	noticing; the note survives so nobody adds a third." This header exists so the
	next fingerprint is a call, not a fourth implementation.

	A HASH IS ITS DOMAIN, NOT ITS FUNCTION (emuapi MODEL.md). State the domain:

	  machineHash  XXH32 over exactly the bytes dc_serialize writes: the MACHINE and
	               nothing else. NOT the movie (session_inputs is not serialized), NOT
	               dojo.frame_number, NOT the trailer lua's savestate.hash() appends -
	               so it is not that number, on purpose: a same-build, same-run A/B
	               compare needs a consistent function of the machine, not a portable
	               identity. Never keyed on, never persisted. READ ONLY WHILE THE
	               EMULATOR IS STOPPED (Paused): a running machine is not a value, and
	               serializing it mid-frame is the "machine advancing" false positive
	               nullDC's verify probe already had to learn to name.
	  movieHash    Dojo::MoviePrefixHash over EVERY authored row: the INPUTS and
	               nothing else. Its odd basis is deliberate (dojo.cpp:637-650) and this
	               header inherits it - a second implementation "corrected" to FNV-1a
	               would agree with itself and disagree with every clip on disk.

	The GAME-STATE oracle is not wrapped here: tas_mvc2::peekCombo / comboPeak /
	mapValidated / readRamSafe (core/dojo/mvc2.h) already have one owner and a public
	header. A tour step that needs the combo byte includes mvc2.h.

	The precondition is a function too: a gate that hashes a running machine has
	measured noise, and "unmeasured" must be a verdict the gate can give.
*/
namespace roll {
namespace oracle {

//! XXH32 over the machine's serialized bytes. Call only when machineStopped().
u32  machineHash();

//! Dojo::MoviePrefixHash over the whole movie (every row of session_inputs).
u64  movieHash();

//! The precondition for machineHash(): the emulator is not running.
bool machineStopped();

//! dojo:PanelSelfTest - pure claims about the hash function, no machine.
void selfTest();

}	// namespace oracle
}	// namespace roll
