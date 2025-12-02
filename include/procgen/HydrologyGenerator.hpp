#pragma once
/*
   HydrologyGenerator.hpp
   -----------------------------------------
   Turn a heightmap into rivers and lakes using:
     1) Priority Flood depression filling (O(N log N))
     2) D8 flow directions (acyclic by construction)
     3) Flow accumulation
     4) River extraction + optional channel carving
     5) Lake labelling

   Drop-in usage:
     - Provide (width * height) elevation array (row-major).
     - Call HydrologyGenerator::generate(...)
     - Optionally call HydrologyGenerator::apply_river_carving(...)
     - Use result.river_mask / water_level / lake_id for rendering & gameplay.

   MIT-style; use freely.
*/

#include <vector>
#include <queue>
#include <limits>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace colony {
namespace procgen {

struct HydrologyParams {
    int   width  = 0;
    int   height = 0;

    // Cells with filled water level <= sea_level are considered ocean/coast.
    float sea_level = 0.0f;

    // River starts where flow accumulation (in cells) >= threshold.
    float river_accumulation_threshold = 200.0f;

    // Whether to sculpt channels back into your elevation (in place).
    bool  carve_rivers = false;

    // Max depth (world units) removed at highest-accumulation cells.
    float carve_depth = 1.5f;

    // Smoothing passes for carved channels (widen banks)
    int   smooth_passes = 2;
};

struct HydrologyResult {
    // D8 flow direction encoding: 0..7 (E, NE, N, NW, W, SW, S, SE), 255 = none/ocean
    std::vector<uint8_t> flow_dir;

    // Upstream contributing cells (includes the cell itself)
    std::vector<float>   accumulation;

    // 1 for river cells (above sea & above threshold), else 0
    std::vector<uint8_t> river_mask;

    // Depression-filled elevation (a.k.a. "spill elevation")
    std::vector<float>   water_level;

    // -1 for non-lake; non-negative contiguous lake index for lake cells
    std::vector<int>     lake_id;

    int num_lakes = 0;
};

namespace detail {

inline bool in_bounds(int x, int y, int w, int h) {
    return (x >= 0 && y >= 0 && x < w && y < h);
}
inline int idx(int x, int y, int w) { return y * w + x; }

// D8 (clockwise starting East)
static constexpr int DX[8] = {+1, +1,  0, -1, -1, -1,  0, +1};
static constexpr int DY[8] = { 0, -1, -1, -1,  0, +1, +1, +1};

inline int opposite(int d) { return (d + 4) & 7; }

template <class T>
inline T clamp(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

} // namespace detail

struct HydrologyGenerator {

    // --- Public API ----------------------------------------------------------

    static HydrologyResult generate(const std::vector<float>& elevation,
                                    const HydrologyParams& p)
    {
        const int W = p.width;
        const int H = p.height;
        const size_t N = static_cast<size_t>(W) * static_cast<size_t>(H);

        assert(W > 1 && H > 1);
        assert(elevation.size() == N);

        HydrologyResult R;
        R.flow_dir.resize(N, 255);
        R.accumulation.resize(N, 1.0f);
        R.river_mask.resize(N, 0);
        R.water_level.resize(N, std::numeric_limits<float>::infinity());
        R.lake_id.resize(N, -1);

        // --- 1) Priority-Flood depression filling ---------------------------
        // Min-heap keyed by current "water level" of the cell.
        using Node = std::pair<float, int>; // (water_level, index)
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

        std::vector<uint8_t> enqueued(N, 0);

        auto push_boundary = [&](int x, int y) {
            const int i = detail::idx(x, y, W);
            if (enqueued[i]) return;
            enqueued[i] = 1;
            R.water_level[i] = elevation[i]; // start from original height
            pq.emplace(R.water_level[i], i);
        };

        // Push all boundary cells.
        for (int x = 0; x < W; ++x) {
            push_boundary(x, 0);
            push_boundary(x, H - 1);
        }
        for (int y = 0; y < H; ++y) {
            push_boundary(0, y);
            push_boundary(W - 1, y);
        }

        // Process
        while (!pq.empty()) {
            const auto [wl, i] = pq.top();
            pq.pop();

            const int x = i % W;
            const int y = i / W;

            // Explore neighbors; set their water level at least wl
            for (int d = 0; d < 8; ++d) {
                const int nx = x + detail::DX[d];
                const int ny = y + detail::DY[d];
                if (!detail::in_bounds(nx, ny, W, H)) continue;

                const int j = detail::idx(nx, ny, W);
                if (enqueued[j]) continue;

                enqueued[j] = 1;

                // If the neighbor is lower than the current water surface,
                // it becomes part of the same filled basin and drains *toward i*.
                if (elevation[j] < wl) {
                    R.water_level[j] = wl;
                    // Flow from j -> i (opposite of i->j)
                    R.flow_dir[j] = static_cast<uint8_t>(detail::opposite(d));
                } else {
                    // Otherwise it sets its own level (on a ridge).
                    R.water_level[j] = elevation[j];
                    // Flow direction for ridges will be set later
                }
                pq.emplace(R.water_level[j], j);
            }
        }

        // --- 2) Flow directions for ridge cells (strictly downhill on filled surface)
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = detail::idx(x, y, W);
                if (R.flow_dir[i] != 255) continue; // already decided during fill

                const float cur = R.water_level[i];
                float best = cur;
                int best_d = 255;

                for (int d = 0; d < 8; ++d) {
                    const int nx = x + detail::DX[d];
                    const int ny = y + detail::DY[d];
                    if (!detail::in_bounds(nx, ny, W, H)) continue;
                    const int j = detail::idx(nx, ny, W);
                    const float nwl = R.water_level[j];
                    if (nwl < best) {
                        best = nwl;
                        best_d = d;
                    }
                }

                R.flow_dir[i] = static_cast<uint8_t>(best_d); // 255 if edge/ocean plateau
            }
        }

