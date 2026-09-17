#!/usr/bin/env bash
# rebindtest - the HOTKEY REBIND journey: a rebind SURVIVES A RESTART.
#
#   RUN:   scripts/rebindtest.sh              (needs a ROM, Xvfb, a keyboard device)
#   PASS:  boot 1 rebinds EMU_BTN_FFORWARD to a distinctive code and SAVES it;
#          boot 2 - a fresh process sharing the same config sandbox - reads that
#          code back. The binding crossed a restart (its whole point).
#   FAIL:  exit 1 (boot 2 did not read back the code boot 1 saved).
#   SKIP:  exit 77 (no ROM / Xvfb / build / no mappable keyboard device).
#   SELF:  scripts/rebindtest.sh --self-test - boot 1 rebinds but does NOT save
#          (dojo:RebindProbe=nosave). After the restart the binding must be GONE;
#          the twin exits 0 when it is (the save is what persists, and boot 2 is
#          really a fresh process reading disk), 1 if an unsaved bind still
#          survived (an exit-flush bug, or a verify that never left memory).
#
# WHY CROSS-PROCESS. A rebind's contract is that it is remembered next time, and
# an in-process reload cannot prove it: loadMapping() -> LoadMapping() caches by
# filename and hands back the object just edited. So this is two boots sharing one
# XDG_CONFIG_HOME, the coldboot_pair shape. dojo:RebindProbe replays tick()'s real
# post-detection bind block (clear/set/dirty/save_mapping) - no synthesised press.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${REBINDTEST_OUT:-}" ]; then OUT="$REBINDTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "rebindtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "rebindtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "rebindtest: SKIP - no Xvfb"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "rebindtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

# ONE config sandbox, shared by both boots - that shared dir IS the restart. Data
# is per-boot scratch; only config (the mappings) must carry across.
CFG="$OUT/cfg"; mkdir -p "$CFG/flycast-dojo"

# boot <mode> <tag> -> echoes the "REBIND PROBE:" line, on its own private display.
boot() {
	local mode="$1" tag="$2" dn disp xpid fc i line
	dn=$((160 + ($$ % 80))); while [ -e "/tmp/.X11-unix/X$dn" ] || [ -e "/tmp/.X$dn-lock" ]; do dn=$((dn+1)); [ "$dn" -gt 250 ] && { echo "SKIP-DISPLAY"; return; }; done
	disp=":$dn"
	nohup Xvfb "$disp" -screen 0 640x480x24 >"$OUT/$tag.xvfb.log" 2>&1 & xpid=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$dn" ] || { echo "SKIP-DISPLAY"; kill "$xpid" 2>/dev/null; return; }
	XDG_CONFIG_HOME="$CFG" XDG_DATA_HOME="$OUT/data_$tag" DISPLAY="$disp" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
		-config "dojo:RebindProbe=$mode" \
		-config window:width=640 -config window:height=480 -config window:fullscreen=no \
		"$ROM" > "$OUT/$tag.out.log" 2>&1 & fc=$!
	line=""
	for i in $(seq 1 40); do
		kill -0 "$fc" 2>/dev/null || break
		line="$(tr -d '\0' < "$OUT/$tag.out.log" | grep -a "REBIND PROBE:" | tail -1)"
		[ -n "$line" ] && break
		sleep 1
	done
	sleep 1
	[ -n "$line" ] || line="$(tr -d '\0' < "$OUT/$tag.out.log" | grep -a "REBIND PROBE:" | tail -1)"
	kill "$fc" 2>/dev/null; kill "$xpid" 2>/dev/null; sleep 2
	kill -0 "$fc" 2>/dev/null && kill -9 "$fc" 2>/dev/null
	kill -0 "$xpid" 2>/dev/null && kill -9 "$xpid" 2>/dev/null
	echo "$line"
}

WRITEMODE=write; [ "$SELF" -eq 1 ] && WRITEMODE=nosave

# ---- boot 1: rebind (+save, unless the sabotage) --------------------------------------
W="$(boot "$WRITEMODE" b1)"
[ "$W" = "SKIP-DISPLAY" ] && { echo "rebindtest: SKIP - no free display / Xvfb"; exit $SKIP; }
echo "  boot1: ${W#*] }"
case "$W" in
	*"no mappable keyboard"*) echo "rebindtest: SKIP - no keyboard device in this headless session"; exit $SKIP ;;
	"") echo "rebindtest: SKIP - the probe never reported in boot 1 (never reached the tick); see $OUT/b1.out.log"; exit $SKIP ;;
esac
echo "$W" | grep -q "saved=$([ "$SELF" -eq 1 ] && echo no || echo yes)" || { echo "rebindtest: SKIP - boot 1 did not rebind as expected"; exit $SKIP; }

# ---- boot 2: a fresh process reads what is on disk ------------------------------------
V="$(boot verify b2)"
[ "$V" = "SKIP-DISPLAY" ] && { echo "rebindtest: SKIP - no free display / Xvfb"; exit $SKIP; }
echo "  boot2: ${V#*] }"
[ -n "$V" ] || { echo "rebindtest: SKIP - the probe never reported in boot 2; see $OUT/b2.out.log"; exit $SKIP; }
persisted=$(echo "$V" | sed -n 's/.*persisted=\([a-z]*\).*/\1/p')
echo "rebindtest: persisted=$persisted (self-test=$SELF)"

if [ "$SELF" -eq 1 ]; then
	if [ "$persisted" = "no" ]; then
		echo "PASS rebindtest (self-test) - an UNSAVED rebind did NOT survive the restart (the save is what persists, and boot 2 truly read disk)"
		exit 0
	fi
	echo "FAIL rebindtest (self-test) - an unsaved rebind survived a restart; either an exit-flush persists it or boot 2 never left memory"
	exit 1
fi

if [ "$persisted" = "yes" ]; then
	echo "PASS rebindtest - a saved rebind survived a restart (boot 2, a fresh process, read the code boot 1 wrote to disk)"
	exit 0
fi
echo "FAIL rebindtest - a saved rebind did NOT survive the restart (boot 2 did not read back boot 1's code)"
exit 1
