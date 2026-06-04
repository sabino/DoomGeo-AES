#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-2}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"

mkdir -p "$OUT_DIR"

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_combat_interaction.sh

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_e1m1_encounter.sh

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_hidden_attack.sh

cat <<EOF
enemy visibility smoke complete:
  $OUT_DIR/combat-initial.png
  $OUT_DIR/combat-fired.png
  $OUT_DIR/combat-death.png
  $OUT_DIR/e1m1-encounter-initial.png
  $OUT_DIR/e1m1-encounter-fired.png
  $OUT_DIR/hidden-attack-initial.png
  $OUT_DIR/hidden-attack-after.png
EOF
