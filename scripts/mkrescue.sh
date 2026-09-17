#!/usr/bin/env bash
#
# Build the rescue system as a single UEFI application: a unified kernel image
# holding the kernel, its command line and an initramfs that is the whole
# system.  Nothing is installed on disk, so there is no rootfs to corrupt and
# the result is a file the CI can rebuild byte for byte from pinned inputs.
#
# The kernel comes from a release of the kernel repo, the userland from an
# Alpine minirootfs.  Both are pinned; run with UPDATE=1 to see the checksums a
# new pin would need.
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/lib/common.sh" 2>/dev/null || {
    step() { printf '\n\033[0;36m== %s ==\033[0m\n' "$*"; }
    ok()   { printf '\033[0;32m[ OK ]\033[0m  %s\n' "$*"; }
    die()  { printf '\033[0;31m[FAIL]\033[0m  %s\n' "$*" >&2; exit 1; }
}

BOARD="${1:-}"
[ -n "$BOARD" ] || die "usage: $0 <board>"
CONF="$ROOT/boards/$BOARD.conf"
[ -f "$CONF" ] || die "no such board config: $CONF"
# shellcheck disable=SC1090
. "$CONF"
: "${PLATFORM_NAME:?}"
[ -n "${RESCUE_KERNEL_RELEASE:-}" ] || die "$BOARD does not build a rescue system"

[ "$(uname -m)" = aarch64 ] || die "needs an aarch64 host: the rootfs is populated by running Alpine's own apk"

ALPINE_BRANCH=v3.21
ALPINE_VER=3.21.3
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/$ALPINE_BRANCH/releases/aarch64/alpine-minirootfs-$ALPINE_VER-aarch64.tar.gz"

# Tools a rescue session actually needs: repair a filesystem, open the NVMe
# install, rewrite the partition table, copy files off, reach the network.
ALPINE_PKGS="btrfs-progs e2fsprogs e2fsprogs-extra dosfstools nvme-cli
             sfdisk sgdisk util-linux lsblk blkid parted rsync
             openssh-client ca-certificates nano less pciutils usbutils
             kmod chrony"

# Modules the initramfs has to carry.  eMMC, ext4 and the GMAC are built in;
# these are not, and without them a rescue session cannot see the NVMe install
# or mount its btrfs root.
RESCUE_MODULES="nvme btrfs phy-rockchip-naneng-combphy rockchipdrm
                phy-rockchip-samsung-hdptx motorcomm"

OUT="$ROOT/out/$PLATFORM_NAME"
WORK="$(mktemp -d)"
trap 'sudo rm -rf "$WORK"' EXIT
mkdir -p "$OUT" "$ROOT/.cache"

step "Fetching the kernel ($RESCUE_KERNEL_RELEASE)"
KREPO="${RESCUE_KERNEL_REPO:-gahingwoo/linux-rk3576-npu}"
for f in Image SHA256SUMS; do
    [ -f "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-$f" ] || \
        curl -fsSL -o "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-$f" \
            "https://github.com/$KREPO/releases/download/$RESCUE_KERNEL_RELEASE/$f"
done
MODTAR=$(awk '/modules-.*\.tar\.gz/ {print $2}' "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-SHA256SUMS" | head -1)
[ -n "$MODTAR" ] || die "no modules tarball named in SHA256SUMS"
KVER="${MODTAR#modules-}"; KVER="${KVER%.tar.gz}"
[ -f "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-$MODTAR" ] || \
    curl -fsSL -o "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-$MODTAR" \
        "https://github.com/$KREPO/releases/download/$RESCUE_KERNEL_RELEASE/$MODTAR"

( cd "$ROOT/.cache" && for l in Image "$MODTAR"; do
      want=$(awk -v f="$l" '$2==f{print $1}' "$RESCUE_KERNEL_RELEASE-SHA256SUMS")
      have=$(sha256sum "$RESCUE_KERNEL_RELEASE-$l" | cut -d' ' -f1)
      [ "$want" = "$have" ] || { echo "checksum mismatch for $l"; exit 1; }
  done ) || die "kernel release failed its own SHA256SUMS"
