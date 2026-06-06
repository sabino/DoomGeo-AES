#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

USE_XVFB="${SMOKE_XVFB:-0}"
XVFB_PID=""

cleanup() {
    if [ -n "$XVFB_PID" ]; then
        kill "$XVFB_PID" >/dev/null 2>&1 || true
    fi
}

if [ "$USE_XVFB" = "1" ]; then
    if ! command -v Xvfb >/dev/null 2>&1; then
        echo "missing required command: Xvfb" >&2
        exit 1
    fi
    DISPLAY_VALUE="${SMOKE_XVFB_DISPLAY:-:99}"
    XVFB_SCREEN="${SMOKE_XVFB_SCREEN:-1024x768x24}"
    mkdir -p .tools/logs
    Xvfb "$DISPLAY_VALUE" -screen 0 "$XVFB_SCREEN" >.tools/logs/chunk-key-door-xvfb.log 2>&1 &
    XVFB_PID="$!"
    trap cleanup EXIT INT TERM
    sleep 1

    SMOKE_XVFB=0 \
    SMOKE_DISPLAY="$DISPLAY_VALUE" \
    SMOKE_WORKSPACE="" \
    SMOKE_TILE_WINDOWS=0 \
    SMOKE_OUTPUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest/chunk-key-door}" \
    KEY_DOOR_BUILD_TARGET=chunk-key-door-test-rom \
    KEY_DOOR_RUN_TARGET=gngeo \
    KEY_DOOR_DIRECT_ROM=build/chunk-key-door-test-rom \
    tools/smoke_key_door.sh
else
    SMOKE_OUTPUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest/chunk-key-door}" \
    KEY_DOOR_BUILD_TARGET=chunk-key-door-test-rom \
    KEY_DOOR_RUN_TARGET=gngeo \
    KEY_DOOR_DIRECT_ROM=build/chunk-key-door-test-rom \
    tools/smoke_key_door.sh
fi
