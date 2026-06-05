#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_TARGET="${SMOKE_BUILD_TARGET:-cart}"
RUN_TARGET="${SMOKE_RUN_TARGET:-gngeo}"
DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
TILE_WINDOWS="${SMOKE_TILE_WINDOWS:-0}"
USE_XVFB="${SMOKE_XVFB:-0}"
XVFB_SCREEN="${SMOKE_XVFB_SCREEN:-1024x768x24}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
START_GAME="${SMOKE_START_GAME:-0}"
EXTRAOPTS_VALUE="${SMOKE_EXTRAOPTS:-}"
OUT="${SMOKE_OUTPUT:-.tools/screens/latest/smoke.png}"
LOG="${SMOKE_LOG:-.tools/logs/smoke-gngeo.log}"
XWD_OUT="${OUT%.png}.xwd"
MAKE_BIN="${MAKE:-make}"
MAKE_ARGS_VALUE="${SMOKE_MAKE_ARGS:-}"
LOCKDIR="${SMOKE_LOCKDIR:-.tools/locks/smoke-capture.lock}"
LOCK_OWNER="$LOCKDIR/pid"
LOCK_ACQUIRED=0
XVFB_PID=""
MAKE_ARGS=()
MAKE_ROM_DIR=""

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

kill_old_gngeo() {
    pgrep -af 'ngdevkit-gngeo|gngeo' | awk '$0 !~ /pgrep/ {print $1}' | xargs -r kill -9 || true
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

smoke_lock_is_stale() {
    local owner_pid=""
    if [ -f "$LOCK_OWNER" ]; then
        owner_pid="$(cat "$LOCK_OWNER" 2>/dev/null || true)"
        if [ -n "$owner_pid" ] && kill -0 "$owner_pid" 2>/dev/null; then
            return 1
        fi
        return 0
    fi

    # Older helper revisions created only the directory. If no other capture
    # wrapper is alive, treat that directory-only lock as stale.
    if pgrep -af 'tools/smoke_capture.sh' | awk -v self="$$" '$1 != self && $0 !~ /pgrep/ {found=1} END {exit found ? 0 : 1}'; then
        return 1
    fi
    return 0
}

require_cmd "$MAKE_BIN"
require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert
if [ "$USE_XVFB" = "1" ]; then
    require_cmd Xvfb
    DISPLAY_VALUE="${SMOKE_XVFB_DISPLAY:-:99}"
    WORKSPACE=""
    TILE_WINDOWS=0
fi

cleanup() {
    if [ -n "$XVFB_PID" ]; then
        kill "$XVFB_PID" >/dev/null 2>&1 || true
    fi
    if [ "$LOCK_ACQUIRED" = 1 ]; then
        rm -rf "$LOCKDIR"
    fi
}

mkdir -p "$(dirname "$OUT")" "$(dirname "$LOG")" "$(dirname "$LOCKDIR")"
if [ -n "$MAKE_ARGS_VALUE" ]; then
    # shellcheck disable=SC2206
    MAKE_ARGS=($MAKE_ARGS_VALUE)
    for arg in "${MAKE_ARGS[@]}"; do
        case "$arg" in
            ROM=*) MAKE_ROM_DIR="${arg#ROM=}" ;;
        esac
    done
fi

for _ in $(seq 1 300); do
    if mkdir "$LOCKDIR" 2>/dev/null; then
        LOCK_ACQUIRED=1
        echo "$$" > "$LOCK_OWNER"
        trap cleanup EXIT INT TERM
        break
    fi
    if smoke_lock_is_stale; then
        rm -rf "$LOCKDIR"
        continue
    fi
    sleep 0.2
done

if [ "$LOCK_ACQUIRED" != 1 ]; then
    echo "could not acquire smoke capture lock: $LOCKDIR" >&2
    exit 1
fi

"$MAKE_BIN" "${MAKE_ARGS[@]}" "$BUILD_TARGET"
if [ -n "$MAKE_ROM_DIR" ] && [ ! -f "$MAKE_ROM_DIR/neogeo.zip" ] && [ -f build/rom/neogeo.zip ]; then
    cp build/rom/neogeo.zip "$MAKE_ROM_DIR/neogeo.zip"
fi
kill_old_gngeo

if [ "$USE_XVFB" = "1" ]; then
    Xvfb "$DISPLAY_VALUE" -screen 0 "$XVFB_SCREEN" >"${LOG%.log}-xvfb.log" 2>&1 &
    XVFB_PID="$!"
    sleep 1
fi

run_args=("$RUN_TARGET")
if [ -n "$EXTRAOPTS_VALUE" ]; then
    run_args+=("EXTRAOPTS=$EXTRAOPTS_VALUE")
fi

setsid env DISPLAY="$DISPLAY_VALUE" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 \
    "$MAKE_BIN" "${MAKE_ARGS[@]}" "${run_args[@]}" >"$LOG" 2>&1 < /dev/null &
sleep "$WAIT_SECS"

wid="$(window_for_gngeo)"
tile_window "$wid"
if [ "$START_GAME" = "1" ]; then
    sleep 0.2
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" x
    sleep 0.25
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" x
    sleep 1.5
fi
sleep 0.5

if [ "$USE_XVFB" = "1" ]; then
    DISPLAY="$DISPLAY_VALUE" xwd -silent -root -out "$XWD_OUT"
else
    DISPLAY="$DISPLAY_VALUE" xwd -silent -id "$wid" -out "$XWD_OUT"
fi
convert "$XWD_OUT" "$OUT"
echo "$OUT"
