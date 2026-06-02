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


@dataclass(frozen=True)
class SideDef:
    texture_x: int
    texture_y: int
    top_texture: str
    bottom_texture: str
    mid_texture: str
    sector: int


@dataclass(frozen=True)
class Sector:
    floor_height: int
    ceiling_height: int
    floor_pic: str
    ceiling_pic: str
    light_level: int
    special: int
    tag: int


@dataclass(frozen=True)
class Seg:
    v1: int
    v2: int
    angle: int
    linedef: int
    side: int
    offset: int


@dataclass(frozen=True)
class Subsector:
    numsegs: int
    firstseg: int


@dataclass(frozen=True)
class Node:
    x: int
    y: int
    dx: int
    dy: int
    bbox: tuple[int, int, int, int, int, int, int, int]
    child0: int
    child1: int


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
        for required in wanted:
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


def decode_lump_name(raw: bytes) -> str:
    return raw.rstrip(b"\0").decode("ascii", "replace").upper()


def parse_sidedefs(data: bytes) -> list[SideDef]:
    if len(data) % 30:
        raise ValueError("SIDEDEFS lump has invalid size")
    result: list[SideDef] = []
    for i in range(0, len(data), 30):
        tex_x, tex_y, top, bottom, mid, sector = struct.unpack_from("<hh8s8s8sh", data, i)
        result.append(
            SideDef(tex_x, tex_y, decode_lump_name(top), decode_lump_name(bottom), decode_lump_name(mid), sector)
        )
    return result


def parse_sectors(data: bytes) -> list[Sector]:
    if len(data) % 26:
        raise ValueError("SECTORS lump has invalid size")
    result: list[Sector] = []
    for i in range(0, len(data), 26):
        floor, ceiling, floor_pic, ceiling_pic, light, special, tag = struct.unpack_from("<hh8s8shhh", data, i)
        result.append(
            Sector(floor, ceiling, decode_lump_name(floor_pic), decode_lump_name(ceiling_pic), light, special, tag)
        )
    return result


def parse_things(data: bytes) -> list[Thing]:
    if len(data) % 10:
        raise ValueError("THINGS lump has invalid size")
    return [Thing(*struct.unpack_from("<hhHHH", data, i)) for i in range(0, len(data), 10)]


def parse_segs(data: bytes) -> list[Seg]:
    if len(data) % 12:
        raise ValueError("SEGS lump has invalid size")
    return [Seg(*struct.unpack_from("<HHHHHH", data, i)) for i in range(0, len(data), 12)]


def parse_subsectors(data: bytes) -> list[Subsector]:
    if len(data) % 4:
        raise ValueError("SSECTORS lump has invalid size")
    return [Subsector(*struct.unpack_from("<HH", data, i)) for i in range(0, len(data), 4)]


def parse_nodes(data: bytes) -> list[Node]:
    if len(data) % 28:
        raise ValueError("NODES lump has invalid size")
    result: list[Node] = []
    for i in range(0, len(data), 28):
        x, y, dx, dy = struct.unpack_from("<hhhh", data, i)
        bbox = struct.unpack_from("<hhhhhhhh", data, i + 8)
        child0, child1 = struct.unpack_from("<HH", data, i + 24)
        result.append(Node(x, y, dx, dy, bbox, child0, child1))
    return result


def parse_blockmap_words(data: bytes) -> list[int]:
    if len(data) % 2:
        raise ValueError("BLOCKMAP lump has invalid size")
    return [struct.unpack_from("<h", data, i)[0] for i in range(0, len(data), 2)]


