#!/usr/bin/env bash
#
# Build and run the host-side test of NetworkTimeSyncDxe's packet code
# (SntpPacket.c) and of EmbeddedPkg's TimeBaseLib, both compiled unchanged
# against the real MdePkg headers.
#
#   run.sh            offline checks, plus EpochToEfiTime() against Python's
#                     calendar for 20000 random times in 1970..2200
#   run.sh --live     also one real SNTP exchange with each fixed fallback
#                     server, and pool.ntp.org / time.cloudflare.com resolved
#                     through a public DNS server, with our own packets
#
# The host has to be little-endian 64-bit (x86_64 or aarch64), like the
# firmware.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
EDK2="${EDK2_DIR:-$ROOT/third_party/edk2}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

case "$(uname -m)" in
    aarch64) ARCH=AArch64 ;;
    x86_64)  ARCH=X64 ;;
    *) echo "unsupported host $(uname -m)" >&2; exit 1 ;;
esac

# MDEPKG_NDEBUG turns ASSERT() into nothing, so TimeBaseLib needs no DebugLib.
cc -std=gnu11 -O1 -g -Wall -Wno-unused-function -DMDEPKG_NDEBUG \
   -fno-strict-aliasing -fshort-wchar \
   -I"$EDK2/MdePkg/Include" -I"$EDK2/MdePkg/Include/$ARCH" \
   -I"$EDK2/EmbeddedPkg/Include" \
   -o "$OUT/SntpPacketTest" \
   "$HERE/SntpPacketTest.c" "$HERE/../SntpPacket.c" \
   "$EDK2/EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.c"

"$OUT/SntpPacketTest"

python3 - "$OUT/SntpPacketTest" <<'EOF'
import calendar, random, subprocess, sys, time
random.seed(4330)
values = [0, 86399, 86400, 951782400, 2085978495, 2085978496, 4107542400,
          4102444799, 4102444800] + [random.randrange(0, 7258118400) for _ in range(20000)]
out = subprocess.run([sys.argv[1], "epoch"], input="\n".join(map(str, values)),
                     capture_output=True, text=True, check=True).stdout.split("\n")
bad = 0
for v, line in zip(values, out):
    e, d, t, back = line.split()
    want = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(v))
    if f"{d} {t}" != want or int(back) != v:
        bad += 1
        if bad < 5:
            print("EpochToEfiTime mismatch:", v, d, t, back, "want", want)
print(f"EpochToEfiTime/EfiTimeToEpoch: {len(values)} values, {bad} mismatch(es)")
sys.exit(1 if bad else 0)
EOF

if [ "${1:-}" = "--live" ]; then
    rc=0
    for ip in 162.159.200.1 216.239.35.0; do
        "$OUT/SntpPacketTest" ntp "$ip" || rc=1
    done
    for name in pool.ntp.org time.cloudflare.com; do
        "$OUT/SntpPacketTest" dns 1.1.1.1 "$name" || rc=1
    done
    exit $rc
fi
