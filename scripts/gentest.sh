#!/usr/bin/env bash
# gentest - can a generation actually be RESTORED?
#
#   RUN:   scripts/gentest.sh
#   PASS:  a generation of a clip that is NOT open restores its files over the
#          live ones, orphans are moved to .trash rather than deleted, and a
#          restore of the OPEN clip is REFUSED. Exit 0.
#   FAIL:  exit 1, naming the claim.
#   SKIP:  exit 77 (no build, no Xvfb, no ROM).
#
# WHY THIS EXISTS. docs/TEST-PLAN.md section 3 says generations restore "does not
# exist - there is no restore function". That is half right and the wrong half.
# `[MEASURED 2026-09-14]` Dojo::RestoreClipDir and tas_clip::restore are both
# DECLARED AND DEFINED, complete with guardrails - and NOTHING CALLS EITHER. No
# UI, no hotkey, no Lua. Compiled, linked, unreachable: the exact shape CLAUDE.md
# opens with, where SaveStateFrame sat unreachable for several commits and
# savestates silently carried no .frame sidecar.
#
# So this is not a feature to build. It is a feature to REACH, and a test that
# fails until it can be reached is the honest way to say so.
#
# THE FILES ARE HAND-MADE AND NOT REAL SAVESTATES, deliberately. tas_clip::restore
# copies files and moves orphans; what it does is filesystem-shaped, so a fixture
# of three small files makes every claim exact and the run take a second. A real
# 28 MB state would test the same code path more slowly and less legibly.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
DISP=":${GENTEST_DISPLAY:-153}"
OUT="$(mktemp -d)"
XPID=0; FC=0
cleanup() {
	for p in $FC $XPID; do [ "$p" -ne 0 ] && kill "$p" 2>/dev/null; done
	sleep 1
	for p in $FC $XPID; do [ "$p" -ne 0 ] && kill -0 "$p" 2>/dev/null && kill -9 "$p" 2>/dev/null; done
	[ "${GENTEST_KEEP:-0}" = 1 ] || rm -rf "$OUT"
	return 0
}
trap cleanup EXIT

[ -x "$EXE" ] || { echo "gentest: SKIP - not built"; exit $SKIP; }
[ -f "$ROM" ] || { echo "gentest: SKIP - no ROM"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "gentest: SKIP - no Xvfb"; exit $SKIP; }

