#!/usr/bin/env bash
# enginediff - our TAS engine and David's, run over the same operation, diffed.
#
#   RUN:   scripts/enginediff.sh
#   PASS:  (1) our copyLiveSet and his copy the IDENTICAL file set (a compiled
#          harness runs both loop bodies over a fixture), and (2) the live-set
#          EXTENSION SET in our pinned source and his match exactly.
#   FAIL:  exit 1, naming the file or extension that diverged.
#   SELF:  scripts/enginediff.sh --self-test - inject a dropped extension into
#          each half and require the divergence to be CAUGHT.
#
# WHY. `[MEASURED 2026-09-15]` the branch engine (tas_branch.cpp) and oslib are
# byte-identical to reference/flycast-rr, so their parity is free. The single
# behavioral divergence is tas_clip::copyLiveSet - "which files make up a clip" -
# and it is exactly the fact that drifts silently when one fork gains a sidecar
# the other lacks. This is the differential method (run both, diff) aimed where
# parity is INTENDED, as opposed to the roll_* modules that deliberately differ.
#
# Two independent checks, because each covers what the other cannot:
#   - the COMPILED HARNESS runs both loops and proves they BEHAVE the same
#     (directory-skip, case sensitivity, byte-for-byte copy) - but its `theirs()`
#     is a transcription that could go stale.
#   - the SOURCE-PARITY check reads BOTH pinned .cpp files and proves their
#     extension SETS are equal - rot-proof against his fork, and it is what keeps
#     the transcription honest.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
OURS="$ROOT/core/dojo/tas_clip.cpp"
HIS="$ROOT/reference/flycast-rr/core/dojo/tas_clip.cpp"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

command -v g++ >/dev/null || { echo "enginediff: SKIP - no g++" >&2; exit $SKIP; }
[ -f "$HIS" ] || { echo "enginediff: SKIP - the reference submodule is not checked out ($HIS)" >&2; exit $SKIP; }

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
bin="$work/engine_diff"
g++ -std=c++17 -O0 -o "$bin" "$ROOT/scripts/tests/engine_diff.cpp" || {
	echo "enginediff: FAIL - the harness did not compile"; exit 1; }

rc=0

# --- 1. the behavioral differential -------------------------------------------------------
if [ "$SELF" -eq 1 ]; then
	# The harness's --self-test drops .map from theirs(); it must report the
	# divergence (exit nonzero). If it exits 0, the comparison cannot fail.
	if "$bin" --self-test >"$work/beh.log" 2>&1; then
		echo "FAIL - the harness accepted a divergent theirs() (the diff cannot fail)"; rc=1
	else
		echo "ok   the harness CATCHES an injected extension drop"
	fi
	grep -a "IDENTICAL file set" "$work/beh.log" | sed 's/^/    /'
else
	if "$bin" >"$work/beh.log" 2>&1; then
		echo "ok   our copyLiveSet and his copy the identical file set"
		grep -a "passed," "$work/beh.log" | sed 's/^/    /'
	else
		echo "FAIL - the two copyLiveSet bodies diverged over the fixture:"
		sed 's/^/    /' "$work/beh.log"; rc=1
	fi
fi

# --- 2. source parity: the extension sets in both pinned files ------------------------------
# Extract the ".xxx" string-literal tokens from OUR copyLiveSet and HIS
# isLiveSetExt. Read the FILE (never a pipeline into grep -q; pipefail turns a
# match into 141 by input size - see CLAUDE.md rule 1).
exts() {	# $1 = file, $2 = awk pattern that starts the function body
	awk "/$2/{f=1} f{print} /^}/{if(f)exit}" "$1" \
		| grep -aoE '"\.[a-zA-Z]+"' | tr -d '"' | sort -u
}
ours_set="$(exts "$OURS" 'int copyLiveSet')"
his_set="$(exts "$HIS" 'isLiveSetExt')"

if [ "$SELF" -eq 1 ]; then
	# Feed a deliberately short "his" set (drop .map) and require the compare to
	# flag it - the same defect the harness injects, checked at the source layer.
	his_bad="$(printf '%s\n' "$his_set" | grep -av '^\.map$')"
	if [ "$ours_set" = "$his_bad" ]; then
		echo "FAIL - source parity accepted a dropped extension"; rc=1
	else
		echo "ok   source parity CATCHES a dropped extension ($(comm -23 <(printf '%s' "$ours_set") <(printf '%s' "$his_bad") | tr '\n' ' '))"
	fi
else
	# NON-VACUITY: a parse that found nothing would make two empty sets "equal".
	n=$(printf '%s\n' "$ours_set" | grep -ac .)
	if [ "$n" -lt 8 ]; then
		echo "FAIL - parsed only $n extensions from ours; the extractor is broken, not the code"; rc=1
	elif [ "$ours_set" = "$his_set" ]; then
		echo "ok   the live-set extension sets match ($n extensions, both forks)"
	else
		echo "FAIL - the live-set extension sets DIVERGED:"
		echo "  only in ours:  $(comm -23 <(printf '%s' "$ours_set") <(printf '%s' "$his_set") | tr '\n' ' ')"
		echo "  only in his:   $(comm -13 <(printf '%s' "$ours_set") <(printf '%s' "$his_set") | tr '\n' ' ')"
		rc=1
	fi
fi

[ $rc -eq 0 ] && echo "enginediff: PASS${SELF:+ (self-test)}"
exit $rc
