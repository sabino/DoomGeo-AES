#!/usr/bin/env python3
"""Convert a Doom-format WAD map into a small Neo Geo raycaster map.

This is intentionally a first-stage converter: it preserves Doom WAD input
compatibility at build time, but emits a compact grid that the current
Neo Geo sprite-column renderer can consume directly.
"""

from __future__ import annotations

import argparse
import math
import os
import struct
import sys
from dataclasses import dataclass
from zipfile import ZipFile


DEFAULT_WAD_IN_ZIP = "freedoom-0.13.0/freedoom1.wad"


@dataclass(frozen=True)
class Lump:
    name: str
    offset: int
    size: int


@dataclass(frozen=True)
class Vertex:
    x: int
    y: int


@dataclass(frozen=True)
class LineDef:
    v1: int
    v2: int
    flags: int
    special: int
    tag: int
    side_front: int
    side_back: int

    @property
    def solid(self) -> bool:
        two_sided = (self.flags & 0x0004) != 0
        return not two_sided or self.side_back == 0xFFFF


@dataclass(frozen=True)
class Thing:
    x: int
    y: int
    angle: int
    type: int
    flags: int


class Wad:
    def __init__(self, data: bytes) -> None:
        ident, num_lumps, dir_offset = struct.unpack_from("<4sii", data, 0)
        if ident not in (b"IWAD", b"PWAD"):
            raise ValueError(f"not a WAD file: {ident!r}")
        self.data = data
        self.lumps: list[Lump] = []
        self.by_name: dict[str, list[int]] = {}
        for i in range(num_lumps):
            off, size, raw_name = struct.unpack_from("<ii8s", data, dir_offset + i * 16)
            name = raw_name.rstrip(b"\0").decode("ascii", "replace").upper()
            self.lumps.append(Lump(name, off, size))
            self.by_name.setdefault(name, []).append(i)

    def lump_data(self, index: int) -> bytes:
        lump = self.lumps[index]
        return self.data[lump.offset : lump.offset + lump.size]

    def map_lumps(self, marker: str) -> dict[str, bytes]:
        marker = marker.upper()
        try:
            start = next(i for i, lump in enumerate(self.lumps) if lump.name == marker)
        except StopIteration as exc:
            raise ValueError(f"map marker {marker!r} not found") from exc

        wanted = {
            "THINGS",
            "LINEDEFS",
            "SIDEDEFS",
            "VERTEXES",
            "SEGS",
            "SSECTORS",
            "NODES",
            "SECTORS",
            "REJECT",
            "BLOCKMAP",
        }
        result: dict[str, bytes] = {}
        for i in range(start + 1, min(start + 16, len(self.lumps))):
            name = self.lumps[i].name
            if name in wanted:
                result[name] = self.lump_data(i)
            if len(result) >= len(wanted):
                break
        for required in ("THINGS", "LINEDEFS", "VERTEXES"):
            if required not in result:
                raise ValueError(f"map {marker} missing required lump {required}")
        return result


def read_wad(path: str, zip_member: str | None) -> bytes:
    if path.lower().endswith(".zip"):
        with ZipFile(path) as zf:
            member = zip_member or DEFAULT_WAD_IN_ZIP
            if member not in zf.namelist():
                wads = [name for name in zf.namelist() if name.lower().endswith(".wad")]
                if not wads:
                    raise ValueError(f"no .wad found in {path}")
                member = wads[0]
            return zf.read(member)
    with open(path, "rb") as f:
        return f.read()


def parse_vertices(data: bytes) -> list[Vertex]:
    if len(data) % 4:
        raise ValueError("VERTEXES lump has invalid size")
    return [Vertex(*struct.unpack_from("<hh", data, i)) for i in range(0, len(data), 4)]


def parse_linedefs(data: bytes) -> list[LineDef]:
    if len(data) % 14:
        raise ValueError("LINEDEFS lump has invalid size")
    return [
        LineDef(*struct.unpack_from("<HHHHHHH", data, i))
        for i in range(0, len(data), 14)
    ]


def parse_things(data: bytes) -> list[Thing]:
    if len(data) % 10:
        raise ValueError("THINGS lump has invalid size")
    return [Thing(*struct.unpack_from("<hhHHH", data, i)) for i in range(0, len(data), 10)]


def grid_coord(x: int, y: int, min_x: int, max_y: int, scale: float, margin: int) -> tuple[float, float]:
    gx = margin + (x - min_x) / scale
    gy = margin + (max_y - y) / scale
    return gx, gy


def grid_point(x: int, y: int, min_x: int, max_y: int, scale: float, margin: int) -> tuple[int, int]:
    gx, gy = grid_coord(x, y, min_x, max_y, scale, margin)
    return int(round(gx)), int(round(gy))


