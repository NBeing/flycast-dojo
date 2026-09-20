#!/usr/bin/env bash
# boothandofftest - the BOOT HANDOFF, measured on the machine: what a boot ARMS is CONSUMED.
#
#   RUN:   scripts/boothandofftest.sh              (needs a ROM, Xvfb, the build, a clip with a State 0)
#   PASS:  exit 0. Three claims, printed `  ok  B<n>  <claim, measured value inlined>` /
#          `  FAIL B<n>  <measured> <why>` / `  SKIP B<n>  <why>`, then
#          `BOOTHANDOFF RESULT: passed=N failed=N skipped=N`:
#            B1 replay boot    - Replay=yes, NO AutoSeekState/AutoPlay: the boot pauses at power-on
#                                and the handoff loads State 0 - `TAS REPLAY BOOT: State 0 loaded at
#                                the boot pause -> frame F` + `TAS READY:`, and ctlserver `query`
#                                reads paused at F == the state's .frame sidecar.
#            B2 macro full     - PlayMacroClip + PlayMacroFile staged (MacroMode+PlayMacro): the
#                                handoff loads State 0 THEN injects the macro RELATIVE to it -
#                                `TAS MACRO FULL: injected N macro frames at State 0's frame F` with
#                                F == the sidecar, and ctlserver `movie` at F reads the macro's
#                                first cell (has=true, p1 == the letter's canon), not neutral.
#            B3 early handoff  - OnEnterFile + OnEnterHandoff=N: `TAS ONENTER: handoff at frame N`
#                                with N < the seed length; paused at >= N.
#   EXIT:  0 every claim ok · 1 a CLAIM failed · 2 usage · 4 --sabotage: the arm FAILED TO FIRE
#          (its target stayed green - the check is decorative) · 77 SKIP (no ROM / clip / Xvfb /
#          build, or a stale binary; a skipped check is not a passing one).
#   ARMS:  --sabotage <class> | --list-sabotage | --self-test (== --sabotage noload). The switch is
#          the RUNNER's (core/dojo/surface_tour.cpp, dojo:TourArm=<cls>) - a test-only TU; the
#          handoff only asks sabotaged() (TEST-PLAN §5.2 rule 2). `noload` skips the State-0 load at
#          the boot pause (B1 must redden: frame stays 0; B2 too: the inject lands at 0; B3 stays
#          green). `absolute` injects the macro at frame 0 instead of State 0's frame (B2 must
#          redden; B1 stays green). Judged by scripts/lib/arms.sh (applied / broke its target /
#          left its control green AND the control ran).
#
# THE FAILURE THIS WOULD HAVE CAUGHT. `[MEASURED 2026-09-17]` docs/PORT-DEFECT-CENSUS.md #1-#5:
# Replay::Init set replay_bootload, LoadMacroFull set macro_fullload + macro_pending,
# SeedOnEnter set boot_ready_arm - and NOTHING read them. Play a Movie froze at power-on and
# never sought State 0 (a human pressed F3; a harness passed AutoSeekState, so no test saw it),
# the Macros panel's "Load full" armed rows that Reset() dropped at the next boot, the READY
# banner never fired. A boot that arms a flag nobody consumes looks exactly like one that
# works, from outside - until you read the frame back.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FIX="$ROOT/scripts/fixtures/mvc2"
ARMS="$ROOT/scripts/lib/arms.sh"
KNOWN_ARMS="noload absolute"

usage() { echo "usage: $0 [--list-sabotage | --sabotage <class> | --self-test]   (exit 2: usage)"; exit 2; }
ARM=""
while [ $# -gt 0 ]; do
	case "$1" in
		--list-sabotage)
			echo "noload      B1 replay boot / B2 macro full - the State-0 load at the boot pause is skipped; B1 must redden (frame stays 0), B3 must stay green"
			echo "absolute    B2 macro full  - the macro is injected at frame 0, not State 0's frame; B2 must redden, B1 must stay green"
			exit 0 ;;
		--sabotage) shift; [ $# -gt 0 ] || usage; ARM="$1" ;;
		--self-test) ARM=noload ;;
		*) usage ;;
	esac
	shift
