#!/usr/bin/env bash
#
# Build the U-Boot SPL (idbloader.img: DDR init blob + SPL) for a board, with
# the patches in uboot/<version>/, into out/<PLATFORM>/idbloader.img.
# scripts/package.sh uses that file in preference to the prebuilt one in
# binaries/.
#
# Usage:  scripts/build-spl.sh <board>
#
# The board config says what to build (SPL_* in boards/<board>.conf).  A
# board without SPL_DEFCONFIG has no SPL build here and the script exits 0.
#
# Inputs, all pinned:
#   U-Boot      SPL_UBOOT_TAG at SPL_UBOOT_COMMIT, cloned once into
#               third_party/u-boot (or UBOOT_DIR), exported clean per build
#   board files SPL_BOARD_DIR: defconfig, device tree, -u-boot.dtsi and the
#               spl.config fragment merged over the defconfig
#   patches     SPL_PATCH_DIR/*.patch, applied in order
#   DDR blob    SPL_DDR_BLOB from armbian/rkbin at RKBIN_COMMIT, SHA-256
#               checked (or RKBIN_DIR with the file in it)
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/lib/common.sh"

BOARD="${1:-}"
[ -n "$BOARD" ] || die "usage: $0 <board>"
CONF="$ROOT/boards/$BOARD.conf"
[ -f "$CONF" ] || die "no such board config: $CONF"
# shellcheck disable=SC1090
. "$CONF"

if [ -z "${SPL_DEFCONFIG:-}" ]; then
    info "$BOARD has no SPL build (no SPL_DEFCONFIG in $CONF); nothing to do"
    exit 0
fi
: "${PLATFORM_NAME:?}" "${SPL_UBOOT_TAG:?}" "${SPL_UBOOT_COMMIT:?}" "${SPL_PATCH_DIR:?}"
: "${SPL_BOARD_DIR:?}" "${SPL_DTS:?}" "${SPL_DDR_BLOB:?}" "${SPL_DDR_SHA256:?}"

RKBIN_COMMIT=98f106532c684c1741c10d027566bfd3662a4698   # armbian/rkbin
UBOOT_URL="${UBOOT_URL:-https://github.com/u-boot/u-boot.git}"
UBOOT_DIR="${UBOOT_DIR:-$ROOT/third_party/u-boot}"
OUTDIR="$ROOT/out/$PLATFORM_NAME"
mkdir -p "$OUTDIR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

step "U-Boot $SPL_UBOOT_TAG"

if ! git -C "$UBOOT_DIR" cat-file -e "$SPL_UBOOT_COMMIT^{commit}" 2>/dev/null; then
    if [ -d "$UBOOT_DIR/.git" ]; then
        git -C "$UBOOT_DIR" fetch --depth 1 origin "refs/tags/$SPL_UBOOT_TAG:refs/tags/$SPL_UBOOT_TAG"
    else
        rm -rf "$UBOOT_DIR"
        git clone --depth 1 --branch "$SPL_UBOOT_TAG" "$UBOOT_URL" "$UBOOT_DIR"
    fi
fi
git -C "$UBOOT_DIR" cat-file -e "$SPL_UBOOT_COMMIT^{commit}" \
    || die "$UBOOT_DIR has no commit $SPL_UBOOT_COMMIT ($SPL_UBOOT_TAG moved?)"

SRC="$WORK/src"
mkdir -p "$SRC"
git -C "$UBOOT_DIR" archive "$SPL_UBOOT_COMMIT" | tar -x -C "$SRC"
ok "exported $SPL_UBOOT_COMMIT"

