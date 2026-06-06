#!/usr/bin/env python3
"""Sanity-check death/drop smoke screenshots."""

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
    score = {"red": 0, "tan": 0, "dark": 0, "bright": 0, "colored": 0, "varied": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in pixels(image, box):
        if max(r, g, b) - min(r, g, b) > 18:
            score["colored"] += 1
        if r > 90 and g < 65 and b < 70 and r > g + 25:
            score["red"] += 1
        if r > 95 and g > 45 and b < 65 and r > g + 20:
            score["tan"] += 1
        if r + g + b < 80:
            score["dark"] += 1
        if r + g + b > 170:
            score["bright"] += 1
        if len(varied) < 1000:
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
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing death/drop PNGs")
    parser.add_argument("--file", default="death-drop.png", help="Death/drop screenshot filename")
    args = parser.parse_args()

    path = Path(args.dir) / args.file
    image, errors = load_image(path)
    if image is None:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    corpse_band = score_region(image, (260, 310, 760, 430))
    center_drop = score_region(image, (360, 380, 570, 555))
    playfield = score_region(image, (120, 220, 840, 560))
    hud = score_region(image, (0, 560, 960, 672))

    require_min(errors, path, "corpse band color", corpse_band, "colored", 5000)
    require_min(errors, path, "corpse band lighting", corpse_band, "bright", 6000)
    require_min(errors, path, "corpse band shadow", corpse_band, "dark", 600)
    require_min(errors, path, "center dropped weapon", center_drop, "tan", 900)
    require_min(errors, path, "center dropped weapon shadow", center_drop, "dark", 2500)
    require_min(errors, path, "playfield corpse/drop", playfield, "colored", 28000)
    require_min(errors, path, "playfield lighting", playfield, "bright", 65000)
    require_min(errors, path, "playfield variety", playfield, "varied", 30)
    require_min(errors, path, "status bar", hud, "red", 8000)

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"death/drop screenshots OK: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
