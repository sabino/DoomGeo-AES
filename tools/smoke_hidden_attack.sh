#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
AFTER_WAIT_SECS="${HIDDEN_ATTACK_AFTER_WAIT_SECS:-3.0}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
INITIAL_OUT="${OUT_DIR}/hidden-attack-initial.png"
AFTER_OUT="${OUT_DIR}/hidden-attack-after.png"

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

capture_window() {
    local wid="$1"
    local out="$2"
    local xwd_out="${out%.png}.xwd"
    xwd -silent -id "$wid" -out "$xwd_out"
    convert "$xwd_out" "$out"
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

trap cleanup_gngeo EXIT INT TERM
mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=hidden-attack-test-rom \
SMOKE_RUN_TARGET=hidden-attack-test-gngeo \
SMOKE_OUTPUT="$INITIAL_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_KEEP_RUNNING=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
sleep "$AFTER_WAIT_SECS"
capture_window "$wid" "$AFTER_OUT"

echo "$INITIAL_OUT"
echo "$AFTER_OUT"
