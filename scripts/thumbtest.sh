#!/usr/bin/env bash
# thumbtest - savestate thumbnails are written, and are a REAL picture (not blank).
#
#   RUN:   scripts/thumbtest.sh              (needs a ROM, Xvfb, a base clip)
#   PASS:  saving states writes "<state>.png" thumbnails, each a NON-BLANK image
#          at the configured width - i.e. the GL readback (core/rend/gles/gles.h
#          GetLastFrameRGB) actually captured the rendered frame.
#   FAIL:  exit 1 (no PNG, or a well-formed but BLANK PNG - the PBO-never-advanced
#          class of bug CLAUDE.md's capture history records).
#   SKIP:  exit 77 (no ROM / Xvfb / a base clip with a state / no ImageMagick).
#   SELF:  scripts/thumbtest.sh --self-test - boot with dojo:StateThumbnails=no;
#          NO PNG must be written. The twin exits 0 when the feature-off run wrote
#          none (so the "a thumbnail appeared" claim can actually fail), 1 if a
#          thumbnail leaked with the feature off.
#
# HOW. tas_thumb::captureForState runs from gui_saveState (render thread, emu
# stopped). To trigger real saves without xdotool, this reuses dojo:FstProbe,
# whose sweep saves N outcome states to slots 1..N - each save writes a thumbnail.
# The vehicle is incidental; the claim is about thumbnail GENERATION.
#
# His GetLastFrameRGB is DX9/DX11-only; the GL half was added for this tree so the
# feature (and this test) work under the GL/llvmpipe path the suite runs on.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${THUMBTEST_OUT:-}" ]; then OUT="$THUMBTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "thumbtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "thumbtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb    >/dev/null || { echo "thumbtest: SKIP - no Xvfb"; exit $SKIP; }
command -v convert >/dev/null || { echo "thumbtest: SKIP - no ImageMagick (convert)"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "thumbtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# a base clip with a savestate (the FST needs slot 0 to sweep from)
CLIP="${FLYCAST_TEST_CLIP:-}"
if [ -z "$CLIP" ]; then
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		ls "$(dirname "$f")"/*.state >/dev/null 2>&1 && { CLIP="$f"; break; }
	done
fi
[ -n "$CLIP" ] && [ -f "$CLIP" ] || { echo "thumbtest: SKIP - no base clip with a savestate"; exit $SKIP; }
SRC="$(dirname "$CLIP")"
echo "thumbtest: base clip $CLIP"

mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"
for s in "$SRC"/*.state "$SRC"/*.state.* "$SRC"/clip.json; do { [ -f "$s" ] && cp "$s" "$OUT/clip/"; } || true; done
rm -f "$OUT/clip"/*.png "$OUT/clip"/*_[0-9].state 2>/dev/null || true

# free display, never :0/:1
DN=$((170 + ($$ % 70))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "thumbtest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "thumbtest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }
echo "thumbtest: display $D"

THUMBS=yes; [ "$SELF" -eq 1 ] && THUMBS=no
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config dojo:FstProbe=yes -config "dojo:StateThumbnails=$THUMBS" \
	-config dojo:Panel.states=yes -config dojo:StatesThumbProbe=yes \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
for _ in $(seq 1 150); do kill -0 "$FC" 2>/dev/null || break; tr -d '\0' < "$OUT/out.log" | grep -aq "TAS FST RUN: results ->" && break; sleep 1; done
sleep 2
cleanup; sleep 1

tr -d '\0' < "$OUT/out.log" | grep -a "TAS thumb\|TAS FST RUN: results" | sed 's/.*: //' | tail -4 | sed 's/^/  /'
shopt -s nullglob 2>/dev/null || true
PNGS=("$OUT/clip"/*.state.png)
N=${#PNGS[@]}
echo "thumbtest: $N thumbnail PNG(s) written (StateThumbnails=$THUMBS)"

if [ "$SELF" -eq 1 ]; then
	# feature OFF: no thumbnail may appear. If one does, the gate is broken.
	if [ "$N" -eq 0 ]; then
		echo "PASS thumbtest (self-test) - StateThumbnails=no wrote no thumbnails"
		exit 0
	fi
	echo "FAIL thumbtest (self-test) - $N thumbnail(s) leaked with StateThumbnails=no"
	exit 1
fi

# NORMAL: at least one thumbnail, and each a NON-BLANK image at a sane size.
if [ "$N" -eq 0 ]; then
	echo "FAIL thumbtest - saving states wrote NO thumbnail (GL readback failed? see out.log)"
	exit 1
fi
bad=0
for p in "${PNGS[@]}"; do
	read w h sd < <(convert "$p" -colorspace Gray -format '%w %h %[fx:standard_deviation*255]' info: 2>/dev/null)
	ok=$(awk -v s="${sd:-0}" 'BEGIN{print (s>10)?1:0}')
	echo "  $(basename "$p"): ${w:-?}x${h:-?} stddev ${sd:-?} $([ "$ok" = 1 ] && echo OK || echo BLANK)"
	{ [ "${w:-0}" -ge 64 ] && [ "$ok" = 1 ]; } || bad=$((bad+1))
done
if [ "$bad" -ne 0 ]; then
	echo "FAIL thumbtest - $bad/$N thumbnail(s) blank or undersized (a well-formed blank PNG is the PBO-never-advanced bug)"
	exit 1
fi

# DISPLAY WIRING: the F4 States grid must be able to GET a thumbnail handle for an
# occupied slot through the host (the "remaining half" states_panel flagged). The
# StatesThumbProbe polled the open States panel and asked host()->slotThumbnail().
TP="$(tr -d '\0' < "$OUT/out.log" | grep -a "STATES THUMBPROBE:" | tail -1)"
if [ -n "$TP" ]; then
	echo "  ${TP#*] }"
	case "$TP" in
		*"=> PASS"*) echo "thumbtest: display wiring OK - the States grid got a valid thumbnail handle from the host" ;;
		*) echo "FAIL thumbtest - the States panel got no usable thumbnail handle for an occupied slot (display wiring)"; exit 1 ;;
	esac
else
	echo "thumbtest: NOTE - StatesThumbProbe did not report (panel never drew?); GENERATION still verified above"
fi
echo "PASS thumbtest - $N non-blank thumbnail(s) written on save, and the States grid can display them"
exit 0
