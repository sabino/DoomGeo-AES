#!/usr/bin/env python3
"""Probe the authored simple-map live enemy visibility path.

The texture-column experiment uses generated Doom sprites inside GnGeo's
puzzledp C-ROM window. A full sprite bank can push live monster tiles past the
visible C-ROM range while the earlier corpse frames still fit, producing an
invisible live monster and a briefly visible death frame.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SIMPLE_MAP = [
    "1111111111111111",
    "1111111111111111",
    "1111111101111111",
    "1111111101111111",
    "1111111101111111",
    "1111111121111111",
    "1111111101111111",
    "1000000000000001",
    "1011110101111011",
    "1000010000010001",
    "1011011101010101",
    "1001000101000101",
    "1101110101110101",
    "1000000000000001",
    "1000000000000001",
    "1111111111111111",
]

SCRW = 320
GAME_H = 224
NUM_COLS = 80
COLW = SCRW // NUM_COLS
FOV = 0.66
BYTES_PER_CROM_TILE = 64


def wall_at(x: int, y: int) -> bool:
    if x < 0 or y < 0 or x >= 16 or y >= 16:
        return True
    return SIMPLE_MAP[y][x] != "0"


def cast_dist(px: float, py: float, ray_x: float, ray_y: float) -> float:
    map_x = int(px)
    map_y = int(py)
    delta_x = abs(1.0 / ray_x) if ray_x else 1.0e9
    delta_y = abs(1.0 / ray_y) if ray_y else 1.0e9
    if ray_x < 0:
        step_x = -1
        side_x = (px - map_x) * delta_x
    else:
        step_x = 1
        side_x = (map_x + 1.0 - px) * delta_x
    if ray_y < 0:
        step_y = -1
        side_y = (py - map_y) * delta_y
    else:
        step_y = 1
        side_y = (map_y + 1.0 - py) * delta_y

    for _ in range(64):
        if side_x < side_y:
            map_x += step_x
            dist = (map_x - px + (1 - step_x) / 2.0) / (ray_x or 1.0e-9)
            side_x += delta_x
        else:
            map_y += step_y
            dist = (map_y - py + (1 - step_y) / 2.0) / (ray_y or 1.0e-9)
            side_y += delta_y
        if wall_at(map_x, map_y):
            return max(dist, 0.001)
    return 999.0


def project_point(px: float, py: float, tx: float, ty: float) -> tuple[int, int, float, bool] | None:
    view_x = tx - px
    view_y = ty - py
    length = math.hypot(view_x, view_y)
    if length <= 0.001:
        return None
    dir_x = view_x / length
    dir_y = view_y / length
    plane_x = -dir_y * FOV
    plane_y = dir_x * FOV

    distbuf = []
    for col in range(NUM_COLS):
        camera_x = 2.0 * ((col + 0.5) / NUM_COLS) - 1.0
        ray_x = dir_x + plane_x * camera_x
        ray_y = dir_y + plane_y * camera_x
        distbuf.append(cast_dist(px, py, ray_x, ray_y))

    sprite_x = tx - px
    sprite_y = ty - py
    det = plane_x * dir_y - dir_x * plane_y
    if abs(det) < 0.001:
        return None
    transform_x = (dir_y * sprite_x - dir_x * sprite_y) / det
    transform_y = (-plane_y * sprite_x + plane_x * sprite_y) / det
    if transform_y < 0.125:
        return None

    screen_x = SCRW // 2 + int((SCRW / 2.0) * (transform_x / transform_y))
    height = min(GAME_H, int(GAME_H / transform_y))
    if screen_x < 0 or screen_x >= SCRW or height < 1:
        return None

    center_col = screen_x // COLW
    center_visible = False
    for delta in range(-2, 3):
        col = center_col + delta
        if 0 <= col < NUM_COLS and transform_y <= distbuf[col] + 0.125:
            center_visible = True
            break
    if not center_visible:
        return None

    width = max(52, height)
    first_col = max(0, screen_x - width // 2) // COLW
    last_col = min(SCRW - 1, screen_x + width // 2) // COLW
    strip_visible = any(transform_y <= distbuf[col] + 0.125 for col in range(first_col, last_col + 1))
    return screen_x, height, transform_y, strip_visible


def parse_generated_sprites(header: Path) -> tuple[list[tuple[int, int, int, int]], list[tuple[int, int, int, int, int, int, int]]]:
    text = header.read_text()
    defs_match = re.search(
        r"static const DoomEnemySpriteDef g_enemy_sprite_defs\[ENEMY_SPRITE_COUNT\] = \{\n(?P<body>.*?)\n\};",
        text,
        re.S,
    )
    scales_match = re.search(
        r"static const DoomSpriteScale g_enemy_scales\[ENEMY_SCALE_COUNT\] = \{\n(?P<body>.*?)\n\};",
        text,
        re.S,
    )
    if not defs_match or not scales_match:
        raise ValueError(f"{header} does not contain generated enemy sprite tables")

    defs: list[tuple[int, int, int, int]] = []
    for line in defs_match.group("body").splitlines():
        values = re.findall(r"-?\d+", line)
        if len(values) >= 4:
            defs.append(tuple(int(v) for v in values[:4]))

    scales: list[tuple[int, int, int, int, int, int, int]] = []
    for line in scales_match.group("body").splitlines():
        values = re.findall(r"-?\d+", line)
        if len(values) >= 7:
            scales.append(tuple(int(v) for v in values[:7]))

    return defs, scales


def max_scale_tile(scale: tuple[int, int, int, int, int, int, int]) -> int:
    tile_base, strips, rows, *_rest = scale
    return tile_base + strips * rows - 1


def assert_sprite_fits(
    label: str,
    defs: list[tuple[int, int, int, int]],
    scales: list[tuple[int, int, int, int, int, int, int]],
    thing_type: int,
    allowed_angles: set[int],
    crom_file_bytes: int,
) -> None:
    matches = [
        (thing, angle, first_scale, scale_count)
        for thing, angle, first_scale, scale_count in defs
        if thing == thing_type and angle in allowed_angles
    ]
    if not matches:
        raise ValueError(f"{label} missing from generated sprite defs")

    max_visible_tile = crom_file_bytes // BYTES_PER_CROM_TILE - 1
    worst_tile = -1
    for _thing, _angle, first_scale, scale_count in matches:
        if first_scale < 0 or first_scale + scale_count > len(scales):
            raise ValueError(f"{label} has an invalid scale range {first_scale}..{first_scale + scale_count - 1}")
        for scale in scales[first_scale : first_scale + scale_count]:
            worst_tile = max(worst_tile, max_scale_tile(scale))

    if worst_tile > max_visible_tile:
        raise ValueError(
            f"{label} tile {worst_tile} is outside the visible C-ROM tile window "
            f"{max_visible_tile} for {crom_file_bytes} bytes per C ROM"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gfx-header", type=Path, required=True)
    parser.add_argument("--crom-file-bytes", type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()

    # Seed 1 in seed_simple_map_things(): imp guarding the upper-right side lane.
    monster_x = 12.5
    monster_y = 7.5
    test_poses = [(8.5, 7.5), (9.5, 7.5), (10.5, 7.5), (11.5, 7.5), (13.5, 8.5)]
    projected = []
    for px, py in test_poses:
        if wall_at(int(px), int(py)):
            continue
        result = project_point(px, py, monster_x, monster_y)
        if result:
            projected.append((px, py, *result))

    if not projected:
        print("simple enemy visibility probe failed: upper-right monster never projects from side-lane poses", file=sys.stderr)
        return 1

    try:
        defs, scales = parse_generated_sprites(args.gfx_header)
        assert_sprite_fits("live former human walk frames", defs, scales, 3004, {1, 2, 3, 4, 5, 6, 7, 8}, args.crom_file_bytes)
        assert_sprite_fits("former human corpse frame", defs, scales, 9001, {0}, args.crom_file_bytes)
        assert_sprite_fits("former human death frames", defs, scales, 9010, {0}, args.crom_file_bytes)
        assert_sprite_fits("live imp walk frames", defs, scales, 3001, {1, 2, 3, 4, 5, 6, 7, 8}, args.crom_file_bytes)
        assert_sprite_fits("imp corpse frame", defs, scales, 9003, {0}, args.crom_file_bytes)
        assert_sprite_fits("imp death frames", defs, scales, 9012, {0}, args.crom_file_bytes)
    except ValueError as exc:
        print(f"simple enemy visibility probe failed: {exc}", file=sys.stderr)
        return 1

    print(
        "simple enemy visibility probe OK: "
        + ", ".join(
            f"pose=({px:.1f},{py:.1f}) sx={sx} h={h} dist={dist:.2f} strip={int(strip)}"
            for px, py, sx, h, dist, strip in projected[:3]
        )
        + f"; live imp/corpse tiles fit within {args.crom_file_bytes // BYTES_PER_CROM_TILE} C-ROM tiles"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