        // --- 3) Flow accumulation (Kahn over the DAG defined by flow_dir) ---
        std::vector<int> downstream(N, -1);
        std::vector<int> indeg(N, 0);

        auto to_index = [&](int x, int y, uint8_t d) -> int {
            if (d == 255) return -1;
            const int nx = x + detail::DX[d];
            const int ny = y + detail::DY[d];
            if (!detail::in_bounds(nx, ny, W, H)) return -1;
            return detail::idx(nx, ny, W);
        };

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = detail::idx(x, y, W);
                const int j = to_index(x, y, R.flow_dir[i]);
                downstream[i] = j;
                if (j >= 0) ++indeg[j];
            }
        }

        std::vector<int> q;
        q.reserve(N);
        for (int i = 0; i < (int)N; ++i) if (indeg[i] == 0) q.push_back(i);

        for (size_t qi = 0; qi < q.size(); ++qi) {
            const int i = q[qi];
            const int j = downstream[i];
            if (j >= 0) {
                R.accumulation[j] += R.accumulation[i];
                if (--indeg[j] == 0) q.push_back(j);
            }
        }

        // --- 4) Lake labelling & river extraction ---------------------------
        // Lake = cell where fill raised the surface (water_level > elevation).
        const float EPS = 1e-5f;
        int next_lake_id = 0;

        std::vector<uint8_t> visited(N, 0);
        std::vector<int> stack;
        stack.reserve(256);

        for (int i = 0; i < (int)N; ++i) {
            if (visited[i]) continue;
            if (!(R.water_level[i] > elevation[i] + EPS)) { visited[i] = 1; continue; }

            // Flood-fill this lake
            const int lake_id = next_lake_id++;
            stack.clear();
            stack.push_back(i);
            visited[i] = 1;
            R.lake_id[i] = lake_id;

            while (!stack.empty()) {
                const int v = stack.back(); stack.pop_back();
                const int vx = v % W, vy = v / W;
                for (int d = 0; d < 8; ++d) {
                    const int nx = vx + detail::DX[d];
                    const int ny = vy + detail::DY[d];
                    if (!detail::in_bounds(nx, ny, W, H)) continue;
                    const int j = detail::idx(nx, ny, W);
                    if (visited[j]) continue;
                    if (R.water_level[j] > elevation[j] + EPS) {
                        visited[j] = 1;
                        R.lake_id[j] = lake_id;
                        stack.push_back(j);
                    } else {
                        visited[j] = 1; // mark even if not part of lake
                    }
                }
            }
        }
        R.num_lakes = next_lake_id;

        // Rivers: high-accumulation channels above sea level.
        float max_acc = 0.0f;
        for (float a : R.accumulation) max_acc = std::max(max_acc, a);

        for (int i = 0; i < (int)N; ++i) {
            const bool above_sea = (R.water_level[i] > p.sea_level + EPS);
            if (above_sea && R.accumulation[i] >= p.river_accumulation_threshold) {
                R.river_mask[i] = 1;
            }
        }

        return R;
    }

    // Optional channel carving—mutates 'elevation' in place.
    static void apply_river_carving(std::vector<float>& elevation,
                                    const HydrologyResult& R,
                                    const HydrologyParams& p)
    {
        const int W = p.width;
        const int H = p.height;
        const size_t N = static_cast<size_t>(W) * static_cast<size_t>(H);
        assert(elevation.size() == N);
        if (!p.carve_rivers) return;

        // Find max accumulation among river cells for normalization.
        float max_river_acc = 1.0f;
        for (int i = 0; i < (int)N; ++i)
            if (R.river_mask[i]) max_river_acc = std::max(max_river_acc, R.accumulation[i]);

        std::vector<float> carved = elevation;

        // Carve: deeper for larger channels (sqrt taper), keep above a floor.
        for (int i = 0; i < (int)N; ++i) {
            if (!R.river_mask[i]) continue;
            const float t = detail::clamp(R.accumulation[i] / std::max(1.0f, max_river_acc), 0.0f, 1.0f);
            const float depth = p.carve_depth * (0.3f + 0.7f * std::sqrt(t));
            carved[i] = std::min(carved[i], R.water_level[i]) - depth;
            carved[i] = std::max(carved[i], p.sea_level - 50.0f); // safety floor
        }

        // Feather banks with light diffusion around river cells.
        std::vector<float> tmp = carved;
        for (int pass = 0; pass < p.smooth_passes; ++pass) {
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const int i = detail::idx(x, y, W);
                    if (!R.river_mask[i]) { tmp[i] = carved[i]; continue; }

                    float sum = carved[i];
                    int cnt = 1;
                    // 8-neighborhood smoothing
                    for (int d = 0; d < 8; ++d) {
                        const int nx = x + detail::DX[d];
                        const int ny = y + detail::DY[d];
                        if (!detail::in_bounds(nx, ny, W, H)) continue;
                        const int j = detail::idx(nx, ny, W);
                        sum += carved[j];
                        ++cnt;
                    }
                    tmp[i] = 0.5f * carved[i] + 0.5f * (sum / std::max(1, cnt));
                }
            }
            carved.swap(tmp);
        }

        elevation.swap(carved);
    }
};

} // namespace procgen
} // namespace colony
