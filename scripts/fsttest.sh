#!/usr/bin/env bash
# fsttest - the FRAME-SKIP / COUNTERFACTUAL journey, driven through the real runner.
#
#   RUN:   scripts/fsttest.sh              (needs a ROM, Xvfb, g++, a base clip)
#   PASS:  a sweep of N variants runs from ONE base state and produces N DISTINCT
#          outcome states (two edges from one node diverge), and re-running the
#          sweep over the SAME fixture reproduces every outcome byte-for-byte
#          (twice down one edge = one node - determinism).
#   FAIL:  exit 1, naming which half broke.
#   SKIP:  exit 77 (no ROM / Xvfb / g++ / build / a base clip with a prefixHash state).
#   SELF:  scripts/fsttest.sh --self-test - the second run's movie is made to DIFFER
#          in the swept region. The outcome states MUST then change; the twin exits
#          0 when they did (the reproducibility check is sensitive, not vacuous), 1
#          if the run ignored the input it is supposed to be sweeping.
#
# WHY A PROBE, NO CLICKS. The FST is a multi-phase async runner behind Capture /
# Generate / Run buttons and a Piano Roll selection. dojo:FstProbe arms a fixed
# four-phase sweep from slot 0 and starts it; the runner's own tick() drives it to
# completion (writeResults). Same doctrine as branchtest/RollEditProbe: real code,
# deterministic trigger. RecordMatches=yes makes the WRITE session steppable
# (session::writeGrow) so the run can advance while paused.
#
# NOTE. This is the test that FOUND the gui_loadState/gui_saveState regression:
# our determinism refactor had narrowed both to GuiState::Closed, so the FST's
# paused base-reload and per-variant outcome-save were silently refused. Fixed by
# restoring reference/flycast-rr's Closed||Paused acceptance.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"		# cap llvmpipe workers (checks.sh)
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${FSTTEST_OUT:-}" ]; then
	OUT="$FSTTEST_OUT"; mkdir -p "$OUT"
else
	OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
fi

