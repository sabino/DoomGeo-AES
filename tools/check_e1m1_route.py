#!/usr/bin/env python3
"""Check generated E1M1 start-to-exit route connectivity."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from collections import deque
from pathlib import Path


def parse_define_number(text: str, name: str) -> float:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+([-0-9.]+)", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    return float(match.group(1))


def parse_grid(text: str, symbol: str) -> list[list[int]]:
    marker = f"static const unsigned char {symbol}"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"missing {symbol}")
    start = text.index("{", start)
    end = text.index("};", start) + 1
    return ast.literal_eval(text[start:end].replace("{", "[").replace("}", "]"))


def parse_exits(text: str) -> list[tuple[int, int]]:
    marker = "static const NgRuntimeExit g_runtime_exits"
    start = text.find(marker)
    if start < 0:
        raise ValueError("missing g_runtime_exits")
    end = text.index("};", start)
    exits: list[tuple[int, int]] = []
    for x_q8, y_q8, _special in re.findall(r"\{(-?\d+),(-?\d+),(\d+)\}", text[start:end]):
        exits.append((int(x_q8) >> 8, int(y_q8) >> 8))
    if not exits:
        raise ValueError("no exits parsed")
    return exits


def neighbors(x: int, y: int) -> tuple[tuple[int, int], ...]:
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


def bfs(
    grid: list[list[int]],
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    allow_doors: bool,
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
            cell = grid[ny][nx]
            if cell and not (allow_doors and cell >= 2):
                continue
            prev[(nx, ny)] = (x, y)
            queue.append((nx, ny))
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", default="build/doom_map_generated.h", help="Generated map header to inspect")
    args = parser.parse_args()

    header = Path(args.header)
    text = header.read_text(encoding="ascii")
    grid = parse_grid(text, "g_map")
    start = (int(parse_define_number(text, "DOOM_START_X")), int(parse_define_number(text, "DOOM_START_Y")))
    exits = parse_exits(text)
    targets = set(exits)

    errors: list[str] = []
    if grid[start[1]][start[0]] != 0:
        errors.append(f"start cell blocked: {start} value={grid[start[1]][start[0]]}")
    for exit_cell in exits:
        if grid[exit_cell[1]][exit_cell[0]] != 0:
            errors.append(f"exit cell blocked: {exit_cell} value={grid[exit_cell[1]][exit_cell[0]]}")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    open_path = bfs(grid, start, targets, allow_doors=False)
    door_path = bfs(grid, start, targets, allow_doors=True)
    if door_path is None:
        print(f"{header}: no generated route from start {start} to exits {sorted(targets)} even with doors", file=sys.stderr)
        return 1

    door_cells = [(x, y, grid[y][x] - 2) for x, y in door_path if grid[y][x] >= 2]
    if not door_cells:
        print(
            f"E1M1 route OK: start={start} exit={door_path[-1]} steps={len(door_path) - 1} doors=0 open_route=yes"
        )
        return 0

    print(
        "E1M1 route OK: "
        f"start={start} exit={door_path[-1]} steps={len(door_path) - 1} "
        f"doors={len(door_cells)} open_route={'yes' if open_path else 'no'} "
        f"door_cells={door_cells}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
