#!/usr/bin/env bash
#
# serial-log-resilient.sh, but every line carries the host's wall-clock time.
#
# Why this exists: on 2026-09-20 an eMMC probe added roughly five minutes to a
# WinPE-era boot, and the plain capture could not say where the time went --
# the log is one unbroken stream with no way to tell a fast retry from a
# multi-second timeout loop.  Three different candidate loops in SdMmcPciHcDxe
# fit the evidence equally well, and picking between them by reading code is
# guesswork.  With per-line stamps the gap is simply visible.
#
# Everything else matches serial-log-resilient.sh: it refuses to start if
# another process already holds the device (two readers split the stream
# between them and neither log is evidence), and it reattaches when the debug
# probe re-enumerates instead of going silently deaf.
#
# Usage: scripts/serial-log-stamped.sh <output-file> [device] [baud]
set -uo pipefail

OUT="${1:?usage: $0 <output-file> [device] [baud]}"
DEV="${2:-/dev/ttyACM0}"
BAUD="${3:-1500000}"

holders=$(ls -l /proc/*/fd/* 2>/dev/null | grep -a "$DEV" | \
          sed 's|.*/proc/\([0-9]*\)/fd.*|\1|' | grep -v "^$$\$" | sort -u)
if [ -n "$holders" ]; then
    echo "REFUSING TO START: $DEV is already held by PID(s):" >&2
    for p in $holders; do
        echo "  $p  $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null)" >&2
    done
    echo "Two readers split the stream between them.  Kill those first." >&2
    exit 2
fi

echo "capturing $DEV at $BAUD -> $OUT  (timestamped, reconnects automatically)"

# Stamp each line as it arrives.  Reads bytes rather than lines so a device
# that stops mid-line does not strand the last fragment in a buffer, and
# flushes every write so the log is readable while the board is still running.
stamp() {
    python3 -u -c '
import sys, time
out = sys.stdout
buf = bytearray()
fresh = True
while True:
    b = sys.stdin.buffer.read(1)
    if not b:
        break
    if fresh:
        out.write(time.strftime("[%H:%M:%S] "))
        fresh = False
    if b == b"\n":
        out.write(buf.decode("utf-8", "replace") + "\n")
        out.flush()
        buf.clear()
        fresh = True
    elif b != b"\r":
        buf += b
if buf:
    out.write(buf.decode("utf-8", "replace") + "\n")
    out.flush()
'
}

while true; do
    if [ -e "$DEV" ]; then
        if stty -F "$DEV" "$BAUD" raw -echo 2>/dev/null; then
            printf '\n--- [serial-log] attached %s %s ---\n' "$DEV" "$(date '+%F %T')" >> "$OUT"
            cat "$DEV" 2>/dev/null | stamp >> "$OUT"
            printf '\n--- [serial-log] device went away %s ---\n' "$(date '+%F %T')" >> "$OUT"
        fi
    fi
    sleep 0.2
done
