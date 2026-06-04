#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_TARGET="${SMOKE_BUILD_TARGET:-cart}"
RUN_TARGET="${SMOKE_RUN_TARGET:-gngeo}"
DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-2}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
OUT="${SMOKE_OUTPUT:-.tools/screens/latest/smoke.png}"
LOG="${SMOKE_LOG:-.tools/logs/smoke-gngeo.log}"
XWD_OUT="${OUT%.png}.xwd"
MAKE_BIN="${MAKE:-make}"
LOCKDIR="${SMOKE_LOCKDIR:-.tools/locks/smoke-capture.lock}"
LOCK_OWNER="$LOCKDIR/pid"
LOCK_ACQUIRED=0

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

kill_old_gngeo() {
    pgrep -af 'ngdevkit-gngeo|gngeo' | awk '$0 !~ /pgrep/ {print $1}' | xargs -r kill -9 || true
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

mkdir -p "$(dirname "$OUT")" "$(dirname "$LOG")" "$(dirname "$LOCKDIR")"

for _ in $(seq 1 300); do
    if mkdir "$LOCKDIR" 2>/dev/null; then
        LOCK_ACQUIRED=1
        echo "$$" > "$LOCK_OWNER"
        trap 'rm -rf "$LOCKDIR"' EXIT INT TERM
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

"$MAKE_BIN" "$BUILD_TARGET"
kill_old_gngeo

setsid env DISPLAY="$DISPLAY_VALUE" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 \
    "$MAKE_BIN" "$RUN_TARGET" >"$LOG" 2>&1 < /dev/null &
sleep "$WAIT_SECS"

wid="$(window_for_gngeo)"
if [ -n "$WORKSPACE" ]; then
    DISPLAY="$DISPLAY_VALUE" xdotool set_desktop_for_window "$wid" "$WORKSPACE" >/dev/null 2>&1 || true
fi
DISPLAY="$DISPLAY_VALUE" xdotool windowactivate "$wid" >/dev/null 2>&1 || true
sleep 0.2

xwd -silent -id "$wid" -out "$XWD_OUT"
convert "$XWD_OUT" "$OUT"
echo "$OUT"