def raster_line(grid: list[list[int]], x0: int, y0: int, x1: int, y1: int) -> None:
    width = len(grid[0])
    height = len(grid)
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    x, y = x0, y0
    while True:
        if 0 <= x < width and 0 <= y < height:
            grid[y][x] = 1
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy


def nearest_open(grid: list[list[int]], sx: int, sy: int) -> tuple[int, int]:
    width = len(grid[0])
    height = len(grid)
    if 0 <= sx < width and 0 <= sy < height and grid[sy][sx] == 0:
        return sx, sy
    for radius in range(1, max(width, height)):
        for y in range(max(1, sy - radius), min(height - 1, sy + radius + 1)):
            for x in range(max(1, sx - radius), min(width - 1, sx + radius + 1)):
                if grid[y][x] == 0:
                    return x, y
    return 1, 1


def carve_start_clearance(grid: list[list[int]], sx: float, sy: float, angle: int) -> tuple[float, float]:
    width = len(grid[0])
    height = len(grid)
    sx = max(1.001, min(width - 2.001, sx))
    sy = max(1.001, min(height - 2.001, sy))

    # Keep the Doom thing's fractional location instead of snapping it to a
    # tile center. At this resolution a center snap can shift the start more
    # than half a Doom room, so clear only the local cells the start occupies.
    base_x = int(math.floor(sx))
    base_y = int(math.floor(sy))
    for y in range(max(1, base_y - 1), min(height - 1, base_y + 2)):
        for x in range(max(1, base_x - 1), min(width - 1, base_x + 2)):
            dx = (x + 0.5) - sx
            dy = (y + 0.5) - sy
            if dx * dx + dy * dy <= 1.35 * 1.35:
                grid[y][x] = 0

    angle_rad = math.radians(angle)
    ahead_x = int(math.floor(sx + math.cos(angle_rad) * 0.85))
    ahead_y = int(math.floor(sy - math.sin(angle_rad) * 0.85))
    if 1 <= ahead_x < width - 1 and 1 <= ahead_y < height - 1:
        grid[ahead_y][ahead_x] = 0

    grid[base_y][base_x] = 0
    return sx, sy


def forward_clearance(grid: list[list[int]], sx: float, sy: float, angle: int) -> float:
    width = len(grid[0])
    height = len(grid)
    angle_rad = math.radians(angle)
    dx = math.cos(angle_rad)
    dy = -math.sin(angle_rad)
    distance = 0.0
    while distance < 4.0:
        x = sx + dx * distance
        y = sy + dy * distance
        ix = int(math.floor(x))
        iy = int(math.floor(y))
        if ix < 0 or iy < 0 or ix >= width or iy >= height or grid[iy][ix]:
            return distance
        distance += 0.125
    return distance


def choose_start_pose(grid: list[list[int]], raw_x: float, raw_y: float, angle: int) -> tuple[float, float]:
    width = len(grid[0])
    height = len(grid)
    best: tuple[float, float] | None = None
    best_score = float("inf")
    base_x = int(math.floor(raw_x))
    base_y = int(math.floor(raw_y))

    for radius in range(0, 3):
        for y in range(max(1, base_y - radius), min(height - 1, base_y + radius + 1)):
            for x in range(max(1, base_x - radius), min(width - 1, base_x + radius + 1)):
                if grid[y][x]:
                    continue
                cx = x + 0.5
                cy = y + 0.5
                clearance = forward_clearance(grid, cx, cy, angle)
                if clearance < 1.25:
                    continue
                dist = (cx - raw_x) * (cx - raw_x) + (cy - raw_y) * (cy - raw_y)
                score = dist - clearance * 0.05
                if score < best_score:
                    best_score = score
                    best = (cx, cy)
        if best is not None:
            return best

    cell_x, cell_y = nearest_open(grid, base_x, base_y)
    return cell_x + 0.5, cell_y + 0.5


