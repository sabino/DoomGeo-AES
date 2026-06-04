#!/usr/bin/env python3
"""Sanity-check focused E1M1 exit completion screenshots."""

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


def color_counts(image: Image.Image, box: tuple[int, int, int, int]) -> dict[str, int]:
    counts = {"green": 0, "bright": 0, "varied": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in pixels(image, box):
        if g > 85 and r < 95 and b < 95 and g > r + 20:
            counts["green"] += 1
        if r + g + b > 150:
            counts["bright"] += 1
        if len(varied) < 1000:
            varied.add((r >> 4, g >> 4, b >> 4))
    counts["varied"] = len(varied)
    return counts


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing E1M1 exit smoke PNGs")
    args = parser.parse_args()

    root = Path(args.dir)
    initial_path = root / "e1m1-exit-initial.png"
    complete_path = root / "e1m1-exit-complete.png"
    initial, errors = load_image(initial_path)
    complete, complete_errors = load_image(complete_path)
    errors.extend(complete_errors)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    assert initial is not None
    assert complete is not None

    center_box = (390, 170, 570, 330)
    stats_box = (390, 220, 570, 330)
    hud_box = (0, 560, 960, 672)
    initial_center = color_counts(initial, center_box)
    complete_center = color_counts(complete, center_box)
    complete_stats = color_counts(complete, stats_box)
    complete_hud = color_counts(complete, hud_box)

    if initial_center["green"] > 250:
        errors.append(f"{initial_path}: unexpected exit overlay before movement green={initial_center['green']}")
    if complete_center["green"] < 450:
        errors.append(f"{complete_path}: weak EXIT overlay evidence green={complete_center['green']}")
    if complete_stats["green"] < 350:
        errors.append(f"{complete_path}: weak completion stats evidence green={complete_stats['green']}")
    if complete_hud["bright"] < 20000 or complete_hud["varied"] < 12:
        errors.append(
            f"{complete_path}: weak HUD/play state evidence bright={complete_hud['bright']} varied={complete_hud['varied']}"
        )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"E1M1 exit screenshots OK: 2 files in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
