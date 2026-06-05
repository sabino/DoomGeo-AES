#!/usr/bin/env python3
"""Report generated route coverage for Doom Episode 1 maps."""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from collections import deque
from pathlib import Path


DEFAULT_MAPS = tuple(f"E1M{i}" for i in range(1, 10))


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
    for match in re.finditer(r"\{(-?\d+),(-?\d+),(\d+)(?:,\d+,\d+)?\}", text[start:end]):
        x_q8, y_q8, _special = match.groups()
        exits.append((int(x_q8) >> 8, int(y_q8) >> 8))
    return exits


def parse_runtime_thing_types(text: str) -> list[int]:
    marker = "static const NgRuntimeThing g_runtime_things"
    start = text.find(marker)
    if start < 0:
        raise ValueError("missing g_runtime_things")
    end = text.index("};", start)
    return [int(match.group(3)) for match in re.finditer(r"\{(-?\d+),(-?\d+),(\d+),0x[0-9a-fA-F]+\}", text[start:end])]


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


def route_status(header: Path, map_name: str) -> tuple[str, str]:
    text = header.read_text(encoding="ascii")
    grid = parse_grid(text, "g_map")
    start = (int(parse_define_number(text, "DOOM_START_X")), int(parse_define_number(text, "DOOM_START_Y")))
    exits = parse_exits(text)
    if not exits:
        if map_name.upper() == "E1M8":
            boss_count = sum(1 for thing_type in parse_runtime_thing_types(text) if thing_type == 3003)
            if boss_count:
                return "boss_exit", f"{boss_count} Baron boss things; runtime boss-death completion"
            return "boss_exit_missing", "no linedef exit and no Baron boss things"
        return "missing_exit", "no generated linedef exit"
    if grid[start[1]][start[0]] != 0:
        return "blocked_start", f"start={start} value={grid[start[1]][start[0]]}"
    for exit_cell in exits:
        if grid[exit_cell[1]][exit_cell[0]] != 0:
            return "blocked_exit", f"exit={exit_cell} value={grid[exit_cell[1]][exit_cell[0]]}"
    targets = set(exits)
    open_path = bfs(grid, start, targets, allow_doors=False)
    door_path = bfs(grid, start, targets, allow_doors=True)
    if door_path is None:
        return "blocked_route", f"start={start} exits={sorted(targets)}"
    door_cells = [(x, y, grid[y][x] - 2) for x, y in door_path if grid[y][x] >= 2]
    if door_cells:
        return "route_doors", (
            f"start={start} exit={door_path[-1]} steps={len(door_path) - 1} "
            f"doors={len(door_cells)} open_route={'yes' if open_path else 'no'}"
        )
    return "route_open", f"start={start} exit={door_path[-1]} steps={len(door_path) - 1}"


def convert_map(args: argparse.Namespace, map_name: str, header: Path) -> None:
    header.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            "tools/doom_convert.py",
            "--iwad",
            args.iwad,
            "--map",
            map_name,
            "--skill-mask",
            str(args.skill_mask),
            "--width",
            str(args.width),
            "--height",
            str(args.height),
            "--out",
            str(header),
            "--assets-header",
            str(header.with_name("doom_assets_generated.h")),
            "--assets-source",
            str(header.with_name("doom_assets_generated.c")),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iwad", default=".tools/assets/doom1.wad.zip")
    parser.add_argument("--maps", default=",".join(DEFAULT_MAPS))
    parser.add_argument("--width", type=int, default=76)
    parser.add_argument("--height", type=int, default=54)
    parser.add_argument("--skill-mask", type=int, default=4)
    parser.add_argument("--build-dir", default="build/episode-route")
    parser.add_argument("--strict", action="store_true", help="Fail if any map does not have a generated route")
    args = parser.parse_args()

    maps = [item.strip().upper() for item in args.maps.split(",") if item.strip()]
    failures = 0
    for map_name in maps:
        header = Path(args.build_dir) / map_name / "doom_map_generated.h"
        try:
            convert_map(args, map_name, header)
            status, detail = route_status(header, map_name)
        except subprocess.CalledProcessError as exc:
            status = "convert_failed"
            detail = (exc.stdout or "").strip().splitlines()[-1] if exc.stdout else str(exc)
        except Exception as exc:
            status = "route_error"
            detail = str(exc)
        if status not in {"route_open", "route_doors", "boss_exit"}:
            failures += 1
        print(f"{map_name:4} {status:18} {detail}")

    if args.strict and failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