# A REAL CLIP MUST BE OPEN, or the guardrail arm tests nothing. `[SOURCE]`
# RestoreClipDir compares the target against hostfs::savestateFolderOverride,
# which a session with no replay loaded leaves EMPTY - so the refusal could never
# fire and "the open clip is refused" would pass or fail for reasons unrelated to
# the guard.
SRC=""
for f in $(ls -1t "${XDG_DATA_HOME:-$HOME/.local/share}"/flycast-dojo/replays/*/*/*.flyr 2>/dev/null); do
	ls "$(dirname "$f")"/*.state >/dev/null 2>&1 || continue
	SRC="$f"; break
done
[ -n "$SRC" ] || { echo "gentest: SKIP - no clip with a savestate to open"; exit $SKIP; }

# ---- the fixture: a clip with one generation and one orphan ----------------
CLIP="$OUT/clipA"
mkdir -p "$CLIP/gen_01"
printf 'NEW' > "$CLIP/NoBGM_VMU.state"           # live, and the backup has one too
printf 'OLD' > "$CLIP/gen_01/NoBGM_VMU.state"    # what a restore must bring back
printf 'ORPHAN' > "$CLIP/NoBGM_VMU_9.state"      # live only - must be TRASHED, not deleted
printf '{}' > "$CLIP/clip.json"
printf '{}' > "$CLIP/gen_01/clip.json"           # must NOT overwrite the live one

# THE OPEN CLIP, on a COPY. Opening a clip rewrites clip.json beside it, so
# pointing a test at the user's own library would modify it every run.
mkdir -p "$OUT/open"
cp "$SRC" "$OUT/open/clip.flyr"
for sib in "$(dirname "$SRC")"/*.state "$(dirname "$SRC")"/*.state.* "$(dirname "$SRC")"/clip.json; do
	{ [ -f "$sib" ] && cp "$sib" "$OUT/open/" 2>/dev/null; } || true
done

mkdir -p "$OUT/config/flycast-dojo" "$OUT/data"
cat > "$OUT/config/flycast-dojo/flycast.lua" <<LUAEOF
local out = "$OUT/result.txt"
local function w(s) local f = io.open(out, "a"); if f then f:write(s .. "\n"); f:close() end end
local done = false
flycast_callbacks = flycast_callbacks or {}
flycast_callbacks.vblank = function()
	if done then return end
	done = true
	-- THE BINDING THIS TEST NAMES. It does not exist yet; the engine behind it
	-- does. A red run here says "unreachable", which is the finding.
	local ok, n = pcall(function() return flycast.replay.restoreGeneration("$CLIP", "gen_01") end)
	w("restore_ok=" .. tostring(ok) .. " n=" .. tostring(n))
	-- AND THE GUARDRAIL: the clip this session has OPEN must be refused. The
	-- binding takes a DIRECTORY, and currentPath() is the .flyr inside it.
	local cp = flycast.replay.currentPath() or ""
	local dir = cp:match("^(.*)/[^/]*$") or ""
	w("openclipdir=" .. dir)
	local ok2, n2 = pcall(function()
		return flycast.replay.restoreGeneration(dir, "gen_01")
	end)
	w("openclip_ok=" .. tostring(ok2) .. " n=" .. tostring(n2))
	w("done")
	flycast.emulator.exit()
end
LUAEOF

nohup Xvfb "$DISP" -screen 0 640x480x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!
sleep 2
kill -0 "$XPID" 2>/dev/null || { echo "gentest: SKIP - Xvfb $DISP did not start"; exit $SKIP; }
XDG_CONFIG_HOME="$OUT/config" XDG_DATA_HOME="$OUT/data" DISPLAY="$DISP" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config dojo:Replay=yes -config "dojo:ReplayFilename=$OUT/open/clip.flyr" \
	"$ROM" > "$OUT/out.log" 2>&1 &
FC=$!
waited=0
while [ $waited -lt 60 ]; do
	grep -qax done "$OUT/result.txt" 2>/dev/null && break
	kill -0 $FC 2>/dev/null || break
	sleep 1; waited=$((waited + 1))
done

fails=0
claim() { if [ "$2" -eq 1 ]; then echo "  PASS  $1  ${3:-}"; else echo "  FAIL  $1  ${3:-}"; fails=$((fails+1)); fi; }

[ -s "$OUT/result.txt" ] || { echo "gentest: FAIL - the script never ran (no result file)"; exit 1; }
RES=$(sed -n 's/^restore_ok=//p' "$OUT/result.txt" | head -1)
OPEN=$(sed -n 's/^openclip_ok=//p' "$OUT/result.txt" | head -1)
echo "  lua: restore -> ${RES:-<none>}; open clip -> ${OPEN:-<none>}"

case "$RES" in
true*) claim "the restore binding exists and returned" 1 "$RES" ;;
*)     claim "the restore binding exists and returned" 0 "${RES:-<no line>} - engine present, nothing reaches it" ;;
esac

# THE FILES ARE THE VERDICT, not the return value: a binding that returns a
# number and copies nothing would satisfy the claim above on its own.
live=$(cat "$CLIP/NoBGM_VMU.state" 2>/dev/null)
claim "the backup's state overwrote the live one" \
	"$([ "$live" = "OLD" ] && echo 1 || echo 0)" "NoBGM_VMU.state = ${live:-<gone>} (want OLD)"

trashed=$(find "$CLIP/.trash" -name 'NoBGM_VMU_9.state' 2>/dev/null | head -1)
claim "the live-only orphan was TRASHED, not deleted" \
	"$([ -n "$trashed" ] && echo 1 || echo 0)" "${trashed:-<not in .trash>}"
claim "...and is gone from the clip directory" \
	"$([ ! -f "$CLIP/NoBGM_VMU_9.state" ] && echo 1 || echo 0)" ""

case "$OPEN" in
true*) claim "restoring the OPEN clip is refused" "$(echo "$OPEN" | grep -q 'n=-1' && echo 1 || echo 0)" "$OPEN" ;;
*)     claim "restoring the OPEN clip is refused" 0 "${OPEN:-<no line>}" ;;
esac

[ $fails -eq 0 ] && { echo "gentest: PASS - a generation restores, orphans are kept, the open clip is refused"; exit 0; }
echo "gentest: FAIL - $fails claim(s)"
exit 1
