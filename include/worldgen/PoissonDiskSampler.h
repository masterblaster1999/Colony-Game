#pragma once
/*
    PoissonDiskSampler.h  —  header-only blue-noise (Poisson-disc) sampler
    For Colony-Game (C++)

    What you get
    ------------
    • Constant-radius Poisson-disc sampler (Bridson 2007, O(N))
    • Variable-radius sampler (radius field; useful for biomes/fertility masks)
    • Chunk-edge stitching via "boundary" seeds to prevent seams between tiles
    • Deterministic results via 64-bit seed

    Why it helps the colony game
    ----------------------------
    • Natural-looking distributions without clumps (great for trees, ore, fauna)
    • Even spacing helps pathfinding and readability
    • Variable radius lets you place more trees near rivers, sparse in deserts, etc.

    References (background reading)
    --------------------------------
    • Bridson, "Fast Poisson Disk Sampling in Arbitrary Dimensions" (SIGGRAPH 2007)
      – O(N) algorithm using a background grid and active list.
    • Red Blob Games: practical notes on Poisson-disc and using it in maps.
    • Variable/anisotropic density approaches (see Mitchell et al. 2012).

    This implementation is self-contained and uses only the C++17 standard library.
*/

#include <vector>
#include <random>
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>
#include <array>

namespace worldgen {

// ------------------------ Basic types ------------------------

struct Float2 {
    float x{};
    float y{};
};

struct Rect {
    float x0{}, y0{}, x1{}, y1{};
    float width()  const { return x1 - x0; }
    float height() const { return y1 - y0; }
    bool contains(const Float2& p) const {
        return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1;
    }
};

// For variable-radius sampling and chunk stitching
struct SeedPoint {
    Float2 p;     // position
    float  r;     // local radius (minimum spacing at this point)
};

// Tuning parameters
struct PoissonParams {
    int      k    = 30;       // candidates per active point (Bridson uses 30)
    uint64_t seed = 1337ull;  // RNG seed for determinism
};

// ------------------------ Utilities ------------------------

namespace detail {

constexpr float PI = 3.14159265358979323846f;

inline float randf(std::mt19937_64& rng, float a, float b) {
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng);
}

inline Float2 randOnAnnulus(std::mt19937_64& rng, float rmin, float rmax) {
    // Sample radius uniformly in area → pick r ~ [rmin, rmax] with pdf ∝ r
    // We approximate by uniform r for simplicity; visually fine in practice.
    const float r     = randf(rng, rmin, rmax);
    const float theta = randf(rng, 0.0f, 2.0f * PI);
    return { r * std::cos(theta), r * std::sin(theta) };
}

inline int floordiv(float v, float s) {
    return static_cast<int>(std::floor(v / s));
}

} // namespace detail

// ============================================================
// 1) Constant-radius Poisson-disc sampler (Bridson 2007 style)
// ============================================================

