#include "tasva2.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace tas_va2
{
u32 Result::totalFrames() const
{
	u32 n = 0;
	for (const Step& s : steps)
		n += (u32)s.frames.size();
	return n;
}

std::vector<u16> Expand(const Result& r)
{
	std::vector<u16> out;
	out.reserve(r.totalFrames());
	for (const Step& s : r.steps)
		out.insert(out.end(), s.frames.begin(), s.frames.end());
	return out;
}

bool LooksLike(const std::string& text)
{
	bool prevLetter = false, prevDigit = false, inComment = false;
	for (char c : text)
	{
		if (c == '\n' || c == '\r')
		{
			inComment = false;
			prevLetter = prevDigit = false;
			continue;
		}
		if (inComment)
			continue;
		if (c == '#')
		{
			inComment = true;
			continue;
		}
		if (strchr("/[]*-_", c))
			return true;
		const bool L = isalpha((unsigned char)c) != 0, D = isdigit((unsigned char)c) != 0;
		if ((L && prevDigit) || (D && prevLetter))
			return true;
		prevLetter = L;
		prevDigit = D;
	}
	return false;
}

namespace
{
// numpad digit -> canon direction bits (5 = neutral). Same table as the notepad's tasParseMash.
const u16 NP[10] = { 0, DOWN | LEFT, DOWN, DOWN | RIGHT, LEFT, 0, RIGHT, UP | LEFT, UP, UP | RIGHT };

enum Kind { K_WORD, K_AMBIG, K_NAME };
struct Tok { const char *tok; Kind kind; u16 bits; };

// The fixed vocabulary. Matched LONGEST-FIRST at the scan position (the table is sorted once), so "start" beats "st",
// "assist1" beats "a1", "sentinel" beats "sent", and concatenated forms ("lphk", "a1hk", "6pp18") fall out naturally.
const Tok VOCAB[] = {
	{ "assist1", K_WORD, A1 }, { "assist2", K_WORD, A2 },
	{ "start", K_WORD, START }, { "taunt", K_WORD, START }, { "thc", K_WORD, A1 | A2 }, { "st", K_WORD, START },
	{ "pp", K_WORD, LP | HP }, { "kk", K_WORD, LK | HK },
	{ "lp", K_WORD, LP }, { "hp", K_WORD, HP }, { "lk", K_WORD, LK }, { "hk", K_WORD, HK },
	{ "mp", K_WORD, LP }, { "mk", K_WORD, LK },
	{ "a1", K_WORD, A1 }, { "a2", K_WORD, A2 },
	// slot-vs-type ambiguous spellings (spec F2: never guess) - the import policy resolves them
	{ "assist-a", K_AMBIG, 0 }, { "assist-b", K_AMBIG, 0 }, { "assista", K_AMBIG, 0 }, { "assistb", K_AMBIG, 0 },
	{ "assist", K_AMBIG, 0 },
	// MvC2 roster + the short forms the corpus uses (spec F1/F12: a bare name or Name-A/B/Y is an assist call whose
	// slot is unknowable from the text). Multi-word names carry '~' - the pre-pass joins them before whitespace splits.
	{ "captain~commando", K_NAME, 0 }, { "captain~america", K_NAME, 0 }, { "silver~samurai", K_NAME, 0 },
	{ "bone~wolverine", K_NAME, 0 }, { "ruby~heart", K_NAME, 0 }, { "omega~red", K_NAME, 0 }, { "iron~man", K_NAME, 0 },
	{ "war~machine", K_NAME, 0 }, { "tron~bonne", K_NAME, 0 }, { "m.~bison", K_NAME, 0 }, { "b.b.~hood", K_NAME, 0 },
	{ "dr.~doom", K_NAME, 0 },
	{ "shuma-gorath", K_NAME, 0 }, { "spider-man", K_NAME, 0 }, { "chun-li", K_NAME, 0 },
	{ "anakaris", K_NAME, 0 }, { "blackheart", K_NAME, 0 }, { "juggernaut", K_NAME, 0 }, { "wolverine", K_NAME, 0 },
	{ "magneto", K_NAME, 0 }, { "sentinel", K_NAME, 0 }, { "cyclops", K_NAME, 0 }, { "psylocke", K_NAME, 0 },
	{ "colossus", K_NAME, 0 }, { "morrigan", K_NAME, 0 }, { "felicia", K_NAME, 0 }, { "zangief", K_NAME, 0 },
	{ "dhalsim", K_NAME, 0 }, { "charlie", K_NAME, 0 }, { "sakura", K_NAME, 0 }, { "cammy", K_NAME, 0 },
	{ "akuma", K_NAME, 0 }, { "guile", K_NAME, 0 }, { "hayato", K_NAME, 0 }, { "sonson", K_NAME, 0 },
	{ "amingo", K_NAME, 0 }, { "marrow", K_NAME, 0 }, { "cable", K_NAME, 0 }, { "storm", K_NAME, 0 },
	{ "venom", K_NAME, 0 }, { "gambit", K_NAME, 0 }, { "rogue", K_NAME, 0 }, { "spiral", K_NAME, 0 },
	{ "iceman", K_NAME, 0 }, { "thanos", K_NAME, 0 }, { "strider", K_NAME, 0 }, { "megaman", K_NAME, 0 },
	{ "servbot", K_NAME, 0 }, { "jill", K_NAME, 0 }, { "hulk", K_NAME, 0 }, { "roll", K_NAME, 0 },
	{ "doom", K_NAME, 0 }, { "ryu", K_NAME, 0 }, { "ken", K_NAME, 0 }, { "dan", K_NAME, 0 }, { "jin", K_NAME, 0 },
	{ "commando", K_NAME, 0 }, { "capcom", K_NAME, 0 }, { "capam", K_NAME, 0 }, { "ruby", K_NAME, 0 },
	{ "sent", K_NAME, 0 }, { "mags", K_NAME, 0 }, { "mag", K_NAME, 0 }, { "psy", K_NAME, 0 }, { "cyke", K_NAME, 0 },
	{ "wolvie", K_NAME, 0 }, { "jugg", K_NAME, 0 }, { "ironman", K_NAME, 0 }, { "tron", K_NAME, 0 },
	{ "bison", K_NAME, 0 }, { "hood", K_NAME, 0 }, { "shuma", K_NAME, 0 }, { "spidey", K_NAME, 0 }, { "chun", K_NAME, 0 },
	// David's spellings: "Psyrock" (Psylocke), "gief", and the multi-word names written without the space
	{ "psyrock", K_NAME, 0 }, { "psylock", K_NAME, 0 }, { "gief", K_NAME, 0 }, { "cap", K_NAME, 0 }, { "cyc", K_NAME, 0 },
	{ "juggs", K_NAME, 0 }, { "bh", K_NAME, 0 },
	{ "captaincommando", K_NAME, 0 }, { "captainamerica", K_NAME, 0 }, { "silversamurai", K_NAME, 0 },
	{ "bonewolverine", K_NAME, 0 }, { "rubyheart", K_NAME, 0 }, { "omegared", K_NAME, 0 }, { "warmachine", K_NAME, 0 },
	{ "tronbonne", K_NAME, 0 }, { "mbison", K_NAME, 0 }, { "bbhood", K_NAME, 0 }, { "drdoom", K_NAME, 0 },
};

const std::vector<const Tok*>& vocabSorted()
{
	static std::vector<const Tok*> v;
	if (v.empty())
	{
		for (const Tok& t : VOCAB)
			v.push_back(&t);
		std::stable_sort(v.begin(), v.end(), [](const Tok *a, const Tok *b) { return strlen(a->tok) > strlen(b->tok); });
	}
	return v;
}

std::string display(std::string s)
{
	for (char& c : s)
		if (c == '~')
			c = ' ';
	return s;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool allDigits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), isDigit); }

