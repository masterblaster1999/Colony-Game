#pragma once
// File: src/sim/Pathfinding.hpp
//
// Header-only navgrid + A* pathfinding for Colony-Game.
// - Builds a 2D walkable grid from your terrain via a height sampler.
// - 8-way A* with octile heuristic, no diagonal corner-cutting.
// - Nearest-walkable fallback, path smoothing (string pulling), and
//   simple dynamic obstacle toggling.
//
// Requirements:
//   * C++17
//   * DirectXMath (XMFLOAT3)
//
// Integration steps:
//   1) Provide heightAtWorld(x,z) -> y (hook to your terrain).
//   2) NavGrid::Build(...) to generate passability.
//   3) AStarPathfinder::FindPath(...) to get world-space waypoints.

#include <DirectXMath.h>
#include <vector>
#include <queue>
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

namespace colony { namespace sim {

using ::DirectX::XMFLOAT2;
using ::DirectX::XMFLOAT3;

// ------------------------------------------------------------
// NavGrid: world <-> grid, passability, and dynamic obstacles
// ------------------------------------------------------------
struct NavGrid
{
    // -------- Build-time params --------
    struct BuildParams {
        int   width  = 0;        // cells in X
        int   height = 0;        // cells in Z
        float cellSize = 1.0f;   // world units per cell
        float originX = 0.0f;    // world min X for cell (0,0)
        float originZ = 0.0f;    // world min Z for cell (0,0)
        float seaLevelY   = 0.0f; // cells at/below are blocked
        float maxSlopeDeg = 35.0f; // walkable slope cap (center gradient)
        float maxStepY    = 0.9f;  // per-edge step height limit
    };

    // Bit flags per cell
    enum : uint8_t {
        Walkable   = 1 << 0,     // static terrain allows walking
        BlockedDyn = 1 << 1      // dynamic obstacle set by gameplay
    };

    bool Build(const BuildParams& p, std::function<float(float,float)> heightAtWorld)
    {
        if (p.width <= 0 || p.height <= 0 || p.cellSize <= 0.0f) return false;

        params = p;
        flags.assign(size_t(p.width) * size_t(p.height), 0);

        // Precompute center heights
        heights.assign(flags.size(), 0.0f);
        for (int z = 0; z < p.height; ++z) {
            for (int x = 0; x < p.width; ++x) {
                XMFLOAT3 c = CellCenterWorld(x, z);
                heights[idx(x,z)] = heightAtWorld(c.x, c.z);
            }
        }

        // Determine static walkability
        const float slopeRad = p.maxSlopeDeg * 3.1415926535f / 180.0f;
        const float maxGrad  = std::tan(slopeRad); // rise/run

        for (int z = 0; z < p.height; ++z) {
            for (int x = 0; x < p.width; ++x) {
                const float h = heights[idx(x,z)];
                if (h <= p.seaLevelY + 1e-4f) {
                    // Underwater
                    continue;
                }

                // Estimate slope via central differences (clamp at edges)
                const int xm = std::max(0, x - 1), xp = std::min(p.width-1,  x + 1);
                const int zm = std::max(0, z - 1), zp = std::min(p.height-1, z + 1);

                const float hx0 = heights[idx(xm, z)];
                const float hx1 = heights[idx(xp, z)];
                const float hz0 = heights[idx(x, zm)];
                const float hz1 = heights[idx(x, zp)];

                const float ddx = (hx1 - hx0) / ((xp - xm) * p.cellSize);
                const float ddz = (hz1 - hz0) / ((zp - zm) * p.cellSize);
                const float grad = std::sqrt(ddx*ddx + ddz*ddz);

                if (grad > maxGrad) {
                    continue; // too steep at cell center
                }

                // Mark provisionally walkable
                flags[idx(x,z)] = Walkable;
            }
        }

        // Per-edge step clamp: if any cardinal neighbor requires stepping
        // higher than maxStepY, disallow that move later in neighbor checks.
        // (We encode only Walkable here; edge checks happen during A*.)
        return true;
    }

