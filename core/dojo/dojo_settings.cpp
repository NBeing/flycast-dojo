#include "dojo_settings.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <cstdlib>
#include <cstring>

namespace dojocfg
{

namespace {

enum class Type { Bool, Int, Str };

struct Row
{
	const char *name;
	Type type;
	const char *defaultText;
	bool launchable;
};

const Row TABLE[] = {
#define DOJO_KEY_ROW(n, t, d, l) { #n, Type::t, d, l },
	DOJO_KEYS(DOJO_KEY_ROW)
#undef DOJO_KEY_ROW
};

static_assert(sizeof(TABLE) / sizeof(TABLE[0]) == (size_t)Key::Count,
		"the table and the enum are generated from one macro and must not diverge");

const Row& row(Key k) { return TABLE[(size_t)k]; }

//! The store's own spelling of a boolean. cfg writes "yes"/"no"; accepting
//! "true"/"1" as well costs nothing and stops a hand-edited emu.cfg reading as
//! false without saying why.
bool textIsTrue(const char *s)
{
	return s != nullptr && (!std::strcmp(s, "yes") || !std::strcmp(s, "true")
			|| !std::strcmp(s, "1") || !std::strcmp(s, "on"));
}

}	// namespace

const char *name(Key k) { return row(k).name; }

bool getBool(Key k)
{
	const Row& r = row(k);
	return cfgLoadBool("dojo", r.name, textIsTrue(r.defaultText));
}

int getInt(Key k)
{
	const Row& r = row(k);
	return cfgLoadInt("dojo", r.name, std::atoi(r.defaultText));
}

std::string getStr(Key k)
{
	const Row& r = row(k);
	return cfgLoadStr("dojo", r.name, r.defaultText);
}

/*
	THE WRITER. Persist always; shadow only when the table says the key is one a
	launch flag may set.

	ORDER MATTERS: persist first, then shadow. The shadow wins every later read,
	so writing it first and persisting second would be indistinguishable - but
	the reverse order makes the intent legible, and if the persist ever throws
	the shadow has not yet claimed a value that will never reach disk.
*/
template <typename T>
static void writeThrough(Key k, const T& v, const std::string& text)
{
	const Row& r = row(k);
	if (r.type == Type::Bool)      cfgSaveBool("dojo", r.name, textIsTrue(text.c_str()));
	else if (r.type == Type::Int)  cfgSaveInt ("dojo", r.name, std::atoi(text.c_str()));
	else                           cfgSaveStr ("dojo", r.name, text);

	if (r.launchable)
		cfgSetVirtual("dojo", r.name, text);
}

void set(Key k, bool v)                { writeThrough(k, v, std::string(v ? "yes" : "no")); }
void set(Key k, int v)                 { writeThrough(k, v, std::to_string(v)); }
void set(Key k, const std::string& v)  { writeThrough(k, v, v); }

bool isOverridden(Key k)
{
	// A shadow entry answers a read that the persisted entry does not. The
	// store has no "is this virtual" query, so this asks the only way available:
	// does the live read differ from what the regular section holds?
	const Row& r = row(k);
	const std::string live = cfgLoadStr("dojo", r.name, r.defaultText);
	// cfgLoadStr consults the virtual section first, so a launchable key whose
	// live value came from the shadow is exactly the case where set() has run
	// or a -config flag is present. Without a store-level query this is the
	// honest approximation, and it is marked as one rather than claimed exact.
	return r.launchable && live != cfgLoadStr("dojo", r.name, r.defaultText);
}

/*
	SELF-TEST. Three claims, and the third is the whole point.

	IT USES THE REAL cfg STORE, not a mock. The bug being prevented lives in the
	store's virtual-section precedence, and a mock would model that correctly by
	definition - proving the mock.
*/
void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "DOJOCFG SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// 1. THE DEFAULT COMES FROM THE TABLE, not from the call site.
	claim("an unset key reads its table default",
			getStr(Key::CineFormQuality) == "film1");
	claim("...and a bool default too", getBool(Key::CaptureLog) == true);

	// 2. set() PERSISTS. A non-launchable key must reach the regular section
	//    and must NOT be shadowed.
	const std::string wasNotes = getStr(Key::PendingNotes);
	set(Key::PendingNotes, std::string("seamtest"));
	claim("set() on a non-launchable key persists",
			cfgLoadStr("dojo", "PendingNotes", "") == "seamtest");

	// 3. set() BEATS A LAUNCH FLAG. This is the claim that matters, and it
	//    FAILS against every raw cfgSaveBool in the tree today - which is how
	//    you know it can fail at all.
	const bool wasProbe = getBool(Key::MemHunt);
	cfgSetVirtual("dojo", "MemHunt", "yes");	// simulate -config dojo:MemHunt=yes
	claim("the simulated launch flag took", getBool(Key::MemHunt) == true);
	set(Key::MemHunt, false);
	claim("set() on a LAUNCHABLE key beats the launch flag", getBool(Key::MemHunt) == false);

	// 4. AND THE OTHER DIRECTION. A non-launchable key must not be shadowed,
	//    or it becomes unpersistable for the life of the process. A set() that
	//    ALWAYS writes the shadow satisfies claim 3 and fails this.
	cfgSaveStr("dojo", "PendingTags", "on-disk");
	set(Key::PendingTags, std::string("via-set"));
	cfgSaveStr("dojo", "PendingTags", "on-disk-again");
	claim("a non-launchable key stays persistable after set()",
			cfgLoadStr("dojo", "PendingTags", "") == "on-disk-again");

	set(Key::PendingNotes, wasNotes);
	set(Key::MemHunt, wasProbe);
	NOTICE_LOG(RENDERER, "DOJOCFG SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace dojocfg
