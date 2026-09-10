#include "roll_meta.h"
#include "dojo.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <vector>

namespace roll
{

namespace {

struct Provider
{
	std::string key;
	std::function<std::string()> capture;
	std::function<void(const std::string&)> apply;
};

std::vector<Provider>& providers()
{
	static std::vector<Provider> v;
	return v;
}

// SEPARATORS THAT CANNOT APPEAR IN A PAYLOAD. The anchor provider's own format
// is "slot=frame,slot=frame", so a comma or an equals would collide; ASCII unit
// and record separators exist for exactly this and no sane payload contains
// them. A payload that somehow did would split wrongly here rather than
// corrupting another provider's, because each record is keyed.
const char REC = '\x1e';	// between providers
const char UNIT = '\x1f';	// between a key and its payload

}	// namespace

void metaRegister(const char *key, std::function<std::string()> capture,
		std::function<void(const std::string&)> apply)
{
	for (auto& p : providers())
		if (p.key == key)
		{
			p.capture = std::move(capture);
			p.apply = std::move(apply);
			return;
		}
	providers().push_back(Provider{ key, std::move(capture), std::move(apply) });
}

std::string metaCapture()
{
	std::string out;
	for (const auto& p : providers())
	{
		if (!p.capture)
			continue;
		const std::string payload = p.capture();
		// A PROVIDER WITH NOTHING TO SAY WRITES NOTHING, so an edit made with no
		// bookmarks and no anchors produces an empty blob - which dojo skips
		// entirely on undo rather than calling every provider with "".
		if (payload.empty())
			continue;
		if (!out.empty())
			out += REC;
		out += p.key;
		out += UNIT;
		out += payload;
	}
	return out;
}

void metaApply(const std::string& blob)
{
	if (blob.empty())
		return;
	size_t pos = 0;
	while (pos <= blob.size())
	{
		const size_t rec = blob.find(REC, pos);
		const std::string one = blob.substr(pos, rec == std::string::npos
				? std::string::npos : rec - pos);
		pos = rec == std::string::npos ? blob.size() + 1 : rec + 1;
		const size_t unit = one.find(UNIT);
		if (unit == std::string::npos)
			continue;
		const std::string key = one.substr(0, unit);
		const std::string payload = one.substr(unit + 1);
		for (const auto& p : providers())
			if (p.key == key && p.apply)
			{
				p.apply(payload);
				break;
			}
		// An unknown key falls through deliberately: a clip edited by a build
		// with a provider this one lacks must not be a parse error.
	}
}

void metaInstall()
{
	dojo.edit_meta_capture = []() { return metaCapture(); };
	dojo.edit_meta_apply   = [](const std::string& blob) { metaApply(blob); };
}

/*
	SELF-TEST. The claims that matter are about the SECOND provider, because one
	provider works with no registry at all - which is how the single-setter
	version looked correct right up until bookmarks arrived.
*/
void metaSelfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ROLLMETA SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// The registry is global and live, so the test installs its own providers
	// under test keys and leaves the real ones alone.
	static std::string a, b;
	metaRegister("t.a", []() { return a; }, [](const std::string& s) { a = s; });
	metaRegister("t.b", []() { return b; }, [](const std::string& s) { b = s; });

	a = "slot=1,frame=2"; b = "10:start;40:combo";
	const std::string blob = metaCapture();
	claim("a blob carries both providers", blob.find("t.a") != std::string::npos
			&& blob.find("t.b") != std::string::npos);
	// THE CLAIM THE SINGLE SETTER COULD NOT MAKE.
	a = "wrong"; b = "wrong";
	metaApply(blob);
	claim("both providers get their OWN payload back",
			a == "slot=1,frame=2" && b == "10:start;40:combo");
	claim("...and a payload containing = and , survives intact",
			a.find('=') != std::string::npos && a.find(',') != std::string::npos);

	a = ""; b = "kept";
	const std::string sparse = metaCapture();
	claim("a provider with nothing to say writes nothing",
			sparse.find("t.a") == std::string::npos && sparse.find("t.b") != std::string::npos);
	a = "clobber";
	metaApply(sparse);
	claim("...and is not called with an empty payload on the way back",
			a == "clobber" && b == "kept");

	a = "x";
	metaApply(std::string("t.zzz") + '\x1f' + "from a newer build");
	claim("an unknown provider is skipped rather than misparsed", a == "x");

	metaApply("");
	claim("an empty blob is a no-op", a == "x" && b == "kept");

	// Re-registering must REPLACE, or metaInstall() called twice would double
	// every payload.
	const size_t before = metaCapture().size();
	metaRegister("t.a", []() { return a; }, [](const std::string& s) { a = s; });
	claim("registering a key twice replaces rather than appends",
			metaCapture().size() == before);

	a.clear(); b.clear();
	NOTICE_LOG(RENDERER, "ROLLMETA SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace roll
