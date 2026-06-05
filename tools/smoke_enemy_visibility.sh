#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-4}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"

mkdir -p "$OUT_DIR"

cleanup_gngeo() {
    pkill -9 -x ngdevkit-gngeo >/dev/null 2>&1 || true
}

trap cleanup_gngeo EXIT INT TERM

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_combat_interaction.sh
cleanup_gngeo

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_e1m1_encounter.sh
cleanup_gngeo

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_e1m1_scout.sh
cleanup_gngeo

SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
SMOKE_OUTPUT_DIR="$OUT_DIR" \
tools/smoke_hidden_attack.sh
cleanup_gngeo

SMOKE_BUILD_TARGET=monster-gallery-rom \
SMOKE_RUN_TARGET=monster-gallery-gngeo \
SMOKE_OUTPUT="$OUT_DIR/monster-gallery.png" \
SMOKE_DISPLAY="$DISPLAY_VALUE" \
SMOKE_WORKSPACE="$WORKSPACE" \
make smoke-screenshot
cleanup_gngeo

tools/check_enemy_visibility_screens.py --dir "$OUT_DIR"

cat <<EOF
enemy visibility smoke complete:
  $OUT_DIR/combat-initial.png
  $OUT_DIR/combat-fired.png
  $OUT_DIR/combat-death.png
  $OUT_DIR/e1m1-encounter-initial.png
  $OUT_DIR/e1m1-encounter-fired.png
  $OUT_DIR/e1m1-scout-initial.png
  $OUT_DIR/e1m1-scout-fired.png
  $OUT_DIR/hidden-attack-initial.png
  $OUT_DIR/hidden-attack-after.png
  $OUT_DIR/monster-gallery.png
EOF