done
if [ -n "$ARM" ]; then
	case " $KNOWN_ARMS " in *" $ARM "*) ;; *) echo "boothandofftest: unknown sabotage class '$ARM' (known: $KNOWN_ARMS)"; exit 2 ;; esac
fi

command -v python3 >/dev/null || { echo "boothandofftest: SKIP - no python3"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "boothandofftest: SKIP - no Xvfb"; exit $SKIP; }
[ -x "$EXE" ] || { echo "boothandofftest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "boothandofftest: SKIP - no ROM ($ROM)"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "boothandofftest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP; fi

OUT="$(mktemp -d)"
if [ -n "${BOOTHANDOFF_KEEP:-}" ]; then echo "boothandofftest: KEEP OUT=$OUT"; else trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

# ---- the base clip (surfacetourtest's pick, verbatim): one with a stamped State 0 -----------
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
[ -n "$CLIP" ] || CLIP="$(pick_clip)" || { echo "boothandofftest: SKIP - no base clip with a stamped savestate"; exit $SKIP; }
[ -f "$CLIP" ] || { echo "boothandofftest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
# THE GAME NAME COMES FROM THE MOVIE'S FILENAME, not the clip's parent folder. `[MEASURED
# 2026-09-18]` a fixture clip under scripts/fixtures/mvc2/css/base/ staged as replays/css/
# tourclip: the engine (Replay::get_game_name) folders macros under replays/<ROM game>/, so
# the Macros browser never saw the macro WriteMacroFile wrote and `macros: place` reddened.
# The .flyr stem is `<game>__<timestamp>__...`, written by that same engine function.
game_of() { local st; st="$(basename "$1" .flyr)"; case "$st" in *__*) echo "${st%%__*}" ;; *) basename "$(dirname "$(dirname "$1")")" ;; esac; }
SRCDIR="$(dirname "$CLIP")"; GAME="$(game_of "$CLIP")"
CLIPDIR="$OUT/data/flycast-dojo/replays/$GAME/bootclip"	# under the sandbox's own replays/<game>/ root
mkdir -p "$OUT/cfg/flycast-dojo" "$CLIPDIR"
cp "$CLIP" "$CLIPDIR/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$CLIPDIR/" 2>/dev/null; } || true
done
rm -f "$CLIPDIR"/*_[0-9].state "$CLIPDIR"/*_[0-9][0-9].state "$CLIPDIR"/results.json "$CLIPDIR"/*_macro.txt 2>/dev/null || true
# slot 0 = the .state with no _N suffix; its .frame sidecar's first u32 is the movie frame it was taken at
STATE0="$(ls "$CLIPDIR"/*.state 2>/dev/null | grep -v '_[0-9]*\.state$' | head -1)"
[ -n "$STATE0" ] && [ -f "$STATE0.frame" ] || { echo "boothandofftest: SKIP - the clip has no State 0 with a .frame sidecar"; exit $SKIP; }
F0=$(python3 -c "import struct,sys; print(struct.unpack('<I', open(sys.argv[1],'rb').read(4))[0])" "$STATE0.frame")
echo "boothandofftest: base clip $CLIP (State 0 at movie frame $F0)"
[ -f "$FIX/vmu_save_A1.bin" ] && cp "$FIX/vmu_save_A1.bin" "$OUT/data/flycast-dojo/vmu_save_A1.bin"	# B3's seed presumes the card (fixtures-check V1)

# ---- claim bookkeeping (surfacetourtest's grammar) -----------------------------------------
PASSED=0; FAILED=0; SKIPPED=0
SEEN="$OUT/seen"; BROKEN="$OUT/broken"; SKIPIDS="$OUT/skipped"; : > "$SEEN"; : > "$BROKEN"; : > "$SKIPIDS"
claim() {	# claim <id> <ok|FAIL|SKIP> <text>
	echo "$1" >> "$SEEN"
	case "$2" in
		ok)   PASSED=$((PASSED+1));  printf '  ok   %s  %s\n' "$1" "$3" ;;
		FAIL) FAILED=$((FAILED+1));  echo "$1" >> "$BROKEN"; printf '  FAIL %s  %s\n' "$1" "$3" ;;
		SKIP) SKIPPED=$((SKIPPED+1)); echo "$1" >> "$SKIPIDS"; printf '  SKIP %s  %s\n' "$1" "$3" ;;
	esac
}

# ---- one sandboxed boot per claim ----------------------------------------------------------
# boot <tag> <extra -config args...> : sets FC XPID BASE LOG; WHY on failure. Every boot carries
# the control server; the arm (if any) rides as dojo:TourArm so the RUNNER owns the switch.
FC=""; XPID=""; BASE=""; LOG=""; WHY=""; SEQ=1
boot() {
	local tag="$1"; shift
	WHY=""
	local ctl="$OUT/ctl_$tag"; mkdir -p "$ctl/_ctl/resp"
	local DN=$((178 + ($$ % 50))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { WHY="no free display"; return 1; }; done
	local D=":$DN"
	nohup Xvfb "$D" -screen 0 900x700x24 >"$OUT/xvfb_$tag.log" 2>&1 & XPID=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$DN" ] || { kill "$XPID" 2>/dev/null; WHY="Xvfb did not come up on $D"; return 1; }
	local armcfg=(); [ -n "$ARM" ] && armcfg=(-config "dojo:TourArm=$ARM")
	XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
		-config dojo:UiIni=no -config audio:backend=null -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:AutoLoadNetState=no -config dojo:AutoLoadTrainingNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
		-config dojo:ControlServer=yes -config "dojo:CtlDir=$ctl" \
		-config window:width=900 -config window:height=700 -config window:fullscreen=no \
		"${armcfg[@]}" "$@" \
		"$ROM" > "$OUT/out_$tag.log" 2>&1 & FC=$!
	BASE="$ctl/_ctl"; LOG="$OUT/out_$tag.log"; SEQ=1
	return 0
}
teardown() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
# A KILLED HARNESS TAKES ITS OWN EMULATOR WITH IT. `[MEASURED 2026-09-18]` `timeout`/ctest TIMEOUT/
# Ctrl-C ended the harness and left flycast + Xvfb orphaned (reparented to 1, still running,
# killed by hand by PID). The trap is PID-scoped: only $FC and $XPID, never a name.
trap 'teardown; exit 143' TERM INT
logged() { tr -d '\0' < "$LOG" | grep -a -- "$1" > /dev/null; }
logline() { tr -d '\0' < "$LOG" | grep -a -- "$1" | tail -1; }
send() {	# send <seq> <verb> <args-json> -> resp json (ctltest's client). The SEQ is the CALLER's to bump:
			# a `$(send ...)` runs in a subshell, so a bump in here is lost and every command goes out as seq 1.
	local seq="$1" verb="$2" args="${3:-{\}}" i
	printf '{"seq":%s,"verb":"%s","args":%s}\n' "$seq" "$verb" "$args" > "$BASE/cmd.json.tmp"
	mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"
	for i in $(seq 1 100); do
		[ -f "$BASE/resp/$seq.json" ] && { cat "$BASE/resp/$seq.json"; return 0; }
		kill -0 "$FC" 2>/dev/null || return 1
		sleep 0.2
	done
	return 1
}
# prime : ctlserver BASELINES on the first cmd.json it sees for a dir (never executes it - dedup by seq);
# write a seq-0 query first so the real seq 1 is the first one handled (fixtures-check's F4 does the same).
prime() { printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; sleep 2; }
field() { python3 -c "import json,sys; d=json.loads(sys.argv[1]); print(d.get(sys.argv[2]))" "$1" "$2" 2>/dev/null; }
# wait_log <pattern> <seconds> : the engine trace that says the handoff happened
wait_log() { local i; for i in $(seq 1 $(( $2 * 2 ))); do kill -0 "$FC" 2>/dev/null || return 1; logged "$1" && return 0; sleep 0.5; done; return 1; }

# ---- B1 replay boot: the handoff seeks State 0 ---------------------------------------------
b1() {
	boot b1 -config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIPDIR/clip.flyr" -config dojo:RecordMatches=no \
		|| { claim B1 SKIP "replay boot: $WHY"; return; }
	if ! wait_log "TAS READY:" 60; then teardown; claim B1 SKIP "replay boot: no 'TAS READY' within 60 s ($(tr -d '\0' < "$LOG" | grep -a -c '') log lines)"; return; fi
	prime
	local r fr paused ld
	r=$(send $SEQ query "{}") || { teardown; claim B1 SKIP "replay boot: control server not answering"; return; }
	fr=$(field "$r" frame); paused=$(field "$r" paused); ld="$(logline 'TAS REPLAY BOOT:')"
	teardown
	local m="frame=$fr paused=$paused (want $F0, True); $(echo "$ld" | sed 's/.*N\[NETWORK\]: //')"
	if [ "$paused" = True ] && [ "${fr:-x}" = "$F0" ] && echo "$ld" | grep -q 'State 0 loaded at the boot pause'; then
		claim B1 ok "replay boot: the handoff loaded State 0 at the boot pause - $m"
	else claim B1 FAIL "replay boot: State 0 NOT reached at the boot pause - $m"; fi
}

# ---- B2 macro full: State 0 then the macro, RELATIVE ---------------------------------------
b2() {
	local macro="$OUT/small_macro.txt"
	printf 'Z\nZ\n.\nX\n' > "$macro"	# P1 LP, LP, neutral, LK - Z = canon 16 (tasmacro.cpp's table)
	boot b2 -config dojo:MacroMode=yes -config dojo:PlayMacro=yes -config dojo:RecordMatches=no -config dojo:Replay=no \
		-config "dojo:PlayMacroClip=$CLIPDIR" -config "dojo:PlayMacroFile=$macro" -config dojo:PlayMacroStage=no \
		|| { claim B2 SKIP "macro full: $WHY"; return; }
	if ! wait_log "TAS READY:" 60; then teardown; claim B2 SKIP "macro full: no 'TAS READY' within 60 s ($(logline 'TAS MACRO FULL' | sed 's/.*N\[NETWORK\]: //'))"; return; fi
	prime
	local r fr paused inj injf mv has p1
	r=$(send $SEQ query "{}") || { teardown; claim B2 SKIP "macro full: control server not answering"; return; }
	fr=$(field "$r" frame); paused=$(field "$r" paused)
	inj="$(logline 'TAS MACRO FULL: injected')"; injf=$(echo "$inj" | sed -n "s/.*at State 0's frame \([0-9]*\).*/\1/p")
	SEQ=$((SEQ+1)); mv=$(send $SEQ movie "{\"frame\":$F0}"); has=$(field "$mv" has); p1=$(field "$mv" p1)
	teardown
	local m="frame=$fr paused=$paused injected_at=${injf:-none} (want $F0); movie[$F0]: has=$has p1=$p1 (want True, 16)"
	if [ "$paused" = True ] && [ "${fr:-x}" = "$F0" ] && [ "${injf:-x}" = "$F0" ] && [ "$has" = True ] && [ "${p1:-0}" = 16 ]; then
		claim B2 ok "macro full: State 0 loaded, then the macro injected RELATIVE to it - $m"
	else claim B2 FAIL "macro full: the macro did not land on State 0's frame - $m"; fi
}

