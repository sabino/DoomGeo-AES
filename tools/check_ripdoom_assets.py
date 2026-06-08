#!/usr/bin/env python3
"""Sanity-check generated RIPDOOM-lite geometry tables."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def macro_value(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(-?0x[0-9a-fA-F]+|-?\d+)\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing macro {name}")
    return int(match.group(1), 0)


def array_body(text: str, typename: str, name: str) -> str:
    match = re.search(
        rf"const\s+{re.escape(typename)}\s+{re.escape(name)}\[[^\]]+\]\s*=\s*\{{\n(.*?)\n?\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing array {name}")
    return match.group(1)


def row_count(body: str) -> int:
    return len(re.findall(r"^\s*\{", body, re.MULTILINE))


def parse_seg_rows(body: str) -> list[tuple[int, int, int, int, int, int]]:
    rows = []
    pattern = re.compile(
        r"^\s*\{(\d+),(\d+),(-?\d+),(-?\d+),(-?\d+),0x([0-9a-fA-F]+),",
        re.MULTILINE,
    )
    for match in pattern.finditer(body):
        rows.append(
            (
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
                int(match.group(4)),
                int(match.group(5)),
                int(match.group(6), 16),
            )
        )
    return rows


def parse_subsector_rows(body: str) -> list[tuple[int, int, int]]:
    rows = []
    pattern = re.compile(r"^\s*\{(\d+),(\d+),(-?\d+)\},\s*$", re.MULTILINE)
    for match in pattern.finditer(body):
        rows.append((int(match.group(1)), int(match.group(2)), int(match.group(3))))
    return rows


def parse_line_seg_spans(body: str) -> list[tuple[int, int]]:
    rows = []
    pattern = re.compile(r"^\s*\{(\d+),(\d+)\},\s*$", re.MULTILINE)
    for match in pattern.finditer(body):
        rows.append((int(match.group(1)), int(match.group(2))))
    return rows


def parse_node_rows(body: str) -> list[tuple[list[int], int, int]]:
    rows = []
    pattern = re.compile(
        r"^\s*\{-?\d+,-?\d+,-?\d+,-?\d+,\{([^}]*)\},\{(\d+),(\d+)\}\},\s*$",
        re.MULTILINE,
    )
    for match in pattern.finditer(body):
        bbox = [int(item) for item in match.group(1).split(",") if item]
        rows.append((bbox, int(match.group(2)), int(match.group(3))))
    return rows


def scalar_count(body: str) -> int:
    return len(re.findall(r"0x[0-9a-fA-F]+|-?\d+", body))


def parse_scalar_values(body: str) -> list[int]:
    return [int(match.group(0), 0) for match in re.finditer(r"0x[0-9a-fA-F]+|-?\d+", body)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", default="build/doom_ripdoom_generated.h")
    parser.add_argument("--source", default="build/doom_ripdoom_generated.c")
    args = parser.parse_args()

    header = Path(args.header).read_text(encoding="ascii")
    source = Path(args.source).read_text(encoding="ascii")

    vertex_count = macro_value(header, "NG_RIP_VERTEX_COUNT")
    line_count = macro_value(header, "NG_RIP_LINE_COUNT")
    side_count = macro_value(header, "NG_RIP_SIDE_COUNT")
    sector_count = macro_value(header, "NG_RIP_SECTOR_COUNT")
    seg_count = macro_value(header, "NG_RIP_SEG_COUNT")
    line_seg_index_count = macro_value(header, "NG_RIP_LINE_SEG_INDEX_COUNT")
    subsector_candidate_index_count = macro_value(header, "NG_RIP_SUBSECTOR_CANDIDATE_INDEX_COUNT")
    subsector_count = macro_value(header, "NG_RIP_SUBSECTOR_COUNT")
    node_count = macro_value(header, "NG_RIP_NODE_COUNT")
    thing_count = macro_value(header, "NG_RIP_THING_COUNT")
    reject_size = macro_value(header, "NG_RIP_REJECT_SIZE")
    blockmap_words = macro_value(header, "NG_RIP_BLOCKMAP_WORD_COUNT")
    blockmap_w = macro_value(header, "NG_RIP_BLOCKMAP_W")
    blockmap_h = macro_value(header, "NG_RIP_BLOCKMAP_H")

    errors: list[str] = []
    typed_counts = (
        ("NgRipVertex", "g_rip_vertices", vertex_count),
        ("NgRipLine", "g_rip_lines", line_count),
        ("NgRipSide", "g_rip_sides", side_count),
        ("NgRipSector", "g_rip_sectors", sector_count),
        ("NgRipThing", "g_rip_things", thing_count),
    )
    for typename, name, expected in typed_counts:
        actual = row_count(array_body(source, typename, name))
        if actual != expected:
            errors.append(f"{name} row count {actual} != {expected}")

    segs = parse_seg_rows(array_body(source, "NgRipSeg", "g_rip_segs"))
    if len(segs) != seg_count:
        errors.append(f"g_rip_segs parsed row count {len(segs)} != {seg_count}")
    for i, (v1, v2, linedef, front_sector, back_sector, _flags) in enumerate(segs):
        if v1 >= vertex_count or v2 >= vertex_count:
            errors.append(f"seg {i} references missing vertex")
            break
        if linedef < 0 or linedef >= line_count:
            errors.append(f"seg {i} references missing linedef {linedef}")
            break
        if front_sector >= sector_count or back_sector >= sector_count:
            errors.append(f"seg {i} references missing sector front={front_sector} back={back_sector}")
            break

    line_seg_spans = parse_line_seg_spans(array_body(source, "NgRipLineSegSpan", "g_rip_line_seg_spans"))
    if len(line_seg_spans) != line_count:
        errors.append(f"g_rip_line_seg_spans parsed row count {len(line_seg_spans)} != {line_count}")
    line_seg_indices = parse_scalar_values(array_body(source, "uint16_t", "g_rip_line_seg_indices"))
    if len(line_seg_indices) != line_seg_index_count:
        errors.append(f"g_rip_line_seg_indices scalar count {len(line_seg_indices)} != {line_seg_index_count}")
    if line_seg_index_count != seg_count:
        errors.append(f"line seg index count {line_seg_index_count} != seg count {seg_count}")
    if not errors:
        for line_index, (firstseg, numsegs) in enumerate(line_seg_spans):
            if firstseg + numsegs > line_seg_index_count:
                errors.append(f"line {line_index} seg span {firstseg}+{numsegs} exceeds {line_seg_index_count}")
                break
            for offset in range(numsegs):
                seg_index = line_seg_indices[firstseg + offset]
                if seg_index >= seg_count:
                    errors.append(f"line {line_index} references missing seg {seg_index}")
                    break
                if segs[seg_index][2] != line_index:
                    errors.append(f"line {line_index} span includes seg {seg_index} for line {segs[seg_index][2]}")
                    break
            if errors:
                break

    subsector_candidate_spans = parse_line_seg_spans(array_body(source, "NgRipSubsectorCandidateSpan", "g_rip_subsector_candidate_spans"))
    if len(subsector_candidate_spans) != subsector_count:
        errors.append(f"g_rip_subsector_candidate_spans parsed row count {len(subsector_candidate_spans)} != {subsector_count}")
    subsector_candidate_indices = parse_scalar_values(array_body(source, "uint16_t", "g_rip_subsector_candidate_indices"))
    if len(subsector_candidate_indices) != subsector_candidate_index_count:
        errors.append(f"g_rip_subsector_candidate_indices scalar count {len(subsector_candidate_indices)} != {subsector_candidate_index_count}")
    if not errors:
        for subsector_index, (firstseg, numsegs) in enumerate(subsector_candidate_spans):
            if firstseg + numsegs > subsector_candidate_index_count:
                errors.append(f"subsector {subsector_index} candidate span {firstseg}+{numsegs} exceeds {subsector_candidate_index_count}")
                break
            for offset in range(numsegs):
                seg_index = subsector_candidate_indices[firstseg + offset]
                if seg_index >= seg_count:
                    errors.append(f"subsector {subsector_index} candidate references missing seg {seg_index}")
                    break
            if errors:
                break

    subsectors = parse_subsector_rows(array_body(source, "NgRipSubsector", "g_rip_subsectors"))
    if len(subsectors) != subsector_count:
        errors.append(f"g_rip_subsectors parsed row count {len(subsectors)} != {subsector_count}")
    for i, (numsegs, firstseg, sector) in enumerate(subsectors):
        if firstseg + numsegs > seg_count:
            errors.append(f"subsector {i} seg range {firstseg}+{numsegs} exceeds {seg_count}")
            break
        if sector >= sector_count:
            errors.append(f"subsector {i} references missing sector {sector}")
            break

    nodes = parse_node_rows(array_body(source, "NgRipNode", "g_rip_nodes"))
    if len(nodes) != node_count:
        errors.append(f"g_rip_nodes parsed row count {len(nodes)} != {node_count}")
    for i, (bbox, child0, child1) in enumerate(nodes):
        if len(bbox) != 8:
            errors.append(f"node {i} bbox value count {len(bbox)} != 8")
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

    reject_actual = scalar_count(array_body(source, "uint8_t", "g_rip_reject"))
    if reject_actual != reject_size:
        errors.append(f"g_rip_reject scalar count {reject_actual} != {reject_size}")
    blockmap_actual = scalar_count(array_body(source, "int16_t", "g_rip_blockmap_words"))
    if blockmap_actual != blockmap_words:
        errors.append(f"g_rip_blockmap_words scalar count {blockmap_actual} != {blockmap_words}")
    if blockmap_w <= 0 or blockmap_h <= 0:
        errors.append(f"invalid blockmap dimensions {blockmap_w}x{blockmap_h}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    solid = sum(1 for *_rest, flags in segs if flags & macro_value(header, "NG_RIP_SEG_SOLID"))
    lower = sum(1 for *_rest, flags in segs if flags & macro_value(header, "NG_RIP_SEG_LOWER"))
    upper = sum(1 for *_rest, flags in segs if flags & macro_value(header, "NG_RIP_SEG_UPPER"))
    print(
        f"RIPDOOM-lite assets OK: vertices={vertex_count} sectors={sector_count} "
        f"segs={seg_count} solid={solid} lower={lower} upper={upper} "
        f"subsectors={subsector_count} candidates={subsector_candidate_index_count} "
        f"nodes={node_count} things={thing_count} "
        f"blockmap={blockmap_w}x{blockmap_h}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
