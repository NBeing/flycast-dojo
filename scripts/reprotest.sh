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
# COLD BOOT. docs/STATE-COVERAGE.md asks for a cold-boot-twice probe because
# that is what caught nbneo's init-residue bugs (a CPU zeroed once per PROCESS
# rather than per game-init, and sprite fields saved into states but never
# cleared at init).
#
# WHAT THIS SCRIPT CAN AND CANNOT ANSWER. Init residue is state a PROCESS
# carries into a second game-init. Two separate processes each initialise once,
# carry identical residue, and therefore AGREE - the effect cancels in any
# cross-process comparison, including --cold. So --cold measures cross-process
# boot reproducibility, which is worth having and was untested, but it is NOT
# the residue probe. That one has to boot twice in one process:
# scripts/tests/coldboot_pair.lua, via lua emulator.restartLater().
#
# TWO CORRECTIONS ARE RECORDED HERE RATHER THAN QUIETLY EDITED OUT, because both
# wrong versions looked measured:
#
#   1. "An in-process restart wedges." It did - stopGame() from a vblank
#      callback joins the emulation thread it runs on. That is FIXED
#      (emulator.restartLater posts to the deferred point); see
#      docs/STATE-COVERAGE.md §8.
#
#   2. "--cold cannot run here: no BIOS, and the HLE boot stalls." WRONG. The
#      run was never un-paused: a replay boots PAUSED and auto-play fires only
#      when AutoPlay, AutoSeekState >= 0 or AutoCapture is set, so passing
#      AutoSeekState=-1 removed the only trigger. `[MEASURED 2026-09-08]` with
#      AutoPlay=yes and no BIOS at all, a cold boot emulates normally: 800
#      frames sampled, movie index advancing 61 -> 661. Paused and wedged look
#      identical from outside, which is the hazard mainui.cpp already documents.

set -euo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
TEST="$ROOT/scripts/tests/repro/hash_sequence.lua"
OUT="${FLYCAST_TEST_OUT:-$ROOT/build-dojo7/testresults}"
# Derived from $TEST, not hardcoded: --oracle swaps the script, and a fixed
# name made one_run look for a log that run had never written - reported as
# "no lua log", which reads like the emulator failed rather than like the
# harness looking in the wrong place.
luaLogFor() { echo "$OUT/$(basename "$1" .lua).lua.log"; }
SKIP=77

RUNS=2; COLD=0; SELFTEST=0; TIMEOUT=180; ORACLE=""
while [ $# -gt 0 ]; do
	case "$1" in
		--cold)       COLD=1; shift ;;
		--from-state) COLD=0; shift ;;
		--self-test)  SELFTEST=1; shift ;;
		--oracle)     ORACLE="$2"; shift 2 ;;
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
	# AutoPlay=yes IS REQUIRED, not decoration. A replay boots PAUSED and the
	# auto-play block fires only when AutoPlay, AutoSeekState >= 0 or AutoCapture
	# is set (mainui.cpp). Removing the seek therefore removes the only thing
	# un-pausing the run, and the emulator sits at frame 0 emulating nothing.
	# `[MEASURED 2026-09-08]` that is exactly what happened, and it was
	# misdiagnosed here as "the HLE boot stalls" - because paused and wedged look
	# identical from outside, which is the hazard mainui.cpp already warns about.
	[ "$COLD" -eq 1 ] && extra=(TESTRUN_EXTRA_CONFIG="-config dojo:AutoSeekState=-1 -config dojo:AutoPlay=yes")
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
	local LOG; LOG="$(luaLogFor "$TEST")"
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
	# `[MEASURED 2026-09-10]` THIS SKIPS INTERMITTENTLY. One suite run in seven
	# reported "run1: 0 samples, run2: 12 samples" and skipped; three immediate
	# re-runs passed. The run simply did not reach its sampling point inside
	# --timeout, which under load it sometimes will not.
	#
	# Left as a SKIP rather than retried: a retry would make an intermittently
	# broken run indistinguishable from a slow one, and this harness exists to
	# tell differences apart. What was added instead is scripts/checks.sh, which
	# refuses to report a suite green when anything skipped - without it this
	# came out of ctest as "100% tests passed" and the coverage hole was
	# invisible. That gate caught this on the day it was written.
	# The Lua console indents every line it writes, so anchoring on ^REPRO
	# silently matched nothing and the run looked empty. Extract the record
	# itself rather than assuming a column.
	# Both record formats: REPRO (hash_sequence, keyed by guest frame) and OR
	# (oracle_probe, unkeyed because the two forks share no clock). One
	# extraction so --oracle and the normal modes cannot drift apart.
	grep -aoE 'REPRO [0-9]+ [0-9]+|OR [0-9]+|OR-MOVED (true|false)' "$LOG" > "$work/$label.seq" || true
	cp "$LOG" "$work/$label.lua.log"
	echo "  $label: $(wc -l < "$work/$label.seq") samples"
}

