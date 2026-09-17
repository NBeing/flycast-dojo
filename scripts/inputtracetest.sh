#!/usr/bin/env bash
# inputtracetest - does dojo:InputTrace SPEAK when an input is held, and STAY QUIET when
# everything is neutral?
#
#   RUN:   scripts/inputtracetest.sh            PASS exit 0; a claim red exit 1; usage 2; SKIP 77
#   ARMS:  --sabotage silent   the probe runs with InputTrace=no: T2 must redden (no trace
#                              line at all), T1 stays green. Inverted: 0 fired / 4 decorative.
#          --list-sabotage
#
# THE FAILURE THIS WOULD HAVE CAUGHT. `[MEASURED 2026-09-17]` the key was in David's tree
# and read by nothing here (docs/PORT-DEFECT-CENSUS.md §3) - a launch profile passing
# InputTrace=yes got a silent log and read it as "every input is neutral". The tracer's
# own contract is the trap: silence IS an answer, so a dead tracer is indistinguishable
# from a quiet pad unless something makes the pad NOT quiet and checks. That is the Lua
# probe: 240 neutral vblanks, P1 A held for 60, neutral again.
#   T1 the probe ran and PASSED (scripts/testrun.sh's own verdict on the Lua test)
#   T2 a `TAS INPUT: P1 kcode=~0x4` line exists (the press was named, with the right bit)
#   T3 the tracer is THROTTLED: <= 6 TAS INPUT lines for a 60-frame hold in a run of
#      ~400 frames (a change prints once, an unchanged hold every 5 s, neutral prints
#      nothing) - the "firehose it exists to replace" arm
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
ARM=""
case "${1:-}" in
	"") ;;
	--list-sabotage) echo "silent      T2 trace named the press - the probe runs with InputTrace=no; T2 must redden, T1 must stay green"; exit 0 ;;
	--sabotage) ARM="${2:-}"; [ "$ARM" = silent ] || { echo "inputtracetest: unknown arm '$ARM' (known: silent)"; exit 2; } ;;
	*) echo "usage: $0 [--sabotage silent | --list-sabotage]"; exit 2 ;;
esac
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
flag="-config dojo:InputTrace=yes"; [ "$ARM" = silent ] && { flag="-config dojo:InputTrace=no"; echo "SABOTAGE armed: silent - InputTrace=no; the run below is EXPECTED to be red"; }
FLYCAST_TEST_OUT="$OUT" TESTRUN_EXTRA_CONFIG="$flag" "$ROOT/scripts/testrun.sh" "$ROOT/scripts/tests/open/input_trace.lua" > "$OUT/testrun.txt" 2>&1; rc=$?
[ "$rc" -eq 77 ] && { echo "inputtracetest: SKIP - testrun skipped:"; tail -3 "$OUT/testrun.txt"; exit $SKIP; }
LOG="$OUT/input_trace.stdout.log"
[ -f "$LOG" ] || { echo "inputtracetest: SKIP - no emulator log at $LOG"; tail -3 "$OUT/testrun.txt"; exit $SKIP; }
PASSED=0; FAILED=0; T2=ok
claim() { if [ "$2" = ok ]; then PASSED=$((PASSED+1)); printf '  ok   %s  %s\n' "$1" "$3"; else FAILED=$((FAILED+1)); printf '  FAIL %s  %s\n' "$1" "$3"; fi; }
lines=$(tr -d '\0' < "$LOG" | grep -a -c 'TAS INPUT: ')
named=$(tr -d '\0' < "$LOG" | grep -a -c 'TAS INPUT: P1 kcode=~0x4')
if [ "$rc" -eq 0 ]; then claim T1 ok "the probe ran and passed (testrun exit 0)"; else claim T1 FAIL "the probe did not pass (testrun exit $rc): $(grep -a 'FAIL\|SUMMARY' "$OUT/testrun.txt" | head -2 | tr '\n' ' ')"; fi
if [ "$named" -ge 1 ]; then claim T2 ok "the press was named: $named line(s) 'TAS INPUT: P1 kcode=~0x4'"; else T2=FAIL; claim T2 FAIL "no 'TAS INPUT: P1 kcode=~0x4' line ($lines TAS INPUT lines in all)"; fi
if [ "$lines" -ge 1 ] && [ "$lines" -le 6 ]; then claim T3 ok "throttled: $lines TAS INPUT line(s) for a 60-frame hold in ~400 frames"; elif [ "$lines" -gt 6 ]; then claim T3 FAIL "a firehose: $lines TAS INPUT lines"; else claim T3 FAIL "silent: 0 TAS INPUT lines"; fi
tr -d '\0' < "$LOG" | grep -a 'TAS INPUT: ' | head -3 | sed 's/^/       /'
echo "INPUTTRACE RESULT: passed=$PASSED failed=$FAILED"
if [ -n "$ARM" ]; then
	if [ "$T2" = FAIL ] && [ "$rc" -eq 0 ]; then echo "SABOTAGE BEHAVED AS PREDICTED"; echo "PASS inputtracetest --sabotage silent - T2 reddened, T1 stayed green"; exit 0; fi
	[ "$rc" -ne 0 ] && { echo "INCONCLUSIVE inputtracetest --sabotage silent - the probe itself failed (exit 2)"; exit 2; }
	echo "FAIL inputtracetest --sabotage silent - T2 stayed green with InputTrace=no: the check is decorative (exit 4)"; exit 4
fi
[ "$FAILED" -eq 0 ] && { echo "PASS inputtracetest - the tracer named the press and stayed quiet otherwise"; exit 0; }
echo "FAIL inputtracetest - $FAILED claim(s) red"; exit 1
