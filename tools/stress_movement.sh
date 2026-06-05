#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-2}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
WAIT_SECS="${SMOKE_WAIT_SECS:-5}"
START_OUT="${OUT_DIR}/movement-stress-start.png"
FORWARD_OUT="${OUT_DIR}/movement-stress-forward.png"
TURN_OUT="${OUT_DIR}/movement-stress-turn.png"
STRAFE_OUT="${OUT_DIR}/movement-stress-strafe.png"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

window_for_gngeo() {
    local wid=""
    for _ in $(seq 1 100); do
        for wid in $(DISPLAY="$DISPLAY_VALUE" xdotool search --class ngdevkit-gngeo 2>/dev/null || true) \
                   $(DISPLAY="$DISPLAY_VALUE" xdotool search --name 'Gngeo' 2>/dev/null || true); do
            if [ -z "$wid" ] || ! DISPLAY="$DISPLAY_VALUE" xwininfo -id "$wid" >/dev/null 2>&1; then
                continue
            fi
            if DISPLAY="$DISPLAY_VALUE" xdotool getwindowname "$wid" 2>/dev/null | grep -qi 'Gngeo'; then
                echo "$wid"
                return 0
            fi
        done
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

hold_keys() {
    local seconds="$1"
    shift
    for key in "$@"; do
        DISPLAY="$DISPLAY_VALUE" xdotool keydown "$key"
    done
    sleep "$seconds"
    for key in "$@"; do
        DISPLAY="$DISPLAY_VALUE" xdotool keyup "$key"
    done
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

mkdir -p "$OUT_DIR"

SMOKE_OUTPUT="$START_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_START_GAME=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
DISPLAY="$DISPLAY_VALUE" xdotool windowactivate "$wid" >/dev/null 2>&1 || true
if [ -n "$WORKSPACE" ]; then
    DISPLAY="$DISPLAY_VALUE" xdotool set_desktop_for_window "$wid" "$WORKSPACE" >/dev/null 2>&1 || true
fi

hold_keys 2.5 Up
sleep 0.2
capture_window "$wid" "$FORWARD_OUT"

hold_keys 2.0 Right
sleep 0.2
capture_window "$wid" "$TURN_OUT"

hold_keys 2.5 z Right
sleep 0.2
capture_window "$wid" "$STRAFE_OUT"

echo "movement stress screenshots OK:"
echo "$START_OUT"
echo "$FORWARD_OUT"
echo "$TURN_OUT"
echo "$STRAFE_OUT"
