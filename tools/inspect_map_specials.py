#!/usr/bin/env python3
"""Summarize Doom map specials relevant to DoomGeo conversion fidelity."""

from __future__ import annotations

import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

import doom_convert as dc  # noqa: E402


KNOWN_LINE_SPECIALS = {
    **{special: "door" for special in dc.DOOR_SPECIALS},
    **{special: "exit" for special in dc.EXIT_SPECIALS},
    5: "floor raise",
    15: "floor raise",
    18: "floor raise",
    20: "floor raise",
    22: "floor raise",
    24: "floor raise",
    37: "floor lower",
    38: "floor lower",
    53: "floor lower",
    56: "floor raise",
    62: "platform/lift",
    66: "floor raise",
    67: "floor raise",
    68: "floor raise",
    69: "floor raise",
    70: "floor lower",
    71: "floor lower",
    88: "platform/lift",
    89: "platform/lift",
    102: "floor lower",
    103: "door",
    112: "door",
    114: "door",
    119: "floor raise",
    120: "platform/lift",
    121: "platform/lift",
    122: "platform/lift",
    123: "platform/lift",
    127: "stairs",
    131: "floor raise",
}

SUPPORTED_LINE_KINDS = {"door", "exit"}


def load_map(iwad: str, map_name: str, zip_member: str | None):
    wad = dc.Wad(dc.read_wad(iwad, zip_member))
    lumps = wad.map_lumps(map_name)
    return (
        dc.parse_linedefs(lumps["LINEDEFS"]),
        dc.parse_sidedefs(lumps["SIDEDEFS"]),
        dc.parse_sectors(lumps["SECTORS"]),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iwad", default=".tools/assets/doom1.wad.zip")
    parser.add_argument("--zip-member", default=None)
    parser.add_argument("--map", default="E1M2")
    args = parser.parse_args()

    linedefs, sidedefs, sectors = load_map(args.iwad, args.map.upper(), args.zip_member)
    line_counts = collections.Counter(line.special for line in linedefs if line.special)
    sector_counts = collections.Counter(sector.special for sector in sectors if sector.special)

    print(f"{args.map.upper()} linedef specials:")
    for special, count in sorted(line_counts.items()):
        kind = KNOWN_LINE_SPECIALS.get(special, "unknown")
        support = "supported" if kind in SUPPORTED_LINE_KINDS else "unsupported"
        print(f"  {special:3d}  {count:3d}  {kind:14s}  {support}")

    print(f"{args.map.upper()} sector specials:")
    for special, count in sorted(sector_counts.items()):
        if dc.sector_damage_amount(special):
            kind = "damage"
            support = "supported"
        elif dc.sector_is_secret(special):
            kind = "secret"
            support = "supported"
        else:
            kind = "unknown"
            support = "unsupported"
        print(f"  {special:3d}  {count:3d}  {kind:14s}  {support}")

    unsupported_motion = [
        special
        for special in line_counts
        if KNOWN_LINE_SPECIALS.get(special) not in SUPPORTED_LINE_KINDS
    ]
    if unsupported_motion:
        print("unsupported runtime motion/switch specials:", ", ".join(str(s) for s in sorted(unsupported_motion)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
