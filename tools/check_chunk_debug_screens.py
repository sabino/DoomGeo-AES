#!/usr/bin/env python3
"""Sanity-check chunk playable debug register screenshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


EXPECTED_SIZE = (960, 672)
DEBUG_BOX = (0, 560, 720, 672)


def load(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGB")
    if image.size != EXPECTED_SIZE:
        raise ValueError(f"{path}: expected {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}, got {image.size[0]}x{image.size[1]}")
    return image


def count_colored(image: Image.Image, box: tuple[int, int, int, int]) -> int:
    total = 0
    region = image.crop(box).convert("RGB")
    data = region.get_flattened_data() if hasattr(region, "get_flattened_data") else region.getdata()
    for r, g, b in data:
        if r + g + b > 50 and max(r, g, b) - min(r, g, b) > 20:
            total += 1
    return total


def debug_delta(before: Image.Image, after: Image.Image) -> tuple[float, int]:
    before_debug = before.crop(DEBUG_BOX).convert("RGB")
    after_debug = after.crop(DEBUG_BOX).convert("RGB")
    diff = ImageChops.difference(before_debug, after_debug).convert("L")
    data = diff.get_flattened_data() if hasattr(diff, "get_flattened_data") else diff.getdata()
    changed = sum(v > 12 for v in data)
    mean = ImageStat.Stat(diff).mean[0]
    return mean, changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest/chunk-debug-movement", help="Directory containing debug movement screenshots")
    parser.add_argument("--min-colored", type=int, default=25000, help="Minimum colored pixels in the debug register crop")
    parser.add_argument("--min-diff-mean", type=float, default=0.2, help="Minimum debug register mean delta after movement")
    parser.add_argument("--min-diff-pixels", type=int, default=50, help="Minimum changed debug register pixels after movement")
    parser.add_argument("--single", help="Check only one screenshot for visible debug-register evidence")
    args = parser.parse_args()

    root = Path(args.dir)
    if args.single:
        single_path = root / args.single
        try:
            image = load(single_path)
        except Exception as exc:
            print(exc, file=sys.stderr)
            return 1
        colored = count_colored(image, DEBUG_BOX)
        if colored < args.min_colored:
            print(f"{single_path}: weak debug register evidence {colored} < {args.min_colored}", file=sys.stderr)
            return 1
        print(f"chunk debug screenshot OK: register colored={colored} in {single_path}")
        return 0

    start_path = root / "movement-stress-start.png"
    forward_path = root / "movement-stress-forward.png"
    errors: list[str] = []

    try:
        start = load(start_path)
        forward = load(forward_path)
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1

    for path, image in ((start_path, start), (forward_path, forward)):
        colored = count_colored(image, DEBUG_BOX)
        if colored < args.min_colored:
            errors.append(f"{path}: weak debug register evidence {colored} < {args.min_colored}")

    mean, changed = debug_delta(start, forward)
    if mean < args.min_diff_mean or changed < args.min_diff_pixels:
        errors.append(
            f"{start_path.name}->{forward_path.name}: weak debug register delta mean={mean:.2f} changed={changed}"
        )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"chunk debug screenshots OK: register delta mean={mean:.2f} changed={changed} in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
