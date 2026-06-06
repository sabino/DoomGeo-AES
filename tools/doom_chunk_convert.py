#!/usr/bin/env python3
"""Convert a Doom WAD map into fixed-scale 16x16 Neo Geo map chunks."""

from __future__ import annotations

import argparse
import heapq
import math
import os
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(__file__))
import doom_convert as dc  # noqa: E402


@dataclass(frozen=True)
class ChunkThing:
    x_q8: int
    y_q8: int
    type: int
    flags: int
    thing_class: int
    info: int
    chunk: int


@dataclass(frozen=True)
class ChunkExit:
    x_q8: int
    y_q8: int
    special: int
    next_episode: int
    next_mission: int
    chunk: int


@dataclass(frozen=True)
class ChunkDoor:
    x: int
    y: int
    special: int
    chunk: int


def snap_floor(value: int, unit: int) -> int:
    return math.floor(value / unit) * unit


def snap_ceil(value: int, unit: int) -> int:
    return math.ceil(value / unit) * unit


def ceil_div(value: int, unit: int) -> int:
    return (value + unit - 1) // unit


def grid_coord(x: int, y: int, origin_x: int, origin_y: int, cell_units: int) -> tuple[float, float]:
    return (x - origin_x) / cell_units, (origin_y - y) / cell_units


def grid_point(x: int, y: int, origin_x: int, origin_y: int, cell_units: int) -> tuple[int, int]:
    gx, gy = grid_coord(x, y, origin_x, origin_y, cell_units)
    return int(round(gx)), int(round(gy))


def doom_coord_from_cell(gx: int, gy: int, origin_x: int, origin_y: int, cell_units: int) -> tuple[float, float]:
    return origin_x + (gx + 0.5) * cell_units, origin_y - (gy + 0.5) * cell_units


def start_direction_score(grid: list[list[int]], start_x: float, start_y: float, dir_x: float, dir_y: float) -> int:
    width = len(grid[0])
    height = len(grid)
    score = 0
    for step in (0.50, 0.95, 1.40, 1.85, 2.30, 2.75, 3.20):
        x = int(math.floor(start_x + dir_x * step))
        y = int(math.floor(start_y + dir_y * step))
        if x < 0 or y < 0 or x >= width or y >= height:
            score -= 8
            break
        if grid[y][x]:
            score -= 8
            break
        score += 2
    return score


def open_span_lengths(grid: list[list[int]], x: int, y: int) -> tuple[int, int]:
    width = len(grid[0])
    height = len(grid)
    left = x
    right = x
    up = y
    down = y
    while left > 0 and not grid[y][left - 1]:
        left -= 1
    while right + 1 < width and not grid[y][right + 1]:
        right += 1
    while up > 0 and not grid[up - 1][x]:
        up -= 1
    while down + 1 < height and not grid[down + 1][x]:
        down += 1
    return right - left + 1, down - up + 1


def wall_clearance(grid: list[list[int]], x: int, y: int, limit: int = 5) -> int:
    width = len(grid[0])
    height = len(grid)
    for radius in range(1, limit + 1):
        for yy in range(y - radius, y + radius + 1):
            for xx in range(x - radius, x + radius + 1):
                if max(abs(xx - x), abs(yy - y)) != radius:
                    continue
                if xx < 0 or yy < 0 or xx >= width or yy >= height or grid[yy][xx]:
                    return radius - 1
    return limit


def visual_linedef_for_start(
    line: dc.LineDef,
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
) -> bool:
    if line.side_back == 0xFFFF:
        return True
    if line.side_front >= len(sidedefs) or line.side_back >= len(sidedefs):
        return True
    front_side = sidedefs[line.side_front]
    back_side = sidedefs[line.side_back]
    if front_side.mid_texture != "-" or back_side.mid_texture != "-":
        return True
    if front_side.sector >= len(sectors) or back_side.sector >= len(sectors):
        return True
    front = sectors[front_side.sector]
    back = sectors[back_side.sector]
    return front.floor_height != back.floor_height or front.ceiling_height != back.ceiling_height


def ray_segment_distance(
    px: float,
    py: float,
    dx: float,
    dy: float,
    ax: float,
    ay: float,
    bx: float,
    by: float,
) -> float | None:
    sx = bx - ax
    sy = by - ay
    den = dx * sy - dy * sx
    if abs(den) < 0.000001:
        return None
    qx = ax - px
    qy = ay - py
    t = (qx * sy - qy * sx) / den
    u = (qx * dy - qy * dx) / den
    if t <= 0.0 or u < 0.0 or u > 1.0:
        return None
    return t


def forward_visual_clearance(
    x: float,
    y: float,
    angle: int,
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    vertices: list[dc.Vertex],
) -> float:
    angle_rad = math.radians(angle)
    dir_x = math.cos(angle_rad)
    dir_y = math.sin(angle_rad)
    best = 4096.0
    for line in linedefs:
        if line.v1 >= len(vertices) or line.v2 >= len(vertices):
            continue
        if not visual_linedef_for_start(line, sidedefs, sectors):
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        dist = ray_segment_distance(x, y, dir_x, dir_y, a.x, a.y, b.x, b.y)
        if dist is not None and dist < best:
            best = dist
    return best


def sector_index_for_point(
    x: float,
    y: float,
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    vertices: list[dc.Vertex],
    sectors: list[dc.Sector],
) -> int | None:
    for sector_index in range(len(sectors)):
        if dc.point_in_sector(x, y, dc.sector_segments(sector_index, linedefs, sidedefs, vertices)):
            return sector_index
    return None


