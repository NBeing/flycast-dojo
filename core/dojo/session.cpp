#include "session.h"
#include "dojo.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "rend/gui.h"
#include "log/LogManager.h"
#include <cstring>

namespace session
{

Kind kind()
{
	// PRECEDENCE, in the order the header declares it. Netplay first: an online
	// session is online whatever else is set, and every TAS answer below would
	// be wrong about who is driving the guest.
	if (config::GGPOEnable || settings.network.online)
		return Kind::Netplay;

	// The macro pair before Replay, because a macro PLAYBACK also sets
	// play_match - so asking about Replay first would swallow it.
	if (cfgLoadBool("dojo", "MacroMode", false))
		return cfgLoadBool("dojo", "PlayMacro", false) ? Kind::PlayMacro : Kind::RecordMacro;

	if (dojo.play_match)
		return Kind::Replay;
	if (cfgLoadBool("dojo", "RecordMatches", false))
		return Kind::RecordMovie;
	return Kind::JustPlay;
}

Mode mode()
{
	// dojo.h's own comment block is the rule; this is that prose given a name.
	//   play_match          = READ (playback overrides both below)
	//   macro_armed         = READ-WRITE (a live signal stomps the active cell)
	//   !macro_armed        = WRITE (advancing overwrites every frame it passes)
	if (dojo.play_match)
		return Mode::Read;
	return dojo.macro_armed ? Mode::ReadWrite : Mode::Write;
}

bool recording() { const Kind k = kind(); return k == Kind::RecordMovie || k == Kind::RecordMacro; }
bool replaying() { const Kind k = kind(); return k == Kind::Replay || k == Kind::PlayMacro; }
bool macro()     { const Kind k = kind(); return k == Kind::RecordMacro || k == Kind::PlayMacro; }
bool netplay()   { return kind() == Kind::Netplay; }
bool readOnly()  { return mode() == Mode::Read; }

bool livePeer()
{
	// Spectate first: its frames come off a socket while play_match is true, so
	// the movie test below would wrongly call it a local session.
	if (cfgLoadBool("dojo", "Receiving", false))
		return true;
	return settings.network.online && !dojo.play_match;
}

bool writeGrow()
{
	// tasWriteGrow, verbatim in meaning and promoted out of MapleApplyAction's
	// body. HasAppendTarget() is the clause that makes a REPLAY the user
	// flipped to READ-WRITE count as writing - which is why this is not
	// !readOnly(), and why the clause lives here rather than at a call site.
	return !dojo.play_match && !settings.network.online && !dojo.replay.ggpo_session
			&& (cfgLoadBool("dojo", "RecordMatches", false)
				|| cfgLoadBool("dojo", "PlayMacro", false)
				|| dojo.replay.HasAppendTarget());
}

const char *label()
{
	switch (kind())
	{
	case Kind::Netplay:     return "NETPLAY";
	case Kind::PlayMacro:   return "PLAY MACRO";
	case Kind::RecordMacro: return "RECORD MACRO";
	case Kind::Replay:      return "REPLAY";
	case Kind::RecordMovie: return "RECORD MOVIE";
	case Kind::JustPlay:    break;
	}
	return "JUST PLAY";
}

/*
	SELF-TEST for the precedence table, which is the part that cannot be read
	off the code: every arm is individually plausible, and only the ORDER
	distinguishes a deliberate table from one that fell out of how the
	conditions were typed.

	It drives the real kind() through cfgSetVirtual rather than a copy of the
	logic - a self-test against a copy proves the copy - and restores every key
	it touched, because these are live settings and the session continues after.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "SESSION SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const bool wasMacro  = cfgLoadBool("dojo", "MacroMode", false);
	const bool wasPlay   = cfgLoadBool("dojo", "PlayMacro", false);
	const bool wasRecord = cfgLoadBool("dojo", "RecordMatches", false);
	const bool wasPlayM  = dojo.play_match;
	const bool wasArmed  = dojo.macro_armed;
	auto set = [](const char *k, bool v) { cfgSetVirtual("dojo", k, v ? "yes" : "no"); };

	set("MacroMode", false); set("PlayMacro", false); set("RecordMatches", false);
	dojo.play_match = false;
	claim("a plain boot is JustPlay", kind() == Kind::JustPlay);

	set("RecordMatches", true);
	claim("RecordMatches alone is RecordMovie", kind() == Kind::RecordMovie);

	dojo.play_match = true;
	claim("play_match OUTRANKS RecordMatches", kind() == Kind::Replay);

	// THE ORDER CLAIM. A macro playback also sets play_match, so asking about
	// Replay first would swallow every macro session - this is the arm that
	// makes the table an ordering rather than a set.
	set("MacroMode", true);
	claim("the macro pair outranks Replay", kind() == Kind::PlayMacro || kind() == Kind::RecordMacro);
	set("PlayMacro", true);
	claim("MacroMode + PlayMacro is PlayMacro", kind() == Kind::PlayMacro);
	set("PlayMacro", false);
	claim("MacroMode without PlayMacro is RecordMacro", kind() == Kind::RecordMacro);

	// MODE IS A SEPARATE AXIS, and this is the claim that a single enum could
	// not have made: the same session can change who drives without changing
	// what it is for.
	dojo.play_match = true;
	claim("a replay reads", mode() == Mode::Read);
	dojo.play_match = false;
	dojo.macro_armed = true;
	claim("armed and not replaying is READ-WRITE", mode() == Mode::ReadWrite);
	dojo.macro_armed = false;
	claim("unarmed and not replaying is WRITE", mode() == Mode::Write);

	// label() MUST TRACK THE KIND. "never null" was the first version of this
	// claim and it cannot fail - every arm returns a string literal. Asserting
	// it against a kind we just forced is the version that can.
	set("MacroMode", false); set("PlayMacro", false); set("RecordMatches", true);
	dojo.play_match = false;
	claim("label() reflects the kind", std::strcmp(label(), "RECORD MOVIE") == 0);
	dojo.play_match = true;
	claim("...and changes with it", std::strcmp(label(), "REPLAY") == 0);

	set("MacroMode", wasMacro); set("PlayMacro", wasPlay); set("RecordMatches", wasRecord);
	dojo.play_match = wasPlayM;
	dojo.macro_armed = wasArmed;

	NOTICE_LOG(RENDERER, "SESSION SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace session
