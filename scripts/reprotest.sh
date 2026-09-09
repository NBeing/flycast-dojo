#!/usr/bin/env bash
# reprotest - is the emulator reproducible ACROSS PROCESSES?
#
# Every other test in this tree runs in one process, so it can only see
# nondeterminism that a single process produces. Anything process-level - ASLR,
# thread scheduling, host state carried in at startup - is invisible to all of
# them, because both halves of the comparison share it. This runs the same
# deterministic input in two separate processes and requires the same hashes.
#
# That matters for the project's own claim. CLAUDE.md calls playing from frame 0
# "the bulletproof/cross-machine mode"; cross-PROCESS agreement is the weakest
# form of that claim, and it had never been measured.
#
# MODES
#   (default)  --from-state   both runs seek to the clip's savestate first.
#                             This is what the rest of the suite does, and it
#                             WORKS here.
#   --cold                    no seek: boot from power-on, the way nbneo's
#                             cold-boot-twice probe does. See COLD BOOT below -
#                             it SKIPs on this machine, for a measured reason.
#   --self-test               the sabotage arm: a third run pokes one word of
#                             guest RAM and MUST disagree with the first two.
#
# COLD BOOT, and what this probe still cannot do. [MEASURED 2026-09-08]
# docs/STATE-COVERAGE.md asks for a cold-boot-twice probe because that is what
# caught nbneo's init-residue bugs (a CPU zeroed once per PROCESS rather than
# per game-init, and sprite fields saved into states but never cleared at init).
# Two separate blockers were measured while building this:
#
#   1. The variant that would catch that class must boot twice IN ONE PROCESS,
#      because two fresh processes each initialise once and therefore agree -
#      the residue is identical on both sides and cancels. Restarting in-process
#      needs emulator.stopGame() from a Lua callback, and that WEDGES: the call
#      never returns and the run times out. It is the same wall that stopped
#      savestate.loadLater (core/lua/lua.cpp, "NO savestate.loadLater HERE").
#
#   2. --cold cannot run here at all: there is no BIOS on this machine
#      ("Did not load BIOS, using reios") and the HLE boot of this title stalls
#      - the emulator's own log stops 0.4s in, at "REIOS: Booting up", and no
#      further frame is emulated in ten minutes.
#
# So --cold SKIPs rather than fails, and even when a BIOS makes it run it will
# test cross-process boot reproducibility, NOT init residue. The strong variant
# needs an in-process restart that does not wedge. Recorded rather than faked.
set -euo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
TEST="$ROOT/scripts/tests/repro/hash_sequence.lua"
OUT="${FLYCAST_TEST_OUT:-$ROOT/build-dojo7/testresults}"
LOG="$OUT/hash_sequence.lua.log"
SKIP=77

RUNS=2; COLD=0; SELFTEST=0; TIMEOUT=180
while [ $# -gt 0 ]; do
	case "$1" in
		--cold)       COLD=1; shift ;;
		--from-state) COLD=0; shift ;;
		--self-test)  SELFTEST=1; shift ;;
		--runs)       RUNS="$2"; shift 2 ;;
		--timeout)    TIMEOUT="$2"; shift 2 ;;
		*) echo "reprotest: unknown argument $1" >&2; exit 2 ;;
	esac
done

# ONE RUN CANNOT ANSWER A REPRODUCIBILITY QUESTION. Without this, --runs 1
# walks past an empty comparison loop and prints "reproducible across
# processes" having compared nothing - a pass that means the opposite of what
# it says. Refused as a usage error rather than skipped, because it is a
# mistake in the invocation, not a missing prerequisite.
if [ "$RUNS" -lt 2 ]; then
	echo "reprotest: --runs must be at least 2; one run compares nothing" >&2
	exit 2
