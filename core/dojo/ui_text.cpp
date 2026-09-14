#include "ui_text.h"
#include "cfg/cfg.h"
#include "stdclass.h"
#include "log/LogManager.h"
#include "deps/json/json.hpp"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>

/*
	See ui_text.h for what this is and why it keeps his function names. This
	file is the key, the override store, the runtime registry and the wrappers.
*/
namespace uitext {

static std::map<std::string, std::string> g_over;	//!< key -> replacement
static std::map<std::string, std::string> g_seen;	//!< key -> the original text
static bool g_loaded = false;
static std::mutex g_mutex;

std::string key(const char *s)
{
	if (s == nullptr)
		s = "";		// TableSetupColumn(nullptr, ...) and friends
	u64 h = 0xcbf29ce484222325ULL;
	for (const unsigned char *p = (const unsigned char *)s; *p != 0; )
	{
		/*
			Skip an ICON glyph. 3-byte UTF-8 is lead 0xE0-0xEF plus two
			0x80-0xBF continuations; the Private Use Area is U+E000..U+F8FF.
			Any OTHER 3-byte character is hashed byte-wise, which is what keeps
			this identical to the Python side for real text.
		*/
		if (p[0] >= 0xE0 && p[0] <= 0xEF && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
		{
			const unsigned cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
			if (cp >= 0xE000u && cp <= 0xF8FFu)
			{
				p += 3;
				continue;
			}
			h ^= p[0]; h *= 0x100000001b3ULL;
			h ^= p[1]; h *= 0x100000001b3ULL;
			h ^= p[2]; h *= 0x100000001b3ULL;
			p += 3;
			continue;
		}
		h ^= *p;
		h *= 0x100000001b3ULL;
		++p;
	}
	static const char *hx = "0123456789abcdef";
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i)
	{
		out[i] = hx[h & 0xF];
		h >>= 4;
	}
	return out;
}

std::string fmtSignature(const char *s)
{
	std::string sig;
	if (s == nullptr)
		return sig;
	for (const char *p = s; *p != 0; ++p)
	{
		if (*p != '%')
			continue;
		++p;
		/*
			`[CORRECTED 2026-09-14]` THE FORK READS PAST THE TERMINATOR HERE, and
			this port had the bug until a self-test claim caught it. His version:

				if (*p == '%' || !*p) continue;

			On a string ending in a lone '%', the ++p above lands on the NUL, the
			guard sees !*p - and `continue` IN A FOR LOOP RUNS THE INCREMENT, so
			p steps PAST the terminator and the next read is out of bounds.

			It is reachable from any text a user types into the UI Text editor
			ending in '%'. `break` is the fix; only the "%%" case may continue,
			and by then *p is known non-zero.
		*/
		if (*p == 0)
			break;					// a trailing lone '%' - the string is over
		if (*p == '%')
			continue;				// "%%" is a literal percent, not a conversion
		while (*p != 0 && strchr("-+ #0123456789.*lhLjztq", *p) != nullptr)
			++p;					// flags, width, precision, length modifiers
		if (*p == 0)
			break;
		sig += *p;
	}
	return sig;
}

bool translatable(const char *s)
{
	if (s == nullptr)
		return false;
	for (const char *p = s; *p != 0; ++p)
	{
		if (*p == '%')
		{
			++p;
			if (*p == 0)
				break;			// trailing lone '%' - see fmtSignature
			if (*p == '%')
				continue;		// a literal percent is ordinary text
			while (*p != 0 && strchr("-+ #0123456789.*lhLjztq", *p) != nullptr)
				++p;
			if (*p == 0)
				break;
			continue;			// skip the conversion character itself
		}
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
			return true;
	}
	return false;
}

static std::string overridesPath()
{
	return get_writable_data_path("ui_text_overrides.json");
}

void load()
{
	std::lock_guard<std::mutex> lk(g_mutex);
	g_over.clear();
	std::ifstream in(overridesPath());
	if (in.good())
	{
		try {
			nlohmann::json j;
			in >> j;
			if (j.is_object())
				for (auto& kv : j.items())
					if (kv.value().is_string())
						g_over[kv.key()] = kv.value().get<std::string>();
		} catch (...) {
			// A corrupt overrides file must not stop the emulator drawing its
			// UI. The labels simply come out as authored.
			WARN_LOG(RENDERER, "UI TEXT: %s is not readable JSON - no overrides",
					overridesPath().c_str());
		}
	}
	g_loaded = true;
}

void save()
{
	std::lock_guard<std::mutex> lk(g_mutex);
	nlohmann::json j = nlohmann::json::object();
	for (const auto& kv : g_over)
		j[kv.first] = kv.second;
	std::ofstream out(overridesPath(), std::ios::binary | std::ios::trunc);
	if (out.good())
		out << j.dump(2);
}

void set(const std::string& k, const std::string& replacement)
{
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		if (replacement.empty())
			g_over.erase(k);
		else
			g_over[k] = replacement;
	}
	save();
}

