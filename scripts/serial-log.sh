#!/usr/bin/env bash
#
# Capture the board's serial output raw, byte for byte.
#
# Do not use a terminal emulator for this.  The UEFI console clears the screen
# a few lines after display bring-up finishes, and a terminal that interprets
# ANSI will silently discard everything in that window -- which is exactly
# where the display diagnostics land.  Three rounds of instrumentation were
# lost that way before anyone noticed.  A log that goes quiet there is not
# evidence the code stopped running; it is evidence the terminal ate it.
#
# The symptom to recognise in a captured file: a run of bare newlines where
# text should be.
#
# Usage:
#   scripts/serial-log.sh boot.log [/dev/ttyACM0]
#
# Start this BEFORE power-cycling the board.  A board that is already up is
# silent, so a logger started afterwards reads nothing and looks broken.
#
set -euo pipefail

OUT="${1:-}"
DEV="${2:-}"
BAUD=1500000

if [ -z "$OUT" ]; then
    echo "usage: $0 <output-file> [device]" >&2
    exit 1
fi

if [ -z "$DEV" ]; then
    for cand in /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyACM1 /dev/ttyUSB1; do
        [ -e "$cand" ] && DEV="$cand" && break
    done
fi
[ -n "$DEV" ] && [ -e "$DEV" ] || {
    echo "no serial device found -- pass one explicitly" >&2
    echo "candidates:" >&2; ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null >&2
    exit 1
}

# Someone else holding the port means this reader gets nothing: the bytes go to
# one reader, not both.  A "quick direct read to check it works" while a logger
# is running proves nothing.
if command -v fuser >/dev/null && fuser "$DEV" >/dev/null 2>&1; then
    echo "warning: $DEV is already open by:" >&2
    fuser -v "$DEV" 2>&1 | sed 's/^/  /' >&2
    echo "  close it first, or this capture will be empty." >&2
fi

stty -F "$DEV" "$BAUD" raw -echo

echo "capturing $DEV at $BAUD -> $OUT"
echo "power-cycle the board now.  Ctrl-C to stop."
echo

# This script exists so it can be killed by name.  `pkill -f 'cat /dev/ttyACM0'`
# matches the shell that launched it and kills that instead.
exec cat "$DEV" >> "$OUT"
