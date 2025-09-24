// src/worldgen/RiversAndLakesGen.cpp
// -------------------------------------------------------------------------------------------------
// Rivers & Lakes generation for a tile/grid heightmap
// - Priority-Flood + epsilon to remove sinks and build a monotone "filled" surface
// - D8 flow directions and flow accumulation
// - River and lake masks (plus optional "ocean" by sea level)
// - Optional terrain carving along rivers
//
// Self-contained. C++17. No external deps.
// Integrate with your existing worldgen: feed in heightmap (float meters or arbitrary units).
//
// Typical use:
//   using namespace colony::worldgen;
//   Grid2D<float> height = ...;               // your terrain heights
//   RiversParams P;                            // tweak thresholds/sizes
//   RiversOut out = GenerateRiversAndLakes(height, P);
//   // Renderer: draw lakes and rivers; Pathfinding: add cost/blocked from out.river/out.lake/out.ocean
//   // Optional: CarveRiversInPlace(height, out, 0.6f, 1); // 0.6 depth, 1-cell radius
//
// -------------------------------------------------------------------------------------------------
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cmath>

namespace colony { namespace worldgen {

// ------------------------ basic grid -------------------------------------------------------------
template <typename T>
struct Grid2D {
    int w = 0, h = 0;
    std::vector<T> v;

    Grid2D() = default;
    Grid2D(int W, int H, const T& init = T{}) : w(W), h(H), v(static_cast<size_t>(W*H), init) {}

    inline bool inBounds(int x, int y) const noexcept { return (unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h; }
    inline int  idx(int x, int y) const noexcept { return y * w + x; }

    inline T& at(int x, int y)             { return v[idx(x,y)]; }
    inline const T& at(int x, int y) const { return v[idx(x,y)]; }
};

struct Vec2i { int x=0, y=0; };

// 8-neighborhood (E, SE, S, SW, W, NW, N, NE) – cw order keeps diagonals balanced
static constexpr int DX[8] = {+1, +1, 0, -1, -1, -1, 0, +1};
static constexpr int DY[8] = { 0, +1,+1, +1,  0, -1,-1, -1};

// ------------------------ params & outputs -------------------------------------------------------
struct RiversParams {
    float seaLevel = 0.0f;         // <= seaLevel is ocean
    float rainfallPerCell = 1.0f;  // "1.0" means each cell contributes 1 unit of water
    int   riverThresholdCells = 250; // how big an upstream area (in cells) creates a river
    int   minLakeCells = 32;       // ignore tiny puddles
    float epsSlope = 1e-3f;        // the epsilon gradient used in Priority-Flood (units of height)
};

struct RiversOut {
    Grid2D<float> filled;     // "filled" heights (no depressions)
    Grid2D<float> accum;      // flow accumulation (units ≈ contributing cell count * rainfallPerCell)
    Grid2D<int>   outIndex;   // outflow neighbor (flattened idx) or -1 if none (boundary)
    Grid2D<uint8_t> river;    // 1 = river cell
    Grid2D<uint8_t> lake;     // 1 = lake cell (depression fill area, filtered by size)
    Grid2D<uint8_t> ocean;    // 1 = ocean (height <= seaLevel)
};

// ------------------------ priority-flood + epsilon ----------------------------------------------
// Barnes, Lehman, Mulla — “Priority-Flood: An Optimal Depression-Filling and Watershed-Labeling Algorithm”
struct PQNode {
    float z;
    int   i;
    bool operator>(const PQNode& o) const noexcept { return z > o.z; } // min-heap via greater<>
};

// Build a filled surface E so every cell can drain to the boundary; flats slope by +eps away from outlets.
static Grid2D<float> PriorityFloodFill(const Grid2D<float>& H, float seaLevel, float epsSlope)
{
    const int W = H.w, Hh = H.h;
    Grid2D<float> E(W, Hh, 0.0f);
    Grid2D<uint8_t> visited(W, Hh, 0);

    std::priority_queue<PQNode, std::vector<PQNode>, std::greater<PQNode>> pq;

    auto push = [&](int x, int y, float z) {
        visited.at(x,y) = 1;
        E.at(x,y) = z;
        pq.push({z, y*W + x});
    };

    // Seed with boundary cells; clamp up to seaLevel so oceans are at least seaLevel.
    for (int x = 0; x < W; ++x) {
        float z0 = std::max(H.at(x,0),     seaLevel);
        float z1 = std::max(H.at(x,Hh-1),  seaLevel);
        push(x, 0, z0);
        if (Hh > 1) push(x, Hh-1, z1);
    }
    for (int y = 1; y < Hh-1; ++y) {
        float zL = std::max(H.at(0,y),     seaLevel);
        float zR = std::max(H.at(W-1,y),   seaLevel);
        push(0, y, zL);
        if (W > 1) push(W-1, y, zR);
    }

    while (!pq.empty()) {
        PQNode n = pq.top(); pq.pop();
        const int cx = n.i % W;
        const int cy = n.i / W;
        const float cz = n.z;

        for (int k = 0; k < 8; ++k) {
            const int nx = cx + DX[k], ny = cy + DY[k];
            if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)Hh) continue;
            if (visited.at(nx,ny)) continue;

            // Enforce a tiny downhill gradient towards the queue cell by lifting neighbor min to cz+eps.
            float target = std::max(H.at(nx,ny), cz + epsSlope);
            push(nx, ny, target);
        }
    }