// reads [0-9]+ at i (saturating - the caps are checked by the caller)
bool readNum(const std::string& s, size_t& i, u32& n)
{
	if (i >= s.size() || !isDigit(s[i]))
		return false;
	n = 0;
	while (i < s.size() && isDigit(s[i]))
	{
		n = n * 10 + (u32)(s[i] - '0');
		if (n > 1000001u)
			n = 1000001u;	// one PAST the cap, so every "over 1,000,000" check fires instead of a silent clamp
		i++;
	}
	return true;
}

const char* slotName(AssistPolicy p) { return p == ASSIST_A1 ? "A1" : "A2"; }

// one vocabulary token at s[i]: bits + length consumed. len == 0 = nothing matched (err set when the reason is known).
bool matchWord(const std::string& s, size_t i, AssistPolicy policy, u16& bits, size_t& len,
		std::vector<std::string>& notices, std::string& err, bool& assumed)
{
	bits = 0;
	len = 0;
	for (const Tok *t : vocabSorted())
	{
		const size_t L = strlen(t->tok);
		if (s.compare(i, L, t->tok) != 0)
			continue;
		if (t->kind == K_WORD)
		{
			if (!strcmp(t->tok, "taunt"))
				notices.push_back("'taunt' read as START x1 (the pad has no taunt button)");
			bits = t->bits;
			len = L;
			return true;
		}
		// an assist call: optional slot letter a/b/y right after a name, with the dash (Magneto-B, Spiral-Y, Gambit-A+hp)
		// or glued (David: ZangiefB11, PsyrockA) - taken only when nothing alphabetic follows it, so "magnetoassist1"
		// still reads as magneto + assist1
		std::string tok = s.substr(i, L);
		size_t used = L;
		if (t->kind == K_NAME)
		{
			size_t j = i + L;
			if (j < s.size() && s[j] == '-')
				j++;
			if (j < s.size() && (s[j] == 'a' || s[j] == 'b' || s[j] == 'y')
					&& (j + 1 == s.size() || !isalpha((unsigned char)s[j + 1])))
			{
				tok += s.substr(i + L, j + 1 - (i + L));
				used = j + 1 - i;
			}
		}
		if (policy == ASSIST_ASK)
		{
			err = "'" + display(tok) + "' is an assist call - which slot (A1/A2) it was depends on the team order at record"
					" time, not on the text. Set the import policy to A1 or A2.";
			return false;
		}
		len = used;
		if (policy == ASSIST_ASSUME_A1)
		{	// the paste path: never drop the combo, never guess silently - A1, flagged on the step's own line
			bits = A1;
			assumed = true;
			notices.push_back("'" + display(tok) + "' -> A1 ASSUMED (the slot is unknowable from the text) - check it");
			return true;
		}
		bits = policy == ASSIST_A1 ? A1 : A2;
		notices.push_back("'" + display(tok) + "' -> " + slotName(policy) + " (import policy)");
		return true;
	}
	if (s[i] == 'n')
		err = "'" + display(s) + "': n (neutral) can't be fused with other inputs - make it its own step";
	else
		err = "unknown input '" + display(s.substr(i)) + "' in '" + display(s) + "'";
	return false;
}

struct Atom
{
	int digits[16];
	int ndig = 0;
	u16 words = 0;
	bool haveDur = false;
	u32 dur = 1;
	size_t stop = 0;	// where tokenizing gave up (== size on success) - the best-guess split point
	bool assumed = false;	// an assist slot was ASSUMED (ASSIST_ASSUME_A1) - flagged on the step's src
};

// one atom: [digits][words][(-|_)N | N]. false = err; a.stop says how far it got.
bool tokenizeAtom(const std::string& s, AssistPolicy policy, Atom& a, std::vector<std::string>& notices, std::string& err)
{
	a = Atom();
	size_t i = 0;
	while (i < s.size() && s[i] >= '1' && s[i] <= '9')
	{
		if (a.ndig >= 16)
		{
			err = "'" + s + "': motion too long";
			return false;
		}
		a.digits[a.ndig++] = s[i] - '0';
		i++;
	}
	if (i < s.size() && s[i] == '0')
	{	// spec rule 4: a number with no button and no '-' is motion digits, never a duration - and 0 is no direction
		a.stop = i;
		err = "'" + s + "': '0' is not a numpad direction (a hold is written 6-29, a button count 6hk29)";
		return false;
	}
	bool anyWord = false;
	while (i < s.size() && s[i] != '-' && s[i] != '_' && !isDigit(s[i]))
	{
		u16 bits = 0;
		size_t len = 0;
		if (!matchWord(s, i, policy, bits, len, notices, err, a.assumed))
		{
			a.stop = i;
			return false;
		}
		a.words |= bits;
		i += len;
		anyWord = true;
	}
	if (i < s.size() && (s[i] == '-' || s[i] == '_'))
	{
		const size_t dash = i++;
		u32 n = 0;
		if (anyWord && i < s.size() && isalpha((unsigned char)s[i]))
		{	// "hk-a1": a dash between two words is a join (the hold syntax needs a number) - best guess + notice
			notices.push_back("'" + display(s) + "': '-' between inputs read as '+' - check it");
			while (i < s.size() && s[i] != '-' && s[i] != '_' && !isDigit(s[i]))
			{
				u16 bits = 0;
				size_t len = 0;
				if (!matchWord(s, i, policy, bits, len, notices, err, a.assumed))
				{
					a.stop = i;
					return false;
				}
				a.words |= bits;
				i += len;
			}
			if (i < s.size() && isDigit(s[i]))
			{
				readNum(s, i, n);
				a.haveDur = true;
				a.dur = n;
			}
		}
		else if (!readNum(s, i, n))
		{
			a.stop = dash;
			err = "'" + display(s) + "': '-' must be followed by a hold count (6-29)";
			return false;
		}
		else
		{
			if (anyWord)	// spec F10: "6hk-60" is invalid (a dash after buttons) - best guess = the plain count
				notices.push_back("'" + display(s) + "': '-' after buttons read as a plain count (x" + std::to_string(n) + ") - check it");
			a.haveDur = true;
			a.dur = n;
		}
	}
	else if (i < s.size() && isDigit(s[i]))
	{
		u32 n = 0;
		readNum(s, i, n);
		a.haveDur = true;
		a.dur = n;
	}
	a.stop = i;
	if (i != s.size())
	{
		err = "unexpected '" + display(s.substr(i)) + "' in '" + display(s) + "'";
		return false;
	}
	if (a.ndig == 0 && !anyWord)
	{
		err = "'" + display(s) + "': no input";
		return false;
	}
	if (a.haveDur && a.dur == 0)
	{
		err = "'" + display(s) + "': zero count";
		return false;
	}
	return true;
}

