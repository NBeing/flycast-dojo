#!/usr/bin/env bash
#
# Build Flycast Dojo in an MSYS2 MINGW64 shell.
#
# Installs the toolchain, configures, builds, and stages a runnable folder
# with the DLLs the exe needs beside it. Safe to re-run: everything it does
# is idempotent, and a second run is an incremental rebuild.
#
# Usage, from the MSYS2 MINGW64 shell:
#   ./shell/windows/build-mingw64.sh              # build the repo it lives in
#   ./shell/windows/build-mingw64.sh --clone      # clone a fresh tree first
#   ./shell/windows/build-mingw64.sh --clean      # wipe the build dir first
#   ./shell/windows/build-mingw64.sh --debug      # Debug instead of RelWithDebInfo
#   ./shell/windows/build-mingw64.sh --jobs 8     # override parallelism
#
set -euo pipefail

REPO_URL="https://github.com/blueminder/flycast-dojo"
BUILD_TYPE="RelWithDebInfo"
DO_CLONE=0
DO_CLEAN=0
JOBS=""

log()  { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m warn:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--clone) DO_CLONE=1 ;;
		--clean) DO_CLEAN=1 ;;
		--debug) BUILD_TYPE="Debug" ;;
		--jobs)  shift; JOBS="${1:-}" ;;
		-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
	shift
done

# --- 1. Right shell? --------------------------------------------------------
# This is the single most common failure: MSYS2 ships several terminals and
# only MINGW64 has the toolchain that produces a native 64-bit Windows exe.
if [ "${MSYSTEM:-}" != "MINGW64" ]; then
	die "wrong shell: MSYSTEM='${MSYSTEM:-unset}', need MINGW64.
     Close this window and open 'MSYS2 MINGW64' from the Start menu."
fi
log "shell OK (MSYSTEM=$MSYSTEM)"

# --- 2. Toolchain and dependencies -----------------------------------------
# This list is the one CI uses (.github/workflows/c-cpp.yml), plus ccache and
# gdb which make iterating bearable.
PACKAGES=(
	git base-devel
	mingw-w64-x86_64-toolchain
	mingw-w64-x86_64-cmake
	mingw-w64-x86_64-breakpad-git
	mingw-w64-x86_64-lua
	mingw-w64-x86_64-ninja
	mingw-w64-x86_64-SDL2
	mingw-w64-x86_64-asio
	mingw-w64-x86_64-ccache
	mingw-w64-x86_64-gdb
	wget zip
)

log "installing/verifying packages (this is a no-op if already present)"
# --needed skips anything already installed, so re-runs are cheap.
pacman -S --needed --noconfirm "${PACKAGES[@]}"

# ffmpeg is only needed at runtime, for video capture. Not fatal if missing.
if ! command -v ffmpeg >/dev/null 2>&1; then
	warn "ffmpeg not found - video recording will not work until it is on PATH."
	warn "install with: pacman -S mingw-w64-x86_64-ffmpeg"
fi

# --- 3. Source tree ---------------------------------------------------------
if [ "$DO_CLONE" = "1" ]; then
	SRC_DIR="$PWD/flycast-dojo"
	if [ -d "$SRC_DIR" ]; then
		log "reusing existing clone at $SRC_DIR"
	else
		log "cloning $REPO_URL"
		git clone --recursive "$REPO_URL" "$SRC_DIR"
	fi
else
	# Resolve the repo root from this script's own location, so the script
	# works regardless of the directory it is invoked from.
	SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi
cd "$SRC_DIR"
log "source tree: $SRC_DIR"

[ -f CMakeLists.txt ] || die "no CMakeLists.txt in $SRC_DIR - not a flycast tree?"

# A plain 'git clone' without --recursive leaves empty submodule directories,
# and the failure surfaces much later as a confusing CMake error naming a
# directory that exists but has no CMakeLists.txt in it.
log "checking submodules"
if git submodule status | grep -q '^-'; then
	log "initialising missing submodules"
	git submodule update --init --recursive
fi
# An emptied-but-initialised submodule fails the same way, so check the one
# that is mandatory on this platform rather than trusting the status flags.
if [ ! -f core/deps/breakpad/CMakeLists.txt ]; then
	warn "core/deps/breakpad looks empty; restoring it"
	git submodule update --init --force --recursive core/deps/breakpad
fi

# --- 4. Configure and build -------------------------------------------------
if [ "$DO_CLEAN" = "1" ]; then
	log "removing build directory"
	rm -rf build
fi

: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

log "configuring ($BUILD_TYPE, Ninja, $JOBS jobs)"
# CPR_FORCE_* mirror CI: use the system curl/OpenSSL rather than building
# curl from scratch.
cmake -B build -G Ninja \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
	-DCMAKE_INSTALL_PREFIX=artifact \
	-DCPR_FORCE_USE_SYSTEM_CURL=ON \
	-DCPR_FORCE_OPENSSL_BACKEND=ON

log "building"
cmake --build build --target install --parallel "$JOBS"

EXE="artifact/bin/flycast-dojo.exe"
[ -f "$EXE" ] || die "build reported success but $EXE is missing"

# --- 5. Stage a runnable folder --------------------------------------------
# Run from the MINGW64 shell the exe finds its DLLs on PATH; launched from
# Explorer it does not. Copy them next to the binary so it runs either way.
STAGE="artifact/flycast-dojo-win64"
log "staging runnable build in $STAGE"
mkdir -p "$STAGE"
cp -f "$EXE" "$STAGE/"

DLLS=(zlib1.dll libssl-3-x64.dll libcrypto-3-x64.dll)
missing=0
for dll in "${DLLS[@]}"; do
	if [ -f "/mingw64/bin/$dll" ]; then
		cp -f "/mingw64/bin/$dll" "$STAGE/"
	else
		warn "missing /mingw64/bin/$dll"
		missing=1
	fi
done

# The OpenSSL DLLs are version-stamped and get renamed across releases, so
# fall back to whatever libssl/libcrypto this installation actually has.
if [ "$missing" = "1" ]; then
	warn "copying any libssl/libcrypto found instead"
	for f in /mingw64/bin/libssl-*.dll /mingw64/bin/libcrypto-*.dll; do
		[ -f "$f" ] && cp -f "$f" "$STAGE/"
	done
fi

# ldd resolves the real import list, catching anything the hardcoded list above
# misses (a different GCC runtime, a differently-named SDL2, etc).
log "resolving remaining dependencies with ldd"
ldd "$EXE" 2>/dev/null | awk '/=> \/mingw64/ { print $3 }' | sort -u | while read -r dep; do
	[ -f "$dep" ] && cp -n "$dep" "$STAGE/" 2>/dev/null || true
done

log "done"
echo
echo "  binary : $SRC_DIR/$EXE"
echo "  folder : $SRC_DIR/$STAGE   (double-clickable)"
echo
echo "  rebuild after edits:  cmake --build build --target install"
echo "  (do NOT delete build/ - incremental rebuilds take seconds)"
