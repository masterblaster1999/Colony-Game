// pathfinding/Jps.cpp — MSVC-friendly implementation of Jump Point Search

#include "JpsCore.hpp"

#include <algorithm>     // min, max, reverse
#include <cstddef>       // size_t
#include <cstdlib>       // abs, llabs
#include <optional>      // optional
#include <queue>         // priority_queue
#include <unordered_map> // unordered_map
#include <utility>       // pair
#include <vector>        // vector

namespace colony::path {

namespace {

static inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

// Treat OOB as blocked for forced-neighbor tests.
static inline bool blocked(const GridView& g, int x, int y) noexcept {
    return !g.inBounds(x, y) || g.isBlocked(x, y);
}

// One-step move legality from (x,y) in direction (dx,dy).
// - If dx/dy is diagonal:
//   - requires allowDiag
//   - if dontCrossCorners: both adjacent cardinals must be passable.
static inline bool canStep(
    const GridView& g,
    int x, int y,
    int dx, int dy,
    bool allowDiag,
    bool dontCrossCorners) noexcept
{
    if (dx == 0 && dy == 0) return false;

    const int nx = x + dx;
    const int ny = y + dy;

    if (!g.passable(nx, ny)) return false;

    if (dx != 0 && dy != 0) {
        if (!allowDiag) return false;
        if (dontCrossCorners) {
            // Prevent diagonal corner cutting: both side-adjacent cells must be open.
            if (!g.passable(x + dx, y) || !g.passable(x, y + dy)) return false;
        }
    }

    return true;
}

// Costed Manhattan/Octile distance.
static inline float octileCost(
    int x0, int y0, int x1, int y1,
    bool allowDiagonal,
    float costStraight,
    float costDiagonal) noexcept
{
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);

    const float D = costStraight;

    if (!allowDiagonal) {
        // Manhattan with scaled straight cost
        return D * float(dx + dy);
    }

    const float D2 = costDiagonal;
    return D * float(dx + dy) + (D2 - 2.0f * D) * float(std::min(dx, dy));
}

// Tie-break nudge based on cross-product magnitude (distance from the start-goal line).
static inline float tieBreakCrossEpsilon(
    int x, int y,
    int sx, int sy,
    int gx, int gy,
    float costStraight) noexcept
{
    const int dx1 = x - gx;
    const int dy1 = y - gy;
    const int dx2 = sx - gx;
    const int dy2 = sy - gy;

    const long long cross = std::llabs(1LL * dx1 * dy2 - 1LL * dy1 * dx2);
    const int denom = std::max(1, std::abs(dx2) + std::abs(dy2));
    const float crossNorm = float(cross) / float(denom);

    // Tiny, scale-aware nudge in "cost units"
    return (1e-3f * crossNorm) * costStraight;
}

static inline float heuristicCost(
    int x, int y,
    int sx, int sy,
    int gx, int gy,
    bool allowDiagonal,
    float costStraight,
    float costDiagonal,
    float heuristicWeight,
    bool tieBreakCross) noexcept
{
    float h = octileCost(x, y, gx, gy, allowDiagonal, costStraight, costDiagonal);
    if (tieBreakCross) {
        h += tieBreakCrossEpsilon(x, y, sx, sy, gx, gy, costStraight);
    }
    return h * heuristicWeight;
}

} // namespace

float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);

    if (!allowDiagonal) {
        // Manhattan
        return float(dx + dy);
    }

    // Octile distance: cost(diagonal)=sqrt(2), straight=1
    const float D  = 1.0f;
    const float D2 = 1.41421356237f;
    return D * float(dx + dy) + (D2 - 2.0f * D) * float(std::min(dx, dy));
}

// ---- Jump Point Search core ----

std::optional<std::pair<int, int>> Jump(
    const GridView& g,
    int x, int y,
    int dx, int dy,
    int goalX, int goalY,
    bool allowDiag,
    bool dontCrossCorners)
{
    if (dx == 0 && dy == 0) return std::nullopt;

    // If diagonal movement is disabled globally, diagonal directions are invalid.
    if (!allowDiag && dx != 0 && dy != 0) return std::nullopt;

    for (;;) {
        // Step once in (dx,dy)
        if (!canStep(g, x, y, dx, dy, allowDiag, dontCrossCorners)) return std::nullopt;

        x += dx;
        y += dy;

        // Goal reached
        if (x == goalX && y == goalY) return std::make_pair(x, y);

        // Forced-neighbor checks (Harabor & Grastien style)
        if (dx != 0 && dy != 0) { // diagonal move
            // Forced neighbors for diagonal movement:
            if ((blocked(g, x - dx, y) && g.passable(x - dx, y + dy)) ||
                (blocked(g, x, y - dy) && g.passable(x + dx, y - dy))) {
                return std::make_pair(x, y);
            }

            // When moving diagonally, also check for jump points in each cardinal direction.
            if (Jump(g, x, y, dx, 0, goalX, goalY, allowDiag, dontCrossCorners).has_value() ||
                Jump(g, x, y, 0, dy, goalX, goalY, allowDiag, dontCrossCorners).has_value()) {
                return std::make_pair(x, y);
            }

            // Continue stepping diagonally
        } else if (dx != 0) { // horizontal
            if ((blocked(g, x, y + 1) && g.passable(x + dx, y + 1)) ||
                (blocked(g, x, y - 1) && g.passable(x + dx, y - 1))) {
                return std::make_pair(x, y);
            }
            // Continue stepping horizontally
        } else { // vertical (dy != 0)
            if ((blocked(g, x + 1, y) && g.passable(x + 1, y + dy)) ||
                (blocked(g, x - 1, y) && g.passable(x - 1, y + dy))) {
                return std::make_pair(x, y);
            }
            // Continue stepping vertically
        }
    }
}

