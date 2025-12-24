#!/usr/bin/env python3
"""tools/check_patch_artifacts.py

Fail CI if a git patch/diff accidentally got pasted into a source file.

This has happened before (e.g. a header starting with:
  diff --git a/... b/...
  --- a/...
  +++ b/...
  @@ -1,3 +1,5 @@
or a file where every line is prefixed with '+'.

Usage:
  python tools/check_patch_artifacts.py
  python tools/check_patch_artifacts.py --root <repo_root>
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Iterable, List, Tuple

# Directories to skip during scanning.
SKIP_DIRS = {
    ".git",
    ".vs",
    ".vscode",
    ".idea",
    "out",
    "build",
    "bin",
    "obj",
    "dist",
    "_build",
    "__pycache__",
}

# File types we consider "source-like" for this check.
SOURCE_EXTS = {
    ".h", ".hpp", ".hh", ".inl", ".ipp",
    ".c", ".cc", ".cpp", ".cxx",
    ".rc",
    ".cmake",
}

SOURCE_BASENAMES = {
    "CMakeLists.txt",
}

# Diff/patch marker regexes (anchored to beginning of line).
DIFF_MARKERS = [
    re.compile(r"^diff --git\s+"),
    re.compile(r"^index\s+[0-9a-fA-F]{7,}\.{2}[0-9a-fA-F]{7,}"),
    re.compile(r"^(---\s+a/|\+\+\+\s+b/)"),
    re.compile(r"^@@\s+-\d"),
    re.compile(r"^\*\*\*\s+/dev/null"),
    re.compile(r"^\*\*\*\s+a/"),
    re.compile(r"^---\s+/dev/null"),
]


def iter_candidate_files(root: Path) -> Iterable[Path]:
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune directories in-place for speed.
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]

        for name in filenames:
            p = Path(dirpath) / name
            if name in SOURCE_BASENAMES:
                yield p
                continue

            ext = p.suffix.lower()
            if ext in SOURCE_EXTS:
                yield p


def scan_file(path: Path) -> List[Tuple[int, str]]:
    """Returns list of (line_no, line_text) suspicious matches."""
    try:
        data = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return []

    lines = data.splitlines()
    matches: List[Tuple[int, str]] = []

    # 1) Look for explicit diff headers/markers.
    for i, line in enumerate(lines, start=1):
        for rx in DIFF_MARKERS:
            if rx.search(line):
                matches.append((i, line))
                break

    # 2) Detect "entire file is prefixed with '+'" artifacts.
    if lines:
        plus_lines = 0
        for line in lines:
            if line.startswith("+") and not line.startswith("+++"):
                plus_lines += 1

        # If most lines start with '+' and the first non-empty line does too,
        # it's almost certainly a pasted patch.
        if plus_lines / max(1, len(lines)) >= 0.80:
            first_non_empty = next((l for l in lines if l.strip()), "")
            if first_non_empty.startswith("+"):
                # Flag the first few lines as context.
                for i, line in enumerate(lines[:10], start=1):
                    if line.startswith("+"):
                        matches.append((i, line))

    return matches


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--root",
        type=str,
        default=None,
        help="Repository root to scan (defaults to repo inferred from this script location).",
    )
    args = ap.parse_args(argv)

    if args.root:
        root = Path(args.root).resolve()
    else:
        # tools/ -> repo root
        root = Path(__file__).resolve().parents[1]

    if not root.exists():
        print(f"error: root does not exist: {root}", file=sys.stderr)
        return 2

    offenders: List[Tuple[Path, List[Tuple[int, str]]]] = []
    for p in iter_candidate_files(root):
        rel = p.relative_to(root)
        hits = scan_file(p)
        if hits:
            offenders.append((rel, hits))

    if not offenders:
        print("ok: no patch/diff artifacts detected")
        return 0

    print("error: patch/diff artifacts detected in source-like files:\n")
    for rel, hits in offenders:
        print(f"- {rel}")
        for (line_no, line) in hits[:12]:
            # Keep output compact.
            safe = line.replace("\t", "\\t")
            print(f"    L{line_no}: {safe}")
        if len(hits) > 12:
            print(f"    ... ({len(hits) - 12} more matches)")
        print()

    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
