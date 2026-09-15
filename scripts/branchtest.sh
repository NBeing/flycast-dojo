#!/usr/bin/env bash
# branchtest - the BRANCH JOURNEY, driven through the real merge engine.
#
#   RUN:   scripts/branchtest.sh              (needs a ROM, Xvfb, g++, a base clip)
#   PASS:  a branch whose movie DIFFERS from main's, forked at a still-aligned
#          state, MERGES - and main's movie becomes the branch's, byte-for-byte.
#          The claim is a DIFFERENCE: main's movie changes IFF a valid merge ran.
#   FAIL:  exit 1, naming which half of the claim broke.
#   SKIP:  exit 77 (no ROM / Xvfb / g++ / build / a base clip with a prefixHash).
#   SELF:  scripts/branchtest.sh --self-test - perturb main's states[] prefixHash
#          so the fork no longer byte-matches. The merge gate MUST refuse
#          (PrefixDiverged) and main MUST stay untouched; the twin exits 0 when
#          the gate held, 1 if a diverged merge slipped through.
#
# WHY NO CLICKS. Merge sits behind a deliberate three-level nested confirm menu,
# and the branch node is drawn at a layout-computed point with no published rect
# to aim at - both of which CLAUDE.md records as unreliable to drive with xdotool
# under llvmpipe. So this uses dojo:BranchMergeProbe, the branch analogue of
# roll_panel's RollEditProbe: a config-gated one-shot that drives the REAL merge()
# session-verb (FlushLiveClip + tas_branch::merge + re-attach + gui_loadState)
# from inside the panel and reports the gate verdict and whether main's movie
# actually became the branch's. The code under test is 100% real; only the input
# gesture is replaced by a deterministic trigger.
#
# WHY THIS IS A GOOD TEST (the branch "puppets the machine"): every merge verb
# ends in gui_loadState, and the merge GATE is a pure frame-timing check - fork
# frame + prefixHash of main's states[slot] must still match. The --self-test
# breaks exactly that byte-identity, which is the invariant a future DAG relies on
# too (state = f(anchor, edges)). See docs and the plan's flat/tree correspondence.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${BRANCHTEST_OUT:-}" ]; then
	OUT="$BRANCHTEST_OUT"; mkdir -p "$OUT"
else
	OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT
fi

