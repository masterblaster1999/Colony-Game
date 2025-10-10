#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
data_guard.py — Sanity checks + manifest generator for Colony-Game data.

Usage (from repo root):
    python tools/data_guard.py
    python tools/data_guard.py --fix               # rewrite JSON with stable formatting
    python tools/data_guard.py --report build/data_report.md
    python tools/data_guard.py --manifest build/data_manifest.json
    python tools/data_guard.py --fail-on-warning

What it does:
  1) Strict-parse all JSON files under <root>/data (no trailing commas, no dup keys).
  2) If a top-level "$schema" is present and 'jsonschema' is installed, validate.
  3) Build an ID index and a dependency graph from *_id and *_ids fields.
  4) Check for duplicate IDs and unresolved references.
  5) Emit a machine-friendly manifest JSON and an optional human report.

Exit codes:
  0 = success (no errors; warnings allowed unless --fail-on-warning)
  1 = errors found OR warnings with --fail-on-warning

This script uses only Python stdlib by default. If you install 'jsonschema',
it will validate JSON files that declare "$schema".
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from collections import Counter, defaultdict, deque, OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple, Union
from datetime import datetime

# -------------------------- Helpers & Types --------------------------

class DuplicateKeyError(ValueError):
    def __init__(self, filename: str, keys: List[str]):
        msg = f"Duplicate key(s) in JSON object: {', '.join(sorted(set(keys)))}"
        super().__init__(msg)
        self.filename = filename
        self.keys = keys

@dataclass
class DataFile:
    path: Path
    rel: str
    content: Dict[str, Any]
    id: Optional[str]
    type_guess: str
    schema: Optional[str]
    sha256: str
    deps: Set[str] = field(default_factory=set)
    asset_refs: Set[str] = field(default_factory=set)

@dataclass
class Report:
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)

    def error(self, msg: str) -> None:
        self.errors.append(msg)

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)

# -------------------------- JSON Strict Loader --------------------------

def _object_pairs_no_duplicates(pairs: List[Tuple[str, Any]], file_for_error: str) -> Dict[str, Any]:
    seen: Dict[str, Any] = {}
    dups: List[str] = []
    for k, v in pairs:
        if k in seen:
            dups.append(k)
        seen[k] = v
    if dups:
        raise DuplicateKeyError(file_for_error, dups)
    return seen

