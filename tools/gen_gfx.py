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
WALL_ALT_TEXTURES = ("BROWNGRN", "BROWN1", "SUPPORT2", "SLADWALL", "COMPTALL", "COMPTILE", "BROWN144")
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
PLANE_TEXEL_Q8_DIV = 1
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
TITLEPIC_BASE = FLOOR_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES
TITLEPIC_COLS = 20
TITLEPIC_ROWS = 13
TITLEPIC_TILES = TITLEPIC_COLS * TITLEPIC_ROWS
SPRITE_CACHE_BASE = TITLEPIC_BASE + TITLEPIC_TILES
# Must match main.c/config.h's weapon sprite-chain top. If this differs, the
# correct Doom psprite is baked into the wrong part of the visible tile window.
WEAPON_SCREEN_TOP = 192 - WEAPON_ROWS * 16
WEAPON_SCREEN_LEFT = (320 - WEAPON_STRIPS * 16) // 2
DOOM_PSPR_SX = 1
DOOM_PSPR_SY = 32
WEAPON_BAKE_Y_ADJUST = 0
WEAPON_CENTERED_FRAMES = {"SHTGB0", "SHTGC0", "SHTGD0"}


def recompute_layout() -> None:
    global WALL_ATLAS_TILES, WALL_ALT_ATLAS_BASE, DOOR_ATLAS_BASE
    global PLANE_PERSPECTIVE_TILES, CEILING_FLAT_BASE, FLOOR_FLAT_BASE
    global HUD_BASE, HUD_FACE_BASE, WEAPON_BASE, HUD_KEYCARD_BASE
    global HUD_DIGIT_BASE, HUD_SMALL_DIGIT_BASE, CEILING_PERSPECTIVE_BASE
    global FLOOR_PERSPECTIVE_BASE, TITLEPIC_BASE, SPRITE_CACHE_BASE

    WALL_ATLAS_TILES = WALL_ATLAS_COLS * WALL_ATLAS_ROWS
    WALL_ALT_ATLAS_BASE = WALL_ATLAS_BASE + WALL_ATLAS_TILES
    DOOR_ATLAS_BASE = WALL_ALT_ATLAS_BASE + len(WALL_ALT_TEXTURES) * WALL_ATLAS_TILES
    PLANE_PERSPECTIVE_TILES = (
        PLANE_PERSPECTIVE_DIRS
        * PLANE_PERSPECTIVE_PHASES
        * PLANE_PERSPECTIVE_PHASES
        * PLANE_PERSPECTIVE_ROWS
        * PLANE_PERSPECTIVE_COLS
    )
    CEILING_FLAT_BASE = DOOR_ATLAS_BASE + WALL_ATLAS_TILES
    FLOOR_FLAT_BASE = CEILING_FLAT_BASE + FLAT_TILES
    HUD_BASE = FLOOR_FLAT_BASE + FLAT_TILES
    HUD_FACE_BASE = HUD_BASE + HUD_TILES
    WEAPON_BASE = HUD_FACE_BASE + len(HUD_FACE_FRAMES) * HUD_FACE_TILES
    HUD_KEYCARD_BASE = WEAPON_BASE + len(WEAPON_FRAMES) * WEAPON_TILES
    HUD_DIGIT_BASE = HUD_KEYCARD_BASE + HUD_KEYCARD_TILES
    HUD_SMALL_DIGIT_BASE = HUD_DIGIT_BASE + HUD_DIGIT_TILES
    CEILING_PERSPECTIVE_BASE = HUD_SMALL_DIGIT_BASE + HUD_SMALL_DIGIT_TILES
    FLOOR_PERSPECTIVE_BASE = CEILING_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES
    TITLEPIC_BASE = FLOOR_PERSPECTIVE_BASE + PLANE_PERSPECTIVE_TILES
    SPRITE_CACHE_BASE = TITLEPIC_BASE + TITLEPIC_TILES


def apply_detail_layout(detail: str) -> None:
    global WALL_ATLAS_COLS, PLANE_PERSPECTIVE_DIRS

    if detail == "clarity":
        WALL_ATLAS_COLS = 32
        PLANE_PERSPECTIVE_DIRS = 4
    else:
        WALL_ATLAS_COLS = 16
        PLANE_PERSPECTIVE_DIRS = 16
    recompute_layout()


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
KEY_MSG_Y_GLYPH = (0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x00)
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


