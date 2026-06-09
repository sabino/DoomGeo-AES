#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-5}"
DEATH_WAIT_SECS="${COMBAT_DEATH_WAIT_SECS:-2.5}"
REVEAL_STEP_SECS="${COMBAT_DEATH_REVEAL_STEP_SECS:-0.45}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
INITIAL_OUT="${OUT_DIR}/combat-initial.png"
FIRED_OUT="${OUT_DIR}/combat-fired.png"
DEATH_OUT="${OUT_DIR}/combat-death.png"

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

press_fire() {
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" x
    sleep 0.25
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" x
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

trap cleanup_gngeo EXIT INT TERM
mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=combat-test-rom \
SMOKE_RUN_TARGET=combat-test-gngeo \
SMOKE_OUTPUT="$INITIAL_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_KEEP_RUNNING=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
sleep 0.3

# GnGeo maps Neo Geo B to keyboard "x" in config.mk. The combat ROM starts
# with a visible imp centered in the shotgun spread, so one held fire tap should
# show weapon feedback and transition the imp into its death/corpse path.
press_fire
sleep 0.15
capture_window "$wid" "$FIRED_OUT"

sleep "$DEATH_WAIT_SECS"
DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Down
sleep "$REVEAL_STEP_SECS"
DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Down
sleep 0.2
capture_window "$wid" "$DEATH_OUT"

echo "$INITIAL_OUT"
echo "$FIRED_OUT"
echo "$DEATH_OUT"
