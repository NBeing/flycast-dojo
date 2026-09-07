#!/usr/bin/env bash
#
# install.sh -- put Flycast Dojo into your launcher.
#
#   RUN:        shell/linux/install.sh
#   PASS:       exit 0, and `rofi -show drun` finds "Flycast Dojo".
#   DRY RUN:    shell/linux/install.sh --check
#   REMOVE:     shell/linux/install.sh --uninstall
#   FAIL:       exit 1 naming what stopped it. It never half-installs quietly.
#
# WHAT IT INSTALLS, and why two things rather than one:
#
#   ~/.local/share/applications/flycast-dojo-rofi.desktop  <- `rofi -show drun`
#   ~/.local/bin/flycast-rofi                   (symlink)  <- `rofi -show run`, $PATH
#
# Those are two rofi modes reading two different indexes, and someone who runs
# one will not find an entry installed only for the other. Installing for just
# `drun` is why a launcher "does not show up" for a user whose keybind is
# `-show run`.
#
# ---------------------------------------------------------------------------
# IT WILL NOT TOUCH A FILE IT DID NOT WRITE
#
# The generated .desktop carries `X-FlycastDojo-Managed=true`. Install refuses
# to overwrite a file lacking it and uninstall refuses to delete one, so a
# hand-written entry is never clobbered by a script the user ran expecting it
# to be additive. Same for the symlink: removed only if it is a symlink
# pointing into this checkout.
# ---------------------------------------------------------------------------

set -uo pipefail

SELF="$(realpath "${BASH_SOURCE[0]}")"
HERE="$(dirname "$SELF")"
LAUNCHER="$HERE/flycast-rofi"
TEMPLATE="$HERE/flycast-dojo-rofi.desktop.in"

APP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
BIN_DIR="${FLYCAST_BIN_DIR:-$HOME/.local/bin}"
DESKTOP="$APP_DIR/flycast-dojo-rofi.desktop"
LINK="$BIN_DIR/flycast-rofi"
MARKER='X-FlycastDojo-Managed=true'

MODE="${1:-install}"
case "$MODE" in install|--check|--uninstall) ;; *)
    printf 'usage: install.sh [--check|--uninstall]\n' >&2; exit 2 ;;
esac

fail() { printf 'install.sh: %s\n' "$1" >&2; exit 1; }
say()  { printf '%s\n' "$1"; }

# A missing file is fine, nothing to protect. A present file is ours ONLY if it
# carries the marker. "Present but unreadable" is neither, and is refused
# rather than assumed either way.
ours() {
    [ -e "$1" ] || return 0
    [ -r "$1" ] || fail "$1 exists and is not readable -- refusing to guess"
    grep -qF "$MARKER" "$1"
}

[ -x "$LAUNCHER" ] || fail "no executable launcher at $LAUNCHER"
[ -r "$TEMPLATE" ] || fail "no template at $TEMPLATE"

# The launcher's own preflight, before advertising it to a menu. Installing a
# launcher that cannot launch is how you get a desktop entry that does nothing
# when clicked: a visible-looking success that fails only on use.
if [ "$MODE" != "--uninstall" ]; then
    FLYCAST_ROFI_NO_UI=1 "$LAUNCHER" --check >/dev/null 2>&1 \
        || fail "the launcher's own --check fails; fix that first: $LAUNCHER --check"
fi

if [ "$MODE" = "--uninstall" ]; then
    n=0
    if [ -e "$DESKTOP" ]; then
        if ours "$DESKTOP"; then rm -f "$DESKTOP"; say "removed  $DESKTOP"; n=$((n+1))
        else say "KEPT     $DESKTOP -- no $MARKER, not ours"; fi
    fi
    if [ -L "$LINK" ]; then
        if [ "$(realpath "$LINK" 2>/dev/null)" = "$LAUNCHER" ]; then
            rm -f "$LINK"; say "removed  $LINK"; n=$((n+1))
        else say "KEPT     $LINK -- points elsewhere, not ours"; fi
    elif [ -e "$LINK" ]; then
        say "KEPT     $LINK -- a real file, not our symlink"
    fi
    command -v update-desktop-database >/dev/null 2>&1 \
        && update-desktop-database "$APP_DIR" 2>/dev/null
    say "removed $n item(s)"
    exit 0
fi

say "launcher   $LAUNCHER"
say "desktop    $DESKTOP"
say "symlink    $LINK"

ours "$DESKTOP" || fail "$DESKTOP exists without $MARKER -- refusing to overwrite something this script did not write"
if [ -e "$LINK" ] && [ ! -L "$LINK" ]; then
    fail "$LINK exists and is a real file, not a symlink -- refusing to replace it"
fi

if [ "$MODE" = "--check" ]; then
    say "--check: nothing written"
    exit 0
fi

mkdir -p "$APP_DIR" "$BIN_DIR" || fail "cannot create $APP_DIR / $BIN_DIR"

tmp="$DESKTOP.tmp.$$"
# `|` as the sed delimiter because a path contains `/` and would end the
# expression early -- a substitution that silently produces a broken Exec line.
sed "s|@EXEC@|$LAUNCHER|g" "$TEMPLATE" > "$tmp" || { rm -f "$tmp"; fail "could not render $TEMPLATE"; }
grep -qF '@EXEC@' "$tmp" && { rm -f "$tmp"; fail "template still holds @EXEC@ after substitution"; }
grep -qF "$MARKER" "$tmp" || { rm -f "$tmp"; fail "rendered file lost its $MARKER -- it would be uninstallable"; }
# Atomic: a temp file and a rename, never a truncating write onto the live file.
mv -f "$tmp" "$DESKTOP" || { rm -f "$tmp"; fail "could not install $DESKTOP"; }
say "wrote      $DESKTOP"

ln -sfn "$LAUNCHER" "$LINK" || fail "could not link $LINK"
say "linked     $LINK"

if command -v desktop-file-validate >/dev/null 2>&1; then
    if desktop-file-validate "$DESKTOP"; then say "validated  ok"
    else fail "desktop-file-validate rejected $DESKTOP (left in place so you can read it)"; fi
else
    say "validated  skipped (desktop-file-validate absent)"
fi

command -v update-desktop-database >/dev/null 2>&1 \
    && update-desktop-database "$APP_DIR" 2>/dev/null

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) say "NOTE: $BIN_DIR is not on your PATH, so \`flycast-rofi\` will not resolve as a command." ;;
esac
say 'done'
