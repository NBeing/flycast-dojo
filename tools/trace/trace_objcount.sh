#!/bin/bash
# boot David's clip (a sandbox copy), seek slot $1, read the cast/health/combo, screenshot.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"; EXE=$ROOT/build-dojo7/flycast; ROM=$HOME/dev/davids_fly/NoBGM_VMU.cdi
SLOT=${1:-1}; FROM=${2:-16400}; TO=${3:-16560}; SRC=$ROOT/scripts/fixtures/mvc2/david/2026-09-20T19_54_03Z
OUT=$(mktemp -d); echo "OUT=$OUT"
GAME=NoBGM_VMU; CLIPDIR=$OUT/data/flycast-dojo/replays/$GAME/davidclip; mkdir -p "$CLIPDIR" "$OUT/cfg/flycast-dojo" "$OUT/ctl/_ctl/resp"
cp -r "$SRC"/. "$CLIPDIR"/; cp "$ROOT/scripts/fixtures/mvc2/vmu_save_A1.bin" "$OUT/data/flycast-dojo/"
FLYR=$(ls "$CLIPDIR"/*.flyr | head -1)
DN=$((205 + ($$ % 40))); D=":$DN"; nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
  -config dojo:UiIni=no -config audio:backend=null -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
  -config dojo:Replay=yes -config "dojo:ReplayFilename=$FLYR" -config "dojo:AutoSeekState=$SLOT" \
  -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
  -config dojo:RecordMatches=yes -config "dojo:SavestateFolder=$CLIPDIR" \
  -config dojo:ControlServer=yes -config "dojo:CtlDir=$OUT/ctl" \
  -config window:width=1280 -config window:height=900 -config window:fullscreen=no \
  "$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC"; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID"; }
trap 'cleanup; exit 143' TERM INT
BASE=$OUT/ctl/_ctl; LOG=$OUT/out.log
send() { local seq="$1" verb="$2" args="${3:-{\}}" i; printf '{"seq":%s,"verb":"%s","args":%s}\n' "$seq" "$verb" "$args" > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; for i in $(seq 1 100); do [ -f "$BASE/resp/$seq.json" ] && { cat "$BASE/resp/$seq.json"; return 0; }; kill -0 "$FC" 2>/dev/null || return 1; sleep 0.2; done; return 1; }
field() { python3 -c "import json,sys; d=json.loads(sys.argv[1]); print(d.get(sys.argv[2]))" "$1" "$2" 2>/dev/null; }
SEQ=1; rd() { local a; a=$(send $SEQ read "{\"addr\":\"$1\",\"width\":${2:-1}}"); SEQ=$((SEQ+1)); RDV=$(field "$a" value); }
for i in $(seq 1 90); do sleep 1; tr -d '\0' < "$LOG" | grep -aq 'STATE LIVENESS\|Loaded state' && break; done; sleep 2
printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; sleep 2
send $SEQ set_mode '{"mode":"READ"}' >/dev/null; SEQ=$((SEQ+1))
r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); f=$(field "$r" frame); echo "at $f (paused=$(field "$r" paused))"
# PAUSE first (the replay auto-plays after the seek; a step from a running machine lands late),
# then step the exact delta in chunks, re-reading the frame each time, until FROM.
send $SEQ pause '{}' >/dev/null; SEQ=$((SEQ+1)); sleep 1
while :; do
  r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); f=$(field "$r" frame); [ "$(field "$r" paused)" = True ] || { sleep 0.3; continue; }
  [ "$f" -ge "$FROM" ] && break
  n=$((FROM - f)); [ "$n" -gt 200 ] && n=200
  send $SEQ step "{\"n\":$n}" >/dev/null; SEQ=$((SEQ+1))
  for i in $(seq 1 100); do r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); [ "$(field "$r" paused)" = True ] && [ "$(field "$r" frame)" -ge $((f + n)) ] && break; sleep 0.2; done
done
echo "landed at $f (want $FROM)"
echo "frame combo p1objs p2objs B_state B_animval B_actflags p2_state p2_hitstop"
# slot addresses: A = base, B = +0xB48, C = +0x1690 (the 0x5A4 stride x2 ... use the SPREADSHEET's per-slot columns instead)
declare -A A=( [combo]=0x2C2685A0 [B_state]=0x2C269058 [B_atk]=0x2C269029 [B_point]=0x2C269299 [B_air]=0x2C269081 [p2_state]=0x2C268AB4 [p2_hitstop]=0x2C268A84 [p2_air]=0x2C268ADD [C_state]=0x2C269BA0 [C_atk]=0x2C269B71 )
while :; do
  r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); f=$(field "$r" frame); [ "$f" -gt "$TO" ] && break
  line="$f"; rd 0x2C2685A0; line="$line $RDV"; rd 0x2C287DDE; line="$line $RDV"; rd 0x2C287DDF; line="$line $RDV"; rd 0x2C269058; line="$line $RDV"; rd 0x2C268FCC 2; line="$line $RDV"; rd 0x2C268E8E; line="$line $RDV"; rd 0x2C268AB4; line="$line $RDV"; rd 0x2C268A84; line="$line $RDV"
  echo "$line"
  send $SEQ step '{"n":1}' >/dev/null; SEQ=$((SEQ+1)); for i in $(seq 1 40); do r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); [ "$(field "$r" paused)" = True ] && break; sleep 0.1; done
done
DISPLAY="$D" import -window root "$OUT/at$TO.png" 2>/dev/null; echo "shot: $OUT/at$TO.png"
cleanup
