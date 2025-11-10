#pragma once
// -----------------------------------------------------------------------------
// PoissonDisk.hpp - Header-only blue-noise (Poisson-disk) point sampler for 2D
// -----------------------------------------------------------------------------
// Usage (example):
//   #include "colony/procgen/PoissonDisk.hpp"
//   using namespace colony::procgen;
//   PoissonOptions opt;
//   opt.width  = 2048.0f;
//   opt.height = 2048.0f;
//   opt.radius = 32.0f;         // minimum spacing (world units / pixels)
//   opt.k      = 30;            // attempts per active sample (Bridson's k)
//   opt.seed   = 1337;
//   // Optional: mask to reject water tiles, etc.
//   opt.accept = [&](float x, float y) { return isLandAt(x, y); };
//   auto points = poissonDisk(opt);
//
// Notes:
//  * Deterministic across runs for a given seed.
//  * O(N) expected time using a background grid of side s = r / sqrt(2).
//  * Good default: k = 30 (per Bridson).
//
// References:
//  * R. Bridson, "Fast Poisson Disk Sampling in Arbitrary Dimensions", SIGGRAPH 2007. 
//    https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf
//
// This implementation adheres to the standard algorithm structure:
//  - pick a random initial sample, push to active list
//  - while active list not empty, pick a random active sample,
//    generate up to k candidates uniformly in the annulus [r, 2r]
//    and accept the first candidate not within r of any existing point
//    (neighbor checks via the grid); else remove the active sample.
//
// -----------------------------------------------------------------------------

#include <vector>
#include <functional>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>

namespace colony::procgen
{
    struct Vec2f {
        float x{}, y{};
    };

    struct PoissonOptions {
        float   width  = 0.0f;    // domain width  (e.g., world units or pixels)
        float   height = 0.0f;    // domain height
        float   radius = 16.0f;   // minimum distance between samples
        uint32_t k     = 30;      // attempts per active point
        uint64_t seed  = 1337;    // RNG seed (deterministic)

        // Optional acceptance mask (e.g., reject water tiles).
        // If unset, all domain points are acceptable.
        // Must be fast (called often).
        std::function<bool(float, float)> accept;
    };

    // Returns a set of evenly-spaced points in [0,width]x[0,height].
    inline std::vector<Vec2f> poissonDisk(const PoissonOptions& opt)
    {
        std::vector<Vec2f> samples;
        if (opt.width <= 0.0f || opt.height <= 0.0f || opt.radius <= 0.0f || opt.k == 0)
            return samples;

        // --- RNG ----------------------------------------------------------------
        std::mt19937_64 rng(opt.seed);
        std::uniform_real_distribution<float> urand01(0.0f, 1.0f);

        // --- Grid (cell size so each holds at most one sample) ------------------
        const float cell = opt.radius / std::sqrt(2.0f);
        const int gridW = static_cast<int>(std::ceil(opt.width  / cell));
        const int gridH = static_cast<int>(std::ceil(opt.height / cell));
        const int gridSize = (gridW > 0 && gridH > 0) ? gridW * gridH : 0;
        if (gridSize <= 0) return samples;

        // Grid stores index of sample in 'samples' or -1 for empty.
        std::vector<int> grid(static_cast<size_t>(gridSize), -1);

        auto gridIndex = [&](float x, float y) -> int {
            const int gx = static_cast<int>(x / cell);
            const int gy = static_cast<int>(y / cell);
            if (gx < 0 || gy < 0 || gx >= gridW || gy >= gridH) return -1;
            return gy * gridW + gx;
        };

        auto inBounds = [&](float x, float y) -> bool {
            return (x >= 0.0f && x < opt.width && y >= 0.0f && y < opt.height);
        };

        auto isAcceptable = [&](float x, float y) -> bool {
            if (!inBounds(x, y)) return false;
            if (opt.accept && !opt.accept(x, y)) return false;

            // Neighbor check: for s = r/sqrt(2), need to search a 5x5 block around cell.
            const int gi = gridIndex(x, y);
            if (gi < 0) return false;

            const int gx = gi % gridW;
            const int gy = gi / gridW;

            const float r2 = opt.radius * opt.radius;

            for (int ny = std::max(0, gy - 2); ny <= std::min(gridH - 1, gy + 2); ++ny) {
                for (int nx = std::max(0, gx - 2); nx <= std::min(gridW - 1, gx + 2); ++nx) {
                    const int nidx = ny * gridW + nx;
                    const int sidx = grid[nidx];
                    if (sidx >= 0) {
                        const float dx = samples[static_cast<size_t>(sidx)].x - x;
                        const float dy = samples[static_cast<size_t>(sidx)].y - y;
                        if (dx*dx + dy*dy < r2) return false;
                    }
                }
            }
            return true;
        };

        auto pushSample = [&](float x, float y) {
            const int gi = gridIndex(x, y);
            if (gi < 0) return false;
            grid[gi] = static_cast<int>(samples.size());
            samples.push_back({x, y});
            return true;
        };

        // --- Active list --------------------------------------------------------
        std::vector<Vec2f> active;
        active.reserve(128);
        samples.reserve(256);

        // Seed with a random initial sample
        {
            const float x0 = urand01(rng) * opt.width;
            const float y0 = urand01(rng) * opt.height;
            if (!isAcceptable(x0, y0)) {
                // Try a few times to find a valid seed before giving up.
                bool seeded = false;
                for (int tries = 0; tries < 32 && !seeded; ++tries) {
                    const float xt = urand01(rng) * opt.width;
                    const float yt = urand01(rng) * opt.height;
                    if (isAcceptable(xt, yt)) {
                        pushSample(xt, yt);
                        active.push_back({xt, yt});
                        seeded = true;
                    }
                }
                if (!seeded) return samples; // mask excludes domain
            } else {
                pushSample(x0, y0);
                active.push_back({x0, y0});
            }
        }

        // Precompute annulus randomization helpers:
        // radius in [r, 2r], angle in [0, 2π]
        auto randomCandidateAround = [&](const Vec2f& s) -> Vec2f {
            const float ang = urand01(rng) * 6.28318530717958647692f; // 2π
            const float rad = opt.radius * (1.0f + urand01(rng));     // [r, 2r]
            return { s.x + rad * std::cos(ang), s.y + rad * std::sin(ang) };
        };

        // --- Bridson loop -------------------------------------------------------
        // While there are active points, generate up to k candidates around a random one.
        while (!active.empty()) {
            // Pick random active index
            const size_t idx = static_cast<size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(active.size() - 1))(rng));
            const Vec2f base = active[idx];

            bool found = false;
            for (uint32_t attempt = 0; attempt < opt.k; ++attempt) {
                Vec2f cand = randomCandidateAround(base);
                if (isAcceptable(cand.x, cand.y)) {
                    pushSample(cand.x, cand.y);
                    active.push_back(cand);
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Remove from active list (swap-pop)
                active[idx] = active.back();
                active.pop_back();
            }
        }

        return samples;
    }

} // namespace colony::procgen
