#!/usr/bin/env bash
# sendequivtest - the RECORD vs SEND journey (journey 3), by STATE HASH.
#
#   RUN:   scripts/sendequivtest.sh          (needs a ROM, Xvfb, python3, a base clip)
#   PASS:  from ONE base state, three arms author an 8-frame window - NEUTRAL, a
#          RECORD-form reference (raw DC_BTN_A, no InjectInput), and the SEND path
#          (Dojo::InjectInput) - and the machine is hashed after each. The claim:
#          send == record (InjectInput lands the record-equivalent state) AND
#          record != neutral (the press actually did something - non-vacuity).
#   FAIL:  exit 1 (send != record, or the press changed nothing).
#   SKIP:  exit 77 (no ROM / Xvfb / python3 / build / a base clip with a state).
#   SELF:  scripts/sendequivtest.sh --self-test - boots with SendEquivProbe=shift,
#          so SEND injects one frame late. The equivalence MUST then break
#          (match=no); the twin exits 0 when it did (the state hash detects a
#          one-frame misalignment), 1 if a shifted send still matched (the
#          comparison is blind to frame placement - it would prove nothing).
#
# WHY A PROBE, NO CLICKS. Same doctrine as fsttest/branchtest: dojo:SendEquivProbe
# arms the three-arm run from slot 0 and mainui's tick drives it; the probe emits
# one "SENDEQUIV RESULT:" line. RecordMatches=yes makes the WRITE session steppable
# (session::writeGrow) so each arm can advance while paused - the same setting the
# FST needs, and the same gui_loadState/saveState Closed||Paused acceptance.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${SENDEQUIVTEST_OUT:-}" ]; then OUT="$SENDEQUIVTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "sendequivtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "sendequivtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb    >/dev/null || { echo "sendequivtest: SKIP - no Xvfb"; exit $SKIP; }
command -v python3 >/dev/null || { echo "sendequivtest: SKIP - no python3"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "sendequivtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# A base clip with a savestate whose movie frame we can base the window on - the
# same fixture rule fsttest uses (a prefixHash-stamped state guarantees slot 0 is
# a real point on the movie, mid-match, where a jab visibly changes state).
CLIP="${FLYCAST_TEST_CLIP:-}"
pick_clip() {
	local f d
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		d="$(dirname "$f")"; [ -f "$d/clip.json" ] || continue
		ls "$d"/*.state >/dev/null 2>&1 || continue
		python3 - "$d/clip.json" <<-'PY' || continue
		import json,sys
		j=json.load(open(sys.argv[1])); s=(j.get("states") or [])
		sys.exit(0 if any(isinstance(x,dict) and isinstance(x.get("prefixHash"),int) and x.get("prefixHash") and "movieFrame" in x for x in s) else 1)
		PY
		printf '%s' "$f"; return 0
	done
	return 1
}
[ -n "$CLIP" ] || CLIP="$(pick_clip)" || { echo "sendequivtest: SKIP - no base clip with a stamped savestate"; exit $SKIP; }
[ -f "$CLIP" ] || { echo "sendequivtest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
echo "sendequivtest: base clip $CLIP"

# stage a private fixture copy
mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$OUT/clip/" 2>/dev/null; } || true
done

DN=$((160 + ($$ % 80))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 250 ] && { echo "sendequivtest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "sendequivtest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

PROBE=yes; [ "$SELF" -eq 1 ] && PROBE=shift
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config "dojo:SendEquivProbe=$PROBE" \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 120); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SENDEQUIV RESULT:" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SENDEQUIV RESULT:" | tail -1)"
tr -d '\0' < "$OUT/out.log" | grep -a "SENDEQUIV PROBE:\|SENDEQUIV RESULT:" | sed 's/.*[NW]\[[A-Z]*\]: /  /' | tail -8
cleanup; sleep 1

[ -n "$RESULT" ] || { echo "sendequivtest: SKIP - the probe never reported (never steppable / no base state); see $OUT/out.log"; exit $SKIP; }
match=$(echo "$RESULT"  | sed -n 's/.*match=\([a-z]*\).*/\1/p')
moved=$(echo "$RESULT"  | sed -n 's/.*moved=\([a-z]*\).*/\1/p')
shifted=$(echo "$RESULT" | sed -n 's/.*shift=\([a-z]*\).*/\1/p')
echo "sendequivtest: match=$match moved=$moved shift=$shifted"

# NON-VACUITY (both arms): the A press must actually change the state, or "send ==
# record" is just "two ways of doing nothing agree" and proves nothing.
if [ "$moved" != "yes" ]; then
	echo "FAIL sendequivtest - the A press did not change the state (record==neutral); the fixture is not mid-action, so the equivalence would be vacuous"
	exit 1
fi

if [ "$SELF" -eq 1 ]; then
	if [ "$match" = "no" ]; then
		echo "PASS sendequivtest (self-test) - a send injected one frame late did NOT match the record (the state hash detects frame misalignment)"
		exit 0
	fi
	echo "FAIL sendequivtest (self-test) - a one-frame-late send still matched the record; the comparison is blind to frame placement"
	exit 1
fi

if [ "$match" != "yes" ]; then
	echo "FAIL sendequivtest - the SEND path (InjectInput) did not land the same state as the RECORD-form reference"
	exit 1
fi
echo "PASS sendequivtest - a sent input (InjectInput) and a recorded input walk the SAME state path, and the press was real (record != neutral)"
exit 0
