#!/usr/bin/env python3
"""Check generated 16x16 chunk-map start-to-exit connectivity."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from collections import deque
from pathlib import Path


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


def parse_lift_cells(text: str) -> list[tuple[int, int]]:
    count = parse_define_int(text, "DOOM_CHUNK_LIFT_CELL_REF_COUNT")
    if count <= 0:
        return []
    body = array_initializer(text, "g_chunk_lift_cells")
    refs = [int(item) for item in re.findall(r"\d+", body)]
    grid_w = parse_define_int(text, "DOOM_CHUNK_GRID_W")
    return [(ref % grid_w, ref // grid_w) for ref in refs]


def neighbors(x: int, y: int) -> tuple[tuple[int, int], ...]:
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


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
    path_doors = [cell for cell in interactive_path if cell in {(x, y) for x, y, _special in doors}]
    path_lifts = [cell for cell in interactive_path if cell in set(lift_cells)]
    print(
        f"{label} chunk route OK: start={start} exit={interactive_path[-1]} "
        f"steps={len(interactive_path) - 1} open_route={'yes' if open_path else 'no'} "
        f"doors={len(path_doors)} lifts={len(path_lifts)} chunks={chunk_count} "
        f"open_forward={open_forward}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
