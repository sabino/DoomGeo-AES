#!/usr/bin/env python3
"""Sanity-check generated BSP data converted into raycaster grid space."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def macro_value(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(\d+)\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing macro {name}")
    return int(match.group(1))


def array_body(text: str, typename: str, name: str) -> str:
    match = re.search(
        rf"const\s+{re.escape(typename)}\s+{re.escape(name)}\[[^\]]+\]\s*=\s*\{{\n(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing array {name}")
    return match.group(1)


def parse_vertices(body: str) -> list[tuple[int, int]]:
    rows = []
    for match in re.finditer(r"^\s*\{(-?\d+),(-?\d+)\},\s*$", body, re.MULTILINE):
        rows.append((int(match.group(1)), int(match.group(2))))
    return rows


def parse_nodes(body: str) -> list[tuple[int, int, int, int, list[int], int, int]]:
    rows = []
    pattern = re.compile(
        r"^\s*\{(-?\d+),(-?\d+),(-?\d+),(-?\d+),\{([^}]*)\},\{(\d+),(\d+)\}\},\s*$",
        re.MULTILINE,
    )
    for match in pattern.finditer(body):
        bbox = [int(item) for item in match.group(5).split(",") if item]
        rows.append(
            (
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
                int(match.group(4)),
                bbox,
                int(match.group(6)),
                int(match.group(7)),
            )
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map-header", default="build/doom_map_generated.h")
    parser.add_argument("--assets-header", default="build/doom_assets_generated.h")
    parser.add_argument("--assets-source", default="build/doom_assets_generated.c")
    args = parser.parse_args()

    map_header = Path(args.map_header).read_text(encoding="ascii")
    assets_header = Path(args.assets_header).read_text(encoding="ascii")
    assets_source = Path(args.assets_source).read_text(encoding="ascii")

    map_w = macro_value(map_header, "MAP_W")
    map_h = macro_value(map_header, "MAP_H")
    vertex_count = macro_value(assets_header, "NG_VERTEX_COUNT")
    node_count = macro_value(assets_header, "NG_NODE_COUNT")
    subsector_count = macro_value(assets_header, "NG_SUBSECTOR_COUNT")

    vertices = parse_vertices(array_body(assets_source, "NgBspVertex", "g_ng_bsp_vertices"))
    nodes = parse_nodes(array_body(assets_source, "NgBspNode", "g_ng_bsp_nodes"))

    errors: list[str] = []
    if len(vertices) != vertex_count:
        errors.append(f"g_ng_bsp_vertices count {len(vertices)} != NG_VERTEX_COUNT {vertex_count}")
    if len(nodes) != node_count:
        errors.append(f"g_ng_bsp_nodes count {len(nodes)} != NG_NODE_COUNT {node_count}")

    max_x_q8 = map_w * 256
    max_y_q8 = map_h * 256
    for i, (x_q8, y_q8) in enumerate(vertices):
        if x_q8 < 0 or x_q8 > max_x_q8 or y_q8 < 0 or y_q8 > max_y_q8:
            errors.append(f"vertex {i} out of grid q8 bounds: {x_q8},{y_q8}")
            break

    for i, (x_q8, y_q8, dx_q8, dy_q8, bbox, child0, child1) in enumerate(nodes):
        if not dx_q8 and not dy_q8:
            errors.append(f"node {i} has degenerate partition vector")
            break
        if x_q8 < 0 or x_q8 > max_x_q8 or y_q8 < 0 or y_q8 > max_y_q8:
            errors.append(f"node {i} origin out of grid q8 bounds: {x_q8},{y_q8}")
            break
        if len(bbox) != 8:
            errors.append(f"node {i} bbox has {len(bbox)} values")
            break
        for value in bbox:
            if value < 0 or value > max(max_x_q8, max_y_q8):
                errors.append(f"node {i} bbox value out of grid q8 bounds: {value}")
                break
        for child in (child0, child1):
            child_index = child & 0x7FFF
            if child & 0x8000:
                if child_index >= subsector_count:
                    errors.append(f"node {i} subsector child {child_index} >= {subsector_count}")
                    break
            elif child_index >= node_count:
                errors.append(f"node {i} node child {child_index} >= {node_count}")
                break
        if errors:
            break

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(
        f"BSP assets OK: vertices={len(vertices)} nodes={len(nodes)} "
        f"subsectors={subsector_count} grid={map_w}x{map_h}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
