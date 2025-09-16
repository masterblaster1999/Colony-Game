#pragma once

#include <vector>
#include <queue>
#include <limits>
#include <functional>
#include <algorithm>
#include <cstdint>

// -----------------------------------------------------------------------------
// Minimal, typed interface with zero unused params/locals.
// Unity-build friendly on Windows.
// -----------------------------------------------------------------------------

// Keep Point simple and visible; several call sites use std::vector<Point>.
struct Point {
    int x{0};
    int y{0};
};

struct GridView {
    int w{0};
    int h{0};
    // Return whether tile (x,y) can be traversed.
    std::function<bool(int,int)> walkable;
    // Non-negative step cost for entering (x,y). Use >= 1 for "normal" tiles.
    std::function<int(int,int)> cost;

    [[nodiscard]] bool inBounds(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < w && y < h; }
    [[nodiscard]] constexpr int index(int x, int y) const noexcept { return y * w + x; }
    [[nodiscard]] Point fromIndex(int idx) const noexcept { return { idx % w, idx / w }; }
};

enum class PFResult { Found, NoPath, Aborted /* exceeded maxExpandedNodes */ };

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
// A* on a 4-neighbour grid. Writes path to outPath (start..goal).
// If maxExpandedNodes >= 0, search aborts after exceeding that budget.
[[nodiscard]] PFResult aStar(const GridView& g, Point start, Point goal,
                             std::vector<Point>& outPath, int maxExpandedNodes = -1);

// Convenience wrapper returning a path directly (empty => no path or aborted).
[[nodiscard]] inline std::vector<Point>
aStar(const GridView& g, Point start, Point goal, int maxExpandedNodes = -1) {
    std::vector<Point> p;
    const PFResult r = aStar(g, start, goal, p, maxExpandedNodes);
    if (r == PFResult::Found) return p;
    return {};
}

// -----------------------------------------------------------------------------
// Backward-compat aliases: if other files used cg::pf::{Point,Result,aStar}
// -----------------------------------------------------------------------------
namespace cg { namespace pf {
    using ::Point;
    using ::GridView;
    using ::PFResult;
    using Result = ::PFResult; // legacy name, if code used cg::pf::Result
    using ::aStar;             // brings both overloads into cg::pf::
}} // namespace cg::pf