# oracle_run_b <binary> <outfile>: launch the OTHER fork and un-pause it.
oracle_run_b() {
	# `set +e` FOR THE WHOLE FUNCTION. This is orchestration - a grep that finds
	# nothing yet, a kill for a process already gone, a glob that matches no
	# sibling - and under `set -e` each of those aborts the SCRIPT rather than
	# taking the branch written for it. Three separate instances were fixed one
	# at a time before it was clear the option itself was the wrong tool here;
	# every one presented identically, as the run stopping with no message and
	# exit 0. The function checks its own statuses explicitly instead.
	set +e
	local bin="$1" out="$2"
	echo "  launching B (no auto-play: it needs a keypress)"
	local w="$work/b"; rm -rf "$w"; mkdir -p "$w/config/flycast-dojo" "$w/clip"
	cp "$CLIP" "$w/clip/clip.flyr"
	# `|| true` is load-bearing under `set -e`: when the LAST glob matches
	# nothing, `[ -f ]` returns 1 and the for loop's final status aborts the
	# whole script. testrun.sh has the identical loop and is not under `set -e`,
	# so copying it here changed its meaning. The symptom was the run stopping
	# after the A side with no message and exit 0.
	for sib in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* \
	           "$(dirname "$CLIP")"/clip.json; do
		{ [ -f "$sib" ] && cp "$sib" "$w/clip/" 2>/dev/null; } || true
	done
	cp "$TEST" "$w/config/flycast-dojo/flycast.lua"

	local disp=":$((BASE_DISPLAY + 40))"
	nohup Xvfb "$disp" -screen 0 800x600x24 >"$w/xvfb.log" 2>&1 & local xpid=$!
	sleep 2
	# i3, because a bare Xvfb has no focus owner and SDL never receives the key.
	printf 'default_border none\nfor_window [class=".*"] floating enable\n' > "$w/i3.conf"
	DISPLAY="$disp" nohup i3 -c "$w/i3.conf" >"$w/i3.log" 2>&1 & local ipid=$!
	sleep 2

	# B starts counting only once un-paused, which is already past its seek.
	DISPLAY="$disp" XDG_CONFIG_HOME="$w/config" ORACLE_SETTLE=30 setsid "$bin" \
		-config dojo:Replay=yes -config "dojo:ReplayFilename=$w/clip/clip.flyr" \
		-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
		-config dojo:Transmitting=no -config dojo:Receiving=no \
		"$ROM" >"$w/stdout.log" 2>&1 & local bpid=$!

	# Wait for the seek to land, then send P. Watching the log rather than
	# sleeping a fixed time: the boot is slower under software rendering and a
	# key sent too early is simply lost.
	local i
	# `|| true` on the loop body: a `cmd && break` that never fires leaves the
	# LAST iteration returning 1, and under `set -e` that aborts the script
	# instead of falling through to the timeout branch. Third instance of this
	# shape in this file - see the sibling-copy loop above.
	local seen=0
	for i in $(seq 1 60); do
		sleep 1
		if grep -aq "TAS READY\|replay seek to movie frame" "$w/stdout.log" 2>/dev/null; then
			seen=1; break
		fi
	done
	[ "$seen" -eq 1 ] || echo "  (B never logged its seek; sending the key anyway)"
	local wid
	wid=$(DISPLAY="$disp" xdotool search --name "Flycast" 2>/dev/null | head -1)
	if [ -n "$wid" ]; then
		DISPLAY="$disp" xdotool windowactivate "$wid" 2>/dev/null; sleep 1
		DISPLAY="$disp" xdotool key --window "$wid" p
	else
		echo "reprotest:   (no window found; B will stay paused)" >&2
	fi

	for i in $(seq 1 "$TIMEOUT"); do
		sleep 1
		if grep -aq "OR-DONE" "$w/stdout.log" 2>/dev/null; then break; fi
	done
	# `|| true` on every kill: under `set -e` a kill that finds nothing already
	# gone aborts the whole script, which showed up as the run simply stopping
	# after the A side with no error and exit 0.
	#
	# pkill -P rather than `kill -- -$bpid`: $! is setsid's PARENT, which exits
	# immediately, so the negated pid is not the new group and the signal can
	# land on our own group instead.
	pkill -P "$bpid" 2>/dev/null || true
	kill "$bpid" 2>/dev/null || true
	kill "$ipid" 2>/dev/null || true
	kill "$xpid" 2>/dev/null || true
	tr -d '\0' < "$w/stdout.log" | grep -aoE "OR [0-9]+|OR-MOVED (true|false)" > "$out" || true
	echo "  oracleB: $(wc -l < "$out") samples"
	set -e
}