    return E;
}

// ------------------------ flow routing & accumulation -------------------------------------------
static Grid2D<int> BuildOutflowD8(const Grid2D<float>& E)
{
    const int W = E.w, Hh = E.h;
    Grid2D<int> out(W, Hh, -1);

    for (int y = 0; y < Hh; ++y) {
        for (int x = 0; x < W; ++x) {
            const float z = E.at(x,y);
            float best = z;
            int bx = -1, by = -1;

            for (int k = 0; k < 8; ++k) {
                int nx = x + DX[k], ny = y + DY[k];
                if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)Hh) continue;
                float nz = E.at(nx,ny);
                if (nz < best) { best = nz; bx = nx; by = ny; }
            }

            if (bx >= 0) out.at(x,y) = by*W + bx; // flow to the lowest neighbor
            else         out.at(x,y) = -1;        // boundary/local minimum (should only happen on the boundary)
        }
    }
    return out;
}

static Grid2D<float> FlowAccumulation(const Grid2D<float>& E, const Grid2D<int>& out, float rainfallPerCell)
{
    const int N = E.w * E.h;
    Grid2D<float> acc(E.w, E.h, rainfallPerCell);

    // Topological order: process from high->low E so upstream contributes before downstream propagates.
    std::vector<int> order(N);
    order.reserve(N);
    for (int i = 0; i < N; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b){ return E.v[a] > E.v[b]; }); // descending by E

    for (int i : order) {
        int j = out.v[i];
        if (j >= 0) acc.v[j] += acc.v[i];
    }
    return acc;
}

// ------------------------ lake detection (size filter) ------------------------------------------
static Grid2D<uint8_t> DetectLakes(const Grid2D<float>& H, const Grid2D<float>& E, int minLakeCells, float seaLevel)
{
    const int W = H.w, Hh = H.h;
    Grid2D<uint8_t> lake(W, Hh, 0);
    Grid2D<uint8_t> candidate(W, Hh, 0);
    const float eps = 1e-6f;

    for (int y = 0; y < Hh; ++y)
        for (int x = 0; x < W; ++x)
            if (H.at(x,y) <= seaLevel) candidate.at(x,y) = 0;      // ocean is not a lake
            else if (E.at(x,y) > H.at(x,y) + eps) candidate.at(x,y) = 1; // depression fill

    // BFS connected components on candidate==1, keep only those >= minLakeCells
    Grid2D<uint8_t> visited(W, Hh, 0);
    std::vector<Vec2i> q; q.reserve(1024);

    for (int y = 0; y < Hh; ++y) for (int x = 0; x < W; ++x) {
        if (!candidate.at(x,y) || visited.at(x,y)) continue;
        q.clear(); q.push_back({x,y}); visited.at(x,y) = 1;
        int head = 0;

        while (head < (int)q.size()) {
            Vec2i p = q[head++];
            for (int k = 0; k < 8; ++k) {
                int nx = p.x + DX[k], ny = p.y + DY[k];
                if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)Hh) continue;
                if (!candidate.at(nx,ny) || visited.at(nx,ny)) continue;
                visited.at(nx,ny) = 1;
                q.push_back({nx,ny});
            }
        }

        if ((int)q.size() >= minLakeCells) {
            for (const Vec2i& p : q) lake.at(p.x,p.y) = 1;
        }
    }
    return lake;
}

