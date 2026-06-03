#!/usr/bin/env python3
"""Preview how the Doom status face is baked and assembled for Neo Geo."""

import argparse
import os
import sys

from PIL import Image, ImageDraw

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from doom_convert import Wad, read_wad
import gen_gfx


def rgb_for_index(index, palette, transparent=(16, 16, 16)):
    if index <= 0:
        return transparent
    return palette[min(index - 1, len(palette) - 1)]


def tile_to_image(tile, palette):
    image = Image.new("RGB", (16, 16))
    pixels = image.load()
    for y, row in enumerate(tile):
        for x, value in enumerate(row):
            pixels[x, y] = rgb_for_index(value, palette)
    return image


def stbar_crop_to_image(stbar, playpal, x0, y0, width, height):
    image = Image.new("RGB", (width, height), (16, 16, 16))
    pixels = image.load()
    for y in range(height):
        sy = y0 + y
        if sy < 0 or sy >= len(stbar):
            continue
        for x in range(width):
            sx = x0 + x
            if 0 <= sx < len(stbar[sy]):
                color = stbar[sy][sx]
                if color >= 0:
                    pixels[x, y] = playpal[color]
    return image


def draw_label(draw, pos, text, fill=(0, 255, 48)):
    x, y = pos
    draw.rectangle((x - 1, y - 1, x + 7, y + 8), fill=(0, 0, 0))
    draw.text((x, y), text, fill=fill)


def paste_scaled(canvas, image, x, y, scale):
    if scale != 1:
        image = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
    canvas.paste(image, (x, y))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", default=".tools/assets/doom1.wad.zip", help="Doom IWAD path or zip")
    parser.add_argument("--zip-member", default=None, help="IWAD member name inside zip")
    parser.add_argument("--face", default="STFST00", help="Doom face patch/lump to preview")
    parser.add_argument("--out", default=".tools/screens/hud-face-preview.png", help="output PNG path")
    parser.add_argument("--scale", type=int, default=6, help="nearest-neighbor preview scale")
    args = parser.parse_args()

    wad = Wad(read_wad(args.iwad, args.zip_member))
    playpal = gen_gfx.playpal_rgb(wad)
    _hud_tiles, _hud_name, hud_palette, _src_w, _src_h = gen_gfx.patch_grid_tiles(
        args.iwad, args.zip_member, "STBAR", gen_gfx.HUD_COLS, gen_gfx.HUD_ROWS
    )
    face_tiles, _face_names = gen_gfx.hud_face_tiles(args.iwad, args.zip_member, (args.face,), hud_palette)
    stbar = gen_gfx.stbar_with_face(wad, args.face.upper())

    crop_x = gen_gfx.HUD_FACE_COL * 16
    crop_y = 0
    crop_w = gen_gfx.HUD_FACE_COLS * 16
    crop_h = gen_gfx.HUD_FACE_ROWS * 16
    source = stbar_crop_to_image(stbar, playpal, crop_x, crop_y, crop_w, crop_h)

    # Runtime layout: two Neo Geo sprite columns, each with two stacked tiles.
    assembled = Image.new("RGB", (crop_w, crop_h), (16, 16, 16))
    tile_images = [tile_to_image(tile, hud_palette) for tile in face_tiles]
    for row in range(gen_gfx.HUD_FACE_ROWS):
        for col in range(gen_gfx.HUD_FACE_COLS):
            index = row * gen_gfx.HUD_FACE_COLS + col
            assembled.paste(tile_images[index], (col * 16, row * 16))

    labels = Image.new("RGB", (crop_w, crop_h), (16, 16, 16))
    labels.paste(assembled, (0, 0))
    label_draw = ImageDraw.Draw(labels)
    for row in range(gen_gfx.HUD_FACE_ROWS):
        for col in range(gen_gfx.HUD_FACE_COLS):
            index = row * gen_gfx.HUD_FACE_COLS + col
            draw_label(label_draw, (col * 16 + 5, row * 16 + 4), str(index))

    scale = max(1, args.scale)
    gap = 16
    title_h = 18
    panel_w = crop_w * scale
    panel_h = crop_h * scale + title_h
    width = panel_w * 3 + gap * 4
    height = panel_h + gap * 2
    canvas = Image.new("RGB", (width, height), (32, 32, 36))
    draw = ImageDraw.Draw(canvas)

    panels = (
        ("DOOM source crop", source),
        ("Neo Geo tiles", labels),
        ("Runtime assembly", assembled),
    )
    for i, (title, image) in enumerate(panels):
        x = gap + i * (panel_w + gap)
        y = gap
        draw.text((x, y), title, fill=(230, 230, 230))
        paste_scaled(canvas, image, x, y + title_h, scale)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    canvas.save(args.out)
    print(args.out)


if __name__ == "__main__":
    main()