BD="$ROOT/$SPL_BOARD_DIR"
cp "$BD/$SPL_DEFCONFIG" "$SRC/configs/"
cp "$BD/$SPL_DTS" "$SRC/dts/upstream/src/arm64/rockchip/"
cp "$BD/${SPL_DTS%.dts}-u-boot.dtsi" "$SRC/arch/arm/dts/"
# Any other .dtsi in the board directory is one the board DTS includes and
# U-Boot's dts/upstream lacks (the CM5-IO's rk3576-armsom-cm5.dtsi).
for f in "$BD"/*.dtsi; do
    [ -e "$f" ] || continue
    case "$f" in *-u-boot.dtsi) continue ;; esac
    cp "$f" "$SRC/dts/upstream/src/arm64/rockchip/"
done

shopt -s nullglob
PATCHES=("$ROOT/$SPL_PATCH_DIR"/*.patch)
shopt -u nullglob
for p in "${PATCHES[@]}"; do
    patch -p1 -d "$SRC" --forward --batch -s < "$p" || die "patch does not apply: ${p#"$ROOT"/}"
    info "applied ${p#"$ROOT"/}"
done

step "DDR init blob"

DDR="$WORK/$SPL_DDR_BLOB"
if [ -n "${RKBIN_DIR:-}" ] && [ -f "$RKBIN_DIR/$SPL_DDR_BLOB" ]; then
    cp "$RKBIN_DIR/$SPL_DDR_BLOB" "$DDR"
else
    curl -sSfL -o "$DDR" \
        "https://raw.githubusercontent.com/armbian/rkbin/$RKBIN_COMMIT/rk35/$SPL_DDR_BLOB" \
        || die "could not fetch $SPL_DDR_BLOB (set RKBIN_DIR to a directory that has it)"
fi
echo "$SPL_DDR_SHA256  $DDR" | sha256sum -c --quiet - || die "$SPL_DDR_BLOB: SHA-256 mismatch"
ok "$SPL_DDR_BLOB"

step "Building the SPL ($SPL_DEFCONFIG)"

MAKE_ARGS=(-C "$SRC" O="$WORK/build")
[ "$(uname -m)" = aarch64 ] || MAKE_ARGS+=(CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}")

LOG="$OUTDIR/build-spl.log"
{
    make "${MAKE_ARGS[@]}" "$SPL_DEFCONFIG"
    "$SRC/scripts/kconfig/merge_config.sh" -m -O "$WORK/build" "$WORK/build/.config" "$BD/spl.config"
    make "${MAKE_ARGS[@]}" olddefconfig
} > "$LOG" 2>&1 || die "configuring failed -- see $LOG"

# merge_config.sh only warns when a value does not survive olddefconfig
# (an unmet dependency).  Every line of the fragment has to be in .config.
while IFS= read -r line; do
    case "$line" in ""|"# CONFIG_"*" is not set") ;; "#"*) continue ;; esac
    [ -n "$line" ] || continue
    grep -qxF -- "$line" "$WORK/build/.config" || die "spl.config: '$line' did not take (dependency?)"
done < "$BD/spl.config"

# BL31 is only there to keep binman quiet about u-boot.itb, which is not used:
# the FIT this SPL loads is built by package.sh.  idbloader.img has no BL31.
make "${MAKE_ARGS[@]}" -j"$(nproc)" \
    BL31="$ROOT/binaries/bl31.elf" ROCKCHIP_TPL="$DDR" >> "$LOG" 2>&1 \
    || die "build failed -- see $LOG"

IDB="$WORK/build/idbloader.img"
[ -f "$IDB" ] || die "no idbloader.img -- see $LOG"
[ "$(head -c 4 "$IDB")" = "RKNS" ] || die "idbloader.img does not start with RKNS"
SPL_VER="$(strings -n 20 "$IDB" | grep -o 'U-Boot SPL [0-9][^ ]*' | head -1 || true)"
if grep -qx 'CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE=y' "$WORK/build/.config"; then
    grep -qa 'ArmbianOnce/1' "$IDB" || die "SPL_ROCKCHIP_ARMBIAN_ONCE is on but its tag is not in the SPL"
fi

cp "$IDB" "$OUTDIR/idbloader.img"
cp "$WORK/build/.config" "$OUTDIR/spl.config"
ok "$OUTDIR/idbloader.img  ($(stat -c%s "$IDB") bytes, ${SPL_VER:-no version string})"
echo "  next: scripts/package.sh $BOARD"
