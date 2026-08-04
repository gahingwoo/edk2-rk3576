#!/usr/bin/env bash
#
# Turn a built firmware volume into a flashable image.
#
#   BL33_AP_UEFI.Fv + BL31 (PT_LOAD segments) + DTB
#     -> FIT image (.itb)
#     -> written into an SPI NOR or SD/eMMC raw image
#
# Boot chain:  BootROM -> SPL (idbloader) -> TF-A BL31 -> EDK2 (BL33) -> OS
#
# Usage:  scripts/package.sh <board>
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
: "${PLATFORM_NAME:?}" "${DEVICE_TREE_NAME:?}"
BOOT_MEDIUM="${BOOT_MEDIUM:-spi}"

FV="$ROOT/Build/$PLATFORM_NAME/RELEASE_GCC/FV/BL33_AP_UEFI.Fv"
[ -f "$FV" ] || die "no firmware volume at $FV -- run scripts/build.sh $BOARD first"

OUTDIR="$ROOT/out/$PLATFORM_NAME"
mkdir -p "$OUTDIR"
if [ "$BOOT_MEDIUM" = "sdcard" ]; then
    OUT_IMG="$OUTDIR/$PLATFORM_NAME-sdcard.img"
else
    OUT_IMG="$OUTDIR/$PLATFORM_NAME-spi.img"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

step "Assembling the FIT payload"

BL31=""
for cand in "$ROOT/binaries/bl31.elf" "$ROOT/binaries/rk3576_bl31_vendor.elf"; do
    [ -f "$cand" ] && BL31="$cand" && break
done
[ -n "$BL31" ] || die "no BL31 in binaries/"
info "BL31: $(basename "$BL31")"

( cd "$WORK" && python3 "$ROOT/scripts/lib/extractbl31.py" "$BL31" >/dev/null )
cp "$FV" "$WORK/BL33_AP_UEFI.Fv"
touch "$WORK/bl32.bin"   # OP-TEE stub: unused, but the ITS template names it

DTB="$ROOT/scripts/lib/rk3576_spl.dtb"
[ -f "$DTB" ] || DTB="$ROOT/devicetree/vendor/$DEVICE_TREE_NAME.dtb"
[ -f "$DTB" ] || die "no DTB for $DEVICE_TREE_NAME"
cp "$DTB" "$WORK/$DEVICE_TREE_NAME.dtb"

# Pad the DTB to a 512-byte boundary.  An external-data FIT on MMC requires
# every data-offset to be a multiple of the block size.
python3 - "$WORK/$DEVICE_TREE_NAME.dtb" <<'PY'
import os, sys
p = sys.argv[1]
pad = (512 - os.path.getsize(p) % 512) % 512
if pad:
    open(p, 'ab').write(b'\0' * pad)
PY

BL31_ENTRY="$(python3 -c "
from elftools.elf.elffile import ELFFile
print(hex(ELFFile(open('$BL31','rb')).header.e_entry))")"
info "BL31 entry: $BL31_ENTRY"

# Booting from SD/eMMC needs the EDK2 payload compressed: the vendor SPL's MMC
# DMA tops out around 2 MB per transfer and a 13 MB uncompressed FV times out.
# gzip brings it to roughly 2 MB.  The SPI path uses mainline SPL and has no
# such limit.
GZIP_FLAG=""
[ "$BOOT_MEDIUM" = "sdcard" ] && GZIP_FLAG="--gzip-edk2"
python3 "$ROOT/scripts/lib/gen_fit_its.py" "$WORK" "$DEVICE_TREE_NAME" "$BL31_ENTRY" $GZIP_FLAG
[ -f "$WORK/${DEVICE_TREE_NAME}_EFI.its" ] || die "gen_fit_its.py produced no ITS"

# mkimage must be >= 2020.07.  Older releases silently ignore -B, which makes
# every SPL hash wrong -- a failure that only shows up as a dead board.
MKIMAGE="$(command -v mkimage || true)"
[ -n "$MKIMAGE" ] || die "mkimage not found -- run scripts/setup-host.sh"
MK_YEAR="$("$MKIMAGE" -V 2>&1 | grep -oE '[0-9]{4}\.[0-9]+' | head -1 | cut -d. -f1)"
[ -n "$MK_YEAR" ] && [ "$MK_YEAR" -ge 2020 ] \
    || die "mkimage $MK_YEAR is too old; -B is ignored before 2020.07"

# -E:        external data -- the SPL mallocs only the FIT header and reads
#            each payload straight to its load address.
# -B 0x1000: pad the header to 4 KB so the SPL's computed ext_base matches
#            where mkimage actually wrote the data.
# Note: -p in mkimage 2025.10 means "static position", not "padding
# alignment"; using it piles every payload at one offset and they overlap.
# Inter-segment alignment is handled by extractbl31.py padding to 16 bytes.
( cd "$WORK" && "$MKIMAGE" -E -B 0x1000 -f "${DEVICE_TREE_NAME}_EFI.its" "${DEVICE_TREE_NAME}_EFI.itb" >/dev/null )
FIT="$WORK/${DEVICE_TREE_NAME}_EFI.itb"
[ -f "$FIT" ] || die "mkimage produced no FIT"
FIT_SIZE="$(stat -c%s "$FIT")"
ok "FIT image: $((FIT_SIZE / 1024)) KB"

# The idbloader this board just built comes first.  Looking at binaries/ first
# is how a CM5-IO image once shipped with a prebuilt SPL that had none of the
# CM5-IO fixes in it.
IDBLOCK=""
for cand in "$OUTDIR/idbloader.img" "$ROOT/binaries/idblock_mainline.bin"; do
    [ -f "$cand" ] && IDBLOCK="$cand" && break
