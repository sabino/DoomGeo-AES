#!/usr/bin/env python3
"""Sanity-check movement stress screenshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


EXPECTED_SIZE = (960, 672)
PLAYFIELD_BOX = (0, 40, 960, 560)
HUD_BOX = (0, 560, 960, 672)
FPS_BOX = (0, 0, 220, 90)
FRAME_STATS_BOX = (640, 165, 780, 285)


def pixels(image: Image.Image, box: tuple[int, int, int, int]) -> list[tuple[int, int, int]]:
    region = image.crop(box).convert("RGB")
    if hasattr(region, "get_flattened_data"):
        return list(region.get_flattened_data())
    return list(region.getdata())


def image_counts(image: Image.Image, box: tuple[int, int, int, int]) -> dict[str, int]:
    counts = {"bright": 0, "dark": 0, "colored": 0, "magenta": 0}
    varied: set[tuple[int, int, int]] = set()
    for r, g, b in pixels(image, box):
        total = r + g + b
        if total > 180:
            counts["bright"] += 1
        if total < 20:
            counts["dark"] += 1
        if total > 50 and max(r, g, b) - min(r, g, b) > 20:
            counts["colored"] += 1
        if r > 150 and b > 100 and g < 90:
            counts["magenta"] += 1
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


def scene_delta(before: Image.Image, after: Image.Image) -> tuple[float, int]:
    before_play = before.crop(PLAYFIELD_BOX).convert("RGB")
    after_play = after.crop(PLAYFIELD_BOX).convert("RGB")
    diff = ImageChops.difference(before_play, after_play).convert("L")
    data = pixels(diff, (0, 0, diff.size[0], diff.size[1]))
    changed = sum(r > 10 for r, _g, _b in data)
    mean = ImageStat.Stat(diff).mean[0]
    return mean, changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=".tools/screens/latest/movement-bench", help="Directory containing movement stress PNGs")
    parser.add_argument("--expect-fps", action="store_true", help="Require GnGeo --showfps overlay evidence")
    parser.add_argument("--expect-frame-stats", action="store_true", help="Require the DoomGeo frame-stats register")
    parser.add_argument("--min-diff-mean", type=float, default=8.0, help="Minimum mean playfield difference between poses")
    parser.add_argument("--min-diff-pixels", type=int, default=80000, help="Minimum changed playfield pixels between poses")
    parser.add_argument("--min-play-colored", type=int, default=100000, help="Minimum colored playfield pixels per capture")
    args = parser.parse_args()

    root = Path(args.dir)
    names = [
        "movement-stress-start.png",
        "movement-stress-forward.png",
        "movement-stress-turn.png",
        "movement-stress-strafe.png",
    ]
    images: list[Image.Image] = []
    errors: list[str] = []

    for name in names:
        path = root / name
        image, image_errors = load_image(path)
        errors.extend(image_errors)
        if image is None:
            continue
        images.append(image)

        play = image_counts(image, PLAYFIELD_BOX)
        hud = image_counts(image, HUD_BOX)
        fps = image_counts(image, FPS_BOX)
        frame_stats = image_counts(image, FRAME_STATS_BOX)
        if play["varied"] < 24:
            errors.append(f"{path}: weak playfield color variation {play['varied']} < 24")
        if play["colored"] < args.min_play_colored:
            errors.append(f"{path}: weak playfield colored evidence {play['colored']} < {args.min_play_colored}")
        if hud["bright"] < 30000:
            errors.append(f"{path}: weak HUD/status evidence {hud['bright']} < 30000")
        if args.expect_fps and fps["magenta"] < 250:
            errors.append(f"{path}: weak FPS overlay evidence {fps['magenta']} < 250")
        if args.expect_frame_stats and frame_stats["colored"] < 120:
            errors.append(f"{path}: weak frame-stats register evidence {frame_stats['colored']} < 120")

    if len(images) == len(names):
        for before_name, after_name, before, after in zip(names, names[1:], images, images[1:]):
            mean, changed = scene_delta(before, after)
            if mean < args.min_diff_mean or changed < args.min_diff_pixels:
                errors.append(
                    f"{before_name}->{after_name}: weak movement delta mean={mean:.2f} changed={changed}"
                )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"movement screenshots OK: {len(names)} files in {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
