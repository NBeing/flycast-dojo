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
dump() {  # dump <label>
  local lbl="$1" side base cnt k p x y st atk
  echo "== $lbl @ frame $(send $SEQ query '{}' | python3 -c 'import json,sys;print(json.load(sys.stdin)["frame"])')"; SEQ=$((SEQ+1))
  for side in 0 1; do
    if [ $side = 0 ]; then rd 0x2C287DDE; cnt=$RDV; base=0x2C287AEC; else rd 0x2C287DDF; cnt=$RDV; base=0x2C287C64; fi
    echo "  P$((side+1)) objects: $cnt"
    for k in $(seq 0 $((cnt>12?11:cnt-1))); do
      a=$(send $SEQ read "{\"addr\":\"$(printf '0x%X' $((base + k*4)))\",\"width\":4}"); SEQ=$((SEQ+1)); p=$(field "$a" value)
      [ -z "$p" ] || [ "$p" = None ] || [ "$p" = 0 ] && continue
      pp=$(( (p & 0x0FFFFFFF) | 0x2C000000 ))   # guest pointer (8C.. or 2C..) -> Demul form
      a=$(send $SEQ read "{\"addr\":\"$(printf '0x%X' $((pp+0x34)))\",\"width\":4}"); SEQ=$((SEQ+1)); x=$(field "$a" value)
      a=$(send $SEQ read "{\"addr\":\"$(printf '0x%X' $((pp+0x38)))\",\"width\":4}"); SEQ=$((SEQ+1)); y=$(field "$a" value)
      rd $(printf '0x%X' $((pp+0x159))); st=$RDV; rd $(printf '0x%X' $((pp+0x1A1))); atk=$RDV; rd $(printf '0x%X' $((pp+0x110))); fc=$RDV
      echo "    obj$k @$(printf '%08X' $pp) x=$x y=$y state=$st atk=$atk facing=$fc"
    done
  done
}
dump "at FROM ($FROM)"
send $SEQ step "{\"n\":$((TO-FROM))}" >/dev/null; SEQ=$((SEQ+1)); for i in $(seq 1 100); do r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); [ "$(field "$r" paused)" = True ] && [ "$(field "$r" frame)" -ge "$TO" ] && break; sleep 0.2; done
dump "at TO ($TO)"
DISPLAY="$D" import -window root "$OUT/at$TO.png" 2>/dev/null; echo "shot: $OUT/at$TO.png"
cleanup