// Back-compat signature (defaults to dontCrossCorners=true).
std::optional<std::pair<int, int>> Jump(
    const GridView& g,
    int x, int y,
    int dx, int dy,
    int goalX, int goalY,
    bool allowDiag)
{
    return Jump(g, x, y, dx, dy, goalX, goalY, allowDiag, /*dontCrossCorners=*/true);
}

void PruneNeighbors(
    const GridView& g,
    int x, int y,
    int dx, int dy,
    bool allowDiag,
    bool dontCrossCorners,
    std::vector<std::pair<int, int>>& outDirs)
{
    outDirs.clear();

    auto pushDirIfFirstStepValid = [&](int ndx, int ndy) {
        if (canStep(g, x, y, ndx, ndy, allowDiag, dontCrossCorners)) {
            outDirs.emplace_back(ndx, ndy);
        }
    };

    if (dx == 0 && dy == 0) {
        // Start node: include all valid neighbors (4 or 8)
        pushDirIfFirstStepValid(1, 0);
        pushDirIfFirstStepValid(-1, 0);
        pushDirIfFirstStepValid(0, 1);
        pushDirIfFirstStepValid(0, -1);

        if (allowDiag) {
            pushDirIfFirstStepValid(1, 1);
            pushDirIfFirstStepValid(-1, 1);
            pushDirIfFirstStepValid(1, -1);
            pushDirIfFirstStepValid(-1, -1);
        }
        return;
    }

    // Moving direction (dx,dy) from parent -> current
    if (dx != 0 && dy != 0) { // diagonal move
        // Natural neighbors
        pushDirIfFirstStepValid(dx, 0);
        pushDirIfFirstStepValid(0, dy);
        pushDirIfFirstStepValid(dx, dy);

        // Forced neighbors (diagonal variants)
        if (blocked(g, x - dx, y) && g.passable(x - dx, y + dy)) pushDirIfFirstStepValid(-dx, dy);
        if (blocked(g, x, y - dy) && g.passable(x + dx, y - dy)) pushDirIfFirstStepValid(dx, -dy);
    } else if (dx != 0) { // horizontal
        // Natural neighbor
        pushDirIfFirstStepValid(dx, 0);

        // Forced neighbors (diagonal around obstacle)
        if (allowDiag) {
            if (blocked(g, x, y + 1) && g.passable(x + dx, y + 1)) pushDirIfFirstStepValid(dx, 1);
            if (blocked(g, x, y - 1) && g.passable(x + dx, y - 1)) pushDirIfFirstStepValid(dx, -1);
        }
    } else { // vertical (dy != 0)
        // Natural neighbor
        pushDirIfFirstStepValid(0, dy);

        // Forced neighbors (diagonal around obstacle)
        if (allowDiag) {
            if (blocked(g, x + 1, y) && g.passable(x + 1, y + dy)) pushDirIfFirstStepValid(1, dy);
            if (blocked(g, x - 1, y) && g.passable(x - 1, y + dy)) pushDirIfFirstStepValid(-1, dy);
        }
    }
}

// Back-compat signature (defaults to dontCrossCorners=true).
void PruneNeighbors(
    const GridView& g,
    int x, int y,
    int dx, int dy,
    bool allowDiag,
    std::vector<std::pair<int, int>>& outDirs)
{
    PruneNeighbors(g, x, y, dx, dy, allowDiag, /*dontCrossCorners=*/true, outDirs);
}

static std::vector<std::pair<int, int>> Reconstruct(
    const std::unordered_map<std::uint64_t, std::pair<int, int>>& parent,
    int sx, int sy,
    int gx, int gy)
{
    std::vector<std::pair<int, int>> path;

    int x = gx;
    int y = gy;

    while (!(x == sx && y == sy)) {
        path.emplace_back(x, y);

        const auto it = parent.find(Pack(x, y));
        if (it == parent.end()) {
            path.clear(); // no path
            return path;
        }

        x = it->second.first;
        y = it->second.second;
    }

    path.emplace_back(sx, sy);
    std::reverse(path.begin(), path.end());
    return path;
}

