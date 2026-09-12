#!/usr/bin/env bash
# clocktest - can the frame-clock type barrier actually say no?
#
#   RUN:   scripts/clocktest.sh
#   PASS:  the compiler REJECTS every snippet that mixes two frame clocks, and
#          ACCEPTS the one that stays on one clock. Exit 0.
#   FAIL:  exit 1, naming the snippet that was judged wrongly.
#   SKIP:  exit 77 (no C++ compiler).
#
# WHY A COMPILER AND NOT A SELF-TEST. `core/frame_clock.h`'s whole purpose is
# that `movieFrame - vblankFrame` DOES NOT BUILD. Code that does not build
# cannot be run, so no in-process suite can reach this half - FRAMECLOCK
# SELFTEST covers the arithmetic that does compile, and is blind to the barrier
# itself. The `static_assert`s in the header are real but weak on their own:
# `!is_convertible<A,B>` is true of any two unrelated classes, including two
# that a later refactor accidentally makes freely mixable through some operator
# nobody thought about. The only evidence that the barrier holds is handing a
# compiler the mistake and watching it refuse.
#
# THE ACCEPT ARM IS NOT OPTIONAL. Four rejections are also what a broken
# harness produces - a typo'd include path, a missing -I, a snippet that fails
# for a reason having nothing to do with clocks. `[MEASURED 2026-09-10]`
# scripts/selftest.sh's three sabotage arms were all being rejected for the
# wrong reason for a whole commit, and only its accept arm could see it. So
# every rejection here must additionally name the CLOCK types in the compiler's
# own words, and one snippet must build clean.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null || { echo "clocktest: no $CXX - SKIP"; exit $SKIP; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
INC=(-I "$ROOT/core" -I "$ROOT/core/deps" -I "$ROOT/core/deps/nowide/include")

# try <name> <expect: reject|accept> <body>
fails=0
try() {
	local name="$1" expect="$2" body="$3" out rc
	printf '#include "frame_clock.h"\nvoid probe_%s() {\n%s\n}\n' "$name" "$body" > "$TMP/$name.cpp"
	out=$("$CXX" -fsyntax-only -std=c++17 "${INC[@]}" "$TMP/$name.cpp" 2>&1); rc=$?

	if [ "$expect" = accept ]; then
		if [ $rc -eq 0 ]; then echo "  ok      $name: accepted, as it must be"
		else
			echo "  WRONG   $name: the LEGAL snippet did not compile - the harness is broken,"
			echo "          not the barrier. Every rejection below is therefore meaningless."
			echo "$out" | head -4 | sed 's/^/          /'
			fails=$((fails+1))
		fi
		return
	fi

	if [ $rc -eq 0 ]; then
		echo "  WRONG   $name: compiled. Two frame clocks were mixed and nothing stopped it."
		fails=$((fails+1))
		return
	fi
	# REJECTED - but for the right reason? A missing header rejects everything.
	# Requiring the clock type names in the diagnostic is what distinguishes
	# "the barrier held" from "this file never compiled at all".
	if echo "$out" | grep -qE "Count<frames::(Movie|Delivered|Vblank)Clock>|frames::(Movie|Delivered|Vblank)"; then
		echo "  ok      $name: rejected, and the diagnostic names the clocks"
	else
		echo "  WRONG   $name: rejected for some OTHER reason - the barrier is not what stopped it."
		echo "$out" | head -3 | sed 's/^/          /'
		fails=$((fails+1))
	fi
}

echo "clocktest: handing $CXX five snippets"

try mix_subtract reject '
	frames::Movie  m(9948);
	frames::Vblank v(500);
	s64 d = m - v;		// the bug this file exists to prevent
	(void)d;'

try cross_assign reject '
	frames::Vblank v = frames::movie();	// the movie index is not a vblank count
	(void)v;'

try raw_integer reject '
	frames::Movie m = 9948;		// a bare integer is not a position on a clock
	(void)m;'

try cross_compare reject '
	bool b = frames::movie() < frames::vblank();
	(void)b;'

try same_clock accept '
	frames::Movie a(100), b(9948);
	s64   d = b - a;			// a delta on ONE clock is fine, and signed
	frames::Movie c = a + 60;	// and so is advancing
	bool  o = a < b;
	(void)d; (void)c; (void)o;'

if [ $fails -eq 0 ]; then
	echo "clocktest: PASS - four mixtures refused, one legal use accepted"
	exit 0
fi
echo "clocktest: FAIL - $fails of 5 snippets judged wrongly"
exit 1
