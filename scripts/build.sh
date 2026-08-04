#!/usr/bin/env bash
#
# Compile the EDK2 firmware for one RK3576 board.
#
# This only compiles.  Host setup (packages, BaseTools, the /Scripts symlink)
# is scripts/setup-host.sh; turning the build output into a flashable image is
# scripts/package.sh.  The old single 628-line script did all three, which is
# why `sudo apt-get` could run in the middle of a rebuild.
#
# Usage:
#   scripts/build.sh <board>          # boards/<board>.conf
#   scripts/build.sh <board> clean    # wipe Build/ and Conf/ first
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/lib/common.sh"

BOARD="${1:-}"
[ -n "$BOARD" ] || die "usage: $0 <board> [clean]   (boards/: $(cd "$ROOT/boards" && ls *.conf | sed 's/\.conf//' | tr '\n' ' '))"
CONF="$ROOT/boards/$BOARD.conf"
[ -f "$CONF" ] || die "no such board config: $CONF"

# shellcheck disable=SC1090
. "$CONF"
: "${PLATFORM_NAME:?$CONF must set PLATFORM_NAME}"
: "${DSC_FILE:?$CONF must set DSC_FILE}"

if [ "${2:-}" = "clean" ]; then
    info "Cleaning Build/ and Conf/"
    rm -rf "$ROOT/Build" "$ROOT/Conf"
fi

EDK2="$(edk2_dir)"
[ -d "$EDK2/BaseTools" ] || die "EDK2 not found at $EDK2 -- run scripts/setup-host.sh first"

step "Preparing build environment"

# tools_def is regenerated from the template every time so the patch stays
# idempotent -- patching an already-patched file used to double the flags.
mkdir -p "$ROOT/Conf"
cp "$EDK2/BaseTools/Conf/tools_def.template" "$ROOT/Conf/tools_def.txt"
cp "$EDK2/BaseTools/Conf/build_rule.template" "$ROOT/Conf/build_rule.txt"
cp "$EDK2/BaseTools/Conf/target.template"     "$ROOT/Conf/target.txt"

GCC_MAJOR="$(gcc -dumpversion | cut -d. -f1)"
python3 "$ROOT/scripts/lib/tools_def_patch.py" "$ROOT/Conf/tools_def.txt" "$GCC_MAJOR"

case "$(uname -m)" in
    aarch64) export GCC5_AARCH64_PREFIX="" GCC_AARCH64_PREFIX="" ;;
    x86_64)
        command -v aarch64-linux-gnu-gcc >/dev/null \
            || die "aarch64-linux-gnu-gcc missing -- run scripts/setup-host.sh"
        export GCC5_AARCH64_PREFIX="aarch64-linux-gnu-" GCC_AARCH64_PREFIX="aarch64-linux-gnu-"
        ;;
    *) die "unsupported host architecture: $(uname -m)" ;;
esac

export WORKSPACE="$ROOT"
export EDK_TOOLS_PATH="$EDK2/BaseTools"
export CONF_PATH="$ROOT/Conf"
export PACKAGES_PATH="$ROOT:$EDK2:$(deps_packages_path)"
export PYTHONPATH="$EDK2/BaseTools/Source/Python"
# BinWrappers/PosixLike must come first: the wrappers resolve their siblings
# through BASH_SOURCE-relative paths, so reaching them via a symlink elsewhere
# breaks Trim.
export PATH="$EDK2/BaseTools/BinWrappers/PosixLike:$EDK2/BaseTools/Source/C/bin:$PATH"

set +u
# shellcheck disable=SC1091
. "$EDK2/edksetup.sh" BaseTools >/dev/null 2>&1 || true
set -u

step "Building $PLATFORM_NAME"
LOG="$ROOT/Build/build-$BOARD.log"
mkdir -p "$ROOT/Build"

set +e
build -s -n "$(nproc)" -a AARCH64 -t GCC \
      -p "$DSC_FILE" \
      -b RELEASE \
      -D FIRMWARE_VER="rk3576-${PLATFORM_NAME}-$(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo dev)" \
      2>&1 | tee "$LOG"
rc="${PIPESTATUS[0]}"
set -e

# Do not trust the exit code alone.  The predecessor of this script could
# report success while the log carried compiler errors, which is how a stale
# image got flashed more than once.
if [ "$rc" -ne 0 ] || grep -qE '^build\.py\.\.\..*error|error [0-9A-F]{4}:' "$LOG"; then
    echo
    grep -E 'error [0-9A-F]+:|\.[ch]:[0-9]+:.*error:' "$LOG" | head -20 || true
    die "build failed (exit $rc) -- full log: $LOG"
fi

FV="$ROOT/Build/$PLATFORM_NAME/RELEASE_GCC/FV/BL33_AP_UEFI.Fv"
[ -f "$FV" ] || die "build reported success but $FV is missing"

ok "$PLATFORM_NAME built: $(basename "$FV") ($(( $(stat -c%s "$FV") / 1024 )) KB)"
echo "  next: scripts/package.sh $BOARD"