def is_solid_linedef(line: LineDef, sidedefs: list[SideDef], sectors: list[Sector]) -> bool:
    ml_blocking = 0x0001
    ml_twosided = 0x0004
    player_height = 56

    if line.side_back == 0xFFFF or (line.flags & ml_twosided) == 0:
        return True
    if (line.flags & ml_blocking) != 0:
        return True
    if line.side_front >= len(sidedefs) or line.side_back >= len(sidedefs):
        return True

    front_side = sidedefs[line.side_front]
    back_side = sidedefs[line.side_back]
    if front_side.sector >= len(sectors) or back_side.sector >= len(sectors):
        return True

    front = sectors[front_side.sector]
    back = sectors[back_side.sector]
    open_bottom = max(front.floor_height, back.floor_height)
    open_top = min(front.ceiling_height, back.ceiling_height)
    return (open_top - open_bottom) < player_height


def grid_coord(x: int, y: int, min_x: int, max_y: int, scale: float, margin: int) -> tuple[float, float]:
    gx = margin + (x - min_x) / scale
    gy = margin + (max_y - y) / scale
    return gx, gy


def grid_point(x: int, y: int, min_x: int, max_y: int, scale: float, margin: int) -> tuple[int, int]:
    gx, gy = grid_coord(x, y, min_x, max_y, scale, margin)
    return int(round(gx)), int(round(gy))


def raster_line(grid: list[list[int]], x0: int, y0: int, x1: int, y1: int) -> None:
    for x, y in line_cells(grid, x0, y0, x1, y1):
        grid[y][x] = 1


def raster_line_value(grid: list[list[int]], x0: int, y0: int, x1: int, y1: int, value: int) -> None:
    for x, y in line_cells(grid, x0, y0, x1, y1):
        grid[y][x] = value


def line_cells(grid: list[list[int]], x0: int, y0: int, x1: int, y1: int) -> list[tuple[int, int]]:
    width = len(grid[0])
    height = len(grid)
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    x, y = x0, y0
    cells: list[tuple[int, int]] = []
    while True:
        if 0 <= x < width and 0 <= y < height:
            cells.append((x, y))
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy
    return cells


def solid_line_texture(line: LineDef, sidedefs: list[SideDef]) -> str:
    side = sidedefs[line.side_front] if line.side_front != 0xFFFF else None
    if side is None:
        return ""
    for name in (side.mid_texture, side.top_texture, side.bottom_texture):
        if name and name != "-":
            return name
    return ""


def wall_texture_class(name: str) -> int:
    return 1 if name == "BROWNGRN" else 0


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


MONSTER_TYPES = {
    9,
    58,
    64,
    65,
    66,
    67,
    68,
    69,
    71,
    84,
    3001,
    3002,
    3003,
    3004,
    3005,
    3006,
}

PICKUP_TYPES = {
    5,     # blue keycard
    6,     # yellow keycard
    13,    # red keycard
    38,    # red skull
    39,    # yellow skull
    40,    # blue skull
    2001,  # shotgun
    2002,  # chaingun
    2007,  # clip
    2008,  # shells
    2010,  # rocket
    2011,  # stimpack
    2012,  # medikit
    2014,  # health bonus
    2015,  # armor bonus
    2018,  # green armor
    2019,  # blue armor
    2048,  # ammo box
    2035,  # explosive barrel
}

RUNTIME_THING_TYPES = MONSTER_TYPES | PICKUP_TYPES
EXIT_SPECIALS = {11, 51, 52, 124, 197, 198, 243, 244}
DOOR_SPECIALS = {1, 26, 27, 28, 31, 32, 33, 34, 46, 61, 63, 76, 86, 90, 103, 117, 118}

def line_of_sight_grid(grid: list[list[int]], ax: float, ay: float, bx: float, by: float) -> bool:
    steps = max(1, int(math.hypot(bx - ax, by - ay) * 8))
    for i in range(1, steps):
        t = i / steps
        x = ax + (bx - ax) * t
        y = ay + (by - ay) * t
        ix = int(math.floor(x))
        iy = int(math.floor(y))
        if iy < 0 or ix < 0 or iy >= len(grid) or ix >= len(grid[0]) or grid[iy][ix]:
            return False
    return True


