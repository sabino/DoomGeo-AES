#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MAP="${DOOM_MAP:-E1M1}"
STAMP="$(date +%Y%m%d-%H%M%S)"
SCREENDIR=".tools/screens"
LOGDIR=".tools/logs"
COMPARISON_WORKSPACE="${COMPARISON_WORKSPACE:-}"
FREEDOOM_VERSION="${FREEDOOM_VERSION:-0.13.0}"
FREEDOOM_ZIP=".tools/assets/freedoom-${FREEDOOM_VERSION}.zip"
FREEDOOM_DIR=".tools/assets/freedoom-${FREEDOOM_VERSION}"
FREEDOOM_WAD="${FREEDOOM_DIR}/freedoom1.wad"
DOOM_IWAD="${DOOM_IWAD:-.tools/assets/doom1.wad.zip}"
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
        if [ -n "$pid" ]; then
            wid="$(xdotool search --onlyvisible --pid "$pid" 2>/dev/null | tail -n1 || true)"
        fi
        if [ -z "$wid" ] && [ -n "$name" ]; then
            wid="$(xdotool search --onlyvisible --name "$name" 2>/dev/null | tail -n1 || true)"
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
    if [ -n "$COMPARISON_WORKSPACE" ] && command -v i3-msg >/dev/null 2>&1; then
        i3-msg workspace "$COMPARISON_WORKSPACE" >/dev/null 2>&1 || true
    fi
}

capture_window() {
    local pid="$1"
    local name="$2"
    local out="$3"
    local wid=""
    local xwd_out="${out%.png}.xwd"
    for _ in $(seq 1 10); do
        wid="$(window_for_pid_or_name "$pid" "$name" || true)"
        if [ -n "$wid" ] && xwininfo -id "$wid" >/dev/null 2>&1; then
            xdotool windowactivate "$wid" >/dev/null 2>&1 || true
            sleep 0.3
            rm -f "$xwd_out"
            if xwd -silent -id "$wid" -out "$xwd_out" >/dev/null 2>&1 && [ -s "$xwd_out" ]; then
                convert "$xwd_out" "$out"
                return 0
            fi
        fi
        sleep 0.3
    done
    echo "failed to capture window for pid=$pid name=$name" >&2
    return 1
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

native_iwad="$DOOM_IWAD"
if [[ "$native_iwad" == *.zip ]]; then
    native_member="$(unzip -Z1 "$native_iwad" | grep -Ei '\.wad$' | head -n1 || true)"
    if [ -z "$native_member" ]; then
        echo "no WAD member found inside ${native_iwad}" >&2
        exit 1
    fi
    native_extract_root=".tools/assets/native-$(basename "${native_iwad%.zip}")"
    native_iwad="${native_extract_root}/${native_member}"
    if [ ! -f "$native_iwad" ]; then
        mkdir -p "$native_extract_root"
        unzip -q "$DOOM_IWAD" "$native_member" -d "$native_extract_root"
    fi
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
    "$native_bin" -iwad "$native_iwad" -warp "$episode" "$level" -skill 1 -nomusic -nosound -window \
    > "${LOGDIR}/native-doom-compare.log" 2>&1 < /dev/null &
native_pid=$!
disown "$native_pid" 2>/dev/null || true
sleep 1.2
capture_window "$native_pid" "$native_title" "$native_png"

setsid env SDL_VIDEODRIVER=x11 make gngeo > "${LOGDIR}/gngeo-compare.log" 2>&1 < /dev/null &
make_pid=$!
disown "$make_pid" 2>/dev/null || true
sleep 4.0
neo_pid=""
for _ in $(seq 1 40); do
    neo_pid="$(pgrep -n -x ngdevkit-gngeo 2>/dev/null || true)"
    if [ -n "$neo_pid" ]; then
        break
    fi
    sleep 0.1
done
if [ -z "$neo_pid" ]; then
    echo "ngdevkit-gngeo process not found" >&2
    exit 1
fi
capture_window "$neo_pid" "" "$neogeo_png"

convert "$native_png" -resize 640x480\> "${native_png%.png}-fit.png"
convert "$neogeo_png" -resize 640x480\> "${neogeo_png%.png}-fit.png"
convert "${native_png%.png}-fit.png" "${neogeo_png%.png}-fit.png" +append "$side_png"

cleanup_spawned
trap - EXIT

echo "native: $native_png"
echo "neogeo: $neogeo_png"
echo "compare: $side_png"
