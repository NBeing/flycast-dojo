#!/usr/bin/env bash
# testrun - run flycast Lua tests, one process each, in parallel, offscreen.
#
#   scripts/testrun.sh                        run every scripts/tests/*.lua
#   scripts/testrun.sh -j 4                   ... four at a time
#   scripts/testrun.sh scripts/tests/foo.lua  just this one
#   scripts/testrun.sh --rom /path/to.cdi     override the ROM
#   scripts/testrun.sh --self-test            prove the runner can report failure
#   TESTRUN_EXTRA_CONFIG="-config dojo:GamePanel=yes" scripts/testrun.sh --watch ...
#                                             extra -config flags, for trying a
#                                             feature without editing this file
#   scripts/testrun.sh --watch <test.lua>     run it on YOUR display, so you can
#                                             see it. Config is still sandboxed.
#   scripts/testrun.sh --watch --hold 60 ...  keep the window up 60s after the
#                                             verdict (default 30; 0 = close now;
#                                             -1 = leave it open until YOU close it)
#
# Exit 0 only if every test PASSed. Requires Xvfb, except under --watch.
#
# WHY EACH PIECE IS HERE. None of this is defensive habit; each line is a
# measured failure from this project or its siblings.
#
#  * ONE PROCESS PER TEST. In-process reuse across tests diverges - see
#    docs/SPIKE-machine-pool.md. A pool WITHIN one test is fine and fast
#    (restore ~8 ms vs seconds to boot); sharing a process BETWEEN tests is not.
#  * SANDBOXED XDG_CONFIG_HOME. find_user_config_dir() honours it
#    (core/linux-dist/main.cpp), so each test gets its own emu.cfg, its own
#    input mappings (deterministic bindings) and its own flycast-lua.log. This
#    replaces the cp-backup-restore of the user's flycast.lua that every earlier
#    harness did and that left the config mutated whenever a run died.
#  * SIGINT, NOT SIGTERM. core/linux/common.cpp does `signal(SIGINT, exit)`, so
#    INT flushes stdio; TERM discards the buffered tail.
#  * WHOLE-LINE marker matching. A line that merely CONTAINS "done" must not end
#    the run - that exact bug turned a truncated run into a false pass in
#    lemalta.
#  * NO VERDICT IS "INCONCLUSIVE", NOT PASS. A test that never ran exits
#    cleanly and says nothing; treating that as success is how a suite becomes
#    decorative.
#  * DEADLINE POLLING, never a fixed sleep. A sleep that cannot report failure
#    is a slow way to be wrong.
#  * EVIDENCE BEFORE THE KILL. The logs are copied out before the process group
#    is torn down; teardown takes the evidence with it otherwise.
set -uo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
BIN="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
CLIP="${FLYCAST_TEST_CLIP:-}"
OUT="${FLYCAST_TEST_OUT:-$ROOT/build-dojo7/testresults}"
JOBS=1
SELFTEST=0
WATCH=0
HOLD=30
TIMEOUT="${FLYCAST_TEST_TIMEOUT:-120}"
BASE_DISPLAY="${FLYCAST_TEST_DISPLAY_BASE:-90}"

TESTS=()
while [ $# -gt 0 ]; do
	case "$1" in
		-j) JOBS="$2"; shift 2 ;;
		--rom) ROM="$2"; shift 2 ;;
		--clip) CLIP="$2"; shift 2 ;;
		--timeout) TIMEOUT="$2"; shift 2 ;;
		--self-test) SELFTEST=1; shift ;;
		--watch) WATCH=1; shift ;;
		--hold) HOLD="$2"; shift 2 ;;
		-h|--help) sed -n '2,12p' "$0"; exit 0 ;;
		*) TESTS+=("$1"); shift ;;
	esac
