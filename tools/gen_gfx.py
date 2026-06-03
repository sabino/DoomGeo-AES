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
import math
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

C_PAD = 0x200000  # pad each C ROM to 2 MiB for larger precomputed tile banks
S_PAD = 0x20000   # 128 KiB fix ROM, all blank
M_PAD = 0x10000   # 64 KiB Z80 program, all 0x00 (NOP) -> silent
V_PAD = 0x10000   # 64 KiB ADPCM samples, empty

WALL_TEXTURE = "BROWN1"
WALL_ALT_TEXTURES = ("BROWNGRN", "BROWN1", "SUPPORT2", "LITE3", "COMPTILE", "DOORSTOP", "BROWN144")
DOOR_TEXTURE = "BIGDOOR2"
WALL_MIP_TILE = 1
SOLID_TILE = 2
WALL_ATLAS_BASE = 3
WALL_ATLAS_COLS = 16
WALL_ATLAS_ROWS = 15
WALL_ATLAS_TILES = WALL_ATLAS_COLS * WALL_ATLAS_ROWS
WALL_ALT_ATLAS_BASE = WALL_ATLAS_BASE + WALL_ATLAS_TILES
DOOR_ATLAS_BASE = WALL_ALT_ATLAS_BASE + len(WALL_ALT_TEXTURES) * WALL_ATLAS_TILES
BG_COLS = 20
BG_HALF_ROWS = 6
BG_HALF_TILES = BG_COLS * BG_HALF_ROWS
FLAT_COLS = 16
FLAT_ROWS = 16
FLAT_TILES = FLAT_COLS * FLAT_ROWS
PLANE_PERSPECTIVE_DIRS = 16
PLANE_PERSPECTIVE_PHASES = 1
PLANE_PERSPECTIVE_ROWS = BG_HALF_ROWS
PLANE_PERSPECTIVE_COLS = BG_COLS
PLANE_PERSPECTIVE_TILES = (
    PLANE_PERSPECTIVE_DIRS
    * PLANE_PERSPECTIVE_PHASES
    * PLANE_PERSPECTIVE_PHASES
    * PLANE_PERSPECTIVE_ROWS
    * PLANE_PERSPECTIVE_COLS
)
PLANE_TEXEL_Q8_DIV = 2
CEILING_FLAT_BASE = DOOR_ATLAS_BASE + WALL_ATLAS_TILES
FLOOR_FLAT_BASE = CEILING_FLAT_BASE + FLAT_TILES
HUD_BASE = FLOOR_FLAT_BASE + FLAT_TILES
HUD_COLS = 20
HUD_ROWS = 2
HUD_TILES = HUD_COLS * HUD_ROWS
HUD_FACE_BASE = HUD_BASE + HUD_TILES
HUD_FACE_COL = 9
HUD_FACE_COLS = 2
HUD_FACE_ROWS = 2
HUD_FACE_TILES = HUD_FACE_COLS * HUD_FACE_ROWS
HUD_FACE_FRAMES = tuple(
    [f"STFST{pain}{variant}" for pain in range(5) for variant in range(3)]
    + [f"STFTR{pain}0" for pain in range(5)]
    + [f"STFTL{pain}0" for pain in range(5)]
    + [f"STFOUCH{pain}" for pain in range(5)]
    + [f"STFEVL{pain}" for pain in range(5)]
    + ["STFDEAD0"]
)
HUD_FACE_TUNE_TRANSFORMS = tuple(
    (
        bool(index & 0x01),  # swap 16px columns
        bool(index & 0x02),  # mirror pixels horizontally inside each tile
        bool(index & 0x04),  # swap 16px rows
        bool(index & 0x08),  # mirror pixels vertically inside each tile
    )
    for index in range(16)
)
WEAPON_BASE = HUD_FACE_BASE + len(HUD_FACE_FRAMES) * HUD_FACE_TILES
WEAPON_STRIPS = 7
WEAPON_ROWS = 8
WEAPON_TILES = WEAPON_STRIPS * WEAPON_ROWS
WEAPON_FRAMES = (
    "PISGA0", "PISGB0+PISFA0", "PISGC0", "PISGD0",
    "SHTGA0", "SHTGB0", "SHTGC0", "SHTGD0",
    "CHGGA0", "CHGGB0",
    "MISGA0", "MISGB0",
    "PLSGA0", "PLSGB0+PLSFA0", "PLSGA0", "PLSGB0+PLSFB0",
    "BFGGA0", "BFGGB0+BFGFA0", "BFGGB0+BFGFB0", "BFGGA0",
    "PUNGA0", "PUNGB0", "PUNGC0", "PUNGD0",
    "SAWGA0", "SAWGB0", "SAWGC0", "SAWGD0",
)
HUD_KEYCARD_FRAMES = ("BKEYA0", "RKEYA0", "YKEYA0")
HUD_KEYCARD_BASE = WEAPON_BASE + len(WEAPON_FRAMES) * WEAPON_TILES
HUD_KEYCARD_TILES = len(HUD_KEYCARD_FRAMES)
HUD_DIGIT_BASE = HUD_KEYCARD_BASE + HUD_KEYCARD_TILES
HUD_DIGIT_TILES = 10
HUD_SMALL_DIGIT_BASE = HUD_DIGIT_BASE + HUD_DIGIT_TILES
HUD_SMALL_DIGIT_TILES = 10
CEILING_PERSPECTIVE_BASE = HUD_SMALL_DIGIT_BASE + HUD_SMALL_DIGIT_TILES
FLOOR_PERSPECTIVE_BASE = CEILING_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES
SPRITE_CACHE_BASE = FLOOR_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES
# Must match main.c/config.h's weapon sprite-chain top. If this differs, the
# correct Doom psprite is baked into the wrong part of the visible tile window.
WEAPON_SCREEN_TOP = 192 - WEAPON_ROWS * 16
WEAPON_SCREEN_LEFT = (320 - WEAPON_STRIPS * 16) // 2
DOOM_PSPR_SX = 1
DOOM_PSPR_SY = 32
WEAPON_BAKE_Y_ADJUST = 0