    // -------- Dynamic obstacles --------
    void BlockCell(int x, int z)   { if (inBounds(x,z)) flags[idx(x,z)] |=  BlockedDyn; }
    void UnblockCell(int x, int z) { if (inBounds(x,z)) flags[idx(x,z)] &= ~BlockedDyn; }

    // -------- Queries --------
    bool IsWalkable(int x, int z) const {
        return inBounds(x,z) && (flags[idx(x,z)] & Walkable) && !(flags[idx(x,z)] & BlockedDyn);
    }

    bool inBounds(int x, int z) const {
        return (x >= 0 && z >= 0 && x < params.width && z < params.height);
    }

    // Center of a cell in world space (y is terrain height for convenience)
    XMFLOAT3 CellCenterWorld(int x, int z) const {
        XMFLOAT3 out;
        out.x = params.originX + (x + 0.5f) * params.cellSize;
        out.z = params.originZ + (z + 0.5f) * params.cellSize;
        out.y = heights.empty() ? 0.0f : heights[idx(x,z)];
        return out;
    }

    // Convert world -> cell indices; returns false if outside grid.
    bool WorldToCell(const XMFLOAT3& w, int& outX, int& outZ) const {
        const float fx = (w.x - params.originX) / params.cellSize;
        const float fz = (w.z - params.originZ) / params.cellSize;
        outX = int(std::floor(fx));
        outZ = int(std::floor(fz));
        return inBounds(outX, outZ);
    }

    // Height at cell center (built during Build)
    float CellHeight(int x, int z) const { return heights[idx(x,z)]; }

    // Params exposed
    BuildParams params{};

private:
    size_t idx(int x, int z) const { return size_t(z) * size_t(params.width) + size_t(x); }

    std::vector<uint8_t> flags;   // Walkable | BlockedDyn
    std::vector<float>   heights; // y at cell center

    friend struct AStarPathfinder;
};

// ------------------------------------------------------------
// Path query options
// ------------------------------------------------------------
struct PathQuery {
    XMFLOAT3 startWorld{0,0,0};
    XMFLOAT3 goalWorld{0,0,0};
    bool     smooth = true;               // string-pulling after A*
    bool     findNearestIfBlocked = true; // if start/goal are not walkable
    int      nearestSearchRadius   = 16;  // Manhattan radius in cells
};

// ------------------------------------------------------------
// A* Pathfinder (8-way, octile heuristic)
// ------------------------------------------------------------
struct AStarPathfinder
{
    // Core entry point: outputs world-space waypoints (center of cells, y from height sampler)
    bool FindPath(const NavGrid& grid,
                  std::function<float(float,float)> heightAtWorld,
                  const PathQuery& q,
                  std::vector<XMFLOAT3>& outWaypoints)
    {
        outWaypoints.clear();

        // 1) Map start/goal to grid
        int sx, sz, gx, gz;
        bool sOK = grid.WorldToCell(q.startWorld, sx, sz);
        bool gOK = grid.WorldToCell(q.goalWorld,  gx, gz);
        if (!sOK || !gOK) return false;

        // 2) If blocked, optionally find nearest walkable
        if (!grid.IsWalkable(sx,sz)) {
            if (!q.findNearestIfBlocked || !NearestWalkable(grid, sx, sz, q.nearestSearchRadius, sx, sz))
                return false;
        }
        if (!grid.IsWalkable(gx,gz)) {
            if (!q.findNearestIfBlocked || !NearestWalkable(grid, gx, gz, q.nearestSearchRadius, gx, gz))
                return false;
        }

        // 3) Early out if same cell
        if (sx == gx && sz == gz) {
            outWaypoints.push_back(grid.CellCenterWorld(sx, sz));
            return true;
        }

        // 4) Run A*
        std::vector<int> cameFrom;
        if (!AStarSearch(grid, sx, sz, gx, gz, cameFrom))
            return false;

        // 5) Reconstruct path (grid indices -> world points)
        std::vector<XMFLOAT3> pts;
        UnwindPath(grid, gx, gz, cameFrom, pts);

        // 6) Smooth (optional)
        if (q.smooth && pts.size() > 2) {
            std::vector<XMFLOAT3> smoothPts;
            SmoothStringPull(grid, pts, smoothPts);
            pts.swap(smoothPts);
        }

        // 7) Fix y from height provider (for consistency if terrain changed)
        for (auto& p : pts) {
            p.y = heightAtWorld(p.x, p.z);
        }

        outWaypoints.swap(pts);
        return true;
    }

private:
    // Node packed index
    static inline int Idx(const NavGrid& g, int x, int z) {
        return z * g.params.width + x;
    }