const char *resolve(const char *original)
{
	if (original == nullptr || !translatable(original))
		return nullptr;
	if (!g_loaded)
		load();
	const std::string k = key(original);
	std::lock_guard<std::mutex> lk(g_mutex);
	// THE REGISTRY, populated here rather than by a source scan - see the
	// header. Recording on every resolve means the list is exactly "what this
	// UI has drawn", with no second owner to drift from.
	if (g_seen.find(k) == g_seen.end())
		g_seen[k] = original;
	const auto it = g_over.find(k);
	return it != g_over.end() ? it->second.c_str() : nullptr;
}

void seen(std::vector<Seen>& out)
{
	std::lock_guard<std::mutex> lk(g_mutex);
	out.clear();
	out.reserve(g_seen.size());
	for (const auto& kv : g_seen)
	{
		Seen s;
		s.key = kv.first;
		s.original = kv.second;
		const auto ov = g_over.find(kv.first);
		if (ov != g_over.end())
		{
			s.overridden = true;
			s.replacement = ov->second;
		}
		out.push_back(s);
	}
}

size_t seenCount()
{
	std::lock_guard<std::mutex> lk(g_mutex);
	return g_seen.size();
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;

	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "UITEXT SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};

	// ---- the key ---------------------------------------------------------------------
	/*
		AGAINST KNOWN FNV-1a VALUES, not against itself. A key function tested
		only for self-consistency ("the same string gives the same key") passes
		with any hash at all, including a broken one - and the whole point of
		this key is that it matches a number computed by a DIFFERENT
		implementation in a different language.
	*/
	claim("the empty string is the FNV-1a offset basis",
			key("") == "cbf29ce484222325");
	claim("'a' matches the reference FNV-1a 64 value",
			key("a") == "af63dc4c8601ec8c");
	claim("a real label matches the reference value",
			key("Generate") == "45cb501abd43ba52" && key("combo row") == "b414040845ce2679");
	claim("a format string matches the reference value",
			key("Frame %u / %u") == "524b01fb63f5abe9");
	claim("nullptr keys as the empty string, not as a crash",
			key(nullptr) == key(""));
	claim("different strings key differently", key("Save") != key("Load"));

	// The icon rule: a Private-Use-Area glyph is skipped, so an icon-prefixed
	// label matches the icon-free key a source scan would produce.
	claim("an ICON glyph is skipped, so icon+text keys as text",
			key("\xEE\x80\x80Generate") == key("Generate"));
	// THE CONTROL. A key() that skipped ALL 3-byte characters, or ignored
	// non-ASCII entirely, would satisfy the claim above.
	claim("...but a NON-icon 3-byte character still counts",
			key("\xE2\x9C\x93Generate") != key("Generate"));

	// ---- the format signature ----------------------------------------------------------
	claim("conversions come back in order", fmtSignature("%d x %s") == "ds");
	claim("a literal %% is not a conversion", fmtSignature("100%% done") == "");
	claim("width, flags and length modifiers are skipped",
			fmtSignature("%-8.3f %llu %zu") == "fuu");
	claim("no conversions is an empty signature", fmtSignature("plain text") == "");
	claim("a trailing lone %% does not run off the end", fmtSignature("oops %") == "");
	// THE PROPERTY THE WHOLE LAYER RESTS ON.
	claim("an override that keeps the specifiers matches",
			fmtSignature("Frame %u / %u") == fmtSignature("Bild %u von %u"));
	claim("...and one that changes them does NOT",
			fmtSignature("Frame %u / %u") != fmtSignature("Frame %s"));

	// ---- what counts as translatable ------------------------------------------------------
	claim("a plain label is translatable", translatable("Generate"));
	claim("a label with values in it is translatable", translatable("Frame %u / %u"));
	claim("a bare conversion is NOT", !translatable("%s") && !translatable("%u"));
	claim("...nor is a string of only conversions and punctuation",
			!translatable("%u / %u") && !translatable("%d-%d"));
	claim("a literal %% counts as ordinary text, not a conversion",
			translatable("100%% done"));
	claim("an empty or null string is not translatable",
			!translatable("") && !translatable(nullptr));
	claim("a bare conversion is not recorded as seen",
			[&]{ const size_t b = seenCount(); resolve("%s"); return seenCount() == b; }());

	// ---- resolve and the registry --------------------------------------------------------
	{
		const size_t before = seenCount();
		const char *r = resolve("a string nothing has overridden");
		claim("an unknown string resolves to nullptr", r == nullptr);
		claim("...and is still recorded as seen", seenCount() == before + 1);
		claim("resolving the same string twice records it once",
				(resolve("a string nothing has overridden"), seenCount() == before + 1));

		const std::string k = key("##uitext selftest subject");
		set(k, "REPLACED");
		const char *got = resolve("##uitext selftest subject");
		claim("an override resolves", got != nullptr && std::string(got) == "REPLACED");
		set(k, "");
		claim("clearing an override restores the original",
				resolve("##uitext selftest subject") == nullptr);
	}

	NOTICE_LOG(RENDERER, "UITEXT SELFTEST: %d passed, %d failed (%u strings seen so far)",
			pass, fail, (unsigned)seenCount());
}

}	// namespace uitext

