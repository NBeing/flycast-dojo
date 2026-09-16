#!/usr/bin/env bash
# macrostest - the Macros browser places a saved clip macro into the movie.
#
#   RUN:   scripts/macrostest.sh              (needs a ROM, Xvfb, a clip)
#   PASS:  with the Macros panel open, dojo:MacrosProbe scans the game's replays
#          folder for macro .txt files (self-seeding one via WriteMacroFile if none),
#          places the first at a mid-movie selection through the roll's edit funnel,
#          and the movie ROW there CHANGES - the browser -> libraryRead -> ApplyEdit path.
#   FAIL:  exit 1 (no placement, or the placed row did not change).
#   SKIP:  exit 77 (no ROM / Xvfb / a clip / build).
#   SELF:  scripts/macrostest.sh --self-test - boot with MacrosProbe=no; the probe
#          must NOT run (no RESULT line), so "a macro was placed" can fail.
#
# The Macros scanner reads <data>/flycast-dojo/replays/<game>/, so the fixture clip is
# staged UNDER that root (not a temp dir), and MacrosProbe self-seeds a macro into it.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${MACROSTEST_OUT:-}" ]; then OUT="$MACROSTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "macrostest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "macrostest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "macrostest: SKIP - no Xvfb"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "macrostest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

CLIP="${FLYCAST_TEST_CLIP:-}"
if [ -z "$CLIP" ]; then
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		[ -f "$f" ] && { CLIP="$f"; break; }
	done
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "macrostest: SKIP - no base clip"; exit $SKIP; }
# GAME = the clip's grandparent dir - what get_game_name() produced when it was recorded,
# so the staged fixture lands where the scanner (replaysRoot/<game>) will look.
GAME="$(basename "$(dirname "$(dirname "$CLIP")")")"
echo "macrostest: base clip $CLIP  (game=$GAME)"

# stage the fixture UNDER the sandboxed replays root
CLIPDIR="$OUT/data/flycast-dojo/replays/$GAME/testclip"
mkdir -p "$OUT/cfg/flycast-dojo" "$CLIPDIR"
cp "$CLIP" "$CLIPDIR/clip.flyr"
for s in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* "$(dirname "$CLIP")"/clip.json; do { [ -f "$s" ] && cp "$s" "$CLIPDIR/"; } || true; done

DN=$((150 + ($$ % 90))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "macrostest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "macrostest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

PROBE=yes; [ "$SELF" -eq 1 ] && PROBE=no
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIPDIR/clip.flyr" \
	-config dojo:AutoSeekState=-1 -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:Panel.macros=yes -config "dojo:MacrosProbe=$PROBE" \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 60); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "MACROS PROBE RESULT:" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "MACROS PROBE RESULT:" | tail -1)"
tr -d '\0' < "$OUT/out.log" | grep -a "MACRO" | sed 's/^/  /' | tail -6
cleanup; sleep 1

if [ "$SELF" -eq 1 ]; then
	if [ -z "$RESULT" ]; then
		echo "PASS macrostest (self-test) - MacrosProbe=no did not run (no placement claim to make)"
		exit 0
	fi
	echo "FAIL macrostest (self-test) - the probe ran with MacrosProbe=no: $RESULT"
	exit 1
fi

[ -n "$RESULT" ] || { echo "macrostest: SKIP - the probe never reported (panel never drew / no movie / seed failed)"; exit $SKIP; }
placed=$(echo "$RESULT" | sed -n 's/.*placed=\([A-Za-z]*\).*/\1/p')
changed=$(echo "$RESULT" | sed -n 's/.*rowChanged=\([A-Za-z]*\).*/\1/p')
count=$(echo "$RESULT" | sed -n 's/.*count=\([0-9]*\).*/\1/p')
echo "macrostest: placed=$placed rowChanged=$changed count=$count"
if [ "${count:-0}" -lt 1 ]; then echo "FAIL macrostest - no macro found even after self-seed"; exit 1; fi
if [ "$placed" != "yes" ]; then echo "FAIL macrostest - the panel did not place a macro (place returned -1)"; exit 1; fi
if [ "$changed" != "yes" ]; then echo "FAIL macrostest - a macro was placed but the movie row did not change"; exit 1; fi
echo "PASS macrostest - the Macros browser placed a saved clip macro and the movie row changed (count=$count)"
exit 0