# oracle_correlate <A.seq> <B.seq>: the two forks share no clock, so find the
# OFFSET at which the sequences agree instead of assuming they start together.
oracle_correlate() {
	awk '
		function best() {
			bestn = -1; besto = 0
			for (off = -maxoff; off <= maxoff; off++) {
				m = 0; n = 0
				for (i = 1; i <= na; i++) {
					j = i + off
					if (j < 1 || j > nb) continue
					n++
					if (a[i] == b[j]) m++
				}
				if (n >= 20 && m > bestn) { bestn = m; besto = off; bestden = n }
			}
		}
		FNR == NR { if ($1 == "OR") a[++na] = $2; next }
		           { if ($1 == "OR") b[++nb] = $2 }
		END {
			maxoff = (na < nb ? na : nb) - 20
			if (maxoff < 1) maxoff = 1
			best()
			printf "  best alignment: offset %d, %d/%d samples identical\n", besto, bestn, bestden
			# THE SHAPE OF THE MISMATCH IS THE FINDING, not the count. Matches
			# in one contiguous run mean the two forks agree and then diverge at
			# a locatable frame; matches scattered through the window mean the
			# fingerprint is aliasing and the comparison is not measuring what it
			# claims to.
			run = 0; bestrun = 0; firstbad = -1; k = 0
			for (i = 1; i <= na; i++) {
				j = i + besto
				if (j < 1 || j > nb) continue
				k++
				if (a[i] == b[j]) { run++; if (run > bestrun) bestrun = run }
				else { run = 0; if (firstbad < 0) firstbad = k }
			}
			printf "  longest identical run: %d   first difference at sample %d of %d\n", bestrun, firstbad, k
			# DISTINCT VALUES INSIDE THE MATCHED RUN. The per-side vacuity gate
			# only asks whether a sequence moved AT ALL; a sequence that moves
			# once (at the seek) passes it while the matched region is a single
			# repeated constant - which is 168 frames of identical DEAD memory
			# masquerading as 168 frames of identical emulation.
			run = 0; runend = 0; k = 0
			for (i = 1; i <= na; i++) {
				j = i + besto
				if (j < 1 || j > nb) continue
				k++
				if (a[i] == b[j]) { run++; if (run == bestrun) runend = k } else run = 0
			}
			delete seen; distinct = 0; k = 0
			for (i = 1; i <= na; i++) {
				j = i + besto
				if (j < 1 || j > nb) continue
				k++
				if (k > runend - bestrun && k <= runend && !(a[i] in seen)) {
					seen[a[i]] = 1; distinct++
				}
			}
			printf "  distinct values inside that run: %d of %d samples\n", distinct, bestrun
			if (distinct <= 1) {
				print "BAD  the matched run is ONE repeated value - identical dead memory, not identical emulation"
				exit 1
			}
			if (bestn == bestden) {
				print "ok   the two forks emulate identically (at that offset)"
				exit 0
			}
			printf "BAD  %d of %d samples differ at the best offset\n", bestden - bestn, bestden
			exit 1
		}
	' "$1" "$2"
}

# NO BIOS PRECONDITION. An earlier version of this script refused --cold unless
# a dc_boot.bin was present, on the measured-looking claim that the HLE fallback
# stalled on this title. That claim was wrong: the run was never un-paused (see
# AutoPlay above), and "paused" reads exactly like "stalled" from outside.
# `[MEASURED 2026-09-08]` with AutoPlay set, a cold boot with NO BIOS emulates
# normally - 800 frames sampled, movie index advancing 61 -> 661.
#
# Kept as a comment rather than deleted, because the wrong version of this gate
# would have made --cold skip forever on a perfectly capable machine, and
# reported that as a fact about the machine.

