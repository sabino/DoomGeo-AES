#!/usr/bin/env python3
"""gen_gfx.py - emit the non-program ROMs for the raycaster cart.

Outputs into ./rom :
  c1.bin / c2.bin   sprite tiles (Doom wall columns, STBAR HUD, sprites)
  s1.bin            fix layer ROM (blank + solid tiles for the minimap)
  m1.bin / v1.bin   silent sound placeholders

C-ROM encoding follows the NeoGeo dev wiki "Sprite graphics format":
a 16x16 tile is four 8x8 blocks in order (8,0)(8,8)(0,0)(0,8); per row,
bitplanes 0,1 go to the odd ROM (C1) and 2,3 to the even ROM (C2); the
leftmost pixel is the MSB; the color index MSB is bitplane 3.
"""
import argparse
import os
import struct
from collections import Counter

from doom_convert import (
    Wad,
    parse_linedefs,
    parse_sectors,
    parse_sidedefs,
    parse_things,
    parse_vertices,
    read_wad,
)

C_PAD = 0x80000   # pad each C ROM to 512 KiB (well above any tile we use)
S_PAD = 0x20000   # 128 KiB fix ROM, all blank
M_PAD = 0x10000   # 64 KiB Z80 program, all 0x00 (NOP) -> silent
V_PAD = 0x10000   # 64 KiB ADPCM samples, empty

WALL_TEXTURE = "BROWN1"
WALL_MIP_TILE = 1
SOLID_TILE = 2
WALL_ATLAS_BASE = 3
WALL_ATLAS_COLS = 16
WALL_ATLAS_ROWS = 15
WALL_ATLAS_TILES = WALL_ATLAS_COLS * WALL_ATLAS_ROWS
HUD_BASE = WALL_ATLAS_BASE + WALL_ATLAS_TILES
HUD_COLS = 20
HUD_ROWS = 2
HUD_TILES = HUD_COLS * HUD_ROWS
BG_COLS = 20
BG_HALF_ROWS = 6
BG_HALF_TILES = BG_COLS * BG_HALF_ROWS
BG_PHASES = 16
CEILING_BASE = HUD_BASE + HUD_TILES
FLOOR_BASE = CEILING_BASE + BG_PHASES * BG_HALF_TILES
WEAPON_BASE = FLOOR_BASE + BG_PHASES * BG_HALF_TILES
WEAPON_STRIPS = 6
WEAPON_ROWS = 6
WEAPON_TILES = WEAPON_STRIPS * WEAPON_ROWS
WEAPON_FRAME = "PISGA0"
SPRITE_CACHE_BASE = WEAPON_BASE + WEAPON_TILES


def encode_tile(px):
    """px: 16x16 list-of-lists of palette indices (0..15) -> (c1, c2) bytes."""
    c1, c2 = bytearray(), bytearray()
    for (xo, yo) in ((8, 0), (8, 8), (0, 0), (0, 8)):
        for row in range(8):
            y = yo + row
            p0 = p1 = p2 = p3 = 0
            for pix in range(8):
                ci = px[y][xo + pix]
                bit = 7 - pix
                p0 |= ((ci >> 0) & 1) << bit
                p1 |= ((ci >> 1) & 1) << bit
                p2 |= ((ci >> 2) & 1) << bit
                p3 |= ((ci >> 3) & 1) << bit
            c1 += bytes((p0, p1))
            c2 += bytes((p2, p3))
    return c1, c2


def tile_blank():
    return [[0] * 16 for _ in range(16)]


def tile_solid():
    return [[1] * 16 for _ in range(16)]


def tile_brick():
    """Running-bond brick: index 1 mortar, 2 brick body, 3 top highlight."""
    px = [[2] * 16 for _ in range(16)]
    for y in range(16):
        course = y // 8                      # two courses per tile
        offset = 0 if course == 0 else 8     # half-brick stagger
        for x in range(16):
            if y % 8 == 0:                   # horizontal mortar
                px[y][x] = 1
            elif (x + offset) % 8 == 0:      # vertical mortar
                px[y][x] = 1
            elif y % 8 == 1:                 # lit top edge of each brick
                px[y][x] = 3
    return px


