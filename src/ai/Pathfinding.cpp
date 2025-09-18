// Pathfinding.cpp — Windows/MSVC Unity-build friendly, matches header layout

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#if defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable : 4100 4189) // quiet benign "unused param/var" from header if any
#endif
#include "Pathfinding.hpp"   // must come first so we see the correct types/scopes
#if defined(_MSC_VER)
  #pragma warning(pop)
#endif

#include <vector>
#include <queue>
#include <limits>
#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <cstdlib> // std::abs(int)

namespace {
    using Key = std::uint64_t;

    struct OpenNode { Key key; int idx; };
    struct OpenCmp {
        bool operator()(const OpenNode& a, const OpenNode& b) const noexcept {
            return a.key > b.key; // min-heap via priority_queue
        }
    };

    inline int manhattan(const ai::Point& a, const ai::Point& b) noexcept {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }

    inline Key packKey(int f, int g) noexcept {
        const std::uint64_t F = static_cast<std::uint32_t>(f < 0 ? 0 : f);
        const std::uint64_t G = static_cast<std::uint32_t>(g < 0 ? 0 : g);
        return (F << 32) | G;
    }
} // namespace

namespace ai {

// Bridging overload for legacy call sites that expect a vector return.
std::vector<Point> aStar(const GridView& g, Point start, Point goal) {
    std::vector<Point> out;
    const PFResult r = aStar(g, start, goal, out, /*maxExpandedNodes=*/-1);
    if (r == PFResult::Found) return out;
    return {};
}

PFResult aStar(const GridView& g, Point start, Point goal,
               std::vector<Point>& out, int maxExpandedNodes)
{
    out.clear();

    if (g.w <= 0 || g.h <= 0) return PFResult::NoPath;
    if (!g.walkable || !g.cost) return PFResult::NoPath;
    if (!g.inBounds(start.x, start.y)) return PFResult::NoPath;
    if (!g.inBounds(goal.x,  goal.y))  return PFResult::NoPath;
    if (!g.walkable(start.x, start.y)) return PFResult::NoPath;
    if (!g.walkable(goal.x,  goal.y))  return PFResult::NoPath;

    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        return PFResult::Found;
    }

    const int N = g.w * g.h;
    constexpr int INF = (std::numeric_limits<int>::max)();

    std::vector<int>          gCost(N, INF);
    std::vector<int>          parent(N, -1);
    std::vector<std::uint8_t> closed(N, 0);

    const int sIdx = g.index(start.x, start.y);
    const int tIdx = g.index(goal.x,  goal.y);

    auto H = [&](int x, int y) noexcept { return manhattan(Point{x, y}, goal); };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCmp> open;
    std::vector<Key> bestKey(N, std::numeric_limits<Key>::max());

    gCost[sIdx] = 0;
    {
        const int h0 = H(start.x, start.y);
        const Key k0 = packKey(/*f=*/h0, /*g=*/0);
        bestKey[sIdx] = k0;
        open.push({k0, sIdx});
    }

    int expanded = 0;

    while (!open.empty()) {
        const OpenNode top = open.top(); open.pop();

        if (top.key != bestKey[top.idx]) continue; // stale
        if (closed[top.idx]) continue;

        if (top.idx == tIdx) {
            std::vector<Point> rev;
            const int roughLen = (std::max)(0, manhattan(start, goal) + 8);
            rev.reserve(static_cast<std::size_t>((std::min)(roughLen, N)));
            for (int p = top.idx; p != -1; p = parent[p]) rev.push_back(g.fromIndex(p));
            out.assign(rev.rbegin(), rev.rend());
            return PFResult::Found;
        }

        closed[top.idx] = 1;
        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes)
            return PFResult::Aborted;

        const Point p = g.fromIndex(top.idx);
        const int nx[4] = { p.x + 1, p.x - 1, p.x,     p.x     };
        const int ny[4] = { p.y,     p.y,     p.y + 1, p.y - 1 };

        for (int i = 0; i < 4; ++i) {
            const int x = nx[i], y = ny[i];
            if (!g.inBounds(x, y) || !g.walkable(x, y)) continue;

            const int nIdx = g.index(x, y);
            if (nIdx < 0 || nIdx >= N) continue;
            if (closed[nIdx])          continue;

            const int step = (std::max)(1, g.cost(x, y));
            const int tentative = gCost[top.idx] + step;

            if (tentative < gCost[nIdx]) {
                parent[nIdx] = top.idx;
                gCost[nIdx]  = tentative;

                const int f = tentative + H(x, y);
                const Key k = packKey(/*f=*/f, /*g=*/tentative);
                if (k < bestKey[nIdx]) {
                    bestKey[nIdx] = k;
                    open.push({k, nIdx});
                }
            }
        }
    }

    return PFResult::NoPath;
}

} // namespace ai
