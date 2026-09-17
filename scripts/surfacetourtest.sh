#!/usr/bin/env bash
# surfacetourtest - the SURFACE TOUR: a self-driving walk of the whole TAS surface.
#
#   RUN:   scripts/surfacetourtest.sh              (needs a ROM, Xvfb, python3, a base clip)
#   PASS:  exit 0. The tour loads David's savestate, REBINDS every window's hotkey through
#          the real rebind engine, opens and closes every window WITH those hotkeys, then
#          exercises each feature one by one. Eight gates, each printed as
#          `  ok  G<n>  <claim, measured count inlined>` or `  FAIL G<n>  <measured> <why>`
#          (the grammar lifted from nbneo-rr's tests/transport-target-check.py):
#            G1 the tour reported a GATE (RESULT carries gate_ok/vacuous/leak/unmeasured)
#            G2 no step FAILED                       G3 no mover was VACUOUS
#            G4 no UI step LEAKED into the machine   G5 the gate measured >= FLOOR steps
#            G6 passed >= FLOOR                       G7 every settled step got a gate verdict
#            G8 the ENGINE's own traces back the tour: >= 14 `PANEL TOGGLE: ... -> open`,
#               >= 14 `HOTKEY REBIND: action ... -> `, >= 2 `gui_loadState: slot`
#          then one summary line: `PASS  ...` / `FAIL  N check(s) red: G2, G4` /
#          `VACUOUS  N check(s) red: G1, G5`.
#   EXIT:  0 pass · 1 a CLAIM failed (G2/G4: a step failed, or a UI step moved the
#          machine) · 2 usage · 4 --self-test: the sabotage FAILED TO FIRE - the gate is
#          decorative · 5 VACUOUS (G1/G3/G5/G6/G7/G8: the run proved nothing - no gate,
#          a mover that moved nothing, a floor missed, or the engine never corroborated a
#          PASS) - its OWN code, because "0 failed" is also what a tour that never ran
#          looks like · 77 SKIP (no ROM / Xvfb / python3 / build / base clip, or the tour
#          aborted before it could run - no keyboard device, never ready).
#   ARMS:  scripts/surfacetourtest.sh --sabotage <class>   (--self-test == --sabotage open;
#          --list-sabotage prints the classes). dojo:SurfaceTour=sabotage:<class> restores
#          ONE measured defect (surface_tour.h v2: open rebind show write-clobber
#          gate-can-pass flip label save branch) and the run is judged by the lifted
#          arms.lua rules in scripts/lib/arms.sh: the arm APPLIED (failed >= 1), it BROKE
#          the one step it targets, and it LEFT its control step green - AND the control
#          RAN. The runner declares target and control (`SURFACE TOUR: arm <class>
#          must_break="…" must_not_break="…"`); a fallback table applies, loudly, only if
#          it does not. Then the gate must have read the arm the way its class predicts
#          (vacuous=0 leak=0, except flip: leak >= 1 IS the skipped undo). INVERTED EXITS:
#          0 fired as predicted · 4 FAILED TO FIRE (the target stayed green: the guard is
#          decorative) · 2 INCONCLUSIVE (the target never ran; write-clobber may be
#          inconclusive-by-design on a fixture the phase does not clobber) · 1 it fired
#          but BROKE ITS CONTROL, or the tally lies · 5 no gate. `gate-can-pass` is the
#          inverse: armed plumbing, nothing broken, the whole gate must go green (else 4).
#   WATCH: scripts/surfacetourtest.sh --watch <clip.flyr> - the HUMAN-VERIFIED run on
#          your real display. A COPY of the clip is staged in a throwaway config+data
#          sandbox and dojo:SavestateFolder points at the copy, so your emu.cfg, key
#          mappings, window layout and the real clip are never touched. No Xvfb, no
#          synthesised input: the tour drives itself in-process. Keep your hands off the
#          keyboard for ~2-3 minutes - an armed rebind would capture a real key.
#
# WHY IN-PROCESS. A key is "pressed" by the tour calling the keyboard device's own
# gamepad_btn_input(code, pressed) - the exact entry SDL uses - so rebinding and the
# hotkey take the genuine path headless AND on screen, and nothing is ever synthesised on
# a real X display (the standing rule). Pacing is dojo:TourBpmMs (click, 1 s) and
# dojo:TourRecordMs (record, 2 s) so a person can follow the on-screen banner.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
# The pass floor: scored steps minus the optional ones. `[MEASURED 2026-09-17]` the
# default tour is 70 steps (66 + a 1-frame "show" after each of the four loads, so a
# human actually sees the loaded picture), 2 of them optional (captures: start/stop,
# which SKIP without a recorder), so 68 must PASS. Overridable for a partial tour.
FLOOR="${TOUR_FLOOR:-75}"	# 77 steps since the studio blanket triplet (2026-09-17; 74 before it); two of slack, as before
SELF=0; WATCH=0; WATCHCLIP=""; ARM=""
# The classes this harness knows how to judge - MIRRORS surface_tour.h v2 and is checked
# against the runner's own `SURFACE TOUR: arms known:` line on every armed run, so the
# two lists cannot drift silently.
KNOWN_ARMS="open+rebind+show+write-clobber+gate-can-pass+flip+label+save+branch+base"
usage() { echo "usage: $0 [--self-test | --sabotage <class> | --list-sabotage | --watch <clip.flyr>]   (exit 2: usage)"; exit 2; }
case "${1:-}" in
	"")               ;;
	--self-test)      ARM=open ;;
	--sabotage)       ARM="${2:-}"; [ -n "$ARM" ] || usage ;;
	--list-sabotage)  printf '%s\n' "${KNOWN_ARMS//+/ }" | tr ' ' '\n' | sed 's/^/ARM /'; exit 0 ;;
	--watch)          WATCH=1; WATCHCLIP="${2:-}" ;;
	*)                usage ;;
