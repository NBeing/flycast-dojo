#!/usr/bin/env bash
# openarm - run a test for a KNOWN-OPEN defect and require it to fail FOR THE
#           STATED REASON.
#
#   RUN:   scripts/openarm.sh <test.lua> "<the claim that must still fail>"
#   PASS:  the named claim is present and FAILING, and no other claim failed.
#          Exit 0 - the defect is still there and still shaped as documented.
#   FAIL:  exit 1, and the message says which of the three ways it went wrong.
#   SKIP:  exit 77, passed straight through from scripts/testrun.sh.
#   SELF:  scripts/openarm.sh --self-test - five canned logs the judge must
#          sort, including the CONTROL. No ROM, no display, no emulator.
#
# WHY NOT ctest's WILL_FAIL. That was the first version and it is a trap of the
# exact kind CLAUDE.md rule 1 describes: WILL_FAIL inverts ANY non-zero exit, so
# a crash, a Lua syntax error, a missing ROM or a renamed binding all read as
# "Passed" - the defect is reported as present and correctly shaped by a run
# that never reached the claim. `[MEASURED 2026-09-12]` the entry it replaced
# took 10.79 s and said "Passed", and only -V showed that the failing claim was
# the intended one rather than a broken harness.
#
# THREE OUTCOMES, AND EACH GETS ITS OWN MESSAGE, because "this went red" is not
# information when red is the expected state:
#
#   still open        the claim failed. Exit 0. Nothing to do.
#   apparently FIXED  the claim passed. Exit 1, loudly - somebody fixed the
#                     emulator and now owes the [OPEN] an update. A test that
#                     quietly starts passing is how an open item rots into a
#                     stale paragraph, and this is the whole reason the arm is
#                     kept executable instead of written down.
#   broke differently the claim is missing, or something ELSE failed. Exit 1 -
#                     the defect may still be there, but this run is not
#                     evidence either way.
set -uo pipefail
SKIP=77

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"

# THE JUDGE, as a function taking a log file and a claim, so --self-test can
# feed it logs no emulator on this machine produced. A judge you cannot feed is
# a judge you cannot prove wrong - and this file IS a judge: it is the sole
# arbiter of both known-open entries, and it replaced WILL_FAIL precisely
# because WILL_FAIL had stopped discriminating without anyone noticing.
judge() {	# $1 = log, $2 = claim, $3 = a label for messages -> 0 open / 1 not
	local LOG="$1" CLAIM="$2" WHERE="$3" verdict OTHER

	[ -s "$LOG" ] || { echo "openarm: no log at $LOG - the test did not run"; return 1; }

	# Read the FILE, never a pipeline into grep -q: `[SOURCE]` CLAUDE.md, pipefail
	# turns a successful match into exit 141 depending on how big the input is.
	if   grep -aq "FAIL  .*$CLAIM" "$LOG"; then verdict=open
	elif grep -aq "PASS  .*$CLAIM" "$LOG"; then verdict=fixed
	else verdict=missing
	fi

	# ONLY the named claim may be failing. Another red claim means this run says
	# nothing about the defect, however it exited.
	OTHER=$(grep -a "FAIL  " "$LOG" | grep -av "$CLAIM" | head -3)

	case "$verdict" in
	open)
		if [ -n "$OTHER" ]; then
			echo "openarm: INCONCLUSIVE - '$CLAIM' failed as expected, but so did:"
			echo "$OTHER" | sed 's/^/    /'
			echo "  Fix those first; until then this run is not evidence about the open defect."
			return 1
		fi
		echo "openarm: still open - '$CLAIM' fails, and nothing else does"
		return 0 ;;
	fixed)
		echo "openarm: THE DEFECT APPEARS TO BE FIXED."
		echo "  '$CLAIM' now PASSES in $WHERE."
		echo "  This is not a test failure - it is a request. Move the [OPEN] item in"
		echo "  docs/TEST-PLAN.md, move the test out of scripts/tests/open/ into"
		echo "  scripts/tests/ so the green suite picks it up, and drop this entry."
		return 1 ;;
	missing)
		echo "openarm: BROKE DIFFERENTLY - no claim matching '$CLAIM' ran at all."
		echo "  The defect may well still be there; this run is not evidence."
		tail -6 "$LOG" | sed 's/^/    /'
		return 1 ;;
	esac
}