// ---------------------------------------------------------------------------------------
// The wrappers.
//
// Each is "resolve, then do what ImGui would have done". The only one with real logic is
// the tooltip/format family, where the override has to be checked for specifier
// compatibility before it can be handed to printf.
// ---------------------------------------------------------------------------------------

//! Shared body: render `fmt` (or its override) through a printf-style sink.
static void tasFormatted(const char *fmt, va_list args,
		void (*sinkV)(const char *, va_list), void (*sinkS)(const char *))
{
	const char *ov = uitext::resolve(fmt);
	if (ov != nullptr && uitext::fmtSignature(ov) == uitext::fmtSignature(fmt))
		sinkV(ov, args);
	else if (ov != nullptr)
		// THE GUARD. The override changed the %-codes, so it must NOT meet
		// printf with the original's arguments. Rendered literally instead.
		sinkS(ov);
	else
		sinkV(fmt, args);
}

static void sinkTextV(const char *f, va_list a) { ImGui::TextV(f, a); }
static void sinkTextS(const char *s) { ImGui::TextUnformatted(s); }

void tasText(const char *fmt, ...)
{
	va_list args; va_start(args, fmt);
	tasFormatted(fmt, args, sinkTextV, sinkTextS);
	va_end(args);
}

void tasTextWrapped(const char *fmt, ...)
{
	va_list args; va_start(args, fmt);
	const char *ov = uitext::resolve(fmt);
	if (ov != nullptr && uitext::fmtSignature(ov) == uitext::fmtSignature(fmt))
		ImGui::TextWrappedV(ov, args);
	else if (ov != nullptr)
		ImGui::TextWrapped("%s", ov);
	else
		ImGui::TextWrappedV(fmt, args);
	va_end(args);
}

void tasTextColored(const ImVec4& col, const char *fmt, ...)
{
	va_list args; va_start(args, fmt);
	const char *ov = uitext::resolve(fmt);
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	if (ov != nullptr && uitext::fmtSignature(ov) == uitext::fmtSignature(fmt))
		ImGui::TextV(ov, args);
	else if (ov != nullptr)
		ImGui::TextUnformatted(ov);
	else
		ImGui::TextV(fmt, args);
	ImGui::PopStyleColor();
	va_end(args);
}

