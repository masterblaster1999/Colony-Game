#pragma once
// JPS_Expander.hpp — bridge from JPS jump points to your existing A* neighbor interface.
//
// Call one of the expand_* helpers from your A* inner loop.
//   - expand_xy(...)  : if your A* uses (x,y) nodes
//   - expand_ids(...) : if your A* uses NodeId = y*W + x
//
// You do NOT need to change your Open list or heuristic types.
//
// Example (IDs):
//   std::vector<jps::Succ> succ;
//   jps::expand_ids(grid, opts, curId, parentIdOrNull, goalId, width, succ);
//   for (auto& s : succ) relax_or_push(s.id, gCur + s.stepCost, heuristic(s.x, s.y));
//
// License: MIT-like.

#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif
#include <vector>
#include <cstdint>
#include <algorithm>
#include "JPS.hpp"

namespace jps {

struct Succ {
    int x, y;
    std::uint32_t id;
    float stepCost;
};

inline std::uint32_t pack_xy(int x, int y, int W) noexcept {
    return static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(W)
         + static_cast<std::uint32_t>(x);
}
inline void unpack_id(std::uint32_t id, int W, int& x, int& y) noexcept {
    y = static_cast<int>(id / static_cast<std::uint32_t>(W));
    x = static_cast<int>(id % static_cast<std::uint32_t>(W));
}

// Expand when your A* works in (x,y) space.
template <class Grid>
inline void expand_xy(const Grid& grid, const Options& opt,
                      int cx, int cy,
                      bool hasParent, int px, int py,
                      int gx, int gy,
                      std::vector<Succ>& out)
{
    JPS<Grid> j(grid, opt);
    typename JPS<Grid>::Successor tmp;
    std::vector<typename JPS<Grid>::Successor> js; js.reserve(16);

    const Coord cur{cx,cy};
    const Coord goal{gx,gy};
    const Coord* parent = hasParent ? &(*new Coord{px,py}) : nullptr; // only for type; not stored

    j.getSuccessors(cur, parent, goal, js);
    out.clear(); out.reserve(js.size());
    for (auto& s : js) {
        out.push_back({ s.x, s.y, 0u, s.stepCost });
    }
    if (parent) delete parent;
}

// Expand when your A* uses NodeId = y*W + x (common pattern).
template <class Grid>
inline void expand_ids(const Grid& grid, const Options& opt,
                       std::uint32_t curId,
                       const std::uint32_t* parentIdOrNull,
                       std::uint32_t goalId,
                       int gridWidth,
                       std::vector<Succ>& out)
{
    int cx, cy; unpack_id(curId, gridWidth, cx, cy);
    int gx, gy; unpack_id(goalId, gridWidth, gx, gy);
    bool hasP = parentIdOrNull != nullptr;
    int px = 0, py = 0;
    if (hasP) unpack_id(*parentIdOrNull, gridWidth, px, py);

    std::vector<typename JPS<Grid>::Successor> js; js.reserve(16);
    JPS<Grid> j(grid, opt);
    const Coord cur{cx,cy}, goal{gx,gy};
    const Coord* parent = hasP ? &(*new Coord{px,py}) : nullptr;

    j.getSuccessors(cur, parent, goal, js);

    out.clear(); out.reserve(js.size());
    for (auto& s : js) {
        out.push_back({ s.x, s.y, pack_xy(s.x, s.y, gridWidth), s.stepCost });
    }
    if (parent) delete parent;
}

} // namespace jps
