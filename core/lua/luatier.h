#pragma once
#include <string>

/*
	WHAT A SCRIPT IS ALLOWED TO DO, HERE, NOW.

	`emuapi/spec.lua` specifies this and is the owner of the vocabulary; this is
	the host side of it, and the names are taken from there rather than invented:

	    observer   reads and draws. Cannot touch the machine.
	    mutator    drives input and writes memory.
	    full       savestates, recording, emulator control.

	TWO DIFFERENT QUESTIONS, and the spec is emphatic that conflating them is how
	a host ends up answering neither:

	    supports(name)   PORTABILITY:    does this host have the function
	    declare{...}     AUTHORISATION:  may this script call it, here, now

	A host can answer yes to the first and no to the second.

	WHY THIS EXISTS AT ALL. The motivating case is netplay. A script that writes
	memory or drives input in a rollback session desyncs the peer; one that reads
	and draws cannot. Upstream flycast-dojo refuses Lua ENTIRELY when online -
	the spec calls that "the bluntest possible version of this" - so an overlay
	that could not desync anything is refused alongside a script that could.

	THREE RULES FROM THE SPEC, all of them load-bearing:

	  THE GRANTED TIER MAY BE NARROWER THAN THE ONE REQUESTED, and that is NOT an
	  error. A script that can degrade should be able to see that it must.

	  DECLARING IS IRREVERSIBLE AND HAPPENS ONCE. A script that could widen its
	  own tier later would not be declaring anything, so declaring twice raises.

	  UNDECLARED MEANS FULL - the script's own default, so every script written
	  before this existed keeps working. The SESSION CEILING still applies on top,
	  which is the whole point: declaring is opting in to being restricted, and
	  not declaring is not opting out of the session's limits.

	A REFUSAL IS LOUD. spec.lua's failure tier 1: a call the tier forbids RAISES
	rather than returning nil, because a mutation that quietly did nothing is the
	worst of the three outcomes.
*/
namespace luatier
{

enum class Tier { Off, Observer, Mutator, Full };

const char *name(Tier t);
bool parse(const std::string& s, Tier& out);

//! The most this SESSION will allow, whatever a script asks for.
Tier ceiling();

//! What is actually in force: the narrower of the ceiling and any declaration.
Tier granted();

/*
	Declare once. `out` is what was granted, which may be narrower.
	False (with `err`) only for a malformed tier or a second declaration -
	being narrowed is a normal answer and not a failure.
*/
bool declare(const std::string& want, Tier& out, std::string& err);

//! A new script owns a new declaration. Called when the interpreter is opened.
void resetDeclaration();

//! What tier does `what` need? Unknown names need Full, deliberately: a
//! capability nobody classified is not one to hand out by default.
Tier needs(const std::string& what);

//! May a script do `what` right now? Counts nothing; this is the query form.
bool can(const std::string& what);

/*
	The enforcement form. Returns true when permitted; on refusal it COUNTS the
	refusal (per name) and returns false, and the caller raises.

	Counting is not decoration: `emuapi/spec.lua` asks for refusal counters, and
	a script degrading correctly and a script failing silently look identical
	without them.
*/
bool allow(const std::string& what);

//! Refusals so far, total and for one name.
int refusals();
int refusals(const std::string& what);

void selfTest();

}	// namespace luatier