// fuse the atoms of one step into its frames: at most one motion, at most one direction, one count
bool emitStep(const std::vector<Atom>& atoms, const std::string& src, Result& out, std::string& err)
{
	int motion = -1, dirCount = 0;
	u16 dir = 0, words = 0;
	bool haveDur = false;
	u32 dur = 1;
	for (size_t i = 0; i < atoms.size(); i++)
	{
		const Atom& a = atoms[i];
		if (a.ndig >= 2)
		{
			if (motion >= 0)
			{
				err = "'" + display(src) + "': two motions in one step";
				return false;
			}
			motion = (int)i;
		}
		if (a.ndig > 0)
		{
			dirCount++;
			dir = NP[a.digits[a.ndig - 1]];
		}
		words |= a.words;
		if (a.haveDur)
		{
			if (haveDur && a.dur != dur)
			{
				err = "'" + display(src) + "': two different counts in one step";
				return false;
			}
			haveDur = true;
			dur = a.dur;
		}
	}
	if (dirCount > 1)
	{
		err = "'" + display(src) + "': two directions in one step ('+' fuses buttons onto a direction, not directions)";
		return false;
	}
	Step st;
	st.src = display(src);
	for (const Atom& a : atoms)
		if (a.assumed)
		{
			st.src += " (A1 assumed - check)";
			break;
		}
	if (motion >= 0)
		for (int j = 0; j < atoms[motion].ndig - 1; j++)
			st.frames.push_back(NP[atoms[motion].digits[j]]);	// 1 frame per leading digit, direction only
	st.frames.insert(st.frames.end(), dur, (u16)(dir | words));	// the last digit carries buttons + the hold
	out.steps.push_back(st);
	return true;
}