def encode_tile(px):
    """px: 16x16 list-of-lists of palette indices (0..15) -> (c1, c2) bytes."""
    c1, c2 = bytearray(), bytearray()
    for (xo, yo) in ((8, 0), (8, 8), (0, 0), (0, 8)):
        for row in range(8):
            y = yo + row
            p0 = p1 = p2 = p3 = 0
            for pix in range(8):
                ci = px[y][xo + pix]
                bit = pix
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


DIGIT_GLYPHS = (
    (0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00),
    (0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00),
    (0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00),
    (0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00),
    (0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00),
    (0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00),
    (0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00),
    (0x7E, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x00),
    (0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00),
    (0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00),
)

CROSSHAIR_GLYPH = (0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00)
EXIT_GLYPHS = (
    (0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00),
    (0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00),
    (0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00),
    (0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00),
)
DEAD_GLYPHS = (
    (0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00),
    (0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00),
)
KEY_GLYPHS = (
    (0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00),
    (0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00),
    (0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x00),
)
KEY_MSG_K_GLYPH = (0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00)
AMMO_GLYPHS = (
    (0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00),
    (0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00),
)
SECRET_GLYPHS = (
    (0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00),
    (0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00),
)


def encode_fix_pixels(pixels):
    data = bytearray()
    for xa, xb in ((4, 5), (6, 7), (0, 1), (2, 3)):
        for y in range(8):
            pixel_a = pixels[y * 8 + xa] & 0x0F
            pixel_b = (pixels[y * 8 + xb] & 0x0F) << 4
            data.append(pixel_b | pixel_a)
    return data


def encode_fix_glyph(rows):
    pixels = [0] * 64
    for y, bits in enumerate(rows):
        for x in range(8):
            if bits & (0x80 >> x):
                pixels[y * 8 + x] = 15

    return encode_fix_pixels(pixels)


