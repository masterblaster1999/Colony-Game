# tools/split_d3d11device.py
# Usage:
#   python tools/split_d3d11device.py [--file src/gfx/D3D11Device.cpp] [--dry-run]
#
# What it does:
#   - Reads src/gfx/D3D11Device.cpp
#   - Extracts the header/preamble (includes, helpers) up to the first method definition
#   - Finds and slices out-of-class definitions:  D3D11Device::MethodName(...) { ... }
#   - Classifies them into buckets and writes:
#       src/gfx/D3D11Device_Shared.inl
#       src/gfx/D3D11Device_Init.cpp
#       src/gfx/D3D11Device_SwapChain.cpp
#       src/gfx/D3D11Device_Resize.cpp
#       src/gfx/D3D11Device_Present.cpp
#       src/gfx/D3D11Device_Debug.cpp
#       src/gfx/D3D11Device_Shutdown.cpp
#       src/gfx/D3D11Device_Misc.cpp  (catch-all)
#   - Replaces the original D3D11Device.cpp with a tiny placeholder that includes only the public header.
#
# Notes:
#   * No public headers are changed.
#   * Your CMake already globs src/*.cpp, so the new files build automatically.
#   * If you have static helpers defined *after* the first method, consider moving them above (the tool assumes
#     helpers live in the preamble). If the build complains about missing helpers, you can cut&paste them into
#     D3D11Device_Shared.inl.
import argparse, os, re, sys
from pathlib import Path

BUCKET_FILES = {
    "init":       "D3D11Device_Init.cpp",
    "swapchain":  "D3D11Device_SwapChain.cpp",
    "resize":     "D3D11Device_Resize.cpp",
    "present":    "D3D11Device_Present.cpp",
    "debug":      "D3D11Device_Debug.cpp",
    "shutdown":   "D3D11Device_Shutdown.cpp",
    "misc":       "D3D11Device_Misc.cpp",
}

def classify(name: str) -> str:
    n = name.lower()
    if any(k in n for k in ("init", "initialize", "createdevice", "createdeviceandcontext", "configuredebug", "choos", "adapter", "context")):
        return "init"
    if any(k in n for k in ("swap", "backbuffer", "rtv", "recreate", "createswapchain", "destroyswapchain", "views")):
        return "swapchain"
    if "resize" in n:
        return "resize"
    if any(k in n for k in ("present", "beginframe", "endframe", "frame")):
        return "present"
    if any(k in n for k in ("annotation", "debugname", "beginevent", "endevent", "pushmarker", "popmarker", "profile")):
        return "debug"
    if any(k in n for k in ("shutdown", "destroy", "terminate", "release")):
        return "shutdown"
    return "misc"

FUNC_START_RE = re.compile(
    # captures return type + qualifiers lazily, then the fully qualified name with D3D11Device::Method
    r'(^[^\S\r\n]*[^\r\n{;]*?\bD3D11Device::([A-Za-z_~][A-Za-z0-9_]*)\s*\([^;{]*\)\s*(?:noexcept\s*)?(?:->\s*[^{\r\n]+)?\s*\{)',
    re.MULTILINE
)

def find_funcs(text: str):
    """Yield (name, start_idx, end_idx) for each D3D11Device::method body (brace-balanced)."""
    for m in FUNC_START_RE.finditer(text):
        name = m.group(2)
        start = m.start(1)
        # find matching closing brace for the body starting at first '{' after the signature
        body_start = text.find('{', m.end(1)-1)
        if body_start == -1: continue
        depth, i, n = 0, body_start, len(text)
        while i < n:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    end = i + 1
                    yield (name, start, end)
                    break
            i += 1

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", default="src/gfx/D3D11Device.cpp")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    cpp_path = Path(args.file)
    if not cpp_path.exists():
        print(f"ERROR: {cpp_path} not found", file=sys.stderr)
        sys.exit(2)

    text = cpp_path.read_text(encoding="utf-8", errors="ignore")
    funcs = list(find_funcs(text))
    if not funcs:
        print("ERROR: No D3D11Device:: methods found. Aborting.", file=sys.stderr)
        sys.exit(3)

    first_start = min(s for _, s, _ in funcs)
    preamble = text[:first_start]
    # carve out function bodies in original order
    pieces = []
    last = 0
    for name, s, e in funcs:
        pieces.append((name, text[s:e]))
    if args.dry_run:
        from collections import Counter
        c = Counter(classify(n) for n,_ in pieces)
        print("Will create files:", {k: v for k,v in c.items()})
        for n,_ in pieces:
            print(f"  {classify(n):>9} : {n}")
        return

    gfx_dir = cpp_path.parent
    shared_inl = gfx_dir / "D3D11Device_Shared.inl"
    shared_inl.write_text(preamble, encoding="utf-8")
    print(f"Wrote {shared_inl.relative_to(Path.cwd())}")

    # write buckets
    buckets = {b: [] for b in BUCKET_FILES.keys()}
    for name, body in pieces:
        buckets[classify(name)].append(body)

    for bucket, outname in BUCKET_FILES.items():
        out_path = gfx_dir / outname
        content = (
f'#include "gfx/D3D11Device.h"\n'
f'#include "D3D11Device_Shared.inl"\n\n'
        )
        # Keep bodies in original order
        content += "\n\n".join(buckets[bucket])
        out_path.write_text(content, encoding="utf-8")
        print(f"Wrote {out_path.relative_to(Path.cwd())} ({len(buckets[bucket])} methods)")

    # backup and replace original
    bak = cpp_path.with_suffix(".cpp.bak")
    bak.write_text(text, encoding="utf-8")
    cpp_path.write_text(
        '#include "gfx/D3D11Device.h"\n'
        '// Implementations moved into the split compilation units next to this file.\n'
        '// Common includes/helpers live in D3D11Device_Shared.inl\n',
        encoding="utf-8"
    )
    print(f"Backed up original to {bak.relative_to(Path.cwd())}")
    print(f"Rewrote {cpp_path.relative_to(Path.cwd())} to a minimal stub.")

if __name__ == "__main__":
    main()
