#!/usr/bin/env python3
"""
tools/build.py — one-command build, test, and package driver for a CMake/vcpkg project.

Usage examples
--------------
# Basic build (auto-detect presets or configure into ./build)
python tools/build.py

# Build Release, run tests, and generate a JUnit report (for CI)
python tools/build.py -c Release --run-tests --junit out/test-results/ctest-junit.xml

# Use a specific configure preset (and matching build/test)
python tools/build.py --preset default --run-tests

# Manually choose generator + build dir (no presets)
python tools/build.py -G "Ninja" -B out/build-ninja -c RelWithDebInfo

# Install and make an installer via CPack
python tools/build.py --install out/install -c Release --package

# Use sccache for faster rebuilds (if installed)
python tools/build.py --use-sccache

# Pass additional cache variables to CMake
python tools/build.py -D MY_FEATURE=ON -D SOME_PATH=C:/foo

Notes
-----
- If CMakePresets.json is present, this script prefers presets. You can override with --ignore-presets.
- In non-preset mode, it tries to pick a sensible generator. You can force one with -G/--generator.
- If VCPKG_ROOT is found (or given), the vcpkg toolchain is applied automatically unless --no-vcpkg is set.
- Parallelism: defaults to CPU count; can be overridden via --jobs or CMAKE_BUILD_PARALLEL_LEVEL.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# -------- Utilities -------- #

def info(msg: str) -> None:
    print(f"[build.py] {msg}")

def warn(msg: str) -> None:
    print(f"[build.py] WARNING: {msg}")

def err(msg: str) -> None:
    print(f"[build.py] ERROR: {msg}", file=sys.stderr)

def run(cmd: List[str], cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None) -> int:
    """Run a command, streaming output. Returns the process return code."""
    nice = " ".join(quote_arg(c) for c in cmd)
    where = f" (cwd: {cwd})" if cwd else ""
    info(f"$ {nice}{where}")
    try:
        completed = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=False)
        return completed.returncode
    except FileNotFoundError:
        err(f"Command not found: {cmd[0]}")
        return 127

def quote_arg(a: str) -> str:
    if " " in a or "\t" in a or '"' in a:
        return f'"{a}"'
    return a

def cpu_count() -> int:
    return max(1, os.cpu_count() or 1)

def is_windows() -> bool:
    return platform.system().lower().startswith("win")

def which(prog: str) -> Optional[str]:
    return shutil.which(prog)

# -------- vcpkg discovery -------- #

def find_vcpkg_root(user_arg: Optional[str], repo_root: Path) -> Optional[Path]:
    """Find VCPKG_ROOT from user arg, env, or common locations (repo submodule)."""
    candidates: List[Path] = []
    if user_arg:
        candidates.append(Path(user_arg))
    env_root = os.environ.get("VCPKG_ROOT")
    if env_root:
        candidates.append(Path(env_root))
    # look for a local vcpkg checkout as submodule
    candidates.append(repo_root / "vcpkg")
    # look one level up (monorepo)
    candidates.append(repo_root.parent / "vcpkg")

    for c in candidates:
        if (c / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            return c.resolve()
    return None

# -------- Presets helpers -------- #

def load_json(path: Path) -> Optional[dict]:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError as e:
        warn(f"Failed to parse JSON at {path}: {e}")
        return None

def have_presets(source_dir: Path) -> bool:
    return (source_dir / "CMakePresets.json").exists() or (source_dir / "CMakeUserPresets.json").exists()

def list_presets(source_dir: Path) -> Tuple[List[dict], List[dict], List[dict]]:
    merged = {}
    for name in ("CMakePresets.json", "CMakeUserPresets.json"):
        p = source_dir / name
        data = load_json(p)
        if not data:
            continue
        for key in ("configurePresets", "buildPresets", "testPresets"):
            if key in data and isinstance(data[key], list):
                merged.setdefault(key, [])
                merged[key].extend(data[key])
    return merged.get("configurePresets", []), merged.get("buildPresets", []), merged.get("testPresets", [])

def choose_configure_preset(configure_presets: List[dict], requested: Optional[str]) -> Optional[dict]:
    if requested:
        for p in configure_presets:
            if p.get("name") == requested:
                return p
        err(f"Configure preset '{requested}' not found.")
        return None
    # heuristic: prefer a preset literally named "default", else the first one
    for p in configure_presets:
        if p.get("name") == "default":
            return p
    return configure_presets[0] if configure_presets else None

def find_build_preset(build_presets: List[dict], cfg_preset_name: str, requested: Optional[str]) -> Optional[dict]:
    """Pick a build preset that uses the chosen configure preset, unless explicitly requested."""
    if requested:
        for p in build_presets:
            if p.get("name") == requested:
                return p
        err(f"Build preset '{requested}' not found.")
        return None
    # Prefer a build preset tied to the chosen configure preset.
    for p in build_presets:
        if p.get("configurePreset") == cfg_preset_name:
            return p
    return None

def find_test_preset(test_presets: List[dict], cfg_preset_name: str, requested: Optional[str]) -> Optional[dict]:
    if requested:
        for p in test_presets:
            if p.get("name") == requested:
                return p
        err(f"Test preset '{requested}' not found.")
        return None
    for p in test_presets:
        if p.get("configurePreset") == cfg_preset_name:
            return p
    return None

def substitute_vars(s: str, source_dir: Path, preset_name: str) -> str:
    """Handle common ${sourceDir} and ${presetName} substitutions."""
    return (
        s.replace("${sourceDir}", str(source_dir))
         .replace("${presetName}", preset_name)
         .replace("${generator}", "")  # best-effort; ignore others
    )

def binary_dir_from_configure_preset(p: dict, source_dir: Path) -> Optional[Path]:
    bin_dir_rel = p.get("binaryDir")
    if not bin_dir_rel:
        return None
    resolved = substitute_vars(bin_dir_rel, source_dir, p.get("name", ""))
    return Path(resolved).resolve()

# -------- Generator detection (non-preset mode) -------- #

SINGLE_CONFIG_GENERATORS = {"Ninja", "Unix Makefiles", "MinGW Makefiles"}
MULTI_CONFIG_GENERATORS = {
    "Ninja Multi-Config",
    "Visual Studio 17 2022",
    "Visual Studio 16 2019",
}

def auto_detect_generator() -> str:
    if which("ninja"):
        # Prefer Ninja Multi-Config on Windows when available, otherwise Ninja
        if is_windows():
            return "Ninja Multi-Config"
        return "Ninja"
    if is_windows():
        # Fall back to Visual Studio
        return "Visual Studio 17 2022"
    # Unix Makefiles is widely available
    return "Unix Makefiles"

def is_single_config_generator(gen: str) -> bool:
    return gen in SINGLE_CONFIG_GENERATORS

# -------- CMake cache helpers -------- #

def read_cmake_cache(build_dir: Path) -> Dict[str, str]:
    out: Dict[str, str] = {}
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.exists():
        return out
    for line in cache_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        if "://" in line or "=" not in line or line.startswith(("#", "//")):
            continue
        # VAR:TYPE=VALUE
        try:
            var_part, value = line.split("=", 1)
            var_name = var_part.split(":", 1)[0]
            out[var_name] = value
        except ValueError:
            continue
    return out

# -------- Argument parsing -------- #

def parse_args(argv: List[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="CMake/vcpkg build helper")
    p.add_argument("-S", "--source", default=".", help="Source directory (default: repo root)")
    p.add_argument("-B", "--build-dir", default=None, help="Binary/build directory (non-preset mode)")
    p.add_argument("-G", "--generator", default=None, help="CMake generator to use (non-preset mode)")
    p.add_argument("-c", "--config", default=None, help="Build configuration (Debug/Release/RelWithDebInfo/MinSizeRel)")
    p.add_argument("--target", default=None, help="Build specific target (default: all)")
    p.add_argument("--jobs", type=int, default=None, help="Parallel jobs for build/ctest")
    p.add_argument("--fresh", action="store_true", help="Delete and re-configure the build directory")

    # Presets
    p.add_argument("--preset", default=None, help="Configure preset name to use")
    p.add_argument("--build-preset", default=None, help="Build preset name to use")
    p.add_argument("--test-preset", default=None, help="Test preset name to use")
    p.add_argument("--ignore-presets", action="store_true", help="Ignore CMake presets even if present")
    p.add_argument("--list-presets", action="store_true", help="List detected presets and exit")

    # vcpkg & toolchain
    p.add_argument("--vcpkg-root", default=None, help="Override VCPKG_ROOT")
    p.add_argument("--no-vcpkg", action="store_true", help="Do not apply vcpkg toolchain")
    p.add_argument("--use-sccache", action="store_true", help="Use sccache via CMAKE_*_COMPILER_LAUNCHER")

    # Actions
    p.add_argument("--run-tests", action="store_true", help="Run ctest after building")
    p.add_argument("--ctest-label", action="append", default=[], help="Add a ctest -L label filter (repeatable)")
    p.add_argument("--ctest-args", nargs=argparse.REMAINDER, help="Extra args passed to ctest after '--'")
    p.add_argument("--junit", default=None, help="Write ctest JUnit XML to this path")
    p.add_argument("--install", default=None, help="Run 'cmake --install' to this prefix directory")
    p.add_argument("--package", action="store_true", help="Run 'cpack' in the build directory")
    p.add_argument("--export-compile-commands", action="store_true", help="Set CMAKE_EXPORT_COMPILE_COMMANDS=ON")

    # Extra defines/args passthrough
    p.add_argument("-D", dest="defines", action="append", default=[], help="CMake cache variable (VAR=VALUE)")
    p.add_argument("--cmake-arg", action="append", default=[], help="Extra argument for 'cmake ..' (repeatable)")
    p.add_argument("--build-arg", action="append", default=[], help="Extra argument for 'cmake --build' (repeatable)")

    # Verbosity
    p.add_argument("-v", "--verbose", action="store_true", help="Verbose build (cmake --build --verbose)")
    return p.parse_args(argv)

# -------- Core logic -------- #

def main(argv: List[str]) -> int:
    args = parse_args(argv)
    source_dir = Path(args.source).resolve()
    if not source_dir.exists():
        err(f"Source directory not found: {source_dir}")
        return 2

    jobs = args.jobs or int(os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", cpu_count()))
    os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(jobs))

    # Presets?
    presets_available = have_presets(source_dir)
    if args.list_presets:
        if not presets_available:
            info("No CMakePresets.json / CMakeUserPresets.json found.")
            return 0
        cfg, bld, tst = list_presets(source_dir)
        info("Configure presets:")
        for p in cfg:
            info(f"  - {p.get('name')}")
        info("Build presets:")
        for p in bld:
            info(f"  - {p.get('name')} (configurePreset={p.get('configurePreset')})")
        info("Test presets:")
        for p in tst:
            info(f"  - {p.get('name')} (configurePreset={p.get('configurePreset')})")
        return 0

    # vcpkg toolchain
    repo_root = source_dir
    vcpkg_root = find_vcpkg_root(args.vcpkg_root, repo_root) if not args.no_vcpkg else None
    toolchain_define: List[str] = []
    if vcpkg_root:
        toolchain = vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake"
        if toolchain.exists():
            toolchain_define = [f"-DCMAKE_TOOLCHAIN_FILE={str(toolchain)}"]
            info(f"Using vcpkg toolchain: {toolchain}")
        else:
            warn(f"vcpkg.cmake not found under {vcpkg_root}, ignoring.")

    # Common -D defines
    user_defines = [f"-D{d}" if not d.startswith("-D") else d for d in args.defines]
    common_defines = []
    if args.export_compile_commands:
        common_defines.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    if args.use_sccache and which("sccache"):
        common_defines += [
            "-DCMAKE_C_COMPILER_LAUNCHER=sccache",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache",
        ]
        info("sccache detected; enabling compiler launcher.")
    elif args.use_sccache:
        warn("sccache requested but not found in PATH; continuing without it.")

    extra_cmake_args = args.cmake_arg or []
    extra_build_args = args.build_arg or []

    # -------- Preset path -------- #
    if presets_available and not args.ignore_presets:
        cfg_presets, build_presets, test_presets = list_presets(source_dir)
        cfg_preset = choose_configure_preset(cfg_presets, args.preset)
        if not cfg_preset:
            return 2

        # Configure
        cmake_cfg_cmd = ["cmake", "--preset", cfg_preset.get("name", "")]
        cmake_cfg_cmd += toolchain_define + common_defines + user_defines + extra_cmake_args
        rc = run(cmake_cfg_cmd, cwd=source_dir)
        if rc != 0:
            return rc

        # Determine build dir
        bin_dir = binary_dir_from_configure_preset(cfg_preset, source_dir)
        if not bin_dir or not bin_dir.exists():
            # Fallback: ask cmake to tell us (CMake >= 3.23 supports --workflow? Keep simple)
            # Use build preset or infer from cache
            build_preset = find_build_preset(build_presets, cfg_preset.get("name", ""), args.build_preset)
            if build_preset and build_preset.get("binaryDir"):
                bin_dir = Path(substitute_vars(build_preset["binaryDir"], source_dir, build_preset.get("name", ""))).resolve()
            else:
                # Try default: build directory named after preset
                bin_dir = (source_dir / "build" / cfg_preset.get("name", "default")).resolve()
        if not bin_dir.exists():
            warn(f"Computed build directory does not exist yet: {bin_dir}. Continuing anyway.")

        # Build
        # We prefer building by path (robust even if build preset missing)
        build_cmd = ["cmake", "--build", str(bin_dir), "--parallel", str(jobs)]
        if args.config:
            build_cmd += ["--config", args.config]
        if args.target:
            build_cmd += ["--target", args.target]
        if args.verbose:
            build_cmd += ["--verbose"]
        if extra_build_args:
            build_cmd += ["--"] + extra_build_args
        rc = run(build_cmd)
        if rc != 0:
            return rc

        # Tests
        if args.run_tests:
            rc = run_ctest(bin_dir, args, jobs)
            if rc != 0:
                return rc

        # Install
        if args.install:
            rc = run_install(bin_dir, args.config, args.install)
            if rc != 0:
                return rc

        # Package
        if args.package:
            rc = run_cpack(bin_dir, args.config)
            if rc != 0:
                return rc

        return 0

    # -------- Non-preset path -------- #
    # Build dir
    build_dir = Path(args.build_dir).resolve() if args.build_dir else (source_dir / "build")
    if args.fresh and build_dir.exists():
        info(f"Removing build directory: {build_dir}")
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    # Generator
    generator = args.generator or auto_detect_generator()
    info(f"Generator: {generator}")

    # Configure
    cfg = args.config or ("Debug" if is_single_config_generator(generator) else "Release")
    cmake_configure_cmd = [
        "cmake",
        "-S", str(source_dir),
        "-B", str(build_dir),
        "-G", generator,
    ]
    if is_single_config_generator(generator):
        cmake_configure_cmd.append(f"-DCMAKE_BUILD_TYPE={cfg}")
    cmake_configure_cmd += toolchain_define + common_defines + user_defines + extra_cmake_args

    rc = run(cmake_configure_cmd)
    if rc != 0:
        return rc

    # Build
    build_cmd = ["cmake", "--build", str(build_dir), "--parallel", str(jobs)]
    if not is_single_config_generator(generator):
        build_cmd += ["--config", cfg]
    if args.target:
        build_cmd += ["--target", args.target]
    if args.verbose:
        build_cmd += ["--verbose"]
    if extra_build_args:
        build_cmd += ["--"] + extra_build_args

    rc = run(build_cmd)
    if rc != 0:
        return rc

    # Tests
    if args.run_tests:
        rc = run_ctest(build_dir, args, jobs, config=cfg if not is_single_config_generator(generator) else None)
        if rc != 0:
            return rc

    # Install
    if args.install:
        rc = run_install(build_dir, None if is_single_config_generator(generator) else cfg, args.install)
        if rc != 0:
            return rc

    # Package
    if args.package:
        rc = run_cpack(build_dir, None if is_single_config_generator(generator) else cfg)
        if rc != 0:
            return rc

    return 0

# -------- Sub-commands -------- #

def run_ctest(build_dir: Path, args: argparse.Namespace, jobs: int, config: Optional[str] = None) -> int:
    cmd = ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "--parallel", str(jobs)]
    # Pass configuration if provided (harmless on single-config)
    cfg_to_use = args.config or config
    if cfg_to_use:
        cmd += ["-C", cfg_to_use]
    for label in args.ctest_label:
        cmd += ["-L", label]
    # JUnit output if requested (requires CTest >= 3.20)
    if args.junit:
        junit_path = Path(args.junit)
        junit_path.parent.mkdir(parents=True, exist_ok=True)
        cmd += ["--output-junit", str(junit_path)]
    # Extra args after '--'
    if args.ctest_args:
        # strip leading '--' if user included it
        extra = args.ctest_args
        if extra and extra[0] == "--":
            extra = extra[1:]
        if extra:
            cmd += ["--"] + extra
    return run(cmd)

def run_install(build_dir: Path, config: Optional[str], prefix: str) -> int:
    dest = Path(prefix).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    cmd = ["cmake", "--install", str(build_dir), "--prefix", str(dest)]
    if config:
        cmd += ["--config", config]
    return run(cmd)

def run_cpack(build_dir: Path, config: Optional[str]) -> int:
    # Prefer running cpack from the build dir so it picks up CPackConfig.cmake
    cmd = ["cpack"]
    if config:
        cmd += ["-C", config]
    return run(cmd, cwd=build_dir)

# -------- Entrypoint -------- #

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
