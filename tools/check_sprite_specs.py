#!/usr/bin/env python3
"""Validate Doom thing sprite specs against a WAD without building a ROM."""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys


def load_gen_gfx():
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    spec = importlib.util.spec_from_file_location("gen_gfx", os.path.join(here, "gen_gfx.py"))
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load tools/gen_gfx.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iwad", required=True, help="WAD path to inspect")
    ap.add_argument("--zip-member", help="WAD member if --iwad is a zip")
    ap.add_argument("--spec", required=True, help="Comma-separated thing_type:FRAME specs")
    args = ap.parse_args()

    gen_gfx = load_gen_gfx()
    specs = gen_gfx.parse_monster_sprites(args.spec)
    _tiles, defs, _metas, _palettes = gen_gfx.monster_sprite_tiles(
        args.iwad, args.zip_member, specs, [1.0]
    )
    found = {frame.replace("-mirror", "") for _thing_type, _angle, _first_scale, _scale_count, frame in defs}
    found_defs = sum(1 for _thing_type, _angle, frame, _flip_x in specs if frame in found)
    missing = [frame for _thing_type, _angle, frame, _flip_x in specs if frame not in found]

    print(f"checked={len(specs)} found={found_defs} missing={len(missing)}")
    if found:
        print("found=" + ",".join(sorted(found)))
    if missing:
        print("missing=" + ",".join(missing))
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
