#!/usr/bin/env bash
# purgestaletest - does dojo:PurgeStale delete the states an edit/rewind just orphaned?
#
#   RUN:   scripts/purgestaletest.sh            PASS exit 0; a claim red 1; usage 2; SKIP 77
#   ARMS:  --sabotage off   the probe runs with PurgeStale=no: P2 must redden (no purge line),
#                           P1 stays green. Inverted exits: 0 fired / 4 decorative / 2 inconclusive.
#          --list-sabotage
#
# THE FAILURE THIS WOULD HAVE CAUGHT. `[MEASURED 2026-09-17]` the key was read by nothing
# here (docs/PORT-DEFECT-CENSUS.md §3): a launch profile passing PurgeStale=yes kept
# every dead-timeline state and the user seeked into them. The probe
# (scripts/tests/open/purge_stale.lua) pauses, saves slot 7 at frame F, edits F-20 -
# the rewind log grows, slot 7 is above the edit - and resumes.
#   P1 the probe ran and PASSED (testrun's verdict: paused, saved, edit applied)
#   P2 `TAS: auto-purged 1 stale state after guard event #N (BASE kept)` in the log
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
ARM=""
case "${1:-}" in
	"") ;;
	--list-sabotage) echo "off         P2 purge line - the probe runs with PurgeStale=no; P2 must redden, P1 must stay green"; exit 0 ;;
	--sabotage) ARM="${2:-}"; [ "$ARM" = off ] || { echo "purgestaletest: unknown arm '$ARM' (known: off)"; exit 2; } ;;
	*) echo "usage: $0 [--sabotage off | --list-sabotage]"; exit 2 ;;
esac
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
flag="-config dojo:PurgeStale=yes"; [ "$ARM" = off ] && { flag="-config dojo:PurgeStale=no"; echo "SABOTAGE armed: off - PurgeStale=no; the run below is EXPECTED to be red"; }
FLYCAST_TEST_OUT="$OUT" TESTRUN_EXTRA_CONFIG="$flag" "$ROOT/scripts/testrun.sh" "$ROOT/scripts/tests/open/purge_stale.lua" > "$OUT/testrun.txt" 2>&1; rc=$?
[ "$rc" -eq 77 ] && { echo "purgestaletest: SKIP - testrun skipped:"; tail -3 "$OUT/testrun.txt"; exit $SKIP; }
LOG="$OUT/purge_stale.stdout.log"
[ -f "$LOG" ] || { echo "purgestaletest: SKIP - no emulator log at $LOG"; tail -3 "$OUT/testrun.txt"; exit $SKIP; }
PASSED=0; FAILED=0; P2=ok
claim() { if [ "$2" = ok ]; then PASSED=$((PASSED+1)); printf '  ok   %s  %s\n' "$1" "$3"; else FAILED=$((FAILED+1)); printf '  FAIL %s  %s\n' "$1" "$3"; fi; }
line=$(tr -d '\0' < "$LOG" | grep -a 'TAS: auto-purged' | head -1 | sed 's/.*N\[NETWORK\]: //')
if [ "$rc" -eq 0 ]; then claim P1 ok "the probe ran and passed (testrun exit 0)"; else claim P1 FAIL "the probe did not pass (testrun exit $rc): $(grep -a 'FAIL\|SUMMARY' "$OUT/testrun.txt" | head -2 | tr '\n' ' ')"; fi
if [ -n "$line" ]; then claim P2 ok "$line"; else P2=FAIL; claim P2 FAIL "no 'TAS: auto-purged' line ($(tr -d '\0' < "$LOG" | grep -a -c 'TAS EDIT: applied') edit(s) applied)"; fi
tr -d '\0' < "$OUT/purge_stale.lua.log" 2>/dev/null | grep -a 'PURGE PROBE' | sed 's/^/       /'
echo "PURGESTALE RESULT: passed=$PASSED failed=$FAILED"
if [ -n "$ARM" ]; then
	if [ "$P2" = FAIL ] && [ "$rc" -eq 0 ]; then echo "SABOTAGE BEHAVED AS PREDICTED"; echo "PASS purgestaletest --sabotage off - P2 reddened, P1 stayed green"; exit 0; fi
	[ "$rc" -ne 0 ] && { echo "INCONCLUSIVE purgestaletest --sabotage off - the probe itself failed (exit 2)"; exit 2; }
	echo "FAIL purgestaletest --sabotage off - P2 stayed green with PurgeStale=no: the check is decorative (exit 4)"; exit 4
fi
[ "$FAILED" -eq 0 ] && { echo "PASS purgestaletest - the edit orphaned slot 7 and the purge removed it"; exit 0; }
echo "FAIL purgestaletest - $FAILED claim(s) red"; exit 1
