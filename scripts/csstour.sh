#!/usr/bin/env bash
# csstour - the CHARACTER-SELECT TOUR: David's char-select utility exercised on DC, from
# power-on, ending on a saved Dhalsim base.
#
#   RUN:   scripts/csstour.sh                      (needs a ROM, Xvfb, python3, the build)
#   PASS:  exit 0. The tour boots with NO CLIP: RecordMatches + the OnEnter seed
#          (scripts/fixtures/mvc2/css/seed_globe.txt = David's fastVS_mcp boot seed + 45
#          neutral, handoff ON the globe at 742, MacroMode so the handoff keeps
#          READ-WRITE), then core/dojo/css_tour.cpp (module `css` on the Surface Tour's
#          v3 rails) presses every pick through tas_auto::playLive and reads every claim
#          back from the game (ID_2 / Assist_Value / PaletteID_2 per slot, health, the skip
#          rate) - open the globe, walk the cursor, pick, team, speed select, FIGHT, save
#          slot 0. Every `SURFACE TOUR: step n/N "css: ..."` line is printed, then:
#            C1 the tour reported (a RESULT line; the runner's own gates green)
#            C2 no step failed (the optional FINDING step may SKIP)
#            C3 the team is the RECIPE's: the six ID_2 the game reported on the team step's line
#               equal [dhalsim_base] id2_* (Dhalsim/Cable/Sentinel vs Ryu/Ken/Guile)
#            C4 the base was SAVED: the `CSS BASE: slot 0 @ frame F hash=H folder=<dir>`
#               line, and <dir>/<game>.state exists in the sandbox -> copied to
#               $OUT/dhalsim_base/ (and to --keep-base <dir>)
#            C5 (report-only, never red) the FINDING: David's Dhalsim97 rows on the base -
#               the `css finding:` step's peak, whatever it is
#   EXIT:  0 pass · 1 a CLAIM failed · 2 usage · 4 --sabotage: the arm FAILED TO FIRE ·
#          5 VACUOUS · 77 SKIP (no ROM / Xvfb / build; the binary is stale; the tour
#          never reported; or NO `css:` STEPS in the log - the module is not built yet,
#          which is a skip, never a pass).
#   ARMS:  --sabotage css-walk|css-timing|css-team | --list-sabotage | --self-test (the
#          judge through a CANNED log, no emulator). Judged by scripts/lib/arms.sh (the
#          lifted arms.lua rules) from the runner's own `SURFACE TOUR: arm <cls>
#          must_break=... must_not_break=...` declaration; inverted exits as the tour's.
#   WATCH: --watch [x]  - on the real DISPLAY (hands off), the emulator stays open.
#   KEEP:  --keep-base <dir> - also copy the saved base's clip folder there (what
#          fixtures-check.sh --make-dhalsim-base uses).
#
# THE FAILURE THIS WOULD HAVE CAUGHT. `[MEASURED 2026-09-18]` every intent module of
# tour v4 ran on a base that was a HARNESS ARTIFACT (Sonson vs Marrow, written by
# scripts/testrun.sh) while claiming David's combo; David ships no DC savestates and
# his PS2 menu timing does not transfer (his whole macro from power-on lands in
# Training on the wrong cast). A base has to be AUTHORED and its cast READ BACK from
# the game - this harness reddens the moment the six ID_2 slots are not the RECIPE's.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FIX="$ROOT/scripts/fixtures/mvc2"
RECIPE="${FIXTURES_RECIPE:-$FIX/RECIPE.toml}"
SEED="$FIX/css/seed_globe.txt"
MACRO="$FIX/candidates/Combo_Dhalsim97_pcsx2_macro.txt"
VMU="$FIX/vmu_save_A1.bin"
ARMS="$ROOT/scripts/lib/arms.sh"
KNOWN_ARMS="css-walk+css-timing+css-team"
HANDOFF="${CSS_HANDOFF:-742}"

