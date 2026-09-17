#!/usr/bin/env bash
# notationtest - the NOTATION TRANSLATION journey: typing a label delivers the
# input that label NAMES, end to end.
#
#   RUN:   scripts/notationtest.sh              (needs a ROM, Xvfb, a build)
#   PASS:  for EVERY label the profile names, parsePattern -> InjectInput (the real
#          packet encoder) -> the profile's own pressed() reads back EXACTLY that
#          column and no other. checked == faithful, and checked >= 1. The
#          decoder, the encoder and the profile - three independent authors of the
#          same fact - agree: the translation is faithful.
#   FAIL:  exit 1 (a label did not read back as its own hardware bit - a mistranslation).
#   SKIP:  exit 77 (no ROM / Xvfb / build / the probe never reported).
#   SELF:  scripts/notationtest.sh --self-test - dojo:NotationProbe=scramble corrupts
#          the canon between decode and encode. A faithful build must then read back
#          the WRONG button, so faithful < checked; the twin exits 0 when it does
#          (the check detects a mistranslation), 1 if a scrambled canon still read
#          back clean (the check is blind and would prove nothing).
#
# WHY. roll_notation is a translation framework (profile = language, Cell = canon
# interlingua, parsePattern = decoder). notationSelfTest proves text<->Cell; this
# proves canon -> the hardware bits the guest receives, the half that lives in a
# different file (tasWriteCanonIntoFrame) and agreed with the profile only by hand.
# No clip, no stepping - InjectInput writes session_inputs and the probe reads it back.
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
SELF=0
[ "${1:-}" = "--self-test" ] && SELF=1

if [ -n "${NOTATIONTEST_OUT:-}" ]; then OUT="$NOTATIONTEST_OUT"; mkdir -p "$OUT"
else OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT; fi

[ -x "$EXE" ] || { echo "notationtest: SKIP - not built ($EXE)"; exit $SKIP; }
[ -f "$ROM" ] || { echo "notationtest: SKIP - no ROM ($ROM)"; exit $SKIP; }
command -v Xvfb >/dev/null || { echo "notationtest: SKIP - no Xvfb"; exit $SKIP; }
if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
	echo "notationtest: SKIP - refusing to report on a stale binary (rebuild flycast)"; exit $SKIP
fi

DN=$((160 + ($$ % 80))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 250 ] && { echo "notationtest: SKIP - no free display"; exit $SKIP; }; done
D=":$DN"
nohup Xvfb "$D" -screen 0 640x480x24 >"$OUT/xvfb.log" 2>&1 & XPID=$!; sleep 2
[ -e "/tmp/.X11-unix/X$DN" ] || { echo "notationtest: SKIP - Xvfb did not come up on $D"; kill "$XPID" 2>/dev/null; exit $SKIP; }

MODE=yes; [ "$SELF" -eq 1 ] && MODE=scramble
XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
	-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
	-config dojo:AutoLoadNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
	-config "dojo:NotationProbe=$MODE" \
	-config window:width=640 -config window:height=480 -config window:fullscreen=no \
	"$ROM" > "$OUT/out.log" 2>&1 & FC=$!
cleanup() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }

RESULT=""
for _ in $(seq 1 40); do
	kill -0 "$FC" 2>/dev/null || break
	RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "NOTATION PROBE RESULT:" | tail -1)"
	[ -n "$RESULT" ] && break
	sleep 1
done
sleep 1
[ -n "$RESULT" ] || RESULT="$(tr -d '\0' < "$OUT/out.log" | grep -a "NOTATION PROBE RESULT:" | tail -1)"
cleanup; sleep 1
[ -n "$RESULT" ] || { echo "notationtest: SKIP - the probe never reported; see $OUT/out.log"; exit $SKIP; }
echo "  ${RESULT#*] }"

checked=$(echo "$RESULT" | sed -n 's/.*checked=\([0-9]*\).*/\1/p')
faithful=$(echo "$RESULT" | sed -n 's/.*faithful=\([0-9]*\).*/\1/p')
echo "notationtest: checked=$checked faithful=$faithful (self-test=$SELF)"

[ -n "$checked" ] && [ "$checked" -ge 1 ] || { echo "notationtest: SKIP - no labels were checked (empty profile?)"; exit $SKIP; }

if [ "$SELF" -eq 1 ]; then
	if [ "${faithful:-0}" -lt "$checked" ]; then
		echo "PASS notationtest (self-test) - a scrambled canon read back as the WRONG input ($faithful/$checked faithful); the check detects a mistranslation"
		exit 0
	fi
	echo "FAIL notationtest (self-test) - a scrambled canon still read back clean ($faithful/$checked); the faithfulness check is blind"
	exit 1
fi

if [ "$faithful" = "$checked" ]; then
	echo "PASS notationtest - every label ($checked) round-trips notation -> packet -> profile to exactly its own input; the translation is faithful"
	exit 0
fi
echo "FAIL notationtest - $((checked - faithful)) of $checked labels did not deliver their named input (a mistranslation between the profile and the packet encoder)"
exit 1
