#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-10}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
BEFORE_OUT="${OUT_DIR}/weapon-shortcut-before.png"
AFTER_OUT="${OUT_DIR}/weapon-shortcut-cdown.png"
HELD_OUT="${OUT_DIR}/weapon-shortcut-held-c-right.png"
BEFORE_XWD="${BEFORE_OUT%.png}.xwd"
AFTER_XWD="${AFTER_OUT%.png}.xwd"
HELD_XWD="${HELD_OUT%.png}.xwd"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

cleanup_gngeo() {
    pkill -9 -x ngdevkit-gngeo >/dev/null 2>&1 || true
}

window_for_gngeo() {
    local wid=""
    for _ in $(seq 1 100); do
        wid="$(DISPLAY="$DISPLAY_VALUE" xdotool search --class ngdevkit-gngeo 2>/dev/null | tail -n 1 || true)"
        if [ -n "$wid" ] && DISPLAY="$DISPLAY_VALUE" xwininfo -id "$wid" >/dev/null 2>&1; then
            echo "$wid"
            return 0
        fi
        sleep 0.1
    done
    echo "ngdevkit-gngeo window not found" >&2
    return 1
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

trap cleanup_gngeo EXIT INT TERM
mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=arsenal-test-rom \
SMOKE_RUN_TARGET=arsenal-test-gngeo \
SMOKE_OUTPUT="$BEFORE_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_KEEP_RUNNING=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
sleep 0.3

# GnGeo maps Neo Geo C to the keyboard "a" key in config.mk. In shareware
# builds the arsenal ROM starts on rocket because plasma/BFG psprites are
# unavailable, so Down+C should stay on rocket. Hold the chord long enough for
# GnGeo's input polling to sample it across a frame.
DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Down
sleep 0.1
DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" a
sleep 0.25
DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" a
sleep 0.1
DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Down
sleep 0.8

xwd -silent -id "$wid" -out "$AFTER_XWD"
convert "$AFTER_XWD" "$AFTER_OUT"

# Keep C held first, then press a direction. This catches the documented
# "hold C + D-pad" flow instead of only the opposite keydown ordering.
DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" a
sleep 0.15
DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Right
sleep 0.25
DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Right
sleep 0.1
DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" a
sleep 0.8

xwd -silent -id "$wid" -out "$HELD_XWD"
convert "$HELD_XWD" "$HELD_OUT"

tools/check_weapon_shortcut_screens.py --dir "$OUT_DIR"

echo "$BEFORE_OUT"
echo "$AFTER_OUT"
echo "$HELD_OUT"