def encode_fix_patch_tile(patch, playpal, palette, tile_x, tile_y):
    src_h = len(patch)
    src_w = len(patch[0])
    canvas = [[-1] * 16 for _ in range(16)]
    x0 = max(0, (16 - src_w) // 2)
    y0 = max(0, (16 - src_h) // 2)
    for sy, row in enumerate(patch):
        dy = y0 + sy
        if dy >= 16:
            break
        for sx, color in enumerate(row):
            dx = x0 + sx
            if color >= 0 and 0 <= dx < 16:
                canvas[dy][dx] = color

    pixels = [0] * 64
    for y in range(8):
        for x in range(8):
            pixels[y * 8 + x] = quantize_color(canvas[tile_y * 8 + y][tile_x * 8 + x], playpal, palette)
    return encode_fix_pixels(pixels)


def doom_status_digit_fix_tiles(iwad, zip_member, palette):
    if not iwad:
        tiles = []
        for glyph in DIGIT_GLYPHS:
            tiles.extend((bytes(32), bytes(32), encode_fix_glyph(glyph), bytes(32)))
        return tiles

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    tiles = []
    for digit in range(10):
        lump_ids = wad.by_name.get(f"STTNUM{digit}")
        if not lump_ids:
            tiles.extend((bytes(32), bytes(32), encode_fix_glyph(DIGIT_GLYPHS[digit]), bytes(32)))
            continue
        patch = decode_patch(wad.lump_data(lump_ids[0]))
        tiles.append(encode_fix_patch_tile(patch, playpal, palette, 0, 0))
        tiles.append(encode_fix_patch_tile(patch, playpal, palette, 1, 0))
        tiles.append(encode_fix_patch_tile(patch, playpal, palette, 0, 1))
        tiles.append(encode_fix_patch_tile(patch, playpal, palette, 1, 1))
    return tiles


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


def patch_header(data):
    return struct.unpack_from("<hhhh", data, 0)


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
    return quantize_rgb(rgb, palette)


def quantize_rgb(rgb, palette):
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


def flat_texture_tiles(iwad, zip_member, flat_name, ceiling=False):
    if not iwad:
        base = (40, 42, 48) if ceiling else (56, 48, 36)
        return flat_name, [base] * 15, [tile_solid() for _ in range(FLAT_TILES)]

    wad = Wad(read_wad(iwad, zip_member))
    flat = decode_flat(wad, flat_name)
    playpal = playpal_rgb(wad)
    palette = texture_palette(flat, playpal)
    if ceiling:
        palette = [(r * 3 // 4, g * 3 // 4, min(255, b * 5 // 4)) for r, g, b in palette]
    tiles = []
    phase_x = 64 // FLAT_COLS
    phase_y = 64 // FLAT_ROWS
    for row in range(FLAT_ROWS):
        for col in range(FLAT_COLS):
            tile = [[0] * 16 for _ in range(16)]
            for y in range(16):
                sy = (row * phase_y + y) & 63
                for x in range(16):
                    sx = (col * phase_x + x) & 63
                    tile[y][x] = quantize_color(flat[sy][sx], playpal, palette)
            tiles.append(tile)
    source = flat_name.upper()
    return source, palette, tiles


def perspective_plane_tiles(iwad, zip_member, flat_name, palette, ceiling=False):
    if not iwad:
        return [tile_solid() for _ in range(PLANE_PERSPECTIVE_TILES)]

    wad = Wad(read_wad(iwad, zip_member))
    flat = decode_flat(wad, flat_name)
    playpal = playpal_rgb(wad)
    tiles = []
    fov_plane = 0.66
    horizon = 96
    game_h = 192

    for direction in range(PLANE_PERSPECTIVE_DIRS):
        angle = (direction / PLANE_PERSPECTIVE_DIRS) * math.tau
        dir_x = math.cos(angle)
        dir_y = math.sin(angle)
        plane_x = -dir_y * fov_plane
        plane_y = dir_x * fov_plane
        for phase_y in range(PLANE_PERSPECTIVE_PHASES):
            origin_y = (phase_y * 256) // PLANE_PERSPECTIVE_PHASES
            for phase_x in range(PLANE_PERSPECTIVE_PHASES):
                origin_x = (phase_x * 256) // PLANE_PERSPECTIVE_PHASES
                for row in range(PLANE_PERSPECTIVE_ROWS):
                    screen_tile_y = row if ceiling else BG_HALF_ROWS + row
                    for col in range(PLANE_PERSPECTIVE_COLS):
                        tile = [[0] * 16 for _ in range(16)]
                        for y in range(16):
                            screen_y = screen_tile_y * 16 + y
                            p = abs(screen_y - horizon)
                            if p < 8:
                                p = 8
                            dist_q8 = (game_h << 7) / (p + 8)
                            for x in range(16):
                                screen_x = col * 16 + x
                                camera_x = (2.0 * screen_x / 320.0) - 1.0
                                ray_x = dir_x + plane_x * camera_x
                                ray_y = dir_y + plane_y * camera_x
                                if ceiling:
                                    world_x = origin_x - ray_x * dist_q8
                                    world_y = origin_y - ray_y * dist_q8
                                else:
                                    world_x = origin_x + ray_x * dist_q8
                                    world_y = origin_y + ray_y * dist_q8
                                sx = int(world_x / PLANE_TEXEL_Q8_DIV) & 63
                                sy = int(world_y / PLANE_TEXEL_Q8_DIV) & 63
                                tile[y][x] = quantize_color(flat[sy][sx], playpal, palette)
                        tiles.append(tile)
    return tiles


def patch_grid_tiles(iwad, zip_member, patch_name, cols, rows):
    if not iwad:
        palette = [(20, 20, 20), (45, 45, 45), (70, 70, 70)] * 5
        return [tile_solid() for _ in range(cols * rows)], "fallback-hud", palette, cols * 16, rows * 16

    wad = Wad(read_wad(iwad, zip_member))
    patch_name = patch_name.upper()
    lump_ids = wad.by_name.get(patch_name)
    if not lump_ids:
        raise ValueError(f"patch {patch_name!r} not found in WAD")

    playpal = playpal_rgb(wad)
    patch = decode_patch(wad.lump_data(lump_ids[0]))
    if patch_name == "STBAR":
        face_ids = wad.by_name.get("STFST00")
        if face_ids:
            face_data = wad.lump_data(face_ids[0])
            _face_w, _face_h, face_left, face_top = patch_header(face_data)
            face = decode_patch(face_data)
            for fy, face_row in enumerate(face):
                dy = 0 - face_top + fy
                if dy < 0:
                    continue
                if dy >= len(patch):
                    break
                for fx, color in enumerate(face_row):
                    dx = 143 - face_left + fx
                    if color >= 0 and 0 <= dx < len(patch[dy]):
                        patch[dy][dx] = color
    palette_src = list(patch)
    if patch_name == "STBAR":
        for face_name in HUD_FACE_FRAMES:
            face_ids = wad.by_name.get(face_name)
            if face_ids:
                palette_src.extend(decode_patch(wad.lump_data(face_ids[0])))
        for digit in range(10):
            digit_ids = wad.by_name.get(f"STTNUM{digit}")
            if digit_ids:
                palette_src.extend(decode_patch(wad.lump_data(digit_ids[0])))
    palette = texture_palette(palette_src, playpal)
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


def stbar_with_face(wad, face_name, face_dx=0, face_dy=0):
    patch_ids = wad.by_name.get("STBAR")
    if not patch_ids:
        raise ValueError("patch 'STBAR' not found in WAD")
    face_ids = wad.by_name.get(face_name)
    if not face_ids:
        raise ValueError(f"status face patch {face_name!r} not found in WAD")

    stbar = decode_patch(wad.lump_data(patch_ids[0]))
    face_data = wad.lump_data(face_ids[0])
    _face_w, _face_h, face_left, face_top = patch_header(face_data)
    face = decode_patch(face_data)
    for fy, face_row in enumerate(face):
        dy = face_dy - face_top + fy
        if dy < 0:
            continue
        if dy >= len(stbar):
            break
        for fx, color in enumerate(face_row):
            dx = 143 + face_dx - face_left + fx
            if color >= 0 and 0 <= dx < len(stbar[dy]):
                stbar[dy][dx] = color
    return stbar


def tile_quadrant_probe():
    tile = tile_blank()
    values = (1, 5, 10, 15)
    positions = ((0, 0), (8, 0), (0, 8), (8, 8))
    for value, (x0, y0) in zip(values, positions):
        for y in range(8):
            for x in range(8):
                tile[y0 + y][x0 + x] = value
    return tile


def tile_bit_order_probe():
    tile = tile_blank()
    values = (1, 5, 10, 15)
    for y in range(16):
        for x in range(16):
            tile[y][x] = values[x // 4]
    return tile


def hud_face_probe_tiles(kind):
    if kind == "solid-order":
        return [
            [[value] * 16 for _ in range(16)]
            for value in (1, 5, 10, 15)
        ]
    if kind == "quadrants":
        return [tile_quadrant_probe() for _ in range(HUD_FACE_TILES)]
    if kind == "bit-order":
        return [tile_bit_order_probe() for _ in range(HUD_FACE_TILES)]
    raise ValueError(f"unknown face probe kind {kind!r}")


def hud_face_tile_set(
    wad,
    playpal,
    palette,
    face_name,
    face_dx=0,
    face_dy=0,
    swap_cols=True,
    mirror_x=True,
    swap_rows=False,
    mirror_y=False,
):
    stbar = stbar_with_face(wad, face_name, face_dx, face_dy)
    tiles = [None] * HUD_FACE_TILES
    for row in range(HUD_FACE_ROWS):
        src_row_base = (HUD_FACE_ROWS - 1 - row) if swap_rows else row
        for col in range(HUD_FACE_COLS):
            tile = [[0] * 16 for _ in range(16)]
            src_col_base = (HUD_FACE_COLS - 1 - col) if swap_cols else col
            src_col = HUD_FACE_COL + src_col_base
            for y in range(16):
                sy = src_row_base * 16 + ((15 - y) if mirror_y else y)
                for x in range(16):
                    sx = src_col * 16 + ((15 - x) if mirror_x else x)
                    if 0 <= sy < len(stbar) and 0 <= sx < len(stbar[sy]):
                        tile[y][x] = quantize_color(stbar[sy][sx], playpal, palette)
            tiles[row * HUD_FACE_COLS + col] = tile
    return tiles


def hud_face_tiles(iwad, zip_member, face_names, palette, face_tune_grid=False):
    if not iwad:
        return [tile_solid() for _ in range(HUD_FACE_TILES * len(HUD_FACE_FRAMES))], "fallback-faces"

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    tiles = []
    if face_tune_grid:
        for frame in range(len(HUD_FACE_FRAMES)):
            if frame == 0:
                tiles.extend(hud_face_probe_tiles("solid-order"))
                continue
            if frame == 1:
                tiles.extend(hud_face_probe_tiles("bit-order"))
                continue
            tune_frame = frame - 2
            swap_cols, mirror_x, swap_rows, mirror_y = HUD_FACE_TUNE_TRANSFORMS[tune_frame % len(HUD_FACE_TUNE_TRANSFORMS)]
            tiles.extend(
                hud_face_tile_set(
                    wad,
                    playpal,
                    palette,
                    "STFST00",
                    swap_cols=swap_cols,
                    mirror_x=mirror_x,
                    swap_rows=swap_rows,
                    mirror_y=mirror_y,
                )
            )
        return tiles, "STFST00 extraction-tune"

    for face_name in face_names:
        tiles.extend(hud_face_tile_set(wad, playpal, palette, face_name.upper()))
    return tiles, "+".join(face_names)


def weapon_tiles(iwad, zip_member, patch_names):
    if not iwad:
        palette = [(16, 16, 16), (48, 48, 48), (96, 96, 96)] * 5
        return [tile_solid() for _ in range(WEAPON_TILES * len(WEAPON_FRAMES))], "fallback-weapon", palette, WEAPON_STRIPS * 16, WEAPON_ROWS * 16

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    frames = []
    for patch_name in patch_names:
        frame_patches = []
        for part_name in patch_name.upper().split("+"):
            part_name = part_name.strip()
            lump_ids = wad.by_name.get(part_name)
            if not lump_ids:
                fallback_ids = wad.by_name.get("PISGA0")
                if not fallback_ids:
                    raise ValueError(f"weapon patch {part_name!r} not found in WAD and PISGA0 fallback is unavailable")
                data = wad.lump_data(fallback_ids[0])
                _width, _height, left, top = patch_header(data)
                frame_patches.append((f"{part_name}:PISGA0-fallback", decode_patch(data), left, top))
                continue
            data = wad.lump_data(lump_ids[0])
            _width, _height, left, top = patch_header(data)
            frame_patches.append((part_name, decode_patch(data), left, top))
        frames.append((patch_name.upper(), frame_patches))

    palette_src = []
    for _frame_name, frame_patches in frames:
        for _name, patch, _left, _top in frame_patches:
            palette_src.extend(patch)
    palette = texture_palette(palette_src, playpal)
    dst_w = WEAPON_STRIPS * 16
    dst_h = WEAPON_ROWS * 16

    tiles = []
    max_w = max(len(patch[0]) for _frame_name, frame_patches in frames for _name, patch, _left, _top in frame_patches)
    max_h = max(len(patch) for _frame_name, frame_patches in frames for _name, patch, _left, _top in frame_patches)
    for _frame_name, frame_patches in frames:
        canvas = [[-1] * dst_w for _ in range(dst_h)]

        for _name, patch, left, top in frame_patches:
            # Doom psprites are positioned from a screen-space anchor and each
            # patch's own left/top offsets. Bake that convention offline so the
            # 68000 only swaps complete tile frames at runtime.
            screen_x = DOOM_PSPR_SX - left
            screen_y = DOOM_PSPR_SY - top
            x0 = screen_x - WEAPON_SCREEN_LEFT
            y0 = screen_y - WEAPON_SCREEN_TOP + WEAPON_BAKE_Y_ADJUST

            for y, row in enumerate(patch):
                dy = y0 + y
                if dy < 0:
                    continue
                if dy >= dst_h:
                    break
                for x, color in enumerate(row):
                    dx = x0 + x
                    if color >= 0 and 0 <= dx < dst_w:
                        canvas[dy][dx] = color

        for row in range(WEAPON_ROWS):
            for strip in range(WEAPON_STRIPS):
                tile = [[0] * 16 for _ in range(16)]
                for y in range(16):
                    for x in range(16):
                        color = canvas[row * 16 + y][strip * 16 + x]
                        tile[y][x] = 0 if color < 0 else quantize_color(color, playpal, palette)
                tiles.append(tile)

    return tiles, "+".join(frame_name for frame_name, _frame_patches in frames), palette, max_w, max_h


def hud_keycard_tiles(iwad, zip_member):
    if not iwad:
        return [tile_solid() for _ in HUD_KEYCARD_FRAMES], "fallback-keycards"

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    tiles = []
    sources = []
    for frame in HUD_KEYCARD_FRAMES:
        lump_ids = wad.by_name.get(frame)
        if not lump_ids:
            tiles.append(tile_blank())
            sources.append(f"{frame}:missing")
            continue
        patch = decode_patch(wad.lump_data(lump_ids[0]))
        palette = texture_palette(patch, playpal)
        tile = [[0] * 16 for _ in range(16)]
        for y, row in enumerate(patch[:16]):
            for x, color in enumerate(row[:16]):
                tile[y][x] = quantize_color(color, playpal, palette)
        tiles.append(tile)
        sources.append(frame)
    return tiles, "+".join(sources)


def hud_digit_tiles(iwad, zip_member, palette):
    if not iwad:
        return [tile_solid() for _ in range(10)], "fallback-digits"

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    tiles = []
    sources = []
    for digit in range(10):
        frame = f"STTNUM{digit}"
        lump_ids = wad.by_name.get(frame)
        if not lump_ids:
            tiles.append(tile_blank())
            sources.append(f"{frame}:missing")
            continue
        patch = decode_patch(wad.lump_data(lump_ids[0]))
        tile = tile_blank()
        x0 = max(0, (16 - len(patch[0])) // 2)
        y0 = max(0, (16 - len(patch)) // 2)
        for sy, row in enumerate(patch):
            dy = y0 + sy
            if dy >= 16:
                break
            for sx, color in enumerate(row):
                dx = x0 + sx
                if color >= 0 and 0 <= dx < 16:
                    tile[dy][dx] = quantize_color(color, playpal, palette)
        tiles.append(tile)
        sources.append(frame)
    return tiles, "+".join(sources)


def hud_small_digit_tiles(iwad, zip_member):
    if not iwad:
        return [tile_solid() for _ in range(10)], "fallback-small-digits"

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    sources = []
    patches = []
    for digit in range(10):
        frame = f"STYSNUM{digit}"
        lump_ids = wad.by_name.get(frame)
        if not lump_ids:
            patches.append(None)
            sources.append(f"{frame}:missing")
        else:
            patch = decode_patch(wad.lump_data(lump_ids[0]))
            patches.append(patch)
            sources.append(frame)
    palette_src = [row for patch in patches if patch for row in patch]
    palette = texture_palette(palette_src, playpal)
    tiles = []
    for patch in patches:
        tile = tile_blank()
        if patch:
            for y, row in enumerate(patch[:16]):
                for x, color in enumerate(row[:16]):
                    tile[y][x] = quantize_color(color, playpal, palette)
        tiles.append(tile)
    return tiles, palette, "+".join(sources)


def sprite_scale_tiles(iwad, zip_member, sprite_name, scales, start_tile):
    if not iwad:
        return [], [], [], start_tile

    wad = Wad(read_wad(iwad, zip_member))
    sprite_name = sprite_name.upper()
    lump_ids = wad.by_name.get(sprite_name)
    if not lump_ids:
        return [], [], [], start_tile
    patch = decode_patch(wad.lump_data(lump_ids[0]))
    playpal = playpal_rgb(wad)
    palette = texture_palette(patch, playpal)
    src_h = len(patch)
    src_w = len(patch[0])

    tiles = []
    meta = []
    next_tile = start_tile
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
    return tiles, meta, palette, next_tile


def parse_monster_sprites(spec):
    result = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        thing_type, frame = item.split(":", 1)
        result.append((int(thing_type), frame.strip().upper()))
    return result


def monster_sprite_tiles(iwad, zip_member, specs, scales):
    tiles = []
    defs = []
    metas = []
    palettes = []
    next_tile = SPRITE_CACHE_BASE
    for thing_type, frame in specs:
        first_scale = len(metas)
        frame_tiles, frame_meta, palette, next_tile = sprite_scale_tiles(iwad, zip_member, frame, scales, next_tile)
        if not frame_meta:
            continue
        tiles.extend(frame_tiles)
        metas.extend(frame_meta)
        palettes.append(palette)
        defs.append((thing_type, first_scale, len(frame_meta), frame))
    return tiles, defs, metas, palettes


def write_palette_header(path, wall_palette, wall_source, wall_alt_palettes, wall_alt_sources, door_palette, door_source, hud_palette, hud_source, hud_small_digit_palette, hud_small_digit_source, ceiling_palette, ceiling_source, floor_palette, floor_source, weapon_palette, weapon_source, sprite_defs, sprite_meta, sprite_palettes):
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
        f.write("#define WALL_ALT_PALETTE_COLORS 15\n")
        f.write(f"#define WALL_ALT_TEXTURE_COUNT {len(wall_alt_palettes)}\n")
        f.write("static const char g_wall_alt_texture_sources[WALL_ALT_TEXTURE_COUNT][9] = {\n")
        for source in wall_alt_sources:
            f.write(f"    \"{source}\",\n")
        f.write("};\n")
        f.write("static const u8 g_wall_alt_palette_rgb[WALL_ALT_TEXTURE_COUNT][WALL_ALT_PALETTE_COLORS][3] = {\n")
        for palette in wall_alt_palettes:
            f.write("    {\n")
            for rgb in palette:
                r, g, b = to_neo_rgb(rgb)
                f.write(f"        {{{r},{g},{b}}},\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write("#define DOOR_PALETTE_COLORS 15\n")
        f.write(f"#define DOOR_TEXTURE_SOURCE \"{door_source}\"\n")
        f.write("static const u8 g_door_palette_rgb[DOOR_PALETTE_COLORS][3] = {\n")
        for rgb in door_palette:
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
        f.write("#define HUD_SMALL_DIGIT_PALETTE_COLORS 15\n")
        f.write(f"#define HUD_SMALL_DIGIT_SOURCE \"{hud_small_digit_source}\"\n")
        f.write("static const u8 g_hud_small_digit_palette_rgb[HUD_SMALL_DIGIT_PALETTE_COLORS][3] = {\n")
        for rgb in hud_small_digit_palette:
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
        f.write("};\n\n")
        f.write("#define ENEMY_PALETTE_COLORS 15\n")
        f.write(f"#define ENEMY_SPRITE_COUNT {len(sprite_defs)}\n")
        f.write("static const u8 g_enemy_palette_rgb[ENEMY_SPRITE_COUNT][ENEMY_PALETTE_COLORS][3] = {\n")
        for palette in sprite_palettes:
            f.write("    {\n")
            for rgb in palette:
                r, g, b = to_neo_rgb(rgb)
                f.write(f"        {{{r},{g},{b}}},\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write("typedef struct DoomSpriteScale {\n")
        f.write("    u16 tile_base;\n")
        f.write("    u8 strips;\n")
        f.write("    u8 rows;\n")
        f.write("    u8 width;\n")
        f.write("    u8 height;\n")
        f.write("} DoomSpriteScale;\n\n")
        f.write("typedef struct DoomEnemySpriteDef {\n")
        f.write("    u16 thing_type;\n")
        f.write("    u8 first_scale;\n")
        f.write("    u8 scale_count;\n")
        f.write("} DoomEnemySpriteDef;\n\n")
        f.write("static const DoomEnemySpriteDef g_enemy_sprite_defs[ENEMY_SPRITE_COUNT] = {\n")
        for thing_type, first_scale, scale_count, _frame in sprite_defs:
            f.write(f"    {{{thing_type},{first_scale},{scale_count}}},\n")
        f.write("};\n\n")
        f.write(f"#define ENEMY_SCALE_COUNT {len(sprite_meta)}\n")
        f.write("static const DoomSpriteScale g_enemy_scales[ENEMY_SCALE_COUNT] = {\n")
        for _name, _scale, base, strips, rows, width, height in sprite_meta:
            f.write(f"    {{{base},{strips},{rows},{width},{height}}},\n")
        f.write("};\n\n#endif /* DOOM_GFX_GENERATED_H */\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iwad", help="Path to a WAD or Freedoom release zip for offline texture conversion")
    ap.add_argument("--zip-member", help="WAD member inside a zip archive")
    ap.add_argument("--wall-texture", default=WALL_TEXTURE, help="Doom wall texture to precompose into C-ROM tiles")
    ap.add_argument("--wall-alt-textures", default=",".join(WALL_ALT_TEXTURES), help="Comma-separated extra Doom wall atlases for per-cell map texture classes")
    ap.add_argument("--door-texture", default=DOOR_TEXTURE, help="Doom door texture to precompose into the second wall atlas")
    ap.add_argument("--map", default="E1M1", help="Doom map used to select player-start floor and ceiling flats")
    ap.add_argument("--palette-header", help="Generated wall palette header")
    ap.add_argument("--weapon-frames", default=",".join(WEAPON_FRAMES), help="Comma-separated Doom weapon patch frames")
    ap.add_argument("--face-frames", default=",".join(HUD_FACE_FRAMES), help="Comma-separated Doom status face patch frames")
    ap.add_argument("--face-tune-grid", action="store_true", help="Bake face probes and STFST00 orientation variants into the face frame slots")
    ap.add_argument("--out-dir", default=None, help="Directory for generated c1/c2/s1/m1/v1 ROM blobs")
    ap.add_argument("--sprite-frame", default="TROOA1", help="Doom sprite patch frame to pre-scale into C-ROM strips")
    ap.add_argument("--monster-sprites", default="3004:POSSA1,3004:POSSB1,9:SPOSA1,9:SPOSB1,3001:TROOA1,3001:TROOB1,3002:SARGA1,3002:SARGB1,58:SARGA1,58:SARGB1,3003:BOSSA1,3003:BOSSB1,5:BKEYA0,6:YKEYA0,13:RKEYA0,38:RSKUA0,39:YSKUA0,40:BSKUA0,8:BPAKA0,2001:SHOTA0,2002:MGUNA0,2003:LAUNA0,2004:PLASA0,2005:CSAWA0,2006:BFUGA0,2007:CLIPA0,2008:SHELA0,2010:ROCKA0,2011:STIMA0,2012:MEDIA0,2013:SOULA0,2014:BON1A0,2015:BON2A0,2018:ARM1A0,2019:ARM2A0,17:CELPA0,2035:BAR1A0,2046:BROKA0,2047:CELLA0,9000:BEXPC0,2048:AMMOA0,9001:POSSL0,9002:SPOSL0,9003:TROOR0,9004:SARGN0,9005:BOSSO0,9006:BAL1A0,9007:BAL7A1A5", help="Comma-separated Doom thing_type:sprite_frame pairs")
    ap.add_argument("--sprite-scales", default="1.00,0.75,0.50,0.33,0.25", help="Comma-separated sprite scale levels")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    out = args.out_dir if args.out_dir else os.path.join(here, "..", "rom")
    os.makedirs(out, exist_ok=True)

    wall_tiles, wall_source, wall_palette = wall_texture_tiles(args.iwad, args.zip_member, args.wall_texture)
    wall_alt_specs = [item.strip().upper() for item in args.wall_alt_textures.split(",") if item.strip()]
    if len(wall_alt_specs) != len(WALL_ALT_TEXTURES):
        raise ValueError(f"expected {len(WALL_ALT_TEXTURES)} alt wall textures, got {len(wall_alt_specs)}")
    wall_alt_results = [wall_texture_tiles(args.iwad, args.zip_member, texture) for texture in wall_alt_specs]
    wall_alt_tilesets = [result[0] for result in wall_alt_results]
    wall_alt_sources = [result[1] for result in wall_alt_results]
    wall_alt_palettes = [result[2] for result in wall_alt_results]
    door_tiles, door_source, door_palette = wall_texture_tiles(args.iwad, args.zip_member, args.door_texture)
    hud_tiles, hud_source, hud_palette, hud_w, hud_h = patch_grid_tiles(args.iwad, args.zip_member, "STBAR", HUD_COLS, HUD_ROWS)
    face_frames = [item.strip().upper() for item in args.face_frames.split(",") if item.strip()]
    if len(face_frames) != len(HUD_FACE_FRAMES):
        raise ValueError(f"expected {len(HUD_FACE_FRAMES)} status face frames, got {len(face_frames)}")
    face_tiles, face_source = hud_face_tiles(args.iwad, args.zip_member, face_frames, hud_palette, args.face_tune_grid)
    ceiling_flat, floor_flat = map_start_flats(args.iwad, args.zip_member, args.map)
    ceiling_source, ceiling_palette, ceiling_tiles = flat_texture_tiles(args.iwad, args.zip_member, ceiling_flat, ceiling=True)
    floor_source, floor_palette, floor_tiles = flat_texture_tiles(args.iwad, args.zip_member, floor_flat)
    ceiling_perspective_tiles = perspective_plane_tiles(args.iwad, args.zip_member, ceiling_flat, ceiling_palette, ceiling=True)
    floor_perspective_tiles = perspective_plane_tiles(args.iwad, args.zip_member, floor_flat, floor_palette)
    weapon_frames = [item.strip().upper() for item in args.weapon_frames.split(",") if item.strip()]
    weapon_cache, weapon_source, weapon_palette, weapon_w, weapon_h = weapon_tiles(args.iwad, args.zip_member, weapon_frames)
    hud_key_tiles, hud_key_source = hud_keycard_tiles(args.iwad, args.zip_member)
    hud_digit_cache, hud_digit_source = hud_digit_tiles(args.iwad, args.zip_member, hud_palette)
    hud_small_digit_cache, hud_small_digit_palette, hud_small_digit_source = hud_small_digit_tiles(args.iwad, args.zip_member)
    scales = [float(item) for item in args.sprite_scales.split(",") if item.strip()]
    monster_specs = parse_monster_sprites(args.monster_sprites or f"3001:{args.sprite_frame}")
    sprite_tiles, sprite_defs, sprite_meta, sprite_palettes = monster_sprite_tiles(args.iwad, args.zip_member, monster_specs, scales)
    if args.palette_header:
        write_palette_header(
            args.palette_header,
            wall_palette,
            wall_source,
            wall_alt_palettes,
            wall_alt_sources,
            door_palette,
            door_source,
            hud_palette,
            hud_source,
            hud_small_digit_palette,
            hud_small_digit_source,
            ceiling_palette,
            ceiling_source,
            floor_palette,
            floor_source,
            weapon_palette,
            weapon_source,
            sprite_defs,
            sprite_meta,
            sprite_palettes,
        )
    tiles = [tile_blank(), wall_tiles[0], tile_solid()] + wall_tiles[1:]
    for wall_alt_tiles in wall_alt_tilesets:
        tiles += wall_alt_tiles[1:]
    tiles += (
        door_tiles[1:]
        + ceiling_tiles
        + floor_tiles
        + hud_tiles
        + face_tiles
        + weapon_cache
        + hud_key_tiles
        + hud_digit_cache
        + hud_small_digit_cache
        + ceiling_perspective_tiles
        + floor_perspective_tiles
        + sprite_tiles
    )
    assert len(tiles) >= WALL_ATLAS_BASE + WALL_ATLAS_TILES
    c1, c2 = bytearray(), bytearray()
    for t in tiles:
        a, b = encode_tile(t)
        c1 += a
        c2 += b
    assert len(c1) == len(tiles) * 64 and len(c2) == len(tiles) * 64

    if len(c1) > C_PAD or len(c2) > C_PAD:
        raise ValueError(f"C-ROM tile data exceeds configured pad: c1={len(c1):#x} c2={len(c2):#x} pad={C_PAD:#x}")
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
    for tile in doom_status_digit_fix_tiles(args.iwad, args.zip_member, hud_palette):
        s1 += tile
    for glyph in DIGIT_GLYPHS:
        s1 += encode_fix_glyph(glyph)
    s1 += encode_fix_glyph(CROSSHAIR_GLYPH)
    for glyph in EXIT_GLYPHS:
        s1 += encode_fix_glyph(glyph)
    for glyph in DEAD_GLYPHS:
        s1 += encode_fix_glyph(glyph)
    for glyph in KEY_GLYPHS:
        s1 += encode_fix_glyph(glyph)
    s1 += encode_fix_glyph(KEY_MSG_K_GLYPH)
    for glyph in AMMO_GLYPHS:
        s1 += encode_fix_glyph(glyph)
    for glyph in SECRET_GLYPHS:
        s1 += encode_fix_glyph(glyph)
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
    print(f"  HUD keycards: {hud_key_source} tiles={HUD_KEYCARD_BASE}..{HUD_KEYCARD_BASE + HUD_KEYCARD_TILES - 1}")
    print(f"  HUD digits: {hud_digit_source} tiles={HUD_DIGIT_BASE}..{HUD_DIGIT_BASE + HUD_DIGIT_TILES - 1}")
    print(f"  HUD small digits: {hud_small_digit_source} tiles={HUD_SMALL_DIGIT_BASE}..{HUD_SMALL_DIGIT_BASE + HUD_SMALL_DIGIT_TILES - 1}")
    for idx, source in enumerate(wall_alt_sources):
        base = WALL_ALT_ATLAS_BASE + idx * WALL_ATLAS_TILES
        print(f"  alt wall texture {idx + 1}: {source} atlas={base}..{base + WALL_ATLAS_TILES - 1} ({WALL_ATLAS_COLS}x{WALL_ATLAS_ROWS})")
    print(f"  door texture: {door_source} atlas={DOOR_ATLAS_BASE}..{DOOR_ATLAS_BASE + WALL_ATLAS_TILES - 1} ({WALL_ATLAS_COLS}x{WALL_ATLAS_ROWS})")
    print(f"  ceiling flat: {ceiling_source} tile={CEILING_FLAT_BASE}..{CEILING_FLAT_BASE + FLAT_TILES - 1} ({FLAT_COLS}x{FLAT_ROWS})")
    print(f"  floor flat: {floor_source} tile={FLOOR_FLAT_BASE}..{FLOOR_FLAT_BASE + FLAT_TILES - 1} ({FLAT_COLS}x{FLAT_ROWS})")
    print(f"  ceiling perspective: tile={CEILING_PERSPECTIVE_BASE}..{CEILING_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES - 1} ({PLANE_PERSPECTIVE_DIRS} dirs x {PLANE_PERSPECTIVE_PHASES}x{PLANE_PERSPECTIVE_PHASES} phases x {PLANE_PERSPECTIVE_ROWS}x{PLANE_PERSPECTIVE_COLS})")
    print(f"  floor perspective: tile={FLOOR_PERSPECTIVE_BASE}..{FLOOR_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES - 1} ({PLANE_PERSPECTIVE_DIRS} dirs x {PLANE_PERSPECTIVE_PHASES}x{PLANE_PERSPECTIVE_PHASES} phases x {PLANE_PERSPECTIVE_ROWS}x{PLANE_PERSPECTIVE_COLS})")
    print(f"  hud patch: {hud_source} tile={HUD_BASE}..{HUD_BASE + HUD_TILES - 1} ({HUD_COLS}x{HUD_ROWS}) source={hud_w}x{hud_h}")
    print(f"  hud faces: {face_source} tile={HUD_FACE_BASE}..{HUD_FACE_BASE + len(face_frames) * HUD_FACE_TILES - 1} ({HUD_FACE_COLS}x{HUD_FACE_ROWS}x{len(face_frames)})")
    print(f"  weapon frames: {weapon_source} tile={WEAPON_BASE}..{WEAPON_BASE + len(weapon_frames) * WEAPON_TILES - 1} ({WEAPON_STRIPS}x{WEAPON_ROWS}x{len(weapon_frames)}) source={weapon_w}x{weapon_h}")
    for thing_type, first_scale, scale_count, frame in sprite_defs:
        print(f"  monster sprite: thing={thing_type} frame={frame} scales={first_scale}..{first_scale + scale_count - 1}")
    for name, scale, base, strips, rows, width, height in sprite_meta:
        print(f"    sprite frame: {name} scale={scale:.2f} tile={base} strips={strips} rows={rows} size={width}x{height}")
    print(f"  tiles encoded: blank wall-cache solid "
          f"({len(tiles)*128} C-ROM bytes used)")


if __name__ == "__main__":
    main()
