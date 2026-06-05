#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MAP="${DOOM_MAP:-E1M1}"
DOOM_SKILL="${DOOM_SKILL:-4}"
WAYPOINT="${COMPARE_WAYPOINT:-start}"
STAMP="$(date +%Y%m%d-%H%M%S)"
SCREENDIR=".tools/screens"
LOGDIR=".tools/logs"
COMPARISON_WORKSPACE="${COMPARISON_WORKSPACE:-}"
LOCKDIR="${COMPARE_LOCKDIR:-${SMOKE_LOCKDIR:-.tools/locks/smoke-capture.lock}}"
LOCK_OWNER="$LOCKDIR/pid"
LOCK_ACQUIRED=0
FREEDOOM_VERSION="${FREEDOOM_VERSION:-0.13.0}"
FREEDOOM_ZIP=".tools/assets/freedoom-${FREEDOOM_VERSION}.zip"
FREEDOOM_DIR=".tools/assets/freedoom-${FREEDOOM_VERSION}"
FREEDOOM_WAD="${FREEDOOM_DIR}/freedoom1.wad"
DOOM_IWAD="${DOOM_IWAD:-.tools/assets/doom1.wad.zip}"
native_pid=""
make_pid=""
neo_make_target="gngeo"
neo_make_args=()
neo_press_start="1"
neo_settle_secs="${COMPARE_NEO_SETTLE_SECS:-}"
waypoint_note=""
route_mode="${COMPARE_ROUTE_MODE:-scripted}"
native_move_modifier="${COMPARE_NATIVE_MOVE_MODIFIER:-Shift_L}"

mkdir -p "$SCREENDIR" "$LOGDIR" "$(dirname "$LOCKDIR")"

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
    if [ -n "$make_pid" ]; then
        kill -- "-${make_pid}" >/dev/null 2>&1 || true
    fi
    kill_old_runners
    if [ "$LOCK_ACQUIRED" = 1 ]; then
        rm -rf "$LOCKDIR"
    fi
}

capture_lock_is_stale() {
    local owner_pid=""
    if [ -f "$LOCK_OWNER" ]; then
        owner_pid="$(cat "$LOCK_OWNER" 2>/dev/null || true)"
        if [ -n "$owner_pid" ] && kill -0 "$owner_pid" 2>/dev/null; then
            return 1
        fi
        return 0
    fi

    if pgrep -af 'tools/(smoke_capture|capture_compare)\.sh' | awk -v self="$$" '$1 != self && $0 !~ /pgrep/ {found=1} END {exit found ? 0 : 1}'; then
        return 1
    fi
    return 0
}

acquire_capture_lock() {
    for _ in $(seq 1 300); do
        if mkdir "$LOCKDIR" 2>/dev/null; then
            LOCK_ACQUIRED=1
            echo "$$" > "$LOCK_OWNER"
            trap cleanup_spawned EXIT INT TERM
            return 0
        fi
        if capture_lock_is_stale; then
            rm -rf "$LOCKDIR"
            continue
        fi
        sleep 0.2
    done

    echo "could not acquire capture lock: $LOCKDIR" >&2
    exit 1
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
    local mean=""
    for _ in $(seq 1 10); do
        wid="$(window_for_pid_or_name "$pid" "$name" || true)"
        if [ -n "$wid" ] && xwininfo -id "$wid" >/dev/null 2>&1; then
            xdotool windowactivate "$wid" >/dev/null 2>&1 || true
            sleep 0.3
            rm -f "$xwd_out"
            if xwd -silent -id "$wid" -out "$xwd_out" >/dev/null 2>&1 && [ -s "$xwd_out" ]; then
                convert "$xwd_out" "$out"
                mean="$(convert "$out" -colorspace RGB -format "%[fx:mean]" info: 2>/dev/null || echo 0)"
                if awk -v mean="$mean" 'BEGIN { exit (mean > 0.01) ? 0 : 1 }'; then
                    return 0
                fi
            fi
        fi
        sleep 0.3
    done
    echo "failed to capture window for pid=$pid name=$name" >&2
    return 1
}