def parse_pnames(wad):
    data = wad.lump_data(wad.by_name["PNAMES"][0])
    count = struct.unpack_from("<i", data, 0)[0]
    return [
        data[4 + i * 8 : 12 + i * 8].rstrip(b"\0").decode("ascii", "replace").upper()
        for i in range(count)
    ]


def parse_textures(wad):
    textures = {}
    for lump_name in ("TEXTURE1", "TEXTURE2"):
        for lump_idx in wad.by_name.get(lump_name, []):
            data = wad.lump_data(lump_idx)
            count = struct.unpack_from("<i", data, 0)[0]
            for i in range(count):
                off = struct.unpack_from("<i", data, 4 + i * 4)[0]
                name = data[off : off + 8].rstrip(b"\0").decode("ascii", "replace").upper()
                _masked, width, height, _coldir, patch_count = struct.unpack_from("<ihhih", data, off + 8)
                patches = []
                patch_off = off + 22
                for p in range(patch_count):
                    origin_x, origin_y, patch, _stepdir, _colormap = struct.unpack_from("<hhHHH", data, patch_off + p * 10)
                    patches.append((origin_x, origin_y, patch))
                textures[name] = (width, height, patches)
    return textures


def decode_patch(data):
    width, height, _left, _top = struct.unpack_from("<hhhh", data, 0)
    colofs = [struct.unpack_from("<i", data, 8 + x * 4)[0] for x in range(width)]
    px = [[-1] * width for _ in range(height)]
    for x, off in enumerate(colofs):
        pos = off
        while True:
            top = data[pos]
            pos += 1
            if top == 0xFF:
                break
            length = data[pos]
            pos += 2  # length byte plus unused byte
            for y in range(length):
                if 0 <= top + y < height:
                    px[top + y][x] = data[pos + y]
            pos += length + 1  # pixels plus trailing unused byte
    return px


def compose_texture(wad, texture_name):
    pnames = parse_pnames(wad)
    textures = parse_textures(wad)
    texture_name = texture_name.upper()
    if texture_name not in textures:
        raise ValueError(f"texture {texture_name!r} not found in WAD")

    width, height, patches = textures[texture_name]
    canvas = [[-1] * width for _ in range(height)]
    for origin_x, origin_y, patch_index in patches:
        patch_name = pnames[patch_index]
        lump_ids = wad.by_name.get(patch_name)
        if not lump_ids:
            continue
        patch = decode_patch(wad.lump_data(lump_ids[0]))
        for py, row in enumerate(patch):
            ty = origin_y + py
            if ty < 0 or ty >= height:
                continue
            for px, color in enumerate(row):
                tx = origin_x + px
                if color >= 0 and 0 <= tx < width:
                    canvas[ty][tx] = color
    used = Counter(color for row in canvas for color in row if color >= 0)
    fill = used.most_common(1)[0][0] if used else 0
    for y, row in enumerate(canvas):
        for x, color in enumerate(row):
            if color < 0:
                canvas[y][x] = fill
    return canvas


def playpal_rgb(wad):
    data = wad.lump_data(wad.by_name["PLAYPAL"][0])
    rgb = []
    for i in range(256):
        r, g, b = data[i * 3 : i * 3 + 3]
        rgb.append((r, g, b))
    return rgb


