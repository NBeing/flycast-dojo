#!/bin/bash
# boot David's clip (a sandbox copy), seek slot $1, read the cast/health/combo, screenshot.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"; EXE=$ROOT/build-dojo7/flycast; ROM=$HOME/dev/davids_fly/NoBGM_VMU.cdi
SLOT=${1:-1}; SRC=$ROOT/scripts/fixtures/mvc2/david/2026-09-20T19_54_03Z
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
for i in $(seq 1 90); do sleep 1; tr -d '\0' < "$LOG" | grep -aq 'STATE VERIFY\|STATE LIVENESS\|Loaded state\|replay end\|Save state not found' && break; done
sleep 3
tr -d '\0' < "$LOG" | grep -aE 'Loaded state|STATE VERIFY|version|replay seek|replay end|LIVENESS|not found|TAS REPLAY BOOT' | cut -c1-170
printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; sleep 2
r=$(send $SEQ query '{}'); SEQ=$((SEQ+1)); echo "query: frame=$(field "$r" frame) paused=$(field "$r" paused)"
for k in P1_A:0x2C268341 P1_B:0x2C268E89 P1_C:0x2C2699D1 P2_A:0x2C2688E5 P2_B:0x2C26942D P2_C:0x2C269F75; do rd ${k##*:}; echo -n "${k%%:*}=$RDV "; done; echo
rd 0x2C2685A0; echo "comboP1=$RDV"; rd 0x2C268760 2; echo "healthP1=$RDV"; rd 0x2C268764 2; echo "healthP2=$RDV"
DISPLAY="$D" import -window root "$OUT/slot$SLOT.png" 2>/dev/null; echo "shot: $OUT/slot$SLOT.png"
cleanup
