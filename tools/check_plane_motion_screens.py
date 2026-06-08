#!/usr/bin/env python3
"""Check that forward movement visibly changes floor and ceiling bands."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat


EXPECTED_SIZE = (960, 672)
CEILING_BOXES = (
    (0, 100, 240, 285),
    (720, 100, 960, 285),
)
FLOOR_BOXES = (
    (0, 330, 260, 560),
    (700, 330, 960, 560),
)


def load(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGB")
    if image.size != EXPECTED_SIZE:
        raise ValueError(f"{path}: expected {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}, got {image.size[0]}x{image.size[1]}")
    return image


def masked_delta(before: Image.Image, after: Image.Image, boxes: tuple[tuple[int, int, int, int], ...]) -> tuple[float, int]:
    means: list[float] = []
    changed = 0
    pixels = 0
    for box in boxes:
        diff = ImageChops.difference(before.crop(box), after.crop(box)).convert("L")
        means.append(ImageStat.Stat(diff).mean[0])
        data = list(diff.getdata())
        changed += sum(value > 8 for value in data)
        pixels += len(data)
    return (sum(means) / len(means)) if means else 0.0, changed


def draw_boxes(image: Image.Image, boxes: tuple[tuple[int, int, int, int], ...], color: tuple[int, int, int]) -> None:
    draw = ImageDraw.Draw(image)
    for box in boxes:
        draw.rectangle(box, outline=color, width=3)


def save_compare(before: Image.Image, after: Image.Image, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    before_marked = before.copy()
    after_marked = after.copy()
    for image in (before_marked, after_marked):
        draw_boxes(image, CEILING_BOXES, (80, 180, 255))
        draw_boxes(image, FLOOR_BOXES, (255, 190, 60))

    gutter = 12
    compare = Image.new("RGB", (before.width * 2 + gutter, before.height), (0, 0, 0))
    compare.paste(before_marked, (0, 0))
    compare.paste(after_marked, (before.width + gutter, 0))
    path = out_dir / "plane-motion-start-forward.png"
    compare.save(path)
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest/movement-bench", help="Directory containing movement stress PNGs")
    parser.add_argument("--out-dir", default=".tools/screens/latest/plane-motion-compare", help="Directory for comparison screenshots")
    parser.add_argument("--start", default="movement-stress-start.png", help="Start capture filename")
    parser.add_argument("--forward", default="movement-stress-forward.png", help="Forward capture filename")
    parser.add_argument("--min-floor-mean", type=float, default=1.6, help="Minimum floor side-band mean delta")
    parser.add_argument("--min-floor-pixels", type=int, default=2500, help="Minimum changed floor side-band pixels")
    parser.add_argument("--min-ceiling-mean", type=float, default=1.0, help="Minimum ceiling side-band mean delta")
    parser.add_argument("--min-ceiling-pixels", type=int, default=1200, help="Minimum changed ceiling side-band pixels")
    args = parser.parse_args()

    root = Path(args.dir)
    errors: list[str] = []
    try:
        start = load(root / args.start)
        forward = load(root / args.forward)
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1

    floor_mean, floor_changed = masked_delta(start, forward, FLOOR_BOXES)
    ceiling_mean, ceiling_changed = masked_delta(start, forward, CEILING_BOXES)
    compare_path = save_compare(start, forward, Path(args.out_dir))

    if floor_mean < args.min_floor_mean or floor_changed < args.min_floor_pixels:
        errors.append(f"weak floor forward-plane delta mean={floor_mean:.2f} changed={floor_changed}")
    if ceiling_mean < args.min_ceiling_mean or ceiling_changed < args.min_ceiling_pixels:
        errors.append(f"weak ceiling forward-plane delta mean={ceiling_mean:.2f} changed={ceiling_changed}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        print(f"plane motion compare: {compare_path}", file=sys.stderr)
        return 1

    print(
        f"plane motion OK: floor mean={floor_mean:.2f} changed={floor_changed}; "
        f"ceiling mean={ceiling_mean:.2f} changed={ceiling_changed}; compare={compare_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