esac
if [ -n "$ARM" ]; then
	case "+$KNOWN_ARMS+" in *"+$ARM+"*) ;; *) echo "no such arm '$ARM'; have: ${KNOWN_ARMS//+/, }"; exit 2 ;; esac
	SELF=1
fi

if [ -n "${SURFACETOURTEST_OUT:-}" ]; then OUT="$SURFACETOURTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "surfacetourtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "surfacetourtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v python3 >/dev/null || { echo "surfacetourtest: SKIP - no python3"; exit $SKIP; }
if [ "$WATCH" -eq 0 ]; then
	command -v Xvfb >/dev/null || { echo "surfacetourtest: SKIP - no Xvfb"; exit $SKIP; }
fi
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "surfacetourtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# ---- the base clip: one of David's, with a prefixHash-stamped savestate (slot 0 = BASE) ----
CLIP="${FLYCAST_TEST_CLIP:-}"
[ "$WATCH" -eq 1 ] && [ -n "$WATCHCLIP" ] && CLIP="$WATCHCLIP"
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
[ -n "$CLIP" ] || CLIP="$(pick_clip)" || { echo "surfacetourtest: SKIP - no base clip with a stamped savestate"; exit $SKIP; }
[ -f "$CLIP" ] || { echo "surfacetourtest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
echo "surfacetourtest: base clip $CLIP"

# A COPY, always. The tour writes slot 99, a branch, a lab test and a capture into the
# clip folder; the sandbox is what keeps the user's real clip, cfg and mappings untouched.
#
# STAGED UNDER THE SANDBOX'S OWN replays/<game>/ ROOT, not a loose "$OUT/clip".
# `[MEASURED 2026-09-17]` the Macros browser scans <data>/flycast-dojo/replays/<game>/
# (see scripts/macrostest.sh), so a clip staged anywhere else makes "macros: place"
# FAIL with "no macro found even after seeding" while the seed file sits right there.
# GAME is the source clip's grandparent dir - what get_game_name() produced when it
# was recorded - so the copy lands exactly where the scanner looks.
GAME="$(basename "$(dirname "$SRCDIR")")"
CLIPDIR="$OUT/data/flycast-dojo/replays/$GAME/tourclip"
mkdir -p "$OUT/cfg/flycast-dojo" "$CLIPDIR"
cp "$CLIP" "$CLIPDIR/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$CLIPDIR/" 2>/dev/null; } || true
done
rm -f "$CLIPDIR"/*_[0-9].state "$CLIPDIR"/*_[0-9][0-9].state "$CLIPDIR"/results.json "$CLIPDIR"/*_macro.txt 2>/dev/null || true

XPID=""
if [ "$WATCH" -eq 1 ]; then
	D="${DISPLAY:-}"
	[ -n "$D" ] || { echo "surfacetourtest: --watch needs a real DISPLAY"; exit $SKIP; }
	echo "surfacetourtest: WATCH mode on $D - HANDS OFF THE KEYBOARD for ~2-3 minutes."
	echo "  (an armed rebind would capture a real key; the tour drives itself in-process)"
	echo "  your emu.cfg, mappings, layout and the real clip are untouched: sandbox = $OUT"
else
	DN=$((160 + ($$ % 80))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 250 ] && { echo "surfacetourtest: SKIP - no free display"; exit $SKIP; }; done
	D=":$DN"
	nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$DN" ] || { echo "surfacetourtest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }
fi

MODE=yes; [ "$SELF" -eq 1 ] && MODE="sabotage:$ARM"
CONSOLE=(-config dojo:NativeConsole=no); [ "$WATCH" -eq 1 ] && CONSOLE=()
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no "${CONSOLE[@]}" -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$CLIPDIR/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:RecordMatches=yes -config dojo:HotkeyTrace=yes \
	-config "dojo:SurfaceTour=$MODE" \
	-config "dojo:TourBpmMs=${TOUR_BPM_MS:-1000}" -config "dojo:TourRecordMs=${TOUR_RECORD_MS:-2000}" \
	-config "dojo:TourSlow=${TOUR_SLOW:-no}" -config "dojo:SavestateFolder=$CLIPDIR" \
	-config window:width=1280 -config window:height=900 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; [ -n "$XPID" ] && kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; [ -n "$XPID" ] && kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 "${TOUR_WAIT_S:-300}"); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"

# THE HUMAN-READABLE REPORT: every step as the tour narrated it, every gate line that was
# NOT ok, and the runner's own G-summary block (two spaces, ok, two spaces - the lifted
# grammar; the runner emits it, this prints it). `tr -d '\0'` first: the emulator writes
# NUL bytes, and this shell's grep may be ugrep with -I. Never pipe an evidence grep
# into head - six runs of one unchanged command once returned 0,12,0,0,0,10 that way.
STRIP='s/.*[NW]\[[A-Z]*\]: /  /'
tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR: step\|SURFACE TOUR: ready\|SURFACE TOUR: restored\|SURFACE TOUR RESULT\|SURFACE TOUR: aborted" | sed "$STRIP"
tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR: gate .* -> \(VACUOUS\|LEAK\|unmeasured\)" | sed "$STRIP"
tr -d '\0' < "$OUT/out.log" | grep -a "\]:   ok  G[0-9]\|\]:   FAIL G[0-9]\|\]:            [^ ]" | sed 's/.*[NW]\[[A-Z]*\]: /  runner: /'

if [ "$WATCH" -eq 1 ]; then
	# Leave the emulator up so the verdict table can be read; the user closes it.
	echo "surfacetourtest: WATCH run finished - the emulator stays open with the verdict table. Close it when done."
	wait "$FC" 2>/dev/null
	exit 0
fi
cleanup; sleep 1

case "$RESULT" in
	"")            echo "surfacetourtest: SKIP - the tour never reported (see $OUT/out.log)"; exit $SKIP ;;
	*"aborted -"*) echo "surfacetourtest: SKIP - ${RESULT#*SURFACE TOUR: }"; exit $SKIP ;;
esac
field() { echo "$RESULT" | sed -n "s/.*$1=\([0-9]*\).*/\1/p"; }
passed=$(field passed); failed=$(field failed); skipped=$(field skipped); total=$(field total)
# The gate fields are APPEND-ONLY on the RESULT line; absent = the runner has no gate.
gate_ok=$(field gate_ok); vacuous=$(field vacuous); leak=$(field leak); unmeasured=$(field unmeasured)
LOG="$(tr -d '\0' < "$OUT/out.log")"
opens=$(printf '%s\n' "$LOG"   | grep -ac "PANEL TOGGLE: .* -> open")
rebinds=$(printf '%s\n' "$LOG" | grep -ac "HOTKEY REBIND: action .* -> ")
loads=$(printf '%s\n' "$LOG"   | grep -ac "gui_loadState: slot")
rebindPass=$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* "rebind: ' | grep -ac -- "-> PASS")
echo "surfacetourtest: passed=$passed failed=$failed skipped=$skipped total=$total gate_ok=${gate_ok:--} vacuous=${vacuous:--} leak=${leak:--} unmeasured=${unmeasured:--} | engine traces: opens=$opens rebinds=$rebinds loads=$loads | rebind steps PASS=$rebindPass (mode=$MODE floor=$FLOOR)"