# THE ARM. Five canned logs, fed to the judge above.
#
# `[MEASURED 2026-09-14]` this file was the WILL_FAIL replacement, the sole
# judge of flycast.emptyslot_known_open and flycast.replay_determinism_known_open,
# distinguishing three verdicts - and it had no coverage of that discrimination
# at all. selftest.sh, livetest.sh and checks.sh each had to learn the same
# thing separately: THE JUDGE IS THE PART THAT SILENTLY STOPS DISCRIMINATING,
# because nothing downstream of it can tell a correct verdict from a constant.
#
# Arm 1 is the CONTROL and is not optional. A judge that returns 1
# unconditionally - the single most likely way for this file to rot - satisfies
# arms 2, 3, 4 and 5 all four, and only the control catches it.
if [ "${1:-}" = "--self-test" ]; then
	rc=0
	tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

	cat > "$tmp/open.log" <<'LOGEOF'
PASS  the movie advanced
FAIL  old_sr mutated on a refused load
PASS  the slot stayed empty
LOGEOF
	cat > "$tmp/fixed.log" <<'LOGEOF'
PASS  the movie advanced
PASS  old_sr mutated on a refused load
PASS  the slot stayed empty
LOGEOF
	cat > "$tmp/missing.log" <<'LOGEOF'
PASS  the movie advanced
PASS  the slot stayed empty
LOGEOF
	cat > "$tmp/alsofailed.log" <<'LOGEOF'
FAIL  old_sr mutated on a refused load
FAIL  the movie advanced
LOGEOF
	: > "$tmp/empty.log"

	check() {	# $1 = log, $2 = wanted rc, $3 = what it proves
		local out got
		out="$(judge "$1" "old_sr mutated on a refused load" "canned" 2>&1)"; got=$?
		if [ "$got" -eq "$2" ]; then
			echo "  ok - $3"
		else
			echo "FAIL openarm --self-test - $3: wanted rc=$2, got rc=$got"
			echo "$out" | sed 's/^/      /'
			rc=1
		fi
	}
	# THE CONTROL. Without it a judge that always returns 1 passes everything else.
	check "$tmp/open.log"       0 "a still-open defect is ACCEPTED, not reported as a failure"
	check "$tmp/fixed.log"      1 "a claim that started passing is reported, not silently green"
	check "$tmp/missing.log"    1 "a claim that never ran is 'broke differently', not 'still open'"
	check "$tmp/alsofailed.log" 1 "another red claim makes the run inconclusive"
	check "$tmp/empty.log"      1 "an empty log is 'did not run', not a quiet pass"

	# NON-VACUITY: the three verdicts must be DISTINGUISHABLE, not merely all
	# non-zero. Without this, collapsing them into one message still passes above.
	#
	# GREP THE FILE, NEVER `judge ... | grep -q`. `[MEASURED 2026-09-14]` the first
	# version of this loop piped, and reported all three as failures while all
	# three messages were plainly there. This is CLAUDE.md's pipefail trap arriving
	# by its OTHER route: the documented one is SIGPIPE turning a match into 141,
	# but here the upstream legitimately returns 1 - every verdict except
	# "still open" does - and pipefail hands back the JUDGE'S verdict in place of
	# grep's answer. Same fix either way, and it is the fix the doctrine names:
	# write the output to a file once and grep the file.
	for pair in "fixed.log:APPEARS TO BE FIXED" "missing.log:BROKE DIFFERENTLY" "alsofailed.log:INCONCLUSIVE"; do
		log="${pair%%:*}"; want="${pair#*:}"
		judge "$tmp/$log" "old_sr mutated on a refused load" "canned" > "$tmp/said.txt" 2>&1 || true
		if grep -q "$want" "$tmp/said.txt"; then
			echo "  ok - '$want' is said in its own words"
		else
			echo "FAIL openarm --self-test - $log did not say '$want'"
			sed 's/^/      /' "$tmp/said.txt"
			rc=1
		fi
	done

	[ $rc -eq 0 ] && echo "PASS openarm --self-test - the judge sorts open, fixed, missing, inconclusive and nothing"
	exit $rc
fi

[ $# -eq 2 ] || { echo "usage: openarm.sh <test.lua> <claim substring>"; exit 1; }
LUA="$1"; CLAIM="$2"
NAME="$(basename "$LUA" .lua)"

"$ROOT/scripts/testrun.sh" --timeout 120 "$LUA" >/dev/null 2>&1
rc=$?
[ $rc -eq $SKIP ] && { echo "openarm: testrun skipped - SKIP"; exit $SKIP; }

judge "$ROOT/build-dojo7/testresults/$NAME.lua.log" "$CLAIM" "$LUA"
exit $?
