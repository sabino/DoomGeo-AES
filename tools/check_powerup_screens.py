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
    score = {"red": 0, "blue": 0, "green": 0, "tan": 0, "bright": 0, "dark": 0, "colored": 0, "varied": 0}
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

    left_powerups = score_region(image, (180, 260, 520, 470))
    right_powerups = score_region(image, (520, 300, 850, 460))
    imp = score_region(image, (390, 250, 590, 430))
    playfield = score_region(image, (100, 220, 850, 555))
    hud = score_region(image, (0, 560, 960, 672))

    require_min(errors, path, "left powerup pickups", left_powerups, "blue", 500)
    require_min(errors, path, "left powerup pickups", left_powerups, "tan", 4500)
    require_min(errors, path, "right powerup pickups", right_powerups, "green", 500)
    require_min(errors, path, "right powerup pickups", right_powerups, "tan", 2500)
    require_min(errors, path, "visible imp", imp, "tan", 1000)
    require_min(errors, path, "visible imp", imp, "red", 500)
    require_min(errors, path, "powerup playfield", playfield, "colored", 30000)
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