done
# --self-test runs the deliberately-broken tests and checks the runner reports
# the RIGHT KIND of failure for each. A harness that has never reported a
# failure is not known to be able to.
if [ "$SELFTEST" -eq 1 ]; then
	TESTS=("$ROOT"/scripts/tests/negative/*.lua)
elif [ ${#TESTS[@]} -eq 0 ]; then
	TESTS=("$ROOT"/scripts/tests/*.lua)
fi

# 77 IS "COULD NOT RUN", AND IT IS NOT A FAILURE OR A PASS.
#
# ctest's SKIP_RETURN_CODE. A missing ROM or no Xvfb means this machine cannot
# answer the question - reporting that as a failure trains people to ignore red,
# and reporting it as a pass is the vacuous pass this whole harness exists to
# prevent. Three outcomes, because there are three.
SKIP=77
[ -x "$BIN" ] || { echo "testrun: SKIP - no binary at $BIN" >&2; exit $SKIP; }
[ -f "$ROM" ] || { echo "testrun: SKIP - no ROM at $ROM" >&2; exit $SKIP; }
if [ "$WATCH" -eq 1 ]; then
	# --watch runs on YOUR display so you can see it, instead of provisioning an
	# Xvfb you cannot. The config directory is still sandboxed, so it does not
	# touch your flycast.lua, emu.cfg or input bindings - that is the friction
	# this flag removes.
	#
	# It sends NO synthetic input. Watching is passive; the isotest contract
	# forbids synthesising input on an attached display, and this does not
	# relax it. Nothing here clicks, types or moves your pointer.
	[ -n "${DISPLAY:-}" ] || { echo "testrun: SKIP - --watch needs DISPLAY set" >&2; exit $SKIP; }
	[ "$JOBS" -eq 1 ] || echo "testrun: --watch forces -j 1 (one window at a time)" >&2
	JOBS=1
else
	command -v Xvfb >/dev/null || { echo "testrun: SKIP - Xvfb not installed" >&2; exit $SKIP; }
fi

# A clip to replay. Tests want a movie running; without one the emulator sits in
# attract mode and anything asserting on playback is vacuous.
if [ -z "$CLIP" ]; then
	# NEWEST USABLE, not merely newest. A .flyr is 93 bytes of header plus 28
	# per frame, so the frame count is arithmetic - and a movie that ends before
	# a test has finished is not a fixture, it is a timeout with a plausible
	# explanation.
	#
	# [MEASURED 2026-09-08] a 120-frame clip left behind by an aborted run
	# became the newest, and BOTH tests timed out with "replay end at frame 119
	# (movie exhausted)". The failure looked like the code change under test.
	#
	# Prefers a clip that has a savestate beside it, since AutoSeekState=0 seeks
	# to one - a clip without it plays from power-on, which is attract mode.
	MINFRAMES="${FLYCAST_TEST_MINFRAMES:-600}"
	best=""; bestwithstate=""
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		frames=$(( ( $(stat -c%s "$f") - 93 ) / 28 ))
		[ "$frames" -ge "$MINFRAMES" ] || continue
		[ -z "$best" ] && best="$f"
		if ls "$(dirname "$f")"/*.state >/dev/null 2>&1; then bestwithstate="$f"; break; fi
	done
	CLIP="${bestwithstate:-$best}"
	[ -n "$CLIP" ] && echo "testrun: clip $(basename "$(dirname "$CLIP")")" \
		"($(( ( $(stat -c%s "$CLIP") - 93 ) / 28 )) frames$(ls "$(dirname "$CLIP")"/*.state >/dev/null 2>&1 && echo ", has a savestate"))"
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || {
	echo "testrun: SKIP - no usable .flyr clip (need >= ${MINFRAMES:-600} frames); pass --clip" >&2
	exit $SKIP
}

mkdir -p "$OUT"

# ONE RUN AT A TIME, AND IT REFUSES RATHER THAN INTERLEAVES.
#
# $OUT is a fixed directory, so two concurrent runs write verdicts into the same
# place and then read each other's. [MEASURED 2026-09-08] a --watch run and a
# ctest run overlapped: the watch reported `FAIL fail_assertion` - a NEGATIVE
# test's verdict, belonging to the other run - and ctest saw its harness fixture
# "fail" and skipped the suite. Both verdicts were false and both looked exactly
# like real ones. The gate built to catch an untrustworthy harness did its job
# perfectly, on evidence that was itself corrupt.
#
# Refusing is the honest answer: a second run cannot produce a trustworthy
# verdict, and a wrong verdict is worse than none. 77, not 1 - "cannot run now"
# is not a failing test.
exec 9>"$OUT/.lock"
if ! flock -n 9; then
	echo "testrun: SKIP - another testrun is already using $OUT" >&2
	echo "testrun:   two runs share one verdict directory and would read each" >&2
	echo "testrun:   other's results. Wait for it, or set FLYCAST_TEST_OUT." >&2
	exit $SKIP
fi

rm -f "$OUT"/*.log "$OUT"/*.verdict 2>/dev/null

run_one() {  # run_one <test.lua> <slot>
	local test="$1" slot="$2"
	local name; name="$(basename "$test" .lua)"
	local work; work="$(mktemp -d)"
	local disp=":$((BASE_DISPLAY + slot))"
	[ "$WATCH" -eq 1 ] && disp="$DISPLAY"
	local cfg="$work/config/flycast-dojo"
	mkdir -p "$cfg"
	# A CLIP IS A FOLDER, NOT A FILE. The movie's savestates live beside it and
	# `dojo:AutoSeekState=0` seeks to state 0 - so copying only the .flyr left
	# the seek with nothing to find, and the replay silently played from
	# power-on instead of from the state. [MEASURED 2026-09-08] the run passed
	# while showing attract mode, which is the wrong thing passing.
	#
	# Copied rather than used in place, because opening a clip REWRITES its
	# clip.json; a shared clip would be mutated by every test that touched it.
	mkdir -p "$work/clip"
	cp "$CLIP" "$work/clip/clip.flyr"
	for sib in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* \
	           "$(dirname "$CLIP")"/clip.json; do
		[ -f "$sib" ] && cp "$sib" "$work/clip/" 2>/dev/null
	done
	cp "$test" "$cfg/flycast.lua"

	local xpid=""
	if [ "$WATCH" -eq 1 ]; then
		echo "testrun: watching $name on $disp - it will close itself; Ctrl-C to stop early"
	else
		nohup Xvfb "$disp" -screen 0 1280x1024x24 >"$work/xvfb.log" 2>&1 &
		xpid=$!
		local ok=0
		for _ in $(seq 1 20); do
			DISPLAY="$disp" xdpyinfo >/dev/null 2>&1 && { ok=1; break; }
			sleep 0.5
		done
		if [ "$ok" -ne 1 ]; then
			echo "INCONCLUSIVE $name  (no display $disp)" > "$OUT/$name.verdict"
			kill -9 "$xpid" 2>/dev/null; rm -rf "$work"; return
		fi
	fi

	# setsid: the emulator gets its own process GROUP, so teardown takes its
	# children with it. Killing only the pid we were handed leaves whatever it
	# spawned holding the display.
	# I3SOCK/SWAYSOCK are stripped for a PROVISIONED display, where they would
	# silently address the real WM instead (the hazard isotest.sh documents). On
	# YOUR display that is exactly the wrong thing: the window should be managed
	# normally, so it is left alone under --watch.
	local strip=(env -u I3SOCK -u SWAYSOCK -u WAYLAND_DISPLAY)
	[ "$WATCH" -eq 1 ] && strip=(env)
	"${strip[@]}" \
		DISPLAY="$disp" XDG_CONFIG_HOME="$work/config" FLYCAST_TESTLIB="$ROOT/scripts/lua/testlib.lua" \
		setsid "$BIN" \
			-config dojo:Replay=yes -config "dojo:ReplayFilename=$work/clip/clip.flyr" \
			-config dojo:AutoSeekState=0 \
			-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
			${TESTRUN_EXTRA_CONFIG:-} \
			"$ROM" >"$work/stdout.log" 2>&1 &
	local pid=$!
	local lua="$work/config/flycast-dojo/flycast-lua.log"

	# Poll for a WHOLE LINE "done", with a deadline.
	local waited=0 finished=0
	while [ "$waited" -lt "$TIMEOUT" ]; do
		if [ -f "$lua" ] && grep -qax '[[:space:]]*done[[:space:]]*' "$lua" 2>/dev/null; then
			finished=1; break
		fi
		kill -0 "$pid" 2>/dev/null || break      # died on its own
		sleep 1; waited=$((waited + 1))
	done

	# HOLD THE WINDOW OPEN. A test closes itself the instant it prints `done`,
	# which is right for a suite and exactly wrong for a mode whose entire
	# purpose is that a person looks at it - the first --watch run finished
	# before its user got to the screen.
	if [ "$WATCH" -eq 1 ] && [ "$HOLD" -lt 0 ]; then
		# --hold -1: DO NOT KILL IT. The window stays until you close the
		# emulator yourself. The verdict is already decided at this point - the
		# `done` line has been seen - so the run is only still alive for you to
		# poke at, which is the entire purpose of --watch.
		echo "testrun: $name finished - LEAVING IT OPEN. Close the emulator when you are done."
		wait "$pid" 2>/dev/null
	elif [ "$WATCH" -eq 1 ] && [ "$finished" -eq 1 ] && [ "$HOLD" -gt 0 ]; then
		echo "testrun: $name finished - holding the window ${HOLD}s so you can read it (Ctrl-C to close now)"
		sleep "$HOLD"
	fi

	# Evidence first, teardown second.
	cp "$lua" "$OUT/$name.lua.log" 2>/dev/null
	cp "$work/stdout.log" "$OUT/$name.stdout.log" 2>/dev/null

	kill -INT -"$pid" 2>/dev/null; sleep 1
	kill -9 -"$pid" 2>/dev/null
	[ -n "$xpid" ] && kill -9 "$xpid" 2>/dev/null
	wait "$pid" 2>/dev/null

	local summary; summary=$(grep -a -oE 'SUMMARY: [0-9]+ passed, [0-9]+ failed[^"]*' "$OUT/$name.lua.log" 2>/dev/null | tail -1)
	if [ "$finished" -ne 1 ]; then
		echo "TIMEOUT      $name  (no 'done' line within ${TIMEOUT}s)" > "$OUT/$name.verdict"
	elif [ -z "$summary" ]; then
		echo "INCONCLUSIVE $name  (finished with no SUMMARY - did it assert anything?)" > "$OUT/$name.verdict"
	else
		local failed; failed=$(echo "$summary" | sed -E 's/.*, ([0-9]+) failed.*/\1/')
		if [ "$failed" -eq 0 ]; then echo "PASS         $name  ($summary)" > "$OUT/$name.verdict"
		else                          echo "FAIL         $name  ($summary)" > "$OUT/$name.verdict"; fi
	fi
	rm -rf "$work"
}

