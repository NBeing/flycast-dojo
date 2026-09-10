#!/usr/bin/env bash
# selftest - run every in-process self-test the tree has and judge the lot.
#
#   RUN:   scripts/selftest.sh
#   PASS:  every "<NAME> SELFTEST: N passed, 0 failed" line says 0 failed, AND
#          at least MIN_SUITES suites and MIN_CLAIMS claims actually ran.
#   FAIL:  exit 1, naming the suites that failed or the floor that was missed.
#   SKIP:  exit 77 (no Xvfb, no build).
#   SELF:  scripts/selftest.sh --self-test - proves this script's JUDGE can say
#          no. Exits 0 when all three sabotage arms are correctly rejected.
#
# WHY THIS EXISTS. `[MEASURED 2026-09-10]` `dojo:PanelSelfTest=yes` runs 17
# suites and 331 claims, and NOTHING RAN THEM. ctest had ten entries, all of
# which boot a ROM; these need no ROM, no input and no window manager, and
# finish in under a second. Three-hundred-odd assertions were being maintained
# and executed only when somebody happened to pick the option out of the rofi
# launcher.
#
# THE FLOORS ARE THE POINT, and they are why this is not a one-line grep.
# "0 failed" is also what a build where the self-tests never ran looks like:
# every one of them opens with `if (!cfgLoadBool("dojo", "PanelSelfTest",
# false)) return;`, so a typo'd config key, a dropped call in flycast_init, or
# a suite quietly deleted all produce a clean, green, empty log. CLAUDE.md
# doctrine rule 5: a skipped check must not report as a passing one. So the
# count is asserted, not just the verdict.
#
# The floors are deliberately BELOW the current numbers - they catch a suite
# vanishing, not a claim being retired. Raise them when a batch of suites
# lands; do not lower them to make a run pass.
set -uo pipefail

SKIP=77
MIN_SUITES=15
MIN_CLAIMS=300

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
# THE COLON IS ADDED HERE, and the variable is a bare number - the convention
# scripts/docktest.sh already uses and CMakeLists.txt already passes.
#
# `[MEASURED 2026-09-10]` this file had `${SELFTEST_DISPLAY:-:145}`, so ctest's
# `SELFTEST_DISPLAY=145` produced `Xvfb 145`, which answers "Unrecognized option:
# 145" and exits. The two harnesses that had this bug failed DIFFERENTLY and
# neither said so: hotkeytest skipped with "no window", and selftest PASSED -
# its 331 claims run inside flycast_init, before os_CreateWindow, so they never
# needed the display the script insists on having. A prerequisite nobody can
# see is false is not a prerequisite.
DISP=":${SELFTEST_DISPLAY:-145}"

# ---- THE JUDGE, as a function, because --self-test has to be able to run it
# against a log this script did not produce. A judge you cannot feed is a judge
# you cannot prove wrong.
judge() {	# $1 = log file -> prints a verdict, returns 0 pass / 1 fail
	local log="$1"
	local lines suites claims failed badsuites
	# `grep -a`: the emulator writes NUL bytes into its output, so plain grep
	# silently matches nothing on a captured log (CLAUDE.md).
	lines=$(tr -d '\0' < "$log" | grep -aoE "[A-Z]+ SELFTEST: [0-9]+ passed, [0-9]+ failed")
	suites=$(printf '%s\n' "$lines" | grep -c "SELFTEST:" )
	[ -z "$lines" ] && suites=0
	claims=$(printf '%s\n' "$lines" | sed -n 's/.*SELFTEST: \([0-9]*\) passed, \([0-9]*\) failed/\1 \2/p' \
			| awk '{p+=$1; f+=$2} END {print p+f+0}')
	failed=$(printf '%s\n' "$lines" | sed -n 's/.*passed, \([0-9]*\) failed/\1/p' \
			| awk '{f+=$1} END {print f+0}')
	badsuites=$(printf '%s\n' "$lines" | grep -av ", 0 failed" | sed 's/ SELFTEST.*//' | tr '\n' ' ')

	echo "  $suites suites, $claims claims, $failed failed"
	if [ "$suites" -lt "$MIN_SUITES" ]; then
		echo "VACUOUS - only $suites suites reported, floor is $MIN_SUITES."
		echo "          A green log with no suites in it is what 'the self-tests"
		echo "          stopped running' looks like, so this is a FAILURE."
		return 1
	fi
	if [ "$claims" -lt "$MIN_CLAIMS" ]; then
		echo "VACUOUS - only $claims claims ran, floor is $MIN_CLAIMS."
		return 1
	fi
	if [ "$failed" -gt 0 ]; then
		echo "FAILED  - $failed claims failed in: $badsuites"
		return 1
	fi
	return 0
}