# ---- B3 early handoff: OnEnterHandoff=N pauses N frames into the seed ---------------------
b3() {
	local seed="$FIX/snippets/fastVS.txt" n
	[ -f "$seed" ] || { claim B3 SKIP "early handoff: no seed ($seed)"; return; }
	n=$(grep -v '^#' "$seed" | grep -c .)
	boot b3 -config dojo:Training=yes -config dojo:RecordMatches=yes -config dojo:Replay=no \
		-config "dojo:OnEnterFile=$seed" -config dojo:OnEnterHandoff=300 \
		|| { claim B3 SKIP "early handoff: $WHY"; return; }
	if ! wait_log "TAS ONENTER: handoff at frame" 60; then teardown; claim B3 SKIP "early handoff: no handoff within 60 s ($(logline 'TAS ONENTER' | sed 's/.*N\[NETWORK\]: //'))"; return; fi
	prime
	local r fr paused hf
	r=$(send $SEQ query "{}") || { teardown; claim B3 SKIP "early handoff: control server not answering"; return; }
	fr=$(field "$r" frame); paused=$(field "$r" paused); hf=$(logline 'TAS ONENTER: handoff at frame' | sed -n 's/.*handoff at frame \([0-9]*\).*/\1/p')
	teardown
	local m="handoff=$hf frame=$fr paused=$paused (want 300, >=300 and <$n, True)"
	if [ "$paused" = True ] && [ "${hf:-x}" = 300 ] && [ "${fr:-0}" -ge 300 ] && [ "${fr:-0}" -lt "$n" ]; then
		claim B3 ok "early handoff: OnEnterHandoff=300 paused 300 frames into a $n-frame seed - $m"
	else claim B3 FAIL "early handoff: did not pause at the requested frame - $m"; fi
}

