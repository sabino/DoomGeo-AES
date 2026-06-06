#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
POWERUP_OUT="${OUT_DIR}/chunk-powerup-test.png"
DEATH_OUT="${OUT_DIR}/chunk-death-drop.png"
MAKE_ARGS_VALUE="${SMOKE_MAKE_ARGS:-}"

mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=chunk-powerup-test-rom \
SMOKE_RUN_TARGET=gngeo \
SMOKE_DIRECT_ROM=build/chunk-powerup-test-rom \
SMOKE_OUTPUT="$POWERUP_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_MAKE_ARGS="$MAKE_ARGS_VALUE" \
tools/smoke_capture.sh >/dev/null

tools/check_powerup_screens.py --dir "$OUT_DIR" --file "$(basename "$POWERUP_OUT")"

SMOKE_BUILD_TARGET=chunk-death-test-rom \
SMOKE_RUN_TARGET=gngeo \
SMOKE_DIRECT_ROM=build/chunk-death-test-rom \
SMOKE_OUTPUT="$DEATH_OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_MAKE_ARGS="$MAKE_ARGS_VALUE" \
tools/smoke_capture.sh >/dev/null

tools/check_death_drop_screens.py --dir "$OUT_DIR" --file "$(basename "$DEATH_OUT")"

echo "$POWERUP_OUT"
echo "$DEATH_OUT"
