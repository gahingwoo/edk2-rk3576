#!/usr/bin/env bash
#
# kd-listen.py, wrapped so it can be the session's single long-lived reader.
#
# Two things this adds.
#
# 1. It reattaches when the debug probe re-enumerates.  kd-listen.py holds one
#    fd and, when /dev/ttyACM0 is destroyed and recreated, does not get an
#    error -- it goes deaf silently, which reads as "the board said nothing".
#
# 2. It sets the line rate.  kd-listen.py opens the device and never calls
#    tcsetattr, so whatever stty left behind is what it gets.  1.5 Mbaud here,
#    not 115200: the firmware's SPCR carries BaudRate 0, meaning "use the rate
#    already configured".
#
# Why this replaces the plain serial logger rather than running beside it:
# two readers on one tty split the stream between them and neither log is
# evidence.  kd-listen.py already appends every received byte to its raw file,
# so it is a full serial capture as well as a debugger peer -- the UEFI console
# text before ExitBootServices lands there too.
#
# And it must be a debugger peer, not just a reader.  A target with `debug on`
# retransmits every packet until something acknowledges it; with nobody
# answering, the boot does not advance.  On 2026-09-19 that state also starved
# USB enumeration until the boot stick disappeared, which was first read as a
# storage regression.
#
# Usage: scripts/kd-resilient.sh <raw-file> [log-file] [device] [baud]
set -uo pipefail

RAW="${1:?usage: $0 <raw-file> [log-file] [device] [baud]}"
LOG="${2:-${RAW%.bin}.log}"
DEV="${3:-/dev/ttyACM0}"
BAUD="${4:-1500000}"
HERE="$(cd "$(dirname "$0")" && pwd)"

holders=$(ls -l /proc/*/fd/* 2>/dev/null | grep -a "$DEV" | \
          sed 's|.*/proc/\([0-9]*\)/fd.*|\1|' | grep -v "^$$\$" | sort -u)
if [ -n "$holders" ]; then
    echo "REFUSING TO START: $DEV is already held by PID(s):" >&2
    for p in $holders; do
        echo "  $p  $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null)" >&2
    done
    exit 2
fi

echo "kd listener on $DEV at $BAUD -> raw:$RAW decoded:$LOG"

while true; do
    if [ -e "$DEV" ] && stty -F "$DEV" "$BAUD" raw -echo 2>/dev/null; then
        printf '\n--- [kd] attached %s %s ---\n' "$DEV" "$(date '+%F %T')" >> "$LOG"
        python3 -u "$HERE/kd-listen.py" "$DEV" "$RAW" 2>&1 \
          | while IFS= read -r line; do printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$line"; done >> "$LOG"
        printf '\n--- [kd] detached %s ---\n' "$(date '+%F %T')" >> "$LOG"
    fi
    sleep 0.2
done