[ -n "$ARM" ] && echo "SABOTAGE arm $ARM: the run below is EXPECTED to be red"
b1; b2; b3
echo "BOOTHANDOFF RESULT: passed=$PASSED failed=$FAILED skipped=$SKIPPED"

# ---- the verdict, armed or not -------------------------------------------------------------
if [ -n "$ARM" ]; then
	[ -x "$ARMS" ] || { echo "boothandofftest: no judge at $ARMS"; exit 2; }
	case "$ARM" in
		noload)   target=B1; control=B3; what="the State-0 load at the boot pause skipped" ;;
		absolute) target=B2; control=B1; what="the macro injected at frame 0, not State 0's frame" ;;
	esac
	if grep -a "^$target$" "$SKIPIDS" > /dev/null; then
		grep -av "^$target$" "$SEEN" > "$SEEN.t"; mv "$SEEN.t" "$SEEN"
	fi
	"$ARMS" judge "$ARM" "$what" "$target" "$control" "$FAILED" "$SEEN" "$BROKEN"; j=$?
	case "$j" in
		0) echo "PASS boothandofftest --sabotage $ARM - the arm fired as predicted ($target reddened, $control stayed green)"; exit 0 ;;
		2) echo "INCONCLUSIVE boothandofftest --sabotage $ARM - $target could not run, so this arm proves nothing (exit 2)"; exit 2 ;;
		*) if grep -aq "^$target$" "$BROKEN"; then echo "FAIL boothandofftest --sabotage $ARM - it fired but broke its control $control, or the tally lies"; exit 1; fi
		   echo "FAIL boothandofftest --sabotage $ARM - the arm did NOT fire: $target stayed green, the check is decorative (exit 4)"; exit 4 ;;
	esac
fi
if [ "$SKIPPED" -gt 0 ]; then echo "SKIP boothandofftest - $SKIPPED claim(s) could not run (a skipped check is not a passing one)"; exit $SKIP; fi
if [ "$FAILED" -eq 0 ]; then echo "PASS boothandofftest - every claim held (the boot consumes what it arms)"; exit 0; fi
echo "FAIL boothandofftest - $FAILED claim(s) red"; exit 1
