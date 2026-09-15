#!/usr/bin/env bash
# tas_clip_parity - assert the "keep identical to David" functions in our
# core/dojo/tas_clip.cpp still match the pinned reference/flycast-rr, modulo the
# David->dev comment convention.
#
#   RUN:   scripts/tas_clip_parity.sh
#   PASS:  each listed function's body is byte-identical to the pin (after
#          s/David/dev/). No emulator, no build.
#   FAIL:  exit 1, naming the function that diverged (or is missing here).
#   SKIP:  exit 77 (the reference submodule is not checked out).
#   SELF:  scripts/tas_clip_parity.sh --self-test - inject drift into one body and
#          require the compare to CATCH it.
#
# WHY. `[MEASURED 2026-09-15]` a re-measure against the pin found our tas_clip had
# been ported from a snapshot EARLIER than edca8915 and was silently BEHIND on
# readLocked/setLocked (finalized-macro protection), labTrashTest (safe delete),
# and the seed() "main node from birth" groundwork. scripts/enginediff.sh guards
# only copyLiveSet's extension set; this guards the rest of the module that is
# meant to track his verbatim. A whole-file line diff is the wrong tool - our
# functions sit at different LINE POSITIONS than his (clipMutex ordering), so a
# line diff reads moved-but-identical code as changed. This compares each function
# BODY, order-independent.
#
# NOTE: copyLiveSet is DELIBERATELY divergent here (extracted from archive() with
# a longer doc comment - the one sanctioned difference, already checked by
# enginediff.sh), so it is NOT in the list below.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
OURS="$ROOT/core/dojo/tas_clip.cpp"
HIS="$ROOT/reference/flycast-rr/core/dojo/tas_clip.cpp"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

[ -f "$HIS" ]  || { echo "tas_clip_parity: SKIP - the reference submodule is not checked out ($HIS)"; exit $SKIP; }
[ -f "$OURS" ] || { echo "tas_clip_parity: SKIP - no tas_clip.cpp ($OURS)"; exit $SKIP; }

# Extract a top-level function body: from the line that STARTS with the signature
# to the next line that is exactly "}" (top-level close). Normalise David->dev.
extract() { awk -v sig="$2" 'index($0,sig)==1{f=1} f{print} f&&/^}$/{exit}' "$1" | sed 's/David/dev/g'; }

FUNCS=("bool readLocked(" "bool setLocked(" "bool labTrashTest(" "bool seed(")
rc=0
n=0
for sig in "${FUNCS[@]}"; do
	h="$(extract "$HIS" "$sig")"
	o="$(extract "$OURS" "$sig")"
	if [ -z "$h" ]; then echo "  skip  '$sig' absent in the reference (not a parity target)"; continue; fi
	n=$((n + 1))
	if [ -z "$o" ]; then echo "  FAIL  '$sig' is MISSING in ours (behind the pin)"; rc=1; continue; fi
	# --self-test: corrupt exactly one body so the compare MUST fail.
	if [ "$SELF" -eq 1 ] && [ "$sig" = "bool readLocked(" ]; then
		o="$o
	// SABOTAGE: injected drift"
	fi
	if [ "$o" = "$h" ]; then
		echo "  ok    '$sig' matches the pin"
	else
		echo "  FAIL  '$sig' DIVERGED from the pin:"
		diff <(printf '%s\n' "$h") <(printf '%s\n' "$o") | sed 's/^/      /'
		rc=1
	fi
done

# NON-VACUITY: the extractor must have found real targets, or a broken awk would
# make two empty strings "match" and pass silently.
if [ "$n" -lt 3 ]; then
	echo "tas_clip_parity: FAIL - only $n parity targets found; the extractor is broken, not the code"
	exit 1
fi

if [ "$SELF" -eq 1 ]; then
	if [ "$rc" -ne 0 ]; then
		echo "tas_clip_parity: PASS (self-test) - the injected drift was CAUGHT"
		exit 0
	fi
	echo "tas_clip_parity: FAIL (self-test) - the injected drift was NOT caught"
	exit 1
fi
[ "$rc" -eq 0 ] && echo "tas_clip_parity: PASS - $n ported functions byte-identical to the pin (modulo David->dev)"
exit $rc
