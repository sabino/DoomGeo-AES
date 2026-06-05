#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
OUT="${OUT_DIR}/powerup-test.png"

mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=powerup-test-rom \
SMOKE_RUN_TARGET=powerup-test-gngeo \
SMOKE_OUTPUT="$OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

tools/check_powerup_screens.py --dir "$OUT_DIR"

echo "$OUT"
