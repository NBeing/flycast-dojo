#!/usr/bin/env bash
# livetest - does the state-liveness watchdog actually SPEAK?
#
#   RUN:   scripts/livetest.sh
#   PASS:  a healthy state load produces "STATE LIVENESS: ... advancing", and
#          produces NO "not advancing". Exit 0.
#   FAIL:  exit 1, printing which half was wrong.
#   SKIP:  exit 77 (no build, no Xvfb, no ROM, no clip with a savestate).
#   SELF:  scripts/livetest.sh --self-test - proves this script's JUDGE can say
#          no, against four canned logs. Exit 0 when all four are judged right.
#
# WHY A SEPARATE HARNESS FROM selftest.sh. `scripts/selftest.sh` already runs
# liveness's eight claims, and they are claims about a class - arm(), check()
# and a grace period over four numbers, with no emulator anywhere. Every one of
# them passes just as happily if NOTHING EVER CALLS the class. That is not
# hypothetical here: `[MEASURED 2026-09-11]` the first wiring of this feature
# checked from mainui_rend_frame(), compiled clean, linked, ran, and emitted no
# verdict at all against the very defect it was written for - because in
# single-threaded rendering that loop IS the SH4 (Emulator::render ->
# recSh4_Run) and a wedged guest never returns to it. Eight green claims and a
# dead feature. CLAUDE.md: "a clean build is not evidence a feature is wired".
#
# So this asserts the WIRING, in a separate process, through the log: the
# watchdog thread started, the arm inside dc_loadstate fired, the verdict was
# reached and it came out somewhere a human can see.
#
# THE HEALTHY ARM, DELIBERATELY. Judging this against the known wedge would be
# easier and would rot the moment the wedge is fixed - the test would start
# failing BECAUSE the bug was repaired, which teaches everyone to delete it.
# "A load that works says so" is true before and after. The negative half - no
# "not advancing" - is what stops a watchdog that shouts at every load from
# passing, and that is the arm a single assertion would miss (CLAUDE.md: an
# arbiter that never refuses and one that refuses everything both satisfy one
# check).
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ALIVE='STATE LIVENESS: the machine is advancing'
DEAD='STATE LIVENESS: no frame completed'

# The judge, as a function, so --self-test exercises THE SAME CODE the real run
# does. A second copy for the fixtures would be a second definition of "pass".
# Echoes a one-word reason so the control can assert WHY it said no, not just
# that it did - selftest.sh's fixtures were rejected for the wrong reason for a
# whole commit and only the accept-arm noticed.
judge() {	# $1 = log file -> 0 and "ok", or 1 and a reason
	local log="$1" a d
	a=$(tr -d '\0' < "$log" | grep -ac "$ALIVE")
	d=$(tr -d '\0' < "$log" | grep -ac "$DEAD")
	# DEAD FIRST. `[MEASURED 2026-09-11]` with these two the other way round the
	# judge reported the right VERDICT for the wrong REASON: a log holding only
	# a Dead line read as "silent", when the watchdog had in fact spoken and was
	# wrong. Caught the first time this control ran, which is the whole argument
	# for asserting the reason rather than the exit code.
	if [ "$d" -gt 0 ]; then echo "cries-wolf"; return 1; fi
	if [ "$a" -eq 0 ]; then echo "silent"; return 1; fi
	echo "ok"; return 0
}

if [ "${1:-}" = "--self-test" ]; then
	tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
	: > "$tmp/silent.log"						# the feature is unwired
	echo "$DEAD in 3s of running" > "$tmp/wolf.log"			# fires on a healthy load
	{ echo "$ALIVE"; echo "$DEAD in 3s"; } > "$tmp/both.log"	# said both - not a pass
	echo "$ALIVE after the load" > "$tmp/good.log"			# must be ACCEPTED
	fails=0
	for want in "silent:silent" "wolf:cries-wolf" "both:cries-wolf" "good:ok"; do
		f=${want%%:*}; expect=${want##*:}
		got=$(judge "$tmp/$f.log")
		if [ "$got" = "$expect" ]; then
			echo "  SELF ok    $f -> $got"
		else
			echo "  SELF WRONG $f -> $got (wanted $expect)"; fails=$((fails+1))
		fi
	done
	[ $fails -eq 0 ] && { echo "livetest: the judge discriminates (4/4)"; exit 0; }
	echo "livetest: the judge is broken ($fails/4 wrong)"; exit 1
fi

[ -x "$EXE" ] || { echo "livetest: no build at $EXE - SKIP"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "livetest: no Xvfb - SKIP"; exit $SKIP; }

# REUSE testrun.sh RATHER THAN BOOTING A ROM HERE. It owns clip selection, the
# offscreen display, the config sandbox and the one-process-per-test rule, and a
# second copy of that would rot in the quiet direction. deferredslot.lua is the
# test chosen because loading a state is its whole subject.
out="$ROOT/build-dojo7/testresults/deferredslot.stdout.log"
rm -f "$out"
"$ROOT/scripts/testrun.sh" "$ROOT/scripts/tests/deferredslot.lua" >/dev/null 2>&1
rc=$?
if [ ! -s "$out" ]; then
	echo "livetest: deferredslot produced no log (rc=$rc) - SKIP"
	exit $SKIP
fi

reason=$(judge "$out")
case "$reason" in
ok)	echo "livetest: PASS - a healthy load reported itself alive, and nothing cried wolf"
	exit 0 ;;
silent)
	echo "livetest: FAIL - no liveness verdict in $out."
	echo "  The watchdog did not speak. Either startWatchdog() is not called,"
	echo "  dc_loadstate no longer arms, or the check runs somewhere it cannot."
	exit 1 ;;
cries-wolf)
	echo "livetest: FAIL - a HEALTHY load was reported as not advancing."
	tr -d '\0' < "$out" | grep -a "STATE LIVENESS" | head -3
	echo "  A warning that fires on good loads is one everybody turns off."
	exit 1 ;;
esac