std::vector<std::string> splitPlus(const std::string& s)
{
	std::vector<std::string> v;
	std::string cur;
	for (char c : s)
	{
		if (c == '+')
		{
			v.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	v.push_back(cur);
	return v;
}

bool parseStep(const std::string& field, AssistPolicy policy, Result& out, std::string& err, int depth);

// X*N (no brackets) = X repeated N times; the src lands on the first copy only
bool parseRepeatAtom(const std::string& field, size_t star, AssistPolicy policy, Result& out, std::string& err, int depth)
{
	if (star == 0 || star + 1 >= field.size() || !allDigits(field.substr(star + 1)))
	{
		err = "'" + display(field) + "': a repeat is written X*N or [X/Y]*N";
		return false;
	}
	size_t i = star + 1;
	u32 n = 0;
	readNum(field, i, n);
	if (n == 0)
	{
		err = "'" + display(field) + "': *0 repeat";
		return false;
	}
	Result sub;
	if (!parseStep(field.substr(0, star), policy, sub, err, depth + 1))
		return false;
	if ((unsigned long long)sub.totalFrames() * n > 1000000ull)
	{	// (review BUG-1) cap BEFORE the copies are made - nested repeats multiply
		err = "'" + display(field) + "': over 1,000,000 frames - a repeat count is off";
		return false;
	}
	for (u32 k = 0; k < n; k++)
		for (size_t j = 0; j < sub.steps.size(); j++)
		{
			Step s = sub.steps[j];
			s.src = (k == 0 && j == 0) ? display(field) : std::string();
			out.steps.push_back(s);
		}
	out.notices.insert(out.notices.end(), sub.notices.begin(), sub.notices.end());
	return true;
}

bool parseStep(const std::string& field, AssistPolicy policy, Result& out, std::string& err, int depth)
{
	if (depth > 8)
	{
		err = "'" + display(field) + "': nested too deep";
		return false;
	}
	// n / nN = neutral for N frames
	if (field[0] == 'n' && (field.size() == 1 || allDigits(field.substr(1))))
	{
		u32 n = 1;
		if (field.size() > 1)
		{
			size_t i = 1;
			readNum(field, i, n);
		}
		if (n == 0)
		{
			err = "'" + field + "': zero count";
			return false;
		}
		Step st;
		st.src = field;
		st.frames.assign(n, 0);
		out.steps.push_back(st);
		return true;
	}
	const size_t star = field.rfind('*');
	if (star != std::string::npos)
		return parseRepeatAtom(field, star, policy, out, err, depth);
	// tag-in <name>NN / tag-<name>NN: the partner-tag chord held NN - LP+LK calls partner 1, HP+HK partner 2, and
	// which one it was is the same unknowable slot as a named assist -> the import policy decides
	if (field.compare(0, 4, "tag-") == 0)
	{
		size_t i = field.size();
		while (i > 4 && isDigit(field[i - 1]))
			i--;
		u32 n = 1;
		if (i < field.size())
		{
			size_t j = i;
			readNum(field, j, n);
		}
		if (n == 0)
		{
			err = "'" + display(field) + "': zero count";
			return false;
		}
		if (policy == ASSIST_ASK)
		{
			err = "'" + display(field) + "' is a partner tag - which partner depends on the team order at record time."
					" Set the import policy to A1 (LP+LK) or A2 (HP+HK).";
			return false;
		}
		Step st;
		st.src = display(field);
		const bool a1 = policy != ASSIST_A2;
		st.frames.assign(n, a1 ? (u16)(LP | LK) : (u16)(HP | HK));
		if (policy == ASSIST_ASSUME_A1)
		{
			st.src += " (A1 assumed - check)";
			out.notices.push_back("'" + display(field) + "' -> LP+LK x" + std::to_string(n)
					+ " with A1 ASSUMED (the partner is unknowable from the text) - check it");
		}
		else
			out.notices.push_back("'" + display(field) + "' -> " + (a1 ? "LP+LK" : "HP+HK") + " x"
					+ std::to_string(n) + " (import policy " + slotName(policy) + ")");
		out.steps.push_back(st);
		return true;
	}
	const std::vector<std::string> atomTxt = splitPlus(field);
	std::vector<Atom> atoms;
	for (const std::string& a : atomTxt)
	{
		if (a.empty())
		{
			err = "'" + display(field) + "': nothing on one side of '+'";
			return false;
		}
		Atom at;
		std::string e1;
		if (tokenizeAtom(a, policy, at, out.notices, e1))
		{
			atoms.push_back(at);
			continue;
		}
		// spec F9: a glued step ("6hp23lp10", "236hkn22", "7n8") - the prefix that DID tokenize is one step and the rest
		// is the next; "kkk" = "kk" (F11). Best guess + a notice the user sees before importing, never a silent repair.
		if (atomTxt.size() == 1 && at.stop > 0 && at.stop < a.size())
		{
			const std::string head = a.substr(0, at.stop), tail = a.substr(at.stop);
			Atom pre;
			std::string e2;
			std::vector<std::string> scratch;
			if (tokenizeAtom(head, policy, pre, scratch, e2))
			{
				Result sub;
				std::string e3;
				if (parseStep(tail, policy, sub, e3, depth + 1))
				{
					out.notices.push_back("'" + display(a) + "' read as '" + display(head) + " / " + display(tail) + "' (missing '/'?) - check it");
					out.notices.insert(out.notices.end(), scratch.begin(), scratch.end());
					if (!emitStep({ pre }, head, out, err))
						return false;
					out.steps.insert(out.steps.end(), sub.steps.begin(), sub.steps.end());
					out.notices.insert(out.notices.end(), sub.notices.begin(), sub.notices.end());
					return true;
				}
				if (tail == "k" || tail == "p")
				{
					out.notices.push_back("'" + display(a) + "' read as '" + display(head) + "' (typo?) - check it");
					out.notices.insert(out.notices.end(), scratch.begin(), scratch.end());
					return emitStep({ pre }, head, out, err);
				}
			}
		}
		err = e1;
		return false;
	}
	return emitStep(atoms, field, out, err);
}

// a '/'-separated sequence (bracket-aware); repeat groups [..]*N recurse
bool parseSequence(const std::string& s, AssistPolicy policy, Result& out, std::string& err, int depth)
{
	std::vector<std::string> fields;
	std::string cur;
	int d = 0;
	for (char c : s)
	{
		if (c == '[')
			d++;
		else if (c == ']')
			d--;
		if (d < 0)
		{
			err = "']' without '['";
			return false;
		}
		if (c == '/' && d == 0)
		{
			fields.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	fields.push_back(cur);
	if (d != 0)
	{
		err = "'[' without ']'";
		return false;
	}
	int empties = 0;
	for (const std::string& f : fields)
	{
		if (f.empty())
		{	// "//" or a boundary '/': the dialect writes its neutrals as 'n' (265x in the corpus), so an empty step is a
			// slip, not a 1-frame neutral - collapse it and say so (the Program Lab does the same)
			empties++;
			continue;
		}
		if (f[0] == '[')
		{
			out.errSrc = display(f);	// any failure in here is about this group text (cleared on success below)
			size_t close = std::string::npos;
			int dd = 0;
			for (size_t i = 0; i < f.size(); i++)
			{
				if (f[i] == '[')
					dd++;
				else if (f[i] == ']' && --dd == 0)
				{
					close = i;
					break;
				}
			}
			if (close == std::string::npos || close + 1 >= f.size() || f[close + 1] != '*' || !allDigits(f.substr(close + 2)))
			{
				err = "'" + display(f) + "': a repeat group is written [X/Y]*N";
				return false;
			}
			size_t i = close + 2;
			u32 n = 0;
			readNum(f, i, n);
			if (n == 0)
			{
				err = "'" + display(f) + "': *0 repeat";
				return false;
			}
			Result sub;
			if (!parseSequence(f.substr(1, close - 1), policy, sub, err, depth + 1))
				return false;
			if (sub.steps.empty())
			{
				err = "'" + display(f) + "': empty repeat group";
				return false;
			}
			if ((unsigned long long)sub.totalFrames() * n > 1000000ull)
			{	// (review BUG-1) cap BEFORE the copies are made - nested repeats multiply
				err = "'" + display(f) + "': over 1,000,000 frames - a repeat count is off";
				return false;
			}
			for (u32 k = 0; k < n; k++)
				for (size_t j = 0; j < sub.steps.size(); j++)
				{
					Step st = sub.steps[j];
					st.src = (k == 0 && j == 0) ? display(f) : std::string();
					out.steps.push_back(st);
				}
			out.notices.insert(out.notices.end(), sub.notices.begin(), sub.notices.end());
			out.notices.push_back("'" + display(f) + "': repeat group x" + std::to_string(n) + " = "
					+ std::to_string(sub.totalFrames() * n) + " frames");
			out.errSrc.clear();
			continue;
		}
		if (!parseStep(f, policy, out, err, depth))
		{
			out.errSrc = display(f);
			return false;
		}
	}
	if (empties > 0 && depth == 0)
		out.notices.push_back(std::to_string(empties) + " empty step(s) ('//' or a boundary '/') collapsed - the dialect writes a neutral as 'n'");
	return true;
}

// comments out, lowercase, chunk labels out, multi-word names joined, ',' -> '+', whitespace -> '/'
std::string normalize(const std::string& text, std::vector<std::string>& notices)
{
	std::string s;
	{
		std::string line;
		for (size_t i = 0; i <= text.size(); i++)
		{
			const char c = i < text.size() ? text[i] : '\n';
			if (c == '\n' || c == '\r')
			{
				const size_t hash = line.find('#');
				if (hash != std::string::npos)
					line.resize(hash);
				s += line + ' ';
				line.clear();
			}
			else if (c == '"' || c == '\'')
				continue;	// spreadsheet quotes (David: auto-remove tabs and quotes on paste; tabs are whitespace already)
			else
				line += (char)tolower((unsigned char)c);
		}
	}
	// <1-10> chunk labels (spreadsheet row stitching) are not steps
	int labels = 0;
	for (size_t a = s.find('<'); a != std::string::npos; a = s.find('<'))
	{
		const size_t b = s.find('>', a);
		if (b == std::string::npos)
			break;
		s.erase(a, b - a + 1);
		labels++;
	}
	if (labels > 0)
		notices.push_back(std::to_string(labels) + " <..> chunk label(s) dropped");
	// multi-word names: keep their inner space from becoming a step separator
	for (const Tok& t : VOCAB)
	{
		if (t.kind != K_NAME || !strchr(t.tok, '~'))
			continue;
		const std::string spaced = display(t.tok);
		for (size_t p = s.find(spaced); p != std::string::npos; p = s.find(spaced, p + 1))
			s.replace(p, spaced.size(), t.tok);
	}
	for (size_t p = s.find("tag-in "); p != std::string::npos; p = s.find("tag-in ", p + 1))
		s[p + 6] = '~';
	for (char& c : s)
	{
		if (c == ',')
			c = '+';	// the spec keeps the comma a joiner (author-confirmed)
		else if (c == '{')
			c = '[';	// David's spreadsheet writes repeat groups with braces: {lp2hp2/}*19
		else if (c == '}')
			c = ']';
	}
	// whitespace = step separator, except next to a separator / joiner / bracket where it is just air
	std::string o;
	for (size_t i = 0; i < s.size();)
	{
		if (!isspace((unsigned char)s[i]))
		{
			o += s[i++];
			continue;
		}
		size_t j = i;
		while (j < s.size() && isspace((unsigned char)s[j]))
			j++;
		const char prev = o.empty() ? '/' : o.back();
		const char next = j < s.size() ? s[j] : '/';
		if (!strchr("/+*[", prev) && !strchr("/+*]", next))
			o += '/';
		i = j;
	}
	return o;
}
// ---- Fold: frames -> program text --------------------------------------------------------------------------
// a canon direction nibble -> the numpad digit (SOCD pairs cancel: U+D or L+R read as no direction on that axis)
char digitOf(u16 m)
{
	const bool u = (m & UP) && !(m & DOWN), d = (m & DOWN) && !(m & UP);
	const bool l = (m & LEFT) && !(m & RIGHT), r = (m & RIGHT) && !(m & LEFT);
	if (u)
		return l ? '7' : r ? '9' : '8';
	if (d)
		return l ? '1' : r ? '3' : '2';
	return l ? '4' : r ? '6' : '5';
}

// two directions are a motion step apart when their numpad digits are king-adjacent on the 3x3 pad (2->3, 6->2, 1->4;
// NOT 1->9): that is what separates a motion ("236", "623", "41236", the 720) from taps that happen to touch
// ("2/1/9_4" stays three steps, the way David writes it)
bool adjacentDigits(char a, char b)
{
	if (a == b || a < '1' || b < '1' || a > '9' || b > '9')
		return false;
	const int ax = (a - '1') % 3, ay = (a - '1') / 3, bx = (b - '1') % 3, by = (b - '1') / 3;
	return ax - bx <= 1 && bx - ax <= 1 && ay - by <= 1 && by - ay <= 1;
}

// one run (mask x count) as a step - David's spellings: "9_30", "3hp10", "4pp20", "pp3", "n255", "4lp+lk+start37"
std::string stepText(u16 m, u32 n)
{
	if (m == 0)
		return n == 1 ? std::string("n") : "n" + std::to_string(n);
	const u16 dir = m & 0x0F;
	std::string s;
	if (dir != 0)
		s += digitOf(dir);
	std::vector<std::string> words, assists;
	const bool lp = (m & LP) != 0, hp = (m & HP) != 0, lk = (m & LK) != 0, hk = (m & HK) != 0;
	if (lp && hp)
		words.push_back("pp");
	else
	{
		if (lp) words.push_back("lp");
		if (hp) words.push_back("hp");
	}
	if (lk && hk)
		words.push_back("kk");
	else
	{
		if (lk) words.push_back("lk");
		if (hk) words.push_back("hk");
	}
	if (m & START)
		words.push_back("start");
	if ((m & A1) && (m & A2))
		assists.push_back("thc");
	else
	{
		if (m & A1) assists.push_back("a1");
		if (m & A2) assists.push_back("a2");
	}
	if (words.empty() && assists.empty())
	{	// a bare direction: held with '_'
		if (n > 1)
			s += "_" + std::to_string(n);
		return s;
	}
	if (words.empty())
	{	// assists only: the count cannot sit on "a1" ("a111" reads badly) -> *N repeat, the same frames
		if (dir != 0)
			s += "+";
		for (size_t i = 0; i < assists.size(); i++)
			s += (i > 0 ? "+" : "") + assists[i];
		if (n > 1)
			s += "*" + std::to_string(n);
		return s;
	}
	for (size_t i = 0; i < words.size(); i++)
	{
		if (i > 0)
			s += "+";
		s += words[i];
	}
	if (n > 1)
		s += std::to_string(n);	// the count rides the last button word
	for (const std::string& a : assists)
		s += "+" + a;
	return s;
}
}	// namespace

std::string StepText(u16 mask, u32 count)
{
	return stepText(mask, count);
}

std::string Fold(const std::vector<u16>& frames, int stepsPerLine)
{
	struct Run { u16 mask; u32 count; };
	std::vector<Run> runs;
	for (u16 m : frames)
	{
		if (!runs.empty() && runs.back().mask == m)
			runs.back().count++;
		else
			runs.push_back({ m, 1 });
	}
	// a "tap" = a single-frame direction-only run: two or more in a row are a motion's prefix digits
	auto isTap = [&](size_t k) {
		return runs[k].count == 1 && (runs[k].mask & 0x0F) != 0 && (runs[k].mask & 0x7F0) == 0 && digitOf(runs[k].mask) != '5';
	};
	auto directed = [&](size_t k) { return (runs[k].mask & 0x0F) != 0 && digitOf(runs[k].mask) != '5'; };
	auto adjacent = [&](size_t a, size_t b) { return adjacentDigits(digitOf(runs[a].mask), digitOf(runs[b].mask)); };
	std::string out;
	int onLine = 0;
	size_t i = 0;
	while (i < runs.size())
	{
		// a chain of taps, each numpad-adjacent to the last, is a motion's digits; a following DIRECTED step that is
		// adjacent too is its tail ("236hk12", "2369_4"). A motion needs >= 3 digits: 2 taps + tail, or >= 3 bare taps
		// ("236"); "2/3" alone stays two steps.
		size_t j = i;
		while (j < runs.size() && isTap(j) && (j == i || adjacent(j - 1, j)))
			j++;
		const size_t taps = j - i;
		const bool tailOk = taps >= 1 && j < runs.size() && directed(j) && adjacent(j - 1, j);
		std::string s;
		if (taps >= 2 && tailOk)
		{
			for (size_t k = i; k < j; k++)
				s += digitOf(runs[k].mask);
			s += stepText(runs[j].mask, runs[j].count);
			i = j + 1;
		}
		else if (taps >= 3)
		{
			for (size_t k = i; k < j; k++)
				s += digitOf(runs[k].mask);
			i = j;
		}
		else
		{
			s = stepText(runs[i].mask, runs[i].count);
			i++;
		}
		if (!out.empty())
		{
			if (stepsPerLine > 0 && onLine >= stepsPerLine)
			{
				out += '\n';
				onLine = 0;
			}
			else
				out += '/';
		}
		out += s;
		onLine++;
	}
	return out;
}

bool Parse(const std::string& text, AssistPolicy policy, Result& out, std::string& err)
{
	out = Result();
	err.clear();
	const std::string norm = normalize(text, out.notices);
	if (norm.empty())
	{
		err = "nothing to import";
		out.steps.clear();
		return false;
	}
	if (!parseSequence(norm, policy, out, err, 0))
	{
		out.steps.clear();
		return false;
	}
	if (out.steps.empty())
	{
		err = "nothing to import";
		return false;
	}
	const u32 total = out.totalFrames();
	if (total > 1000000u)
	{
		err = "over 1,000,000 frames - a repeat count is off";
		out.steps.clear();
		return false;
	}
	if (total > 65535u)
		out.notices.push_back("over 65,535 frames (" + std::to_string(total) + ") - longer than the pad could hold");
	return true;
}

std::string SelfTest()
{
	struct Case { const char *in; AssistPolicy pol; int frames; u16 first; u16 last; int steps; };	// frames < 0 = must FAIL
	static const Case C[] = {
		{ "n", ASSIST_ASK, 1, 0, 0, 1 }, { "n65", ASSIST_ASK, 65, 0, 0, 1 },
		{ "6", ASSIST_ASK, 1, RIGHT, RIGHT, 1 }, { "5", ASSIST_ASK, 1, 0, 0, 1 },
		{ "6-29", ASSIST_ASK, 29, RIGHT, RIGHT, 1 }, { "8_51", ASSIST_ASK, 51, UP, UP, 1 },
		{ "236", ASSIST_ASK, 3, DOWN, RIGHT, 1 }, { "2369-4", ASSIST_ASK, 7, DOWN, UP | RIGHT, 1 },
		{ "pp", ASSIST_ASK, 1, LP | HP, LP | HP, 1 }, { "hk140", ASSIST_ASK, 140, HK, HK, 1 },
		{ "2hk29", ASSIST_ASK, 29, DOWN | HK, DOWN | HK, 1 }, { "9pp2", ASSIST_ASK, 2, UP | RIGHT | LP | HP, UP | RIGHT | LP | HP, 1 },
		{ "236pp", ASSIST_ASK, 3, DOWN, RIGHT | LP | HP, 1 }, { "236hk12", ASSIST_ASK, 14, DOWN, RIGHT | HK, 1 },
		{ "63214hk96", ASSIST_ASK, 100, RIGHT, LEFT | HK, 1 }, { "12369874kk", ASSIST_ASK, 8, DOWN | LEFT, LEFT | LK | HK, 1 },
		{ "236+assist1", ASSIST_ASK, 3, DOWN, RIGHT | A1, 1 }, { "6a1hk", ASSIST_ASK, 1, RIGHT | A1 | HK, RIGHT | A1 | HK, 1 },
		{ "214a2", ASSIST_ASK, 3, DOWN, LEFT | A2, 1 },
		{ "6hp+hk82", ASSIST_ASK, 82, RIGHT | HP | HK, RIGHT | HP | HK, 1 },
		{ "4+lp+lk+start37", ASSIST_ASK, 37, LEFT | LP | LK | START, LEFT | LP | LK | START, 1 },
		{ "lk+start15", ASSIST_ASK, 15, LK | START, LK | START, 1 }, { "start19", ASSIST_ASK, 19, START, START, 1 },
		{ "[lp/hp]*360", ASSIST_ASK, 720, LP, HP, 720 }, { "lp*3", ASSIST_ASK, 3, LP, LP, 3 },
		{ "[[lp]*2/hp]*2", ASSIST_ASK, 6, LP, HP, 6 },
		{ "thc", ASSIST_ASK, 1, A1 | A2, A1 | A2, 1 }, { "taunt", ASSIST_ASK, 1, START, START, 1 },
		{ "lp/n/hk", ASSIST_ASK, 3, LP, HK, 3 }, { "lp//hk", ASSIST_ASK, 2, LP, HK, 2 }, { "/lp/", ASSIST_ASK, 1, LP, LP, 1 },
		{ "lp hp\nlk", ASSIST_ASK, 3, LP, LK, 3 }, { "# c\nlp # x\n", ASSIST_ASK, 1, LP, LP, 1 },
		{ "LP / HP", ASSIST_ASK, 2, LP, HP, 2 }, { "[lp / hp] * 2", ASSIST_ASK, 4, LP, HP, 4 },
		{ "6hk-60", ASSIST_ASK, 60, RIGHT | HK, RIGHT | HK, 1 },
		{ "6hp23lp10", ASSIST_ASK, 33, RIGHT | HP, LP, 2 }, { "236hkn22", ASSIST_ASK, 25, DOWN, 0, 2 },
		{ "214lpn27", ASSIST_ASK, 30, DOWN, 0, 2 }, { "7n8", ASSIST_ASK, 9, UP | LEFT, 0, 2 }, { "kkk", ASSIST_ASK, 1, LK | HK, LK | HK, 1 },
		{ "4+magneto-b", ASSIST_A2, 1, LEFT | A2, LEFT | A2, 1 }, { "632+spiral-y", ASSIST_A1, 3, RIGHT, DOWN | A1, 1 },
		{ "ruby heart-b", ASSIST_A1, 1, A1, A1, 1 }, { "captain commando-y+hp", ASSIST_A2, 1, A2 | HP, A2 | HP, 1 },
		{ "gambit-a+hp", ASSIST_A1, 1, A1 | HP, A1 | HP, 1 }, { "pp10+cammy-a", ASSIST_A2, 10, LP | HP | A2, LP | HP | A2, 1 },
		{ "tag-in magneto85", ASSIST_A1, 85, LP | LK, LP | LK, 1 }, { "tag-sentinel", ASSIST_A2, 1, HP | HK, HP | HK, 1 },
		{ "6rubypp2", ASSIST_A2, 2, RIGHT | A2 | LP | HP, RIGHT | A2 | LP | HP, 1 },
		{ "assist-a", ASSIST_A1, 1, A1, A1, 1 }, { "strider", ASSIST_A2, 1, A2, A2, 1 },
		{ "<1-10> lp", ASSIST_ASK, 1, LP, LP, 1 }, { "lp,hk", ASSIST_ASK, 1, LP | HK, LP | HK, 1 },
		{ "mp/mk/st", ASSIST_ASK, 3, LP, START, 3 }, { "236/a1+lp+hp", ASSIST_ASK, 4, DOWN, A1 | LP | HP, 2 },
		{ "\"2lp14\"\t\"lk21\"\n", ASSIST_ASK, 35, DOWN | LP, LK, 2 },	// a spreadsheet paste: quotes + tabs
		{ "4+magneto-b", ASSIST_ASSUME_A1, 1, LEFT | A1, LEFT | A1, 1 }, { "tag-in magneto85", ASSIST_ASSUME_A1, 85, LP | LK, LP | LK, 1 },
		{ "hp+ZangiefB11", ASSIST_ASSUME_A1, 11, HP | A1, HP | A1, 1 }, { "hk+PsyrockA", ASSIST_A2, 1, HK | A2, HK | A2, 1 },
		{ "rubyheart-b", ASSIST_A1, 1, A1, A1, 1 }, { "giefy11", ASSIST_A2, 11, A2, A2, 1 }, { "magnetoassist1", ASSIST_A2, 1, A2 | A1, A2 | A1, 1 },
		{ "6magnetob+hk", ASSIST_A1, 1, RIGHT | A1 | HK, RIGHT | A1 | HK, 1 },
		{ "{lp2hp2/}*19", ASSIST_ASK, 76, LP, HP, 38 }, { "hk-a1", ASSIST_ASK, 1, HK | A1, HK | A1, 1 },
		{ "hk-a1/n14", ASSIST_ASK, 15, HK | A1, 0, 2 }, { "4_5/lk10/n/lk15/hk6/hk-a1/n14/236", ASSIST_ASK, 55, LEFT, RIGHT, 8 },
		{ "236lk_40", ASSIST_ASK, 42, DOWN, RIGHT | LK, 1 },
		// must fail
		{ "4+magneto-b", ASSIST_ASK, -1, 0, 0, 0 }, { "tag-in magneto85", ASSIST_ASK, -1, 0, 0, 0 },
		{ "assist-a", ASSIST_ASK, -1, 0, 0, 0 }, { "2360", ASSIST_ASK, -1, 0, 0, 0 },
		{ "lp10+hk5", ASSIST_ASK, -1, 0, 0, 0 }, { "6+2", ASSIST_ASK, -1, 0, 0, 0 }, { "236+214", ASSIST_ASK, -1, 0, 0, 0 },
		{ "n0", ASSIST_ASK, -1, 0, 0, 0 }, { "lp0", ASSIST_ASK, -1, 0, 0, 0 }, { "[lp]*0", ASSIST_ASK, -1, 0, 0, 0 },
		{ "n+hk", ASSIST_ASK, -1, 0, 0, 0 }, { "xyz", ASSIST_ASK, -1, 0, 0, 0 }, { "[lp/hp", ASSIST_ASK, -1, 0, 0, 0 },
		{ "lp]", ASSIST_ASK, -1, 0, 0, 0 }, { "", ASSIST_ASK, -1, 0, 0, 0 }, { "# only\n", ASSIST_ASK, -1, 0, 0, 0 },
		{ "lp+", ASSIST_ASK, -1, 0, 0, 0 }, { "6-", ASSIST_ASK, -1, 0, 0, 0 },
		{ "[[lp]*100000]*100000", ASSIST_ASK, -1, 0, 0, 0 }, { "[lp]*1000001", ASSIST_ASK, -1, 0, 0, 0 },
		{ "n1000*1000", ASSIST_ASK, 1000000, 0, 0, 1000 }, { "n255*4000", ASSIST_ASK, -1, 0, 0, 0 },
	};
	std::string fails;
	for (const Case& c : C)
	{
		Result r;
		std::string err;
		const bool ok = Parse(c.in, c.pol, r, err);
		if (c.frames < 0)
		{
			if (ok)
				fails += "'" + std::string(c.in) + "': expected FAIL, parsed " + std::to_string(r.totalFrames()) + " frames\n";
			continue;
		}
		if (!ok)
		{
			fails += "'" + std::string(c.in) + "': " + err + "\n";
			continue;
		}
		const std::vector<u16> f = Expand(r);
		if ((int)f.size() != c.frames || (int)r.steps.size() != c.steps || f.front() != c.first || f.back() != c.last)
			fails += "'" + std::string(c.in) + "': got " + std::to_string(f.size()) + "f/" + std::to_string(r.steps.size())
					+ "s first=" + std::to_string(f.front()) + " last=" + std::to_string(f.back()) + ", want "
					+ std::to_string(c.frames) + "f/" + std::to_string(c.steps) + "s first=" + std::to_string(c.first)
					+ " last=" + std::to_string(c.last) + "\n";
	}
	// the Program Lab's "Combo I1" preset: 55 steps, 441 frames (hand-counted from the notation)
	{
		const char *i1 =
			"n/2lp14/lk21/2lp14/lk61/2/8_6/2pp/n/hp5\n"
			"6/n/6_20/2_2/6_4/6hk/8_51/6_10/2lk8/8\n"
			"2/9_6/3pp2/3lk9/n/lk13/6/n/6_2/2/\n"
			"9_3/lk17/3pp3/3lk9/n/lk16/2/8_5/lk15/2pp\n"
			"2lk12/2/lk15/2/8_3/4lp17/4/2pp2/1lk12/n\n"
			"6lp23/n2/2lk8/n/2lk10/\n";
		Result r;
		std::string err;
		if (!Parse(i1, ASSIST_ASK, r, err))
			fails += "i1: " + err + "\n";
		else if (r.steps.size() != 55 || r.totalFrames() != 441)
			fails += "i1: got " + std::to_string(r.steps.size()) + " steps / " + std::to_string(r.totalFrames()) + " frames, want 55 / 441\n";
	}
	// David's own program (2026-09-03), written as one '/'-joined line: 84 steps / 1545 frames (hand-counted)
	{
		const char *dv = "n6/7_27/hp+ZangiefB11/n/236hk/6_52/pp3/n/hp27/214lk/6_48/pp3/lk11/hk+PsyrockA/n18/2/1/9_4/4lk/6lk55/"
			"lp16/2/8_5/lp7/214lk/n36/lp13/lk7/214lk/n57/2/8_4/4lp16/4lk7/214lk/n37/236/pp/n/3hp21/lk63/pp11/2/pp20/2/pp20/2/"
			"pp20/2/pp20/2/pp20/2/pp20/2/pp20/2/pp20/2/4pp20/2/4pp10/n255/pp10/9_30/6hk21/pp2/n/hp14/1/9_5/236lk42/3hp10/"
			"9_17/lp19/n/mp18/mk18/214hk15/236pp/n130/236pp/n130/236pp";
		Result r;
		std::string err;
		if (!Parse(dv, ASSIST_ASSUME_A1, r, err))
			fails += "david: " + err + "\n";
		else
		{
			const std::vector<u16> f = Expand(r);
			if (r.steps.size() != 84 || f.size() != 1545 || f.front() != 0 || f.back() != (RIGHT | LP | HP))
				fails += "david: got " + std::to_string(r.steps.size()) + " steps / " + std::to_string(f.size())
						+ " frames (want 84 / 1545), last=" + std::to_string(f.back()) + "\n";
			if (Parse(dv, ASSIST_ASK, r, err))
				fails += "david: ASK must refuse the assist calls\n";
		}
	}
	// the paste sniff: va2 text says yes, CE letters / a lone N / trainer noise / comment text say no
	{
		struct L { const char *in; bool want; };
		static const L LL[] = {
			{ "n65", true }, { "2lp14", true }, { "lp/hk", true }, { "236hk12", true }, { "6-29", true }, { "8_51", true },
			{ "[lp/hp]*3", true }, { "a1", true }, { "\"2lp14\"\t\"lk21\"", true },
			{ "N", false }, { "WZ", false }, { "WZ\n12\n", false }, { "WZ # 2-frame link", false }, { "LP", false },
			{ ".", false }, { "", false }, { "# 236/lp only a comment", false },
		};
		for (const L& l : LL)
			if (LooksLike(l.in) != l.want)
				fails += "LooksLike('" + std::string(l.in) + "') != " + (l.want ? "true" : "false") + "\n";
		// the assumed slot is flagged on the step itself, where the paste lands it
		Result r;
		std::string err;
		if (!Parse("4+magneto-b", ASSIST_ASSUME_A1, r, err) || r.steps.size() != 1 || r.steps[0].src.find("assumed") == std::string::npos)
			fails += "ASSUME_A1: the step src is not flagged\n";
		// the lint squiggle lands on the broken step: errSrc names it
		if (Parse("lp/xyz/hk", ASSIST_ASK, r, err) || r.errSrc != "xyz")
			fails += "errSrc: want 'xyz', got '" + r.errSrc + "'\n";
		if (Parse("lp/[hp/2360]*2", ASSIST_ASK, r, err) || r.errSrc != "[hp/2360]*2")
			fails += "errSrc group: want '[hp/2360]*2', got '" + r.errSrc + "'\n";
	}
	// Fold: frames -> text. (a) the canonical spellings come back verbatim, (b) exact frame round trips on the two
	// real programs (the i1 preset, David's), (c) line breaking
	{
		const char *canon[] = {
			"n", "n3", "2", "6_29", "9_30", "lp", "pp3", "3hp10", "4pp20", "6hp+hk82", "4lp+lk+start37", "start19",
			"236", "214", "623", "41236", "12369874kk", "236hk12", "2369_4", "214lk", "63214hk96", "6+a1*11", "a2", "thc*3",
			"2+a1", "6hk+a1", "6hk21+a2", "2/3", "2/1/9_4", "2/8", "6/3hp10", "8/2/8/2",
			"n6/7_27/hp11+a1/n/236hk/6_52/pp3/n/hp27/214lk/6_48/pp3/lk11/hk+a2/n18/2/1/9_4/4lk/6lk55",
		};
		for (const char *c : canon)
		{
			Result r;
			std::string err;
			if (!Parse(c, ASSIST_ASK, r, err))
			{
				fails += "fold canon '" + std::string(c) + "': " + err + "\n";
				continue;
			}
			const std::string back = Fold(Expand(r));
			if (back != c)
				fails += "fold '" + std::string(c) + "' -> '" + back + "'\n";
		}
		const char *progs[] = {
			"n/2lp14/lk21/2lp14/lk61/2/8_6/2pp/n/hp5\n6/n/6_20/2_2/6_4/6hk/8_51/6_10/2lk8/8\n2/9_6/3pp2/3lk9/n/lk13/6/n/6_2/2/\n"
			"9_3/lk17/3pp3/3lk9/n/lk16/2/8_5/lk15/2pp\n2lk12/2/lk15/2/8_3/4lp17/4/2pp2/1lk12/n\n6lp23/n2/2lk8/n/2lk10/\n",
			"n6/7_27/hp+ZangiefB11/n/236hk/6_52/pp3/n/hp27/214lk/6_48/pp3/lk11/hk+PsyrockA/n18/2/1/9_4/4lk/6lk55/lp16/2/8_5/lp7/"
			"214lk/n36/lp13/lk7/214lk/n57/2/8_4/4lp16/4lk7/214lk/n37/236/pp/n/3hp21/lk63/pp11/2/pp20/2/4pp20/2/4pp10/n255/pp10/"
			"9_30/6hk21/pp2/n/hp14/1/9_5/236lk42/3hp10/9_17/lp19/n/mp18/mk18/214hk15/236pp/n130/236pp/n130/236pp",
		};
		for (const char *p : progs)
		{
			Result r1, r2;
			std::string e1, e2;
			if (!Parse(p, ASSIST_ASSUME_A1, r1, e1))
			{
				fails += "fold prog parse: " + e1 + "\n";
				continue;
			}
			const std::vector<u16> f1 = Expand(r1);
			const std::string folded = Fold(f1, 10);
			if (!Parse(folded, ASSIST_ASK, r2, e2))
			{
				fails += "fold prog reparse: " + e2 + " in '" + folded + "'\n";
				continue;
			}
			if (Expand(r2) != f1)
				fails += "fold prog round trip differs: '" + folded + "'\n";
		}
		Result r;
		std::string err;
		if (Parse("lp/hp/lk", ASSIST_ASK, r, err) && Fold(Expand(r), 2) != "lp/hp\nlk")
			fails += "fold stepsPerLine: '" + Fold(Expand(r), 2) + "'\n";
		if (Fold({}) != "")
			fails += "fold empty\n";
	}
	return fails;
}
}	// namespace tas_va2
