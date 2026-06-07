#!/usr/bin/env python3
"""Check generated 16x16 chunk maps for visible gameplay actor coverage."""

from __future__ import annotations

import argparse
import ast
import os
import re
import sys
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
import doom_convert as dc  # noqa: E402


KEY_DOOR_SPECIALS = {26, 27, 28, 32, 33, 34}
KEY_TYPES = {5, 6, 13, 38, 39, 40}
WEAPON_TYPES = {2001, 2002, 2003, 2004, 2005, 2006}
DROP_SOURCE_TYPES = {3004, 9, 65}
CORPSE_SOURCE_TYPES = {3004, 9, 3001, 3002, 58, 3003, 69, 3005}
DEFAULT_REQUIRED_CATEGORIES = ("monster", "pickup", "weapon", "corpse-source", "drop-source")


@dataclass(frozen=True)
class ChunkThing:
    x_q8: int
    y_q8: int
    thing_type: int
    flags: int
    thing_class: int
    info: int
    chunk: int


def parse_define_int(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(-?\d+)\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return int(match.group(1))


def parse_define_string(text: str, name: str) -> str:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+\"([^\"]*)\"", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return match.group(1)


def array_initializer(text: str, symbol: str) -> str:
    match = re.search(rf"const\s+[^\n=]+\s+{re.escape(symbol)}\s*\[[^\n=]+\]\s*=", text)
    if not match:
        raise ValueError(f"missing {symbol}")
    start = text.index("{", match.end())
    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise ValueError(f"unterminated {symbol}")


def parse_u8_chunks(text: str, symbol: str) -> list[list[int]]:
    return ast.literal_eval(array_initializer(text, symbol).replace("{", "[").replace("}", "]"))


def parse_int_array(text: str, symbol: str) -> list[int]:
    return [int(item) for item in re.findall(r"-?\d+", array_initializer(text, symbol))]


def parse_things(text: str) -> list[ChunkThing]:
    body = array_initializer(text, "g_chunk_things")
    return [
        ChunkThing(*(int(group) for group in match.groups()))
        for match in re.finditer(r"\{(-?\d+),(-?\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\}", body)
    ]


def parse_doors(text: str) -> list[tuple[int, int, int, int]]:
    body = array_initializer(text, "g_chunk_doors")
    return [
        (int(match.group(1)), int(match.group(2)), int(match.group(3)), int(match.group(4)))
        for match in re.finditer(r"\{(\d+),(\d+),(\d+),(\d+)\}", body)
    ]


def parse_lift_cells(text: str) -> list[tuple[int, int]]:
    count = parse_define_int(text, "DOOM_CHUNK_LIFT_CELL_REF_COUNT")
    if count <= 0:
        return []
    grid_w = parse_define_int(text, "DOOM_CHUNK_GRID_W")
    return [(ref % grid_w, ref // grid_w) for ref in parse_int_array(text, "g_chunk_lift_cells")[:count]]


def build_global_grid(chunks: list[list[int]], chunk_cols: int, chunk_size: int) -> list[list[int]]:
    chunk_rows = (len(chunks) + chunk_cols - 1) // chunk_cols
    width = chunk_cols * chunk_size
    height = chunk_rows * chunk_size
    grid = [[1 for _x in range(width)] for _y in range(height)]
    for chunk_index, cells in enumerate(chunks):
        chunk_x = chunk_index % chunk_cols
        chunk_y = chunk_index // chunk_cols
        for local_y in range(chunk_size):
            for local_x in range(chunk_size):
                grid[chunk_y * chunk_size + local_y][chunk_x * chunk_size + local_x] = cells[
                    local_y * chunk_size + local_x
                ]
    return grid


def neighbors(x: int, y: int) -> tuple[tuple[int, int], ...]:
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


def reachable_cells(
    grid: list[list[int]],
    start: tuple[int, int],
    passable_overrides: set[tuple[int, int]],
) -> set[tuple[int, int]]:
    width = len(grid[0])
    height = len(grid)
    if start[0] < 0 or start[1] < 0 or start[0] >= width or start[1] >= height:
        return set()
    if grid[start[1]][start[0]] and start not in passable_overrides:
        return set()
    queue: deque[tuple[int, int]] = deque([start])
    seen = {start}
    while queue:
        x, y = queue.popleft()
        for nx, ny in neighbors(x, y):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            if (nx, ny) in seen:
                continue
            if grid[ny][nx] and (nx, ny) not in passable_overrides:
                continue
            seen.add((nx, ny))
            queue.append((nx, ny))
    return seen


def thing_categories(thing: ChunkThing) -> set[str]:
    categories: set[str] = set()
    if thing.thing_class == dc.THING_CLASS_MONSTER:
        categories.add("monster")
    elif thing.thing_class == dc.THING_CLASS_THREAT:
        categories.add("threat")
    elif thing.thing_class == dc.THING_CLASS_PICKUP:
        categories.add("pickup")
    elif thing.thing_class == dc.THING_CLASS_CORPSE:
        categories.add("corpse")

    if thing.thing_type in KEY_TYPES:
        categories.add("key")
    if thing.thing_type in WEAPON_TYPES:
        categories.add("weapon")
    if thing.thing_type in DROP_SOURCE_TYPES:
        categories.add("drop-source")
    if thing.thing_type in CORPSE_SOURCE_TYPES:
        categories.add("corpse-source")
    return categories


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", default="build/doom_chunks_generated.h")
    parser.add_argument("--source", default="build/doom_chunks_generated.c")
    parser.add_argument("--label")
    parser.add_argument(
        "--require-categories",
        default=",".join(DEFAULT_REQUIRED_CATEGORIES),
        help="Comma-separated categories required somewhere in reachable chunk things",
    )
    parser.add_argument(
        "--require-key-if-key-door",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require a key thing when generated chunk doors include key-locked specials",
    )
    parser.add_argument(
        "--min-reachable-chunk-things",
        type=int,
        default=1,
        help="Minimum rendered gameplay things required in each reachable non-trivial chunk",
    )
    parser.add_argument(
        "--min-reachable-open-cells",
        type=int,
        default=1,
        help="Only apply the per-chunk minimum to reachable chunks with at least this many reachable cells",
    )
    parser.add_argument(
        "--check-thing-cells",
        action="store_true",
        help="Also fail when generated things are placed on blocked, interactive, or unreachable cells",
    )
    parser.add_argument(
        "--runtime-thing-cap",
        type=int,
        default=0,
        help="Fail when any active 3x3 chunk window exceeds this runtime thing slot count",
    )
    args = parser.parse_args()

    text = Path(args.header).read_text(encoding="ascii")
    text += "\n" + Path(args.source).read_text(encoding="ascii")
    label = args.label or parse_define_string(text, "DOOM_CHUNK_MAP_NAME")
    chunk_size = parse_define_int(text, "DOOM_CHUNK_SIZE")
    chunk_cols = parse_define_int(text, "DOOM_CHUNK_COLS")
    chunk_count = parse_define_int(text, "DOOM_CHUNK_COUNT")
    thing_count = parse_define_int(text, "DOOM_CHUNK_THING_COUNT")
    max_active_define = parse_define_int(text, "DOOM_CHUNK_MAX_ACTIVE_THINGS")
    start_chunk = parse_define_int(text, "DOOM_CHUNK_START_CHUNK")
    start_local = (
        parse_define_int(text, "DOOM_CHUNK_START_X_Q8") >> 8,
        parse_define_int(text, "DOOM_CHUNK_START_Y_Q8") >> 8,
    )
    start = (
        (start_chunk % chunk_cols) * chunk_size + start_local[0],
        (start_chunk // chunk_cols) * chunk_size + start_local[1],
    )

    chunks = parse_u8_chunks(text, "g_chunk_solid")
    if len(chunks) != chunk_count:
        raise ValueError(f"g_chunk_solid has {len(chunks)} chunks, expected {chunk_count}")
    grid = build_global_grid(chunks, chunk_cols, chunk_size)
    doors = parse_doors(text)
    lift_cells = parse_lift_cells(text)
    passable_overrides = {(x, y) for x, y, _special, _chunk in doors}
    passable_overrides.update(lift_cells)
    reachable = reachable_cells(grid, start, passable_overrides)
    reachable_chunks = {((y // chunk_size) * chunk_cols + (x // chunk_size)) for x, y in reachable}
    reachable_open_by_chunk = Counter(
        ((y // chunk_size) * chunk_cols + (x // chunk_size)) for x, y in reachable
    )

    thing_first = parse_int_array(text, "g_chunk_thing_first")
    thing_counts = parse_int_array(text, "g_chunk_thing_count")
    things = parse_things(text)
    if len(things) != thing_count:
        raise ValueError(f"g_chunk_things has {len(things)} things, expected {thing_count}")
    if len(thing_first) != chunk_count or len(thing_counts) != chunk_count:
        raise ValueError("chunk thing first/count arrays do not match DOOM_CHUNK_COUNT")

    thing_cells: dict[int, list[tuple[ChunkThing, int, int]]] = {chunk: [] for chunk in range(chunk_count)}
    errors: list[str] = []
    category_counts: Counter[str] = Counter()
    for chunk in range(chunk_count):
        first = thing_first[chunk]
        count = thing_counts[chunk]
        if first + count > len(things):
            errors.append(f"chunk {chunk}: thing range {first}+{count} exceeds {len(things)}")
            continue
        for thing in things[first : first + count]:
            local_cell_x = thing.x_q8 >> 8
            local_cell_y = thing.y_q8 >> 8
            cell_x = (chunk % chunk_cols) * chunk_size + local_cell_x
            cell_y = (chunk // chunk_cols) * chunk_size + local_cell_y
            if thing.chunk != chunk:
                errors.append(f"chunk {chunk}: thing type {thing.thing_type} has embedded chunk {thing.chunk}")
            if local_cell_x < 0 or local_cell_y < 0 or local_cell_x >= chunk_size or local_cell_y >= chunk_size:
                errors.append(
                    f"chunk {chunk}: thing type {thing.thing_type} local cell {(local_cell_x, local_cell_y)} "
                    f"outside {chunk_size}x{chunk_size}"
                )
            if cell_x < 0 or cell_y < 0 or cell_y >= len(grid) or cell_x >= len(grid[0]):
                errors.append(f"chunk {chunk}: thing type {thing.thing_type} out of bounds at {(cell_x, cell_y)}")
                continue
            if args.check_thing_cells:
                if grid[cell_y][cell_x]:
                    errors.append(
                        f"chunk {chunk}: thing type {thing.thing_type} is on blocked cell {(cell_x, cell_y)} "
                        f"value={grid[cell_y][cell_x]}"
                    )
                if (cell_x, cell_y) in passable_overrides:
                    errors.append(f"chunk {chunk}: thing type {thing.thing_type} is on interactive cell {(cell_x, cell_y)}")
                if (cell_x, cell_y) not in reachable:
                    errors.append(f"chunk {chunk}: thing type {thing.thing_type} at {(cell_x, cell_y)} is unreachable")
            thing_cells[chunk].append((thing, cell_x, cell_y))
            for category in thing_categories(thing):
                category_counts[category] += 1

    required = {item.strip() for item in args.require_categories.split(",") if item.strip()}
    if args.require_key_if_key_door and any(special in KEY_DOOR_SPECIALS for _x, _y, special, _chunk in doors):
        required.add("key")
    for category in sorted(required):
        if category_counts[category] == 0:
            errors.append(f"missing required category: {category}")

    if args.min_reachable_chunk_things > 0:
        for chunk in sorted(reachable_chunks):
            reachable_open = reachable_open_by_chunk[chunk]
            if reachable_open < args.min_reachable_open_cells:
                continue
            if not thing_cells[chunk]:
                continue
            rendered = sum(1 for thing, _x, _y in thing_cells[chunk] if thing.info & dc.THING_INFO_RENDER)
            if rendered < args.min_reachable_chunk_things:
                errors.append(
                    f"chunk {chunk}: reachable open cells={reachable_open} rendered things={rendered} "
                    f"expected>={args.min_reachable_chunk_things}"
                )

    max_window_things = 0
    max_window_chunk = 0
    for chunk in range(chunk_count):
        chunk_x = chunk % chunk_cols
        chunk_y = chunk // chunk_cols
        window_things = 0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx = chunk_x + dx
                ny = chunk_y + dy
                if nx < 0 or ny < 0:
                    continue
                neighbor = ny * chunk_cols + nx
                if neighbor >= chunk_count:
                    continue
                window_things += thing_counts[neighbor]
        if window_things > max_window_things:
            max_window_things = window_things
            max_window_chunk = chunk

    if args.runtime_thing_cap and max_window_things > args.runtime_thing_cap:
        errors.append(
            f"max 3x3 thing window {max_window_things} at chunk {max_window_chunk} "
            f"exceeds runtime cap {args.runtime_thing_cap}"
        )
    if max_active_define < max_window_things:
        errors.append(
            f"DOOM_CHUNK_MAX_ACTIVE_THINGS={max_active_define} is below max 3x3 thing window "
            f"{max_window_things} at chunk {max_window_chunk}"
        )

    summary = " ".join(f"{name}={category_counts[name]}" for name in sorted(category_counts))
    print(
        f"{label} chunk visibility: chunks={chunk_count} reachable={len(reachable_chunks)} "
        f"things={len(things)} max_3x3_window={max_window_things}@{max_window_chunk} "
        f"active_cap={max_active_define} "
        f"categories: {summary}"
    )
    for chunk in range(chunk_count):
        rendered = sum(1 for thing, _x, _y in thing_cells[chunk] if thing.info & dc.THING_INFO_RENDER)
        if rendered or chunk in reachable_chunks:
            print(
                f"  chunk {chunk}: reachable_cells={reachable_open_by_chunk[chunk]} "
                f"rendered_things={rendered} total_things={len(thing_cells[chunk])}"
            )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
