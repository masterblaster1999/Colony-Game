#pragma once

// Header-only ore vein generator.
// Produces a per-tile ore mask + optional per-tile ResourceInstance list.
//
// Intended use:
//   - Call after height/biome generation to create more "geological" deposits
//     than simple point scattering.
//   - Append returned instances into WorldData::resources (if you store per-tile resources).

#include "Types.h"
#include "Biome.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <type_traits>
#include <vector>

namespace procgen {

struct OreVeinParams {
    // Determinism
    uint32_t seed = 1337u;

    // Number of independent vein "systems".
    int veinCount = 48;

    // Random-walk length per vein system (in tile steps).
    int minLength = 32;
    int maxLength = 128;

    // Vein thickness (disk radius around the walker).
    // 0 = single-tile thread, 1..2 = chunkier.
    int minRadius = 1;
    int maxRadius = 2;

    // Branching behavior.
    float branchChance = 0.07f;     // chance per step to spawn a branch
    int maxBranchesPerVein = 2;     // cap branches spawned from one vein system

    // Where veins are allowed to appear (based on heightmap).
    float minHeight = 0.58f;        // [0..1] heightmap threshold (tune for your world)
    float maxHeight = 1.00f;

    // If true, restrict to colder/highland biomes by default (ore tends to be mountain-ish).
    bool restrictToMountainBiomes = true;

    // Walker steering (tuning knobs)
    float heightBias = 1.8f;        // prefer higher ground
    float inertia = 0.55f;          // prefer continuing direction (0..1)

    // Ore mix
    float copperChance = 0.35f;     // otherwise iron

    // Overlap handling
    bool allowOverlap = false;      // if false: don't overwrite existing ore mask cells
    bool avoidSteppingIntoOre = true;

    // Emit a per-tile ResourceInstance list as a convenience
    bool emitInstances = true;
};

struct OreVeinResult {
    int w = 0;
    int h = 0;

    // 0 for empty, otherwise stores underlying ResourceType value
    // (ResourceType is enum struct : uint8_t in your Types.h).
    std::vector<uint8_t> oreMask;

    // Optional: one ResourceInstance per non-zero tile in oreMask.
    std::vector<ResourceInstance> instances;
};

namespace detail {

inline int idx(int x, int y, int w) { return y * w + x; }

inline float rand01(std::mt19937& rng) {
    return std::generate_canonical<float, 24>(rng);
}

template <class T>
inline Biome toBiome(T v) noexcept {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, Biome>) {
        return v;
    } else {
        return static_cast<Biome>(v);
    }
}

inline bool isLand(Biome b) noexcept {
    return !(b == Biome::Ocean || b == Biome::Beach);
}

inline bool isMountainish(Biome b) noexcept {
    switch (b) {
        case Biome::Mountain:
        case Biome::Snow:
        case Biome::Tundra:
        case Biome::Taiga:
            return true;
        default:
            return false;
    }
}

inline void markDisk(
    std::vector<uint8_t>& mask,
    int w, int h,
    int cx, int cy,
    int r,
    uint8_t value,
    bool allowOverlap
) {
    const int r2 = r * r;
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= h) continue;
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= w) continue;
            if (dx * dx + dy * dy > r2) continue;

            const int i = idx(x, y, w);
            if (!allowOverlap && mask[i] != 0) continue;
            mask[i] = value;
        }
    }
}

struct Walker {
    int x = 0, y = 0;
    int prev_dx = 0, prev_dy = 0;
    int steps = 0;
    int branchesLeft = 0;
    int radius = 1;
    uint8_t oreValue = 0;
};