hold_key() {
    local wid="$1"
    local key="$2"
    local secs="$3"
    xdotool windowactivate "$wid" >/dev/null 2>&1 || true
    xdotool keydown "$key"
    sleep "$secs"
    xdotool keyup "$key"
    sleep 0.15
}

hold_move_key() {
    local wid="$1"
    local key="$2"
    local secs="$3"
    local modifier="${4:-}"
    xdotool windowactivate "$wid" >/dev/null 2>&1 || true
    if [ -n "$modifier" ]; then
        xdotool keydown "$modifier"
    fi
    xdotool keydown "$key"
    sleep "$secs"
    xdotool keyup "$key"
    if [ -n "$modifier" ]; then
        xdotool keyup "$modifier"
    fi
    sleep 0.15
}

tap_key() {
    local wid="$1"
    local key="$2"
    xdotool windowactivate "$wid" >/dev/null 2>&1 || true
    xdotool key "$key"
    sleep 0.15
}

drive_waypoint_script() {
    local wid="$1"
    local use_key="$2"
    local move_modifier="${3:-}"
    case "$WAYPOINT" in
        start|e1m1-start|e1m2-start)
            return 0
            ;;
        e1m1-encounter)
            hold_move_key "$wid" Up 1.0 "$move_modifier"
            hold_key "$wid" Right 0.35
            ;;
        e1m1-scout)
            hold_move_key "$wid" Up 1.8 "$move_modifier"
            hold_key "$wid" Left 0.85
            hold_move_key "$wid" Up 0.55 "$move_modifier"
            ;;
        e1m2-keydoor)
            hold_move_key "$wid" Up 1.2 "$move_modifier"
            hold_key "$wid" Left 0.45
            hold_move_key "$wid" Up 0.8 "$move_modifier"
            tap_key "$wid" "$use_key"
            ;;
        *)
            echo "unknown COMPARE_WAYPOINT=${WAYPOINT}" >&2
            exit 1
            ;;
    esac
}

drive_native_waypoint() {
    local pid="$1"
    local title="$2"
    local wid=""
    case "$WAYPOINT" in
        start|e1m1-start|e1m2-start)
            return 0
            ;;
    esac
    wid="$(window_for_pid_or_name "$pid" "$title")"
    drive_waypoint_script "$wid" space "$native_move_modifier"
}

drive_neo_waypoint() {
    local pid="$1"
    local wid=""
    case "$WAYPOINT" in
        start|e1m1-start|e1m2-start)
            return 0
            ;;
    esac
    wid="$(window_for_pid_or_name "$pid" "")"
    drive_waypoint_script "$wid" s
}