# ---- --self-test: three canned logs the judge MUST reject ------------------
if [ "${1:-}" = "--self-test" ]; then
	tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
	rc=0

	# SUITE NAMES ARE LETTERS, and that is not cosmetic. `[MEASURED 2026-09-10]`
	# the first version of these fixtures used SUITE1..SUITE20; the judge's
	# pattern is `[A-Z]+ SELFTEST:`, so the DIGIT before " SELFTEST" meant not
	# one of them matched. All three arms were then rejected for having zero
	# suites - including arm A, which is supposed to exercise the failure path
	# and never reached it. Only the clean-run control below caught that.
	names="AA AB AC AD AE AF AG AH AI AJ AK AL AM AN AO AP AQ AR AS AT"
	full() { for n in $names; do echo "$n SELFTEST: 20 passed, 0 failed"; done; }

	# A. a real failure inside an otherwise full run -> rejected as FAILED
	{ full; echo "BROKEN SELFTEST: 19 passed, 1 failed"; } > "$tmp/a.log"
	# B. the self-tests never ran - the log is clean and empty -> VACUOUS
	echo "flycast started, nothing to see here" > "$tmp/b.log"
	# C. enough suites, hollowed out to one claim each -> VACUOUS
	{ for n in $names; do echo "$n SELFTEST: 1 passed, 0 failed"; done; } > "$tmp/c.log"

	# THE REASON IS ASSERTED, not just the verdict. An arm rejected for the
	# wrong reason is the defect this block was written to have, and "it said
	# no" cannot tell the two apart.
	for arm in "a:FAILED:a real failure is caught" \
			"b:VACUOUS:an empty log is VACUOUS, not a pass" \
			"c:VACUOUS:hollowed-out suites are VACUOUS"; do
		f="${arm%%:*}"; rest="${arm#*:}"; want="${rest%%:*}"; what="${rest#*:}"
		out="$(judge "$tmp/$f.log")"
		if [ $? -eq 0 ]; then
			echo "FAIL selftest --self-test - $what: the judge said PASS"
			echo "$out" | sed 's/^/    /'
			rc=1
		elif ! printf '%s' "$out" | grep -q "$want"; then
			echo "FAIL selftest --self-test - $what: rejected, but as the WRONG KIND"
			echo "    wanted $want, got:"
			echo "$out" | sed 's/^/    /'
			rc=1
		else
			echo "  ok - $what (as $want)"
		fi
	done
	# The CONTROL. Without it a judge that rejects everything passes all three
	# arms above, which is the pair CLAUDE.md doctrine rule 3 asks for.
	full > "$tmp/ok.log"
	if judge "$tmp/ok.log" >/dev/null; then
		echo "  ok - a genuinely clean run is still accepted"
	else
		echo "FAIL selftest --self-test - the judge rejects a clean run; it refuses everything"
		rc=1
	fi
	[ $rc -eq 0 ] && echo "PASS selftest --self-test - the judge can say no, and can say yes"
	exit $rc
fi

# ---- the real run ----------------------------------------------------------
[ -x "$EXE" ] || { echo "selftest: SKIP - not built"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "selftest: SKIP - no Xvfb"; exit $SKIP; }

OUT="${SELFTEST_OUT:-}"
if [ -n "$OUT" ]; then mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
fi
mkdir -p "$OUT/config/flycast-dojo" "$OUT/data"

# NO ROM. Every suite here runs inside flycast_init, before a game is loaded -
# which is exactly why this is cheap enough to run on every build.
nohup Xvfb "$DISP" -screen 0 640x480x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 & XPID=$!
sleep 2
XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:PanelSelfTest=yes -config dojo:UiIni=no \
	-config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	> "$OUT/out.log" 2>&1 & FC=$!
sleep 12
# PID-SCOPED, and confirmed dead. Never `pkill -x Xvfb` - it matches displays
# this script did not start, including the user's.
kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null
sleep 2
kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null
kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null

tr -d '\0' < "$OUT/out.log" | grep -aoE "[A-Z]+ SELFTEST: [0-9]+ passed, [0-9]+ failed" | sed 's/^/  /'
if judge "$OUT/out.log"; then
	echo "PASS selftest - every in-process suite ran and every claim held"
	exit 0
fi
echo "FAIL selftest"
exit 1
