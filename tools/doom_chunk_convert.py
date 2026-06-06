#!/usr/bin/env python3
"""Convert a Doom WAD map into fixed-scale 16x16 Neo Geo map chunks."""

from __future__ import annotations

import argparse
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


def build_bounds(vertices: list[dc.Vertex], chunk_size: int, cell_units: int) -> tuple[int, int, int, int, int, int]:
    min_x = min(v.x for v in vertices)
    max_x = max(v.x for v in vertices)
    min_y = min(v.y for v in vertices)
    max_y = max(v.y for v in vertices)
    page_units = chunk_size * cell_units
    origin_x = snap_floor(min_x, page_units)
    origin_y = snap_ceil(max_y, page_units)
    end_x = snap_ceil(max_x, page_units)
    end_y = snap_floor(min_y, page_units)
    width = max(chunk_size, ceil_div(end_x - origin_x, cell_units))
    height = max(chunk_size, ceil_div(origin_y - end_y, cell_units))
    return origin_x, origin_y, width, height, min_x, max_y


def build_base_grids(
    vertices: list[dc.Vertex],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    cell_units: int,
) -> tuple[list[list[int]], list[list[int]], list[list[int]]]:
    grid = [[0 for _ in range(width)] for _ in range(height)]
    tex_grid = [[0 for _ in range(width)] for _ in range(height)]
    priority_grid = [[0 for _ in range(width)] for _ in range(height)]

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
        for cell_x, cell_y in dc.line_cells(grid, x0, y0, x1, y1):
            grid[cell_y][cell_x] = 1
            if texture_priority >= priority_grid[cell_y][cell_x]:
                tex_grid[cell_y][cell_x] = texture_class
                priority_grid[cell_y][cell_x] = texture_priority
    return grid, tex_grid, priority_grid


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
        rows.append(
            ChunkThing(
                int(round(gx * 256)),
                int(round(gy * 256)),
                thing.type,
                thing.flags,
                dc.runtime_thing_class(thing.type),
                dc.runtime_thing_info(thing.type),
                chunk,
            )
        )
    rows.sort(key=lambda thing: (thing.chunk, thing.y_q8, thing.x_q8, thing.type))
    return rows


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
    grid: list[list[int]],
    tex_grid: list[list[int]],
    floor_grid: list[list[int]],
    damage_grid: list[list[int]],
    light_grid: list[list[int]],
    floor_height_grid: list[list[int]],
    ceiling_height_grid: list[list[int]],
    things: list[ChunkThing],
) -> None:
    chunk_count = chunk_cols * chunk_rows
    chunk_first: list[int] = []
    chunk_count_things: list[int] = []
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
        f.write(f"#define DOOM_CHUNK_THING_COUNT {len(things)}\n\n")
        f.write("typedef struct NgChunkThing {\n")
        f.write("    short x_q8;\n")
        f.write("    short y_q8;\n")
        f.write("    unsigned short type;\n")
        f.write("    unsigned short flags;\n")
        f.write("    unsigned char thing_class;\n")
        f.write("    unsigned char info;\n")
        f.write("    unsigned short chunk;\n")
        f.write("} NgChunkThing;\n\n")
        f.write("extern const unsigned char g_chunk_solid[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_tex[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_floor_visual[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_damage[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned char g_chunk_light[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const short g_chunk_floor_height[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const short g_chunk_ceiling_height[DOOM_CHUNK_COUNT][DOOM_CHUNK_CELLS];\n")
        f.write("extern const unsigned short g_chunk_thing_first[DOOM_CHUNK_COUNT];\n")
        f.write("extern const unsigned char g_chunk_thing_count[DOOM_CHUNK_COUNT];\n")
        f.write("extern const NgChunkThing g_chunk_things[DOOM_CHUNK_THING_COUNT ? DOOM_CHUNK_THING_COUNT : 1];\n\n")
        f.write("#endif /* DOOM_CHUNKS_GENERATED_H */\n")

    with open(source_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_chunk_convert.py; do not edit by hand. */\n")
        f.write('#include "doom_chunks_generated.h"\n\n')
        write_u8_chunks(f, "g_chunk_solid", grid, chunk_count, chunk_size, chunk_cols)
        write_u8_chunks(f, "g_chunk_tex", tex_grid, chunk_count, chunk_size, chunk_cols)
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


def write_preview(
    preview_path: str,
    grid: list[list[int]],
    things: list[ChunkThing],
    chunk_size: int,
) -> None:
    thing_cells: dict[tuple[int, int], str] = {}
    for thing in things:
        cell = (thing.x_q8 >> 8, thing.y_q8 >> 8)
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
    _wad, vertices, linedefs, sidedefs, sectors, things = parse_map(args.iwad, args.zip_member, args.map)
    origin_x, origin_y, width, height, _min_x, _max_y = build_bounds(vertices, args.chunk_size, args.cell_units)
    chunk_cols = ceil_div(width, args.chunk_size)
    chunk_rows = ceil_div(height, args.chunk_size)
    width = chunk_cols * args.chunk_size
    height = chunk_rows * args.chunk_size
    grid, tex_grid, _priority_grid = build_base_grids(
        vertices, linedefs, sidedefs, sectors, origin_x, origin_y, width, height, args.cell_units
    )
    floor_grid, damage_grid, light_grid, floor_height_grid, ceiling_height_grid = build_sector_grids(
        grid, linedefs, sidedefs, sectors, vertices, origin_x, origin_y, args.cell_units
    )
    converted_things = build_things(
        things, origin_x, origin_y, width, height, args.cell_units,
        args.chunk_size, chunk_cols, args.skill_mask
    )
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
        grid,
        tex_grid,
        floor_grid,
        damage_grid,
        light_grid,
        floor_height_grid,
        ceiling_height_grid,
        converted_things,
    )
    if args.preview:
        write_preview(args.preview, grid, converted_things, args.chunk_size)
    solid_cells = sum(1 for row in grid for cell in row if cell)
    print(
        f"{args.map.upper()}: {width}x{height} fixed cells, "
        f"{chunk_cols}x{chunk_rows} chunks of {args.chunk_size}x{args.chunk_size}, "
        f"{solid_cells} solid cells, {len(converted_things)} things -> {args.out}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", required=True)
    parser.add_argument("--zip-member")
    parser.add_argument("--map", default="E1M1")
    parser.add_argument("--skill-mask", type=lambda value: int(value, 0), default=4)
    parser.add_argument("--chunk-size", type=int, default=16)
    parser.add_argument("--cell-units", type=int, default=128, help="Doom map units represented by one chunk cell")
    parser.add_argument("--out", default="build/doom_chunks_generated.h")
    parser.add_argument("--chunk-source", default="build/doom_chunks_generated.c")
    parser.add_argument("--preview", help="Optional ASCII chunk map preview")
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