def load_json_strict(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        try:
            return json.load(f, object_pairs_hook=lambda p: _object_pairs_no_duplicates(p, str(path)))
        except json.JSONDecodeError as e:
            # Enrich with path for better UX
            raise ValueError(f"JSON syntax error in {path} at line {e.lineno}, col {e.colno}: {e.msg}") from e
        except DuplicateKeyError:
            raise  # bubble up

# -------------------------- Scanning & Hashing --------------------------

JSON_EXTS = {".json"}
ASSET_EXTS = {".png", ".jpg", ".jpeg", ".dds", ".tga",
              ".wav", ".mp3", ".ogg",
              ".ttf", ".otf",
              ".bin", ".shader", ".hlsl", ".glsl", ".fx"}

def sha256_of_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def guess_type_from_path(rel: str) -> str:
    # Heuristic: use the folder name directly under data/
    # e.g. data/items/weapon.json -> type_guess = "items"
    parts = Path(rel).parts
    try:
        idx = parts.index("data")
        if idx + 1 < len(parts):
            return parts[idx + 1]
    except ValueError:
        pass
    return "data"

def collect_json_files(data_dir: Path) -> List[Path]:
    return [p for p in data_dir.rglob("*.json") if p.is_file()]

def collect_asset_files(root: Path) -> List[Path]:
    # Look under 'res' and 'resources' (both exist in the repo)
    paths: List[Path] = []
    for folder in ("res", "resources"):
        d = root / folder
        if d.exists():
            for p in d.rglob("*"):
                if p.is_file() and p.suffix.lower() in ASSET_EXTS:
                    paths.append(p)
    return paths

# -------------------------- JSON Traversal --------------------------

def iter_json_values(obj: Any) -> Iterable[Any]:
    if isinstance(obj, dict):
        for k, v in obj.items():
            yield from iter_json_values(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from iter_json_values(v)
    else:
        yield obj

def iter_json_pairs(obj: Any) -> Iterable[Tuple[str, Any]]:
    if isinstance(obj, dict):
        for k, v in obj.items():
            yield (k, v)
            yield from iter_json_pairs(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from iter_json_pairs(v)

# -------------------------- Reference Detection --------------------------

def extract_deps(content: Dict[str, Any]) -> Set[str]:
    deps: Set[str] = set()
    for k, v in iter_json_pairs(content):
        if isinstance(v, str) and k.endswith("_id"):
            deps.add(v)
        elif isinstance(v, list) and k.endswith("_ids"):
            deps.update([x for x in v if isinstance(x, str)])
    return deps

def extract_asset_refs(content: Dict[str, Any]) -> Set[str]:
    refs: Set[str] = set()
    for v in iter_json_values(content):
        if isinstance(v, str):
            low = v.lower()
            if any(low.endswith(ext) for ext in ASSET_EXTS):
                # Keep as-is; we'll resolve against root/ and res|resources/ in multiple ways
                refs.add(v)
    return refs

# -------------------------- Schema Validation (optional) --------------------------

def try_schema_validate(df: DataFile, report: Report, schema_dir: Path) -> None:
    if not df.schema:
        return
    try:
        import jsonschema  # type: ignore
    except Exception:
        report.warn(f"{df.rel}: '$schema' present but 'jsonschema' is not installed; skipping validation.")
        return

    # Resolve schema path: allow absolute, relative to data file, or relative to schema_dir
    schema_path = None
    cand = Path(df.schema)
    if cand.is_file():
        schema_path = cand
    else:
        cand2 = df.path.parent / df.schema
        if cand2.is_file():
            schema_path = cand2
        else:
            cand3 = schema_dir / df.schema
            if cand3.is_file():
                schema_path = cand3

    if not schema_path or not schema_path.is_file():
        report.warn(f"{df.rel}: '$schema'='{df.schema}' not found next to file nor in '{schema_dir}'.")
        return

    try:
        schema = load_json_strict(schema_path)
    except Exception as e:
        report.error(f"{df.rel}: Failed to load schema '{schema_path}': {e}")
        return

    try:
        jsonschema.validate(instance=df.content, schema=schema)  # type: ignore
    except Exception as e:
        report.error(f"{df.rel}: JSON Schema validation failed: {e}")

# -------------------------- Formatting --------------------------

def stable_json_dump(obj: Any, path: Path) -> None:
    # Put "id" first for readability
    def sort_keys(o: Dict[str, Any]) -> OrderedDict:
        if not isinstance(o, dict):
            return o  # type: ignore
        keys = list(o.keys())
        keys.sort()
        if "id" in o:
            keys.remove("id")
            keys.insert(0, "id")
        return OrderedDict((k, sort_keys(o[k])) for k in keys)

    sorted_obj = sort_keys(obj)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(sorted_obj, f, ensure_ascii=False, indent=2)
        f.write("\n")
    tmp.replace(path)

# -------------------------- Main pipeline --------------------------

def analyze(root: Path, out_manifest: Path, schema_dir: Path, do_fix: bool, fail_on_warning: bool, out_report_md: Optional[Path]) -> int:
    report = Report()

    data_dir = root / "data"
    if not data_dir.exists():
        report.error(f"Expected data directory at: {data_dir}")
        return 1

    json_paths = collect_json_files(data_dir)
    if not json_paths:
        report.warn("No JSON files found under 'data/'. Nothing to do.")

    assets = collect_asset_files(root)
    asset_set: Set[Path] = set(assets)

    # Parse and index
    datafiles: List[DataFile] = []
    id_to_df: Dict[str, DataFile] = {}
    id_dups: Counter[str] = Counter()

    for jp in json_paths:
        rel = jp.relative_to(root).as_posix()
        try:
            content = load_json_strict(jp)
        except Exception as e:
            report.error(str(e))
            continue

        jid = content.get("id")
        if jid is not None and not isinstance(jid, str):
            report.error(f"{rel}: 'id' must be a string if present.")
            jid = None

        schema = content.get("$schema")
        if schema is not None and not isinstance(schema, str):
            report.warn(f"{rel}: '$schema' should be a string path (got {type(schema).__name__}).")
            schema = None

        tguess = guess_type_from_path(rel)
        file_hash = sha256_of_file(jp)
        deps = extract_deps(content)
        asset_refs = extract_asset_refs(content)

        df = DataFile(
            path=jp,
            rel=rel,
            content=content,
            id=jid,
            type_guess=tguess,
            schema=schema,
            sha256=file_hash,
            deps=deps,
            asset_refs=asset_refs
        )
        datafiles.append(df)

        if jid:
            if jid in id_to_df:
                id_dups[jid] += 1
            else:
                id_to_df[jid] = df

        # Optional fix/format after successful parse
        if do_fix:
            try:
                stable_json_dump(content, jp)
            except Exception as e:
                report.error(f"{rel}: Failed to rewrite JSON: {e}")

    # Duplicate IDs
    for bad_id, count in id_dups.items():
        df1 = id_to_df.get(bad_id)
        msg = f"Duplicate id '{bad_id}' found ({count+1} occurrences)"
        if df1:
            msg += f"; first seen at {df1.rel}"
        report.error(msg)

    # Referenced IDs must exist
    all_known_ids: Set[str] = set(id_to_df.keys())
    unresolved: Dict[str, List[str]] = defaultdict(list)
    for df in datafiles:
        for dep in df.deps:
            if dep not in all_known_ids:
                unresolved[df.rel].append(dep)

    for rel, missing_list in unresolved.items():
        missing_list = sorted(set(missing_list))
        report.error(f"{rel}: unresolved reference(s) -> {', '.join(missing_list)}")

    # Check asset references
    def resolve_asset(candidate: str) -> Optional[Path]:
        # Try as-is relative to root
        p = (root / candidate).resolve()
        if p in asset_set:
            return p if p.exists() else None
        if p.exists() and p.is_file() and p.suffix.lower() in ASSET_EXTS:
            return p

        # Try prefixing res/ and resources/
        for base in ("res", "resources"):
            p2 = (root / base / candidate).resolve()
            if p2 in asset_set:
                return p2 if p2.exists() else None
            if p2.exists() and p2.is_file() and p2.suffix.lower() in ASSET_EXTS:
                return p2
        return None

    for df in datafiles:
        for ref in df.asset_refs:
            if resolve_asset(ref) is None:
                report.warn(f"{df.rel}: missing asset reference '{ref}'")

    # Build manifest
    manifest = {
        "generated_at_utc": datetime.utcnow().isoformat(timespec="seconds") + "Z",
        "root": str(root),
        "data": [
            {
                "id": df.id,
                "type": df.type_guess,
                "path": df.rel,
                "sha256": df.sha256,
                "deps": sorted(df.deps),
                "has_schema": bool(df.schema),
                "schema": df.schema
            }
            for df in sorted(datafiles, key=lambda d: (d.type_guess, d.id or d.rel))
        ],
        "assets": [
            {
                "path": p.relative_to(root).as_posix(),
                "sha256": sha256_of_file(p),
                "size_bytes": p.stat().st_size
            }
            for p in sorted(assets)
        ],
        "stats": {
            "data_files": len(datafiles),
            "assets": len(assets),
            "types": dict(sorted(Counter(df.type_guess for df in datafiles).items()))
        }
    }

    out_manifest.parent.mkdir(parents=True, exist_ok=True)
    with out_manifest.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")

    # Optional human-readable report
    if out_report_md:
        lines: List[str] = []
        lines.append("# Data Guard Report\n")
        lines.append(f"- Generated: `{manifest['generated_at_utc']}`\n")
        lines.append(f"- Data files: **{manifest['stats']['data_files']}**")
        lines.append(f"- Assets: **{manifest['stats']['assets']}**\n")
        lines.append("## Types\n")
        for t, n in manifest["stats"]["types"].items():
            lines.append(f"- `{t}`: {n}")
        lines.append("")

        if report.errors:
            lines.append("## Errors")
            for e in report.errors:
                lines.append(f"- ❌ {e}")
            lines.append("")
        else:
            lines.append("## Errors\n- None 🎉\n")

        if report.warnings:
            lines.append("## Warnings")
            for w in report.warnings:
                lines.append(f"- ⚠️ {w}")
            lines.append("")
        else:
            lines.append("## Warnings\n- None\n")

        out_report_md.parent.mkdir(parents=True, exist_ok=True)
        out_report_md.write_text("\n".join(lines), encoding="utf-8")

    # Schema validation (after manifest so you can still inspect it)
    for df in datafiles:
        try_schema_validate(df, report, schema_dir)

    # Final summary to console
    if report.errors:
        print("\nErrors:")
        for e in report.errors:
            print("  -", e)
    if report.warnings:
        print("\nWarnings:")
        for w in report.warnings:
            print("  -", w)

    if report.errors:
        return 1
    if fail_on_warning and report.warnings:
        return 1
    return 0

# -------------------------- CLI --------------------------

def main() -> int:
    repo_root_default = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Validate data JSON and build a manifest for fast loading.")
    parser.add_argument("--root", type=Path, default=repo_root_default, help="Repo root (default: tools/..)")
    parser.add_argument("--schema-dir", type=Path, default=repo_root_default / "data" / "schema",
                        help="Path to JSON schemas (default: <root>/data/schema)")
    parser.add_argument("--manifest", type=Path, default=repo_root_default / "build" / "data_manifest.json",
                        help="Where to write the manifest JSON.")
    parser.add_argument("--report", type=Path, default=None, help="Optional Markdown report path.")
    parser.add_argument("--fix", action="store_true", help="Rewrite JSON with stable formatting.")
    parser.add_argument("--fail-on-warning", action="store_true", help="Exit nonzero if warnings are present.")
    args = parser.parse_args()

    return analyze(
        root=args.root.resolve(),
        out_manifest=args.manifest.resolve(),
        schema_dir=args.schema_dir.resolve(),
        do_fix=args.fix,
        fail_on_warning=args.fail_on_warning,
        out_report_md=args.report.resolve() if args.report else None
    )

if __name__ == "__main__":
    raise SystemExit(main())