def emit_header(
    out_path: str,
    grid: list[list[int]],
    start_x: float,
    start_y: float,
    angle: int,
    source_name: str,
    map_name: str,
    stats: dict[str, int],
) -> None:
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    angle_rad = math.radians(angle)
    dir_x = math.cos(angle_rad)
    dir_y = -math.sin(angle_rad)
    plane_x = -dir_y * 0.66
    plane_y = dir_x * 0.66
    with open(out_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_convert.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_MAP_GENERATED_H\n#define DOOM_MAP_GENERATED_H\n\n")
        f.write(f"#define DOOM_MAP_SOURCE \"{source_name}\"\n")
        f.write(f"#define DOOM_MAP_NAME \"{map_name}\"\n")
        f.write(f"#define MAP_W {len(grid[0])}\n#define MAP_H {len(grid)}\n")
        f.write(f"#define DOOM_START_X {start_x:.3f}\n")
        f.write(f"#define DOOM_START_Y {start_y:.3f}\n")
        f.write(f"#define DOOM_DIR_X {dir_x:.6f}\n")
        f.write(f"#define DOOM_DIR_Y {dir_y:.6f}\n")
        f.write(f"#define DOOM_PLANE_X {plane_x:.6f}\n")
        f.write(f"#define DOOM_PLANE_Y {plane_y:.6f}\n")
        f.write(f"#define DOOM_CONVERTED_VERTICES {stats['vertices']}\n")
        f.write(f"#define DOOM_CONVERTED_LINEDEFS {stats['linedefs']}\n")
        f.write(f"#define DOOM_CONVERTED_SOLID_LINEDEFS {stats['solid_linedefs']}\n")
        f.write(f"#define DOOM_CONVERTED_CULLED_LINEDEFS {stats['culled_linedefs']}\n")
        f.write(f"#define DOOM_CONVERTED_THINGS {stats['things']}\n\n")
        f.write("static const unsigned char g_map[MAP_H][MAP_W] = {\n")
        for row in grid:
            f.write("    {")
            f.write(",".join(str(cell) for cell in row))
            f.write("},\n")
        f.write("};\n\n")
        f.write("static inline int map_at(int x, int y) {\n")
        f.write("    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;\n")
        f.write("    return g_map[y][x];\n")
        f.write("}\n\n#endif /* DOOM_MAP_GENERATED_H */\n")


def convert(args: argparse.Namespace) -> None:
    wad = Wad(read_wad(args.iwad, args.zip_member))
    lumps = wad.map_lumps(args.map)
    vertices = parse_vertices(lumps["VERTEXES"])
    linedefs = parse_linedefs(lumps["LINEDEFS"])
    things = parse_things(lumps["THINGS"])
    if not vertices:
        raise ValueError("map has no vertices")

    min_x = min(v.x for v in vertices)
    max_x = max(v.x for v in vertices)
    min_y = min(v.y for v in vertices)
    max_y = max(v.y for v in vertices)
    margin = 1
    usable_w = args.width - margin * 2 - 1
    usable_h = args.height - margin * 2 - 1
    scale = max((max_x - min_x) / max(1, usable_w), (max_y - min_y) / max(1, usable_h), 1.0)

    grid = [[0 for _ in range(args.width)] for _ in range(args.height)]
    for x in range(args.width):
        grid[0][x] = 1
        grid[-1][x] = 1
    for y in range(args.height):
        grid[y][0] = 1
        grid[y][-1] = 1

    solid_count = 0
    culled_count = 0
    min_solid_len = scale * args.detail_cull
    for line in linedefs:
        if not line.solid:
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        if math.hypot(b.x - a.x, b.y - a.y) < min_solid_len:
            culled_count += 1
            continue
        solid_count += 1
        x0, y0 = grid_point(a.x, a.y, min_x, max_y, scale, margin)
        x1, y1 = grid_point(b.x, b.y, min_x, max_y, scale, margin)
        raster_line(grid, x0, y0, x1, y1)

    player = next((thing for thing in things if thing.type == 1), None)
    if player is None:
        raise ValueError("map has no player 1 start thing")
    sx, sy = grid_coord(player.x, player.y, min_x, max_y, scale, margin)
    sx, sy = carve_start_clearance(grid, sx, sy, player.angle)
    sx, sy = choose_start_pose(grid, sx, sy, player.angle)

    emit_header(
        args.out,
        grid,
        sx,
        sy,
        player.angle,
        os.path.basename(args.iwad),
        args.map.upper(),
        {
            "vertices": len(vertices),
            "linedefs": len(linedefs),
            "solid_linedefs": solid_count,
            "culled_linedefs": culled_count,
            "things": len(things),
        },
    )
    print(
        f"{args.map.upper()}: {len(vertices)} vertices, {solid_count}/{len(linedefs)} solid lines "
        f"({culled_count} detail culled), "
        f"{len(things)} things -> {args.width}x{args.height} grid at {args.out}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", required=True, help="Path to a WAD or Freedoom release zip")
    parser.add_argument("--zip-member", help="WAD member inside a zip archive")
    parser.add_argument("--map", default="E1M1", help="Doom map marker to convert")
    parser.add_argument("--out", required=True, help="Generated C header path")
    parser.add_argument("--width", type=int, default=38)
    parser.add_argument("--height", type=int, default=27)
    parser.add_argument(
        "--detail-cull",
        type=float,
        default=0.20,
        help="Cull solid linedefs shorter than this fraction of one output cell",
    )
    args = parser.parse_args()
    try:
        convert(args)
    except Exception as exc:
        print(f"doom_convert.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