fi

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# one_run <label> [extra env assignments...]
one_run() {
	local label="$1"; shift
	local extra=()
	[ "$COLD" -eq 1 ] && extra=(TESTRUN_EXTRA_CONFIG="-config dojo:AutoSeekState=-1")
	# testrun.sh owns display allocation, clip staging, teardown and the SKIP
	# codes. Reusing it means this harness cannot drift from those.
	# `|| rc=$?`, NOT `if ! cmd; then rc=$?`. Inside the then-branch of `if !`,
	# $? is the status of the NEGATION (0), not of the command - so the skip
	# passthrough below was unreachable and a testrun SKIP would have been
	# swallowed into a confusing no-samples failure instead.
	# DELETE THE LOG FIRST. $LOG is a fixed path that testrun.sh overwrites, so
	# a run that produces none leaves the PREVIOUS run's log in place - and two
	# reads of one file are identical, which is this harness reporting perfect
	# reproducibility from a run that never happened. The no-samples gate below
	# cannot catch that: the stale file has samples.
	rm -f "$LOG"
	local rc=0
	env "${extra[@]}" "$@" "$ROOT/scripts/testrun.sh" --timeout "$TIMEOUT" "$TEST" \
		>"$work/$label.out" 2>&1 || rc=$?
	if [ "$rc" -eq "$SKIP" ]; then
		echo "reprotest: SKIP - testrun could not run ($label)" >&2
		sed -n 's/^testrun: /reprotest:   /p' "$work/$label.out" >&2
		exit $SKIP
	fi
	[ -f "$LOG" ] || { echo "reprotest: SKIP - no lua log from $label" >&2; exit $SKIP; }
	# The Lua console indents every line it writes, so anchoring on ^REPRO
	# silently matched nothing and the run looked empty. Extract the record
	# itself rather than assuming a column.
	grep -ao 'REPRO [0-9]\{1,\} [0-9]\{1,\}' "$LOG" > "$work/$label.seq" || true
	cp "$LOG" "$work/$label.lua.log"
	echo "  $label: $(wc -l < "$work/$label.seq") samples"
}

# COLD needs a real BIOS. Checked FIRST because the alternative is finding out
# by booting: the HLE path stalls, and every cold run then burns its whole
# timeout before the no-samples gate can skip it. This costs milliseconds.
# nvmem.cpp:261 loads "%boot.bin;%boot.bin.bin;%bios.bin;%bios.bin.bin" with the
# platform prefix, which is dc_ for Dreamcast.
#
# Conservative on purpose: if this misses a BIOS that flycast would have found,
# the run proceeds and the no-samples gate still skips correctly - just slowly.
# It can make the skip cheap; it cannot make a broken run look like a pass.
if [ "$COLD" -eq 1 ]; then
	found=""
	for d in "${XDG_DATA_HOME:-$HOME/.local/share}/flycast-dojo" \
	         "${XDG_DATA_HOME:-$HOME/.local/share}/flycast" \
	         "$ROOT/data" "$ROOT/build-dojo7/data"; do
		for f in dc_boot.bin dc_bios.bin dc_boot.bin.bin dc_bios.bin.bin; do
			[ -f "$d/$f" ] && found="$d/$f"
		done
	done
	if [ -z "$found" ]; then
		echo "reprotest: SKIP - no Dreamcast BIOS found; a cold boot needs one" >&2
		echo "reprotest:   looked for dc_boot.bin / dc_bios.bin in the data dirs" >&2
		echo "reprotest:   the HLE fallback (reios) stalls on this title - see COLD BOOT above" >&2
		exit $SKIP
	fi
	echo "reprotest: BIOS $found"
fi

echo "reprotest: mode=$([ "$COLD" -eq 1 ] && echo cold || echo from-state) runs=$RUNS"
for i in $(seq 1 "$RUNS"); do one_run "run$i"; done

# A run that produced no samples cannot answer the question. This is the gate
# that stops "two empty files are identical" from reading as a pass - the exact
# shape of vacuity the tree has been bitten by before.
for i in $(seq 1 "$RUNS"); do
	n=$(wc -l < "$work/run$i.seq")
	if [ "$n" -eq 0 ]; then
		echo "reprotest: SKIP - run$i produced no samples" >&2
		if [ "$COLD" -eq 1 ]; then
			echo "reprotest:   cold boot did not reach the sampling frame - see COLD BOOT in this script" >&2
		fi
		exit $SKIP
	fi
done

rc=0
for i in $(seq 2 "$RUNS"); do
	if diff -q "$work/run1.seq" "$work/run$i.seq" >/dev/null; then
		echo "ok   run1 == run$i  ($(wc -l < "$work/run1.seq") frames of hashes, identical)"
	else
		echo "BAD  run1 != run$i"; diff "$work/run1.seq" "$work/run$i.seq" | head -8; rc=1
	fi
done

if [ "$SELFTEST" -eq 1 ]; then
	# The sabotage arm. Without it, "the two runs agreed" is unfalsifiable: a
	# comparison that cannot report a difference agrees with everything.
	one_run poke FLYCAST_REPRO_POKE=1
	if [ "$(wc -l < "$work/poke.seq")" -eq 0 ]; then
		echo "reprotest: SKIP - the poke run produced no samples" >&2; exit $SKIP
	fi
	if diff -q "$work/run1.seq" "$work/poke.seq" >/dev/null; then
		echo "BAD  the poked run MATCHED - the comparison cannot detect a difference"; rc=1
	else
		echo "ok   the poked run differs - the comparison can fail"
	fi
fi

[ "$rc" -eq 0 ] && echo "reprotest: reproducible across processes"
exit $rc
