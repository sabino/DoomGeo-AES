#!/usr/bin/env python3
"""Check generated 16x16 chunk-map start-to-exit connectivity."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from collections import deque
from pathlib import Path

KEY_BLUE = 0x01
KEY_RED = 0x02
KEY_YELLOW = 0x04
KEY_DOOR_MASKS = {
    26: KEY_BLUE,
    32: KEY_BLUE,
    28: KEY_RED,
    33: KEY_RED,
    27: KEY_YELLOW,
    34: KEY_YELLOW,
}
KEY_THING_MASKS = {
    5: KEY_BLUE,
    40: KEY_BLUE,
    13: KEY_RED,
    38: KEY_RED,
    6: KEY_YELLOW,
    39: KEY_YELLOW,
}
PLAYER_RADIUS_Q8 = 51


def parse_define_int(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(-?\d+)\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return int(match.group(1))


def parse_define_float(text: str, name: str) -> float:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(-?\d+(?:\.\d+)?)\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return float(match.group(1))


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
    body = array_initializer(text, symbol)
    return ast.literal_eval(body.replace("{", "[").replace("}", "]"))


def parse_int_array(text: str, symbol: str) -> list[int]:
    return [int(item) for item in re.findall(r"-?\d+", array_initializer(text, symbol))]


def parse_exits(text: str) -> list[tuple[int, int, int]]:
    body = array_initializer(text, "g_chunk_exits")
    return [
        (int(match.group(1)) >> 8, int(match.group(2)) >> 8, int(match.group(3)))
        for match in re.finditer(r"\{(-?\d+),(-?\d+),(\d+),\d+,\d+,\d+\}", body)
    ]


def parse_doors(text: str) -> list[tuple[int, int, int]]:
    body = array_initializer(text, "g_chunk_doors")
    return [
        (int(match.group(1)), int(match.group(2)), int(match.group(3)))
        for match in re.finditer(r"\{(\d+),(\d+),(\d+),\d+\}", body)
    ]


def parse_things(text: str) -> list[tuple[int, int, int, int]]:
    body = array_initializer(text, "g_chunk_things")
    return [
        (
            int(match.group(1)),
            int(match.group(2)),
            int(match.group(3)),
            int(match.group(7)),
        )
        for match in re.finditer(r"\{(-?\d+),(-?\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\}", body)
    ]


def parse_lift_cells(text: str) -> list[tuple[int, int]]:
    count = parse_define_int(text, "DOOM_CHUNK_LIFT_CELL_REF_COUNT")
    if count <= 0:
        return []
    body = array_initializer(text, "g_chunk_lift_cells")
    refs = [int(item) for item in re.findall(r"\d+", body)]
    grid_w = parse_define_int(text, "DOOM_CHUNK_GRID_W")
    return [(ref % grid_w, ref // grid_w) for ref in refs]


def parse_lift_triggers(text: str) -> list[tuple[int, int, int, int, int]]:
    count = parse_define_int(text, "DOOM_CHUNK_LIFT_TRIGGER_COUNT")
    if count <= 0:
        return []
    body = array_initializer(text, "g_chunk_lift_triggers")
    rows = [
        (
            int(match.group(1)),
            int(match.group(2)),
            int(match.group(3)),
            int(match.group(4)),
            int(match.group(5)),
        )
        for match in re.finditer(r"\{(\d+),(\d+),(\d+),(\d+),(\d+)\}", body)
    ]
    return rows[:count]


def neighbors(x: int, y: int) -> tuple[tuple[int, int], ...]:
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


def floor_q8_cell(value_q8: int) -> int:
    return value_q8 // 256


def cell_blocked(
    grid: list[list[int]],
    x: int,
    y: int,
    passable_overrides: set[tuple[int, int]],
) -> bool:
    width = len(grid[0])
    height = len(grid)
    if x < 0 or y < 0 or x >= width or y >= height:
        return True
    return bool(grid[y][x] and (x, y) not in passable_overrides)


def can_player_occupy_q8(
    grid: list[list[int]],
    x_q8: int,
    y_q8: int,
    passable_overrides: set[tuple[int, int]],
) -> bool:
    cx = floor_q8_cell(x_q8)
    cy = floor_q8_cell(y_q8)
    return not (
        cell_blocked(grid, cx, cy, passable_overrides)
        or cell_blocked(grid, floor_q8_cell(x_q8 - PLAYER_RADIUS_Q8), cy, passable_overrides)
        or cell_blocked(grid, floor_q8_cell(x_q8 + PLAYER_RADIUS_Q8), cy, passable_overrides)
        or cell_blocked(grid, cx, floor_q8_cell(y_q8 - PLAYER_RADIUS_Q8), passable_overrides)
        or cell_blocked(grid, cx, floor_q8_cell(y_q8 + PLAYER_RADIUS_Q8), passable_overrides)
    )


def cell_center_q8(cell: tuple[int, int]) -> tuple[int, int]:
    return cell[0] * 256 + 128, cell[1] * 256 + 128


def validate_player_path(
    grid: list[list[int]],
    path: list[tuple[int, int]],
    passable_overrides: set[tuple[int, int]],
) -> str | None:
    for index, cell in enumerate(path):
        x_q8, y_q8 = cell_center_q8(cell)
        if not can_player_occupy_q8(grid, x_q8, y_q8, passable_overrides):
            return f"path cell {index} {cell} cannot fit player radius"
        if index == 0:
            continue
        prev_x_q8, prev_y_q8 = cell_center_q8(path[index - 1])
        mid_x_q8 = (prev_x_q8 + x_q8) // 2
        mid_y_q8 = (prev_y_q8 + y_q8) // 2
        if not can_player_occupy_q8(grid, mid_x_q8, mid_y_q8, passable_overrides):
            return f"path edge {index - 1}->{index} {path[index - 1]}->{cell} cannot fit player radius"
    return None


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


def bfs(
    grid: list[list[int]],
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    passable_overrides: set[tuple[int, int]],
) -> list[tuple[int, int]] | None:
    width = len(grid[0])
    height = len(grid)
    queue: deque[tuple[int, int]] = deque([start])
    prev: dict[tuple[int, int], tuple[int, int] | None] = {start: None}
    while queue:
        x, y = queue.popleft()
        if (x, y) in targets:
            path: list[tuple[int, int]] = []
            cur: tuple[int, int] | None = (x, y)
            while cur is not None:
                path.append(cur)
                cur = prev[cur]
            return list(reversed(path))
        for nx, ny in neighbors(x, y):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            if (nx, ny) in prev:
                continue
            if grid[ny][nx] and (nx, ny) not in passable_overrides:
                continue
            prev[(nx, ny)] = (x, y)
            queue.append((nx, ny))
    return None


def key_aware_bfs(
    grid: list[list[int]],
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    normal_interactive: set[tuple[int, int]],
    locked_doors: dict[tuple[int, int], int],
    keys: dict[tuple[int, int], int],
) -> tuple[list[tuple[int, int]], int] | None:
    width = len(grid[0])
    height = len(grid)
    start_state = (start[0], start[1], keys.get(start, 0))
    queue: deque[tuple[int, int, int]] = deque([start_state])
    prev: dict[tuple[int, int, int], tuple[int, int, int] | None] = {start_state: None}
    while queue:
        x, y, owned_keys = queue.popleft()
        if (x, y) in targets:
            path: list[tuple[int, int]] = []
            cur: tuple[int, int, int] | None = (x, y, owned_keys)
            while cur is not None:
                path.append((cur[0], cur[1]))
                cur = prev[cur]
            return list(reversed(path)), owned_keys
        for nx, ny in neighbors(x, y):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            cell = (nx, ny)
            required_key = locked_doors.get(cell, 0)
            if required_key and (owned_keys & required_key) == 0:
                continue
            if grid[ny][nx] and cell not in normal_interactive and not required_key:
                continue
            next_keys = owned_keys | keys.get(cell, 0)
            next_state = (nx, ny, next_keys)
            if next_state in prev:
                continue
            prev[next_state] = (x, y, owned_keys)
            queue.append(next_state)
    return None


def collect_cell_state(
    cell: tuple[int, int],
    owned_keys: int,
    opened_lifts: int,
    keys: dict[tuple[int, int], int],
    lift_triggers: dict[tuple[int, int], int],
) -> tuple[int, int]:
    owned_keys |= keys.get(cell, 0)
    x, y = cell
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            opened_lifts |= lift_triggers.get((x + dx, y + dy), 0)
    return owned_keys, opened_lifts


def key_lift_aware_bfs(
    grid: list[list[int]],
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    normal_interactive: set[tuple[int, int]],
    locked_doors: dict[tuple[int, int], int],
    lift_cells: dict[tuple[int, int], int],
    keys: dict[tuple[int, int], int],
    lift_triggers: dict[tuple[int, int], int],
) -> tuple[list[tuple[int, int]], int, int] | None:
    width = len(grid[0])
    height = len(grid)
    start_keys, start_lifts = collect_cell_state(start, 0, 0, keys, lift_triggers)
    start_state = (start[0], start[1], start_keys, start_lifts)
    queue: deque[tuple[int, int, int, int]] = deque([start_state])
    prev: dict[tuple[int, int, int, int], tuple[int, int, int, int] | None] = {start_state: None}
    while queue:
        x, y, owned_keys, opened_lifts = queue.popleft()
        if (x, y) in targets:
            path: list[tuple[int, int]] = []
            cur: tuple[int, int, int, int] | None = (x, y, owned_keys, opened_lifts)
            while cur is not None:
                path.append((cur[0], cur[1]))
                cur = prev[cur]
            return list(reversed(path)), owned_keys, opened_lifts
        for nx, ny in neighbors(x, y):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            cell = (nx, ny)
            required_key = locked_doors.get(cell, 0)
            if required_key and (owned_keys & required_key) == 0:
                continue
            required_lift = lift_cells.get(cell, 0)
            if required_lift and (opened_lifts & required_lift) == 0:
                continue
            if grid[ny][nx] and cell not in normal_interactive and not required_key and not required_lift:
                continue
            next_keys, next_lifts = collect_cell_state(cell, owned_keys, opened_lifts, keys, lift_triggers)
            next_state = (nx, ny, next_keys, next_lifts)
            if next_state in prev:
                continue
            prev[next_state] = (x, y, owned_keys, opened_lifts)
            queue.append(next_state)
    return None


def validate_key_player_path(
    grid: list[list[int]],
    path: list[tuple[int, int]],
    normal_interactive: set[tuple[int, int]],
    locked_doors: dict[tuple[int, int], int],
    keys: dict[tuple[int, int], int],
) -> str | None:
    owned_keys = 0
    for index, cell in enumerate(path):
        required_key = locked_doors.get(cell, 0)
        if required_key and (owned_keys & required_key) == 0:
            return f"key route cell {index} {cell} crosses locked door without key mask 0x{required_key:x}"
        passable = set(normal_interactive)
        for door_cell, key_mask in locked_doors.items():
            if owned_keys & key_mask:
                passable.add(door_cell)
        if not can_player_occupy_q8(grid, *cell_center_q8(cell), passable):
            return f"key route cell {index} {cell} cannot fit player radius with keys 0x{owned_keys:x}"
        if index:
            prev_x_q8, prev_y_q8 = cell_center_q8(path[index - 1])
            cell_x_q8, cell_y_q8 = cell_center_q8(cell)
            mid_x_q8 = (prev_x_q8 + cell_x_q8) // 2
            mid_y_q8 = (prev_y_q8 + cell_y_q8) // 2
            if not can_player_occupy_q8(grid, mid_x_q8, mid_y_q8, passable):
                return (
                    f"key route edge {index - 1}->{index} {path[index - 1]}->{cell} "
                    f"cannot fit player radius with keys 0x{owned_keys:x}"
                )
        owned_keys |= keys.get(cell, 0)
    return None


def validate_key_lift_player_path(
    grid: list[list[int]],
    path: list[tuple[int, int]],
    normal_interactive: set[tuple[int, int]],
    locked_doors: dict[tuple[int, int], int],
    lift_cells: dict[tuple[int, int], int],
    keys: dict[tuple[int, int], int],
    lift_triggers: dict[tuple[int, int], int],
) -> str | None:
    owned_keys = 0
    opened_lifts = 0
    for index, cell in enumerate(path):
        required_key = locked_doors.get(cell, 0)
        if required_key and (owned_keys & required_key) == 0:
            return f"state route cell {index} {cell} crosses locked door without key mask 0x{required_key:x}"
        required_lift = lift_cells.get(cell, 0)
        if required_lift and (opened_lifts & required_lift) == 0:
            return f"state route cell {index} {cell} crosses unopened lift mask 0x{required_lift:x}"
        passable = set(normal_interactive)
        for door_cell, key_mask in locked_doors.items():
            if owned_keys & key_mask:
                passable.add(door_cell)
        for lift_cell, lift_mask in lift_cells.items():
            if opened_lifts & lift_mask:
                passable.add(lift_cell)
        if not can_player_occupy_q8(grid, *cell_center_q8(cell), passable):
            return (
                f"state route cell {index} {cell} cannot fit player radius "
                f"with keys 0x{owned_keys:x} lifts 0x{opened_lifts:x}"
            )
        if index:
            prev_x_q8, prev_y_q8 = cell_center_q8(path[index - 1])
            cell_x_q8, cell_y_q8 = cell_center_q8(cell)
            mid_x_q8 = (prev_x_q8 + cell_x_q8) // 2
            mid_y_q8 = (prev_y_q8 + cell_y_q8) // 2
            if not can_player_occupy_q8(grid, mid_x_q8, mid_y_q8, passable):
                return (
                    f"state route edge {index - 1}->{index} {path[index - 1]}->{cell} "
                    f"cannot fit player radius with keys 0x{owned_keys:x} lifts 0x{opened_lifts:x}"
                )
        owned_keys, opened_lifts = collect_cell_state(cell, owned_keys, opened_lifts, keys, lift_triggers)
    return None


def forward_open_cells(
    grid: list[list[int]],
    start_x: float,
    start_y: float,
    dir_x: float,
    dir_y: float,
    passable_overrides: set[tuple[int, int]],
) -> int:
    width = len(grid[0])
    height = len(grid)
    open_cells = 0
    visited: set[tuple[int, int]] = set()
    for step in (0.50, 0.95, 1.40, 1.85, 2.30, 2.75, 3.20):
        x = int(start_x + dir_x * step)
        y = int(start_y + dir_y * step)
        if x < 0 or y < 0 or x >= width or y >= height:
            break
        if grid[y][x] and (x, y) not in passable_overrides:
            break
        if (x, y) not in visited:
            visited.add((x, y))
            open_cells += 1
    return open_cells


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", default="build/doom_chunks_generated.h")
    parser.add_argument("--source", default="build/doom_chunks_generated.c")
    parser.add_argument("--label")
    parser.add_argument(
        "--check-locked-key-route",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require a start-to-exit route that collects matching keys before locked doors",
    )
    parser.add_argument(
        "--check-player-radius-route",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require accepted chunk routes to fit the runtime player collision radius",
    )
    args = parser.parse_args()

    text = Path(args.header).read_text(encoding="ascii")
    text += "\n" + Path(args.source).read_text(encoding="ascii")
    label = args.label or parse_define_string(text, "DOOM_CHUNK_MAP_NAME")
    chunk_size = parse_define_int(text, "DOOM_CHUNK_SIZE")
    chunk_cols = parse_define_int(text, "DOOM_CHUNK_COLS")
    chunk_count = parse_define_int(text, "DOOM_CHUNK_COUNT")
    start_chunk = parse_define_int(text, "DOOM_CHUNK_START_CHUNK")
    start_local = (
        parse_define_int(text, "DOOM_CHUNK_START_X_Q8") >> 8,
        parse_define_int(text, "DOOM_CHUNK_START_Y_Q8") >> 8,
    )
    start_local_q8 = (
        parse_define_int(text, "DOOM_CHUNK_START_X_Q8"),
        parse_define_int(text, "DOOM_CHUNK_START_Y_Q8"),
    )
    start = (
        (start_chunk % chunk_cols) * chunk_size + start_local[0],
        (start_chunk // chunk_cols) * chunk_size + start_local[1],
    )
    chunks = parse_u8_chunks(text, "g_chunk_solid")
    if len(chunks) != chunk_count:
        raise ValueError(f"g_chunk_solid has {len(chunks)} chunks, expected {chunk_count}")
    grid = build_global_grid(chunks, chunk_cols, chunk_size)
    exits = parse_exits(text)
    if not exits:
        print(f"{label}: no chunk exits", file=sys.stderr)
        return 1
    targets = {(x, y) for x, y, _special in exits}
    doors = parse_doors(text)
    lift_cells = parse_lift_cells(text)
    interactive = {(x, y) for x, y, _special in doors}
    interactive.update(lift_cells)
    normal_interactive = set(lift_cells)
    normal_doors: set[tuple[int, int]] = set()
    locked_doors: dict[tuple[int, int], int] = {}
    for x, y, special in doors:
        key_mask = KEY_DOOR_MASKS.get(special, 0)
        if key_mask:
            locked_doors[(x, y)] = key_mask
        else:
            normal_interactive.add((x, y))
            normal_doors.add((x, y))
    lift_grid = build_global_grid(parse_u8_chunks(text, "g_chunk_lift_cell"), chunk_cols, chunk_size)
    lift_cell_masks: dict[tuple[int, int], int] = {}
    for y, row in enumerate(lift_grid):
        for x, lift_id in enumerate(row):
            if lift_id:
                lift_cell_masks[(x, y)] = 1 << (lift_id - 1)
    lift_trigger_masks: dict[tuple[int, int], int] = {}
    for x, y, lift_index, _special, _walk in parse_lift_triggers(text):
        if lift_index < 0:
            continue
        lift_trigger_masks[(x, y)] = lift_trigger_masks.get((x, y), 0) | (1 << lift_index)
    thing_count = parse_define_int(text, "DOOM_CHUNK_THING_COUNT")
    thing_first = parse_int_array(text, "g_chunk_thing_first")
    thing_counts = parse_int_array(text, "g_chunk_thing_count")
    things = parse_things(text)
    if len(things) != thing_count:
        raise ValueError(f"g_chunk_things has {len(things)} things, expected {thing_count}")
    if len(thing_first) != chunk_count or len(thing_counts) != chunk_count:
        raise ValueError("chunk thing first/count arrays do not match DOOM_CHUNK_COUNT")
    keys: dict[tuple[int, int], int] = {}
    for chunk in range(chunk_count):
        first = thing_first[chunk]
        count = thing_counts[chunk]
        if first + count > len(things):
            raise ValueError(f"chunk {chunk}: thing range {first}+{count} exceeds {len(things)}")
        for x_q8, y_q8, thing_type, embedded_chunk in things[first : first + count]:
            if embedded_chunk != chunk:
                raise ValueError(f"chunk {chunk}: thing type {thing_type} has embedded chunk {embedded_chunk}")
            key_mask = KEY_THING_MASKS.get(thing_type, 0)
            if not key_mask:
                continue
            cell = (
                (chunk % chunk_cols) * chunk_size + (x_q8 >> 8),
                (chunk // chunk_cols) * chunk_size + (y_q8 >> 8),
            )
            keys[cell] = keys.get(cell, 0) | key_mask

    errors: list[str] = []
    width = len(grid[0])
    height = len(grid)
    if not (0 <= start[0] < width and 0 <= start[1] < height):
        errors.append(f"start out of bounds: {start}")
    elif grid[start[1]][start[0]]:
        errors.append(f"start blocked: {start} value={grid[start[1]][start[0]]}")
    for exit_cell in sorted(targets):
        x, y = exit_cell
        if not (0 <= x < width and 0 <= y < height):
            errors.append(f"exit out of bounds: {exit_cell}")
        elif grid[y][x] and exit_cell not in interactive:
            errors.append(f"exit blocked: {exit_cell} value={grid[y][x]}")
    start_float = (
        (start_chunk % chunk_cols) * chunk_size + (start_local_q8[0] / 256.0),
        (start_chunk // chunk_cols) * chunk_size + (start_local_q8[1] / 256.0),
    )
    dir_x = parse_define_float(text, "DOOM_CHUNK_START_DIR_X")
    dir_y = parse_define_float(text, "DOOM_CHUNK_START_DIR_Y")
    open_forward = forward_open_cells(grid, start_float[0], start_float[1], dir_x, dir_y, interactive)
    if open_forward < 2:
        errors.append(
            f"start view blocked: start=({start_float[0]:.2f},{start_float[1]:.2f}) "
            f"dir=({dir_x:.3f},{dir_y:.3f}) open_forward={open_forward}"
        )
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    open_path = bfs(grid, start, targets, set())
    interactive_path = bfs(grid, start, targets, interactive)
    if interactive_path is None:
        print(
            f"{label}: no chunk route from start {start} to exits {sorted(targets)} "
            f"even after opening doors/lifts",
            file=sys.stderr,
        )
        return 1
    if args.check_player_radius_route:
        error = validate_player_path(grid, interactive_path, interactive)
        if error:
            print(f"{label}: player-radius route failed: {error}", file=sys.stderr)
            return 1
    key_route = None
    key_route_keys = 0
    if args.check_locked_key_route:
        result = key_aware_bfs(grid, start, targets, normal_interactive, locked_doors, keys)
        if result is None:
            locked_summary = ", ".join(
                f"{cell}:0x{mask:x}" for cell, mask in sorted(locked_doors.items())
            ) or "none"
            key_summary = ", ".join(f"{cell}:0x{mask:x}" for cell, mask in sorted(keys.items())) or "none"
            print(
                f"{label}: no key-valid chunk route from start {start} to exits {sorted(targets)} "
                f"locked_doors=[{locked_summary}] keys=[{key_summary}]",
                file=sys.stderr,
            )
            return 1
        key_route, key_route_keys = result
        if args.check_player_radius_route:
            error = validate_key_player_path(grid, key_route, normal_interactive, locked_doors, keys)
            if error:
                print(f"{label}: player-radius key route failed: {error}", file=sys.stderr)
                return 1
    path_doors = [cell for cell in interactive_path if cell in {(x, y) for x, y, _special in doors}]
    path_lifts = [cell for cell in interactive_path if cell in set(lift_cells)]
    if key_route is None:
        key_route = interactive_path
    state_route_result = key_lift_aware_bfs(
        grid,
        start,
        targets,
        normal_doors,
        locked_doors,
        lift_cell_masks,
        keys,
        lift_trigger_masks,
    )
    if state_route_result is None:
        lift_summary = ", ".join(f"{cell}:0x{mask:x}" for cell, mask in sorted(lift_cell_masks.items())) or "none"
        trigger_summary = ", ".join(f"{cell}:0x{mask:x}" for cell, mask in sorted(lift_trigger_masks.items())) or "none"
        print(
            f"{label}: no key/lift-valid chunk route from start {start} to exits {sorted(targets)} "
            f"lift_cells=[{lift_summary}] lift_triggers=[{trigger_summary}]",
            file=sys.stderr,
        )
        return 1
    state_route, state_route_keys, state_route_lifts = state_route_result
    if args.check_player_radius_route:
        error = validate_key_lift_player_path(
            grid,
            state_route,
            normal_doors,
            locked_doors,
            lift_cell_masks,
            keys,
            lift_trigger_masks,
        )
        if error:
            print(f"{label}: player-radius key/lift route failed: {error}", file=sys.stderr)
            return 1
    key_path_doors = [cell for cell in key_route if cell in {(x, y) for x, y, _special in doors}]
    locked_path_doors = [cell for cell in key_route if cell in locked_doors]
    state_path_lifts = [cell for cell in state_route if cell in lift_cell_masks]
    print(
        f"{label} chunk route OK: start={start} exit={interactive_path[-1]} "
        f"steps={len(interactive_path) - 1} open_route={'yes' if open_path else 'no'} "
        f"doors={len(path_doors)} lifts={len(path_lifts)} chunks={chunk_count} "
        f"open_forward={open_forward} key_route_steps={len(key_route) - 1} "
        f"key_route_doors={len(key_path_doors)} locked_doors={len(locked_path_doors)} "
        f"keys=0x{key_route_keys:x} state_route_steps={len(state_route) - 1} "
        f"state_lifts={len(state_path_lifts)} state_keys=0x{state_route_keys:x} "
        f"state_lift_mask=0x{state_route_lifts:x} "
        f"player_radius={'yes' if args.check_player_radius_route else 'no'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