    struct OpenNode {
        int id;
        float f;
        bool operator<(const OpenNode& o) const { return f > o.f; } // min-heap
    };

    // Octile heuristic (8-way)
    static inline float Heur(int x0, int z0, int x1, int z1) {
        const float dx = std::abs(x1 - x0);
        const float dz = std::abs(z1 - z0);
        const float D  = 1.0f;
        const float D2 = 1.41421356237f;
        return D * (dx + dz) + (D2 - 2.0f * D) * std::min(dx, dz);
    }

    // Edge validity: step height and static/dynamic walkable checks
    static inline bool CanStep(const NavGrid& g, int x0, int z0, int x1, int z1, bool diagonal)
    {
        if (!g.inBounds(x1,z1)) return false;
        if (!g.IsWalkable(x1,z1)) return false;

        // No diagonal corner cutting
        if (diagonal) {
            if (!g.IsWalkable(x1, z0)) return false;
            if (!g.IsWalkable(x0, z1)) return false;
        }

        // Step height constraint (per-edge)
        const float h0 = g.CellHeight(x0, z0);
        const float h1 = g.CellHeight(x1, z1);
        if (std::abs(h1 - h0) > g.params.maxStepY) return false;

        return true;
    }

    bool AStarSearch(const NavGrid& g, int sx, int sz, int gx, int gz, std::vector<int>& outCame)
    {
        const int W = g.params.width, H = g.params.height, N = W * H;
        std::vector<float> gScore(N, std::numeric_limits<float>::infinity());
        std::vector<float> fScore(N, std::numeric_limits<float>::infinity());
        std::vector<uint8_t> closed(N, 0);
        outCame.assign(N, -1);

        auto push = [&](std::priority_queue<OpenNode>& open, int id, float f) {
            open.push(OpenNode{id, f});
        };

        std::priority_queue<OpenNode> open;
        const int sId = Idx(g, sx, sz);
        const int tId = Idx(g, gx, gz);
        gScore[sId] = 0.0f;
        fScore[sId] = Heur(sx, sz, gx, gz);
        push(open, sId, fScore[sId]);

        while (!open.empty()) {
            const int cur = open.top().id;
            open.pop();
            if (closed[cur]) continue;
            closed[cur] = 1;

            if (cur == tId) return true; // found

            int cx = cur % W, cz = cur / W;

            // 8 neighbors
            static const int NX[8] = {+1,-1, 0, 0, +1,+1,-1,-1};
            static const int NZ[8] = { 0, 0,+1,-1, +1,-1,+1,-1};
            static const float NC[8] = {1,1,1,1, 1.41421356f,1.41421356f,1.41421356f,1.41421356f};

            for (int k = 0; k < 8; ++k) {
                const int nx = cx + NX[k];
                const int nz = cz + NZ[k];
                const int nid = Idx(g, nx, nz);
                const bool diag = (k >= 4);
                if (!g.inBounds(nx,nz)) continue;
                if (!CanStep(g, cx, cz, nx, nz, diag)) continue;

                if (closed[nid]) continue;

                const float tentative = gScore[cur] + NC[k];
                if (tentative < gScore[nid]) {
                    outCame[nid] = cur;
                    gScore[nid]  = tentative;
                    fScore[nid]  = tentative + Heur(nx, nz, gx, gz);
                    push(open, nid, fScore[nid]);
                }
            }
        }
        return false;
    }

