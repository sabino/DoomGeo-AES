#!/usr/bin/env python3
"""Sanity-check enemy visibility smoke screenshots.

The checks are intentionally image-stat based rather than exact sprite matching:
they catch blank/error captures and missing playfield content while tolerating
small animation, palette, and emulator timing differences.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image


EXPECTED_SIZE = (960, 672)


def crop_score(image: Image.Image, box: tuple[int, int, int, int]) -> tuple[int, int, int]:
    region = image.crop(box).convert("RGB")
    if hasattr(region, "get_flattened_data"):
        pixels = list(region.get_flattened_data())
    else:
        pixels = list(region.getdata())
    colored = 0
    bright = 0
    varied = set()
    for r, g, b in pixels:
        if max(r, g, b) - min(r, g, b) >= 18:
            colored += 1
        if r + g + b >= 120:
            bright += 1
        if len(varied) < 512:
            varied.add((r >> 3, g >> 3, b >> 3))
    return colored, bright, len(varied)


def check_image(path: Path, playfield_box: tuple[int, int, int, int], hud_box: tuple[int, int, int, int]) -> list[str]:
    errors: list[str] = []
    if not path.exists():
        return [f"{path}: missing"]
    try:
        image = Image.open(path).convert("RGB")
    except Exception as exc:  # pragma: no cover - defensive CLI path
        return [f"{path}: cannot open image: {exc}"]
    if image.size != EXPECTED_SIZE:
        errors.append(f"{path}: expected {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}, got {image.size[0]}x{image.size[1]}")

    play_colored, play_bright, play_varied = crop_score(image, playfield_box)
    hud_colored, hud_bright, hud_varied = crop_score(image, hud_box)
    if play_colored < 1200 or play_bright < 1400 or play_varied < 16:
        errors.append(
            f"{path}: weak playfield evidence colored={play_colored} bright={play_bright} varied={play_varied}"
        )
    if hud_colored < 900 or hud_bright < 900 or hud_varied < 12:
        errors.append(
            f"{path}: weak HUD evidence colored={hud_colored} bright={hud_bright} varied={hud_varied}"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing smoke PNGs")
    args = parser.parse_args()

    root = Path(args.dir)
    playfield_center = (260, 220, 700, 560)
    full_playfield = (80, 120, 900, 560)
    hud = (0, 560, 960, 672)
    checks = [
        ("combat-initial.png", playfield_center),
        ("combat-fired.png", playfield_center),
        ("combat-death.png", full_playfield),
        ("e1m1-encounter-initial.png", playfield_center),
        ("e1m1-encounter-fired.png", playfield_center),
        ("hidden-attack-after.png", full_playfield),
        ("monster-gallery.png", full_playfield),
    ]

    errors: list[str] = []
    for name, playfield_box in checks:
        errors.extend(check_image(root / name, playfield_box, hud))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"enemy visibility screenshots OK: {len(checks)} files in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
