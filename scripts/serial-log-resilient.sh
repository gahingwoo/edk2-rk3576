#!/usr/bin/env bash
#
# Raw serial capture that survives the adapter re-enumerating.
#
# Why this exists: the Raspberry Pi Debug Probe on this host re-enumerates
# (USB passthrough flap, or the board's power cycle taking the probe with it).
# When it does, /dev/ttyACM0 is destroyed and recreated -- and a reader holding
# the old character device does NOT get an error and does NOT exit.  It just
# goes deaf, silently, forever.  minicom and Tabby behave the same way: the
# window stays open and empty, which reads as "the board said nothing".
#
# So: poll for the device, (re)configure it, read until it goes away, repeat.
# Every (re)connect is stamped into the log so a gap is visible rather than
# being mistaken for the board going quiet.
#
# Usage: scripts/serial-log-resilient.sh <output-file> [device] [baud]
set -uo pipefail

OUT="${1:?usage: $0 <output-file> [device] [baud]}"
DEV="${2:-/dev/ttyACM0}"
BAUD="${3:-1500000}"

#
# Refuse to start if something else already holds the device.  Two readers on
# the same tty do not each get a copy -- the kernel hands each byte to exactly
# one of them, so both logs come out shredded and neither is evidence.  This
# cost a full test round on 2026-09-17: two orphaned `cat` processes from
# earlier runs were still attached, and the resulting garbled output was first
# misread as the firmware overrunning the UART.
#
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

echo "capturing $DEV at $BAUD -> $OUT  (reconnects automatically; Ctrl-C to stop)"

while true; do
    if [ -e "$DEV" ]; then
        if stty -F "$DEV" "$BAUD" raw -echo 2>/dev/null; then
            printf '\n--- [serial-log] attached %s %s ---\n' "$DEV" "$(date '+%F %T')" >> "$OUT"
            cat "$DEV" >> "$OUT" 2>/dev/null
            printf '\n--- [serial-log] device went away %s ---\n' "$(date '+%F %T')" >> "$OUT"
        fi
    fi
    sleep 0.2
done
