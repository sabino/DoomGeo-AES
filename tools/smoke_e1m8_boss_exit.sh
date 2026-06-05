#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
INITIAL_OUT="${OUT_DIR}/e1m8-boss-initial.png"
COMPLETE_OUT="${OUT_DIR}/e1m8-boss-complete.png"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
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

capture_window() {
    local wid="$1"
    local out="$2"
    local xwd_out="${out%.png}.xwd"
    xwd -silent -id "$wid" -out "$xwd_out"
    convert "$xwd_out" "$out"
}

press_fire() {
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" x
    sleep 0.25
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" x
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=e1m8-boss-test-rom \
SMOKE_RUN_TARGET=e1m8-boss-test-gngeo \
SMOKE_OUTPUT="$INITIAL_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
sleep 0.3

press_fire
sleep 1.4
capture_window "$wid" "$COMPLETE_OUT"

tools/check_exit_screens.py --dir "$OUT_DIR" --initial "$(basename "$INITIAL_OUT")" --complete "$(basename "$COMPLETE_OUT")"

echo "$INITIAL_OUT"
echo "$COMPLETE_OUT"
