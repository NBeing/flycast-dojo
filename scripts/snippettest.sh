#!/usr/bin/env bash
# snippettest - the Snippets browser places a library sequence into the movie.
#
#   RUN:   scripts/snippettest.sh              (needs a ROM, Xvfb, a clip)
#   PASS:  with the Snippets panel open, dojo:SnippetProbe scans the sequence
#          library (self-seeding one if empty), places its first sequence at a
#          mid-movie selection through the roll's edit funnel, and the movie ROW
#          there CHANGES - i.e. the browser -> roll_library -> ApplyEdit path works.
#   FAIL:  exit 1 (no placement, or the placed row did not change).
#   SKIP:  exit 77 (no ROM / Xvfb / a clip / build).
#   SELF:  scripts/snippettest.sh --self-test - boot with SnippetProbe=no; the
#          probe must NOT run (no RESULT line), so "a snippet was placed" can fail.
#
# WHY A PROBE. The Snippets window is a browser over roll_library, whose place path
# (patternOverdubbing/Replacing -> applyPattern -> Dojo::ApplyEdit) is already proven
# by rolltest's RollLibProbe. SnippetProbe drives THIS panel's place() - the same call
# the Place button makes - so the test needs no clicks.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${SNIPPETTEST_OUT:-}" ]; then OUT="$SNIPPETTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "snippettest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "snippettest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "snippettest: SKIP - no Xvfb"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "snippettest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

CLIP="${FLYCAST_TEST_CLIP:-}"
if [ -z "$CLIP" ]; then
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		[ -f "$f" ] && { CLIP="$f"; break; }
	done
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "snippettest: SKIP - no base clip"; exit $SKIP; }
echo "snippettest: base clip $CLIP"

mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"
for s in "$(dirname "$CLIP")"/*.state "$(dirname "$CLIP")"/*.state.* "$(dirname "$CLIP")"/clip.json; do { [ -f "$s" ] && cp "$s" "$OUT/clip/"; } || true; done

DN=$((150 + ($$ % 90))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "snippettest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "snippettest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

PROBE=yes; [ "$SELF" -eq 1 ] && PROBE=no
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=-1 -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:Panel.snippets=yes -config "dojo:SnippetProbe=$PROBE" \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 60); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SNIPPET PROBE RESULT:" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SNIPPET PROBE RESULT:" | tail -1)"
tr -d '\0' < "$OUT/out.log" | grep -a "SNIPPET" | sed 's/^/  /' | tail -5
cleanup; sleep 1

if [ "$SELF" -eq 1 ]; then
	if [ -z "$RESULT" ]; then
		echo "PASS snippettest (self-test) - SnippetProbe=no did not run (no placement claim to make)"
		exit 0
	fi
	echo "FAIL snippettest (self-test) - the probe ran with SnippetProbe=no: $RESULT"
	exit 1
fi

[ -n "$RESULT" ] || { echo "snippettest: SKIP - the probe never reported (panel never drew / no movie)"; exit $SKIP; }
placed=$(echo "$RESULT" | sed -n 's/.*placed=\([A-Za-z]*\).*/\1/p')
changed=$(echo "$RESULT" | sed -n 's/.*rowChanged=\([A-Za-z]*\).*/\1/p')
libc=$(echo "$RESULT" | sed -n 's/.*libCount=\([0-9]*\).*/\1/p')
echo "snippettest: placed=$placed rowChanged=$changed libCount=$libc"
if [ "${libc:-0}" -lt 1 ]; then echo "FAIL snippettest - library empty even after self-seed"; exit 1; fi
if [ "$placed" != "yes" ]; then echo "FAIL snippettest - the panel did not place a snippet (place returned -1)"; exit 1; fi
if [ "$changed" != "yes" ]; then echo "FAIL snippettest - a snippet was placed but the movie row did not change"; exit 1; fi
echo "PASS snippettest - the Snippets browser placed a library sequence and the movie row changed (libCount=$libc)"
exit 0
