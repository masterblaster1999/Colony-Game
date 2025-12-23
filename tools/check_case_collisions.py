#!/usr/bin/env python3
"""Detect case-insensitive path collisions (Windows-unsafe files).

Why: On typical Windows filesystems, paths are case-insensitive. A repo that
contains both 'Foo.hpp' and 'foo.hpp' (same directory) will not round-trip
cleanly on Windows and can break checkouts/builds.

This script walks the repo and errors out if it finds collisions when comparing
paths using casefold().
"""

from __future__ import annotations

import argparse
import os
import sys
from collections import defaultdict
from pathlib import Path


DEFAULT_EXCLUDES = {
    ".git",
    ".vcpkg",
    ".vs",
    "build",
    "out",
    "bin",
    "dist",
    "stage",
    "cmake-build-debug",
    "cmake-build-release",
}


def walk_paths(root: Path, exclude: set[str]) -> list[str]:
    paths: list[str] = []

    # os.walk mutates dirnames in-place to prune traversal.
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        parts = [] if rel_dir == "." else rel_dir.split(os.sep)

        if parts and parts[0] in exclude:
            # Pruned by parent; this is just a safety net.
            continue

        # Prune excluded top-level directories.
        dirnames[:] = [d for d in dirnames if not (len(parts) == 0 and d in exclude)]

        for fn in filenames:
            rel = os.path.normpath(os.path.join(rel_dir, fn))
            # Normalize separators to forward slashes for stable output.
            rel = rel.replace(os.sep, "/")
            paths.append(rel)

    return paths


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Detect case-insensitive path collisions (Windows-unsafe).")
    ap.add_argument("--root", type=Path, default=Path("."), help="Repo root to scan (default: .)")
    ap.add_argument("--exclude", action="append", default=[], help="Extra top-level dirs to exclude.")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    exclude = set(DEFAULT_EXCLUDES) | set(args.exclude)

    if not root.exists():
        print(f"error: root does not exist: {root}", file=sys.stderr)
        return 2

    all_paths = walk_paths(root, exclude)

    by_fold = defaultdict(list)
    for p in all_paths:
        by_fold[p.casefold()].append(p)

    collisions = [v for v in by_fold.values() if len(v) > 1]

    if collisions:
        print("Case-insensitive path collisions found (Windows-unsafe):", file=sys.stderr)
        for group in sorted(collisions, key=lambda g: g[0].casefold()):
            print("  - " + "  |  ".join(group), file=sys.stderr)
        print("", file=sys.stderr)
        print("Fix: rename or remove one of the colliding files.", file=sys.stderr)
        return 1

    print("OK: no case-insensitive path collisions detected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
