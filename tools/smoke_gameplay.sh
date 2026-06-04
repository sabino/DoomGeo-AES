#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DISPLAY_VALUE="${SMOKE_DISPLAY:-:1}"
WORKSPACE="${SMOKE_WORKSPACE:-2}"
OUT_DIR="${SMOKE_OUTPUT_DIR:-.tools/screens/latest}"

mkdir -p "$OUT_DIR"

run_smoke() {
    local name="$1"
    shift
    echo "==> ${name}"
    SMOKE_DISPLAY="$DISPLAY_VALUE" \
    SMOKE_WORKSPACE="$WORKSPACE" \
    SMOKE_OUTPUT_DIR="$OUT_DIR" \
    "$@"
}

make route-check
run_smoke "enemy visibility" tools/smoke_enemy_visibility.sh
run_smoke "E1M1 exit" tools/smoke_e1m1_exit.sh
run_smoke "key door" tools/smoke_key_door.sh
run_smoke "weapon shortcuts" tools/smoke_weapon_shortcuts.sh
run_smoke "death/drop" tools/smoke_death_drop.sh
run_smoke "powerups" tools/smoke_powerup.sh

cat <<EOF
gameplay smoke complete:
  $OUT_DIR/combat-initial.png
  $OUT_DIR/e1m1-encounter-initial.png
  $OUT_DIR/e1m1-scout-initial.png
  $OUT_DIR/e1m1-exit-complete.png
  $OUT_DIR/monster-gallery.png
  $OUT_DIR/key-door-through.png
  $OUT_DIR/weapon-shortcut-held-c-right.png
  $OUT_DIR/death-drop.png
  $OUT_DIR/powerup-test.png
EOF
