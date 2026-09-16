#!/usr/bin/env bash
# ctltest - the tas_ctl control plane drives the emulator deterministically.
#
#   RUN:   scripts/ctltest.sh              (needs a ROM, Xvfb, python3, a clip)
#   PASS:  in a TRAINING session, an external client (cmd.json + resp/<seq>.json)
#          gets synchronous per-seq answers, a `step {n}` advances dojo.frame_number
#          by EXACTLY n, and `input` injects guest frames - all with no keystrokes,
#          no xdotool. Training (not a replay) is what lets step land frame-exact.
#   FAIL:  exit 1 (no response, pause not reported, step off by != n, or no injection).
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
# TRAINING session (not a replay): the game runs live with no movie driving playback,
# so `step` advances EXACTLY n frames and re-pauses (a replay free-runs past the target -
# see the [OPEN] this closes). trainingEnabled() alone makes the session steppable.
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Training=yes -config dojo:RecordMatches=yes \
	-config dojo:AutoLoadNetState=no -config dojo:AutoLoadTrainingNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config "dojo:ControlServer=$CTLSRV" -config "dojo:CtlDir=$OUT/ctl" \
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

# give the emulator time to boot the game to a live, steppable state (so tick() polls)
for _ in $(seq 1 40); do kill -0 "$FC" 2>/dev/null || break; grep -aq "gui_start_game" <(tr -d '\0' <"$OUT/out.log") && break; sleep 0.5; done
sleep 10

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

# 1) query - transport works, reports a frame + mode
send 1 query "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to query (control server not answering)"; tr -d '\0' <"$OUT/out.log"|grep -a "CTL\|control" |tail -5; cleanup; exit 1; }
ok1=$(field "$RQ" ok); mode0=$(field "$RQ" mode)
echo "ctltest: query -> ok=$ok1 mode=$mode0"
[ "$ok1" = "True" ] || { echo "FAIL ctltest - query not ok"; cleanup; exit 1; }

# 2) pause - stop the live game so step advances exactly N and re-pauses
send 2 pause "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to pause"; cleanup; exit 1; }
paused=$(field "$RQ" paused)
echo "ctltest: pause -> ok=$(field "$RQ" ok) paused=$paused"

# 3) query the pre-step frame
send 3 query "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to pre-step query"; cleanup; exit 1; }
f0=$(field "$RQ" frame)

# 4) step N - the deterministic, frame-EXACT advance primitive (the [OPEN] this closes)
N=10
send 4 step "{\"n\":$N}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to step"; cleanup; exit 1; }
stepok=$(field "$RQ" ok)
echo "ctltest: step n=$N -> ok=$stepok frame=$(field "$RQ" frame)"

# 5) query - assert the counter advanced by EXACTLY N
send 5 query "{}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to post-step query"; cleanup; exit 1; }
f1=$(field "$RQ" frame)

# 6) input - inject guest input at upcoming frames (maple layer, no keystrokes)
send 6 input "{\"p1\":16,\"frame\":$f1,\"hold\":5}" > "$RQ" 2>/dev/null || { echo "FAIL ctltest - no response to input"; cleanup; exit 1; }
inputok=$(field "$RQ" ok)
echo "ctltest: input p1=0x10 @frame $f1 x5 -> ok=$inputok"
injlog="$(tr -d '\0' <"$OUT/out.log" | grep -a "CTL: InjectInput" | tail -1)"

tr -d '\0' <"$OUT/out.log" | grep -a "CTL" | tail -5 | sed 's/^/  /'
cleanup; sleep 1
echo "ctltest: frame $f0 -> $f1 after step $N"

# --- verdicts ---
if [ "$paused" != "True" ]; then echo "FAIL ctltest - pause did not report paused=true"; exit 1; fi
case "$f0$f1" in *None*|"") echo "FAIL ctltest - missing frame in a response"; exit 1 ;; esac
if [ "$stepok" != "True" ]; then echo "FAIL ctltest - step verb returned ok=$stepok"; exit 1; fi
adv=$((f1 - f0))
if [ "$adv" -ne "$N" ]; then
	echo "FAIL ctltest - step advanced $adv frames, expected EXACTLY $N ($f0 -> $f1)"
	exit 1
fi
if [ "$inputok" != "True" ]; then echo "FAIL ctltest - input verb returned ok=$inputok"; exit 1; fi
if [ -z "$injlog" ]; then echo "FAIL ctltest - input verb ok but no InjectInput trace (nothing written)"; exit 1; fi
echo "PASS ctltest - remote step advanced the frame counter by EXACTLY $N, and input injected guest frames ($injlog)"
exit 0
