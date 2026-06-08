#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
TILE_WINDOWS="${SMOKE_TILE_WINDOWS:-0}"
WAIT_SECS="${SMOKE_WAIT_SECS:-10}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
BUILD_TARGET="${KEY_DOOR_BUILD_TARGET:-key-door-test-rom}"
RUN_TARGET="${KEY_DOOR_RUN_TARGET:-key-door-test-gngeo}"
DIRECT_ROM="${KEY_DOOR_DIRECT_ROM:-}"
MAKE_ARGS_VALUE="${SMOKE_MAKE_ARGS:-}"
INITIAL_OUT="${OUT_DIR}/key-door-initial.png"
MISSING_OUT="${OUT_DIR}/key-door-missing-key.png"
PICKED_OUT="${OUT_DIR}/key-door-picked-key.png"
OPENED_OUT="${OUT_DIR}/key-door-opened.png"
THROUGH_OUT="${OUT_DIR}/key-door-through.png"

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
    if [ -z "$wid" ] || ! DISPLAY="$DISPLAY_VALUE" xwininfo -id "$wid" >/dev/null 2>&1; then
        wid="$(window_for_gngeo)"
    fi
    DISPLAY="$DISPLAY_VALUE" xwd -silent -id "$wid" -out "$xwd_out"
    convert "$xwd_out" "$out"
}

tile_window() {
    local wid="$1"
    local floating="enable"
    if [ "$TILE_WINDOWS" = "1" ]; then
        floating="disable"
    fi
    if [ -n "$WORKSPACE" ] && command -v i3-msg >/dev/null 2>&1; then
        i3-msg "[id=\"$wid\"] move container to workspace number $WORKSPACE, floating $floating" >/dev/null 2>&1 || true
    elif [ -n "$WORKSPACE" ] && command -v swaymsg >/dev/null 2>&1; then
        swaymsg '[class="ngdevkit-gngeo"] move container to workspace number '"$WORKSPACE"', floating '"$floating" >/dev/null 2>&1 || true
    elif [ -n "$WORKSPACE" ]; then
        DISPLAY="$DISPLAY_VALUE" xdotool set_desktop_for_window "$wid" "$WORKSPACE" >/dev/null 2>&1 || true
    fi
}

press_d() {
    wid="$(window_for_gngeo)"
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" s
    sleep "${KEY_DOOR_USE_SECS:-0.45}"
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" s
}

hold_up() {
    local seconds="$1"
    wid="$(window_for_gngeo)"
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Up
    sleep "$seconds"
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Up
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert

trap cleanup_gngeo EXIT INT TERM
mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET="$BUILD_TARGET" \
SMOKE_RUN_TARGET="$RUN_TARGET" \
SMOKE_DIRECT_ROM="$DIRECT_ROM" \
SMOKE_OUTPUT="$INITIAL_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_KEEP_RUNNING=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_MAKE_ARGS="$MAKE_ARGS_VALUE" \
tools/smoke_capture.sh >/dev/null

wid="$(window_for_gngeo)"
tile_window "$wid"
sleep 0.3

# GnGeo maps Neo Geo D to keyboard "s" in config.mk. The first press should
# report the missing red key while the second press should open the real E1M2
# red door after walking forward through the staged red key pickup.
press_d
sleep 0.4
capture_window "$wid" "$MISSING_OUT"

hold_up "${KEY_DOOR_KEY_WALK_SECS:-1.25}"
sleep 0.5
capture_window "$wid" "$PICKED_OUT"

press_d
sleep 0.2
press_d
sleep 0.25
capture_window "$wid" "$OPENED_OUT"

hold_up "${KEY_DOOR_THROUGH_WALK_SECS:-0.85}"
sleep 0.4
capture_window "$wid" "$THROUGH_OUT"

tools/check_key_door_screens.py --dir "$OUT_DIR"

echo "$INITIAL_OUT"
echo "$MISSING_OUT"
echo "$PICKED_OUT"
echo "$OPENED_OUT"
echo "$THROUGH_OUT"
