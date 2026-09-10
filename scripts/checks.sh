#!/usr/bin/env bash
# checks - run the whole suite and refuse to be green over a SKIP.
#
#   RUN:   scripts/checks.sh [ctest args...]
#   PASS:  every test ran and passed.
#   FAIL:  exit 1 on a failure, exit 2 on a skip (nothing failed, but something
#          did not run - which is a different thing and must not read as green).
#   SELF:  scripts/checks.sh --self-test - four canned ctest summaries the judge
#          must sort correctly, including a clean one it must ACCEPT.
#
# WHY. `[MEASURED 2026-09-10]` ctest printed
#
#     100% tests passed, 0 tests failed out of 15
#     The following tests did not run:
#           4 - flycast.hotkeytest (Skipped)
#
# and the exit status was 0. flycast.hotkeytest had been skipping for its entire
# existence: its Xvfb never started, because CMakeLists passes a BARE display
# number and the script expected a leading colon. The suite said "100% passed"
# every time.
#
# `SKIP_RETURN_CODE 77` is right and stays - a machine with no ROM, no Xvfb or
# no clip genuinely cannot run those, and reporting that as a failure would
# train everyone to ignore it. What is wrong is the SUMMARY: CLAUDE.md doctrine
# rule 5, "a skipped check is not a passing one, and must not report as one".
# shell/linux/integration-tests already exits 2 for exactly this; this is that
# rule applied to the whole suite instead of one script.
set -uo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
BUILD="${FLYCAST_BUILD:-$ROOT/build-dojo7}"

# THE JUDGE, as a function taking a file, so --self-test can feed it summaries
# this machine did not produce. A judge you cannot feed is a judge you cannot
# prove wrong.
judge() {	# $1 = ctest output -> prints a verdict, returns 0 pass / 1 fail / 2 skip
	local out="$1" failed skipped total
	total=$(grep -aoE "out of [0-9]+" "$out" | tail -1 | grep -aoE "[0-9]+")
	failed=$(grep -aoE "[0-9]+ tests failed" "$out" | tail -1 | grep -aoE "^[0-9]+")
	skipped=$(grep -acE "^[[:space:]]+[0-9]+ - .*\(Skipped\)" "$out")

	# NON-VACUITY. No summary line at all means ctest did not run to completion -
	# a build error, a crash, a kill - and "no failures were reported" is then
	# true and meaningless.
	if [ -z "${total:-}" ]; then
		echo "NO SUMMARY - ctest did not finish; there is no result to report"
		return 1
	fi
	if [ "${failed:-0}" -gt 0 ]; then
		echo "FAILED - $failed of $total"
		grep -aE "\(Failed\)|\(Not Run\)" "$out" | sed 's/^/    /'
		return 1
	fi
	if [ "$skipped" -gt 0 ]; then
		echo "SKIPPED - $skipped of $total did not run:"
		grep -aE "^[[:space:]]+[0-9]+ - .*\(Skipped\)" "$out" | sed 's/^/    /'
		echo "  Nothing failed, but the suite did not cover what it claims to."
		return 2
	fi
	echo "ALL $total RAN AND PASSED"
	return 0
}

if [ "${1:-}" = "--self-test" ]; then
	tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
	rc=0
	printf '100%%%% tests passed, 0 tests failed out of 15\n' > "$tmp/clean.log"
	{ echo "97% tests passed, 1 tests failed out of 15"
	  echo "	  4 - flycast.hotkeytest (Failed)"; } > "$tmp/failed.log"
	{ echo "100% tests passed, 0 tests failed out of 15"
	  echo "The following tests did not run:"
	  echo "	  4 - flycast.hotkeytest (Skipped)"; } > "$tmp/skipped.log"
	echo "ctest: error: build directory not found" > "$tmp/nosummary.log"

	check() {	# file, wanted-rc, label
		local out; out="$(judge "$tmp/$1")"; local got=$?
		if [ "$got" -eq "$2" ]; then
			echo "  ok - $3"
		else
			echo "FAIL checks --self-test - $3: wanted rc=$2, got rc=$got"
			echo "$out" | sed 's/^/      /'
			rc=1
		fi
	}
	check failed.log     1 "a failure is a failure"
	# THE ONE THIS SCRIPT EXISTS FOR: ctest's own summary says 100% passed.
	check skipped.log    2 "a skip is NOT a pass, even when ctest says 100%"
	check nosummary.log  1 "no summary at all is a failure, not a quiet pass"
	# THE CONTROL. Without it, a judge that returns non-zero unconditionally
	# satisfies all three arms above.
	check clean.log      0 "a genuinely clean run is still accepted"
	[ $rc -eq 0 ] && echo "PASS checks --self-test - the judge sorts pass, fail, skip and nothing"
	exit $rc
fi

log="$(mktemp)"; trap 'rm -f "$log"' EXIT
ctest --test-dir "$BUILD" --output-on-failure "$@" 2>&1 | tee "$log"
echo
echo "---- checks ----"
judge "$log"
exit $?