# ---- THE GATES, in the lifted grammar ----------------------------------------------
# Two red classes, two exit codes: a CLAIM failed (exit 1) is not the same finding as
# "this run proved nothing" (exit 5, VACUOUS), and "0 failed" is also what a tour that
# never ran looks like. Each gate prints its measured number whether it passed or not;
# a red gate names its offenders on indented lines beneath it; the summary lists the red
# gate names so one grep reads the verdict.
FAILRED=(); VACRED=()
G() {	# G <n> <ok 1|0> <class fail|vac> <ok-claim> <fail-text> [offender lines via stdin]
	local n=$1 ok=$2 cls=$3 claim=$4 failtxt=$5
	if [ "$ok" = 1 ]; then printf '  ok  G%s  %s\n' "$n" "$claim"; return; fi
	printf '  FAIL G%s  %s\n' "$n" "$failtxt"
	[ -t 0 ] || sed 's/^/           /'
	if [ "$cls" = fail ]; then FAILRED+=("G$n"); else VACRED+=("G$n"); fi
}
summary() {	# prints the one-line verdict and exits with its code
	local what=$1
	if [ "${#FAILRED[@]}" -gt 0 ]; then
		echo "FAIL  ${#FAILRED[@]} check(s) red: $(IFS=', '; echo "${FAILRED[*]}")$([ "${#VACRED[@]}" -gt 0 ] && echo " (and vacuous: $(IFS=', '; echo "${VACRED[*]}"))") - $what"; exit 1
	fi
	if [ "${#VACRED[@]}" -gt 0 ]; then
		echo "VACUOUS  ${#VACRED[@]} check(s) red: $(IFS=', '; echo "${VACRED[*]}") - $what proved nothing"; exit 5
	fi
	echo "PASS  $what"; exit 0
}
gatePresent=0; [ -n "$gate_ok" ] && [ -n "$vacuous" ] && [ -n "$leak" ] && [ -n "$unmeasured" ] && gatePresent=1

if [ "$SELF" -eq 1 ]; then
	# ---- THE SABOTAGE JUDGE (scripts/lib/arms.sh - emuapi's arms.lua rules, lifted) ------
	# The arm must have APPLIED (failed > 0), BROKEN the one step it targets, and LEFT
	# its control step green - and the control must have RUN. The runner declares
	# target and control itself (`SURFACE TOUR: arm <cls> must_break="…"
	# must_not_break="…"`); the table below is only a fallback for a runner that has not
	# learned to, and says so. Inverted exits: 0 the arm fired as predicted · 4 it FAILED
	# TO FIRE (target still green - the gate is decorative) · 2 INCONCLUSIVE (the target
	# never ran, or the arm is inconclusive-by-design on this fixture) · 1 it fired but
	# BROKE ITS CONTROL (a demolition, not a measurement) or the tally lies.
	known="$(printf '%s\n' "$LOG" | sed -n 's/.*SURFACE TOUR: arms known: \([^ ]*\).*/\1/p' | tail -1)"
	if [ -n "$known" ] && [ "$known" != "$KNOWN_ARMS" ]; then
		echo "  note  the runner knows arms '$known' but this harness lists '$KNOWN_ARMS' - update KNOWN_ARMS / --list-sabotage"
	fi
	armedLine="$(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: sabotage armed: " | tail -1 | sed "$STRIP")"
	[ -n "$armedLine" ] && echo "$armedLine"
	decl="$(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: arm $ARM must_break=" | tail -1)"
	if [ -n "$decl" ]; then
		MB="$(printf '%s\n' "$decl" | sed -n 's/.*must_break="\([^"]*\)".*/\1/p')"
		MNB="$(printf '%s\n' "$decl" | sed -n 's/.*must_not_break="\([^"]*\)".*/\1/p')"
		echo "  arm $ARM declared by the runner: must_break='$MB' must_not_break='$MNB'"
	else
		case "$ARM" in
			open)          MB="open: pianoroll";                     MNB="rebind: pianoroll -> Ctrl+F1" ;;
			rebind)        MB="rebind: macros -> Alt+F6";            MNB="rebind: snippets -> Alt+F5" ;;
			show)          MB="show: David's base state (1 frame)";  MNB="load slot 0 (David's base)" ;;
			write-clobber) MB="show: slot 99 (1 frame)";             MNB="savestate: load slot 99" ;;
			gate-can-pass) MB="";                                    MNB="open: pianoroll" ;;
			flip)          MB="roll: flip a cell + undo";            MNB="states: label round-trip" ;;
			label)         MB="states: label round-trip";           MNB="roll: flip a cell + undo" ;;
			save)          MB="savestate: save slot 99";             MNB="states: label round-trip" ;;
			branch)        MB="branch: create from slot 0";          MNB="test lab: add test from slot 0" ;;
		esac
		echo "  note  the runner did not declare arm '$ARM' (no 'SURFACE TOUR: arm $ARM must_break=' line) - judging from this harness's FALLBACK table: must_break='$MB' must_not_break='$MNB'"
	fi
	case "$ARM" in
		open)          WHAT="the WRONG chord on the piano roll's open step" ;;
		rebind)        WHAT="a rebind press inside the engine's 0.2 s deaf window" ;;
		show)          WHAT="the 1-frame show step skipped, frame+1 faked" ;;
		write-clobber) WHAT="the feature phase run in WRITE (a load in READ-WRITE rewinds the recording)" ;;
		gate-can-pass) WHAT="NOTHING broken - the whole gate must go GREEN under the sabotage plumbing" ;;
		flip)          WHAT="the roll edit's UNDO skipped while reporting success (the movie keeps the flipped row)" ;;
		label)         WHAT="setSlotLabel skipped, the intended label reported as written" ;;
		save)          WHAT="the scratch save lands in slot 98 while claiming slot 99" ;;
		branch)        WHAT="tas_branch::create skipped, the create reported as done" ;;
		base)          WHAT="the BASE guard skipped - F1 SAVES ON PRESS, the tap step writes slot 0 through gui_saveState()" ;;
	esac
	# SEEN = every step that RAN (PASS or FAIL); a SKIPped control did not run, and a
	# control that did not run cannot be "left green" (arms.lua rule 3).
	printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step [0-9]*/[0-9]* "' \
		| sed 's/.*SURFACE TOUR: step [0-9]*\/[0-9]* "\(.*\)" -> \(PASS\|FAIL\|SKIP\).*/\1\t\2/' > "$OUT/steps.tsv"
	awk -F'\t' '$2=="PASS"||$2=="FAIL"{print $1}' "$OUT/steps.tsv" > "$OUT/seen"
	awk -F'\t' '$2=="FAIL"{print $1}'               "$OUT/steps.tsv" > "$OUT/broken"

	if [ "$ARM" = write-clobber ] && printf '%s\n' "$LOG" | grep -aq "inconclusive-by-design"; then
		printf '%s\n' "$LOG" | grep -a "inconclusive-by-design" | sed "$STRIP"
		echo ""; echo "=== SABOTAGE $ARM: $WHAT"
		echo "  SKIP  the runner reports this arm inconclusive-by-design on this fixture (the phase did not clobber)"
		echo "SABOTAGE INCONCLUSIVE"; exit 2
	fi
	if [ "$ARM" = gate-can-pass ]; then
		# The inverse arm: armed plumbing, nothing broken, and the WHOLE gate must be green -
		# so an arm can never be "satisfied" by a run that reddens everything.
		echo ""; echo "=== SABOTAGE $ARM: $WHAT"
		bad=0
		ok() { if [ "$1" = 1 ]; then echo "  ok    $2"; else echo "  FAIL  $2"; bad=$((bad+1)); fi; }
		ok "$([ "$MODE" != yes ] && printf '%s' "$RESULT" | grep -q "mode=sabotage" && echo 1 || echo 0)" "the run was ARMED (RESULT mode=sabotage)"
		ok "$([ "${failed:-1}" -eq 0 ] && echo 1 || echo 0)" "no step failed -- failed=$failed"
		ok "$([ "${passed:-0}" -ge "$FLOOR" ] && echo 1 || echo 0)" "passed=$passed >= floor $FLOOR"
		ok "$gatePresent" "the tour reported a gate -- gate_ok=${gate_ok:--} vacuous=${vacuous:--} leak=${leak:--} unmeasured=${unmeasured:--}"
		ok "$([ "$gatePresent" = 1 ] && [ "$vacuous" -eq 0 ] && [ "$leak" -eq 0 ] && [ "$gate_ok" -ge "$FLOOR" ] && echo 1 || echo 0)" "the gate is GREEN -- vacuous=${vacuous:--} leak=${leak:--} gate_ok=${gate_ok:--} (>= $FLOOR)"
		ok "$(printf '%s\n' "$LOG" | grep -aq "\]:   FAIL G[0-9]" && echo 0 || echo 1)" "the runner's own G-block has no FAIL line"
		if [ "$bad" -eq 0 ]; then echo "SABOTAGE BEHAVED AS PREDICTED"; exit 0; fi
		echo "SABOTAGE DID NOT BEHAVE AS PREDICTED: $bad"
		echo "gate-can-pass: the gate did not go green under the sabotage plumbing -> exit 4"; exit 4
	fi

	"$ROOT/scripts/lib/arms.sh" judge "$ARM" "$WHAT" "$MB" "$MNB" "${failed:-0}" "$OUT/seen" "$OUT/broken"; J=$?
	if [ "$J" -eq 2 ]; then exit 2; fi
	if [ "$J" -ne 0 ]; then
		if ! grep -qF -- "$MB" "$OUT/broken"; then
			echo "SABOTAGE FAILED TO FIRE: '$MB' stayed green under arm '$ARM'; failed=$failed. The guard this arm tests is decorative. -> exit 4"; exit 4
		fi
		echo "sabotage '$ARM' fired but did not behave: it broke its control or the tally lies -> exit 1"; exit 1
	fi
	# ---- the arm behaved; now the GATE must have read it the way the class predicts ------
	if [ "$ARM" != rebind ]; then
		G 2 "$([ "$rebindPass" -ge 14 ] && echo 1 || echo 0)" fail \
			"the setup held: $rebindPass/14 rebinds still PASSED" \
			"$rebindPass/14 rebinds PASSED - the sabotage reddened the SETUP, not the claim" </dev/null
	fi
	G 1 "$gatePresent" vac "the tour reported a gate (gate_ok=$gate_ok vacuous=$vacuous leak=$leak unmeasured=$unmeasured)" \
		"RESULT carries no gate fields - the runner has no gate, so this arm cannot say what the gate made of the sabotage" </dev/null
	if [ "$gatePresent" = 1 ]; then
		# PER CLASS: most arms redden a claim and must NOT be filed as a vacuity or a leak.
		# `flip` is the exception by its mechanism - `[MEASURED 2026-09-17]` a skipped UNDO
		# leaves the flipped row in the movie, the movie hash moves, and the gate's net-zero
		# expectation for that step reads as a LEAK-shaped verdict: leak >= 1 is the arm
		# WORKING, not a second defect. See docs/TEST-PLAN.md §5.2.
		case "$ARM" in
			show)
				# `[MEASURED 2026-09-17, Track A]` a skipped 1-frame step is a mover that moved
				# nothing: the gate's G3 files it VACUOUS - that IS the second instrument catching
				# it (beside the step's own "frame N, expected N+1"), so vacuous >= 1 is the arm working.
				G 4 "$([ "$vacuous" -ge 1 ] && echo 1 || echo 0)" fail "the skipped show was filed as a vacuity (vacuous=$vacuous >= 1)" \
					"vacuous=$vacuous - the gate did not notice a 1-frame step that advanced no frame" < <(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: gate .* "show: ' | sed "$STRIP")
				G 5 "$([ "$leak" -eq 0 ] && echo 1 || echo 0)" fail "no UI step leaked (leak=0)" "leak=$leak - a UI step moved the machine or the movie" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> LEAK" | sed "$STRIP") ;;
			flip)
				G 4 "$([ "$vacuous" -eq 0 ] && echo 1 || echo 0)" fail "no step was filed as a vacuity (vacuous=0)" \
					"vacuous=$vacuous - the gate filed the skipped UNDO under 'moved nothing'" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> VACUOUS" | sed "$STRIP")
				G 5 "$([ "$leak" -ge 1 ] && echo 1 || echo 0)" fail "the skipped UNDO showed up as a movie leak (leak=$leak >= 1)" \
					"leak=$leak - the movie hash did not move: the flipped row was undone after all, or the oracle is not looking at the movie" < <(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: gate .* "roll: flip' | sed "$STRIP") ;;
			*)
				G 4 "$([ "$vacuous" -eq 0 ] && echo 1 || echo 0)" fail "the sabotaged step was judged a FAIL, not a vacuity (vacuous=0)" \
					"vacuous=$vacuous - the gate filed a sabotaged step under 'moved nothing' instead of FAIL" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> VACUOUS" | sed "$STRIP")
				G 5 "$([ "$leak" -eq 0 ] && echo 1 || echo 0)" fail "no UI step leaked (leak=0)" "leak=$leak - a UI step moved the machine or the movie" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> LEAK" | sed "$STRIP") ;;
		esac
	fi
	if [ "${#FAILRED[@]}" -eq 0 ] && [ "${#VACRED[@]}" -eq 0 ]; then
		echo "PASS  sabotage '$ARM' fired as predicted: '$MB' went red, '$MNB' stayed green, rebinds $rebindPass/14, failed=$failed, gate vacuous=$vacuous leak=$leak. exit 0."
		exit 0
	fi
	summary "surfacetourtest --sabotage $ARM"
