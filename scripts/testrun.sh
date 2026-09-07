#!/usr/bin/env bash
# testrun - run flycast Lua tests, one process each, in parallel, offscreen.
#
#   scripts/testrun.sh                        run every scripts/tests/*.lua
#   scripts/testrun.sh -j 4                   ... four at a time
#   scripts/testrun.sh scripts/tests/foo.lua  just this one
#   scripts/testrun.sh --rom /path/to.cdi     override the ROM
#
# Exit 0 only if every test PASSed. Requires Xvfb.
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
command -v Xvfb >/dev/null || { echo "testrun: SKIP - Xvfb not installed" >&2; exit $SKIP; }

# A clip to replay. Tests want a movie running; without one the emulator sits in
# attract mode and anything asserting on playback is vacuous.
if [ -z "$CLIP" ]; then
	CLIP=$(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null | head -1)
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "testrun: SKIP - no .flyr clip found; pass --clip" >&2; exit $SKIP; }

mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.verdict 2>/dev/null

run_one() {  # run_one <test.lua> <slot>
	local test="$1" slot="$2"
	local name; name="$(basename "$test" .lua)"
	local work; work="$(mktemp -d)"
	local disp=":$((BASE_DISPLAY + slot))"
	local cfg="$work/config/flycast-dojo"
	mkdir -p "$cfg"
	# The clip is COPIED: opening one rewrites clip.json beside the movie, so a
	# shared clip would be mutated by every test that touched it.
	mkdir -p "$work/clip"; cp "$CLIP" "$work/clip/clip.flyr"
	cp "$test" "$cfg/flycast.lua"

	nohup Xvfb "$disp" -screen 0 1280x1024x24 >"$work/xvfb.log" 2>&1 &
	local xpid=$!
	local ok=0
	for _ in $(seq 1 20); do
		DISPLAY="$disp" xdpyinfo >/dev/null 2>&1 && { ok=1; break; }
		sleep 0.5
	done
	if [ "$ok" -ne 1 ]; then
		echo "INCONCLUSIVE $name  (no display $disp)" > "$OUT/$name.verdict"
		kill -9 "$xpid" 2>/dev/null; rm -rf "$work"; return
	fi

	# setsid: the emulator gets its own process GROUP, so teardown takes its
	# children with it. Killing only the pid we were handed leaves whatever it
	# spawned holding the display.
	env -u I3SOCK -u SWAYSOCK -u WAYLAND_DISPLAY \
		DISPLAY="$disp" XDG_CONFIG_HOME="$work/config" FLYCAST_TESTLIB="$ROOT/scripts/lua/testlib.lua" \
		setsid "$BIN" \
			-config dojo:Replay=yes -config "dojo:ReplayFilename=$work/clip/clip.flyr" \
			-config dojo:AutoSeekState=0 \
			-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
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

	# Evidence first, teardown second.
	cp "$lua" "$OUT/$name.lua.log" 2>/dev/null
	cp "$work/stdout.log" "$OUT/$name.stdout.log" 2>/dev/null

	kill -INT -"$pid" 2>/dev/null; sleep 1
	kill -9 -"$pid" 2>/dev/null
	kill -9 "$xpid" 2>/dev/null
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
