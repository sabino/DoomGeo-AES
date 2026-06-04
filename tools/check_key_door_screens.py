#!/usr/bin/env python3
"""Sanity-check focused key-door smoke screenshots."""

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
    counts = {"red": 0, "green": 0, "blue": 0, "bright": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in pixels(image, box):
        if r > 95 and g < 80 and b < 90 and r > g + 25:
            counts["red"] += 1
        if g > 85 and r < 90 and b < 90 and g > r + 20:
            counts["green"] += 1
        if b > 85 and r < 90 and g < 120 and b > r + 20:
            counts["blue"] += 1
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


def require_count(errors: list[str], path: Path, label: str, counts: dict[str, int], key: str, minimum: int) -> None:
    if counts[key] < minimum:
        errors.append(f"{path}: weak {label} {key} evidence {counts[key]} < {minimum}")


def require_below(errors: list[str], path: Path, label: str, counts: dict[str, int], key: str, maximum: int) -> None:
    if counts[key] > maximum:
        errors.append(f"{path}: unexpected {label} {key} evidence {counts[key]} > {maximum}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest", help="Directory containing key-door smoke PNGs")
    args = parser.parse_args()

    root = Path(args.dir)
    names = {
        "initial": "key-door-initial.png",
        "missing": "key-door-missing-key.png",
        "picked": "key-door-picked-key.png",
        "opened": "key-door-opened.png",
        "through": "key-door-through.png",
    }
    images: dict[str, Image.Image] = {}
    errors: list[str] = []
    for state, name in names.items():
        path = root / name
        image, image_errors = load_image(path)
        errors.extend(image_errors)
        if image is not None:
            images[state] = image

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    center_box = (300, 160, 660, 470)
    hud_box = (0, 560, 960, 672)
    initial_center = color_counts(images["initial"], center_box)
    missing_center = color_counts(images["missing"], center_box)
    picked_center = color_counts(images["picked"], center_box)
    opened_center = color_counts(images["opened"], center_box)
    through_center = color_counts(images["through"], center_box)
    picked_hud = color_counts(images["picked"], hud_box)
    opened_hud = color_counts(images["opened"], hud_box)

    require_below(errors, root / names["initial"], "pre-key message", initial_center, "green", 200)
    require_count(errors, root / names["missing"], "missing-key message", missing_center, "green", 450)
    require_count(errors, root / names["picked"], "picked-key message", picked_center, "green", 450)
    require_count(errors, root / names["picked"], "picked-key HUD", picked_hud, "red", 9000)
    require_count(errors, root / names["opened"], "opened-door message", opened_center, "green", 450)
    require_below(errors, root / names["opened"], "opened-door center wall", opened_center, "red", 600)
    require_count(errors, root / names["opened"], "opened-door HUD", opened_hud, "red", 9000)
    require_count(errors, root / names["through"], "through-door wall", through_center, "red", 3000)
    require_count(errors, root / names["through"], "through-door scene", through_center, "bright", 90000)

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"key-door screenshots OK: {len(names)} files in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