usage() { echo "usage: $0 [--self-test | --sabotage <class> | --list-sabotage | --watch [x] | --keep-base <dir>]   (exit 2: usage)"; exit 2; }
SELF=0; WATCH=0; ARM=""; KEEP=""; CANNED=0
while [ $# -gt 0 ]; do
	case "$1" in
		--self-test)     CANNED=1 ;;
		--sabotage)      shift; [ $# -gt 0 ] || usage; ARM="$1" ;;
		--list-sabotage)
			echo "css-walk    css: walk to Dhalsim      - plan() fed start=Cable for P1: the wrong path; 'css: walk' must redden, 'css: on the globe' must stay green"
			echo "css-timing  css: team P1 + P2        - navgap 0: a repeated direction never re-presses (Sentinel D,D,D,D lands wrong); the team step must redden, 'css: walk' must stay green"
			echo "css-team    css: team P1 + P2        - the P2 stream skipped: P2 slots stay at Cable/0; the team step must redden, 'css: pick' must stay green"
			exit 0 ;;
		--watch)         WATCH=1; [ $# -gt 1 ] && [ "${2#--}" = "$2" ] && shift ;;
		--keep-base)     shift; [ $# -gt 0 ] || usage; KEEP="$1" ;;
		*) usage ;;
	esac
	shift
done
if [ -n "$ARM" ]; then
	case "+$KNOWN_ARMS+" in *"+$ARM+"*) ;; *) echo "csstour: no such arm '$ARM'; have: ${KNOWN_ARMS//+/, }"; exit 2 ;; esac
	SELF=1
fi
case "$ARM" in
	css-walk)   WHAT="plan() fed the wrong start (Cable) for P1 - the walk lands on the wrong character" ;;
	css-timing) WHAT="navgap 0 - a repeated direction never re-presses, Sentinel's D,D,D,D lands wrong" ;;
	css-team)   WHAT="the P2 pick stream skipped - P2's slots never lock" ;;
	*)          WHAT="" ;;
esac

if [ -n "${CSSTOUR_OUT:-}" ]; then OUT="$CSSTOUR_OUT"; mkdir -p "$OUT"	# a kept sandbox, like SURFACETOURTEST_OUT
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi
command -v python3 >/dev/null || { echo "csstour: SKIP - no python3"; exit $SKIP; }

# ---- the small python: RECIPE reads (tomllib) ----------------------------------------
py() { python3 - "$@" <<'PYEOF'
import sys, tomllib
cmd=sys.argv[1]
if cmd=='get':      # get <recipe> <sect> <key>
    d=tomllib.load(open(sys.argv[2],'rb')); v=d[sys.argv[3]][sys.argv[4]]
    print(','.join(map(str,v)) if isinstance(v,list) else v)
PYEOF
}

