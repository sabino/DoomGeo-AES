#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export STRESS_SHOWFPS="${STRESS_SHOWFPS:-0}"
export SMOKE_BUILD_TARGET="${SMOKE_BUILD_TARGET:-chunk-movement-test-rom}"
export SMOKE_RUN_TARGET="${SMOKE_RUN_TARGET:-chunk-movement-test-gngeo}"
export SMOKE_OUTPUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest/chunk-debug-movement}"
export SMOKE_LOG="${SMOKE_LOG:-.tools/logs/chunk-debug-movement-gngeo.log}"
export SMOKE_MAKE_ARGS="${SMOKE_MAKE_ARGS:-ROM=build/chunk-movement-test-rom}"
export SMOKE_DIRECT_ROM="${SMOKE_DIRECT_ROM:-build/chunk-movement-test-rom}"
MOVED_WAIT="${CHUNK_DEBUG_MOVED_WAIT:-8.0}"
MOVED_OUT="${SMOKE_OUTPUT_DIR}/movement-stress-forward.png"
HOST_BUILDDIR="${CHUNK_DEBUG_HOST_BUILDDIR:-build/chunk-debug-movement-host-check}"

mkdir -p "$SMOKE_OUTPUT_DIR"

make chunk-movement-check \
    DOOM_IWAD="${DOOM_IWAD:-/home/sabino/Downloads/Doom1.WAD}" \
    DOOM_MAP=E1M1 \
    DOOM_SIMPLE_MAP=1 \
    DOOM_CHUNKED_SIMPLE_MAP=1 \
    DOOM_CHUNK_CELL_UNITS=256 \
    DOOM_RIPDOOM_RENDER=1 \
    BUILDDIR="$HOST_BUILDDIR"

SMOKE_OUTPUT="$MOVED_OUT" \
SMOKE_WAIT_SECS="$MOVED_WAIT" \
tools/smoke_capture.sh >/dev/null

status=0
tools/check_chunk_debug_screens.py --dir "$SMOKE_OUTPUT_DIR" --single "$(basename "$MOVED_OUT")" || status=1
if [ ! -f "$SMOKE_LOG" ]; then
    echo "chunk debug movement log missing: $SMOKE_LOG" >&2
    exit 1
fi
if grep -q 'Invalid write' "$SMOKE_LOG"; then
    echo "chunk debug movement saw invalid emulator writes in $SMOKE_LOG" >&2
    grep 'Invalid write' "$SMOKE_LOG" | head -20 >&2
    exit 1
fi

echo "chunk debug movement screenshots OK:"
echo "$MOVED_OUT"
echo "chunk debug movement log: $SMOKE_LOG"
exit "$status"