def to_neo_rgb(rgb):
    return tuple(max(0, min(31, (component * 31 + 127) // 255)) for component in rgb)


def texture_palette(texture, playpal, colors=15):
    hist = Counter()
    for row in texture:
        for color in row:
            if color >= 0:
                hist[color] += 1
    if not hist:
        return [(0, 0, 0)] * colors

    seeds = [playpal[index] for index, _count in hist.most_common(colors)]
    while len(seeds) < colors:
        seeds.append(seeds[-1])

    centroids = [(float(r), float(g), float(b)) for r, g, b in seeds]
    weighted = [(playpal[index], count) for index, count in hist.items()]
    for _ in range(10):
        sums = [[0.0, 0.0, 0.0, 0.0] for _ in range(colors)]
        for rgb, count in weighted:
            best = min(
                range(colors),
                key=lambda i: (
                    (rgb[0] - centroids[i][0]) ** 2
                    + (rgb[1] - centroids[i][1]) ** 2
                    + (rgb[2] - centroids[i][2]) ** 2
                ),
            )
            sums[best][0] += rgb[0] * count
            sums[best][1] += rgb[1] * count
            sums[best][2] += rgb[2] * count
            sums[best][3] += count
        for i, (r, g, b, count) in enumerate(sums):
            if count:
                centroids[i] = (r / count, g / count, b / count)

    palette = [tuple(int(round(c)) for c in centroid) for centroid in centroids]
    palette.sort(key=lambda rgb: (rgb[0] * 30 + rgb[1] * 59 + rgb[2] * 11, rgb[0], rgb[1], rgb[2]))
    return palette


def quantize_color(color_index, playpal, palette):
    if color_index < 0:
        return 0
    rgb = playpal[color_index]
    best = min(
        range(len(palette)),
        key=lambda i: (
            (rgb[0] - palette[i][0]) ** 2
            + (rgb[1] - palette[i][1]) ** 2
            + (rgb[2] - palette[i][2]) ** 2
        ),
    )
    return best + 1


def sample_texture_tile(texture, playpal, palette, src_x, src_y, src_w, src_h):
    height = len(texture)
    width = len(texture[0])
    tile = [[0] * 16 for _ in range(16)]
    for y in range(16):
        ty = min(height - 1, src_y + int((y + 0.5) * src_h / 16))
        for x in range(16):
            tx = min(width - 1, src_x + int((x + 0.5) * src_w / 16))
            tile[y][x] = quantize_color(texture[ty][tx], playpal, palette)
    return tile


def sample_wall_column_tile(texture, playpal, palette, tex_col, tex_row, cols, rows):
    height = len(texture)
    width = len(texture[0])
    tx = min(width - 1, int((tex_col + 0.5) * width / cols))
    tile = [[0] * 16 for _ in range(16)]
    for y in range(16):
        ty = min(height - 1, int(((tex_row * 16) + y + 0.5) * height / (rows * 16)))
        color = quantize_color(texture[ty][tx], playpal, palette)
        for x in range(16):
            tile[y][x] = color
    return tile


def wall_texture_tiles(iwad, zip_member, texture_name):
    if not iwad:
        brick = tile_brick()
        return [brick] + [brick for _ in range(WALL_ATLAS_TILES)], "fallback-brick", [(8, 8, 8), (24, 8, 6), (29, 14, 12)] * 5

    wad = Wad(read_wad(iwad, zip_member))
    texture = compose_texture(wad, texture_name)
    playpal = playpal_rgb(wad)
    palette = texture_palette(texture, playpal)
    height = len(texture)
    width = len(texture[0])
    tiles = [sample_texture_tile(texture, playpal, palette, 0, 0, width, height)]
    for ty in range(WALL_ATLAS_ROWS):
        for tx in range(WALL_ATLAS_COLS):
            tiles.append(sample_wall_column_tile(texture, playpal, palette, tx, ty, WALL_ATLAS_COLS, WALL_ATLAS_ROWS))
    return tiles, texture_name.upper(), palette


def line_side_sector(line, sidedefs, back=False):
    idx = line.side_back if back else line.side_front
    if idx == 0xFFFF or idx >= len(sidedefs):
        return None
    return sidedefs[idx].sector


def point_in_sector(px, py, sector_idx, vertices, linedefs, sidedefs):
    inside = False
    for line in linedefs:
        if line_side_sector(line, sidedefs, False) != sector_idx and line_side_sector(line, sidedefs, True) != sector_idx:
            continue
        v1 = vertices[line.v1]
        v2 = vertices[line.v2]
        if (v1.y > py) == (v2.y > py):
            continue
        x_cross = v1.x + (py - v1.y) * (v2.x - v1.x) / (v2.y - v1.y)
        if x_cross > px:
            inside = not inside
    return inside


def map_start_flats(iwad, zip_member, map_name):
    if not iwad:
        return "fallback-ceiling", "fallback-floor"

    wad = Wad(read_wad(iwad, zip_member))
    lumps = wad.map_lumps(map_name)
    vertices = parse_vertices(lumps["VERTEXES"])
    linedefs = parse_linedefs(lumps["LINEDEFS"])
    sidedefs = parse_sidedefs(lumps["SIDEDEFS"])
    sectors = parse_sectors(lumps["SECTORS"])
    things = parse_things(lumps["THINGS"])
    start = next((thing for thing in things if thing.type == 1), things[0] if things else None)
    if start is None:
        return sectors[0].ceiling_pic, sectors[0].floor_pic

    for i, sector in enumerate(sectors):
        if point_in_sector(start.x, start.y, i, vertices, linedefs, sidedefs):
            return sector.ceiling_pic, sector.floor_pic
    return sectors[0].ceiling_pic, sectors[0].floor_pic


def decode_flat(wad, flat_name):
    flat_name = flat_name.upper()
    lump_ids = wad.by_name.get(flat_name)
    if not lump_ids:
        raise ValueError(f"flat {flat_name!r} not found in WAD")
    data = wad.lump_data(lump_ids[0])
    if len(data) != 64 * 64:
        raise ValueError(f"flat {flat_name!r} has invalid size {len(data)}")
    return [list(data[y * 64 : (y + 1) * 64]) for y in range(64)]


def flat_grid_tiles(flat, playpal, palette, cols, rows, ceiling=False, phase=0):
    tiles = []

    width = cols * 16
    height = rows * 16
    center_x = width // 2

    phase_offset = (phase * 64) // BG_PHASES

    for row in range(rows):
        for col in range(cols):
            tile = [[0] * 16 for _ in range(16)]
            for y in range(16):
                screen_y = row * 16 + y
                if ceiling:
                    horizon_delta = max(1, height - screen_y)
                    shade_step = height - screen_y
                else:
                    horizon_delta = max(1, screen_y + 1)
                    shade_step = screen_y

                # Precompute an affine strip for this scanline. Near the
                # horizon, distance is high and the flat compresses; near the
                # viewer, distance drops and texels widen. This costs ROM, not
                # 68000 time, and avoids a runtime floor-span renderer.
                distance = (height * 48) / horizon_delta
                forward = int(distance * 2.0)
                for x in range(16):
                    screen_x = col * 16 + x
                    lateral = int((screen_x - center_x) * distance / 40.0)
                    sx = (lateral + (phase_offset >> 1)) & 63
                    sy = (forward + phase_offset) & 63
                    q = quantize_color(flat[sy][sx], playpal, palette)
                    if shade_step < 24 and q > 1:
                        q -= 1
                    if shade_step < 12 and q > 1:
                        q -= 1
                    tile[y][x] = q
            tiles.append(tile)

    return tiles


def flat_phase_tiles(iwad, zip_member, flat_name, cols, rows, ceiling=False):
    if not iwad:
        palette = [(12, 12, 12), (36, 36, 36), (72, 72, 72)] * 5
        return [tile_solid() for _ in range(cols * rows * BG_PHASES)], flat_name, palette

    wad = Wad(read_wad(iwad, zip_member))
    flat = decode_flat(wad, flat_name)
    playpal = playpal_rgb(wad)
    palette = texture_palette(flat, playpal)
    all_tiles = []
    source = flat_name.upper()
    for phase in range(BG_PHASES):
        all_tiles.extend(flat_grid_tiles(flat, playpal, palette, cols, rows, ceiling=ceiling, phase=phase))
    return all_tiles, source, palette


def patch_grid_tiles(iwad, zip_member, patch_name, cols, rows):
    if not iwad:
        palette = [(20, 20, 20), (45, 45, 45), (70, 70, 70)] * 5
        return [tile_solid() for _ in range(cols * rows)], "fallback-hud", palette, cols * 16, rows * 16

    wad = Wad(read_wad(iwad, zip_member))
    patch_name = patch_name.upper()
    lump_ids = wad.by_name.get(patch_name)
    if not lump_ids:
        raise ValueError(f"patch {patch_name!r} not found in WAD")

    patch = decode_patch(wad.lump_data(lump_ids[0]))
    playpal = playpal_rgb(wad)
    palette = texture_palette(patch, playpal)
    src_h = len(patch)
    src_w = len(patch[0])
    dst_w = cols * 16
    dst_h = rows * 16
    tiles = []

    for row in range(rows):
        for col in range(cols):
            tile = [[0] * 16 for _ in range(16)]
            for y in range(16):
                dy = row * 16 + y
                sy = dy if src_h == dst_h else min(src_h - 1, int((dy + 0.5) * src_h / dst_h))
                for x in range(16):
                    dx = col * 16 + x
                    sx = dx if src_w == dst_w else min(src_w - 1, int((dx + 0.5) * src_w / dst_w))
                    if 0 <= sx < src_w and 0 <= sy < src_h:
                        tile[y][x] = quantize_color(patch[sy][sx], playpal, palette)
            tiles.append(tile)

    return tiles, patch_name, palette, src_w, src_h


def weapon_tiles(iwad, zip_member, patch_name):
    if not iwad:
        palette = [(16, 16, 16), (48, 48, 48), (96, 96, 96)] * 5
        return [tile_solid() for _ in range(WEAPON_TILES)], "fallback-weapon", palette, WEAPON_STRIPS * 16, WEAPON_ROWS * 16

    wad = Wad(read_wad(iwad, zip_member))
    patch_name = patch_name.upper()
    lump_ids = wad.by_name.get(patch_name)
    if not lump_ids:
        raise ValueError(f"weapon patch {patch_name!r} not found in WAD")

    patch = decode_patch(wad.lump_data(lump_ids[0]))
    playpal = playpal_rgb(wad)
    palette = texture_palette(patch, playpal)
    src_h = len(patch)
    src_w = len(patch[0])
    dst_w = WEAPON_STRIPS * 16
    dst_h = WEAPON_ROWS * 16
    x0 = max(0, (dst_w - src_w) // 2)
    y0 = max(0, dst_h - src_h)
    canvas = [[-1] * dst_w for _ in range(dst_h)]

    for y, row in enumerate(patch):
        dy = y0 + y
        if dy >= dst_h:
            break
        for x, color in enumerate(row):
            dx = x0 + x
            if color >= 0 and 0 <= dx < dst_w:
                canvas[dy][dx] = color

    tiles = []
    for row in range(WEAPON_ROWS):
        for strip in range(WEAPON_STRIPS):
            tile = [[0] * 16 for _ in range(16)]
            for y in range(16):
                for x in range(16):
                    tile[y][x] = quantize_color(canvas[row * 16 + y][strip * 16 + x], playpal, palette)
            tiles.append(tile)

    return tiles, patch_name, palette, src_w, src_h


def sprite_scale_tiles(iwad, zip_member, sprite_name, scales):
    if not iwad:
        return [], []

    wad = Wad(read_wad(iwad, zip_member))
    sprite_name = sprite_name.upper()
    lump_ids = wad.by_name.get(sprite_name)
    if not lump_ids:
        raise ValueError(f"sprite frame {sprite_name!r} not found in WAD")
    patch = decode_patch(wad.lump_data(lump_ids[0]))
    playpal = playpal_rgb(wad)
    palette = texture_palette(patch, playpal)
    src_h = len(patch)
    src_w = len(patch[0])

    tiles = []
    meta = []
    next_tile = SPRITE_CACHE_BASE
    for scale in scales:
        dst_w = max(1, int(round(src_w * scale)))
        dst_h = max(1, int(round(src_h * scale)))
        strips = (dst_w + 15) // 16
        rows = (dst_h + 15) // 16
        meta.append((sprite_name, scale, next_tile, strips, rows, dst_w, dst_h))
        for row in range(rows):
            for strip in range(strips):
                tile = [[0] * 16 for _ in range(16)]
                for y in range(16):
                    dy = row * 16 + y
                    if dy >= dst_h:
                        continue
                    sy = min(src_h - 1, int(dy / scale))
                    for x in range(16):
                        dx = strip * 16 + x
                        if dx >= dst_w:
                            continue
                        sx = min(src_w - 1, int(dx / scale))
                        tile[y][x] = quantize_color(patch[sy][sx], playpal, palette)
                tiles.append(tile)
        next_tile += strips * rows
    return tiles, meta


def write_palette_header(path, wall_palette, wall_source, hud_palette, hud_source, ceiling_palette, ceiling_source, floor_palette, floor_source, weapon_palette, weapon_source):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/gen_gfx.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_GFX_GENERATED_H\n#define DOOM_GFX_GENERATED_H\n\n")
        f.write("#define WALL_PALETTE_COLORS 15\n")
        f.write(f"#define WALL_TEXTURE_SOURCE \"{wall_source}\"\n")
        f.write("static const u8 g_wall_palette_rgb[WALL_PALETTE_COLORS][3] = {\n")
        for rgb in wall_palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n")
        f.write("#define HUD_PALETTE_COLORS 15\n")
        f.write(f"#define HUD_PATCH_SOURCE \"{hud_source}\"\n")
        f.write("static const u8 g_hud_palette_rgb[HUD_PALETTE_COLORS][3] = {\n")
        for rgb in hud_palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n")
        f.write("#define CEILING_PALETTE_COLORS 15\n")
        f.write(f"#define CEILING_FLAT_SOURCE \"{ceiling_source}\"\n")
        f.write("static const u8 g_ceiling_palette_rgb[CEILING_PALETTE_COLORS][3] = {\n")
        for rgb in ceiling_palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n")
        f.write("#define FLOOR_PALETTE_COLORS 15\n")
        f.write(f"#define FLOOR_FLAT_SOURCE \"{floor_source}\"\n")
        f.write("static const u8 g_floor_palette_rgb[FLOOR_PALETTE_COLORS][3] = {\n")
        for rgb in floor_palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n")
        f.write("#define WEAPON_PALETTE_COLORS 15\n")
        f.write(f"#define WEAPON_PATCH_SOURCE \"{weapon_source}\"\n")
        f.write("static const u8 g_weapon_palette_rgb[WEAPON_PALETTE_COLORS][3] = {\n")
        for rgb in weapon_palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n#endif /* DOOM_GFX_GENERATED_H */\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iwad", help="Path to a WAD or Freedoom release zip for offline texture conversion")
    ap.add_argument("--zip-member", help="WAD member inside a zip archive")
    ap.add_argument("--wall-texture", default=WALL_TEXTURE, help="Doom wall texture to precompose into C-ROM tiles")
    ap.add_argument("--map", default="E1M1", help="Doom map used to select player-start floor and ceiling flats")
    ap.add_argument("--palette-header", help="Generated wall palette header")
    ap.add_argument("--weapon-frame", default=WEAPON_FRAME, help="Doom weapon patch frame to render as the player overlay")
    ap.add_argument("--sprite-frame", default="TROOA1", help="Doom sprite patch frame to pre-scale into C-ROM strips")
    ap.add_argument("--sprite-scales", default="1.00,0.75,0.50,0.33,0.25", help="Comma-separated sprite scale levels")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "rom")
    os.makedirs(out, exist_ok=True)

    wall_tiles, wall_source, wall_palette = wall_texture_tiles(args.iwad, args.zip_member, args.wall_texture)
    hud_tiles, hud_source, hud_palette, hud_w, hud_h = patch_grid_tiles(args.iwad, args.zip_member, "STBAR", HUD_COLS, HUD_ROWS)
    ceiling_flat, floor_flat = map_start_flats(args.iwad, args.zip_member, args.map)
    ceiling_tiles, ceiling_source, ceiling_palette = flat_phase_tiles(args.iwad, args.zip_member, ceiling_flat, BG_COLS, BG_HALF_ROWS, ceiling=True)
    floor_tiles, floor_source, floor_palette = flat_phase_tiles(args.iwad, args.zip_member, floor_flat, BG_COLS, BG_HALF_ROWS)
    weapon_cache, weapon_source, weapon_palette, weapon_w, weapon_h = weapon_tiles(args.iwad, args.zip_member, args.weapon_frame)
    if args.palette_header:
        write_palette_header(
            args.palette_header,
            wall_palette,
            wall_source,
            hud_palette,
            hud_source,
            ceiling_palette,
            ceiling_source,
            floor_palette,
            floor_source,
            weapon_palette,
            weapon_source,
        )
    scales = [float(item) for item in args.sprite_scales.split(",") if item.strip()]
    sprite_tiles, sprite_meta = sprite_scale_tiles(args.iwad, args.zip_member, args.sprite_frame, scales)
    tiles = [tile_blank(), wall_tiles[0], tile_solid()] + wall_tiles[1:] + hud_tiles + ceiling_tiles + floor_tiles + weapon_cache + sprite_tiles
    assert len(tiles) >= WALL_ATLAS_BASE + WALL_ATLAS_TILES
    c1, c2 = bytearray(), bytearray()
    for t in tiles:
        a, b = encode_tile(t)
        c1 += a
        c2 += b
    assert len(c1) == len(tiles) * 64 and len(c2) == len(tiles) * 64

    c1 += bytes(C_PAD - len(c1))
    c2 += bytes(C_PAD - len(c2))

    # Fix (S-ROM) tileset: tile 0 = transparent (all index 0 -> all 0x00),
    # tile 1 = solid (all index 15 -> all 0xFF). The all-0xFF tile reads as
    # color index 15 regardless of the fix format's byte/plane ordering, so
    # the minimap picks its colour purely via the fix word's palette field.
    FIX_TILE = 32  # 8x8 * 4bpp = 32 bytes
    s1 = bytearray()
    s1 += bytes(FIX_TILE)            # tile 0: blank
    s1 += bytes([0xFF]) * FIX_TILE   # tile 1: solid index 15
    s1 += bytes(S_PAD - len(s1))

    files = {
        "c1.bin": c1,
        "c2.bin": c2,
        "s1.bin": s1,
        "m1.bin": bytes(M_PAD),
        "v1.bin": bytes(V_PAD),
    }
    for name, data in files.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
        print(f"  {name:8} {len(data):#8x} bytes")

    print(f"  wall texture: {wall_source} mip={WALL_MIP_TILE} atlas={WALL_ATLAS_BASE}..{WALL_ATLAS_BASE + WALL_ATLAS_TILES - 1} ({WALL_ATLAS_COLS}x{WALL_ATLAS_ROWS})")
    print(f"  hud patch: {hud_source} tile={HUD_BASE}..{HUD_BASE + HUD_TILES - 1} ({HUD_COLS}x{HUD_ROWS}) source={hud_w}x{hud_h}")
    print(f"  ceiling flat: {ceiling_source} tile={CEILING_BASE}..{CEILING_BASE + BG_PHASES * BG_HALF_TILES - 1} ({BG_COLS}x{BG_HALF_ROWS}x{BG_PHASES})")
    print(f"  floor flat: {floor_source} tile={FLOOR_BASE}..{FLOOR_BASE + BG_PHASES * BG_HALF_TILES - 1} ({BG_COLS}x{BG_HALF_ROWS}x{BG_PHASES})")
    print(f"  weapon frame: {weapon_source} tile={WEAPON_BASE}..{WEAPON_BASE + WEAPON_TILES - 1} ({WEAPON_STRIPS}x{WEAPON_ROWS}) source={weapon_w}x{weapon_h}")
    for name, scale, base, strips, rows, width, height in sprite_meta:
        print(f"  sprite frame: {name} scale={scale:.2f} tile={base} strips={strips} rows={rows} size={width}x{height}")
    print(f"  tiles encoded: blank wall-cache solid "
          f"({len(tiles)*128} C-ROM bytes used)")


if __name__ == "__main__":
    main()