void tasTextDisabled(const char *fmt, ...)
{
	va_list args; va_start(args, fmt);
	const char *ov = uitext::resolve(fmt);
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
	if (ov != nullptr && uitext::fmtSignature(ov) == uitext::fmtSignature(fmt))
		ImGui::TextV(ov, args);
	else if (ov != nullptr)
		ImGui::TextUnformatted(ov);
	else
		ImGui::TextV(fmt, args);
	ImGui::PopStyleColor();
	va_end(args);
}

void tasTextUnformatted(const char *text, const char *textEnd)
{
	const char *ov = uitext::resolve(text);
	if (ov != nullptr)
		ImGui::TextUnformatted(ov);
	else
		ImGui::TextUnformatted(text, textEnd);
}

static void sinkTipV(const char *f, va_list a) { ImGui::SetTooltipV(f, a); }
static void sinkTipS(const char *s) { ImGui::SetTooltip("%s", s); }

void tasTip(const char *fmt, ...)
{
	va_list args; va_start(args, fmt);
	tasFormatted(fmt, args, sinkTipV, sinkTipS);
	va_end(args);
}

void tasTipItem(const char *fmt, ...)
{
	if (!ImGui::IsItemHovered())
		return;
	va_list args; va_start(args, fmt);
	tasFormatted(fmt, args, sinkTipV, sinkTipS);
	va_end(args);
}

/*
	LABELS CARRY AN ID, AND THE ID MUST NOT MOVE.

	ImGui derives a widget's identity from its label, including everything after
	"##". Overriding a label therefore changes the widget's ID - which loses its
	state (an open combo closes, a drag resets) and, worse, can COLLIDE with
	another widget silently.

	So an override replaces only the VISIBLE part and the original's "##" suffix
	is re-attached. A label that is nothing but an id ("##foo") has no visible
	text and is left entirely alone.
*/
static std::string tasLabel(const char *label, const char *ov)
{
	const char *hash = strstr(label, "##");
	if (hash == nullptr)
		return ov;
	return std::string(ov) + hash;
}

//! Resolve a widget label, keeping its ##id. Returns label unchanged if no override.
static const char *tasResolveLabel(const char *label, std::string& scratch)
{
	if (label == nullptr)
		return label;
	if (label[0] == '#' && label[1] == '#')
		return label;		// pure id, no visible text to override
	const char *ov = uitext::resolve(label);
	if (ov == nullptr)
		return label;
	scratch = tasLabel(label, ov);
	return scratch.c_str();
}

bool tasButton(const char *label, const ImVec2& size)
{
	std::string s;
	return ImGui::Button(tasResolveLabel(label, s), size);
}

bool tasSmallButton(const char *label)
{
	std::string s;
	return ImGui::SmallButton(tasResolveLabel(label, s));
}

bool tasCheckbox(const char *label, bool *v)
{
	std::string s;
	return ImGui::Checkbox(tasResolveLabel(label, s), v);
}

bool tasSelectable(const char *label, bool selected, ImGuiSelectableFlags flags,
		const ImVec2& size)
{
	std::string s;
	return ImGui::Selectable(tasResolveLabel(label, s), selected, flags, size);
}

bool tasMenuItem(const char *label, const char *shortcut, bool selected, bool enabled)
{
	std::string s;
	return ImGui::MenuItem(tasResolveLabel(label, s), shortcut, selected, enabled);
}

bool tasRadioButton(const char *label, bool active)
{
	std::string s;
	return ImGui::RadioButton(tasResolveLabel(label, s), active);
}

bool tasInputTextWithHint(const char *label, const char *hint, char *buf, size_t bufSize,
		ImGuiInputTextFlags flags)
{
	std::string ls, hs;
	const char *h = hint;
	if (hint != nullptr)
	{
		const char *ov = uitext::resolve(hint);
		if (ov != nullptr) { hs = ov; h = hs.c_str(); }
	}
	return ImGui::InputTextWithHint(tasResolveLabel(label, ls), h, buf, bufSize, flags);
}

void tasTableSetupColumn(const char *label, ImGuiTableColumnFlags flags, float initW,
		ImGuiID userId)
{
	std::string s;
	ImGui::TableSetupColumn(tasResolveLabel(label, s), flags, initW, userId);
}
