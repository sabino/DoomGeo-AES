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

tools/stress_movement.sh
tools/check_movement_screens.py --dir "$SMOKE_OUTPUT_DIR" --expect-fps

echo "movement bench log: $SMOKE_LOG"