inline bool chooseNextStep(
    const std::vector<float>& height,
    const std::vector<uint8_t>& oreMask,
    int w, int h,
    int& x, int& y,
    int& prev_dx, int& prev_dy,
    const OreVeinParams& p,
    std::mt19937& rng
) {
    static constexpr std::array<std::pair<int, int>, 8> dirs{{
        { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    }};

    const float h0 = height[idx(x, y, w)];

    struct Opt { int dx, dy; float w; };
    std::array<Opt, 8> opts{};
    int count = 0;
    float total = 0.f;

    for (auto [dx, dy] : dirs) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;

        const int ni = idx(nx, ny, w);

        if (p.avoidSteppingIntoOre && !p.allowOverlap && oreMask[ni] != 0) {
            continue;
        }

        const float h1 = height[ni];

        // Soft constraint here; hard constraint can be enforced by caller.
        if (h1 < p.minHeight * 0.90f || h1 > p.maxHeight) continue;

        const float dh = h1 - h0;

        float weight = 1.0f;

        // Prefer moving toward higher ground
        weight += p.heightBias * std::max(0.0f, dh);

        // Momentum / direction inertia
        if (prev_dx != 0 || prev_dy != 0) {
            const float dot = float(dx * prev_dx + dy * prev_dy); // [-2..2]
            const float inertia = std::clamp(p.inertia, 0.0f, 1.0f);
            weight *= (1.0f + inertia * (dot / 2.0f));
        }

        // Randomness so it doesn't become too uniform
        weight *= (0.80f + 0.40f * rand01(rng));

        if (weight <= 0.f) continue;

        opts[count++] = { dx, dy, weight };
        total += weight;
    }

    if (count == 0 || total <= 0.f) return false;

    float r = rand01(rng) * total;
    for (int i = 0; i < count; ++i) {
        r -= opts[i].w;
        if (r <= 0.f) {
            prev_dx = opts[i].dx;
            prev_dy = opts[i].dy;
            x += opts[i].dx;
            y += opts[i].dy;
            return true;
        }
    }

    // Fallback
    prev_dx = opts[count - 1].dx;
    prev_dy = opts[count - 1].dy;
    x += prev_dx;
    y += prev_dy;
    return true;
}