// New overload: costs + heuristicWeight + tieBreakCross actually drive algorithm.
std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid,
    int sx, int sy,
    int gx, int gy,
    bool allowDiagonal,
    bool dontCrossCorners,
    float costStraight,
    float costDiagonal,
    float heuristicWeight,
    bool tieBreakCross)
{
    // Parameter sanitization (avoid weird NaNs / negatives)
    float cs = (costStraight > 0.0f) ? costStraight : 1.0f;
    float cd = (costDiagonal > 0.0f) ? costDiagonal : cs * 1.41421356237f;
    float hw = (heuristicWeight > 0.0f) ? heuristicWeight : 1.0f;

    // Correctness guards (helps unit tests & prevents weird behavior)
    if (!grid.passable(sx, sy) || !grid.passable(gx, gy)) return {};
    if (sx == gx && sy == gy) return { { sx, sy } };

    struct PQItem {
        int x{}, y{};
        float f{}, g{};
        int px{}, py{}; // parent (for direction pruning)

        bool operator<(const PQItem& o) const noexcept {
            // std::priority_queue is a max-heap by default; invert for min-heap by f.
            return f > o.f;
        }
    };

    std::priority_queue<PQItem> open;
    std::unordered_map<std::uint64_t, float> gScore;
    std::unordered_map<std::uint64_t, std::pair<int, int>> parent;

    if (grid.width > 0 && grid.height > 0) {
        const std::size_t cap =
            static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
        gScore.reserve(cap);
        parent.reserve(cap);
    }

    const auto startKey = Pack(sx, sy);
    gScore[startKey] = 0.0f;

    const float h0 = heuristicCost(sx, sy, sx, sy, gx, gy, allowDiagonal, cs, cd, hw, tieBreakCross);
    open.push(PQItem{ sx, sy, /*f=*/h0, /*g=*/0.0f, sx, sy });

    std::vector<std::pair<int, int>> dirs;
    dirs.reserve(8);

    while (!open.empty()) {
        const PQItem cur = open.top();
        open.pop();

        // Skip stale queue entries
        const auto curKey = Pack(cur.x, cur.y);
        const auto bestIt = gScore.find(curKey);
        if (bestIt != gScore.end() && cur.g > bestIt->second) continue;

        if (cur.x == gx && cur.y == gy) {
            return Reconstruct(parent, sx, sy, gx, gy);
        }

        PruneNeighbors(
            grid,
            cur.x, cur.y,
            sgn(cur.x - cur.px),
            sgn(cur.y - cur.py),
            allowDiagonal,
            dontCrossCorners,
            dirs);

        for (const auto& d : dirs) {
            const int dx = d.first;
            const int dy = d.second;

            auto jp = Jump(grid, cur.x, cur.y, dx, dy, gx, gy, allowDiagonal, dontCrossCorners);
            if (!jp) continue;

            const int jx = jp->first;
            const int jy = jp->second;

            const auto key = Pack(jx, jy);

            const float segCost = octileCost(cur.x, cur.y, jx, jy, allowDiagonal, cs, cd);
            const float newG = cur.g + segCost;

            const auto it = gScore.find(key);
            if (it == gScore.end() || newG < it->second) {
                gScore[key] = newG;
                parent[key] = { cur.x, cur.y };

                const float h = heuristicCost(jx, jy, sx, sy, gx, gy, allowDiagonal, cs, cd, hw, tieBreakCross);
                const float f = newG + h;

                open.push(PQItem{ jx, jy, f, newG, cur.x, cur.y });
            }
        }
    }

    return {}; // no path
}

// Existing overloads remain as wrappers (backwards compatible).
std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid,
    int sx, int sy,
    int gx, int gy,
    bool allowDiagonal,
    bool dontCrossCorners)
{
    return FindPathJPS(
        grid,
        sx, sy, gx, gy,
        allowDiagonal,
        dontCrossCorners,
        /*costStraight=*/1.0f,
        /*costDiagonal=*/1.41421356237f,
        /*heuristicWeight=*/1.0f,
        /*tieBreakCross=*/false);
}

std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid,
    int sx, int sy,
    int gx, int gy,
    bool allowDiagonal)
{
    return FindPathJPS(
        grid,
        sx, sy, gx, gy,
        allowDiagonal,
        /*dontCrossCorners=*/true,
        /*costStraight=*/1.0f,
        /*costDiagonal=*/1.41421356237f,
        /*heuristicWeight=*/1.0f,
        /*tieBreakCross=*/false);
}

} // namespace colony::path
