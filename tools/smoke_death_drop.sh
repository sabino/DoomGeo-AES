#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-2}"
WAIT_SECS="${SMOKE_WAIT_SECS:-8}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"
OUT="${OUT_DIR}/death-drop.png"

mkdir -p "$OUT_DIR"

SMOKE_BUILD_TARGET=death-test-rom \
SMOKE_RUN_TARGET=death-test-gngeo \
SMOKE_OUTPUT="$OUT" \
SMOKE_WAIT_SECS="$WAIT_SECS" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
tools/smoke_capture.sh >/dev/null

echo "$OUT"