template <class BiomeLike>
inline OreVeinResult generateImpl(
    const std::vector<float>& height,
    const std::vector<BiomeLike>& biomes,
    int w, int h,
    const OreVeinParams& p
) {
    OreVeinResult out;
    out.w = w;
    out.h = h;

    if (w <= 0 || h <= 0) return out;
    if (height.size() != size_t(w) * size_t(h)) return out;
    if (biomes.size() != size_t(w) * size_t(h)) return out;

    out.oreMask.assign(size_t(w) * size_t(h), 0);

    std::mt19937 rng(p.seed);

    const int minLen = std::max(1, std::min(p.minLength, p.maxLength));
    const int maxLen = std::max(1, std::max(p.minLength, p.maxLength));
    const int minRad = std::max(0, std::min(p.minRadius, p.maxRadius));
    const int maxRad = std::max(0, std::max(p.minRadius, p.maxRadius));

    std::uniform_int_distribution<int> lenDist(minLen, maxLen);
    std::uniform_int_distribution<int> radDist(minRad, maxRad);
    std::uniform_int_distribution<int> xDist(0, w - 1);
    std::uniform_int_distribution<int> yDist(0, h - 1);

    auto biomeOk = [&](Biome b) {
        if (!isLand(b)) return false;
        if (p.restrictToMountainBiomes) return isMountainish(b);
        return true;
    };

    auto cellOk = [&](int x, int y) {
        const int i = idx(x, y, w);
        if (height[i] < p.minHeight || height[i] > p.maxHeight) return false;
        const Biome b = toBiome(biomes[i]);
        return biomeOk(b);
    };

    constexpr int kMaxStartAttempts = 10'000;

    for (int v = 0; v < p.veinCount; ++v) {
        int sx = 0, sy = 0;
        bool found = false;

        for (int a = 0; a < kMaxStartAttempts; ++a) {
            const int x = xDist(rng);
            const int y = yDist(rng);
            if (!cellOk(x, y)) continue;

            const int i = idx(x, y, w);
            if (!p.allowOverlap && out.oreMask[i] != 0) continue;

            sx = x; sy = y;
            found = true;
            break;
        }

        if (!found) continue;

        Walker root;
        root.x = sx;
        root.y = sy;
        root.prev_dx = 0;
        root.prev_dy = 0;
        root.steps = lenDist(rng);
        root.branchesLeft = std::max(0, p.maxBranchesPerVein);
        root.radius = radDist(rng);

        const bool copper = (rand01(rng) < p.copperChance);
        const ResourceType rt = copper ? ResourceType::OreCopper : ResourceType::OreIron;
        root.oreValue = static_cast<uint8_t>(rt);

        std::vector<Walker> stack;
        stack.push_back(root);

        while (!stack.empty()) {
            Walker wlk = stack.back();
            stack.pop_back();

            int x = wlk.x;
            int y = wlk.y;
            int pdx = wlk.prev_dx;
            int pdy = wlk.prev_dy;

            for (int step = 0; step < wlk.steps; ++step) {
                if (!cellOk(x, y)) break;

                markDisk(out.oreMask, w, h, x, y, wlk.radius, wlk.oreValue, p.allowOverlap);

                // Branch: spawn a shorter walker that turns sideways-ish
                if (wlk.branchesLeft > 0 && rand01(rng) < p.branchChance) {
                    Walker br = wlk;
                    br.steps = std::max(6, int(float(wlk.steps - step) * (0.35f + 0.35f * rand01(rng))));
                    br.branchesLeft = wlk.branchesLeft - 1;

                    // Small thickness variation on branches
                    if (maxRad > minRad) {
                        br.radius = std::clamp(wlk.radius + (rand01(rng) < 0.5f ? -1 : 1), minRad, maxRad);
                    }

                    // Rotate direction by 90 degrees if possible; otherwise pick random cardinal direction
                    if (pdx != 0 || pdy != 0) {
                        const bool cw = (rand01(rng) < 0.5f);
                        br.prev_dx = cw ? -pdy : pdy;
                        br.prev_dy = cw ? pdx : -pdx;
                    } else {
                        static constexpr std::array<std::pair<int, int>, 4> card{{ {1,0}, {-1,0}, {0,1}, {0,-1} }};
                        auto [rdx, rdy] = card[size_t(rng() % card.size())];
                        br.prev_dx = rdx;
                        br.prev_dy = rdy;
                    }

                    br.x = x;
                    br.y = y;

                    stack.push_back(br);
                    wlk.branchesLeft--;
                }

                if (!chooseNextStep(height, out.oreMask, w, h, x, y, pdx, pdy, p, rng)) {
                    break;
                }
            }
        }
    }

    if (p.emitInstances) {
        out.instances.reserve(4096);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t v = out.oreMask[idx(x, y, w)];
                if (v == 0) continue;
                out.instances.push_back({ static_cast<ResourceType>(v), x, y });
            }
        }
    }

    return out;
}

} // namespace detail

// Public API: accepts Biome storage as either Biome or uint8_t (or anything static_cast-able to Biome).
template <class BiomeLike>
inline OreVeinResult generateOreVeins(
    const std::vector<float>& height,
    const std::vector<BiomeLike>& biomes,
    int w, int h,
    const OreVeinParams& p = {}
) {
    return detail::generateImpl(height, biomes, w, h, p);
}

// Convenience overload for your WorldData (Types.h defines WorldData + resources) :contentReference[oaicite:4]{index=4}
inline OreVeinResult generateOreVeins(const WorldData& world, const OreVeinParams& p = {}) {
    return generateOreVeins(world.height, world.biome, world.w, world.h, p);
}

// Convenience helper: append per-tile ore instances into world.resources (if you store ResourceInstance tiles).
inline void appendOreVeins(WorldData& world, const OreVeinParams& p = {}) {
    auto veins = generateOreVeins(world, p);
    if (!p.emitInstances) return;
    world.resources.insert(world.resources.end(), veins.instances.begin(), veins.instances.end());
}

} // namespace procgen