echo "testrun: ${#TESTS[@]} test(s), ${JOBS} at a time, clip $(basename "$CLIP")"
slot=0
for test in "${TESTS[@]}"; do
	[ -f "$test" ] || continue
	while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.5; done
	run_one "$test" "$slot" &
	slot=$(((slot + 1) % JOBS))
done
wait

echo
rc=0
if [ "$SELFTEST" -eq 1 ]; then
	# Inverted: each negative test must produce the verdict its NAME promises.
	for v in "$OUT"/*.verdict; do
		[ -f "$v" ] || continue
		name=$(basename "$v" .verdict)
		want=$(echo "$name" | cut -d_ -f1)
		case "$want" in
			fail)         expect="FAIL" ;;
			timeout)      expect="TIMEOUT" ;;
			inconclusive) expect="INCONCLUSIVE" ;;
			*)            expect="?" ;;
		esac
		got=$(awk '{print $1}' "$v")
		if [ "$got" = "$expect" ]; then echo "ok   $name -> $got"
		else echo "BAD  $name -> $got (expected $expect)"; rc=1; fi
	done
	[ "$rc" -eq 0 ] && echo "self-test: the runner reports every failure mode correctly"
else
	for v in "$OUT"/*.verdict; do
		[ -f "$v" ] || continue
		cat "$v"
		grep -q '^PASS' "$v" || rc=1
	done
fi
echo
echo "logs: $OUT"
exit "$rc"
