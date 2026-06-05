#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export STRESS_SHOWFPS="${STRESS_SHOWFPS:-1}"
export STRESS_FORWARD_SECS="${STRESS_FORWARD_SECS:-4}"
export STRESS_TURN_SECS="${STRESS_TURN_SECS:-3}"
export STRESS_STRAFE_SECS="${STRESS_STRAFE_SECS:-4}"
export SMOKE_OUTPUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest/movement-bench}"
export SMOKE_LOG="${SMOKE_LOG:-.tools/logs/movement-bench-gngeo.log}"
if [ -z "${SMOKE_MAKE_ARGS:-}" ]; then
    export SMOKE_MAKE_ARGS="DOOM_FRAME_STATS=1 BUILDDIR=build/frame-stats ROM=build/frame-stats-rom GFX_ROM_DIR=build/frame-stats-assets"
elif [[ " ${SMOKE_MAKE_ARGS:-} " != *" DOOM_FRAME_STATS="* ]]; then
    export SMOKE_MAKE_ARGS="${SMOKE_MAKE_ARGS} DOOM_FRAME_STATS=1"
fi

tools/stress_movement.sh
tools/check_movement_screens.py --dir "$SMOKE_OUTPUT_DIR" --expect-fps --expect-frame-stats

echo "movement bench log: $SMOKE_LOG"
