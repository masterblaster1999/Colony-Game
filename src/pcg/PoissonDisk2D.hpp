#pragma once
/*
  PoissonDisk2D.hpp  —  Header-only Bridson Poisson‑disk sampler (2D)

  Implements:
    std::vector<pcg::Vec2f> pcg::poisson_disk_2d(const Params& P);

  Key facts (Bridson, 2007):
    • Background grid cell size <= r / sqrt(n) (n=2) → at most one sample per cell
    • Maintain an active list of samples to spawn from
    • Spawn up to k candidates uniformly in the annulus [r, 2r] about a random active sample
    • Accept the first candidate that is ≥ r from all existing samples; else retire the active sample
    • Expected O(N) time

  References:
    Robert Bridson, “Fast Poisson Disk Sampling in Arbitrary Dimensions,” SIGGRAPH Sketches, 2007.
*/

#include <vector>
#include <functional>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <limits>

namespace pcg {

struct Vec2f {
    float x, y;
};

struct PoissonParams2D {
    float width  = 1.0f;    // domain width  (x in [0,width))
    float height = 1.0f;    // domain height (y in [0,height))
    float r      = 0.1f;    // minimum spacing between samples
    int   k      = 30;      // attempts per active sample (Bridson suggests ~30)
    uint32_t seed = 1337u;  // RNG seed
    bool wrap    = false;   // toroidal domain (tileable) if true

    // Optional spatial predicate: return true to allow a position, false to reject.
    // Useful for slope/biome masks etc. If empty, all positions are allowed.
    std::function<bool(float /*x*/, float /*y*/)> allow{};
};

// Returns a set of 2D points with minimum spacing >= r inside [0,width)×[0,height).
inline std::vector<Vec2f> poisson_disk_2d(const PoissonParams2D& P)
{
    std::vector<Vec2f> samples;

    if (!(P.width > 0 && P.height > 0 && P.r > 0)) return samples;
    const float r = P.r;
    const int   k = std::max(1, P.k);

    // Background grid. With n=2 we use cell = r / sqrt(2).
    const float cell = r / std::sqrt(2.0f);
    const int gx = std::max(1, int(std::ceil(P.width  / cell)));
    const int gy = std::max(1, int(std::ceil(P.height / cell)));
    std::vector<int> grid(gx * gy, -1);

    // Utilities
    auto wrapf = [](float v, float maxv) -> float {
        if (maxv <= 0) return v;
        v = std::fmod(v, maxv);
        if (v < 0) v += maxv;
        return v;
    };
    auto inside = [&](float x, float y) -> bool {
        if (P.wrap) return true;
        return (x >= 0.f && x < P.width && y >= 0.f && y < P.height);
    };
    auto cellIndex = [&](float x, float y) -> std::pair<int,int> {
        int ix = int(std::floor(x / cell));
        int iy = int(std::floor(y / cell));
        if (P.wrap) {
            ix = (ix % gx + gx) % gx;
            iy = (iy % gy + gy) % gy;
        } else {
            ix = std::clamp(ix, 0, gx-1);
            iy = std::clamp(iy, 0, gy-1);
        }
        return {ix, iy};
    };
    auto gridAt = [&](int ix, int iy) -> int& {
        if (P.wrap) {
            ix = (ix % gx + gx) % gx;
            iy = (iy % gy + gy) % gy;
        }
        return grid[iy * gx + ix];
    };

    std::mt19937 rng(P.seed);
    std::uniform_real_distribution<float> U01(0.f, 1.f);

    // Reserve a reasonable amount (rough heuristic).
    samples.reserve(size_t((P.width * P.height) / (r * r)));

    std::vector<int> active; active.reserve(128);

    auto pushSample = [&](const Vec2f& p){
        samples.push_back(p);
        int idx = int(samples.size()) - 1;
        auto [ix, iy] = cellIndex(p.x, p.y);
        grid[iy * gx + ix] = idx;
        active.push_back(idx);
    };

    // Pick an initial sample uniformly at random; try a few times if the allow() predicate rejects.
    {
        bool seeded = false;
        for (int tries = 0; tries < 1024 && !seeded; ++tries) {
            Vec2f s { U01(rng) * P.width, U01(rng) * P.height };
            if (!P.allow || P.allow(s.x, s.y)) {
                pushSample(s);
                seeded = true;
            }
        }
        if (!seeded) return samples; // nothing allowed
    }

    // Neighbor search: with cell = r/sqrt(2), checking a 5×5 neighborhood (±2 cells)
    // conservatively covers all positions within distance < r (Bridson 2007).
    const int NEIGHBOR_RADIUS = 2;

    const float r2 = r * r;
    const float min2 = r2;
    const float max2 = (2.f * r) * (2.f * r); // for candidate radius sampling

    constexpr float PI = 3.14159265358979323846f;

    auto tooClose = [&](const Vec2f& p) -> bool {
        auto [ix, iy] = cellIndex(p.x, p.y);
        for (int dy = -NEIGHBOR_RADIUS; dy <= NEIGHBOR_RADIUS; ++dy)
            for (int dx = -NEIGHBOR_RADIUS; dx <= NEIGHBOR_RADIUS; ++dx) {
                int nx = ix + dx, ny = iy + dy;
                int gi = -1;
                if (P.wrap) {
                    gi = gridAt(nx, ny);
                } else if (nx >= 0 && ny >= 0 && nx < gx && ny < gy) {
                    gi = grid[ny * gx + nx];
                }
                if (gi >= 0) {
                    const Vec2f& q = samples[(size_t)gi];
                    const float ddx = p.x - q.x;
                    const float ddy = p.y - q.y;
                    if (ddx*ddx + ddy*ddy < min2) return true;
                }
            }
        return false;
    };

    // Main loop (Bridson)
    while (!active.empty()) {
        // pick a random active sample
        int ai = std::uniform_int_distribution<int>(0, int(active.size()) - 1)(rng);
        const Vec2f base = samples[(size_t)active[ai]];
        bool found = false;

        for (int attempt = 0; attempt < k; ++attempt) {
            // Uniform area sampling in annulus [r, 2r]
            float u = U01(rng);
            float radius = std::sqrt(min2 + u * (max2 - min2));
            float theta  = 2.f * PI * U01(rng);

            Vec2f p {
                base.x + radius * std::cos(theta),
                base.y + radius * std::sin(theta)
            };

            if (P.wrap) {
                p.x = wrapf(p.x, P.width);
                p.y = wrapf(p.y, P.height);
            }
            if (!inside(p.x, p.y)) continue;
            if (P.allow && !P.allow(p.x, p.y)) continue;
            if (!tooClose(p)) {
                pushSample(p);
                found = true;
                break;
            }
        }

        if (!found) {
            // retire this active sample
            active[ai] = active.back();
            active.pop_back();
        }
    }

    return samples;
}

} // namespace pcg
