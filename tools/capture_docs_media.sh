#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DOOM_IWAD="${DOOM_IWAD:-/home/sabino/Downloads/Doom1.WAD}"
DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${DOCS_WAIT_SECS:-8}"
OUT_DIR="${DOCS_MEDIA_OUT_DIR:-.tools/screens/docs-refresh}"
STATIC_DIR="$OUT_DIR/static"
GIF_FRAME_DIR="$OUT_DIR/gif-frames"
DOC_SCREEN_DIR="docs/screenshots"
LOG_DIR=".tools/logs"
LOG="$LOG_DIR/docs-media-gngeo.log"
GIF_OUT="$DOC_SCREEN_DIR/doomgeo-motion.gif"
CAPTURE_P1CONTROL="${SMOKE_P1CONTROL:-A=K122,B=K120,C=K97,D=K115,START=K49,COIN=K51,UP=K82,DOWN=K81,LEFT=K80,RIGHT=K79}"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

window_for_gngeo() {
    local wid=""
    for _ in $(seq 1 120); do
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

tile_window() {
    local wid="$1"
    if [ -n "$WORKSPACE" ] && command -v i3-msg >/dev/null 2>&1; then
        i3-msg "[id=\"$wid\"] move container to workspace number $WORKSPACE, floating enable" >/dev/null 2>&1 || true
    elif [ -n "$WORKSPACE" ] && command -v swaymsg >/dev/null 2>&1; then
        swaymsg '[class="ngdevkit-gngeo"] move container to workspace number '"$WORKSPACE"', floating enable' >/dev/null 2>&1 || true
    elif [ -n "$WORKSPACE" ]; then
        DISPLAY="$DISPLAY_VALUE" xdotool set_desktop_for_window "$wid" "$WORKSPACE" >/dev/null 2>&1 || true
    fi
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

tap_key() {
    local wid="$1"
    local key="$2"
    local hold="${3:-0.18}"
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" "$key"
    sleep "$hold"
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" "$key"
}

capture_gif_frame() {
    local wid="$1"
    local idx="$2"
    local out
    out="$(printf '%s/frame-%03d.png' "$GIF_FRAME_DIR" "$idx")"
    capture_window "$wid" "$out"
}

cleanup_gngeo() {
    pkill -9 -x ngdevkit-gngeo >/dev/null 2>&1 || true
    pkill -9 -x gngeo >/dev/null 2>&1 || true
}

run_static_smokes() {
    DOOM_IWAD="$DOOM_IWAD" \
    SMOKE_DISPLAY="$DISPLAY_VALUE" \
    SMOKE_WORKSPACE="$WORKSPACE" \
    SMOKE_OUTPUT_DIR="$STATIC_DIR" \
    tools/smoke_enemy_visibility.sh
    cleanup_gngeo

    DOOM_IWAD="$DOOM_IWAD" \
    SMOKE_DISPLAY="$DISPLAY_VALUE" \
    SMOKE_WORKSPACE="$WORKSPACE" \
    SMOKE_OUTPUT_DIR="$STATIC_DIR" \
    tools/smoke_weapon_shortcuts.sh
    cleanup_gngeo

    DOOM_IWAD="$DOOM_IWAD" \
    SMOKE_DISPLAY="$DISPLAY_VALUE" \
    SMOKE_WORKSPACE="$WORKSPACE" \
    SMOKE_OUTPUT_DIR="$STATIC_DIR" \
    tools/smoke_death_drop.sh
    cleanup_gngeo

}

capture_default_rom_media() {
    local wid=""
    local frame=0

    DOOM_IWAD="$DOOM_IWAD" make cart
    cleanup_gngeo

    setsid env DISPLAY="$DISPLAY_VALUE" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 \
        make gngeo DOOM_IWAD="$DOOM_IWAD" GNGEO_P1CONTROL="$CAPTURE_P1CONTROL" >"$LOG" 2>&1 < /dev/null &

    sleep "$WAIT_SECS"
    wid="$(window_for_gngeo)"
    tile_window "$wid"
    sleep 0.5
    capture_window "$wid" "$STATIC_DIR/intro-menu.png"

    tap_key "$wid" x 0.25
    sleep 1.5
    wid="$(window_for_gngeo)"
    capture_window "$wid" "$STATIC_DIR/current-gameplay.png"

    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" z
    sleep 0.08
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" a
    sleep 0.25
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" a
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" z
    sleep 0.5
    capture_window "$wid" "$STATIC_DIR/current-minimap.png"

    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" z
    sleep 0.08
    DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" a
    sleep 0.25
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" a
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" z
    sleep 0.4

    capture_gif_frame "$wid" "$frame"; frame=$((frame + 1))
    for _ in 1 2 3 4 5 6; do
        DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Up
        sleep 0.18
        capture_gif_frame "$wid" "$frame"; frame=$((frame + 1))
    done
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Up
    sleep 0.2
    for _ in 1 2 3 4 5 6; do
        DISPLAY="$DISPLAY_VALUE" xdotool keydown --window "$wid" Right
        sleep 0.18
        capture_gif_frame "$wid" "$frame"; frame=$((frame + 1))
    done
    DISPLAY="$DISPLAY_VALUE" xdotool keyup --window "$wid" Right
    sleep 0.2
    tap_key "$wid" x 0.22
    for _ in 1 2 3 4 5; do
        sleep 0.16
        capture_gif_frame "$wid" "$frame"; frame=$((frame + 1))
    done

    cleanup_gngeo
}

copy_curated_media() {
    cp "$STATIC_DIR/intro-menu.png" "$DOC_SCREEN_DIR/doomgeo-intro-menu.png"
    cp "$STATIC_DIR/current-gameplay.png" "$DOC_SCREEN_DIR/doomgeo-current-gameplay.png"
    cp "$STATIC_DIR/current-minimap.png" "$DOC_SCREEN_DIR/doomgeo-current-minimap.png"
    cp "$STATIC_DIR/combat-initial.png" "$DOC_SCREEN_DIR/doomgeo-combat-test.png"
    cp "$STATIC_DIR/combat-fired.png" "$DOC_SCREEN_DIR/doomgeo-combat-fired.png"
    cp "$STATIC_DIR/combat-death.png" "$DOC_SCREEN_DIR/doomgeo-combat-kill.png"
    cp "$STATIC_DIR/e1m1-encounter-initial.png" "$DOC_SCREEN_DIR/doomgeo-e1m1-encounter.png"
    cp "$STATIC_DIR/e1m1-scout-initial.png" "$DOC_SCREEN_DIR/doomgeo-e1m1-scout.png"
    cp "$STATIC_DIR/monster-gallery.png" "$DOC_SCREEN_DIR/doomgeo-monster-gallery.png"
    cp "$STATIC_DIR/weapon-shortcut-before.png" "$DOC_SCREEN_DIR/doomgeo-arsenal-test.png"
    cp "$STATIC_DIR/weapon-shortcut-held-c-right.png" "$DOC_SCREEN_DIR/doomgeo-weapon-shortcut-held.png"
    cp "$STATIC_DIR/death-drop.png" "$DOC_SCREEN_DIR/doomgeo-death-test.png"
}

encode_motion_gif() {
    ffmpeg -y -hide_banner -loglevel error \
        -framerate 10 \
        -i "$GIF_FRAME_DIR/frame-%03d.png" \
        -vf "scale=480:-1:flags=neighbor,split[s0][s1];[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=bayer" \
        "$GIF_OUT"
}

remove_duplicate_docs_media() {
    rm -f \
        "$DOC_SCREEN_DIR/doom-neogeo-current.png" \
        "$DOC_SCREEN_DIR/doomgeo-bfg-fallback.png" \
        "$DOC_SCREEN_DIR/doomgeo-current-hud.png" \
        "$DOC_SCREEN_DIR/doomgeo-e1m1-encounter-fired.png" \
        "$DOC_SCREEN_DIR/doomgeo-e1m1-exit-complete.png" \
        "$DOC_SCREEN_DIR/doomgeo-e1m1-scout-fired.png" \
        "$DOC_SCREEN_DIR/doomgeo-hidden-attack.png" \
        "$DOC_SCREEN_DIR/doomgeo-key-door-missing-key.png" \
        "$DOC_SCREEN_DIR/doomgeo-key-door-opened.png" \
        "$DOC_SCREEN_DIR/doomgeo-key-door-test.png" \
        "$DOC_SCREEN_DIR/doomgeo-key-door-through.png" \
        "$DOC_SCREEN_DIR/doomgeo-key-test-start.png" \
        "$DOC_SCREEN_DIR/doomgeo-native-comparison.png" \
        "$DOC_SCREEN_DIR/item-ground-after-bias.png" \
        "$DOC_SCREEN_DIR/item-ground-after.png" \
        "$DOC_SCREEN_DIR/item-ground-before-after.png" \
        "$DOC_SCREEN_DIR/item-ground-before.png"
}

require_cmd convert
require_cmd ffmpeg
require_cmd make
require_cmd xdotool
require_cmd xwininfo
require_cmd xwd

mkdir -p "$STATIC_DIR" "$GIF_FRAME_DIR" "$DOC_SCREEN_DIR" "$LOG_DIR"
rm -f "$STATIC_DIR"/*.png "$STATIC_DIR"/*.xwd "$GIF_FRAME_DIR"/*.png "$GIF_FRAME_DIR"/*.xwd

capture_default_rom_media
run_static_smokes
copy_curated_media
encode_motion_gif
remove_duplicate_docs_media

echo "docs media refreshed:"
echo "  $DOC_SCREEN_DIR"
echo "  $GIF_OUT"