    void UnwindPath(const NavGrid& g, int gx, int gz, const std::vector<int>& came, std::vector<XMFLOAT3>& outPts)
    {
        const int W = g.params.width;
        int cur = Idx(g, gx, gz);
        if (came[cur] < 0) return;

        // Backtrack to start
        std::vector<XMFLOAT3> rev;
        rev.reserve(64);
        while (cur >= 0) {
            int x = cur % W;
            int z = cur / W;
            rev.push_back(g.CellCenterWorld(x, z));
            cur = came[cur];
        }
        // Reverse to start->goal
        outPts.assign(rev.rbegin(), rev.rend());
    }

    // Bresenham-like traversal to test line-of-sight across grid cells
    static bool LineOfSight(const NavGrid& g, int x0, int z0, int x1, int z1)
    {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dz = std::abs(z1 - z0), sz = z0 < z1 ? 1 : -1;
        int err = dx - dz;

        int x = x0, z = z0;
        auto ok = [&](int ax, int az) -> bool { return g.IsWalkable(ax, az); };

        // Also prevent diagonal cutting by ensuring both cardinals ok when we "turn"
        while (true) {
            if (!ok(x,z)) return false;
            if (x == x1 && z == z1) return true;

            const int e2 = 2 * err;
            int nx = x, nz = z;

            if (e2 > -dz) { err -= dz; nx += sx; }
            if (e2 <  dx) { err += dx; nz += sz; }

            // If moving diagonally, enforce corner rule
            if (nx != x && nz != z) {
                if (!ok(nx, z) || !ok(x, nz)) return false;
            }

            x = nx; z = nz;
        }
    }

    void SmoothStringPull(const NavGrid& g,
                          const std::vector<XMFLOAT3>& inPts,
                          std::vector<XMFLOAT3>& outPts)
    {
        if (inPts.empty()) { outPts.clear(); return; }

        outPts.clear();
        outPts.push_back(inPts.front());

        int curX, curZ;
        g.WorldToCell(inPts.front(), curX, curZ);

        int i = 1;
        while (i < (int)inPts.size()) {
            int lastGood = i;
            int gx, gz;
            g.WorldToCell(inPts[i], gx, gz);

            // Extend while LOS holds
            int j = i;
            while (j + 1 < (int)inPts.size()) {
                int nx, nz;
                g.WorldToCell(inPts[j+1], nx, nz);
                if (!LineOfSight(g, curX, curZ, nx, nz)) break;
                j++;
                gx = nx; gz = nz;
            }

            // Advance to furthest visible and commit that point
            outPts.push_back(inPts[j]);
            curX = gx; curZ = gz;
            i = j + 1;
        }
    }

    bool NearestWalkable(const NavGrid& g, int sx, int sz, int radius, int& outX, int& outZ)
    {
        // Spiral (Manhattan ring) search up to radius
        if (g.IsWalkable(sx, sz)) { outX = sx; outZ = sz; return true; }

        for (int r = 1; r <= radius; ++r) {
            int x = sx - r, z = sz - r;
            int side = 2 * r;
            // 4 sides of the square
            for (int i = 0; i < side; ++i, ++x) { if (g.inBounds(x,z) && g.IsWalkable(x,z)) { outX = x; outZ = z; return true; } }
            for (int i = 0; i < side; ++i, ++z) { if (g.inBounds(x,z) && g.IsWalkable(x,z)) { outX = x; outZ = z; return true; } }
            for (int i = 0; i < side; ++i, --x) { if (g.inBounds(x,z) && g.IsWalkable(x,z)) { outX = x; outZ = z; return true; } }
            for (int i = 0; i < side; ++i, --z) { if (g.inBounds(x,z) && g.IsWalkable(x,z)) { outX = x; outZ = z; return true; } }
        }
        return false;
    }
};

}} // namespace colony::sim