done
[ -n "$IDBLOCK" ] || die "no idblock (mainline SPL) found"
info "idblock: ${IDBLOCK##*/}  ($(strings -n 20 "$IDBLOCK" | grep -o 'U-Boot SPL [0-9][^ ]*' | head -1 || echo 'no version string'))"

step "Writing $OUT_IMG"

if [ "$BOOT_MEDIUM" = "sdcard" ]; then
    SD_MB=32
    FIT_MAX=$(( SD_MB * 1024 * 1024 - 0x800000 ))
    [ "$FIT_SIZE" -le "$FIT_MAX" ] || die "FIT is $FIT_SIZE bytes, limit is $FIT_MAX -- raise SD_MB"

    dd if=/dev/zero bs=1M count=$SD_MB of="$OUT_IMG" status=none
    dd if="$IDBLOCK" of="$OUT_IMG" bs=512 seek=64 conv=notrunc status=none
    dd if="$FIT" of="$OUT_IMG" bs=512 seek=16384 conv=notrunc status=none

    # NV variable store, 3 x 64 KB, filled with 0xFF.  The rest of the image is
    # zeros, and the variable driver reads all-zero as "corrupted" rather than
    # "erased", so it would report a broken store on every boot.  The offset
    # must match PcdRkFvbNvStorageSpiOffset in the board DSC.
    NVS_OFF=$((0x1600000)); NVS_SIZE=$((3 * 64 * 1024))
    if [ $((NVS_OFF + NVS_SIZE)) -le $((SD_MB * 1024 * 1024)) ] \
       && [ "$NVS_OFF" -ge $((0x800000 + FIT_SIZE)) ]; then
        # head -c bounds the producer.  `tr < /dev/zero` never stops on its own,
        # so once dd exits tr takes SIGPIPE, and with `set -o pipefail` that
        # fails the whole pipeline and kills the script here.
        head -c "$NVS_SIZE" /dev/zero | tr '\0' '\377' \
            | dd of="$OUT_IMG" bs=64K oflag=seek_bytes seek="$NVS_OFF" conv=notrunc status=none
        # Read it back.  "wrote it but not all of it" should not be something
        # you discover by eye in a log.
        LEFT="$(dd if="$OUT_IMG" bs=64K iflag=skip_bytes,count_bytes \
                   skip="$NVS_OFF" count="$NVS_SIZE" status=none | tr -d '\377' | wc -c)"
        [ "$LEFT" -eq 0 ] || die "NV store fill incomplete ($LEFT bytes are not 0xFF)"
        ok "NV store: 0xFF at $(printf '0x%X' $NVS_OFF), verified"
    else
        warn "NV store at $(printf '0x%X' $NVS_OFF) overlaps the FIT or exceeds the image -- variables will not persist"
    fi

    LAYOUT="  sector 64    (0x008000)  idblock (DDR init + SPL)
  sector 16384 (0x800000)  FIT (BL31 + EDK2 + DTB)
  0x1600000                NV variable store (3 x 64 KB)"
    FLASH="  dd if=out/$PLATFORM_NAME/$PLATFORM_NAME-sdcard.img of=/dev/sdX bs=1M status=progress && sync"
else
    FIT_MAX=$((0xFC0000 - 0x60000))
    [ "$FIT_SIZE" -le "$FIT_MAX" ] || die "FIT is $FIT_SIZE bytes, limit is $FIT_MAX"

    dd if=/dev/zero bs=1M count=16 of="$OUT_IMG" status=none
    dd if="$IDBLOCK" of="$OUT_IMG" bs=1K seek=32 conv=notrunc status=none
    dd if="$FIT" of="$OUT_IMG" bs=1K seek=384 conv=notrunc status=none

    # Copy the erased-state NV region out of the FD (ErasePolarity=1, so it is
    # already 0xFF).  48 x 4 KB = 192 KB covers all three 64 KB regions:
    # store at 0xFC0000, FTW Working at 0xFD0000, FTW Spare at 0xFE0000.
    # This was count=3 once, which left Working and Spare as zeros from the
    # dd above -- the FTW driver reads that as corrupt, not erased, and the
    # store never initialises ("Both working and spare block are invalid").
    FD="$ROOT/Build/$PLATFORM_NAME/RELEASE_GCC/FV/NOR_FLASH_IMAGE.fd"
    if [ -f "$FD" ]; then
        dd if="$FD" of="$OUT_IMG" bs=4K skip=4032 count=48 seek=4032 conv=notrunc status=none
        ok "NV store: 0xFC0000..0xFEFFFF (3 x 64 KB, erased state)"
    fi

    LAYOUT="  0x000000  GPT
  0x008000  idblock (DDR init + SPL)
  0x060000  FIT (BL31 + EDK2 + DTB)
  0xFC0000  NV store / 0xFD0000 FTW Working / 0xFE0000 FTW Spare"
    FLASH="  rkdeveloptool db binaries/rk3576_ddr.bin
  rkdeveloptool wl 0 out/$PLATFORM_NAME/$PLATFORM_NAME-spi.img
  rkdeveloptool rd"
fi

echo
ok "$OUT_IMG  ($(du -h "$OUT_IMG" | cut -f1))"
echo
echo "layout:"; echo "$LAYOUT"
echo
echo "flash:"; echo "$FLASH"
echo
echo "serial is 1500000 8N1; expect:  U-Boot SPL ... -> BL31 ... -> TianoCore"