ok "kernel $KVER"

step "Building the initramfs"
curl -fsSL -o "$WORK/alpine.tar.gz" "$ALPINE_URL"
mkdir -p "$WORK/rootfs"
sudo tar xzf "$WORK/alpine.tar.gz" -C "$WORK/rootfs"
sudo cp /etc/resolv.conf "$WORK/rootfs/etc/resolv.conf"
# shellcheck disable=SC2086
sudo chroot "$WORK/rootfs" /sbin/apk add --no-cache $ALPINE_PKGS >/dev/null
sudo rm -f "$WORK/rootfs/etc/resolv.conf" "$WORK/rootfs/var/cache/apk/"*

mkdir -p "$WORK/mod"
tar xzf "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-$MODTAR" -C "$WORK/mod"
MODROOT="$WORK/mod"
[ -d "$MODROOT/lib/modules/$KVER" ] || MODROOT="$WORK/mod/.."
# shellcheck disable=SC2086
for m in $RESCUE_MODULES; do
    modprobe -d "$MODROOT" -S "$KVER" --show-depends "$m" 2>/dev/null \
      | awk '/^insmod/ {print $2}'
done | sort -u | while read -r ko; do
    rel="${ko#$MODROOT/}"
    sudo install -D "$ko" "$WORK/rootfs/$rel"
done
for f in modules.builtin modules.builtin.modinfo modules.order; do
    [ -f "$MODROOT/lib/modules/$KVER/$f" ] && \
        sudo install -D "$MODROOT/lib/modules/$KVER/$f" "$WORK/rootfs/lib/modules/$KVER/$f"
done
sudo depmod -b "$WORK/rootfs" "$KVER"

sudo tee "$WORK/rootfs/init" >/dev/null <<'INIT'
#!/bin/sh
# The whole rescue system runs from RAM; there is no root to pivot to.
mount -t proc     proc     /proc
mount -t sysfs    sysfs    /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts
mount -t tmpfs    tmpfs    /run

for m in nvme btrfs phy-rockchip-naneng-combphy; do modprobe "$m" 2>/dev/null; done
/sbin/mdev -s 2>/dev/null
[ -x /sbin/syslogd ] && /sbin/syslogd
/sbin/udhcpc -i end0 -b -q 2>/dev/null

cat <<'BANNER'

  CM5-IO rescue.  Everything here is in RAM, so nothing you break persists
  and nothing you write survives unless you write it to a disk.

    lsblk                                  what is attached
    mount /dev/nvme0n1p3 /mnt -o subvol=root    the Fedora install
    sfdisk --wipe always /dev/mmcblk0 < layout  put the eMMC table back

BANNER
exec /bin/sh -l
INIT
sudo chmod +x "$WORK/rootfs/init"

( cd "$WORK/rootfs" && sudo find . | sudo cpio -o -H newc --quiet --owner root:root ) \
    | zstd -19 -T0 -q -o "$WORK/initramfs.zst"
ok "initramfs $(numfmt --to=iec < <(stat -c %s "$WORK/initramfs.zst"))"

step "Linking the UKI"
STUB=/usr/lib/systemd/boot/efi/linuxaa64.efi.stub
[ -f "$STUB" ] || die "$STUB missing -- install systemd-boot-efi"
command -v ukify >/dev/null || die "ukify missing -- install systemd-ukify"
printf 'console=ttyS0,1500000n8 console=tty1\n' > "$WORK/cmdline"
cp "$ROOT/.cache/$RESCUE_KERNEL_RELEASE-Image" "$WORK/Image"
ukify build --linux="$WORK/Image" --initrd="$WORK/initramfs.zst" \
            --cmdline=@"$WORK/cmdline" --stub="$STUB" \
            --output="$OUT/rescue.efi" >/dev/null
ok "$OUT/rescue.efi  ($(numfmt --to=iec < <(stat -c %s "$OUT/rescue.efi")))"
