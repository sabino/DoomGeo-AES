#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
USE_XVFB="${SMOKE_XVFB:-0}"
XVFB_SCREEN="${SMOKE_XVFB_SCREEN:-1024x768x24}"
XVFB_PID=""
XVFB_LOG="${SMOKE_XVFB_LOG:-${SMOKE_LOG:-.tools/logs/movement-bench-gngeo.log}.xvfb.log}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
WAIT_SECS="${SMOKE_WAIT_SECS:-5}"
FORWARD_SECS="${STRESS_FORWARD_SECS:-2.5}"
TURN_SECS="${STRESS_TURN_SECS:-2.0}"
STRAFE_SECS="${STRESS_STRAFE_SECS:-2.5}"
EXTRAOPTS_VALUE="${STRESS_EXTRAOPTS:-}"
if [ "${STRESS_SHOWFPS:-0}" = "1" ]; then
    EXTRAOPTS_VALUE="${EXTRAOPTS_VALUE:+$EXTRAOPTS_VALUE }--showfps"
fi
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
    local geometry=""
    if [ "$USE_XVFB" = "1" ]; then
        geometry="$(DISPLAY="$DISPLAY_VALUE" xwininfo -id "$wid" | awk '
            /Absolute upper-left X:/ { x = $NF }
            /Absolute upper-left Y:/ { y = $NF }
            /Width:/ { w = $NF }
            /Height:/ { h = $NF }
            END {
                if (w > 0 && h > 0) {
                    printf "%dx%d+%d+%d", w, h, x, y
                }
            }')"
        DISPLAY="$DISPLAY_VALUE" xwd -silent -root -out "$xwd_out"
        if [ -n "$geometry" ]; then
            convert "$xwd_out" -crop "$geometry" +repage "$out"
        else
            convert "$xwd_out" "$out"
        fi
    else
        if ! DISPLAY="$DISPLAY_VALUE" xwininfo -id "$wid" >/dev/null 2>&1; then
            wid="$(window_for_gngeo)"
        fi
        DISPLAY="$DISPLAY_VALUE" xwd -silent -id "$wid" -out "$xwd_out"
        convert "$xwd_out" "$out"
    fi
}

hold_keys() {
    local wid="$1"
    local seconds="$2"
    shift 2
    local key=""
    if [ "$USE_XVFB" = "1" ]; then
        DISPLAY="$DISPLAY_VALUE" xdotool windowfocus "$wid" >/dev/null 2>&1 || true
        DISPLAY="$DISPLAY_VALUE" xdotool windowactivate "$wid" >/dev/null 2>&1 || true
    fi
    trap 'for key in "$@"; do DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" "$key" >/dev/null 2>&1 || true; done' RETURN
    for key in "$@"; do
        DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" "$key"
    done
    sleep "$seconds"
    for key in "$@"; do
        DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" "$key"
    done
    trap - RETURN
}

move_and_capture() {
    local seconds="$1"
    local out="$2"
    shift
    shift
    local wid=""
    wid="$(window_for_gngeo)"
    hold_keys "$wid" "$seconds" "$@"
    sleep 0.2
    wid="$(window_for_gngeo)"
    capture_window "$wid" "$out"
}

keyup_all() {
    local wid=""
    wid="$(window_for_gngeo)" || return 0
    for key in "$@"; do
        DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" "$key"
    done
}

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert
if [ "$USE_XVFB" = "1" ]; then
    require_cmd Xvfb
    DISPLAY_VALUE="${SMOKE_XVFB_DISPLAY:-:99}"
    WORKSPACE=""
fi

cleanup() {
    if [ -n "$XVFB_PID" ]; then
        kill "$XVFB_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"

if [ "$USE_XVFB" = "1" ]; then
    mkdir -p "$(dirname "$XVFB_LOG")"
    Xvfb "$DISPLAY_VALUE" -screen 0 "$XVFB_SCREEN" >"$XVFB_LOG" 2>&1 &
    XVFB_PID="$!"
    sleep 1
fi

SMOKE_OUTPUT="$START_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_START_GAME=1 \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_XVFB=0 \
SMOKE_EXTRAOPTS="$EXTRAOPTS_VALUE" \
tools/smoke_capture.sh >/dev/null

keyup_all Up Down Left Right z x a s Return space Escape || true

move_and_capture "$FORWARD_SECS" "$FORWARD_OUT" Up

move_and_capture "$TURN_SECS" "$TURN_OUT" Right

move_and_capture "$STRAFE_SECS" "$STRAFE_OUT" z Right

echo "movement stress screenshots OK:"
echo "$START_OUT"
echo "$FORWARD_OUT"
echo "$TURN_OUT"
echo "$STRAFE_OUT"