/*
    PoissonDisk()
    -------------
    Generate evenly distributed points with minimum spacing `radius`
    inside `bounds`. Optional `boundary` points are inserted first but
    are NOT active emitters; use them to stitch chunk edges.

    Returns a vector of positions (Float2).
*/
inline std::vector<Float2>
PoissonDisk(const Rect& bounds,
            float radius,
            const PoissonParams& params = {},
            const std::vector<Float2>& boundary = {})
{
    if (radius <= 0.0f || bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
        return {};
    }

    std::mt19937_64 rng(params.seed);
    const float cellSize = radius / std::sqrt(2.0f);
    const int   gw = std::max(1, static_cast<int>(std::ceil(bounds.width()  / cellSize)));
    const int   gh = std::max(1, static_cast<int>(std::ceil(bounds.height() / cellSize)));

    // grid stores index of sample at each cell; -1 = empty
    std::vector<int> grid(static_cast<size_t>(gw) * static_cast<size_t>(gh), -1);
    auto gridIndex = [&](int gx, int gy) -> int {
        return gy * gw + gx;
    };
    auto toGridCell = [&](const Float2& p) {
        const int gx = std::clamp(detail::floordiv(p.x - bounds.x0, cellSize), 0, gw - 1);
        const int gy = std::clamp(detail::floordiv(p.y - bounds.y0, cellSize), 0, gh - 1);
        return std::array<int,2>{gx, gy};
    };

    std::vector<Float2> samples;
    samples.reserve(static_cast<size_t>((bounds.width()*bounds.height()) / (radius*radius)));

    std::vector<int> active; // indices into samples

    auto placeSample = [&](const Float2& p, bool makeActive) {
        const auto cell = toGridCell(p);
        const int  idx  = static_cast<int>(samples.size());
        samples.push_back(p);
        grid[gridIndex(cell[0], cell[1])] = idx;
        if (makeActive) active.push_back(idx);
    };

    // Insert boundary seeds (if any) but don't activate them;
    // they only enforce spacing near chunk edges.
    for (const auto& bp : boundary) {
        if (!bounds.contains(bp)) continue;
        const auto cell = toGridCell(bp);
        // If cell empty, insert; otherwise keep existing
        if (grid[gridIndex(cell[0], cell[1])] == -1) {
            placeSample(bp, /*makeActive=*/false);
        }
    }

    // First active sample: uniform in domain
    const Float2 p0 { detail::randf(rng, bounds.x0, bounds.x1),
                      detail::randf(rng, bounds.y0, bounds.y1) };
    placeSample(p0, /*makeActive=*/true);

    const float radius2 = radius * radius;

    auto isFarEnough = [&](const Float2& p) -> bool {
        const auto cell = toGridCell(p);
        const int gx = cell[0], gy = cell[1];

        // Neighbor range of 2 cells in each direction is sufficient with s=r/sqrt(2)
        for (int ny = std::max(0, gy - 2); ny <= std::min(gh - 1, gy + 2); ++ny) {
            for (int nx = std::max(0, gx - 2); nx <= std::min(gw - 1, gx + 2); ++nx) {
                const int sidx = grid[gridIndex(nx, ny)];
                if (sidx < 0) continue;
                const Float2& q = samples[static_cast<size_t>(sidx)];
                const float dx = p.x - q.x, dy = p.y - q.y;
                if (dx*dx + dy*dy < radius2) return false;
            }
        }
        return true;
    };

    // Main loop
    while (!active.empty()) {
        // Pick a random active index
        std::uniform_int_distribution<int> pick(0, static_cast<int>(active.size()) - 1);
        const int activeSlot = pick(rng);
        const int sidx       = active[static_cast<size_t>(activeSlot)];
        const Float2 base    = samples[static_cast<size_t>(sidx)];

        bool found = false;
        for (int attempt = 0; attempt < params.k; ++attempt) {
            const Float2 d   = detail::randOnAnnulus(rng, radius, 2.0f * radius);
            const Float2 cand{ base.x + d.x, base.y + d.y };
            if (!bounds.contains(cand)) continue;
            if (!isFarEnough(cand))     continue;

            placeSample(cand, /*makeActive=*/true);
            found = true;
            break;
        }

        if (!found) {
            // Remove this active by swapping with last for O(1)
            active[static_cast<size_t>(activeSlot)] = active.back();
            active.pop_back();
        }
    }

    return samples;
}

// ====================================================================
// 2) Variable-radius Poisson-disc sampler (for biome-driven densities)
// ====================================================================