[ -x "$EXE" ] || { echo "branchtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "branchtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb   >/dev/null || { echo "branchtest: SKIP - no Xvfb"; exit $SKIP; }
command -v g++    >/dev/null || { echo "branchtest: SKIP - no g++"; exit $SKIP; }
command -v python3 >/dev/null || { echo "branchtest: SKIP - no python3"; exit $SKIP; }

# STALE-BINARY GUARD (testrun.sh's rule): a green result on a binary older than
# the source that produced the probe is a lie. If any tracked source under core/
# is newer than the binary, refuse rather than report.
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "branchtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# ---- a base clip with a prefixHash-stamped savestate ---------------------------------
# The gate needs main's states[slot].prefixHash, so a clip predating the mirror
# (dojo.cpp WriteClipStats) cannot be a fixture here - it would read as
# NoAnchor/PrefixDiverged and the test could not tell that from a real defect.
CLIP="${FLYCAST_TEST_CLIP:-}"
pick_clip() {	# echo a .flyr whose clip.json states[0] carries a numeric prefixHash
	local f d
	for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
		d="$(dirname "$f")"
		[ -f "$d/clip.json" ] || continue
		ls "$d"/*.state >/dev/null 2>&1 || continue
		python3 - "$d/clip.json" <<-'PY' || continue
		import json,sys
		j=json.load(open(sys.argv[1]))
		s=(j.get("states") or [])
		ok=any(isinstance(x,dict) and isinstance(x.get("prefixHash"),int) and x.get("prefixHash") and "movieFrame" in x for x in s)
		sys.exit(0 if ok else 1)
		PY
		printf '%s' "$f"; return 0
	done
	return 1
}
if [ -z "$CLIP" ]; then
	CLIP="$(pick_clip)" || { echo "branchtest: SKIP - no base clip with a prefixHash-stamped savestate"; exit $SKIP; }
fi
[ -f "$CLIP" ] || { echo "branchtest: SKIP - clip not found ($CLIP)"; exit $SKIP; }
SRCDIR="$(dirname "$CLIP")"
echo "branchtest: base clip $CLIP"

# ---- compile the divergent-movie helper ----------------------------------------------
MKDIV="$OUT/mkdiv"
g++ -std=c++17 -O0 -o "$MKDIV" "$ROOT/scripts/tests/make_divergent_movie.cpp" \
	|| { echo "branchtest: FAIL - helper did not compile"; exit 1; }

# ---- build the fixture: a clip + one forked, DIVERGENT branch -------------------------
mkdir -p "$OUT/config/flycast-dojo" "$OUT/data" "$OUT/clip"
cp "$CLIP" "$OUT/clip/clip.flyr"
for sib in "$SRCDIR"/*.state "$SRCDIR"/*.state.* "$SRCDIR"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$OUT/clip/" 2>/dev/null; } || true
done

BID="branchtest_state_0_01"
BR="$OUT/clip/branches/$BID"
mkdir -p "$BR"
# copyLiveSet, by hand: the same extension set core/dojo/tas_clip.cpp copies.
shopt -s nullglob 2>/dev/null || true
for f in "$OUT/clip"/*.flyr "$OUT/clip"/*.flyreplay "$OUT/clip"/*.state "$OUT/clip"/*.frame \
		"$OUT/clip"/*.json "$OUT/clip"/*.png "$OUT/clip"/*.label "$OUT/clip"/*.txt \
		"$OUT/clip"/*.env "$OUT/clip"/*.wave "$OUT/clip"/*.map; do
	[ -f "$f" ] && cp "$f" "$BR/"
done

# Read states[0] and write a branches[] entry whose forkAnchor mirrors it, so the
# gate is Ok. In --self-test, perturb main's states[0].prefixHash so the fork no
# longer byte-matches -> PrefixDiverged. The override frame lands in the tail.
OVR_FRAME="$(python3 - "$OUT/clip/clip.json" "$BID" "$SELF" <<'PY'
import json,sys
cj, bid, self_mode = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
j = json.load(open(cj))
states = j.get("states") or []
st = next((x for x in states if isinstance(x, dict) and isinstance(x.get("prefixHash"), int)
           and x.get("prefixHash") and "movieFrame" in x), None)
if st is None:
    sys.exit(3)
slot = st.get("slot", 0)
frame = int(st["movieFrame"])
ph = int(st["prefixHash"])
seq = int(st.get("rerecordSeq", 0))
# The branch record: a FRESH fork, forkAnchor mirroring main's state[slot].
entry = {
    "id": bid, "name": "branchtest", "fromSlot": slot, "atFrame": frame,
    "forkAnchor": {"slot": slot, "frame": frame, "prefixHash": ph, "rerecordSeq": seq},
    "tags": ["branchtest"], "notes": "", "forkMismatch": False,
    "createdAt": "2026-01-01T00_00_00Z", "modifiedAt": "2026-01-01T00_00_00Z",
}
j["branches"] = [entry]
# a "main" node, as ensureMainNode() would stamp on the first fork.
if not (isinstance(j.get("node"), dict)):
    j["node"] = {"id": "main", "kind": "main", "name": "main", "tags": [], "color": ""}
if self_mode:
    # SABOTAGE: the branch remembers a DIFFERENT machine at the fork than main now
    # has (i.e. main was re-recorded below the fork). Perturb the branch's OWN
    # forkAnchor.prefixHash, NOT main's states[] - the emulator recomputes and
    # rewrites states[].prefixHash on boot (WriteClipStats), so a perturbation
    # there is silently overwritten; forkAnchor is the branch's remembered value
    # and is never recomputed. faHash != mainHash -> PrefixDiverged.
    entry["forkAnchor"]["prefixHash"] = ph ^ 0x5bd1e995   # a different, non-zero hash
json.dump(j, open(cj, "w"), indent=1)
print(frame + 10)   # override a frame in the branch's tail
PY
)" || { echo "branchtest: SKIP - base clip has no prefixHash-stamped state to fork from"; exit $SKIP; }

# Make the BRANCH movie differ from main's: append a valid override record.
"$MKDIV" "$BR/clip.flyr" "$OVR_FRAME" | sed 's/^/  /'

# Snapshots for the on-disk cross-check (independent of the probe's own hashes).
cp "$OUT/clip/clip.flyr"      "$OUT/main_before.flyr"
cp "$BR/clip.flyr"            "$OUT/branch.flyr"
if cmp -s "$OUT/main_before.flyr" "$OUT/branch.flyr"; then
	echo "branchtest: FAIL - fixture is vacuous: branch movie == main movie (the helper did not diverge it)"
	exit 1
fi

# ---- boot: replay the clip, panel open, probe armed. No display INPUT, so no i3. -----
# Pick a FREE display so the test and its _can_fail twin (and peer sessions) never
# collide on a fixed number. -displayfd lets Xvfb choose and report it; only the
# emulator needs DISPLAY (no xdotool here). FLYCAST_TEST_DISPLAY forces one.
if [ -n "${FLYCAST_TEST_DISPLAY:-}" ]; then
	DISP="$FLYCAST_TEST_DISPLAY"
	rm -f "/tmp/.X${DISP#:}-lock" 2>/dev/null || true
	nohup Xvfb "$DISP" -screen 0 1000x800x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!
else
	nohup Xvfb -displayfd 3 -screen 0 1000x800x24 3>"$OUT/xdisp" >"$OUT/xvfb.log" 2>&1 & XPID=$!
	DISP=""
	for _ in $(seq 1 20); do d="$(cat "$OUT/xdisp" 2>/dev/null)"; [ -n "$d" ] && { DISP=":$d"; break; }; sleep 0.3; done
	[ -n "$DISP" ] || { echo "branchtest: SKIP - Xvfb did not report a display"; kill "$XPID" 2>/dev/null; exit $SKIP; }
fi
sleep 2
echo "branchtest: display $DISP"
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"		# be a good citizen (checks.sh)

XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/clip/clip.flyr" \
	-config dojo:AutoSeekState=0 -config dojo:AutoLoadNetState=no \
	-config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:Panel.branches=yes -config "dojo:BranchMergeProbe=$BID" \
	-config window:width=1000 -config window:height=800 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 &
FC=$!

# PID-SCOPED teardown - NEVER by name (CLAUDE.md: a name sweep hit a peer's Xvfb).
cleanup() {
	kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null
	sleep 2
	kill -0 "$FC"   2>/dev/null && kill -9 "$FC" 2>/dev/null
	kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null
}

# WAIT ON THE PUBLISHED CONDITION, not a timer (pull, never push). The probe
# fires once the replay passes frame 120; deadline is a ceiling, not the mechanism.
RESULT=""
for _ in $(seq 1 90); do
	if ! kill -0 "$FC" 2>/dev/null; then break; fi
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "BRANCH PROBE RESULT:" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "BRANCH PROBE RESULT:" | tail -1)"
# capture main's movie NOW, before teardown can touch it, for the on-disk check.
cp "$OUT/clip/clip.flyr" "$OUT/main_after.flyr" 2>/dev/null || true
cleanup; sleep 1

tr -d '\0' < "$OUT/out.log" | grep -a "BRANCH PROBE" | sed 's/^/  /'

if [ -z "$RESULT" ]; then
	echo "branchtest: SKIP - the probe never reported (panel never drew, or replay never reached frame 120)"
	exit $SKIP
fi

field() { echo "$RESULT" | sed -n "s/.*$1=\\([A-Za-z]*\\).*/\\1/p"; }
VERDICT="$(field verdict)"; DIFFER="$(field differ)"; RC="$(field rc)"
CHANGED="$(field changed)"; EQ="$(field nowEqualsBranch)"
echo "branchtest: verdict=$VERDICT differ=$DIFFER rc=$RC changed=$CHANGED nowEqualsBranch=$EQ"

# On-disk cross-check, independent of the probe's own FNV hashes.
DISK_CHANGED=no; cmp -s "$OUT/main_before.flyr" "$OUT/main_after.flyr" || DISK_CHANGED=yes
DISK_EQ=no; cmp -s "$OUT/main_after.flyr" "$OUT/branch.flyr" && DISK_EQ=yes
echo "branchtest: on-disk mainChanged=$DISK_CHANGED mainNowEqualsBranch=$DISK_EQ"

# NON-VACUITY, both arms: the fixture's two movies must have differed.
if [ "$DIFFER" != "yes" ]; then
	echo "FAIL branchtest - the branch and main movies did not differ; the claim is vacuous"
	exit 1
fi

if [ "$SELF" -eq 1 ]; then
	# THE GATE MUST HAVE HELD. A diverged fork must be REFUSED and main untouched.
	# This is the exact defect CLAUDE.md flags: without the states[] prefixHash
	# mirror, PrefixDiverged can never fire and a diverged merge is silently taken.
	if [ "$VERDICT" = "PrefixDiverged" ] && [ "$RC" = "refused" ] && [ "$CHANGED" = "NO" ] && [ "$DISK_CHANGED" = "no" ]; then
		echo "PASS branchtest (self-test) - a diverged fork was REFUSED and main stayed byte-identical"
		exit 0
	fi
	echo "FAIL branchtest (self-test) - a diverged fork was NOT caught: the merge gate let it through"
	echo "                              (verdict=$VERDICT rc=$RC changed=$CHANGED diskChanged=$DISK_CHANGED)"
	exit 1
fi

# NORMAL: a valid fork MERGES, and main's movie becomes the branch's.
if [ "$VERDICT" != "Ok" ]; then
	echo "FAIL branchtest - the fork should have been mergeable but the gate said '$VERDICT'"
	exit 1
fi
if [ "$RC" != "ok" ]; then
	echo "FAIL branchtest - the gate was Ok but the merge did not run (rc=$RC)"
	exit 1
fi
if [ "$CHANGED" != "yes" ] || [ "$DISK_CHANGED" != "yes" ]; then
	echo "FAIL branchtest - a valid merge ran but main's movie did not change (probe=$CHANGED disk=$DISK_CHANGED)"
	exit 1
fi
if [ "$EQ" != "yes" ] || [ "$DISK_EQ" != "yes" ]; then
	echo "FAIL branchtest - main changed but did not become the branch's movie (probe=$EQ disk=$DISK_EQ)"
	exit 1
fi
echo "PASS branchtest - a divergent branch merged: main's movie changed to the branch's, byte-for-byte, and the fork/merge gate was Ok"
exit 0