configure_waypoint() {
    case "$WAYPOINT" in
        start)
            neo_make_target="episode-map-gngeo"
            neo_make_args=("EPISODE_MAP=${MAP}")
            ;;
        e1m1-start)
            MAP="E1M1"
            neo_make_target="episode-map-gngeo"
            neo_make_args=("EPISODE_MAP=E1M1")
            ;;
        e1m2-start)
            MAP="E1M2"
            neo_make_target="episode-map-gngeo"
            neo_make_args=("EPISODE_MAP=E1M2")
            ;;
        e1m1-encounter)
            MAP="E1M1"
            if [ "$route_mode" = "focused" ]; then
                neo_make_target="encounter-test-gngeo"
                neo_press_start="0"
                waypoint_note="focused mode uses scripted native movement and a staged Neo Geo verification ROM; camera poses are not exact"
            else
                neo_make_target="episode-map-gngeo"
                neo_make_args=("EPISODE_MAP=E1M1")
                neo_press_start="1"
                waypoint_note="route waypoint uses the same timed input script from the E1M1 start in both engines; native movement holds ${native_move_modifier:-no speed modifier}"
            fi
            neo_settle_secs="${neo_settle_secs:-1.5}"
            ;;
        e1m1-scout)
            MAP="E1M1"
            if [ "$route_mode" = "focused" ]; then
                neo_make_target="scout-test-gngeo"
                neo_press_start="0"
                waypoint_note="focused mode uses scripted native movement and a staged Neo Geo verification ROM; camera poses are not exact"
            else
                neo_make_target="episode-map-gngeo"
                neo_make_args=("EPISODE_MAP=E1M1")
                neo_press_start="1"
                waypoint_note="route waypoint uses the same timed input script from the E1M1 start in both engines; native movement holds ${native_move_modifier:-no speed modifier}"
            fi
            neo_settle_secs="${neo_settle_secs:-1.5}"
            ;;
        e1m2-keydoor)
            MAP="E1M2"
            if [ "$route_mode" = "focused" ]; then
                neo_make_target="key-door-test-gngeo"
                neo_press_start="0"
                waypoint_note="focused mode uses scripted native movement and a staged Neo Geo verification ROM; camera poses are not exact"
            else
                neo_make_target="episode-map-gngeo"
                neo_make_args=("EPISODE_MAP=E1M2")
                neo_press_start="1"
                waypoint_note="route waypoint uses the same timed input script from the E1M2 start in both engines; native movement holds ${native_move_modifier:-no speed modifier}"
            fi
            neo_settle_secs="${neo_settle_secs:-1.5}"
            ;;
        *)
            echo "unknown COMPARE_WAYPOINT=${WAYPOINT}" >&2
            echo "supported: start, e1m1-start, e1m2-start, e1m1-encounter, e1m1-scout, e1m2-keydoor" >&2
            exit 1
            ;;
    esac
}

configure_waypoint

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
require_cmd awk

acquire_capture_lock

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
native_png="${SCREENDIR}/compare-${MAP}-${WAYPOINT}-${STAMP}-native.png"
neogeo_png="${SCREENDIR}/compare-${MAP}-${WAYPOINT}-${STAMP}-neogeo.png"
side_png="${SCREENDIR}/compare-${MAP}-${WAYPOINT}-${STAMP}-side-by-side.png"

kill_old_runners
sleep 0.3
switch_workspace

setsid env SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
    "$native_bin" -iwad "$native_iwad" -warp "$episode" "$level" -skill "$DOOM_SKILL" -nomusic -nosound -window \
    > "${LOGDIR}/native-doom-compare.log" 2>&1 < /dev/null &
native_pid=$!
disown "$native_pid" 2>/dev/null || true
sleep 1.2
drive_native_waypoint "$native_pid" "$native_title"
capture_window "$native_pid" "$native_title" "$native_png"

setsid env SDL_VIDEODRIVER=x11 make "$neo_make_target" "${neo_make_args[@]}" DOOM_MAP="$MAP" > "${LOGDIR}/gngeo-compare.log" 2>&1 < /dev/null &
make_pid=$!
disown "$make_pid" 2>/dev/null || true
neo_pid=""
for _ in $(seq 1 3000); do
    neo_pid="$(pgrep -n -x ngdevkit-gngeo 2>/dev/null || true)"
    if [ -n "$neo_pid" ]; then
        break
    fi
    sleep 0.1
done
if [ -z "$neo_pid" ]; then
    echo "ngdevkit-gngeo process not found" >&2
    tail -n 40 "${LOGDIR}/gngeo-compare.log" >&2 || true
    exit 1
fi
neo_wid="$(window_for_pid_or_name "$neo_pid" "" || true)"
if [ -n "$neo_wid" ]; then
    xdotool windowactivate "$neo_wid" >/dev/null 2>&1 || true
    if [ "$neo_press_start" = "1" ]; then
        sleep 1.5
        hold_key "$neo_wid" x 0.25
        hold_key "$neo_wid" s 0.25
        sleep 2.0
    fi
    if [ "$route_mode" != "focused" ]; then
        drive_neo_waypoint "$neo_pid"
    fi
fi
if [ -n "$neo_settle_secs" ]; then
    sleep "$neo_settle_secs"
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
if [ -n "$waypoint_note" ]; then
    echo "$waypoint_note"
fi
