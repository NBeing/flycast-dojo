#!/usr/bin/env bash
# ctltest - the tas_ctl control plane drives the emulator deterministically.
#
#   RUN:   scripts/ctltest.sh              (needs a ROM, Xvfb, python3, a clip)
#   PASS:  an external client, writing <ctlDir>/_ctl/cmd.json and reading
#          resp/<seq>.json, gets synchronous per-seq answers, and a state-changing
#          verb (save) has an OBSERVABLE effect - a <base>_<slot>.state on disk. No
#          keystrokes, no xdotool. (step/input want an authoring session - see below.)
#   FAIL:  exit 1 (no response, pause not reported, or no state file written).
#   SKIP:  exit 77 (no ROM / Xvfb / python3 / a clip / build).
#   SELF:  scripts/ctltest.sh --self-test - boot with dojo:ControlServer=no; the
#          same cmd.json must be IGNORED (no resp ever appears). The twin exits 0
#          when the gate held (no response), 1 if a disabled server answered.
#
# WHY. dojo:ControlServer (ported from dev's 0915 tree) is the xdotool replacement
# for input-driven journeys: it drives the running emulator via a JSON command file
# polled once per rendered frame, on the render/UI thread, deterministically. This
# proves the transport + one state-changing verb (step) end to end.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${CTLTEST_OUT:-}" ]; then OUT="$CTLTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "ctltest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "ctltest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb    >/dev/null || { echo "ctltest: SKIP - no Xvfb"; exit $SKIP; }
command -v python3 >/dev/null || { echo "ctltest: SKIP - no python3"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "ctltest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

CLIP="${FLYCAST_TEST_CLIP:-}"
if [ -z "$CLIP" ]; then
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		ls "$(dirname "$f")"/*.state >/dev/null 2>&1 && { CLIP="$f"; break; }
	done
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "ctltest: SKIP - no base clip"; exit $SKIP; }
SRC="$(dirname "$CLIP")"
echo "ctltest: base clip $CLIP"

mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data" "$OUT/clip" "$OUT/ctl/_ctl/resp"
cp "$CLIP" "$OUT/clip/clip.flyr"
for s in "$SRC"/*.state "$SRC"/*.state.* "$SRC"/clip.json; do { [ -f "$s" ] && cp "$s" "$OUT/clip/"; } || true; done

DN=$((150 + ($$ % 90))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "ctltest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 900x700x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "ctltest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

CTLSRV=yes; [ "$SELF" -eq 1 ] && CTLSRV=no
# boot PAUSED (no AutoPlay/AutoSeek/AutoCapture) so frame_number only moves when the
# control server's step verb moves it - the whole point of the measurement.
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=-1 -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config "dojo:ControlServer=$CTLSRV" -config "dojo:CtlDir=$OUT/ctl" \
	-config window:width=900 -config window:height=700 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

BASE="$OUT/ctl/_ctl"
# send <seq> <verb> <args-json>; wait up to ~12s for resp/<seq>.json; echo it (or empty)
send() {
	local seq="$1" verb="$2" args="${3:-{\}}" i
	printf '{"seq":%s,"verb":"%s","args":%s}\n' "$seq" "$verb" "$args" > "$BASE/cmd.json.tmp"
	mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"		# atomic: the server mtime-polls cmd.json
	for i in $(seq 1 60); do
		[ -f "$BASE/resp/$seq.json" ] && { cat "$BASE/resp/$seq.json"; return 0; }
		kill -0 "$FC" 2>/dev/null || return 1
		sleep 0.2
	done
	return 1
}
field() { python3 -c "import json,sys; d=json.load(open('$1')); print(d.get('$2'))" 2>/dev/null; }

# give the emulator time to boot the game and start rendering (so tick() polls)
for _ in $(seq 1 40); do kill -0 "$FC" 2>/dev/null || break; grep -aq "LOAD REPLAY FILE" <(tr -d '\0' <"$OUT/out.log") && break; sleep 0.5; done
sleep 6

# PROTOCOL: the server baselines s_lastHandledSeq on the FIRST cmd.json it sees and
# does NOT execute it (so a stale command left from a prior session is skipped, not
# replayed). A real client therefore primes with a throwaway seq the server will
# baseline + ignore; real commands (seq > it) then execute. Prime with seq 0.
printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"
sleep 2

RQ="$OUT/rq.json"

if [ "$SELF" -eq 1 ]; then
	# server OFF: a command must be ignored - no response file should ever appear.
	send 1 query "{}" > "$RQ" 2>/dev/null
	got=$?
	cleanup; sleep 1
	if [ "$got" -ne 0 ] && [ ! -s "$RQ" ]; then
		echo "PASS ctltest (self-test) - ControlServer=no ignored the command (no response)"
		exit 0
	fi
	echo "FAIL ctltest (self-test) - a disabled control server answered: $(cat "$RQ" 2>/dev/null)"
	exit 1
fi

# 1) query - the transport works and reports a frame + mode
send 1 query "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to query (control server not answering)"; tr -d '\0' <"$OUT/out.log"|grep -a "CTL\|control" |tail -5; cleanup; exit 1; }
ok1=$(field "$RQ" ok); mode0=$(field "$RQ" mode)
echo "ctltest: query -> ok=$ok1 mode=$mode0"
[ "$ok1" = "True" ] || { echo "FAIL ctltest - query not ok"; cleanup; exit 1; }

# 2) pause - a state-changing verb whose effect is reported back (paused=true).
send 2 pause "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to pause"; cleanup; exit 1; }
paused=$(field "$RQ" paused)
echo "ctltest: pause -> ok=$(field "$RQ" ok) paused=$paused"

# 3) save {slot} - a state-changing verb with an OBSERVABLE ON-DISK side effect: a
# <base>_<slot>.state must appear. This is the end-to-end proof - an external client,
# over the JSON channel with no keystrokes, drove the emulator to write a file.
SLOT=7
send 3 save "{\"slot\":$SLOT}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to save"; cleanup; exit 1; }
saveok=$(field "$RQ" ok); savedslot=$(field "$RQ" saved_slot)
echo "ctltest: save slot $SLOT -> ok=$saveok saved_slot=$savedslot"
sleep 1
STATEFILE="$(ls "$OUT/clip"/*_"$SLOT".state 2>/dev/null | head -1)"
tr -d '\0' <"$OUT/out.log" | grep -a "CTL" | tail -4 | sed 's/^/  /'
cleanup; sleep 1

# NOTE: `step {n}` is intentionally NOT asserted here. In a bare replay it un-pauses
# and the movie free-runs past the target (the watchdog fires) - a clean N-frame
# advance needs an authoring/Training session the way the Frame Skip Test gets one
# via gui_loadState. [OPEN] a Training-session ctltest would exercise step/input.

if [ "$paused" != "True" ]; then echo "FAIL ctltest - pause did not report paused=true"; exit 1; fi
if [ "$saveok" != "True" ]; then echo "FAIL ctltest - save verb returned ok=$saveok"; exit 1; fi
if [ -z "$STATEFILE" ] || [ ! -s "$STATEFILE" ]; then
	echo "FAIL ctltest - save verb reported ok but no slot-$SLOT state file was written"
	exit 1
fi
echo "PASS ctltest - the control plane answered per-seq (query/pause/save) and a remote save wrote $(basename "$STATEFILE") ($(stat -c %s "$STATEFILE") bytes)"
exit 0