# ---------------------------------------------------------------------------
# --oracle <other-flycast>: is a SECOND FORK's emulation the same as ours?
#
# Every other mode here compares our binary against itself. This compares it
# against a different tree - David's flycast-rr - on the same clip and the same
# savestate, which are interchangeable between the forks
# (`[MEASURED 2026-09-09]` his build loads ours and passes its own STATE VERIFY).
#
# THE TWO FORKS SHARE NO CLOCK. His Lua has no frame/savestate/movie namespace
# at all (822 lines to our 2572), so there is no frame number both sides can
# report and no way to say "sample N of each is the same moment". The sequences
# are therefore aligned by CROSS-CORRELATION: find the offset at which they
# agree, and report it. An offset is expected and is not a fault - the two
# builds start emulating at different points relative to the seek.
#
# HIS BUILD HAS NO AUTO-PLAY. That block is ours (mainui.cpp); his replay boots
# PAUSED waiting for a human, so headless it seeks and then emulates nothing
# forever. The keypress is supplied with xdotool, and a bare Xvfb swallows keys
# (scripts/docktest.sh records the same finding), so the display gets a
# config-less i3 first.
if [ -n "$ORACLE" ]; then
	[ -x "$ORACLE" ] || { echo "reprotest: SKIP - no such binary: $ORACLE" >&2; exit $SKIP; }
	command -v xdotool >/dev/null || { echo "reprotest: SKIP - no xdotool" >&2; exit $SKIP; }
	command -v i3 >/dev/null      || { echo "reprotest: SKIP - no i3 (bare Xvfb swallows keys)" >&2; exit $SKIP; }
	TEST="$ROOT/scripts/tests/repro/oracle_probe.lua"

	# Oracle mode launches the second fork DIRECTLY, so it needs the fixtures
	# testrun.sh normally resolves on our behalf. Same rules as testrun.sh:
	# prefer a clip that has a savestate beside it, and use flyrframes.sh for
	# the length rather than arithmetic on the file size.
	ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
	BASE_DISPLAY="${FLYCAST_TEST_DISPLAY_BASE:-90}"
	[ -f "$ROM" ] || { echo "reprotest: SKIP - no ROM at $ROM" >&2; exit $SKIP; }
	CLIP=""
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		frames=$("$ROOT/scripts/flyrframes.sh" "$f" 2>/dev/null) || continue
		[ "$frames" -ge 600 ] || continue
		ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
		CLIP="$f"; break
	done
	[ -n "$CLIP" ] || { echo "reprotest: SKIP - no clip with a savestate" >&2; exit $SKIP; }
	echo "  clip = $(basename "$(dirname "$CLIP")")"

	echo "reprotest: oracle mode"
	echo "  A = ${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
	echo "  B = $ORACLE"

	# A is ours: auto-play handles the un-pause, but its vblanks start at BOOT
	# and the seek to the clip's frame only lands around vblank 300 - so it must
	# skip past that or it samples attract mode against David's gameplay.
	ORACLE_SETTLE=380 one_run oursA
	# B is the other fork: launched by hand so it can be un-paused.
	oracle_run_b "$ORACLE" "$work/oracleB.seq"

	a=$(grep -c '^OR ' "$work/oursA.seq" 2>/dev/null || echo 0)
	b=$(grep -c '^OR ' "$work/oracleB.seq" 2>/dev/null || echo 0)
	echo "  A: $a samples   B: $b samples"
	[ "$a" -gt 0 ] && [ "$b" -gt 0 ] || {
		echo "reprotest: SKIP - a side produced no samples" >&2; exit $SKIP; }

	# DISCRIMINATING POWER, not a moved/didn't-move flag. A fingerprint of a
	# RUNNING machine should be close to unique per frame; one that takes only a
	# handful of values produces long ACCIDENTAL matching runs, and a binary
	# "did it move at all" gate waves those through.
	#
	# `[MEASURED 2026-09-09]` sequences of 400 samples holding 24 and 19 distinct
	# values passed the old gate and yielded a 168-sample "identical run" that
	# was two IDLE machines coinciding. The old gate is why that took four probe
	# designs to notice; this one would have rejected all four immediately.
	for side in oursA oracleB; do
		tot=$(grep -c '^OR ' "$work/$side.seq" 2>/dev/null || echo 0)
		uniq=$(grep '^OR ' "$work/$side.seq" 2>/dev/null | awk '{print $2}' | sort -u | wc -l)
		echo "  $side: $uniq distinct of $tot samples"
		# A tenth is generous: a live machine is near 1.0, dead memory near 0.
		if [ "$tot" -gt 0 ] && [ $((uniq * 10)) -lt "$tot" ]; then
			echo "reprotest: SKIP - $side's fingerprint takes only $uniq values over $tot" >&2
			echo "reprotest:   samples. That is not a running machine's memory; long" >&2
			echo "reprotest:   accidental matches would be reported as agreement." >&2
			exit $SKIP
		fi
	done

	# KEEP THE SEQUENCES. $work is a mktemp wiped on exit, so every question
	# about a result ("was the matched region actually moving?") cost another
	# five-minute pair of emulator runs. They are small.
	mkdir -p "$OUT"
	cp "$work/oursA.seq"   "$OUT/oracle_A.seq" 2>/dev/null || true
	cp "$work/oracleB.seq" "$OUT/oracle_B.seq" 2>/dev/null || true
	echo "  sequences kept: $OUT/oracle_{A,B}.seq"
	oracle_correlate "$work/oursA.seq" "$work/oracleB.seq"
	exit $?
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