def runtime_things(
    things: list[Thing],
    grid: list[list[int]],
    min_x: int,
    max_y: int,
    scale: float,
    margin: int,
    start_x: float,
    start_y: float,
    angle: int,
) -> list[tuple[int, int, int, int]]:
    angle_rad = math.radians(angle)
    dir_x = math.cos(angle_rad)
    dir_y = -math.sin(angle_rad)
    rows: list[tuple[float, int, int, int, int]] = []
    for thing in things:
        if thing.type not in RUNTIME_THING_TYPES:
            continue
        gx, gy = grid_coord(thing.x, thing.y, min_x, max_y, scale, margin)
        cell_x, cell_y = nearest_open(grid, int(math.floor(gx)), int(math.floor(gy)))
        if abs((cell_x + 0.5) - gx) > 2.5 or abs((cell_y + 0.5) - gy) > 2.5:
            continue
        gx = cell_x + 0.5
        gy = cell_y + 0.5
        dx = gx - start_x
        dy = gy - start_y
        forward = dx * dir_x + dy * dir_y
        lateral = abs(dx * (-dir_y) + dy * dir_x)
        dist = math.hypot(dx, dy)
        los_bonus = -8.0 if forward > 0.25 and line_of_sight_grid(grid, start_x, start_y, gx, gy) else 0.0
        score = dist + lateral * 0.35 + (0.0 if forward > 0 else 8.0) + los_bonus
        rows.append((score, int(round(gx * 256)), int(round(gy * 256)), thing.type, thing.flags))
    rows.sort(key=lambda row: row[0])
    return [(x, y, typ, flags) for _score, x, y, typ, flags in rows]


