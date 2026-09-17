#!/usr/bin/env bash
# arms.sh - the SABOTAGE JUDGE, lifted from emuapi's arms.lua (M.judge, lines 119-160)
# so a harness in this tree prints the SAME lines and markers a reader of that tree
# already greps for. Nothing here is invented; the three rules and every string are
# arms.lua's, byte for byte:
#
#   (1) the arm APPLIED         - failed > 0. A control that silently fails to apply
#                                 runs the unmodified code and passes, "the defect
#                                 wearing a lab coat".
#   (2) it broke its TARGET     - must_break is a substring of some BROKEN case.
#   (3) it LEFT its CONTROL     - must_not_break is a substring of some SEEN case AND
#       green, and the control    of no BROKEN case. Testing only absence from the
#       RAN                       broken list passes a control that stopped running.
#   INCONCLUSIVE (exit 2) when must_break is not in SEEN at all: an arm whose target
#   never ran proves nothing, and without this it is indistinguishable from an arm
#   the suite caught.
#
#   USAGE:  arms.sh judge <name> <what> <must_break> <must_not_break> <failed> <seenfile> <brokenfile>
#             seenfile/brokenfile: one case name per line. Prints the verdict block,
#             exits 0 behaved / 1 did not / 2 INCONCLUSIVE.
#           arms.sh --self-test   - the judge through every outcome, no emulator.
#
# Matching is SUBSTRING (arms.lua's hit(): s:find(needle, 1, true)), so a declaration
# may name a step by its prefix. Vacuity is not an input here on purpose: arms.lua
# ignores it under an arm ("a run that is supposed to be broken has no business
# reporting on its own coverage"); the harness applies that rule before calling.
set -uo pipefail

hit() {	# hit <file> <needle>  - true when some line of <file> contains <needle>
	[ -s "$1" ] && grep -qF -- "$2" "$1"
}

judge() {
	local name=$1 what=$2 must_break=$3 must_not_break=$4 failed=$5 seen=$6 broken=$7
	local expect="the run above is EXPECTED to be red" label="the run went red"
	echo ""
	echo "=== SABOTAGE $name: $what"
	echo "    ($expect)"
	if ! hit "$seen" "$must_break"; then
		echo "  SKIP  the case this arm targets never ran"
		echo "        '$must_break'"
		echo "        so this arm is INCONCLUSIVE, which is not a pass"
		echo "SABOTAGE INCONCLUSIVE"
		return 2
	fi
	local bad=0
	want() {	# want <cond 0|1> <what> <detail>
		if [ "$1" = 1 ]; then printf '  ok    %s -- %s\n' "$2" "$3"
		else printf '  FAIL  %s -- %s\n' "$2" "$3"; bad=$((bad+1)); fi
	}
	want "$([ "${failed:-0}" -gt 0 ] && echo 1 || echo 0)" "the arm applied -- $label" "${failed:-0} failing cases"
	want "$(hit "$broken" "$must_break" && echo 1 || echo 0)" "it broke the case it targets" "'$must_break'"
	want "$(hit "$seen" "$must_not_break" && ! hit "$broken" "$must_not_break" && echo 1 || echo 0)" \
		"and it LEFT its control case green" "'$must_not_break'"
	if [ "$bad" -eq 0 ]; then echo "SABOTAGE BEHAVED AS PREDICTED"; return 0; fi
	echo "SABOTAGE DID NOT BEHAVE AS PREDICTED: $bad"
	return 1
}

selftest() {	# arms.lua M.selftest(), the armed outcomes (the unarmed ones are the harness's own gates)
	local good=0 bad=0 t out code
	t="$(mktemp -d)"; trap 'rm -rf "$t"' RETURN
	want() { if [ "$1" = 1 ]; then echo "  ok    $2"; good=$((good+1)); else echo "  FAIL  $2"; bad=$((bad+1)); fi; }
	run() {	# run <seen...> -- <broken...>   (sets out, code)
		: > "$t/seen"; : > "$t/broken"; local f="$t/seen"
		for a in "$@"; do if [ "$a" = "--" ]; then f="$t/broken"; else echo "$a" >> "$f"; fi; done
		local n; n=$(wc -l < "$t/broken")
		out="$(judge a "a test arm" TARGET CONTROL "$n" "$t/seen" "$t/broken")"; code=$?
	}
	echo "arms.sh --self-test: the verdict logic, through every outcome"; echo ""

	run TARGET CONTROL -- TARGET
	want "$([ $code -eq 0 ] && echo 1 || echo 0)" "an arm that breaks its target and holds its control exits 0"
	want "$(grep -qF 'SABOTAGE BEHAVED AS PREDICTED' <<<"$out" && echo 1 || echo 0)" "and prints the BEHAVED marker"

	# THE ONE THAT MATTERS: the arm did not apply.
	run TARGET CONTROL --
	want "$([ $code -eq 1 ] && echo 1 || echo 0)" "an arm that changed nothing exits 1"
	want "$(grep -F 'the arm applied' <<<"$out" | grep -qF FAIL && echo 1 || echo 0)" "and it is reported as the arm failing to apply, by name"

	run TARGET CONTROL OTHER -- OTHER
	want "$([ $code -eq 1 ] && echo 1 || echo 0)" "an arm that broke a DIFFERENT case exits 1"
	want "$(grep -F 'broke the case it targets' <<<"$out" | grep -qF FAIL && echo 1 || echo 0)" "and it is reported as missing its target"

	run TARGET CONTROL -- TARGET CONTROL
	want "$([ $code -eq 1 ] && echo 1 || echo 0)" "an arm that breaks its own control exits 1"
	want "$(grep -F 'LEFT its control case green' <<<"$out" | grep -qF FAIL && echo 1 || echo 0)" "and it is reported as a demolition rather than a measurement"

	run CONTROL --
	want "$([ $code -eq 2 ] && echo 1 || echo 0)" "an arm whose target never ran exits 2, not 0 and not 1"
	want "$(grep -qF INCONCLUSIVE <<<"$out" && echo 1 || echo 0)" "and says INCONCLUSIVE"

	# A control that is ABSENT from the broken list but never RAN is not green.
	run TARGET -- TARGET
	want "$([ $code -eq 1 ] && echo 1 || echo 0)" "a control that never ran does not count as left green (exits 1)"

	echo ""; echo "$good ok, $bad FAILED"
	[ "$bad" -eq 0 ]
}

case "${1:-}" in
	judge)       shift; [ $# -eq 7 ] || { echo "usage: arms.sh judge <name> <what> <must_break> <must_not_break> <failed> <seenfile> <brokenfile>"; exit 2; }; judge "$@" ;;
	--self-test) selftest ;;
	*)           echo "usage: arms.sh judge ... | arms.sh --self-test"; exit 2 ;;
esac
