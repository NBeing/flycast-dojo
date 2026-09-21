#!/bin/bash
# fingerprint.sh <slot> [target-frame] [extra -config flags]
#   Boot David's 2026-09-20 clip in a sandbox, land EXACTLY on <slot>'s frame (paused), print a
#   fingerprint of the game (a fixed list of SPREADSHEET bytes), then - if <target-frame> is past
#   the slot - step exactly to it and print the fingerprint again. Two machines that agree on
#   every byte here at the same frame are, for the game's purposes, the same machine.
#   `[2026-09-20]` the tool that asks "which CPU core matches David": his states 2/3 loaded
#   directly vs our replay from his state 1 reaching those frames, dynarec vs interpreter.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"; ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SLOT=${1:-1}; TARGET=${2:-0}; TO=${3:-0}; EXTRA="${4:-}"
SRC="${FP_CLIP_DIR:-$ROOT/scripts/fixtures/mvc2/david/2026-09-20T19_54_03Z}"
OUT=$(mktemp -d); if [ -n "${FP_KEEP:-}" ]; then echo "KEEP $OUT"; trap 'cleanup 2>/dev/null' EXIT; else trap 'cleanup 2>/dev/null; rm -rf "$OUT"' EXIT; fi
CLIPDIR=$OUT/data/flycast-dojo/replays/NoBGM_VMU/fpclip; mkdir -p "$CLIPDIR" "$OUT/cfg/flycast-dojo" "$OUT/ctl/_ctl/resp"
cp -r "$SRC"/. "$CLIPDIR"/; cp "$ROOT/scripts/fixtures/mvc2/vmu_save_A1.bin" "$OUT/data/flycast-dojo/"
FLYR=$(ls "$CLIPDIR"/*.flyr | head -1)
DN=$((205 + ($$ % 40))); while [ -e "/tmp/.X11-unix/X$DN" ]; do DN=$((DN+1)); done; D=":$DN"
nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
# shellcheck disable=SC2086
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
  -config dojo:UiIni=no -config audio:backend=null $EXTRA -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
  -config dojo:Replay=yes -config "dojo:ReplayFilename=$FLYR" -config "dojo:AutoSeekState=$SLOT" \
  -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
  -config dojo:RecordMatches=yes -config "dojo:SavestateFolder=$CLIPDIR" \
  -config dojo:ControlServer=yes -config "dojo:CtlDir=$OUT/ctl" \
  -config window:width=1280 -config window:height=900 -config window:fullscreen=no \
  "$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 1; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
trap 'cleanup; exit 143' TERM INT
BASE=$OUT/ctl/_ctl; LOG=$OUT/out.log; SEQ=1
send() { local seq="$1" verb="$2" args="${3:-{\}}" i; printf '{"seq":%s,"verb":"%s","args":%s}\n' "$seq" "$verb" "$args" > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; for i in $(seq 1 150); do [ -f "$BASE/resp/$seq.json" ] && { cat "$BASE/resp/$seq.json"; return 0; }; kill -0 "$FC" 2>/dev/null || return 1; sleep 0.2; done; return 1; }
field() { python3 -c "import json,sys; d=json.loads(sys.argv[1]); print(d.get(sys.argv[2]))" "$1" "$2" 2>/dev/null; }
rd() { local a; a=$(send $SEQ read "{\"addr\":\"$1\",\"width\":${2:-1}}"); SEQ=$((SEQ+1)); RDV=$(field "$a" value); }
# NO COMMAND SUBSTITUTION around anything that bumps SEQ: `f=$(frame)` ran the bump in a subshell,
# the parent's SEQ fell behind, and every later `send` read an already-answered seq's stale
# response - the step verb was never sent and the fingerprint sat on the slot's frame forever
# (`[MEASURED 2026-09-20]`, the same bug the send builder found in the harness's rd()).
q() { local r; r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); QF=$(field "$r" frame); QP=$(field "$r" paused); }
fp() {	# the fingerprint - sets FPV (never call inside $(...)): P1 A/B/C + P2 A state/attack/anim/health/dist, meters, object counts, timer, skip, combo
  local line="" k a w
  for k in 0x2C268510 0x2C2684E1 0x2C268484:2 0x2C268760 0x2C2685D8:4 0x2C269058 0x2C269029 0x2C268FCC:2 0x2C269BA0 0x2C269B71 0x2C268AB4 0x2C268A85 0x2C268A28:2 0x2C268D04 0x2C268B7C:4 0x2C28964A 0x2C28964B 0x2C28966C:2 0x2C287DDE 0x2C287DDF 0x2C289630 0x2C289620 0x2C289621 0x2C2685A0; do
    a=${k%%:*}; w=${k##*:}; [ "$w" = "$k" ] && w=1; rd "$a" "$w"; line="$line $RDV"; done
  FPV="$line"
}
for i in $(seq 1 150); do sleep 1; tr -d '\0' < "$LOG" 2>/dev/null | grep -aq 'TAS: replay seek to movie frame' && break; done
sleep 3
printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; sleep 1
# land EXACTLY on the slot: pause (the replay auto-plays after the seek), then load the slot through ctl
send $SEQ pause '{}' >/dev/null; SEQ=$((SEQ+1)); sleep 1
send $SEQ load "{\"slot\":$SLOT}" >/dev/null; SEQ=$((SEQ+1)); sleep 4
q; f=$QF; fp; echo "FP@$f paused=$QP slot=$SLOT:$FPV"
if [ "$TARGET" -gt "$f" ]; then
  while :; do
    q; f=$QF; [ "$f" -ge "$TARGET" ] && break
    n=$((TARGET - f)); [ "$n" -gt 400 ] && n=400
    send $SEQ step "{\"n\":$n}" >/dev/null; SEQ=$((SEQ+1))
    for i in $(seq 1 60); do sleep 1; q; [ "$QP" = True ] && [ "$QF" -ge $((f + n)) ] && break; done
  done
  q; f=$QF; fp; echo "FP@$f paused=$QP from-slot=$SLOT:$FPV"
fi
if [ "$TO" -gt "$TARGET" ]; then
  # OBJ_COLS / OBJ_HDR override the per-frame columns (addr[:width] list / header) - `[2026-09-20]` the
  # game-code chase needed the registration accumulators and the freeze flag next to the published counts.
  echo "frame ${OBJ_HDR:-combo p1objs p2objs stormState stormAnim stormFlags p2State timer skipCnt}"
  while :; do
    q; f=$QF; [ "$f" -gt "$TO" ] && break
    line="$f"; for k in ${OBJ_COLS:-0x2C2685A0 0x2C287DDE 0x2C287DDF 0x2C269058 0x2C268FCC:2 0x2C268E8E 0x2C268AB4 0x2C289630 0x2C289621}; do a=${k%%:*}; w=${k##*:}; [ "$w" = "$k" ] && w=1; rd "$a" "$w"; line="$line $RDV"; done
    echo "$line"
    send $SEQ step '{"n":1}' >/dev/null; SEQ=$((SEQ+1)); for i in $(seq 1 30); do q; [ "$QP" = True ] && [ "$QF" -gt "$f" ] && break; sleep 0.1; done
  done
fi
# OBJ_SHOT=<file>: screenshot the sandbox display at the last frame (the picture David's video shows, for the eye)
if [ -n "${OBJ_SHOT:-}" ]; then q; DISPLAY="$D" import -window root "$OBJ_SHOT" 2>/dev/null && echo "shot: $OBJ_SHOT @ $QF"; fi
cleanup
