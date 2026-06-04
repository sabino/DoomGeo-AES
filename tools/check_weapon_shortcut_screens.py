#!/usr/bin/env python3
"""Sanity-check arsenal weapon shortcut smoke screenshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops


EXPECTED_SIZE = (960, 672)
WEAPON_BOX = (330, 280, 630, 560)
HUD_BOX = (0, 560, 960, 672)


def image_pixels(image: Image.Image, box: tuple[int, int, int, int]) -> list[tuple[int, int, int]]:
    region = image.crop(box).convert("RGB")
    if hasattr(region, "get_flattened_data"):
        return list(region.get_flattened_data())
    return list(region.getdata())


def load_image(path: Path) -> tuple[Image.Image | None, list[str]]:
    if not path.exists():
        return None, [f"{path}: missing"]
    try:
        image = Image.open(path).convert("RGB")
    except Exception as exc:  # pragma: no cover - defensive CLI path
        return None, [f"{path}: cannot open image: {exc}"]
    if image.size != EXPECTED_SIZE:
        return image, [f"{path}: expected {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}, got {image.size[0]}x{image.size[1]}"]
    return image, []


def region_score(image: Image.Image, box: tuple[int, int, int, int]) -> dict[str, int]:
    score = {"red": 0, "bright": 0, "dark": 0, "colored": 0, "varied": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in image_pixels(image, box):
        if max(r, g, b) - min(r, g, b) > 18:
            score["colored"] += 1
        if r > 95 and g < 85 and b < 90 and r > g + 25:
            score["red"] += 1
        if r + g + b > 180:
            score["bright"] += 1
        if r + g + b < 70:
            score["dark"] += 1
        if len(varied) < 2000:
            varied.add((r >> 4, g >> 4, b >> 4))
    score["varied"] = len(varied)
    return score


def diff_pixels(a: Image.Image, b: Image.Image, box: tuple[int, int, int, int]) -> int:
    diff = ImageChops.difference(a.crop(box), b.crop(box))
    return sum(1 for r, g, b in image_pixels(diff, (0, 0, box[2] - box[0], box[3] - box[1])) if r + g + b > 60)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing weapon shortcut PNGs")
    args = parser.parse_args()

    root = Path(args.dir)
    names = {
        "plasma": "weapon-shortcut-before.png",
        "rocket": "weapon-shortcut-cdown.png",
        "chaingun": "weapon-shortcut-held-c-right.png",
    }
    images: dict[str, Image.Image] = {}
    errors: list[str] = []
    for state, name in names.items():
        image, image_errors = load_image(root / name)
        errors.extend(image_errors)
        if image is not None:
            images[state] = image

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    for state, image in images.items():
        weapon = region_score(image, WEAPON_BOX)
        hud = region_score(image, HUD_BOX)
        if weapon["colored"] < 7000 or weapon["bright"] < 30000 or weapon["varied"] < 20:
            errors.append(f"{root / names[state]}: weak weapon evidence {weapon}")
        if hud["red"] < 9000 or hud["bright"] < 65000:
            errors.append(f"{root / names[state]}: weak arsenal HUD evidence {hud}")

    comparisons = [
        ("plasma", "rocket", 12000),
        ("rocket", "chaingun", 12000),
        ("plasma", "chaingun", 14000),
    ]
    for left, right, minimum in comparisons:
        changed = diff_pixels(images[left], images[right], WEAPON_BOX)
        if changed < minimum:
            errors.append(f"{left}->{right}: weak weapon shortcut image delta {changed} < {minimum}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"weapon shortcut screenshots OK: {len(names)} files in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
