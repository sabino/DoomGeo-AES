#!/usr/bin/env python3
"""Sanity-check powerup test smoke screenshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image


EXPECTED_SIZE = (960, 672)


def pixels(image: Image.Image, box: tuple[int, int, int, int]) -> list[tuple[int, int, int]]:
    region = image.crop(box).convert("RGB")
    if hasattr(region, "get_flattened_data"):
        return list(region.get_flattened_data())
    return list(region.getdata())


def score_region(image: Image.Image, box: tuple[int, int, int, int]) -> dict[str, int]:
    score = {"red": 0, "blue": 0, "green": 0, "tan": 0, "gray": 0, "bright": 0, "dark": 0, "colored": 0, "varied": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in pixels(image, box):
        if max(r, g, b) - min(r, g, b) > 18:
            score["colored"] += 1
        if r > 95 and g < 80 and b < 90 and r > g + 25:
            score["red"] += 1
        if b > 95 and r < 90 and g < 120 and b > r + 25:
            score["blue"] += 1
        if g > 85 and r < 100 and b < 100 and g > r + 15:
            score["green"] += 1
        if r > 95 and g > 45 and b < 85 and r > g + 15:
            score["tan"] += 1
        if abs(r - g) < 20 and abs(g - b) < 20 and r > 90:
            score["gray"] += 1
        if r + g + b > 180:
            score["bright"] += 1
        if r + g + b < 80:
            score["dark"] += 1
        if len(varied) < 1200:
            varied.add((r >> 4, g >> 4, b >> 4))
    score["varied"] = len(varied)
    return score


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


def require_min(errors: list[str], path: Path, label: str, score: dict[str, int], key: str, minimum: int) -> None:
    if score[key] < minimum:
        errors.append(f"{path}: weak {label} {key} evidence {score[key]} < {minimum}; score={score}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing powerup PNGs")
    parser.add_argument("--file", default="powerup-test.png", help="Powerup screenshot filename")
    args = parser.parse_args()

    path = Path(args.dir) / args.file
    image, errors = load_image(path)
    if image is None:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    armor = score_region(image, (280, 330, 460, 470))
    ammo = score_region(image, (390, 335, 500, 430))
    weapon = score_region(image, (450, 300, 650, 430))
    shells = score_region(image, (600, 320, 715, 430))
    playfield = score_region(image, (240, 270, 720, 490))
    hud = score_region(image, (0, 560, 960, 672))

    require_min(errors, path, "green armor pickup", armor, "green", 900)
    require_min(errors, path, "green armor pickup", armor, "colored", 3000)
    require_min(errors, path, "ammo box pickup", ammo, "green", 400)
    require_min(errors, path, "ammo box pickup", ammo, "gray", 500)
    require_min(errors, path, "weapon pickup", weapon, "gray", 2000)
    require_min(errors, path, "weapon pickup", weapon, "bright", 2500)
    require_min(errors, path, "shell pickup", shells, "red", 250)
    require_min(errors, path, "shell pickup", shells, "colored", 600)
    require_min(errors, path, "powerup playfield", playfield, "colored", 12000)
    require_min(errors, path, "status bar", hud, "red", 9000)
    require_min(errors, path, "status bar", hud, "bright", 65000)

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"powerup screenshots OK: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