def start_room_center_cell(
    grid: list[list[int]],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    vertices: list[dc.Vertex],
    origin_x: int,
    origin_y: int,
    cell_units: int,
    player: dc.Thing,
    fallback_x: int,
    fallback_y: int,
    chunk_size: int,
    start_angle: int,
) -> tuple[int, int]:
    sector_index = sector_index_for_point(player.x, player.y, linedefs, sidedefs, vertices, sectors)
    if sector_index is None:
        return fallback_x, fallback_y

    segments = dc.sector_segments(sector_index, linedefs, sidedefs, vertices)
    chunk_min_x = (fallback_x // chunk_size) * chunk_size
    chunk_min_y = (fallback_y // chunk_size) * chunk_size
    chunk_max_x = chunk_min_x + chunk_size
    chunk_max_y = chunk_min_y + chunk_size
    candidates: list[tuple[int, int]] = []
    for y in range(chunk_min_y, min(chunk_max_y, len(grid))):
        row = grid[y]
        for x in range(chunk_min_x, min(chunk_max_x, len(row))):
            solid = row[x]
            if solid:
                continue
            doom_x, doom_y = doom_coord_from_cell(x, y, origin_x, origin_y, cell_units)
            if dc.point_in_sector(doom_x, doom_y, segments):
                candidates.append((x, y))

    if not candidates:
        return fallback_x, fallback_y

    min_x = min(x for x, _y in candidates)
    max_x = max(x for x, _y in candidates)
    min_y = min(y for _x, y in candidates)
    max_y = max(y for _x, y in candidates)
    target_x = (min_x + max_x) / 2.0
    target_y = (min_y + max_y) / 2.0
    best_cell = (fallback_x, fallback_y)
    best_score = -999999
    for x, y in candidates:
        dx = x - target_x
        dy = y - target_y
        horizontal, vertical = open_span_lengths(grid, x, y)
        clearance = wall_clearance(grid, x, y)
        doom_x, doom_y = doom_coord_from_cell(x, y, origin_x, origin_y, cell_units)
        forward_clearance = min(
            forward_visual_clearance(doom_x, doom_y, start_angle, linedefs, sidedefs, sectors, vertices),
            384.0,
        )
        score = int(
            -(dx * dx + dy * dy) * 64
            + clearance * 16
            + horizontal
            + vertical
            + forward_clearance * 6
        )
        if forward_clearance < 160.0:
            score -= int((160.0 - forward_clearance) * 16)
        if score > best_score:
            best_score = score
            best_cell = (x, y)
    return best_cell


def chunk_start_view(
    start_angle: int,
    grid: list[list[int]],
    start_chunk: int,
    chunk_cols: int,
    chunk_size: int,
    start_x: float,
    start_y: float,
) -> tuple[float, float, float, float]:
    angle_rad = math.radians(start_angle)
    base_x = math.cos(angle_rad)
    base_y = -math.sin(angle_rad)
    candidates = (
        (base_x, base_y),
        (base_x, -base_y),
        (1.0, 0.0),
        (-1.0, 0.0),
        (0.0, 1.0),
        (0.0, -1.0),
    )
    global_x = (start_chunk % chunk_cols) * chunk_size + start_x
    global_y = (start_chunk // chunk_cols) * chunk_size + start_y
    best_dir_x, best_dir_y = candidates[0]
    best_score = -9999
    for dir_x, dir_y in candidates:
        score = start_direction_score(grid, global_x, global_y, dir_x, dir_y)
        if score > best_score:
            best_score = score
            best_dir_x = dir_x
            best_dir_y = dir_y
    plane_x = -best_dir_y * 0.66
    plane_y = best_dir_x * 0.66
    return best_dir_x, best_dir_y, plane_x, plane_y


def parse_map(iwad: str, zip_member: str | None, map_name: str):
    wad = dc.Wad(dc.read_wad(iwad, zip_member))
    lumps = wad.map_lumps(map_name)
    return (
        wad,
        dc.parse_vertices(lumps["VERTEXES"]),
        dc.parse_linedefs(lumps["LINEDEFS"]),
        dc.parse_sidedefs(lumps["SIDEDEFS"]),
        dc.parse_sectors(lumps["SECTORS"]),
        dc.parse_things(lumps["THINGS"]),
    )


def build_bounds(
    vertices: list[dc.Vertex],
    chunk_size: int,
    cell_units: int,
    player: dc.Thing,
    start_local_x: float,
    start_local_y: float,
) -> tuple[int, int, int, int, int, int]:
    min_x = min(v.x for v in vertices)
    max_x = max(v.x for v in vertices)
    min_y = min(v.y for v in vertices)
    max_y = max(v.y for v in vertices)
    page_units = chunk_size * cell_units
    start_offset_x = int(round(start_local_x * cell_units))
    start_offset_y = int(round(start_local_y * cell_units))
    start_page_x = max(0, math.ceil((player.x - start_offset_x - min_x) / page_units))
    start_page_y = max(0, math.ceil((max_y - player.y - start_offset_y) / page_units))
    origin_x = player.x - start_page_x * page_units - start_offset_x
    origin_y = player.y + start_page_y * page_units + start_offset_y
    end_x = snap_ceil(max_x, page_units)
    end_y = snap_floor(min_y, page_units)
    width = max(chunk_size, ceil_div(end_x - origin_x, cell_units))
    height = max(chunk_size, ceil_div(origin_y - end_y, cell_units))
    return origin_x, origin_y, width, height, min_x, max_y


def build_base_grids(
    wad: dc.Wad,
    vertices: list[dc.Vertex],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    cell_units: int,
) -> tuple[list[list[int]], list[list[int]], list[list[int]], list[list[int]]]:
    grid = [[1 for _ in range(width)] for _ in range(height)]
    tex_grid = [[0 for _ in range(width)] for _ in range(height)]
    tex_phase_grid = [[0 for _ in range(width)] for _ in range(height)]
    priority_grid = [[0 for _ in range(width)] for _ in range(height)]
    texture_widths = dc.parse_texture_widths(wad)
    sector_edges = [dc.sector_segments(i, linedefs, sidedefs, vertices) for i in range(len(sectors))]
    sector_bounds: list[tuple[int, int, int, int]] = []

    for edges in sector_edges:
        if not edges:
            sector_bounds.append((0, 0, -1, -1))
            continue
        xs = [point.x for segment in edges for point in segment]
        ys = [point.y for segment in edges for point in segment]
        sector_bounds.append((min(xs), min(ys), max(xs), max(ys)))

    for gy in range(height):
        for gx in range(width):
            doom_x, doom_y = doom_coord_from_cell(gx, gy, origin_x, origin_y, cell_units)
            for sector_index, edges in enumerate(sector_edges):
                if not edges:
                    continue
                min_x, min_y, max_x, max_y = sector_bounds[sector_index]
                if doom_x < min_x or doom_x > max_x or doom_y < min_y or doom_y > max_y:
                    continue
                if dc.point_in_sector(doom_x, doom_y, edges):
                    grid[gy][gx] = 0
                    break

    for line in linedefs:
        if not dc.is_solid_linedef(line, sidedefs, sectors):
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        x0, y0 = grid_point(a.x, a.y, origin_x, origin_y, cell_units)
        x1, y1 = grid_point(b.x, b.y, origin_x, origin_y, cell_units)
        texture_name = dc.solid_line_texture(line, sidedefs)
        texture_class = dc.wall_texture_class(texture_name)
        texture_priority = dc.wall_texture_priority(texture_class)
        texture_width = texture_widths.get(texture_name, 64)
        texture_x = dc.solid_line_texture_x(line, sidedefs)
        cells = dc.line_cells(grid, x0, y0, x1, y1)
        cell_count = max(1, len(cells))
        for step, (cell_x, cell_y) in enumerate(cells):
            grid[cell_y][cell_x] = 1
            if texture_priority >= priority_grid[cell_y][cell_x]:
                tex_grid[cell_y][cell_x] = texture_class
                tex_phase_grid[cell_y][cell_x] = dc.texture_phase_for_cell(texture_x, texture_width, step, cell_count)
                priority_grid[cell_y][cell_x] = texture_priority
    return grid, tex_grid, tex_phase_grid, priority_grid


def build_sector_grids(
    grid: list[list[int]],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    vertices: list[dc.Vertex],
    origin_x: int,
    origin_y: int,
    cell_units: int,
) -> tuple[list[list[int]], list[list[int]], list[list[int]], list[list[int]], list[list[int]]]:
    height = len(grid)
    width = len(grid[0])
    floor_grid = [[0 for _ in range(width)] for _ in range(height)]
    damage_grid = [[0 for _ in range(width)] for _ in range(height)]
    light_grid = [[2 for _ in range(width)] for _ in range(height)]
    floor_height_grid = [[0 for _ in range(width)] for _ in range(height)]
    ceiling_height_grid = [[128 for _ in range(width)] for _ in range(height)]
    sector_edges = [dc.sector_segments(i, linedefs, sidedefs, vertices) for i in range(len(sectors))]
    sector_bounds: list[tuple[int, int, int, int]] = []

    for edges in sector_edges:
        if not edges:
            sector_bounds.append((0, 0, -1, -1))
            continue
        xs = [point.x for segment in edges for point in segment]
        ys = [point.y for segment in edges for point in segment]
        sector_bounds.append((min(xs), min(ys), max(xs), max(ys)))

    for gy, row in enumerate(grid):
        for gx, cell in enumerate(row):
            if cell:
                continue
            doom_x, doom_y = doom_coord_from_cell(gx, gy, origin_x, origin_y, cell_units)
            for sector_index, sector in enumerate(sectors):
                min_x, min_y, max_x, max_y = sector_bounds[sector_index]
                if doom_x < min_x or doom_x > max_x or doom_y < min_y or doom_y > max_y:
                    continue
                edges = sector_edges[sector_index]
                if edges and dc.point_in_sector(doom_x, doom_y, edges):
                    floor_grid[gy][gx] = dc.sector_floor_visual_kind(sector)
                    damage_grid[gy][gx] = dc.sector_damage_amount(sector.special)
                    light_grid[gy][gx] = dc.sector_light_band(sector.light_level)
                    floor_height_grid[gy][gx] = sector.floor_height
                    ceiling_height_grid[gy][gx] = sector.ceiling_height
                    break
    return floor_grid, damage_grid, light_grid, floor_height_grid, ceiling_height_grid


def build_things(
    things: list[dc.Thing],
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    cell_units: int,
    chunk_size: int,
    chunk_cols: int,
    skill_mask: int,
) -> list[ChunkThing]:
    rows: list[ChunkThing] = []
    for thing in things:
        if thing.type not in dc.RUNTIME_THING_TYPES:
            continue
        if (thing.flags & skill_mask) == 0:
            continue
        gx, gy = grid_coord(thing.x, thing.y, origin_x, origin_y, cell_units)
        if gx < 0 or gy < 0 or gx >= width or gy >= height:
            continue
        cell_x = int(math.floor(gx))
        cell_y = int(math.floor(gy))
        chunk_x = cell_x // chunk_size
        chunk_y = cell_y // chunk_size
        chunk = chunk_y * chunk_cols + chunk_x
        local_x = gx - chunk_x * chunk_size
        local_y = gy - chunk_y * chunk_size
        rows.append(
            ChunkThing(
                int(round(local_x * 256)),
                int(round(local_y * 256)),
                thing.type,
                thing.flags,
                dc.runtime_thing_class(thing.type),
                dc.runtime_thing_info(thing.type),
                chunk,
            )
        )
    rows.sort(key=lambda thing: (thing.chunk, thing.y_q8, thing.x_q8, thing.type))
    return rows


def build_exits(
    linedefs: list[dc.LineDef],
    vertices: list[dc.Vertex],
    grid: list[list[int]],
    origin_x: int,
    origin_y: int,
    cell_units: int,
    chunk_size: int,
    chunk_cols: int,
    map_name: str,
) -> list[ChunkExit]:
    rows: list[ChunkExit] = []
    seen: set[tuple[int, int]] = set()
    for line in linedefs:
        if line.special not in dc.EXIT_SPECIALS:
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        gx, gy = grid_coord((a.x + b.x) // 2, (a.y + b.y) // 2, origin_x, origin_y, cell_units)
        cell_x, cell_y = dc.nearest_open(grid, int(math.floor(gx)), int(math.floor(gy)))
        key = (cell_x, cell_y)
        if key in seen:
            continue
        seen.add(key)
        chunk = (cell_y // chunk_size) * chunk_cols + (cell_x // chunk_size)
        next_episode, next_mission = dc.exit_episode_mission(map_name, line.special)
        rows.append(
            ChunkExit(
                int(round((cell_x + 0.5) * 256)),
                int(round((cell_y + 0.5) * 256)),
                line.special,
                next_episode,
                next_mission,
                chunk,
            )
        )
    rows.sort(key=lambda row: (row.chunk, row.y_q8, row.x_q8, row.special))
    return rows


def build_doors(
    linedefs: list[dc.LineDef],
    vertices: list[dc.Vertex],
    grid: list[list[int]],
    origin_x: int,
    origin_y: int,
    cell_units: int,
    chunk_size: int,
    chunk_cols: int,
) -> list[ChunkDoor]:
    rows: list[ChunkDoor] = []
    seen: set[tuple[int, int]] = set()
    for line in linedefs:
        if line.special not in dc.DOOR_SPECIALS:
            continue
        a = vertices[line.v1]
        b = vertices[line.v2]
        x0, y0 = grid_point(a.x, a.y, origin_x, origin_y, cell_units)
        x1, y1 = grid_point(b.x, b.y, origin_x, origin_y, cell_units)
        for cell_x, cell_y in dc.line_cells(grid, x0, y0, x1, y1):
            if not grid[cell_y][cell_x]:
                continue
            key = (cell_x, cell_y)
            if key in seen:
                continue
            seen.add(key)
            chunk = (cell_y // chunk_size) * chunk_cols + (cell_x // chunk_size)
            rows.append(ChunkDoor(cell_x, cell_y, line.special, chunk))
    rows.sort(key=lambda row: (row.chunk, row.y, row.x, row.special))
    return rows


def build_door_grid(width: int, height: int, doors: list[ChunkDoor]) -> list[list[int]]:
    grid = [[0 for _ in range(width)] for _ in range(height)]
    for index, door in enumerate(doors):
        if index >= 254:
            raise ValueError("chunk door id table supports at most 254 doors")
        grid[door.y][door.x] = index + 2
    return grid


def route_neighbors(x: int, y: int) -> tuple[tuple[int, int], ...]:
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


def minimum_wall_route(
    grid: list[list[int]],
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    passable: set[tuple[int, int]],
) -> list[tuple[int, int]] | None:
    width = len(grid[0])
    height = len(grid)
    queue: list[tuple[int, int, int, int]] = [(0, 0, start[0], start[1])]
    best: dict[tuple[int, int], tuple[int, int]] = {start: (0, 0)}
    prev: dict[tuple[int, int], tuple[int, int] | None] = {start: None}
    while queue:
        cost, steps, x, y = heapq.heappop(queue)
        if best.get((x, y)) != (cost, steps):
            continue
        if (x, y) in targets:
            path: list[tuple[int, int]] = []
            cur: tuple[int, int] | None = (x, y)
            while cur is not None:
                path.append(cur)
                cur = prev[cur]
            return list(reversed(path))
        for nx, ny in route_neighbors(x, y):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            extra = 1 if grid[ny][nx] and (nx, ny) not in passable else 0
            next_score = (cost + extra, steps + 1)
            old_score = best.get((nx, ny))
            if old_score is not None and old_score <= next_score:
                continue
            best[(nx, ny)] = next_score
            prev[(nx, ny)] = (x, y)
            heapq.heappush(queue, (next_score[0], next_score[1], nx, ny))
    return None


def repair_start_exit_route(
    grid: list[list[int]],
    tex_grid: list[list[int]],
    tex_phase_grid: list[list[int]],
    floor_grid: list[list[int]],
    damage_grid: list[list[int]],
    light_grid: list[list[int]],
    floor_height_grid: list[list[int]],
    ceiling_height_grid: list[list[int]],
    start: tuple[int, int],
    exits: list[ChunkExit],
    doors: list[ChunkDoor],
    lifts: list[tuple[int, list[tuple[int, int]]]],
) -> int:
    targets = {(exit.x_q8 >> 8, exit.y_q8 >> 8) for exit in exits}
    if not targets:
        return 0
    passable = {(door.x, door.y) for door in doors}
    passable.update((x, y) for _tag, cells in lifts for x, y in cells)
    path = minimum_wall_route(grid, start, targets, passable)
    if path is None:
        return 0
    repaired = 0
    for x, y in path:
        if not grid[y][x] or (x, y) in passable:
            continue
        grid[y][x] = 0
        tex_grid[y][x] = 0
        tex_phase_grid[y][x] = 0
        floor_grid[y][x] = 0
        damage_grid[y][x] = 0
        light_grid[y][x] = 2
        floor_height_grid[y][x] = 0
        ceiling_height_grid[y][x] = 128
        repaired += 1
    return repaired


def chunk_cells(grid: list[list[int]], chunk: int, chunk_size: int, chunk_cols: int) -> list[int]:
    chunk_x = chunk % chunk_cols
    chunk_y = chunk // chunk_cols
    base_x = chunk_x * chunk_size
    base_y = chunk_y * chunk_size
    width = len(grid[0])
    height = len(grid)
    values: list[int] = []
    for y in range(base_y, base_y + chunk_size):
        for x in range(base_x, base_x + chunk_size):
            values.append(grid[y][x] if 0 <= x < width and 0 <= y < height else 1)
    return values


def write_u8_chunks(f, name: str, grids: list[list[int]], chunk_count: int, chunk_size: int, chunk_cols: int) -> None:
    f.write(f"const unsigned char {name}[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS] = {{\n")
    for chunk in range(chunk_count):
        values = chunk_cells(grids, chunk, chunk_size, chunk_cols)
        f.write("    {")
        f.write(",".join(str(value & 0xFF) for value in values))
        f.write("},\n")
    f.write("};\n\n")


def write_i16_chunks(f, name: str, grids: list[list[int]], chunk_count: int, chunk_size: int, chunk_cols: int) -> None:
    f.write(f"const short {name}[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS] = {{\n")
    for chunk in range(chunk_count):
        values = chunk_cells(grids, chunk, chunk_size, chunk_cols)
        f.write("    {")
        f.write(",".join(str(value) for value in values))
        f.write("},\n")
    f.write("};\n\n")


def max_active_window_things(chunk_count_things: list[int], chunk_cols: int) -> int:
    chunk_count = len(chunk_count_things)
    max_window = 0
    for chunk in range(chunk_count):
        chunk_x = chunk % chunk_cols
        chunk_y = chunk // chunk_cols
        total = 0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx = chunk_x + dx
                ny = chunk_y + dy
                if nx < 0 or ny < 0 or nx >= chunk_cols:
                    continue
                neighbor = ny * chunk_cols + nx
                if neighbor >= chunk_count:
                    continue
                total += chunk_count_things[neighbor]
        max_window = max(max_window, total)
    return max(1, max_window)


def write_outputs(
    header_path: str,
    source_path: str,
    iwad_name: str,
    map_name: str,
    origin_x: int,
    origin_y: int,
    cell_units: int,
    chunk_size: int,
    chunk_cols: int,
    chunk_rows: int,
    start_chunk: int,
    start_x: float,
    start_y: float,
    start_angle: int,
    grid: list[list[int]],
    tex_grid: list[list[int]],
    tex_phase_grid: list[list[int]],
    floor_grid: list[list[int]],
    damage_grid: list[list[int]],
    light_grid: list[list[int]],
    floor_height_grid: list[list[int]],
    ceiling_height_grid: list[list[int]],
    lift_grid: list[list[int]],
    things: list[ChunkThing],
    exits: list[ChunkExit],
    doors: list[ChunkDoor],
    lifts: list[tuple[int, list[tuple[int, int]]]],
    lift_triggers: list[tuple[int, int, int, int, int]],
) -> None:
    chunk_count = chunk_cols * chunk_rows
    chunk_first: list[int] = []
    chunk_count_things: list[int] = []
    door_grid = build_door_grid(len(grid[0]), len(grid), doors)
    cursor = 0
    for chunk in range(chunk_count):
        chunk_first.append(cursor)
        count = 0
        while cursor + count < len(things) and things[cursor + count].chunk == chunk:
            count += 1
        chunk_count_things.append(count)
        cursor += count

    os.makedirs(os.path.dirname(header_path) or ".", exist_ok=True)
    os.makedirs(os.path.dirname(source_path) or ".", exist_ok=True)
    with open(header_path, "w", encoding="ascii") as f:
        dir_x, dir_y, plane_x, plane_y = chunk_start_view(
            start_angle, grid, start_chunk, chunk_cols, chunk_size, start_x, start_y
        )
        f.write("/* Generated by tools/doom_chunk_convert.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_CHUNKS_GENERATED_H\n#define DOOM_CHUNKS_GENERATED_H\n\n")
        f.write(f"#define DOOM_CHUNK_SOURCE \"{iwad_name}\"\n")
        f.write(f"#define DOOM_CHUNK_MAP_NAME \"{map_name.upper()}\"\n")
        f.write(f"#define DOOM_CHUNK_SIZE {chunk_size}\n")
        f.write(f"#define DOOM_CHUNK_CELLS {chunk_size * chunk_size}\n")
        f.write(f"#define DOOM_CHUNK_CELL_DOOM_UNITS {cell_units}\n")
        f.write(f"#define DOOM_CHUNK_ORIGIN_X {origin_x}\n")
        f.write(f"#define DOOM_CHUNK_ORIGIN_Y {origin_y}\n")
        f.write(f"#define DOOM_CHUNK_GRID_W {len(grid[0])}\n")
        f.write(f"#define DOOM_CHUNK_GRID_H {len(grid)}\n")
        f.write(f"#define DOOM_CHUNK_COLS {chunk_cols}\n")
        f.write(f"#define DOOM_CHUNK_ROWS {chunk_rows}\n")
        f.write(f"#define DOOM_CHUNK_COUNT {chunk_count}\n")
        f.write(f"#define DOOM_CHUNK_THING_COUNT {len(things)}\n")
        f.write(f"#define DOOM_CHUNK_MAX_ACTIVE_THINGS {max_active_window_things(chunk_count_things, chunk_cols)}\n\n")
        f.write(f"#define DOOM_CHUNK_EXIT_COUNT {len(exits)}\n")
        f.write(f"#define DOOM_CHUNK_DOOR_COUNT {len(doors)}\n\n")
        f.write(f"#define DOOM_CHUNK_LIFT_COUNT {len(lifts)}\n")
        f.write(f"#define DOOM_CHUNK_LIFT_TRIGGER_COUNT {len(lift_triggers)}\n")
        lift_cell_refs = [y * len(grid[0]) + x for _tag, cells in lifts for x, y in cells]
        f.write(f"#define DOOM_CHUNK_LIFT_CELL_REF_COUNT {len(lift_cell_refs)}\n\n")
        f.write(f"#define DOOM_CHUNK_START_CHUNK {start_chunk}\n")
        f.write(f"#define DOOM_CHUNK_START_X {start_x:.3f}\n")
        f.write(f"#define DOOM_CHUNK_START_Y {start_y:.3f}\n")
        f.write(f"#define DOOM_CHUNK_START_X_Q8 {int(round(start_x * 256))}\n")
        f.write(f"#define DOOM_CHUNK_START_Y_Q8 {int(round(start_y * 256))}\n")
        f.write(f"#define DOOM_CHUNK_START_ANGLE {start_angle}\n")
        f.write(f"#define DOOM_CHUNK_START_DIR_X {dir_x:.6f}\n")
        f.write(f"#define DOOM_CHUNK_START_DIR_Y {dir_y:.6f}\n")
        f.write(f"#define DOOM_CHUNK_START_PLANE_X {plane_x:.6f}\n")
        f.write(f"#define DOOM_CHUNK_START_PLANE_Y {plane_y:.6f}\n\n")
        f.write("typedef struct NgChunkThing {\n")
        f.write("    short x_q8;\n")
        f.write("    short y_q8;\n")
        f.write("    unsigned short type;\n")
        f.write("    unsigned short flags;\n")
        f.write("    unsigned char thing_class;\n")
        f.write("    unsigned char info;\n")
        f.write("    unsigned short chunk;\n")
        f.write("} NgChunkThing;\n\n")
        f.write("typedef struct NgChunkExit {\n")
        f.write("    short x_q8;\n")
        f.write("    short y_q8;\n")
        f.write("    unsigned short special;\n")
        f.write("    unsigned char next_episode;\n")
        f.write("    unsigned char next_mission;\n")
        f.write("    unsigned short chunk;\n")
        f.write("} NgChunkExit;\n\n")
        f.write("typedef struct NgChunkDoor {\n")
        f.write("    unsigned short x;\n")
        f.write("    unsigned short y;\n")
        f.write("    unsigned short special;\n")
        f.write("    unsigned short chunk;\n")
        f.write("} NgChunkDoor;\n\n")
        f.write("typedef struct NgChunkLift {\n")
        f.write("    unsigned short first_cell;\n")
        f.write("    unsigned short cell_count;\n")
        f.write("    unsigned short tag;\n")
        f.write("} NgChunkLift;\n\n")
        f.write("typedef struct NgChunkLiftTrigger {\n")
        f.write("    unsigned short x;\n")
        f.write("    unsigned short y;\n")
        f.write("    unsigned char lift;\n")
        f.write("    unsigned short special;\n")
        f.write("    unsigned char walk;\n")
        f.write("} NgChunkLiftTrigger;\n\n")
        f.write("extern const unsigned char g_chunk_solid[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_door_cell[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_lift_cell[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_tex[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_tex_phase[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_floor_visual[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_damage[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_light[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const short g_chunk_floor_height[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const short g_chunk_ceiling_height[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned short g_chunk_thing_first[DOOM_CHUNK_COUNT];\n")
        f.write("extern const unsigned char g_chunk_thing_count[DOOM_CHUNK_COUNT];\n")
        f.write("extern const NgChunkThing g_chunk_things[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];\n\n")
        f.write("extern const NgChunkExit g_chunk_exits[DOOM_CHUNK_EXIT_COUNT ? DOOM_CHUNK_EXIT_COUNT : 1];\n")
        f.write("extern const NgChunkDoor g_chunk_doors[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1];\n\n")
        f.write("extern const NgChunkLift g_chunk_lifts[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1];\n")
        f.write("extern const NgChunkLiftTrigger g_chunk_lift_triggers[DOOM_CHUNK_LIFT_TRIGGER_COUNT ? DOOM_CHUNK_LIFT_TRIGGER_COUNT : 1];\n")
        f.write("extern const unsigned short g_chunk_lift_cells[DOOM_CHUNK_LIFT_CELL_REF_COUNT ? DOOM_CHUNK_LIFT_CELL_REF_COUNT : 1];\n\n")
        f.write("#endif /* DOOM_CHUNKS_GENERATED_H */\n")

    with open(source_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_chunk_convert.py; do not edit by hand. */\n")
        f.write('#include "doom_chunks_generated.h"\n\n')
        write_u8_chunks(f, "g_chunk_solid", grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_door_cell", door_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_lift_cell", lift_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_tex", tex_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_tex_phase", tex_phase_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_floor_visual", floor_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_damage", damage_grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_light", light_grid, chunk_count, chunk_size, chunk_cols)
        write_i16_chunks(f, "g_chunk_floor_height", floor_height_grid, chunk_count, chunk_size, chunk_cols)
        write_i16_chunks(f, "g_chunk_ceiling_height", ceiling_height_grid, chunk_count, chunk_size, chunk_cols)
        f.write("const unsigned short g_chunk_thing_first[DOOM_CHUNK_COUNT] = {")
        f.write(",".join(str(value) for value in chunk_first))
        f.write("};\n\n")
        f.write("const unsigned char g_chunk_thing_count[DOOM_CHUNK_COUNT] = {")
        f.write(",".join(str(value) for value in chunk_count_things))
        f.write("};\n\n")
        f.write("const NgChunkThing g_chunk_things[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1] = {\n")
        if things:
            for thing in things:
                f.write(
                    f"    {{{thing.x_q8},{thing.y_q8},{thing.type},{thing.flags},"
                    f"{thing.thing_class},{thing.info},{thing.chunk}}},\n"
                )
        else:
            f.write("    {0,0,0,0,0,0,0},\n")
        f.write("};\n")
        f.write("\nconst NgChunkExit g_chunk_exits[DOOM_CHUNK_EXIT_COUNT ? DOOM_CHUNK_EXIT_COUNT : 1] = {\n")
        if exits:
            for row in exits:
                f.write(
                    f"    {{{row.x_q8},{row.y_q8},{row.special},"
                    f"{row.next_episode},{row.next_mission},{row.chunk}}},\n"
                )
        else:
            f.write("    {0,0,0,0,0,0},\n")
        f.write("};\n")
        f.write("\nconst NgChunkDoor g_chunk_doors[DOOM_CHUNK_DOOR_COUNT ? DOOM_CHUNK_DOOR_COUNT : 1] = {\n")
        if doors:
            for row in doors:
                f.write(f"    {{{row.x},{row.y},{row.special},{row.chunk}}},\n")
        else:
            f.write("    {0,0,0,0},\n")
        f.write("};\n")
        f.write("\nconst NgChunkLift g_chunk_lifts[DOOM_CHUNK_LIFT_COUNT ? DOOM_CHUNK_LIFT_COUNT : 1] = {\n")
        first_cell = 0
        if lifts:
            for tag, cells in lifts:
                f.write(f"    {{{first_cell},{len(cells)},{tag}}},\n")
                first_cell += len(cells)
        else:
            f.write("    {0,0,0},\n")
        f.write("};\n")
        f.write("\nconst NgChunkLiftTrigger g_chunk_lift_triggers[DOOM_CHUNK_LIFT_TRIGGER_COUNT ? DOOM_CHUNK_LIFT_TRIGGER_COUNT : 1] = {\n")
        if lift_triggers:
            for x, y, lift_index, special, walk in lift_triggers:
                f.write(f"    {{{x},{y},{lift_index},{special},{walk}}},\n")
        else:
            f.write("    {0,0,0,0,0},\n")
        f.write("};\n")
        f.write("\nconst unsigned short g_chunk_lift_cells[DOOM_CHUNK_LIFT_CELL_REF_COUNT ? DOOM_CHUNK_LIFT_CELL_REF_COUNT : 1] = {\n")
        if lift_cell_refs:
            for i in range(0, len(lift_cell_refs), 24):
                f.write("    ")
                f.write(",".join(str(cell) for cell in lift_cell_refs[i : i + 24]))
                f.write(",\n")
        else:
            f.write("    0,\n")
        f.write("};\n")


def write_preview(
    preview_path: str,
    grid: list[list[int]],
    things: list[ChunkThing],
    chunk_size: int,
) -> None:
    thing_cells: dict[tuple[int, int], str] = {}
    chunk_cols = math.ceil(len(grid[0]) / chunk_size)
    for thing in things:
        chunk_x = thing.chunk % chunk_cols
        chunk_y = thing.chunk // chunk_cols
        cell = (chunk_x * chunk_size + (thing.x_q8 >> 8), chunk_y * chunk_size + (thing.y_q8 >> 8))
        if thing.thing_class == dc.THING_CLASS_MONSTER:
            marker = "M"
        elif thing.thing_class == dc.THING_CLASS_PICKUP:
            marker = "I"
        elif thing.thing_class == dc.THING_CLASS_THREAT:
            marker = "T"
        else:
            marker = "?"
        thing_cells.setdefault(cell, marker)

    os.makedirs(os.path.dirname(preview_path) or ".", exist_ok=True)
    with open(preview_path, "w", encoding="ascii") as f:
        f.write("# = wall, . = open, M = monster, I = item/powerup/weapon, T = threat/barrel\n")
        for y, row in enumerate(grid):
            if y and y % chunk_size == 0:
                f.write("\n")
            line: list[str] = []
            for x, cell in enumerate(row):
                if x and x % chunk_size == 0:
                    line.append(" ")
                line.append(thing_cells.get((x, y), "#" if cell else "."))
            f.write("".join(line) + "\n")


def convert(args: argparse.Namespace) -> None:
    wad, vertices, linedefs, sidedefs, sectors, things = parse_map(args.iwad, args.zip_member, args.map)
    player = next((thing for thing in things if thing.type == 1), None)
    if player is None:
        raise ValueError("map has no player 1 start thing")
    origin_x, origin_y, width, height, _min_x, _max_y = build_bounds(
        vertices,
        args.chunk_size,
        args.cell_units,
        player,
        args.start_local_x,
        args.start_local_y,
    )
    chunk_cols = ceil_div(width, args.chunk_size)
    chunk_rows = ceil_div(height, args.chunk_size)
    width = chunk_cols * args.chunk_size
    height = chunk_rows * args.chunk_size
    grid, tex_grid, tex_phase_grid, _priority_grid = build_base_grids(
        wad, vertices, linedefs, sidedefs, sectors, origin_x, origin_y, width, height, args.cell_units
    )
    floor_grid, damage_grid, light_grid, floor_height_grid, ceiling_height_grid = build_sector_grids(
        grid, linedefs, sidedefs, sectors, vertices, origin_x, origin_y, args.cell_units
    )
    converted_lifts, converted_lift_triggers, lift_grid = dc.runtime_lifts(
        linedefs, sidedefs, sectors, vertices, grid, origin_x, origin_y, args.cell_units, 0
    )
    converted_things = build_things(
        things, origin_x, origin_y, width, height, args.cell_units,
        args.chunk_size, chunk_cols, args.skill_mask
    )
    converted_exits = build_exits(
        linedefs, vertices, grid, origin_x, origin_y, args.cell_units,
        args.chunk_size, chunk_cols, args.map.upper()
    )
    converted_doors = build_doors(
        linedefs, vertices, grid, origin_x, origin_y, args.cell_units,
        args.chunk_size, chunk_cols
    )
    start_x, start_y = grid_coord(player.x, player.y, origin_x, origin_y, args.cell_units)
    start_cell_x = int(math.floor(start_x))
    start_cell_y = int(math.floor(start_y))
    if grid[start_cell_y][start_cell_x]:
        start_cell_x, start_cell_y = dc.nearest_open(grid, start_cell_x, start_cell_y)
        start_x = start_cell_x + 0.5
        start_y = start_cell_y + 0.5
    if not args.keep_wad_start_offset:
        start_cell_x, start_cell_y = start_room_center_cell(
            grid,
            linedefs,
            sidedefs,
            sectors,
            vertices,
            origin_x,
            origin_y,
            args.cell_units,
            player,
            start_cell_x,
            start_cell_y,
            args.chunk_size,
            player.angle,
        )
        start_x = start_cell_x + 0.5
        start_y = start_cell_y + 0.5
    route_start = (start_cell_x, start_cell_y)
    route_repair_cells = repair_start_exit_route(
        grid,
        tex_grid,
        tex_phase_grid,
        floor_grid,
        damage_grid,
        light_grid,
        floor_height_grid,
        ceiling_height_grid,
        route_start,
        converted_exits,
        converted_doors,
        converted_lifts,
    )
    start_chunk = (start_cell_y // args.chunk_size) * chunk_cols + (start_cell_x // args.chunk_size)
    start_x -= (start_chunk % chunk_cols) * args.chunk_size
    start_y -= (start_chunk // chunk_cols) * args.chunk_size
    write_outputs(
        args.out,
        args.chunk_source,
        os.path.basename(args.iwad),
        args.map,
        origin_x,
        origin_y,
        args.cell_units,
        args.chunk_size,
        chunk_cols,
        chunk_rows,
        start_chunk,
        start_x,
        start_y,
        player.angle,
        grid,
        tex_grid,
        tex_phase_grid,
        floor_grid,
        damage_grid,
        light_grid,
        floor_height_grid,
        ceiling_height_grid,
        lift_grid,
        converted_things,
        converted_exits,
        converted_doors,
        converted_lifts,
        converted_lift_triggers,
    )
    if args.preview:
        write_preview(args.preview, grid, converted_things, args.chunk_size)
    solid_cells = sum(1 for row in grid for cell in row if cell)
    print(
        f"{args.map.upper()}: {width}x{height} fixed cells, "
        f"{chunk_cols}x{chunk_rows} chunks of {args.chunk_size}x{args.chunk_size}, "
        f"{solid_cells} solid cells, {len(converted_things)} things, "
        f"{len(converted_exits)} exits, {len(converted_doors)} doors, "
        f"{len(converted_lifts)} lifts/{len(converted_lift_triggers)} triggers, "
        f"{route_repair_cells} route cells -> {args.out}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", required=True)
    parser.add_argument("--zip-member")
    parser.add_argument("--map", default="E1M1")
    parser.add_argument("--skill-mask", type=lambda value: int(value, 0), default=4)
    parser.add_argument("--chunk-size", type=int, default=16)
    parser.add_argument("--cell-units", type=int, default=64, help="Doom map units represented by one chunk cell")
    parser.add_argument("--out", default="build/doom_chunks_generated.h")
    parser.add_argument("--chunk-source", default="build/doom_chunks_generated.c")
    parser.add_argument("--preview", help="Optional ASCII chunk map preview")
    parser.add_argument(
        "--start-local-x",
        type=float,
        default=8.5,
        help="Preferred player start X inside the active 16x16 runtime chunk",
    )
    parser.add_argument(
        "--start-local-y",
        type=float,
        default=8.5,
        help="Preferred player start Y inside the active 16x16 runtime chunk",
    )
    parser.add_argument(
        "--keep-wad-start-offset",
        action="store_true",
        help="Keep the exact WAD-derived fractional player start instead of centering in the open start area",
    )
    args = parser.parse_args()
    if args.chunk_size <= 0:
        parser.error("--chunk-size must be positive")
    if args.cell_units <= 0:
        parser.error("--cell-units must be positive")
    convert(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"doom_chunk_convert.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