def runtime_exits(
    linedefs: list[LineDef],
    vertices: list[Vertex],
    grid: list[list[int]],
    min_x: int,
    max_y: int,
    scale: float,
    margin: int,
) -> list[tuple[int, int, int]]:
    exits: list[tuple[int, int, int]] = []
    seen: set[tuple[int, int]] = set()
    for line in linedefs:
        if line.special not in EXIT_SPECIALS:
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        gx, gy = grid_coord((a.x + b.x) // 2, (a.y + b.y) // 2, min_x, max_y, scale, margin)
        cell_x, cell_y = nearest_open(grid, int(math.floor(gx)), int(math.floor(gy)))
        key = (cell_x, cell_y)
        if key in seen:
            continue
        seen.add(key)
        exits.append((int(round((cell_x + 0.5) * 256)), int(round((cell_y + 0.5) * 256)), line.special))
    return exits


def runtime_doors(
    linedefs: list[LineDef],
    vertices: list[Vertex],
    grid: list[list[int]],
    min_x: int,
    max_y: int,
    scale: float,
    margin: int,
) -> list[tuple[int, int, int]]:
    doors: list[tuple[int, int, int]] = []
    seen: set[tuple[int, int]] = set()
    for line in linedefs:
        if line.special not in DOOR_SPECIALS:
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        x0, y0 = grid_point(a.x, a.y, min_x, max_y, scale, margin)
        x1, y1 = grid_point(b.x, b.y, min_x, max_y, scale, margin)
        for cell_x, cell_y in line_cells(grid, x0, y0, x1, y1):
            if not grid[cell_y][cell_x]:
                continue
            key = (cell_x, cell_y)
            if key in seen:
                continue
            seen.add(key)
            doors.append((cell_x, cell_y, line.special))
    return doors


def emit_header(
    out_path: str,
    grid: list[list[int]],
    texture_grid: list[list[int]],
    start_x: float,
    start_y: float,
    angle: int,
    source_name: str,
    map_name: str,
    stats: dict[str, int],
    things: list[tuple[int, int, int, int]],
    exits: list[tuple[int, int, int]],
    doors: list[tuple[int, int, int]],
) -> None:
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    angle_rad = math.radians(angle)
    dir_x = math.cos(angle_rad)
    dir_y = -math.sin(angle_rad)
    plane_x = -dir_y * 0.66
    plane_y = dir_x * 0.66
    with open(out_path, "w", encoding="ascii") as f:
        door_cell_id = {(x, y): i + 2 for i, (x, y, _special) in enumerate(doors)}
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
        f.write(f"#define DOOM_CONVERTED_SIDEDEFS {stats['sidedefs']}\n")
        f.write(f"#define DOOM_CONVERTED_SECTORS {stats['sectors']}\n")
        f.write(f"#define DOOM_CONVERTED_SEGS {stats['segs']}\n")
        f.write(f"#define DOOM_CONVERTED_SSECTORS {stats['subsectors']}\n")
        f.write(f"#define DOOM_CONVERTED_NODES {stats['nodes']}\n")
        f.write(f"#define DOOM_CONVERTED_REJECT_BYTES {stats['reject_bytes']}\n")
        f.write(f"#define DOOM_CONVERTED_BLOCKMAP_WORDS {stats['blockmap_words']}\n")
        f.write(f"#define DOOM_CONVERTED_THINGS {stats['things']}\n")
        f.write(f"#define DOOM_CONVERTED_EXITS {len(exits)}\n")
        f.write(f"#define DOOM_CONVERTED_DOORS {len(doors)}\n")
        f.write(f"#define NG_RUNTIME_THING_COUNT {len(things)}\n\n")
        f.write(f"#define NG_RUNTIME_EXIT_COUNT {len(exits)}\n\n")
        f.write(f"#define NG_RUNTIME_DOOR_COUNT {len(doors)}\n\n")
        f.write("typedef struct NgRuntimeThing { short x_q8; short y_q8; unsigned short type; unsigned short flags; } NgRuntimeThing;\n\n")
        f.write("typedef struct NgRuntimeExit { short x_q8; short y_q8; unsigned short special; } NgRuntimeExit;\n\n")
        f.write("typedef struct NgRuntimeDoor { unsigned char x; unsigned char y; unsigned short special; } NgRuntimeDoor;\n\n")
        if doors:
            f.write("extern unsigned char g_runtime_door_open[NG_RUNTIME_DOOR_COUNT];\n\n")
        f.write("static const unsigned char g_map[MAP_H][MAP_W] = {\n")
        for y, row in enumerate(grid):
            f.write("    {")
            f.write(",".join(str(door_cell_id.get((x, y), cell)) for x, cell in enumerate(row)))
            f.write("},\n")
        f.write("};\n\n")
        f.write("static const unsigned char g_map_tex[MAP_H][MAP_W] = {\n")
        for row in texture_grid:
            f.write("    {")
            f.write(",".join(str(cell) for cell in row))
            f.write("},\n")
        f.write("};\n\n")
        f.write("static const NgRuntimeThing g_runtime_things[NG_RUNTIME_THING_COUNT] = {\n")
        for x_q8, y_q8, typ, flags in things:
            f.write(f"    {{{x_q8},{y_q8},{typ},0x{flags & 0xffff:04x}}},\n")
        f.write("};\n\n")
        f.write("static const NgRuntimeExit g_runtime_exits[NG_RUNTIME_EXIT_COUNT] = {\n")
        for x_q8, y_q8, special in exits:
            f.write(f"    {{{x_q8},{y_q8},{special}}},\n")
        f.write("};\n\n")
        f.write("static const NgRuntimeDoor g_runtime_doors[NG_RUNTIME_DOOR_COUNT] = {\n")
        for x, y, special in doors:
            f.write(f"    {{{x},{y},{special}}},\n")
        f.write("};\n\n")
        f.write("static inline int map_at(int x, int y) {\n")
        f.write("    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;\n")
        f.write("    unsigned char cell = g_map[y][x];\n")
        f.write("    if (!cell) return 0;\n")
        if doors:
            f.write("    if (cell >= 2) return g_runtime_door_open[cell - 2] ? 0 : 1;\n")
        f.write("    return 1;\n")
        f.write("}\n\n")
        f.write("static inline unsigned char map_cell_value(int x, int y) {\n")
        f.write("    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;\n")
        f.write("    return g_map[y][x];\n")
        f.write("}\n\n")
        f.write("static inline unsigned char map_cell_texture(int x, int y) {\n")
        f.write("    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;\n")
        f.write("    return g_map_tex[y][x];\n")
        f.write("}\n\n#endif /* DOOM_MAP_GENERATED_H */\n")


def texture_ids(sidedefs: list[SideDef], sectors: list[Sector]) -> dict[str, int]:
    ids = {"-": 0, "": 0}

    def intern(name: str) -> None:
        if name not in ids:
            ids[name] = len(ids) - 1

    for side in sidedefs:
        intern(side.top_texture)
        intern(side.bottom_texture)
        intern(side.mid_texture)
    for sector in sectors:
        intern(sector.floor_pic)
        intern(sector.ceiling_pic)
    return ids


def side_index(value: int) -> int:
    return -1 if value == 0xFFFF else value


def write_array(f, typename: str, name: str, rows: list[str], count_macro: str) -> None:
    f.write(f"const {typename} {name}[{count_macro}] = {{\n")
    for row in rows:
        f.write(f"    {row},\n")
    f.write("};\n\n")


def write_scalar_array(f, typename: str, name: str, values: list[str], count_macro: str, per_row: int = 12) -> None:
    f.write(f"const {typename} {name}[{count_macro}] = {{\n")
    for i in range(0, len(values), per_row):
        f.write("    ")
        f.write(",".join(values[i : i + per_row]))
        f.write(",\n")
    f.write("};\n\n")


def emit_assets(
    header_path: str,
    source_path: str,
    vertices: list[Vertex],
    linedefs: list[LineDef],
    sidedefs: list[SideDef],
    sectors: list[Sector],
    segs: list[Seg],
    subsectors: list[Subsector],
    nodes: list[Node],
    things: list[Thing],
    reject: bytes,
    blockmap: list[int],
) -> None:
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    tex_ids = texture_ids(sidedefs, sectors)
    include_name = os.path.basename(header_path)

    with open(header_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_convert.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_ASSETS_GENERATED_H\n#define DOOM_ASSETS_GENERATED_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* 16-bit fields keep these structures compact and naturally big-endian in the 68000 ROM. */\n")
        f.write("typedef struct NgVertex { int16_t x; int16_t y; } NgVertex;\n")
        f.write("typedef struct NgLine {\n")
        f.write("    int16_t v1;\n")
        f.write("    int16_t v2;\n")
        f.write("    int16_t front_side;\n")
        f.write("    int16_t back_side;\n")
        f.write("    uint16_t flags;\n")
        f.write("    uint16_t special;\n")
        f.write("    uint16_t tag;\n")
        f.write("} NgLine;\n")
        f.write("typedef struct NgSide { int16_t texture_x; int16_t texture_y; uint16_t top_texture; uint16_t bottom_texture; uint16_t mid_texture; uint16_t sector; } NgSide;\n")
        f.write("typedef struct NgSector { int16_t floor_height; int16_t ceiling_height; uint16_t floor_flat; uint16_t ceiling_flat; uint16_t light_level; uint16_t special; uint16_t tag; } NgSector;\n")
        f.write("typedef struct NgSeg { int16_t v1; int16_t v2; int16_t angle; int16_t linedef; int16_t side; int16_t offset; } NgSeg;\n")
        f.write("typedef struct NgSubsector { uint16_t numsegs; uint16_t firstseg; } NgSubsector;\n")
        f.write("typedef struct NgNode { int16_t x; int16_t y; int16_t dx; int16_t dy; int16_t bbox[8]; uint16_t child[2]; } NgNode;\n")
        f.write("typedef struct NgThing { int16_t x; int16_t y; uint16_t angle; uint16_t type; uint16_t flags; } NgThing;\n\n")
        f.write(f"#define NG_VERTEX_COUNT {len(vertices)}\n")
        f.write(f"#define NG_LINE_COUNT {len(linedefs)}\n")
        f.write(f"#define NG_SIDE_COUNT {len(sidedefs)}\n")
        f.write(f"#define NG_SECTOR_COUNT {len(sectors)}\n")
        f.write(f"#define NG_SEG_COUNT {len(segs)}\n")
        f.write(f"#define NG_SUBSECTOR_COUNT {len(subsectors)}\n")
        f.write(f"#define NG_NODE_COUNT {len(nodes)}\n")
        f.write(f"#define NG_THING_COUNT {len(things)}\n")
        f.write(f"#define NG_REJECT_SIZE {len(reject)}\n")
        f.write(f"#define NG_BLOCKMAP_WORD_COUNT {len(blockmap)}\n")
        f.write(f"#define NG_TEXTURE_ID_COUNT {len(tex_ids) - 1}\n\n")
        f.write("extern const NgVertex g_ng_vertices[NG_VERTEX_COUNT];\n")
        f.write("extern const NgLine g_ng_lines[NG_LINE_COUNT];\n")
        f.write("extern const NgSide g_ng_sides[NG_SIDE_COUNT];\n")
        f.write("extern const NgSector g_ng_sectors[NG_SECTOR_COUNT];\n")
        f.write("extern const NgSeg g_ng_segs[NG_SEG_COUNT];\n")
        f.write("extern const NgSubsector g_ng_subsectors[NG_SUBSECTOR_COUNT];\n")
        f.write("extern const NgNode g_ng_nodes[NG_NODE_COUNT];\n")
        f.write("extern const NgThing g_ng_things[NG_THING_COUNT];\n")
        f.write("extern const uint8_t g_ng_reject[NG_REJECT_SIZE];\n")
        f.write("extern const int16_t g_ng_blockmap[NG_BLOCKMAP_WORD_COUNT];\n\n")
        f.write("#endif /* DOOM_ASSETS_GENERATED_H */\n")

    with open(source_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_convert.py; do not edit by hand. */\n")
        f.write(f"#include \"{include_name}\"\n\n")
        write_array(f, "NgVertex", "g_ng_vertices", [f"{{{v.x},{v.y}}}" for v in vertices], "NG_VERTEX_COUNT")
        write_array(
            f,
            "NgLine",
            "g_ng_lines",
            [
                f"{{{line.v1},{line.v2},{side_index(line.side_front)},{side_index(line.side_back)},0x{line.flags & 0xffff:04x},0x{line.special & 0xffff:04x},0x{line.tag & 0xffff:04x}}}"
                for line in linedefs
            ],
            "NG_LINE_COUNT",
        )
        write_array(
            f,
            "NgSide",
            "g_ng_sides",
            [
                f"{{{side.texture_x},{side.texture_y},{tex_ids[side.top_texture]},{tex_ids[side.bottom_texture]},{tex_ids[side.mid_texture]},{side.sector}}}"
                for side in sidedefs
            ],
            "NG_SIDE_COUNT",
        )
        write_array(
            f,
            "NgSector",
            "g_ng_sectors",
            [
                f"{{{sector.floor_height},{sector.ceiling_height},{tex_ids[sector.floor_pic]},{tex_ids[sector.ceiling_pic]},{sector.light_level},{sector.special},{sector.tag}}}"
                for sector in sectors
            ],
            "NG_SECTOR_COUNT",
        )
        write_array(f, "NgSeg", "g_ng_segs", [f"{{{seg.v1},{seg.v2},{seg.angle},{seg.linedef},{seg.side},{seg.offset}}}" for seg in segs], "NG_SEG_COUNT")
        write_array(f, "NgSubsector", "g_ng_subsectors", [f"{{{ss.numsegs},{ss.firstseg}}}" for ss in subsectors], "NG_SUBSECTOR_COUNT")
        write_array(
            f,
            "NgNode",
            "g_ng_nodes",
            [
                f"{{{node.x},{node.y},{node.dx},{node.dy},{{{','.join(str(v) for v in node.bbox)}}},{{{node.child0},{node.child1}}}}}"
                for node in nodes
            ],
            "NG_NODE_COUNT",
        )
        write_array(f, "NgThing", "g_ng_things", [f"{{{thing.x},{thing.y},{thing.angle},{thing.type},{thing.flags}}}" for thing in things], "NG_THING_COUNT")
        write_scalar_array(f, "uint8_t", "g_ng_reject", [f"0x{b:02x}" for b in reject], "NG_REJECT_SIZE", 16)
        write_scalar_array(f, "int16_t", "g_ng_blockmap", [str(word) for word in blockmap], "NG_BLOCKMAP_WORD_COUNT", 12)


def convert(args: argparse.Namespace) -> None:
    wad = Wad(read_wad(args.iwad, args.zip_member))
    lumps = wad.map_lumps(args.map)
    vertices = parse_vertices(lumps["VERTEXES"])
    linedefs = parse_linedefs(lumps["LINEDEFS"])
    sidedefs = parse_sidedefs(lumps["SIDEDEFS"])
    sectors = parse_sectors(lumps["SECTORS"])
    segs = parse_segs(lumps["SEGS"])
    subsectors = parse_subsectors(lumps["SSECTORS"])
    nodes = parse_nodes(lumps["NODES"])
    things = parse_things(lumps["THINGS"])
    reject = lumps["REJECT"]
    blockmap = parse_blockmap_words(lumps["BLOCKMAP"])
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
    texture_grid = [[0 for _ in range(args.width)] for _ in range(args.height)]
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
        if not is_solid_linedef(line, sidedefs, sectors):
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
        raster_line_value(texture_grid, x0, y0, x1, y1, wall_texture_class(solid_line_texture(line, sidedefs)))

    player = next((thing for thing in things if thing.type == 1), None)
    if player is None:
        raise ValueError("map has no player 1 start thing")
    sx, sy = grid_coord(player.x, player.y, min_x, max_y, scale, margin)
    sx, sy = carve_start_clearance(grid, sx, sy, player.angle)
    sx, sy = choose_start_pose(grid, sx, sy, player.angle)
    converted_things = runtime_things(things, grid, min_x, max_y, scale, margin, sx, sy, player.angle)
    converted_exits = runtime_exits(linedefs, vertices, grid, min_x, max_y, scale, margin)
    converted_doors = runtime_doors(linedefs, vertices, grid, min_x, max_y, scale, margin)

    emit_header(
        args.out,
        grid,
        texture_grid,
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
            "sidedefs": len(sidedefs),
            "sectors": len(sectors),
            "segs": len(segs),
            "subsectors": len(subsectors),
            "nodes": len(nodes),
            "reject_bytes": len(reject),
            "blockmap_words": len(blockmap),
            "things": len(things),
        },
        converted_things,
        converted_exits,
        converted_doors,
    )
    if args.assets_header and args.assets_source:
        emit_assets(
            args.assets_header,
            args.assets_source,
            vertices,
            linedefs,
            sidedefs,
            sectors,
            segs,
            subsectors,
            nodes,
            things,
            reject,
            blockmap,
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
    parser.add_argument("--assets-header", help="Generated Neo Geo map asset declarations")
    parser.add_argument("--assets-source", help="Generated Neo Geo map asset data")
    parser.add_argument("--width", type=int, default=38)
    parser.add_argument("--height", type=int, default=27)
    parser.add_argument(
        "--detail-cull",
        type=float,
        default=0.20,
        help="Cull solid linedefs shorter than this fraction of one output cell",
    )
    args = parser.parse_args()
    if bool(args.assets_header) != bool(args.assets_source):
        parser.error("--assets-header and --assets-source must be provided together")
    try:
        convert(args)
    except Exception as exc:
        print(f"doom_convert.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