# ---- claim bookkeeping (surfacetourtest's grammar) ---------------------------------------
PASSED=0; FAILED=0; SKIPPED=0
FAILRED=(); VACRED=()
claim() {	# claim <id> <ok|FAIL|SKIP|VAC> <text>
	case "$2" in
		ok)   PASSED=$((PASSED+1));  printf '  ok   %s  %s\n' "$1" "$3" ;;
		FAIL) FAILED=$((FAILED+1));  FAILRED+=("$1"); printf '  FAIL %s  %s\n' "$1" "$3" ;;
		VAC)  FAILED=$((FAILED+1));  VACRED+=("$1");  printf '  FAIL %s  %s\n' "$1" "$3" ;;
		SKIP) SKIPPED=$((SKIPPED+1)); printf '  SKIP %s  %s\n' "$1" "$3" ;;
	esac
}
summary() {
	local what=$1
	echo "CSSTOUR RESULT: passed=$PASSED failed=$FAILED skipped=$SKIPPED"
	if [ "${#FAILRED[@]}" -gt 0 ]; then echo "FAIL  ${#FAILRED[@]} check(s) red: $(IFS=', '; echo "${FAILRED[*]}")$([ "${#VACRED[@]}" -gt 0 ] && echo " (and vacuous: $(IFS=', '; echo "${VACRED[*]}"))") - $what"; exit 1; fi
	if [ "${#VACRED[@]}" -gt 0 ]; then echo "VACUOUS  ${#VACRED[@]} check(s) red: $(IFS=', '; echo "${VACRED[*]}") - $what proved nothing"; exit 5; fi
	echo "PASS  $what"; exit 0
}

# ---- the judge over a log ($LOG, NUL-stripped) -----------------------------------------
# One function for the real run AND --self-test's canned log, so the canned log proves
# the same code path the emulator feeds.
judge_log() {
	local STRIP='s/.*[NW]\[[A-Z]*\]: /  /'
	printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step\|SURFACE TOUR: ready\|SURFACE TOUR RESULT\|SURFACE TOUR: aborted\|CSS BASE:' | sed "$STRIP"
	printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* -> \(VACUOUS\|LEAK\|unmeasured\)" | sed "$STRIP"
	printf '%s\n' "$LOG" | grep -a "\]:   ok  G[0-9]\|\]:   FAIL G[0-9]\|\]:            [^ ]" | sed 's/.*[NW]\[[A-Z]*\]: /  runner: /'
	local RESULT; RESULT="$(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"
	case "$RESULT" in
		"")            echo "csstour: SKIP - the tour never reported"; exit $SKIP ;;
		*"aborted -"*) echo "csstour: SKIP - ${RESULT#*SURFACE TOUR: }"; exit $SKIP ;;
	esac
	local ncss; ncss=$(printf '%s\n' "$LOG" | grep -ac 'SURFACE TOUR: step [0-9]*/[0-9]* "css')
	if [ "$ncss" -eq 0 ]; then echo "csstour: SKIP - the tour reported but ran NO 'css:' steps (module css not built / not selected: dojo:TourModules=css, dojo:CssTour=yes)"; exit $SKIP; fi
	field() { echo "$RESULT" | sed -n "s/.*$1=\([0-9]*\).*/\1/p"; }
	local passed failed skipped total gate_ok vacuous leak unmeasured gates_red
	passed=$(field passed); failed=$(field failed); skipped=$(field skipped); total=$(field total)
	gate_ok=$(field gate_ok); vacuous=$(field vacuous); leak=$(field leak); unmeasured=$(field unmeasured); gates_red=$(field gates_red)
	echo "csstour: passed=$passed failed=$failed skipped=$skipped total=$total css_steps=$ncss gate_ok=${gate_ok:--} vacuous=${vacuous:--} leak=${leak:--} gates_red=${gates_red:--}"

	# SEEN / BROKEN for the judge (a SKIPped control did not run - arms.lua rule 3)
	printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step [0-9]*/[0-9]* "' \
		| sed 's/.*SURFACE TOUR: step [0-9]*\/[0-9]* "\(.*\)" -> \(PASS\|FAIL\|SKIP\).*/\1\t\2/' > "$OUT/steps.tsv"
	awk -F'\t' '$2=="PASS"||$2=="FAIL"{print $1}' "$OUT/steps.tsv" > "$OUT/seen"
	awk -F'\t' '$2=="FAIL"{print $1}'               "$OUT/steps.tsv" > "$OUT/broken"

	if [ "$SELF" -eq 1 ]; then
		# ---- the sabotage judge: the runner declares target + control; arms.sh rules --------
		local decl MB MNB
		decl="$(printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: arm $ARM must_break=" | tail -1)"
		if [ -z "$decl" ]; then echo "csstour: the runner did not declare arm '$ARM' (no 'SURFACE TOUR: arm $ARM must_break=' line) - INCONCLUSIVE (exit 2)"; exit 2; fi
		MB="$(printf '%s\n' "$decl" | sed -n 's/.*must_break="\([^"]*\)".*/\1/p')"
		MNB="$(printf '%s\n' "$decl" | sed -n 's/.*must_not_break="\([^"]*\)".*/\1/p')"
		echo "  arm $ARM declared by the runner: must_break='$MB' must_not_break='$MNB'"
		[ -x "$ARMS" ] || { echo "csstour: no judge at $ARMS"; exit 2; }
		"$ARMS" judge "$ARM" "$WHAT" "$MB" "$MNB" "${failed:-0}" "$OUT/seen" "$OUT/broken"; local J=$?
		if [ "$J" -eq 2 ]; then echo "INCONCLUSIVE csstour --sabotage $ARM - the target never ran (exit 2)"; exit 2; fi
		if [ "$J" -ne 0 ]; then
			if ! grep -qF -- "$MB" "$OUT/broken"; then echo "SABOTAGE FAILED TO FIRE: '$MB' stayed green under arm '$ARM'; failed=$failed. The guard is decorative. -> exit 4"; exit 4; fi
			echo "sabotage '$ARM' fired but did not behave: it broke its control or the tally lies -> exit 1"; exit 1
		fi
		# the target must be a FAIL (an outcome), never a vacuity (a no-op verb) - §5.2 rule 6
		if printf '%s\n' "$LOG" | grep -a "SURFACE TOUR: gate .* \"$MB\" .* -> VACUOUS" > /dev/null; then
			echo "FAIL  the target '$MB' was filed VACUOUS - the arm made the verb a no-op, not a wrong outcome (exit 1)"; exit 1; fi
		echo "PASS  sabotage '$ARM' fired as predicted: '$MB' went red, '$MNB' stayed green, failed=$failed. exit 0."; exit 0
	fi

	# ---- the gates ------------------------------------------------------------------------
	local gatePresent=0; [ -n "$gate_ok" ] && [ -n "$vacuous" ] && [ -n "$leak" ] && gatePresent=1
	if [ "$gatePresent" = 1 ] && [ "${gates_red:-0}" -eq 0 ]; then claim C1 ok "the tour reported and its own gates are green (gate_ok=$gate_ok vacuous=$vacuous leak=$leak gates_red=${gates_red:-0})"
	elif [ "$gatePresent" = 1 ]; then claim C1 FAIL "the tour reported but reddened $gates_red of its own gates (see the runner: lines above)"
	else claim C1 VAC "the RESULT line carries no gate fields - the runner has no gate, a PASS would be the tour taking its own word"; fi
	local nfail; nfail=$(awk -F'\t' '$2=="FAIL"' "$OUT/steps.tsv" | wc -l)
	if [ "$nfail" -eq 0 ]; then claim C2 ok "no step failed ($ncss css steps; $passed passed, $skipped skipped of $total)"
	else claim C2 FAIL "$nfail step(s) failed:"; awk -F'\t' '$2=="FAIL"{print "         "$1}' "$OUT/steps.tsv"; fi
	# C3: the six ID_2 the game reported for the LOCKED team == the RECIPE's [dhalsim_base]
	# id2_* pins. The team step's why lists P1_A=..P1_B=..P1_C=..P2_A=..P2_B=..P2_C=.. - each
	# as `got` or `got/want` (css_tour.cpp's grammar); the six `got` values are read, in
	# slot order, from the LAST step line that names all six slots.
	local want got teamline
	want="$(py get "$RECIPE" dhalsim_base id2_p1_a)/$(py get "$RECIPE" dhalsim_base id2_p1_b)/$(py get "$RECIPE" dhalsim_base id2_p1_c) $(py get "$RECIPE" dhalsim_base id2_p2_a)/$(py get "$RECIPE" dhalsim_base id2_p2_b)/$(py get "$RECIPE" dhalsim_base id2_p2_c)"
	teamline="$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step [0-9]*/[0-9]* "css' | grep -a 'P1_A=[0-9]' | grep -a 'P2_C=[0-9]' | tail -1)"
	got="$(printf '%s' "$teamline" | python3 -c "
import re,sys; t=sys.stdin.read(); v=[]
for sl in ('P1_A','P1_B','P1_C','P2_A','P2_B','P2_C'):
    m=re.search(sl+r'=(\d+)', t); v.append(m.group(1) if m else '?')
print('%s/%s/%s %s/%s/%s'%tuple(v))")"
	if [ -z "$teamline" ]; then claim C3 SKIP "team: no css step reported all six ID_2 slots"
	elif [ "$got" = "$want" ]; then claim C3 ok "team: the six ID_2 read back == RECIPE [dhalsim_base] ($got = P1 $(py get "$RECIPE" dhalsim_base name_p1_a)/$(py get "$RECIPE" dhalsim_base name_p1_b)/$(py get "$RECIPE" dhalsim_base name_p1_c) vs P2 $(py get "$RECIPE" dhalsim_base name_p2_a)/$(py get "$RECIPE" dhalsim_base name_p2_b)/$(py get "$RECIPE" dhalsim_base name_p2_c))"
	else claim C3 FAIL "team: ID_2 read back '$got' != RECIPE '$want' (${teamline#*SURFACE TOUR: })"; fi
	# C4: the base saved - the CSS BASE line and the .state on disk
	local baseline bframe bhash bdir
	baseline="$(printf '%s\n' "$LOG" | grep -a 'CSS BASE: slot 0 @ frame' | tail -1)"
	bframe=$(printf '%s' "$baseline" | sed -n 's/.*@ frame \([0-9]*\).*/\1/p'); bhash=$(printf '%s' "$baseline" | sed -n 's/.*hash=\([0-9A-Fa-f]*\).*/\1/p'); bdir=$(printf '%s' "$baseline" | sed -n 's/.*folder=\(.*\)$/\1/p')
	if [ -z "$baseline" ]; then claim C4 FAIL "base: no 'CSS BASE: slot 0 @ frame F hash=H folder=<dir>' line - nothing was saved"
	elif [ "$CANNED" -eq 1 ]; then claim C4 ok "base: CSS BASE line present (canned log; no disk check) - frame $bframe hash $bhash"
	elif ls "$bdir"/*.state >/dev/null 2>&1; then
		mkdir -p "$OUT/dhalsim_base"; cp -r "$bdir"/. "$OUT/dhalsim_base/"
		if [ -n "$KEEP" ]; then mkdir -p "$KEEP"; cp -r "$bdir"/. "$KEEP/"; fi
		claim C4 ok "base: slot 0 saved @ frame $bframe hash $bhash ($(ls "$bdir"/*.state | head -1 | xargs basename), $(stat -c %s "$(ls "$bdir"/*.state | head -1)") bytes)$([ -n "$KEEP" ] && echo " -> kept in $KEEP")"
	else claim C4 FAIL "base: the CSS BASE line names $bdir but no .state is there"; fi
	# C5: the finding - reported, never red
	local fline
	fline="$(printf '%s\n' "$LOG" | grep -a 'SURFACE TOUR: step [0-9]*/[0-9]* "css finding' | tail -1)"
	if [ -n "$fline" ]; then echo "  note C5  FINDING: ${fline#*SURFACE TOUR: }"; else echo "  note C5  FINDING: no 'css finding:' step in the log"; fi
	summary "csstour - David's char-select utility on DC: $ncss css steps, the team read back from the game, the Dhalsim base saved"
}

# ---- --self-test: the judge through a canned log, no emulator -----------------------------
if [ "$CANNED" -eq 1 ]; then
	echo "csstour --self-test: the verdict logic over a CANNED log (the same judge_log the emulator feeds)"
	mk() {	# mk <team-ids> <base-line?1> <fail-step?> -> a log in $LOG
		local ids="$1" base="$2" failstep="${3:-}"
		LOG="00:00:001 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: arms known: css-walk+css-timing+css-team
00:00:002 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: arm css-walk must_break=\"css: walk to Dhalsim\" must_not_break=\"css: on the globe\"
00:00:003 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 1/8 \"css: on the globe\" -> PASS (ID_2 P1_A=19 P2_A=23)
00:00:004 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 2/8 \"css: walk to Dhalsim\" -> $([ "$failstep" = walk ] && echo 'FAIL (ID_2 P1_A=22, want 37)' || echo 'PASS (ID_2 P1_A=37 via 18,4,55,37)')
00:00:005 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 3/8 \"css: pick Dhalsim (LP, alpha)\" -> PASS ()
00:00:006 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 4/8 \"css: pick the rest of P1 + all of P2 (build_picks)\" -> PASS (ID_2 got/want $ids, assists all alpha: YES)
00:00:007 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 5/8 \"css: speed select -> fight\" -> PASS (health 144/144 skip 4)
00:00:008 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 6/8 \"css: save the Dhalsim base\" -> PASS ()
$([ "$base" = 1 ] && echo '00:00:009 dojo/css_tour.cpp:1 N[RENDERER]: CSS BASE: slot 0 @ frame 1500 hash=DEADBEEF team=Dhalsim/Cable/Sentinel folder=/nonexistent/replays/NoBGM_VMU/x')
00:00:010 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 7/8 \"css finding: David's Dhalsim97 rows on the Dhalsim base\" -> SKIP (peak 0)
00:00:011 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR: step 8/8 \"css: end\" -> PASS ()
00:00:012 dojo/surface_tour.cpp:1 N[RENDERER]:   ok  G1  x
00:00:013 dojo/surface_tour.cpp:1 N[RENDERER]: SURFACE TOUR RESULT: passed=7 failed=$([ -n "$failstep" ] && echo 1 || echo 0) skipped=1 total=8 mode=$([ "$SELF" -eq 1 ] && echo sabotage || echo yes) gate_ok=8 vacuous=0 leak=0 unmeasured=0 gates_red=0"
	}
	run() { ( judge_log ) > "$OUT/st.out" 2>&1; echo $?; }
	bad=0
	ok() { if [ "$1" = "$2" ]; then echo "  ok    $3 (exit $2)"; else echo "  FAIL  $3 (exit $1, want $2)"; bad=$((bad+1)); grep -E '^  (ok|FAIL|SKIP) C|SKIP|PASS|FAIL' "$OUT/st.out" | sed 's/^/          /'; fi; }
	mk "P1_A=37/37 P1_B=23/23 P1_C=52/52 P2_A=0/0 P2_B=39/39 P2_C=2/2" 1;            ok "$(run)" 0 "a green log with the RECIPE's team and a CSS BASE line passes"
	mk "P1_A=37/37 P1_B=23/23 P1_C=52/52 P2_A=0/0 P2_B=39/39 P2_C=23/2" 1;            ok "$(run)" 1 "the wrong P2 team (P2_C still on the cursor) reddens C3"
	mk "P1_A=37/37 P1_B=23/23 P1_C=52/52 P2_A=0/0 P2_B=39/39 P2_C=2/2" 0;            ok "$(run)" 1 "no CSS BASE line reddens C4"
	LOG="$(echo 'x N[RENDERER]: SURFACE TOUR RESULT: passed=77 failed=0 skipped=0 total=77 mode=yes gate_ok=77 vacuous=0 leak=0 unmeasured=0 gates_red=0')"; ok "$(run)" 77 "a tour with no css: steps is a SKIP, never a pass"
	LOG=""; ok "$(run)" 77 "no RESULT line is a SKIP"
	SELF=1; ARM=css-walk; WHAT="canned"
	mk "P1_A=37/37 P1_B=23/23 P1_C=52/52 P2_A=0/0 P2_B=39/39 P2_C=2/2" 1 walk;       ok "$(run)" 0 "arm css-walk: the walk reddened, the globe stayed green -> BEHAVED"
	mk "P1_A=37/37 P1_B=23/23 P1_C=52/52 P2_A=0/0 P2_B=39/39 P2_C=2/2" 1;            ok "$(run)" 4 "arm css-walk with nothing red -> FAILED TO FIRE (exit 4)"
	SELF=0; ARM=""
	echo ""; if [ "$bad" -eq 0 ]; then echo "PASS csstour --self-test - the judge behaves through every outcome"; exit 0; fi
	echo "FAIL csstour --self-test - $bad outcome(s) wrong"; exit 1
fi

# ---- the real thing: boot with NO clip ----------------------------------------------------
[ -x "$EXE" ] || { echo "csstour: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "csstour: SKIP - no ROM ($ROM)"; exit $SKIP; }
for f in "$SEED" "$MACRO" "$VMU" "$RECIPE"; do [ -f "$f" ] || { echo "csstour: SKIP - fixture input absent: $f"; exit $SKIP; }; done
if [ "$WATCH" -eq 0 ]; then command -v Xvfb >/dev/null || { echo "csstour: SKIP - no Xvfb"; exit $SKIP; }; fi
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "csstour: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi
mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data/flycast-dojo"
cp "$VMU" "$OUT/data/flycast-dojo/vmu_save_A1.bin"	# THE FIXTURE IS THE ROM AND THE VMU (RECIPE [vmu])
XPID=""
if [ "$WATCH" -eq 1 ]; then
	D="${DISPLAY:-}"; [ -n "$D" ] || { echo "csstour: --watch needs a real DISPLAY"; exit $SKIP; }
	echo "csstour: WATCH mode on $D - HANDS OFF THE KEYBOARD for ~3-5 minutes (the tour presses the picks in-process)."
	echo "  your emu.cfg, mappings, layout and clips are untouched: sandbox = $OUT"
else
	DN=$((200 + ($$ % 50))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { echo "csstour: SKIP - no free display"; exit $SKIP; }; done
	D=":$DN"
	nohup Xvfb "$D" -screen 0 1280x900x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$DN" ] || { echo "csstour: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }
fi
MODE=yes; [ "$SELF" -eq 1 ] && MODE="sabotage:$ARM"
CONSOLE=(-config dojo:NativeConsole=no); [ "$WATCH" -eq 1 ] && CONSOLE=()
# A fresh RECORD boot from power-on (the seed needs frame 0), MacroMode so the handoff
# KEEPS READ-WRITE (a Record-Movie handoff drops to WRITE and the neutral pad clobbers
# every row after it - gui.cpp's handoff stop), Training so a step lands frame-exact.
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no "${CONSOLE[@]}" -config dojo:StartupPrompt=no \
	-config dojo:Training=yes -config dojo:RecordMatches=yes -config dojo:MacroMode=yes -config dojo:Replay=no \
	-config dojo:AutoLoadNetState=no -config dojo:AutoLoadTrainingNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config "dojo:OnEnterFile=$SEED" -config "dojo:OnEnterHandoff=$HANDOFF" \
	-config dojo:CssTour=yes -config dojo:TourModules=css -config "dojo:SurfaceTour=$MODE" \
	-config "dojo:TourBpmMs=${TOUR_BPM_MS:-1000}" -config "dojo:TourRecordMs=${TOUR_RECORD_MS:-2000}" \
	-config "dojo:IntentMacro=$MACRO" -config dojo:HotkeyTrace=yes \
	-config window:width=1280 -config window:height=900 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; [ -n "$XPID" ] && kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; [ -n "$XPID" ] && kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
logged() { tr -d '\0' < "$OUT/out.log" | grep -a -- "$1" > /dev/null; }

# (a) the seed must be SEEDED and HANDED OFF - the two engine traces that say the boot
# played it to the globe; without them the tour would be walking a title screen.
seeded=0; for i in $(seq 1 80); do kill -0 "$FC" 2>/dev/null || break; logged "TAS ONENTER: seeded" && { seeded=1; break; }; logged "gui_start_game" && [ "$i" -gt 40 ] && break; sleep 0.5; done
if [ "$seeded" -eq 0 ]; then cleanup; echo "csstour: SKIP - the OnEnter seed never logged 'TAS ONENTER: seeded' (the boot did not play the seed)"; exit $SKIP; fi
handed=0; for i in $(seq 1 240); do kill -0 "$FC" 2>/dev/null || break; logged "TAS ONENTER: handoff at frame" && { handed=1; break; }; sleep 0.5; done
if [ "$handed" -eq 0 ]; then cleanup; echo "csstour: SKIP - no 'TAS ONENTER: handoff at frame' within 120 s (the seed never reached the globe)"; exit $SKIP; fi
tr -d '\0' < "$OUT/out.log" | grep -a "TAS ONENTER: seeded\|TAS ONENTER: handoff" | sed 's/.*[NW]\[[A-Z]*\]: /  /'

# (b) the tour's RESULT line - the wait exits on it, so a generous budget costs nothing
RESULT=""
for _ in $(seq 1 "${TOUR_WAIT_S:-600}"); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "SURFACE TOUR RESULT:\|SURFACE TOUR: aborted" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
LOG="$(tr -d '\0' < "$OUT/out.log")"
if [ "$WATCH" -eq 1 ]; then
	( judge_log ) ; rc=$?
	echo "csstour: WATCH run finished (verdict above, exit $rc) - the emulator stays open. Close it when done."
	wait "$FC" 2>/dev/null; exit $rc
fi
# C4 copies the base folder BEFORE teardown (the sandbox is removed on exit)
judge_log_rc=0; ( judge_log ); judge_log_rc=$?
cleanup; sleep 1
exit $judge_log_rc