def shade_rgb(rgb, scale):
    return tuple(max(0, min(255, int(round(channel * scale)))) for channel in rgb)


def mix_rgb(a, b, amount):
    inv = 1.0 - amount
    return tuple(max(0, min(255, int(round(a[i] * inv + b[i] * amount)))) for i in range(3))


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
    src_x0 = tex_col * width / cols
    src_x1 = (tex_col + 1) * width / cols
    tile = [[0] * 16 for _ in range(16)]
    for y in range(16):
        ty = min(height - 1, int(((tex_row * 16) + y + 0.5) * height / (rows * 16)))
        for x in range(16):
            tx = min(width - 1, int(src_x0 + (x + 0.5) * (src_x1 - src_x0) / 16))
            tile[y][x] = quantize_color(texture[ty][tx], playpal, palette)
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


def normalize_floor_palette(palette):
    normalized = []
    for r, g, b in palette:
        if g > r + 12 and g > b + 12:
            lum = int(round(r * 0.30 + g * 0.59 + b * 0.11))
            normalized.append((
                min(255, int(lum * 1.06)),
                min(255, int(lum * 0.98)),
                min(255, int(lum * 0.78)),
            ))
        else:
            normalized.append((r, g, b))
    return normalized


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
    else:
        palette = normalize_floor_palette(palette)
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
    base_rgb = tuple(sum(rgb[i] for rgb in palette) // len(palette) for i in range(3))
    floor_blend = (42, 39, 34)
    ceiling_blend = (32, 35, 58)

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
                            row_ratio = min(1.0, p / 96.0)
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
                                rgb = playpal[flat[sy][sx]]
                                if ceiling:
                                    rgb = shade_rgb(mix_rgb(rgb, ceiling_blend, 0.28), 0.62 + row_ratio * 0.22)
                                else:
                                    rgb = shade_rgb(mix_rgb(rgb, base_rgb, 0.22), 0.45 + row_ratio * 0.38)
                                    rgb = mix_rgb(rgb, floor_blend, 0.12)
                                tile[y][x] = quantize_rgb(rgb, palette)
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


def titlepic_tiles(iwad, zip_member):
    if not iwad:
        palette = [(6, 6, 8), (54, 40, 26), (118, 26, 18), (170, 146, 74), (210, 198, 150)] * 3
        return [tile_solid() for _ in range(TITLEPIC_TILES)], "fallback-title", palette, TITLEPIC_COLS * 16, 200
    try:
        return patch_grid_tiles(iwad, zip_member, "TITLEPIC", TITLEPIC_COLS, TITLEPIC_ROWS)
    except ValueError:
        palette = [(6, 6, 8), (54, 40, 26), (118, 26, 18), (170, 146, 74), (210, 198, 150)] * 3
        return [tile_solid() for _ in range(TITLEPIC_TILES)], "fallback-title", palette, TITLEPIC_COLS * 16, 200


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


def closest_playpal_index(playpal, rgb):
    r, g, b = rgb
    return min(
        range(len(playpal)),
        key=lambda i: (
            (playpal[i][0] - r) ** 2
            + (playpal[i][1] - g) ** 2
            + (playpal[i][2] - b) ** 2
        ),
    )


def synthetic_weapon_color_set(playpal):
    return {
        "black": closest_playpal_index(playpal, (8, 8, 8)),
        "dark": closest_playpal_index(playpal, (42, 42, 42)),
        "mid": closest_playpal_index(playpal, (96, 96, 90)),
        "light": closest_playpal_index(playpal, (168, 160, 145)),
        "white": closest_playpal_index(playpal, (230, 224, 196)),
        "green_dark": closest_playpal_index(playpal, (10, 82, 25)),
        "green": closest_playpal_index(playpal, (30, 180, 58)),
        "green_hot": closest_playpal_index(playpal, (120, 255, 115)),
        "yellow": closest_playpal_index(playpal, (255, 220, 90)),
    }


def synthetic_weapon_patch(part_name, playpal):
    name = part_name.upper()
    colors = synthetic_weapon_color_set(playpal)

    def blank(width, height):
        return [[-1] * width for _ in range(height)]

    def rect(patch, x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(len(patch), y1)):
            row = patch[y]
            for x in range(max(0, x0), min(len(row), x1)):
                row[x] = color

    def ellipse(patch, cx, cy, rx, ry, color):
        if rx <= 0 or ry <= 0:
            return
        for y in range(max(0, cy - ry), min(len(patch), cy + ry + 1)):
            row = patch[y]
            for x in range(max(0, cx - rx), min(len(row), cx + rx + 1)):
                dx = x - cx
                dy = y - cy
                if dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry:
                    row[x] = color

    def line_diag(patch, x0, y0, length, slope, width, color):
        for i in range(length):
            x = x0 + i
            y = y0 + (i * slope) // 16
            rect(patch, x, y, x + width, y + width, color)

    if name.startswith("PLSF"):
        patch = blank(42, 34)
        ellipse(patch, 21, 16, 18, 11, colors["green"])
        ellipse(patch, 21, 16, 10, 6, colors["green_hot"])
        rect(patch, 5, 15, 37, 19, colors["yellow"])
        rect(patch, 19, 3, 23, 31, colors["yellow"])
        return patch, -139, -70

    if name.startswith("BFGF"):
        patch = blank(58, 42)
        ellipse(patch, 29, 20, 26, 15, colors["green"])
        ellipse(patch, 29, 20, 15, 8, colors["green_hot"])
        rect(patch, 4, 18, 54, 24, colors["yellow"])
        rect(patch, 26, 4, 32, 38, colors["yellow"])
        return patch, -131, -72

    if name.startswith("PLSG"):
        patch = blank(78, 70)
        alt = "B" in name
        rect(patch, 24, 10, 54, 42, colors["dark"])
        rect(patch, 29, 14, 49, 37, colors["mid"])
        rect(patch, 34, 16, 44, 34, colors["green_dark"])
        rect(patch, 37, 18, 47 if alt else 43, 32, colors["green"])
        rect(patch, 16, 42, 62, 58, colors["dark"])
        rect(patch, 21, 46, 57, 54, colors["mid"])
        rect(patch, 30, 55, 48, 68, colors["black"])
        rect(patch, 34, 58, 44, 69, colors["dark"])
        line_diag(patch, 8, 47, 24, -5, 5, colors["light"])
        line_diag(patch, 46, 40, 24, 5, 5, colors["light"])
        rect(patch, 31, 9, 47, 13, colors["light"])
        rect(patch, 35, 17, 43, 20, colors["green_hot"])
        return patch, -121, -112

    if name.startswith("BFGG"):
        patch = blank(98, 78)
        alt = "B" in name
        rect(patch, 14, 22, 84, 58, colors["dark"])
        rect(patch, 22, 17, 76, 47, colors["mid"])
        rect(patch, 31, 21, 67, 43, colors["light"])
        rect(patch, 38, 24, 60, 40, colors["green_dark"])
        rect(patch, 42, 27, 64 if alt else 56, 37, colors["green"])
        rect(patch, 8, 34, 26, 64, colors["black"])
        rect(patch, 72, 34, 90, 64, colors["black"])
        rect(patch, 20, 58, 78, 73, colors["dark"])
        rect(patch, 30, 63, 68, 76, colors["black"])
        ellipse(patch, 49, 32, 12, 7, colors["green_hot"] if alt else colors["green"])
        rect(patch, 30, 17, 68, 21, colors["white"])
        return patch, -113, -116

    return None


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
                synthetic = synthetic_weapon_patch(part_name, playpal)
                if synthetic:
                    patch, left, top = synthetic
                    frame_patches.append((f"{part_name}:synthetic-shareware-fallback", patch, left, top))
                    continue
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
    for frame_name, frame_patches in frames:
        canvas = [[-1] * dst_w for _ in range(dst_h)]

        for _name, patch, left, top in frame_patches:
            # Doom psprites are positioned from a screen-space anchor and each
            # patch's own left/top offsets. Bake that convention offline so the
            # 68000 only swaps complete tile frames at runtime.
            screen_x = DOOM_PSPR_SX - left
            screen_y = DOOM_PSPR_SY - top
            x0 = screen_x - WEAPON_SCREEN_LEFT
            y0 = screen_y - WEAPON_SCREEN_TOP + WEAPON_BAKE_Y_ADJUST
            if frame_name in WEAPON_CENTERED_FRAMES:
                x0 = (dst_w - len(patch[0])) // 2

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
        palette = [(0, 0, 160), (160, 0, 0), (160, 128, 0)] * 5
        return [tile_solid() for _ in HUD_KEYCARD_FRAMES], "fallback-keycards", palette

    wad = Wad(read_wad(iwad, zip_member))
    playpal = playpal_rgb(wad)
    patches = []
    sources = []
    for frame in HUD_KEYCARD_FRAMES:
        lump_ids = wad.by_name.get(frame)
        if not lump_ids:
            patches.append(None)
            sources.append(f"{frame}:missing")
            continue
        patch = decode_patch(wad.lump_data(lump_ids[0]))
        patches.append(patch)
        sources.append(frame)

    palette_src = [row for patch in patches if patch for row in patch]
    palette = texture_palette(palette_src, playpal)
    tiles = []
    for patch in patches:
        tile = [[0] * 16 for _ in range(16)]
        if patch:
            # Doom's keycard pickup patches are about 14x16 pixels, but the
            # status bar key cells are much smaller.  Pre-shrink them here so
            # the 68000 can still draw one Neo Geo sprite per owned key without
            # covering the armor label or ammo counter table.
            src_h = len(patch)
            src_w = len(patch[0]) if src_h else 0
            dst_w = 8
            dst_h = 8
            dst_x0 = 4
            dst_y0 = 4
            for y in range(dst_h):
                sy = min(src_h - 1, (y * src_h + src_h // 2) // dst_h)
                for x in range(dst_w):
                    sx = min(src_w - 1, (x * src_w + src_w // 2) // dst_w)
                    color = patch[sy][sx]
                    if color >= 0:
                        tile[dst_y0 + y][dst_x0 + x] = quantize_color(color, playpal, palette)
        tiles.append(tile)
    return tiles, "+".join(sources), palette


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


def sprite_scale_tiles(iwad, zip_member, sprite_name, scales, start_tile, flip_x=False):
    if not iwad:
        return [], [], [], start_tile

    wad = Wad(read_wad(iwad, zip_member))
    sprite_name = sprite_name.upper()
    lump_ids = wad.by_name.get(sprite_name)
    if not lump_ids:
        return [], [], [], start_tile
    data = wad.lump_data(lump_ids[0])
    _patch_w, _patch_h, left_offset, top_offset = patch_header(data)
    patch = decode_patch(data)
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
        origin_x_src = (src_w - left_offset) if flip_x else left_offset
        origin_x = int(round(origin_x_src * scale))
        origin_y = int(round(top_offset * scale))
        meta.append((sprite_name, scale, next_tile, strips, rows, dst_w, dst_h, origin_x, origin_y))
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
                        if flip_x:
                            sx = src_w - 1 - sx
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
        angle = -1
        angle_explicit = False
        if "@" in thing_type:
            thing_type, angle_spec = thing_type.split("@", 1)
            angle = int(angle_spec)
            angle_explicit = True
        frame = frame.strip().upper()
        suffix = frame[4:]
        if angle < 0:
            angle = next((int(ch) for ch in suffix if ch.isdigit()), 0)
        result.append((int(thing_type), angle, frame, False))
        if len(suffix) >= 4 and suffix[0] == suffix[2] and suffix[1].isdigit() and suffix[3].isdigit():
            base_angle = angle - int(suffix[1]) if angle_explicit else 0
            mirror_angle = int(suffix[3])
            mirror_angle = base_angle + mirror_angle
            if mirror_angle != angle:
                result.append((int(thing_type), mirror_angle, frame, True))
    return result


def monster_sprite_tiles(iwad, zip_member, specs, scales):
    tiles = []
    defs = []
    metas = []
    palettes = []
    next_tile = SPRITE_CACHE_BASE
    for thing_type, angle, frame, flip_x in specs:
        first_scale = len(metas)
        frame_tiles, frame_meta, palette, next_tile = sprite_scale_tiles(iwad, zip_member, frame, scales, next_tile, flip_x=flip_x)
        if not frame_meta:
            continue
        tiles.extend(frame_tiles)
        metas.extend(frame_meta)
        palettes.append(palette)
        defs.append((thing_type, angle, first_scale, len(frame_meta), frame + ("-mirror" if flip_x else "")))
    return tiles, defs, metas, palettes


def write_palette_header(path, wall_palette, wall_source, wall_alt_palettes, wall_alt_sources, door_palette, door_source, hud_palette, hud_source, hud_key_palette, hud_key_source, hud_small_digit_palette, hud_small_digit_source, ceiling_palette, ceiling_source, floor_palette, floor_source, title_palette, title_source, weapon_palette, weapon_source, sprite_defs, sprite_meta, sprite_palettes):
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
        f.write("#define HUD_KEY_PALETTE_COLORS 15\n")
        f.write(f"#define HUD_KEY_SOURCE \"{hud_key_source}\"\n")
        f.write("static const u8 g_hud_key_palette_rgb[HUD_KEY_PALETTE_COLORS][3] = {\n")
        for rgb in hud_key_palette:
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
        f.write("#define TITLE_PALETTE_COLORS 15\n")
        f.write(f"#define TITLEPIC_SOURCE \"{title_source}\"\n")
        f.write("static const u8 g_title_palette_rgb[TITLE_PALETTE_COLORS][3] = {\n")
        for rgb in title_palette:
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
        f.write("    s16 origin_x;\n")
        f.write("    s16 origin_y;\n")
        f.write("} DoomSpriteScale;\n\n")
        f.write("typedef struct DoomEnemySpriteDef {\n")
        f.write("    u16 thing_type;\n")
        f.write("    u8 angle;\n")
        f.write("    u16 first_scale;\n")
        f.write("    u8 scale_count;\n")
        f.write("} DoomEnemySpriteDef;\n\n")
        f.write("static const DoomEnemySpriteDef g_enemy_sprite_defs[ENEMY_SPRITE_COUNT] = {\n")
        for thing_type, angle, first_scale, scale_count, _frame in sprite_defs:
            f.write(f"    {{{thing_type},{angle},{first_scale},{scale_count}}},\n")
        f.write("};\n\n")
        f.write(f"#define ENEMY_SCALE_COUNT {len(sprite_meta)}\n")
        f.write("static const DoomSpriteScale g_enemy_scales[ENEMY_SCALE_COUNT] = {\n")
        for _name, _scale, base, strips, rows, width, height, origin_x, origin_y in sprite_meta:
            f.write(f"    {{{base},{strips},{rows},{width},{height},{origin_x},{origin_y}}},\n")
        f.write("};\n\n#endif /* DOOM_GFX_GENERATED_H */\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iwad", help="Path to a WAD or Freedoom release zip for offline texture conversion")
    ap.add_argument("--zip-member", help="WAD member inside a zip archive")
    ap.add_argument("--wall-texture", default=WALL_TEXTURE, help="Doom wall texture to precompose into C-ROM tiles")
    ap.add_argument("--wall-alt-textures", default=",".join(WALL_ALT_TEXTURES), help="Comma-separated extra Doom wall atlases for per-cell map texture classes")
    ap.add_argument("--door-texture", default=DOOR_TEXTURE, help="Doom door texture to precompose into the second wall atlas")
    ap.add_argument("--detail", choices=("clarity", "quality", "balanced", "speed"), default="quality", help="Tile layout tier; clarity doubles wall atlas sampling and trims plane direction cache")
    ap.add_argument("--map", default="E1M1", help="Doom map used to select player-start floor and ceiling flats")
    ap.add_argument("--palette-header", help="Generated wall palette header")
    ap.add_argument("--weapon-frames", default=",".join(WEAPON_FRAMES), help="Comma-separated Doom weapon patch frames")
    ap.add_argument("--face-frames", default=",".join(HUD_FACE_FRAMES), help="Comma-separated Doom status face patch frames")
    ap.add_argument("--face-tune-grid", action="store_true", help="Bake face probes and STFST00 orientation variants into the face frame slots")
    ap.add_argument("--out-dir", default=None, help="Directory for generated c1/c2/s1/m1/v1 ROM blobs")
    ap.add_argument("--sprite-frame", default="TROOA1", help="Doom sprite patch frame to pre-scale into C-ROM strips")
    default_monster_sprites = ",".join((
        "3004:POSSA1,3004:POSSA2A8,3004:POSSA3A7,3004:POSSA4A6,3004:POSSA5,3004:POSSB1,3004:POSSB2B8,3004:POSSB3B7,3004:POSSB4B6,3004:POSSB5",
        "9:SPOSA1,9:SPOSA2A8,9:SPOSA3A7,9:SPOSA4A6,9:SPOSA5,9:SPOSB1,9:SPOSB2B8,9:SPOSB3B7,9:SPOSB4B6,9:SPOSB5",
        "3001:TROOA1,3001:TROOA2A8,3001:TROOA3A7,3001:TROOA4A6,3001:TROOA5,3001:TROOB1,3001:TROOB2B8,3001:TROOB3B7,3001:TROOB4B6,3001:TROOB5",
        "3002:SARGA1,3002:SARGA2A8,3002:SARGA3A7,3002:SARGA4A6,3002:SARGA5,3002:SARGB1,3002:SARGB2B8,3002:SARGB3B7,3002:SARGB4B6,3002:SARGB5",
        "58:SARGA1,58:SARGA2A8,58:SARGA3A7,58:SARGA4A6,58:SARGA5,58:SARGB1,58:SARGB2B8,58:SARGB3B7,58:SARGB4B6,58:SARGB5",
        "3003:BOSSA1,3003:BOSSA2A8,3003:BOSSA3A7,3003:BOSSA4A6,3003:BOSSA5,3003:BOSSB1,3003:BOSSB2B8,3003:BOSSB3B7,3003:BOSSB4B6,3003:BOSSB5",
        "69:BOS2A1,69:BOS2A2A8,69:BOS2A3A7,69:BOS2A4A6,69:BOS2A5,69:BOS2B1,69:BOS2B2B8,69:BOS2B3B7,69:BOS2B4B6,69:BOS2B5",
        "3005:HEADA1,3005:HEADA2A8,3005:HEADA3A7,3005:HEADA4A6,3005:HEADA5,3005:HEADB1,3005:HEADB2B8,3005:HEADB3B7,3005:HEADB4B6,3005:HEADB5",
        "3006:SKULA1,3006:SKULA2A8,3006:SKULA3A7,3006:SKULA4A6,3006:SKULA5,3006:SKULB1,3006:SKULB2B8,3006:SKULB3B7,3006:SKULB4B6,3006:SKULB5",
        "65:CPOSA1,65:CPOSA2A8,65:CPOSA3A7,65:CPOSA4A6,65:CPOSA5,65:CPOSB1,65:CPOSB2B8,65:CPOSB3B7,65:CPOSB4B6,65:CPOSB5",
        "84:SSWVA1,84:SSWVA2A8,84:SSWVA3A7,84:SSWVA4A6,84:SSWVA5,84:SSWVB1,84:SSWVB2B8,84:SSWVB3B7,84:SSWVB4B6,84:SSWVB5",
        "66:SKELA1,66:SKELA2A8,66:SKELA3A7,66:SKELA4A6,66:SKELA5,66:SKELB1,66:SKELB2B8,66:SKELB3B7,66:SKELB4B6,66:SKELB5",
        "67:FATTA1,67:FATTA2A8,67:FATTA3A7,67:FATTA4A6,67:FATTA5,67:FATTB1,67:FATTB2B8,67:FATTB3B7,67:FATTB4B6,67:FATTB5",
        "68:BSPIA1,68:BSPIA2A8,68:BSPIA3A7,68:BSPIA4A6,68:BSPIA5,68:BSPIB1,68:BSPIB2B8,68:BSPIB3B7,68:BSPIB4B6,68:BSPIB5",
        "64:VILEA1,64:VILEA2A8,64:VILEA3A7,64:VILEA4A6,64:VILEA5,64:VILEB1,64:VILEB2B8,64:VILEB3B7,64:VILEB4B6,64:VILEB5",
        "71:PAINA1,71:PAINA2A8,71:PAINA3A7,71:PAINA4A6,71:PAINA5,71:PAINB1,71:PAINB2B8,71:PAINB3B7,71:PAINB4B6,71:PAINB5",
        "16:CYBRA1,16:CYBRA2A8,16:CYBRA3A7,16:CYBRA4A6,16:CYBRA5,16:CYBRB1,16:CYBRB2B8,16:CYBRB3B7,16:CYBRB4B6,16:CYBRB5",
        "7:SPIDA1,7:SPIDA2A8,7:SPIDA3A7,7:SPIDA4A6,7:SPIDA5,7:SPIDB1,7:SPIDB2B8,7:SPIDB3B7,7:SPIDB4B6,7:SPIDB5",
        "3004@101:POSSE1,3004@102:POSSE2E8,3004@103:POSSE3E7,3004@104:POSSE4E6,3004@105:POSSE5,3004@101:POSSF1,3004@102:POSSF2F8,3004@103:POSSF3F7,3004@104:POSSF4F6,3004@105:POSSF5",
        "9@101:SPOSE1,9@102:SPOSE2E8,9@103:SPOSE3E7,9@104:SPOSE4E6,9@105:SPOSE5,9@101:SPOSF1,9@102:SPOSF2F8,9@103:SPOSF3F7,9@104:SPOSF4F6,9@105:SPOSF5",
        "3001@101:TROOE1,3001@102:TROOE2E8,3001@103:TROOE3E7,3001@104:TROOE4E6,3001@105:TROOE5,3001@101:TROOF1,3001@102:TROOF2F8,3001@103:TROOF3F7,3001@104:TROOF4F6,3001@105:TROOF5",
        "3002@101:SARGE1,3002@102:SARGE2E8,3002@103:SARGE3E7,3002@104:SARGE4E6,3002@105:SARGE5,3002@101:SARGF1,3002@102:SARGF2F8,3002@103:SARGF3F7,3002@104:SARGF4F6,3002@105:SARGF5",
        "58@101:SARGE1,58@102:SARGE2E8,58@103:SARGE3E7,58@104:SARGE4E6,58@105:SARGE5,58@101:SARGF1,58@102:SARGF2F8,58@103:SARGF3F7,58@104:SARGF4F6,58@105:SARGF5",
        "3003@101:BOSSE1,3003@102:BOSSE2E8,3003@103:BOSSE3E7,3003@104:BOSSE4E6,3003@105:BOSSE5,3003@101:BOSSF1,3003@102:BOSSF2F8,3003@103:BOSSF3F7,3003@104:BOSSF4F6,3003@105:BOSSF5",
        "3004@201:POSSG1,3004@202:POSSG2G8,3004@203:POSSG3G7,3004@204:POSSG4G6,3004@205:POSSG5,9@201:SPOSG1,9@202:SPOSG2G8,9@203:SPOSG3G7,9@204:SPOSG4G6,9@205:SPOSG5",
        "3001@201:TROOG1,3001@202:TROOG2G8,3001@203:TROOG3G7,3001@204:TROOG4G6,3001@205:TROOG5,3002@201:SARGG1,3002@202:SARGG2G8,3002@203:SARGG3G7,3002@204:SARGG4G6,3002@205:SARGG5",
        "58@201:SARGG1,58@202:SARGG2G8,58@203:SARGG3G7,58@204:SARGG4G6,58@205:SARGG5,3003@201:BOSSG1,3003@202:BOSSG2G8,3003@203:BOSSG3G7,3003@204:BOSSG4G6,3003@205:BOSSG5",
        "3004@9:POSSE1,3004@9:POSSF1,9@9:SPOSE1,9@9:SPOSF1,3001@9:TROOE1,3001@9:TROOF1,3002@9:SARGE1,3002@9:SARGF1,58@9:SARGE1,58@9:SARGF1,3003@9:BOSSE1,3003@9:BOSSF1,69@9:BOS2E1,69@9:BOS2F1,3005@9:HEADC1,3005@9:HEADD1,65@9:CPOSE1,65@9:CPOSF1,84@9:SSWVE1,84@9:SSWVF1,66@9:SKELE1,66@9:SKELF1,67@9:FATTE1,67@9:FATTF1,68@9:BSPIC1,68@9:BSPID1,64@9:VILEG1,64@9:VILEH1,71@9:PAINE1,71@9:PAINF1,16@9:CYBRE1,16@9:CYBRF1,7@9:SPIDE1,7@9:SPIDF1",
        "3004@10:POSSG1,9@10:SPOSG1,3001@10:TROOG1,3002@10:SARGG1,58@10:SARGG1,3003@10:BOSSG1,69@10:BOS2G1,3005@10:HEADF1,65@10:CPOSG1,84@10:SSWVG1,66@10:SKELG1,67@10:FATTG1,68@10:BSPIE1,64@10:VILEE1,71@10:PAING1,16@10:CYBRG1,7@10:SPIDG1",
        "5:BKEYA0,6:YKEYA0,13:RKEYA0,38:RSKUA0,39:YSKUA0,40:BSKUA0,8:BPAKA0",
        "2001:SHOTA0,2002:MGUNA0,2003:LAUNA0,2004:PLASA0,2005:CSAWA0,2006:BFUGA0",
        "2007:CLIPA0,2008:SHELA0,2010:ROCKA0,2011:STIMA0,2012:MEDIA0,2013:SOULA0,2014:BON1A0,2015:BON2A0,2018:ARM1A0,2019:ARM2A0,17:CELPA0,2049:SBOXA0",
        "2022:PINVA0,2023:PSTRA0,2024:PINSA0,2025:SUITA0,2026:PMAPA0,2045:PVISA0",
        "2035:BAR1A0,2046:BROKA0,2047:CELLA0,9000:BEXPC0,2048:AMMOA0",
        "9001:POSSL0,9002:SPOSL0,9003:TROOR0,9004:SARGN0,9005:BOSSO0,9009:BOS2O0,9028:HEADL0",
        "9010:POSSH0,9011:SPOSH0,9012:TROOI0,9013:SARGI0,9014:BOSSI0",
        "9015:POSSI0,9016:SPOSI0,9017:TROOJ0,9018:SARGJ0,9019:BOSSJ0",
        "9020:POSSJ0,9021:SPOSJ0,9022:TROOK0,9023:SARGK0,9024:BOSSK0,9025:BOS2I0,9026:BOS2J0,9027:BOS2K0,9029:HEADG0,9030:HEADH0,9031:HEADI0",
        "9032:POSSK0,9033:SPOSK0,9034:TROOL0,9035:SARGL0,9036:BOSSL0",
        "9006:BAL1A0,9007:BAL7A1A5,9008:BAL2A0",
    ))
    ap.add_argument("--monster-sprites", default=default_monster_sprites, help="Comma-separated Doom thing_type:sprite_frame pairs")
    ap.add_argument("--sprite-scales", default="1.00,0.75,0.50,0.33,0.25", help="Comma-separated sprite scale levels")
    args = ap.parse_args()
    apply_detail_layout(args.detail)

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
    title_tiles, title_source, title_palette, title_w, title_h = titlepic_tiles(args.iwad, args.zip_member)
    weapon_frames = [item.strip().upper() for item in args.weapon_frames.split(",") if item.strip()]
    weapon_cache, weapon_source, weapon_palette, weapon_w, weapon_h = weapon_tiles(args.iwad, args.zip_member, weapon_frames)
    hud_key_tiles, hud_key_source, hud_key_palette = hud_keycard_tiles(args.iwad, args.zip_member)
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
            hud_key_palette,
            hud_key_source,
            hud_small_digit_palette,
            hud_small_digit_source,
            ceiling_palette,
            ceiling_source,
            floor_palette,
            floor_source,
            title_palette,
            title_source,
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
        + title_tiles
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
    s1 += encode_fix_glyph(KEY_MSG_Y_GLYPH)
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
    print(f"  titlepic: {title_source} tile={TITLEPIC_BASE}..{TITLEPIC_BASE + TITLEPIC_TILES - 1} ({TITLEPIC_COLS}x{TITLEPIC_ROWS}) source={title_w}x{title_h}")
    print(f"  hud patch: {hud_source} tile={HUD_BASE}..{HUD_BASE + HUD_TILES - 1} ({HUD_COLS}x{HUD_ROWS}) source={hud_w}x{hud_h}")
    print(f"  hud faces: {face_source} tile={HUD_FACE_BASE}..{HUD_FACE_BASE + len(face_frames) * HUD_FACE_TILES - 1} ({HUD_FACE_COLS}x{HUD_FACE_ROWS}x{len(face_frames)})")
    print(f"  weapon frames: {weapon_source} tile={WEAPON_BASE}..{WEAPON_BASE + len(weapon_frames) * WEAPON_TILES - 1} ({WEAPON_STRIPS}x{WEAPON_ROWS}x{len(weapon_frames)}) source={weapon_w}x{weapon_h}")
    for thing_type, angle, first_scale, scale_count, frame in sprite_defs:
        print(f"  monster sprite: thing={thing_type} angle={angle} frame={frame} scales={first_scale}..{first_scale + scale_count - 1}")
    for name, scale, base, strips, rows, width, height, origin_x, origin_y in sprite_meta:
        print(f"    sprite frame: {name} scale={scale:.2f} tile={base} strips={strips} rows={rows} size={width}x{height} origin={origin_x},{origin_y}")
    print(f"  tiles encoded: blank wall-cache solid "
          f"({len(tiles)*128} C-ROM bytes used)")


if __name__ == "__main__":
    main()
