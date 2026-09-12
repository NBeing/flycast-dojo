#!/usr/bin/env bash
# openarm - run a test for a KNOWN-OPEN defect and require it to fail FOR THE
#           STATED REASON.
#
#   RUN:   scripts/openarm.sh <test.lua> "<the claim that must still fail>"
#   PASS:  the named claim is present and FAILING, and no other claim failed.
#          Exit 0 - the defect is still there and still shaped as documented.
#   FAIL:  exit 1, and the message says which of the three ways it went wrong.
#   SKIP:  exit 77, passed straight through from scripts/testrun.sh.
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

[ $# -eq 2 ] || { echo "usage: openarm.sh <test.lua> <claim substring>"; exit 1; }
LUA="$1"; CLAIM="$2"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
NAME="$(basename "$LUA" .lua)"

"$ROOT/scripts/testrun.sh" --timeout 120 "$LUA" >/dev/null 2>&1
rc=$?
[ $rc -eq $SKIP ] && { echo "openarm: testrun skipped - SKIP"; exit $SKIP; }

LOG="$ROOT/build-dojo7/testresults/$NAME.lua.log"
[ -s "$LOG" ] || { echo "openarm: no log at $LOG - the test did not run"; exit 1; }

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
		exit 1
	fi
	echo "openarm: still open - '$CLAIM' fails, and nothing else does"
	exit 0 ;;
fixed)
	echo "openarm: THE DEFECT APPEARS TO BE FIXED."
	echo "  '$CLAIM' now PASSES in $LUA."
	echo "  This is not a test failure - it is a request. Move the [OPEN] item in"
	echo "  docs/TEST-PLAN.md, move the test out of scripts/tests/open/ into"
	echo "  scripts/tests/ so the green suite picks it up, and drop this entry."
	exit 1 ;;
missing)
	echo "openarm: BROKE DIFFERENTLY - no claim matching '$CLAIM' ran at all."
	echo "  The defect may well still be there; this run is not evidence."
	tail -6 "$LOG" | sed 's/^/    /'
	exit 1 ;;
esac