/*
    PoissonDiskVariable()
    ---------------------
    Like PoissonDisk(), but the minimum spacing is a function of position:
      r(x,y) = radius_at(x,y), clamped to [minRadius, maxRadiusHint]

    • Use this to place *more* samples where r(x,y) is smaller (dense forest)
      and *fewer* where it’s larger (barren).
    • For chunk stitching, pass `boundary` = edge samples from neighbor chunks
      (with their local radii). They are inserted but not activated.

    NOTE: We use a uniform grid sized for `minRadius` and search neighbors
    within ±ceil(maxRadiusHint / cellSize) cells. Ensure `maxRadiusHint`
    ≥ the true maximum radius in your map for strict guarantees.

    Returns vector<SeedPoint> with per-sample radius.
*/
inline std::vector<SeedPoint>
PoissonDiskVariable(const Rect& bounds,
                    const std::function<float(float,float)>& radius_at,
                    float minRadius,
                    float maxRadiusHint,
                    const PoissonParams& params = {},
                    const std::vector<SeedPoint>& boundary = {})
{
    if (minRadius <= 0.0f || maxRadiusHint < minRadius ||
        bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
        return {};
    }

    std::mt19937_64 rng(params.seed);
    const float cellSize = minRadius / std::sqrt(2.0f);
    const int   gw = std::max(1, static_cast<int>(std::ceil(bounds.width()  / cellSize)));
    const int   gh = std::max(1, static_cast<int>(std::ceil(bounds.height() / cellSize)));
    std::vector<int> grid(static_cast<size_t>(gw) * static_cast<size_t>(gh), -1);

    auto gridIndex = [&](int gx, int gy) -> int { return gy * gw + gx; };
    auto toGridCell = [&](const Float2& p) {
        const int gx = std::clamp(detail::floordiv(p.x - bounds.x0, cellSize), 0, gw - 1);
        const int gy = std::clamp(detail::floordiv(p.y - bounds.y0, cellSize), 0, gh - 1);
        return std::array<int,2>{gx, gy};
    };

    const int neighborRange = std::max(2,
        static_cast<int>(std::ceil(maxRadiusHint / cellSize)) + 1);

    std::vector<SeedPoint> samples;
    samples.reserve(static_cast<size_t>(
        (bounds.width()*bounds.height()) / (minRadius*minRadius)));

    std::vector<int> active;

    auto clampRadius = [&](float r) -> float {
        if (!(r == r) || !std::isfinite(r)) r = minRadius; // sanitize NaN/inf
        return std::clamp(r, minRadius, maxRadiusHint);
    };

    auto placeSample = [&](const Float2& p, float r, bool makeActive) {
        const auto cell = toGridCell(p);
        const int  idx  = static_cast<int>(samples.size());
        samples.push_back(SeedPoint{p, r});
        grid[gridIndex(cell[0], cell[1])] = idx;
        if (makeActive) active.push_back(idx);
    };

    auto isFarEnough = [&](const Float2& p, float rCand) -> bool {
        const auto cell = toGridCell(p);
        const int gx = cell[0], gy = cell[1];

        for (int ny = std::max(0, gy - neighborRange); ny <= std::min(gh - 1, gy + neighborRange); ++ny) {
            for (int nx = std::max(0, gx - neighborRange); nx <= std::min(gw - 1, gx + neighborRange); ++nx) {
                const int sidx = grid[gridIndex(nx, ny)];
                if (sidx < 0) continue;
                const SeedPoint& q = samples[static_cast<size_t>(sidx)];
                const float dx = p.x - q.p.x, dy = p.y - q.p.y;
                const float rr = std::max(rCand, q.r);
                if (dx*dx + dy*dy < rr*rr) return false;
            }
        }
        return true;
    };

    // Insert boundary seeds (edge points from neighbor chunks)
    for (const auto& b : boundary) {
        if (!bounds.contains(b.p)) continue;
        const auto cell = toGridCell(b.p);
        if (grid[gridIndex(cell[0], cell[1])] == -1 && isFarEnough(b.p, clampRadius(b.r))) {
            placeSample(b.p, clampRadius(b.r), /*makeActive=*/false);
        }
    }

    // First active sample: uniform in bounds, radius from radius_at()
    const Float2 p0 { detail::randf(rng, bounds.x0, bounds.x1),
                      detail::randf(rng, bounds.y0, bounds.y1) };
    const float r0 = clampRadius(radius_at(p0.x, p0.y));
    placeSample(p0, r0, /*makeActive=*/true);

    while (!active.empty()) {
        std::uniform_int_distribution<int> pick(0, static_cast<int>(active.size()) - 1);
        const int activeSlot = pick(rng);
        const int sidx       = active[static_cast<size_t>(activeSlot)];
        const SeedPoint base = samples[static_cast<size_t>(sidx)];

        bool found = false;
        for (int attempt = 0; attempt < params.k; ++attempt) {
            // Sample around the parent using its local radius as step
            const Float2 d   = detail::randOnAnnulus(rng, base.r, 2.0f * base.r);
            const Float2 cand{ base.p.x + d.x, base.p.y + d.y };
            if (!bounds.contains(cand)) continue;

            const float rCand = clampRadius(radius_at(cand.x, cand.y));
            if (!isFarEnough(cand, rCand)) continue;

            placeSample(cand, rCand, /*makeActive=*/true);
            found = true;
            break;
        }

        if (!found) {
            active[static_cast<size_t>(activeSlot)] = active.back();
            active.pop_back();
        }
    }

    return samples;
}

} // namespace worldgen

/*
---------------------------------- Usage ----------------------------------

#include "worldgen/PoissonDiskSampler.h"

void placeTrees(MyWorld& world, uint64_t seed) {
    using namespace worldgen;

    Rect map { 0.0f, 0.0f, world.width(), world.height() };

    // 1) Constant density (uniform forest)
    auto trees = PoissonDisk(map, /*radius=*/12.0f, PoissonParams{30, seed});
    for (auto& p : trees) world.spawnTree(p.x, p.y);

    // 2) Variable density (e.g., more trees near water)
    auto radius_at = [&](float x, float y) {
        float moisture = world.sampleMoisture(x, y); // [0,1]
        // Denser where moisture high → smaller radius there
        // Radius range: 8..22 (tune for your scale)
        return 22.0f - 14.0f * moisture;
    };

    // If generating per-chunk, collect neighbor-edge seeds as `boundary`.
    std::vector<SeedPoint> boundarySeeds = world.collectNeighborEdgeSeeds(/*...*/);

    auto var = PoissonDiskVariable(map, radius_at,
                                   /*minRadius=*/6.0f,
                                   /*maxRadiusHint=*/24.0f,
                                   PoissonParams{30, seed ^ 0xCAFE1234ull},
                                   boundarySeeds);

    for (auto& s : var) world.spawnBush(s.p.x, s.p.y);
}

Notes:
• For chunked worlds, pass edge samples from already-generated neighbor chunks
  to the next chunk's `boundary` to eliminate seams on chunk borders.
• `k` (attempts) of 15–30 balances speed vs. saturation.
• To exclude water tiles or roads, wrap your `radius_at` with a predicate and
  return a very large radius (or skip candidates that fall outside).
*/