[ -x "$EXE" ] || { echo "fsttest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "fsttest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb    >/dev/null || { echo "fsttest: SKIP - no Xvfb"; exit $SKIP; }
command -v g++     >/dev/null || { echo "fsttest: SKIP - no g++"; exit $SKIP; }
command -v python3 >/dev/null || { echo "fsttest: SKIP - no python3"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "fsttest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# ---- a base clip with a prefixHash-stamped savestate (same rule as branchtest) -------
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
[ -n "$CLIP" ] || CLIP="$(pick_clip)" || { echo "fsttest: SKIP - no base clip with a prefixHash-stamped savestate"; exit $SKIP; }
[ -f "$CLIP" ] || { echo "fsttest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
echo "fsttest: base clip $CLIP"

MKDIV="$OUT/mkdiv"
g++ -std=c++17 -O0 -o "$MKDIV" "$ROOT/scripts/tests/make_divergent_movie.cpp" \
	|| { echo "fsttest: FAIL - helper did not compile"; exit 1; }

# base movie frame of slot 0 (= the sweep base); the probe sweeps selLo=base+5..+15.
BASEFRAME="$(python3 - "$SRCDIR/clip.json" <<'PY'
import json,sys
j=json.load(open(sys.argv[1]))
st=next((x for x in (j.get("states") or []) if isinstance(x,dict) and isinstance(x.get("prefixHash"),int) and x.get("prefixHash") and "movieFrame" in x), None)
sys.exit(3) if st is None else print(int(st["movieFrame"]))
PY
)" || { echo "fsttest: SKIP - no prefixHash-stamped state to base the sweep on"; exit $SKIP; }

pick_display() {
	local base=$((160 + ($$ % 80))) n
	for n in $(seq "$base" 250) $(seq 160 "$base"); do
		[ "$n" -le 1 ] && continue
		[ -e "/tmp/.X11-unix/X$n" ] && continue
		[ -e "/tmp/.X$n-lock" ] && continue
		printf '%s' "$n"; return 0
	done
	return 1
}

# run_sweep <fixture-clip-dir> <out-prefix>  ->  writes <prefix>.runs (endFrames) and
# <prefix>.hashes (the N outcome-state sha256s, variant order). Echoes the RESULT line.
run_sweep() {
	local clipdir="$1" pfx="$2" dn disp xpid fc i
	dn="$(pick_display)" || { echo "fsttest: SKIP - no free display >=160"; return 77; }
	disp=":$dn"
	nohup Xvfb "$disp" -screen 0 1000x800x24 >"$pfx.xvfb.log" 2>&1 & xpid=$!
	sleep 2
	[ -e "/tmp/.X11-unix/X$dn" ] || { echo "fsttest: SKIP - Xvfb did not come up on $disp"; kill "$xpid" 2>/dev/null; return 77; }
	XDG_CONFIG_HOME="$clipdir/../cfg" XDG_DATA_HOME="$clipdir/../data" DISPLAY="$disp" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:Replay=yes -config "dojo:ReplayFilename=$clipdir/clip.flyr" \
		-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
		-config dojo:Transmitting=no -config dojo:Receiving=no \
		-config dojo:RecordMatches=yes -config dojo:FstProbe=yes \
		-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
		"$ROM" > "$pfx.out.log" 2>&1 & fc=$!
	local rc_result=""
	for i in $(seq 1 150); do
		kill -0 "$fc" 2>/dev/null || break
		rc_result="$(tr -d '\0' < "$pfx.out.log" | grep -a "TAS FST RUN: results ->" | tail -1)"
		[ -n "$rc_result" ] && break
		sleep 1
	done
	sleep 1
	# harvest BEFORE teardown
	cp "$clipdir/results.json" "$pfx.results.json" 2>/dev/null || true
	: > "$pfx.hashes"
	for f in "$clipdir"/*_[0-9].state "$clipdir"/*_[0-9][0-9].state; do
		[ -f "$f" ] && printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$(basename "$f")" >> "$pfx.hashes"
	done
	sort -k2 "$pfx.hashes" -o "$pfx.hashes"
	# PID-scoped teardown
	kill "$fc" 2>/dev/null; kill "$xpid" 2>/dev/null; sleep 2
	kill -0 "$fc" 2>/dev/null && kill -9 "$fc" 2>/dev/null
	kill -0 "$xpid" 2>/dev/null && kill -9 "$xpid" 2>/dev/null
	echo "$rc_result"
}

# build a fixture copy; if $3=diverge, append an override in the swept region.
build_fixture() {
	local dst="$1" diverge="${2:-}"
	mkdir -p "$dst/cfg/flycast-dojo" "$dst/data" "$dst/clip"
	cp "$CLIP" "$dst/clip/clip.flyr"
	for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
		{ [ -f "$sib" ] && cp "$sib" "$dst/clip/" 2>/dev/null; } || true
	done
	rm -f "$dst/clip"/*_[0-9].state "$dst/clip"/*_[0-9][0-9].state "$dst/clip"/results.json 2>/dev/null || true
	if [ "$diverge" = "diverge" ]; then
		"$MKDIV" "$dst/clip/clip.flyr" "$((BASEFRAME + 10))" >/dev/null
	fi
}

echo "fsttest: base frame $BASEFRAME; sweep region $((BASEFRAME+5))..$((BASEFRAME+15))"

# ---- run A (the reference sweep) -----------------------------------------------------
build_fixture "$OUT/A"
RA="$(run_sweep "$OUT/A/clip" "$OUT/runA")"; rc=$?
[ "$rc" = "77" ] && { echo "fsttest: SKIP - display/Xvfb unavailable"; exit $SKIP; }
echo "  A: $RA"

# ---- run B: same fixture (normal) or divergent region (self-test) --------------------
if [ "$SELF" -eq 1 ]; then build_fixture "$OUT/B" diverge; else build_fixture "$OUT/B"; fi
RB="$(run_sweep "$OUT/B/clip" "$OUT/runB")"; rc=$?
[ "$rc" = "77" ] && { echo "fsttest: SKIP - display/Xvfb unavailable"; exit $SKIP; }
echo "  B: $RB"

nA=$(grep -ac . "$OUT/runA.hashes" 2>/dev/null || echo 0)
nB=$(grep -ac . "$OUT/runB.hashes" 2>/dev/null || echo 0)
distinctA=$(cut -d' ' -f1 "$OUT/runA.hashes" 2>/dev/null | sort -u | grep -ac .)
echo "fsttest: run A wrote $nA outcome states ($distinctA distinct); run B wrote $nB"

# NON-VACUITY: the sweep must actually have run several variants from one base, and
# they must DIVERGE - four counterfactual timings landing on four different states.
if [ "$nA" -lt 2 ]; then
	echo "fsttest: SKIP - the sweep produced $nA outcome states (probe never armed? see runA.out.log)"; exit $SKIP
fi
if [ "$distinctA" -lt "$nA" ]; then
	echo "FAIL fsttest - $nA variants from one base collapsed to $distinctA distinct outcomes; the counterfactual did not diverge"
	exit 1
fi

if [ "$SELF" -eq 1 ]; then
	# The swept region's movie changed, so the outcomes MUST change. If they are
	# identical, the run ignored the input it is sweeping - a vacuous reproducibility.
	if diff -q "$OUT/runA.hashes" "$OUT/runB.hashes" >/dev/null 2>&1; then
		echo "FAIL fsttest (self-test) - a changed movie in the swept region produced IDENTICAL outcomes; the sweep is not running the game"
		exit 1
	fi
	echo "PASS fsttest (self-test) - changing the swept region changed the outcome states (the sweep really runs the game)"
	exit 0
fi

# NORMAL: two sweeps over the SAME fixture must reproduce every outcome - one node
# per edge, deterministic.
if ! diff -q "$OUT/runA.hashes" "$OUT/runB.hashes" >/dev/null 2>&1; then
	echo "FAIL fsttest - two sweeps over the same fixture produced DIFFERENT outcome states (nondeterministic):"
	diff "$OUT/runA.hashes" "$OUT/runB.hashes" | sed 's/^/    /'
	exit 1
fi
echo "PASS fsttest - $nA counterfactual variants from one base, all distinct, and the whole sweep reproduced byte-for-byte on a second run"
exit 0
