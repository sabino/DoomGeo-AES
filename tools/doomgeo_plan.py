#!/usr/bin/env python3
"""Plan-only tracker for DoomGeo-AES.

This helper deliberately does not build, install, or modify generated assets.
It only edits the markdown plan file committed under docs/.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


PLAN_PATH = Path("docs") / "release-plan.md"
CHECKBOX_RE = re.compile(r"^(?P<prefix>\s*-\s+\[)(?P<mark>[ xX])(?P<suffix>\]\s+)(?P<title>.*?)(?P<trail>\s*)$")


@dataclass
class Item:
    line_no: int
    checked: bool
    title: str


def repo_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if (candidate / "Makefile").exists() and (candidate / "docs").exists():
            return candidate
    return Path.cwd().resolve()


def plan_file(root: Path, override: Path | None) -> Path:
    return (root / override).resolve() if override else root / PLAN_PATH


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        raise FileNotFoundError(f"plan file not found: {path}")
    return path.read_text(encoding="utf-8").splitlines(keepends=True)


def items(lines: list[str]) -> list[Item]:
    found: list[Item] = []
    for idx, line in enumerate(lines):
        match = CHECKBOX_RE.match(line.rstrip("\n"))
        if match:
            found.append(Item(idx, match.group("mark").lower() == "x", match.group("title")))
    return found


def write_lines(path: Path, lines: list[str]) -> None:
    path.write_text("".join(lines), encoding="utf-8")


def list_items(path: Path) -> int:
    lines = read_lines(path)
    found = items(lines)
    for index, item in enumerate(found, start=1):
        mark = "x" if item.checked else " "
        print(f"{index:02d}. [{mark}] {item.title}")
    print(f"{sum(1 for item in found if item.checked)}/{len(found)} complete")
    return 0


def status(path: Path) -> int:
    found = items(read_lines(path))
    complete = sum(1 for item in found if item.checked)
    remaining = len(found) - complete
    print(f"plan: {path}")
    print(f"items: {len(found)}")
    print(f"complete: {complete}")
    print(f"remaining: {remaining}")
    return 0 if remaining == 0 else 1


def set_item(path: Path, index: int, checked: bool) -> int:
    lines = read_lines(path)
    found = items(lines)
    if index < 1 or index > len(found):
        raise IndexError(f"item index out of range: {index}")
    item = found[index - 1]
    line = lines[item.line_no].rstrip("\n")
    lines[item.line_no] = CHECKBOX_RE.sub(
        lambda match: f"{match.group('prefix')}{'x' if checked else ' '}{match.group('suffix')}{match.group('title')}{match.group('trail')}",
        line,
    ) + "\n"
    write_lines(path, lines)
    return 0


def add_item(path: Path, text: str) -> int:
    lines = read_lines(path)
    insert_at = len(lines)
    for idx, line in enumerate(lines):
        if line.startswith("## Evidence"):
            insert_at = idx
            break
    if insert_at and lines[insert_at - 1].strip():
        lines.insert(insert_at, "\n")
        insert_at += 1
    lines.insert(insert_at, f"- [ ] {text}\n")
    write_lines(path, lines)
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Track the DoomGeo-AES release plan only.")
    parser.add_argument("--repo", type=Path, default=None)
    parser.add_argument("--plan", type=Path, default=None)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    sub.add_parser("status")
    check = sub.add_parser("check")
    check.add_argument("index", type=int)
    uncheck = sub.add_parser("uncheck")
    uncheck.add_argument("index", type=int)
    add = sub.add_parser("add")
    add.add_argument("text")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    root = args.repo.resolve() if args.repo else repo_root()
    path = plan_file(root, args.plan)
    try:
        if args.command == "list":
            return list_items(path)
        if args.command == "status":
            return status(path)
        if args.command == "check":
            return set_item(path, args.index, True)
        if args.command == "uncheck":
            return set_item(path, args.index, False)
        if args.command == "add":
            return add_item(path, args.text)
    except (FileNotFoundError, IndexError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