// ------------------------ public API -------------------------------------------------------------
RiversOut GenerateRiversAndLakes(const Grid2D<float>& height, const RiversParams& P)
{
    RiversOut out;
    out.filled = PriorityFloodFill(height, P.seaLevel, P.epsSlope);
    out.outIndex = BuildOutflowD8(out.filled);
    out.accum = FlowAccumulation(out.filled, out.outIndex, P.rainfallPerCell);

    const int W = height.w, Hh = height.h;
    out.ocean = Grid2D<uint8_t>(W, Hh, 0);
    out.river = Grid2D<uint8_t>(W, Hh, 0);
    out.lake  = DetectLakes(height, out.filled, P.minLakeCells, P.seaLevel);

    for (int y = 0; y < Hh; ++y) {
        for (int x = 0; x < W; ++x) {
            if (height.at(x,y) <= P.seaLevel) out.ocean.at(x,y) = 1;
            if (out.accum.at(x,y) >= (float)P.riverThresholdCells) out.river.at(x,y) = 1;
            // Avoid tagging lake cells as “river” unless you want outlets highlighted inside lakes:
            if (out.lake.at(x,y)) out.river.at(x,y) = 0;
        }
    }
    return out;
}

// Optional: carve channels into the terrain in-place (visual flair; can help pathfinding avoid rivers)
void CarveRiversInPlace(Grid2D<float>& height, const RiversOut& R, float carveDepth, int carveRadiusCells)
{
    if (carveDepth <= 0.0f) return;
    const int W = height.w, Hh = height.h;
    const int r = std::max(0, carveRadiusCells);
    const float depth = carveDepth;

    auto lowerCell = [&](int x, int y, float amt) {
        height.at(x,y) -= amt;
    };

    for (int y = 0; y < Hh; ++y) for (int x = 0; x < W; ++x) {
        if (!R.river.at(x,y)) continue;

        if (r == 0) {
            lowerCell(x,y, depth);
        } else {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!height.inBounds(nx,ny)) continue;
                    float d2 = float(dx*dx + dy*dy);
                    float t = std::max(0.0f, 1.0f - d2 / float(r*r + 1)); // smooth falloff
                    lowerCell(nx, ny, depth * t);
                }
            }
        }
    }
}

// ------------------------ tiny helpers for integration ------------------------------------------
// Convert a mask into walk cost (example): 255 for blocked, otherwise 1.
Grid2D<uint16_t> BuildNavCostFromWater(const RiversOut& R, uint16_t riverCost, uint16_t lakeCost, uint16_t oceanCost)
{
    Grid2D<uint16_t> cost(R.filled.w, R.filled.h, 1);
    for (int y = 0; y < R.filled.h; ++y) for (int x = 0; x < R.filled.w; ++x) {
        if (R.ocean.at(x,y))      cost.at(x,y) = oceanCost;
        else if (R.lake.at(x,y))  cost.at(x,y) = lakeCost;
        else if (R.river.at(x,y)) cost.at(x,y) = riverCost;
    }
    return cost;
}

}} // namespace colony::worldgen
