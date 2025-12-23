#pragma once
// PriorityFlood.hpp
// -----------------------------------------------------------------------------
// Depression filling (pit removal) for heightfields using a simplified
// Priority-Flood algorithm.
//
// Why you might want this:
//   - Naive D8 flow direction often creates "sinks" where water gets stuck.
//   - Filling depressions makes rivers reach the sea (or map boundary) more
//     reliably, and gives you natural lakes (the filled regions).
//
// References (algorithm background):
//   Barnes, Lehman, Mulla — "Priority-Flood: An Optimal Depression-Filling and
//   Watershed-Labeling Algorithm for Digital Elevation Models" (2014).
// -----------------------------------------------------------------------------

#include "procgen/ProceduralGraph.hpp"

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <limits>

namespace pg::hydro {

struct PriorityFloodResult {
    Map2D filled;       // heightfield after filling depressions
    U8Map filled_mask;  // 1 if the cell was raised by the fill pass
};

// 8-neighborhood offsets (matches typical D8 hydrology).
static inline constexpr int kDx8[8] = {+1,+1, 0,-1,-1,-1, 0,+1};
static inline constexpr int kDy8[8] = { 0,-1,-1,-1, 0,+1,+1,+1};

static inline bool inside(int x,int y,int W,int H) {
    return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H;
}

// Fill depressions. "Outlets" are:
//   - all boundary cells (map edges)
//   - and all cells with height <= outlet_level (typically sea)
//
// epsilon is a small height threshold to prevent classifying tiny float noise as "filled".
inline PriorityFloodResult priority_flood_fill(const Map2D& height,
                                              float outlet_level = 0.0f,
                                              float epsilon = 0.0f)
{
    const int W = height.w;
    const int H = height.h;

    PriorityFloodResult R;
    R.filled = height;
    R.filled_mask = U8Map(W, H, 0);

    if (W <= 0 || H <= 0) return R;

    struct Node { float h; int idx; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const noexcept { return a.h > b.h; } };

    std::priority_queue<Node, std::vector<Node>, Cmp> pq;
    std::vector<std::uint8_t> visited((std::size_t)W * H, 0);

    auto push = [&](int x, int y) {
        const int i = y * W + x;
        if (visited[(std::size_t)i]) return;
        visited[(std::size_t)i] = 1;
        pq.push(Node{ R.filled.v[(std::size_t)i], i });
    };

    // Seed outlets
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool edge = (x == 0 || y == 0 || x == W - 1 || y == H - 1);
            const float h = height.at(x, y);
            if (edge || h <= outlet_level) push(x, y);
        }
    }

    while (!pq.empty()) {
        Node n = pq.top();
        pq.pop();

        const int cx = n.idx % W;
        const int cy = n.idx / W;
        const float ch = R.filled.v[(std::size_t)n.idx]; // current (possibly raised) height

        for (int k = 0; k < 8; ++k) {
            const int nx = cx + kDx8[k];
            const int ny = cy + kDy8[k];
            if (!inside(nx, ny, W, H)) continue;

            const int ni = ny * W + nx;
            if (visited[(std::size_t)ni]) continue;
            visited[(std::size_t)ni] = 1;

            float nh = R.filled.v[(std::size_t)ni];
            if (nh + epsilon < ch) {
                // Use nextafter() so filled surfaces have a tiny gradient.
                // This avoids perfectly-flat areas that can create ambiguous
                // flow directions in later D8 routing.
                const float fillh = std::nextafter(ch, std::numeric_limits<float>::infinity());
                R.filled.v[(std::size_t)ni] = fillh;
                R.filled_mask.v[(std::size_t)ni] = 1;
                pq.push(Node{ ch, ni });
            } else {
                pq.push(Node{ nh, ni });
            }
        }
    }

    return R;
}

} // namespace pg::hydro
