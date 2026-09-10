#include "luatier.h"
#include "dojo/session.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <map>

namespace luatier
{

namespace {

Tier declared_ = Tier::Full;		// spec: UNDECLARED MEANS FULL
bool hasDeclared_ = false;
std::map<std::string, int> refused_;
int refusedTotal_ = 0;

Tier narrower(Tier a, Tier b) { return (int)a < (int)b ? a : b; }

/*
	WHAT EACH CAPABILITY NEEDS, as data.

	A prefix table rather than a check at each call site, so the classification
	is in ONE place and can be read as a whole - which is the only way to notice
	that something is missing from it.
*/
struct Rule { const char *prefix; Tier need; };
const Rule RULES[] = {
	// Reading and drawing. Everything an overlay does.
	{ "ui.",            Tier::Observer },
	{ "gui.",           Tier::Observer },
	{ "memory.read",    Tier::Observer },
	//! WATCHING IS READING. Without this line it falls through to the
	//! unclassified default of `full`, which is the safe direction for an
	//! omission but the wrong answer for an overlay - and an overlay noticing a
	//! value change is the whole use case.
	{ "memory.watch",   Tier::Observer },
	{ "frame.",         Tier::Observer },
	{ "state.",         Tier::Observer },
	{ "session.",       Tier::Observer },
	{ "movie.read",     Tier::Observer },

	// Touching the machine.
	{ "memory.write",   Tier::Mutator },
	{ "input.",         Tier::Mutator },
	{ "movie.edit",     Tier::Mutator },

	// Owning the session.
	{ "savestate.",     Tier::Full },
	{ "emulator.",      Tier::Full },
	{ "replay.",        Tier::Full },
	{ "video.",         Tier::Full },
};

}	// namespace

const char *name(Tier t)
{
	switch (t)
	{
	case Tier::Off:      return "off";
	case Tier::Observer: return "observer";
	case Tier::Mutator:  return "mutator";
	case Tier::Full:     break;
	}
	return "full";
}

bool parse(const std::string& s, Tier& out)
{
	if (s == "off")      { out = Tier::Off;      return true; }
	if (s == "observer") { out = Tier::Observer; return true; }
	if (s == "mutator")  { out = Tier::Mutator;  return true; }
	if (s == "full")     { out = Tier::Full;     return true; }
	return false;
}

Tier ceiling()
{
	// THE SESSION'S OWN LIMIT FIRST. A script cannot drive input or write memory
	// in a rollback session without desyncing the peer - and it CAN read and
	// draw, which is exactly the distinction upstream's all-or-nothing switch
	// cannot make.
	Tier t = session::netplay() ? Tier::Observer : Tier::Full;

	// And a user may lower it further, never raise it. `dojo:LuaTier` is a
	// ceiling on a ceiling; a config that could widen a netplay session would
	// be a config that desyncs other people's games.
	Tier cfgT;
	if (parse(cfgLoadStr("dojo", "LuaTier", "full"), cfgT))
		t = narrower(t, cfgT);
	return t;
}

Tier granted() { return narrower(ceiling(), declared_); }

bool declare(const std::string& want, Tier& out, std::string& err)
{
	Tier w;
	if (!parse(want, w))
	{
		err = "unknown tier '" + want + "' (off, observer, mutator, full)";
		return false;
	}
	if (hasDeclared_)
	{
		// A BUG, NOT A WIDENING. A script that could re-declare would not be
		// declaring anything.
		err = "already declared; declaring twice is a bug, not a widening";
		return false;
	}
	hasDeclared_ = true;
	declared_ = w;
	out = granted();
	return true;
}

void resetDeclaration()
{
	hasDeclared_ = false;
	declared_ = Tier::Full;
	refused_.clear();
	refusedTotal_ = 0;
}

Tier needs(const std::string& what)
{
	for (const Rule& r : RULES)
		if (what.compare(0, std::string(r.prefix).size(), r.prefix) == 0)
			return r.need;
	// UNCLASSIFIED MEANS FULL. A capability nobody has classified is not one to
	// hand out by default - the safe direction for an omission is refusing a
	// script, not letting one through.
	return Tier::Full;
}

bool can(const std::string& what)
{
	const Tier g = granted();
	return g != Tier::Off && (int)needs(what) <= (int)g;
}

bool allow(const std::string& what)
{
	if (can(what))
		return true;
	refused_[what]++;
	refusedTotal_++;
	if (cfgLoadBool("dojo", "LuaTierTrace", false))
		NOTICE_LOG(COMMON, "LUA TIER: refused %s - needs %s, granted %s (%d refusals)",
				what.c_str(), name(needs(what)), name(granted()), refusedTotal_);
	return false;
}

int refusals() { return refusedTotal_; }

int refusals(const std::string& what)
{
	auto it = refused_.find(what);
	return it == refused_.end() ? 0 : it->second;
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(COMMON, "LUATIER SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	const bool wasOnline = settings.network.online;
	const std::string wasCfg = cfgLoadStr("dojo", "LuaTier", "full");
	settings.network.online = false;
	cfgSetVirtual("dojo", "LuaTier", "full");
	resetDeclaration();

	claim("an offline session allows everything", ceiling() == Tier::Full);
	// UNDECLARED IS FULL - the spec's compatibility rule, and the one a host
	// could quietly get wrong by defaulting to observer "to be safe".
	claim("an undeclared script is granted full", granted() == Tier::Full);
	claim("...so an old script may still write memory", can("memory.write"));

	Tier got;
	std::string err;
	claim("declaring narrower is honoured", declare("observer", got, err) && got == Tier::Observer);
	claim("...and it takes effect", !can("memory.write") && can("ui.rect"));
	// IRREVERSIBLE. Widening is what a declaration is supposed to prevent.
	claim("declaring twice is refused", !declare("full", got, err) && !err.empty());
	claim("...and did not widen anything", granted() == Tier::Observer);

	resetDeclaration();
	claim("a bad tier name is refused", !declare("wizard", got, err));

	// ---- the session ceiling ----
	resetDeclaration();
	settings.network.online = true;
	claim("netplay lowers the ceiling to observer", ceiling() == Tier::Observer);
	claim("...and an UNDECLARED script is narrowed by it too",
			granted() == Tier::Observer && !can("input.setButton"));
	// THE POINT OF THE WHOLE EXERCISE: an overlay still runs.
	claim("...while drawing still works, which all-or-nothing could not allow",
			can("ui.text") && can("memory.read"));
	claim("a declaration cannot widen past the session",
			declare("full", got, err) && got == Tier::Observer);

	// ---- refusals are counted ----
	resetDeclaration();
	settings.network.online = true;
	const int before = refusals();
	claim("a permitted call is not counted", allow("ui.text") && refusals() == before);
	claim("a refused call is refused", !allow("memory.write"));
	claim("...and counted, by name", refusals() == before + 1
			&& refusals("memory.write") == 1);

	// ---- the classification ----
	resetDeclaration();
	settings.network.online = false;
	claim("an unclassified capability needs FULL, not observer",
			needs("something.nobody.classified") == Tier::Full);
	claim("reads and writes of memory are different tiers",
			needs("memory.read") == Tier::Observer && needs("memory.write") == Tier::Mutator);
	claim("off allows nothing at all", [&]{
		cfgSetVirtual("dojo", "LuaTier", "off");
		const bool none = !can("ui.text") && !can("memory.read");
		cfgSetVirtual("dojo", "LuaTier", "full");
		return none;
	}());

	settings.network.online = wasOnline;
	cfgSetVirtual("dojo", "LuaTier", wasCfg);
	resetDeclaration();
	NOTICE_LOG(COMMON, "LUATIER SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace luatier
