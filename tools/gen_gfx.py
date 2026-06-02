#!/usr/bin/env python3
"""gen_gfx.py - emit the non-program ROMs for the raycaster cart.

Outputs into ./rom :
  c1.bin / c2.bin   sprite tiles (wall texture cache, a solid tile, a blank tile)
  s1.bin            fix layer ROM (all-blank: we never draw the fix layer)
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

from doom_convert import Wad, read_wad

C_PAD = 0x80000   # pad each C ROM to 512 KiB (well above any tile we use)
S_PAD = 0x20000   # 128 KiB fix ROM, all blank
M_PAD = 0x10000   # 64 KiB Z80 program, all 0x00 (NOP) -> silent
V_PAD = 0x10000   # 64 KiB ADPCM samples, empty

WALL_TEXTURE = "BROWN1"
WALL_MIP_TILE = 1
SOLID_TILE = 2
WALL_ATLAS_BASE = 3
WALL_ATLAS_TILES = 64
SPRITE_CACHE_BASE = WALL_ATLAS_BASE + WALL_ATLAS_TILES


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
    tiles = [sample_texture_tile(texture, playpal, palette, 0, 0, max(1, width // 8), max(1, height // 8))]
    for ty in range(8):
        for tx in range(8):
            tiles.append(sample_texture_tile(texture, playpal, palette, tx * width // 8, ty * height // 8, max(1, width // 8), max(1, height // 8)))
    return tiles, texture_name.upper(), palette


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


def write_palette_header(path, palette, source_name):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii") as f:
        f.write("/* Generated by tools/gen_gfx.py; do not edit by hand. */\n")
        f.write("#ifndef DOOM_GFX_GENERATED_H\n#define DOOM_GFX_GENERATED_H\n\n")
        f.write("#define WALL_PALETTE_COLORS 15\n")
        f.write(f"#define WALL_TEXTURE_SOURCE \"{source_name}\"\n")
        f.write("static const u8 g_wall_palette_rgb[WALL_PALETTE_COLORS][3] = {\n")
        for rgb in palette:
            r, g, b = to_neo_rgb(rgb)
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n#endif /* DOOM_GFX_GENERATED_H */\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iwad", help="Path to a WAD or Freedoom release zip for offline texture conversion")
    ap.add_argument("--zip-member", help="WAD member inside a zip archive")
    ap.add_argument("--wall-texture", default=WALL_TEXTURE, help="Doom wall texture to precompose into C-ROM tiles")
    ap.add_argument("--palette-header", help="Generated wall palette header")
    ap.add_argument("--sprite-frame", default="TROOA1", help="Doom sprite patch frame to pre-scale into C-ROM strips")
    ap.add_argument("--sprite-scales", default="1.00,0.75,0.50,0.33,0.25", help="Comma-separated sprite scale levels")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "rom")
    os.makedirs(out, exist_ok=True)

    wall_tiles, wall_source, wall_palette = wall_texture_tiles(args.iwad, args.zip_member, args.wall_texture)
    if args.palette_header:
        write_palette_header(args.palette_header, wall_palette, wall_source)
    scales = [float(item) for item in args.sprite_scales.split(",") if item.strip()]
    sprite_tiles, sprite_meta = sprite_scale_tiles(args.iwad, args.zip_member, args.sprite_frame, scales)
    tiles = [tile_blank(), wall_tiles[0], tile_solid()] + wall_tiles[1:] + sprite_tiles
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

    print(f"  wall texture: {wall_source} mip={WALL_MIP_TILE} atlas={WALL_ATLAS_BASE}..{WALL_ATLAS_BASE + WALL_ATLAS_TILES - 1}")
    for name, scale, base, strips, rows, width, height in sprite_meta:
        print(f"  sprite frame: {name} scale={scale:.2f} tile={base} strips={strips} rows={rows} size={width}x{height}")
    print(f"  tiles encoded: blank wall-cache solid "
          f"({len(tiles)*128} C-ROM bytes used)")


if __name__ == "__main__":
    main()