fi

# ---- normal mode --------------------------------------------------------------------
G 1 "$gatePresent" vac "the tour reported a gate (gate_ok=$gate_ok vacuous=$vacuous leak=$leak unmeasured=$unmeasured)" \
	"RESULT carries no gate fields (gate_ok/vacuous/leak/unmeasured) - the runner has no gate, so a PASS here is the tour taking its own word" </dev/null
G 2 "$([ "${failed:-1}" -eq 0 ] && echo 1 || echo 0)" fail "no step failed ($total settled: $passed passed, $skipped skipped)" \
	"$failed of $total step(s) failed:" < <(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step .* -> FAIL' | sed "$STRIP")
if [ "$gatePresent" = 1 ]; then
	G 3 "$([ "$vacuous" -eq 0 ] && echo 1 || echo 0)" vac "no mover was vacuous (vacuous=0 of $gate_ok measured)" \
		"$vacuous mover(s) changed NOTHING - a step whose verb did not fire:" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> VACUOUS" | sed "$STRIP")
	G 4 "$([ "$leak" -eq 0 ] && echo 1 || echo 0)" fail "no UI step leaked (leak=0)" \
		"$leak UI step(s) moved the machine or the movie - a chord, a panel or a mode flip reached the guest:" < <(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> LEAK" | sed "$STRIP")
	G 5 "$([ "$gate_ok" -ge "$FLOOR" ] && echo 1 || echo 0)" vac "the gate measured $gate_ok step(s) >= floor $FLOOR (unmeasured=$unmeasured)" \
		"the gate measured only $gate_ok step(s), floor is $FLOOR (unmeasured=$unmeasured) - it compares too little to mean anything" </dev/null
	judged=$(( ${gate_ok:-0} + ${vacuous:-0} + ${leak:-0} + ${unmeasured:-0} ))
	G 7 "$([ "$judged" -eq "${total:-0}" ] && echo 1 || echo 0)" vac "every settled step got a gate verdict ($judged of $total)" \
		"$judged gate verdict(s) for $total settled step(s) - the guard did not fire on every step; a counter reading 0 is what a guard that never ran reports" </dev/null
fi
G 6 "$([ "${passed:-0}" -ge "$FLOOR" ] && echo 1 || echo 0)" vac "passed=$passed >= floor $FLOOR" \
	"passed=$passed, floor is $FLOOR (skipped=$skipped) - the tour is not covering the surface" </dev/null
G 8 "$([ "$opens" -ge 14 ] && [ "$rebinds" -ge 14 ] && [ "$loads" -ge 2 ] && echo 1 || echo 0)" vac \
	"the engine's own traces back the tour: opens=$opens (>=14) rebinds=$rebinds (>=14) loads=$loads (>=2)" \
	"engine traces short: opens=$opens (need 14) rebinds=$rebinds (need 14) loads=$loads (need 2) - the tour's PASSes are not backed by the engine's own lines" </dev/null
summary "surfacetourtest - $passed steps walked the surface (14 rebinds, 14 windows opened+closed by hotkey, features exercised), failed=$failed, skipped=$skipped, gate vacuous=${vacuous:--} leak=${leak:--}"
