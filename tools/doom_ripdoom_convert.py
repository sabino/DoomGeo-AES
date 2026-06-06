#!/usr/bin/env python3
"""Emit a compact RIPDOOM-style geometry package from a Doom WAD map.

This is a build-time bridge between native Doom geometry and the Neo Geo
sprite-strip renderer.  It keeps real Doom BSP/SEGS/SSECTORS/BLOCKMAP data in a
compact C form with explicit limits, but it does not change the runtime renderer
yet.
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import doom_convert as dc  # noqa: E402


MAX_RIP_VERTICES = 1056
MAX_RIP_LINEDEFS = 1118
MAX_RIP_SIDEDEFS = 1536
MAX_RIP_SECTORS = 205
MAX_RIP_SEGS = 1536
MAX_RIP_SUBSECTORS = 512
MAX_RIP_NODES = 512
MAX_RIP_THINGS = 384
MAX_RIP_BLOCKMAP_WORDS = 8192

SEG_ONE_SIDED = 0x0001
SEG_TWO_SIDED = 0x0002
SEG_BLOCKING = 0x0004
SEG_SOLID = 0x0008
SEG_PASSABLE = 0x0010
SEG_DOOR = 0x0020
SEG_LOWER = 0x0040
SEG_UPPER = 0x0080
SEG_MID = 0x0100


def side_index(value: int) -> int:
    return -1 if value == 0xFFFF else value


def clean_texture(name: str) -> str:
    return "" if name == "-" else name


def texture_id(tex_ids: dict[str, int], name: str) -> int:
    return tex_ids.get(name, 0)


def front_back_sides(line: dc.LineDef, seg: dc.Seg) -> tuple[int, int]:
    if seg.side:
        return line.side_back, line.side_front
    return line.side_front, line.side_back


def sector_for_side(sidedefs: list[dc.SideDef], side: int) -> int:
    if side == 0xFFFF or side < 0 or side >= len(sidedefs):
        return -1
    return sidedefs[side].sector


def seg_flags(line: dc.LineDef, seg: dc.Seg, sidedefs: list[dc.SideDef], sectors: list[dc.Sector]) -> int:
    front_side_index, back_side_index = front_back_sides(line, seg)
    front_sector = sector_for_side(sidedefs, front_side_index)
    back_sector = sector_for_side(sidedefs, back_side_index)
    flags = 0
    if back_side_index == 0xFFFF or back_side_index < 0:
        flags |= SEG_ONE_SIDED
    else:
        flags |= SEG_TWO_SIDED
    if line.flags & 0x0001:
        flags |= SEG_BLOCKING
    if dc.is_solid_linedef(line, sidedefs, sectors):
        flags |= SEG_SOLID
    else:
        flags |= SEG_PASSABLE
    if line.special in dc.DOOR_SPECIALS:
        flags |= SEG_DOOR

    if front_sector >= 0 and back_sector >= 0 and front_sector < len(sectors) and back_sector < len(sectors):
        front = sectors[front_sector]
        back = sectors[back_sector]
        if front.floor_height != back.floor_height:
            flags |= SEG_LOWER
        if front.ceiling_height != back.ceiling_height:
            flags |= SEG_UPPER

    for side_index_value in (front_side_index, back_side_index):
        if side_index_value == 0xFFFF or side_index_value < 0 or side_index_value >= len(sidedefs):
            continue
        side = sidedefs[side_index_value]
        if clean_texture(side.mid_texture):
            flags |= SEG_MID
            break
    return flags


def seg_textures(line: dc.LineDef, seg: dc.Seg, sidedefs: list[dc.SideDef], tex_ids: dict[str, int]) -> tuple[int, int, int, int, int, int]:
    front_side_index, back_side_index = front_back_sides(line, seg)
    sides: list[dc.SideDef] = []
    for side_index_value in (front_side_index, back_side_index):
        if side_index_value != 0xFFFF and 0 <= side_index_value < len(sidedefs):
            sides.append(sidedefs[side_index_value])

    upper = next((clean_texture(side.top_texture) for side in sides if clean_texture(side.top_texture)), "")
    lower = next((clean_texture(side.bottom_texture) for side in sides if clean_texture(side.bottom_texture)), "")
    mid = next((clean_texture(side.mid_texture) for side in sides if clean_texture(side.mid_texture)), "")
    if not mid:
        mid = upper or lower or dc.solid_line_texture(line, sidedefs)
    upper_kind = 8 if line.special in dc.DOOR_SPECIALS else dc.wall_texture_class(upper)
    lower_kind = 8 if line.special in dc.DOOR_SPECIALS else dc.wall_texture_class(lower)
    mid_kind = 8 if line.special in dc.DOOR_SPECIALS else dc.wall_texture_class(mid)
    return (
        texture_id(tex_ids, upper),
        texture_id(tex_ids, lower),
        texture_id(tex_ids, mid),
        upper_kind,
        lower_kind,
        mid_kind,
    )


def subsector_sector(index: int, subsector: dc.Subsector, segs: list[dc.Seg], linedefs: list[dc.LineDef], sidedefs: list[dc.SideDef]) -> int:
    del index
    if subsector.numsegs == 0 or subsector.firstseg >= len(segs):
        return -1
    seg = segs[subsector.firstseg]
    if seg.linedef >= len(linedefs):
        return -1
    front_side_index, _back_side_index = front_back_sides(linedefs[seg.linedef], seg)
    return sector_for_side(sidedefs, front_side_index)


def check_limit(name: str, value: int, maximum: int) -> None:
    if value > maximum:
        raise ValueError(f"{name} count {value} exceeds RIPDOOM-lite limit {maximum}")


def validate_limits(
    vertices: list[dc.Vertex],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    segs: list[dc.Seg],
    subsectors: list[dc.Subsector],
    nodes: list[dc.Node],
    things: list[dc.Thing],
    blockmap: list[int],
) -> None:
    check_limit("vertex", len(vertices), MAX_RIP_VERTICES)
    check_limit("linedef", len(linedefs), MAX_RIP_LINEDEFS)
    check_limit("sidedef", len(sidedefs), MAX_RIP_SIDEDEFS)
    check_limit("sector", len(sectors), MAX_RIP_SECTORS)
    check_limit("seg", len(segs), MAX_RIP_SEGS)
    check_limit("subsector", len(subsectors), MAX_RIP_SUBSECTORS)
    check_limit("node", len(nodes), MAX_RIP_NODES)
    check_limit("thing", len(things), MAX_RIP_THINGS)
    check_limit("blockmap word", len(blockmap), MAX_RIP_BLOCKMAP_WORDS)

    for i, seg in enumerate(segs):
        if seg.v1 >= len(vertices) or seg.v2 >= len(vertices):
            raise ValueError(f"seg {i} references missing vertex")
        if seg.linedef >= len(linedefs):
            raise ValueError(f"seg {i} references missing linedef")
    for i, subsector in enumerate(subsectors):
        if subsector.firstseg + subsector.numsegs > len(segs):
            raise ValueError(f"subsector {i} seg range exceeds seg table")
    for i, node in enumerate(nodes):
        for child in (node.child0, node.child1):
            child_index = child & 0x7FFF
            if child & 0x8000:
                if child_index >= len(subsectors):
                    raise ValueError(f"node {i} references missing subsector child {child_index}")
            elif child_index >= len(nodes):
                raise ValueError(f"node {i} references missing node child {child_index}")
    if len(blockmap) < 4:
        raise ValueError("blockmap is too small")
    if blockmap[2] <= 0 or blockmap[3] <= 0:
        raise ValueError(f"invalid blockmap dimensions {blockmap[2]}x{blockmap[3]}")


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


def emit(
    header_path: str,
    source_path: str,
    report_path: str | None,
    source_name: str,
    map_name: str,
    vertices: list[dc.Vertex],
    linedefs: list[dc.LineDef],
    sidedefs: list[dc.SideDef],
    sectors: list[dc.Sector],
    segs: list[dc.Seg],
    subsectors: list[dc.Subsector],
    nodes: list[dc.Node],
    things: list[dc.Thing],
    reject: bytes,
    blockmap: list[int],
) -> None:
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    os.makedirs(os.path.dirname(source_path), exist_ok=True)
    if report_path:
        os.makedirs(os.path.dirname(report_path), exist_ok=True)

    tex_ids = dc.texture_ids(sidedefs, sectors)
    include_name = os.path.basename(header_path)

    seg_rows: list[str] = []
    segs_by_line: list[list[int]] = [[] for _ in linedefs]
    seg_flag_counts = {
        "solid": 0,
        "two_sided": 0,
        "passable": 0,
        "door": 0,
        "lower": 0,
        "upper": 0,
        "mid": 0,
    }
    for seg_index, seg in enumerate(segs):
        line = linedefs[seg.linedef]
        segs_by_line[seg.linedef].append(seg_index)
        front_side_index, back_side_index = front_back_sides(line, seg)
        front_sector = sector_for_side(sidedefs, front_side_index)
        back_sector = sector_for_side(sidedefs, back_side_index)
        flags = seg_flags(line, seg, sidedefs, sectors)
        upper, lower, mid, upper_kind, lower_kind, mid_kind = seg_textures(line, seg, sidedefs, tex_ids)
        seg_flag_counts["solid"] += 1 if flags & SEG_SOLID else 0
        seg_flag_counts["two_sided"] += 1 if flags & SEG_TWO_SIDED else 0
        seg_flag_counts["passable"] += 1 if flags & SEG_PASSABLE else 0
        seg_flag_counts["door"] += 1 if flags & SEG_DOOR else 0
        seg_flag_counts["lower"] += 1 if flags & SEG_LOWER else 0
        seg_flag_counts["upper"] += 1 if flags & SEG_UPPER else 0
        seg_flag_counts["mid"] += 1 if flags & SEG_MID else 0
        seg_rows.append(
            "{"
            f"{seg.v1},{seg.v2},{seg.linedef},{side_index(front_sector)},{side_index(back_sector)},"
            f"0x{flags:04x},{upper},{lower},{mid},{seg.offset},{seg.angle},"
            f"{upper_kind},{lower_kind},{mid_kind}"
            "}"
        )
    line_seg_indices = [seg_index for line_segs in segs_by_line for seg_index in line_segs]
    line_seg_spans: list[str] = []
    first_seg = 0
    for line_segs in segs_by_line:
        line_seg_spans.append(f"{{{first_seg},{len(line_segs)}}}")
        first_seg += len(line_segs)

    with open(header_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_ripdoom_convert.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_RIPDOOM_GENERATED_H\n#define DOOM_RIPDOOM_GENERATED_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define DOOM_RIPDOOM_SOURCE \"{source_name}\"\n")
        f.write(f"#define DOOM_RIPDOOM_MAP \"{map_name}\"\n")
        f.write(f"#define NG_RIP_VERTEX_COUNT {len(vertices)}\n")
        f.write(f"#define NG_RIP_LINE_COUNT {len(linedefs)}\n")
        f.write(f"#define NG_RIP_SIDE_COUNT {len(sidedefs)}\n")
        f.write(f"#define NG_RIP_SECTOR_COUNT {len(sectors)}\n")
        f.write(f"#define NG_RIP_SEG_COUNT {len(segs)}\n")
        f.write(f"#define NG_RIP_LINE_SEG_INDEX_COUNT {len(line_seg_indices)}\n")
        f.write(f"#define NG_RIP_SUBSECTOR_COUNT {len(subsectors)}\n")
        f.write(f"#define NG_RIP_NODE_COUNT {len(nodes)}\n")
        f.write(f"#define NG_RIP_THING_COUNT {len(things)}\n")
        f.write(f"#define NG_RIP_REJECT_SIZE {len(reject)}\n")
        f.write(f"#define NG_RIP_BLOCKMAP_WORD_COUNT {len(blockmap)}\n")
        f.write(f"#define NG_RIP_TEXTURE_ID_COUNT {len(tex_ids) - 1}\n")
        f.write(f"#define NG_RIP_BLOCKMAP_ORIGIN_X {blockmap[0]}\n")
        f.write(f"#define NG_RIP_BLOCKMAP_ORIGIN_Y {blockmap[1]}\n")
        f.write(f"#define NG_RIP_BLOCKMAP_W {blockmap[2]}\n")
        f.write(f"#define NG_RIP_BLOCKMAP_H {blockmap[3]}\n\n")
        f.write(f"#define NG_RIP_MAX_VERTICES {MAX_RIP_VERTICES}\n")
        f.write(f"#define NG_RIP_MAX_LINEDEFS {MAX_RIP_LINEDEFS}\n")
        f.write(f"#define NG_RIP_MAX_SIDEDEFS {MAX_RIP_SIDEDEFS}\n")
        f.write(f"#define NG_RIP_MAX_SECTORS {MAX_RIP_SECTORS}\n")
        f.write(f"#define NG_RIP_MAX_SEGS {MAX_RIP_SEGS}\n")
        f.write(f"#define NG_RIP_MAX_SUBSECTORS {MAX_RIP_SUBSECTORS}\n")
        f.write(f"#define NG_RIP_MAX_NODES {MAX_RIP_NODES}\n")
        f.write(f"#define NG_RIP_MAX_THINGS {MAX_RIP_THINGS}\n")
        f.write(f"#define NG_RIP_MAX_BLOCKMAP_WORDS {MAX_RIP_BLOCKMAP_WORDS}\n\n")
        f.write(f"#define NG_RIP_SEG_ONE_SIDED 0x{SEG_ONE_SIDED:04x}\n")
        f.write(f"#define NG_RIP_SEG_TWO_SIDED 0x{SEG_TWO_SIDED:04x}\n")
        f.write(f"#define NG_RIP_SEG_BLOCKING 0x{SEG_BLOCKING:04x}\n")
        f.write(f"#define NG_RIP_SEG_SOLID 0x{SEG_SOLID:04x}\n")
        f.write(f"#define NG_RIP_SEG_PASSABLE 0x{SEG_PASSABLE:04x}\n")
        f.write(f"#define NG_RIP_SEG_DOOR 0x{SEG_DOOR:04x}\n")
        f.write(f"#define NG_RIP_SEG_LOWER 0x{SEG_LOWER:04x}\n")
        f.write(f"#define NG_RIP_SEG_UPPER 0x{SEG_UPPER:04x}\n")
        f.write(f"#define NG_RIP_SEG_MID 0x{SEG_MID:04x}\n\n")
        f.write("typedef struct NgRipVertex { int16_t x; int16_t y; } NgRipVertex;\n")
        f.write("typedef struct NgRipLine { uint16_t v1; uint16_t v2; int16_t front_side; int16_t back_side; uint16_t flags; uint16_t special; uint16_t tag; } NgRipLine;\n")
        f.write("typedef struct NgRipSide { int16_t texture_x; int16_t texture_y; uint16_t top_texture; uint16_t bottom_texture; uint16_t mid_texture; uint16_t sector; } NgRipSide;\n")
        f.write("typedef struct NgRipSector { int16_t floor_height; int16_t ceiling_height; uint16_t floor_flat; uint16_t ceiling_flat; uint8_t light; uint8_t floor_visual; uint8_t damage; uint8_t special; uint16_t tag; } NgRipSector;\n")
        f.write("typedef struct NgRipSeg { uint16_t v1; uint16_t v2; int16_t linedef; int16_t front_sector; int16_t back_sector; uint16_t flags; uint16_t upper_texture; uint16_t lower_texture; uint16_t mid_texture; int16_t offset; int16_t angle; uint8_t upper_kind; uint8_t lower_kind; uint8_t mid_kind; } NgRipSeg;\n")
        f.write("typedef struct NgRipLineSegSpan { uint16_t firstseg; uint16_t numsegs; } NgRipLineSegSpan;\n")
        f.write("typedef struct NgRipSubsector { uint16_t numsegs; uint16_t firstseg; int16_t sector; } NgRipSubsector;\n")
        f.write("typedef struct NgRipNode { int16_t x; int16_t y; int16_t dx; int16_t dy; int16_t bbox[8]; uint16_t child[2]; } NgRipNode;\n")
        f.write("typedef struct NgRipThing { int16_t x; int16_t y; uint16_t angle; uint16_t type; uint16_t flags; uint8_t thing_class; uint8_t info; } NgRipThing;\n\n")
        f.write("extern const NgRipVertex g_rip_vertices[NG_RIP_VERTEX_COUNT];\n")
        f.write("extern const NgRipLine g_rip_lines[NG_RIP_LINE_COUNT];\n")
        f.write("extern const NgRipSide g_rip_sides[NG_RIP_SIDE_COUNT];\n")
        f.write("extern const NgRipSector g_rip_sectors[NG_RIP_SECTOR_COUNT];\n")
        f.write("extern const NgRipSeg g_rip_segs[NG_RIP_SEG_COUNT];\n")
        f.write("extern const NgRipLineSegSpan g_rip_line_seg_spans[NG_RIP_LINE_COUNT];\n")
        f.write("extern const uint16_t g_rip_line_seg_indices[NG_RIP_LINE_SEG_INDEX_COUNT];\n")
        f.write("extern const NgRipSubsector g_rip_subsectors[NG_RIP_SUBSECTOR_COUNT];\n")
        f.write("extern const NgRipNode g_rip_nodes[NG_RIP_NODE_COUNT];\n")
        f.write("extern const NgRipThing g_rip_things[NG_RIP_THING_COUNT];\n")
        f.write("extern const uint8_t g_rip_reject[NG_RIP_REJECT_SIZE];\n")
        f.write("extern const int16_t g_rip_blockmap_words[NG_RIP_BLOCKMAP_WORD_COUNT];\n\n")
        f.write("#endif /* DOOM_RIPDOOM_GENERATED_H */\n")

    with open(source_path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/doom_ripdoom_convert.py; do not edit by hand. */\n")
        f.write(f"#include \"{include_name}\"\n\n")
        write_array(f, "NgRipVertex", "g_rip_vertices", [f"{{{v.x},{v.y}}}" for v in vertices], "NG_RIP_VERTEX_COUNT")
        write_array(
            f,
            "NgRipLine",
            "g_rip_lines",
            [
                f"{{{line.v1},{line.v2},{side_index(line.side_front)},{side_index(line.side_back)},0x{line.flags & 0xffff:04x},0x{line.special & 0xffff:04x},0x{line.tag & 0xffff:04x}}}"
                for line in linedefs
            ],
            "NG_RIP_LINE_COUNT",
        )
        write_array(
            f,
            "NgRipSide",
            "g_rip_sides",
            [
                f"{{{side.texture_x},{side.texture_y},{texture_id(tex_ids, side.top_texture)},{texture_id(tex_ids, side.bottom_texture)},{texture_id(tex_ids, side.mid_texture)},{side.sector}}}"
                for side in sidedefs
            ],
            "NG_RIP_SIDE_COUNT",
        )
        write_array(
            f,
            "NgRipSector",
            "g_rip_sectors",
            [
                "{"
                f"{sector.floor_height},{sector.ceiling_height},"
                f"{texture_id(tex_ids, sector.floor_pic)},{texture_id(tex_ids, sector.ceiling_pic)},"
                f"{max(0, min(255, sector.light_level))},{dc.sector_floor_visual_kind(sector)},"
                f"{dc.sector_damage_amount(sector.special)},0x{sector.special & 0xff:02x},{sector.tag}"
                "}"
                for sector in sectors
            ],
            "NG_RIP_SECTOR_COUNT",
        )
        write_array(f, "NgRipSeg", "g_rip_segs", seg_rows, "NG_RIP_SEG_COUNT")
        write_array(f, "NgRipLineSegSpan", "g_rip_line_seg_spans", line_seg_spans, "NG_RIP_LINE_COUNT")
        write_scalar_array(
            f,
            "uint16_t",
            "g_rip_line_seg_indices",
            [str(seg_index) for seg_index in line_seg_indices],
            "NG_RIP_LINE_SEG_INDEX_COUNT",
            12,
        )
        write_array(
            f,
            "NgRipSubsector",
            "g_rip_subsectors",
            [
                f"{{{subsector.numsegs},{subsector.firstseg},{subsector_sector(i, subsector, segs, linedefs, sidedefs)}}}"
                for i, subsector in enumerate(subsectors)
            ],
            "NG_RIP_SUBSECTOR_COUNT",
        )
        write_array(
            f,
            "NgRipNode",
            "g_rip_nodes",
            [
                f"{{{node.x},{node.y},{node.dx},{node.dy},{{{','.join(str(v) for v in node.bbox)}}},{{{node.child0},{node.child1}}}}}"
                for node in nodes
            ],
            "NG_RIP_NODE_COUNT",
        )
        write_array(
            f,
            "NgRipThing",
            "g_rip_things",
            [
                f"{{{thing.x},{thing.y},{thing.angle},{thing.type},0x{thing.flags & 0xffff:04x},{dc.runtime_thing_class(thing.type)},0x{dc.runtime_thing_info(thing.type):02x}}}"
                for thing in things
            ],
            "NG_RIP_THING_COUNT",
        )
        write_scalar_array(f, "uint8_t", "g_rip_reject", [f"0x{byte:02x}" for byte in reject], "NG_RIP_REJECT_SIZE", 16)
        write_scalar_array(f, "int16_t", "g_rip_blockmap_words", [str(word) for word in blockmap], "NG_RIP_BLOCKMAP_WORD_COUNT", 12)

    if report_path:
        with open(report_path, "w", encoding="ascii") as f:
            f.write(f"map={map_name}\n")
            f.write(f"source={source_name}\n")
            f.write(f"vertices={len(vertices)}/{MAX_RIP_VERTICES}\n")
            f.write(f"linedefs={len(linedefs)}/{MAX_RIP_LINEDEFS}\n")
            f.write(f"sidedefs={len(sidedefs)}/{MAX_RIP_SIDEDEFS}\n")
            f.write(f"sectors={len(sectors)}/{MAX_RIP_SECTORS}\n")
            f.write(f"segs={len(segs)}/{MAX_RIP_SEGS}\n")
            f.write(f"line_seg_indices={len(line_seg_indices)}\n")
            f.write(f"subsectors={len(subsectors)}/{MAX_RIP_SUBSECTORS}\n")
            f.write(f"nodes={len(nodes)}/{MAX_RIP_NODES}\n")
            f.write(f"things={len(things)}/{MAX_RIP_THINGS}\n")
            f.write(f"reject_bytes={len(reject)}\n")
            f.write(f"blockmap_words={len(blockmap)}/{MAX_RIP_BLOCKMAP_WORDS}\n")
            f.write(f"blockmap={blockmap[2]}x{blockmap[3]} origin=({blockmap[0]},{blockmap[1]})\n")
            for name, count in seg_flag_counts.items():
                f.write(f"seg_{name}={count}\n")


def convert(args: argparse.Namespace) -> None:
    wad = dc.Wad(dc.read_wad(args.iwad, args.zip_member))
    lumps = wad.map_lumps(args.map)
    vertices = dc.parse_vertices(lumps["VERTEXES"])
    linedefs = dc.parse_linedefs(lumps["LINEDEFS"])
    sidedefs = dc.parse_sidedefs(lumps["SIDEDEFS"])
    sectors = dc.parse_sectors(lumps["SECTORS"])
    segs = dc.parse_segs(lumps["SEGS"])
    subsectors = dc.parse_subsectors(lumps["SSECTORS"])
    nodes = dc.parse_nodes(lumps["NODES"])
    things = dc.parse_things(lumps["THINGS"])
    reject = lumps["REJECT"]
    blockmap = dc.parse_blockmap_words(lumps["BLOCKMAP"])

    validate_limits(vertices, linedefs, sidedefs, sectors, segs, subsectors, nodes, things, blockmap)
    emit(
        args.out,
        args.source,
        args.report,
        os.path.basename(args.iwad),
        args.map.upper(),
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
        f"{args.map.upper()}: RIPDOOM-lite geometry "
        f"vertices={len(vertices)} sectors={len(sectors)} segs={len(segs)} "
        f"subsectors={len(subsectors)} nodes={len(nodes)} things={len(things)} "
        f"blockmap_words={len(blockmap)} -> {args.out}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", required=True, help="Path to a WAD or Freedoom release zip")
    parser.add_argument("--zip-member", help="WAD member inside a zip archive")
    parser.add_argument("--map", default="E1M1", help="Doom map marker to convert")
    parser.add_argument("--out", required=True, help="Generated C header path")
    parser.add_argument("--source", required=True, help="Generated C source path")
    parser.add_argument("--report", help="Optional text report with counts and semantic span totals")
    args = parser.parse_args()
    try:
        convert(args)
    except Exception as exc:
        print(f"doom_ripdoom_convert.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
