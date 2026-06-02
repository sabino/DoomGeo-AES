#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MAP="${DOOM_MAP:-E1M1}"
STAMP="$(date +%Y%m%d-%H%M%S)"
SCREENDIR=".tools/screens"
LOGDIR=".tools/logs"
COMPARISON_WORKSPACE="${COMPARISON_WORKSPACE:-2}"
FREEDOOM_VERSION="${FREEDOOM_VERSION:-0.13.0}"
FREEDOOM_ZIP=".tools/assets/freedoom-${FREEDOOM_VERSION}.zip"
FREEDOOM_DIR=".tools/assets/freedoom-${FREEDOOM_VERSION}"
FREEDOOM_WAD="${FREEDOOM_DIR}/freedoom1.wad"
native_pid=""
make_pid=""

mkdir -p "$SCREENDIR" "$LOGDIR"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

doom_runner() {
    if command -v crispy-doom >/dev/null 2>&1; then
        command -v crispy-doom
    elif command -v chocolate-doom >/dev/null 2>&1; then
        command -v chocolate-doom
    elif command -v dsda-doom >/dev/null 2>&1; then
        command -v dsda-doom
    else
        echo "no supported native Doom runner found" >&2
        exit 1
    fi
}

doom_window_title() {
    case "$(basename "$1")" in
        crispy-doom) echo "Crispy Doom" ;;
        chocolate-doom) echo "Chocolate Doom" ;;
        dsda-doom) echo "DSDA-Doom" ;;
        *) echo "$(basename "$1")" ;;
    esac
}

window_for_pid_or_name() {
    local pid="$1"
    local name="$2"
    local wid=""
    for _ in $(seq 1 80); do
        wid="$(xdotool search --onlyvisible --name "$name" 2>/dev/null | tail -n1 || true)"
        if [ -z "$wid" ]; then
            wid="$(xdotool search --onlyvisible --pid "$pid" 2>/dev/null | tail -n1 || true)"
        fi
        if [ -n "$wid" ] && xwininfo -id "$wid" >/dev/null 2>&1; then
            echo "$wid"
            return 0
        fi
        sleep 0.1
    done
    echo "window not found for pid=$pid name=$name" >&2
    return 1
}

kill_old_runners() {
    pkill -9 -x ngdevkit-gngeo >/dev/null 2>&1 || true
    pkill -9 -x crispy-doom >/dev/null 2>&1 || true
    pkill -9 -x chocolate-doom >/dev/null 2>&1 || true
    pkill -9 -x dsda-doom >/dev/null 2>&1 || true
}

cleanup_spawned() {
    kill_old_runners
}

switch_workspace() {
    if command -v i3-msg >/dev/null 2>&1; then
        i3-msg workspace "$COMPARISON_WORKSPACE" >/dev/null 2>&1 || true
    fi
}

capture_window() {
    local wid="$1"
    local out="$2"
    xdotool windowactivate "$wid" >/dev/null 2>&1 || true
    sleep 0.2
    xwd -silent -id "$wid" -out "${out%.png}.xwd"
    convert "${out%.png}.xwd" "$out"
}

episode="${MAP:1:1}"
level="${MAP:3:1}"
if [[ ! "$MAP" =~ ^E[0-9]M[0-9]$ ]]; then
    echo "capture_compare.sh currently expects an ExMy map name; got ${MAP}" >&2
    exit 1
fi

require_cmd xdotool
require_cmd xwininfo
require_cmd xwd
require_cmd convert
require_cmd unzip

if [ ! -f "$FREEDOOM_ZIP" ]; then
    make "$FREEDOOM_ZIP"
fi
if [ ! -f "$FREEDOOM_WAD" ]; then
    unzip -q "$FREEDOOM_ZIP" "freedoom-${FREEDOOM_VERSION}/freedoom1.wad" -d .tools/assets
fi

native_bin="$(doom_runner)"
native_title="$(doom_window_title "$native_bin")"
native_png="${SCREENDIR}/compare-${MAP}-${STAMP}-native.png"
neogeo_png="${SCREENDIR}/compare-${MAP}-${STAMP}-neogeo.png"
side_png="${SCREENDIR}/compare-${MAP}-${STAMP}-side-by-side.png"

kill_old_runners
sleep 0.3
switch_workspace
trap cleanup_spawned EXIT

setsid env SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
    "$native_bin" -iwad "$FREEDOOM_WAD" -warp "$episode" "$level" -skill 1 -nomusic -nosound -window \
    > "${LOGDIR}/native-doom-compare.log" 2>&1 < /dev/null &
native_pid=$!
disown "$native_pid" 2>/dev/null || true
sleep 1.2
native_wid="$(window_for_pid_or_name "$native_pid" "$native_title")"
capture_window "$native_wid" "$native_png"

setsid env SDL_VIDEODRIVER=x11 make gngeo > "${LOGDIR}/gngeo-compare.log" 2>&1 < /dev/null &
make_pid=$!
disown "$make_pid" 2>/dev/null || true
sleep 2.0
neo_wid="$(window_for_pid_or_name "$make_pid" "GnGeo")"
capture_window "$neo_wid" "$neogeo_png"

convert "$native_png" -resize 640x480\> "${native_png%.png}-fit.png"
convert "$neogeo_png" -resize 640x480\> "${neogeo_png%.png}-fit.png"
convert "${native_png%.png}-fit.png" "${neogeo_png%.png}-fit.png" +append "$side_png"

cleanup_spawned
trap - EXIT

echo "native: $native_png"
echo "neogeo: $neogeo_png"
echo "compare: $side_png"
