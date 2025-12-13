#pragma once
// ============================================================================
// BiomeSmoothing.h
// Post-process helpers for procgen biome maps.
//
// What it does:
//   1) (Optional) Majority/mode filter pass to soften jagged edges.
//   2) Remove tiny connected components ("islands") by merging them into the
//      most common neighboring biome.
//
// Why it's useful:
//   - WorldGen often classifies each tile independently (noise thresholds),
//     which can create speckle. This makes biomes more coherent without
//     changing your core generator.
//
// Header-only: drop into include/procgen and include where needed.
// ============================================================================

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

#include "Biome.h" // procgen::Biome, biome_count(), to_underlying()

namespace procgen {

// Controls how aggressive the post-processing is.
struct BiomeSmoothingParams {
    // Connected components smaller than this (cells) will be merged.
    int minRegionSize = 24;

    // How many relabel+merge passes to run (merging can create new small islands).
    // 0 disables island removal.
    int maxMergePasses = 3;

    // Region connectivity for labeling (true=8-neighbor, false=4-neighbor).
    bool eightConnected = true;

    // Lock specific biomes so they do not get changed when they are the "source"
    // region being merged. (They can still be merge TARGETS; see notes below.)
    bool lockOcean = true;
    bool lockBeach = true;
    bool lockSnow = false;
    bool lockMountain = false;
    bool lockRainforest = false;

    // Optional: run a 3x3 majority/mode filter BEFORE merging islands.
    // This can help reduce jaggedness, but it can also blur narrow bands.
    int majorityIters = 0;

    // When true, majority filter won't modify locked biomes.
    bool majorityRespectsLocks = true;
};

// Metadata for a single connected biome region.
struct BiomeRegion {
    int id = -1;
    Biome biome = Biome::Ocean;
    int size = 0;

    // AABB bounds in grid coordinates (inclusive min, inclusive max).
    int minX = 0, minY = 0, maxX = 0, maxY = 0;

    // Span into BiomeRegionMap::cells (compressed list of cell indices).
    int cellOffset = 0;
    int cellCount = 0;
};

// Region labeling result.
struct BiomeRegionMap {
    int w = 0, h = 0;

    // regionId[i] = region index for cell i (size w*h).
    std::vector<int> regionId;

    // Regions and their metadata.
    std::vector<BiomeRegion> regions;

    // Concatenated cell indices for all regions (size w*h).
    std::vector<int> cells;
};

namespace detail {

inline int idx(int x, int y, int w) { return y * w + x; }

inline bool inb(int x, int y, int w, int h) {
    return (x >= 0) && (y >= 0) && (x < w) && (y < h);
}

inline bool locked(Biome b, const BiomeSmoothingParams& p) {
    if (p.lockOcean && b == Biome::Ocean) return true;
    if (p.lockBeach && b == Biome::Beach) return true;
    if (p.lockSnow && b == Biome::Snow) return true;
    if (p.lockMountain && b == Biome::Mountain) return true;
    if (p.lockRainforest && b == Biome::Rainforest) return true;
    return false;
}

// Packed (dx,dy) pairs: out[2*k]=dx, out[2*k+1]=dy
inline void neighbor_deltas(bool eightConnected, std::array<int, 16>& out, int& count) {
    static constexpr std::array<int, 8> D4 = { -1,0,  1,0,  0,-1,  0,1 };
    static constexpr std::array<int, 16> D8 = { -1,0,  1,0,  0,-1,  0,1,  -1,-1,  1,-1,  -1,1,  1,1 };

    out.fill(0);
    if (eightConnected) {
        out = D8;
        count = 8;
    } else {
        for (int i = 0; i < 8; ++i) out[i] = D4[i];
        count = 4;
    }
}

} // namespace detail

//------------------------------------------------------------------------------
// LabelBiomeRegions
//------------------------------------------------------------------------------
// Computes connected regions of identical biome (4- or 8-connected).
// Returns regionId per cell + region metadata (including cell lists).
inline BiomeRegionMap LabelBiomeRegions(const std::vector<Biome>& biome, int w, int h,
                                        bool eightConnected = true) {
    BiomeRegionMap out;
    out.w = w;
    out.h = h;

    const size_t N = (w > 0 && h > 0) ? (size_t)w * (size_t)h : 0u;
    if (N == 0 || biome.size() != N) return out;

    out.regionId.assign(N, -1);
    out.cells.reserve(N);

    std::array<int, 16> deltas{};
    int nd = 0;
    detail::neighbor_deltas(eightConnected, deltas, nd);

    std::deque<int> q;
    int nextId = 0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int start = detail::idx(x, y, w);
            if (out.regionId[(size_t)start] != -1) continue;

            const Biome b = biome[(size_t)start];

            BiomeRegion R;
            R.id = nextId;
            R.biome = b;
            R.minX = R.maxX = x;
            R.minY = R.maxY = y;
            R.cellOffset = (int)out.cells.size();

            // BFS flood fill
            q.clear();
            q.push_back(start);
            out.regionId[(size_t)start] = nextId;

            while (!q.empty()) {
                const int v = q.front();
                q.pop_front();

                out.cells.push_back(v);
                R.size++;

                const int vx = v % w;
                const int vy = v / w;

                R.minX = std::min(R.minX, vx);
                R.maxX = std::max(R.maxX, vx);
                R.minY = std::min(R.minY, vy);
                R.maxY = std::max(R.maxY, vy);

                for (int k = 0; k < nd; ++k) {
                    const int nx = vx + deltas[2 * k + 0];
                    const int ny = vy + deltas[2 * k + 1];
                    if (!detail::inb(nx, ny, w, h)) continue;

                    const int ni = detail::idx(nx, ny, w);
                    if (out.regionId[(size_t)ni] != -1) continue;
                    if (biome[(size_t)ni] != b) continue;

                    out.regionId[(size_t)ni] = nextId;
                    q.push_back(ni);
                }
            }

            R.cellCount = (int)out.cells.size() - R.cellOffset;
            out.regions.push_back(R);
            nextId++;
        }
    }

    return out;
}

//------------------------------------------------------------------------------
// MajorityFilterBiomes (optional)
//------------------------------------------------------------------------------
// A small 3x3 mode filter to soften edges.
// Notes:
//   - If majorityRespectsLocks=true, locked biomes are not changed.
//   - Also avoids painting locked biomes into unlocked cells unless the cell
//     is already that biome (prevents ocean “creep” when lockOcean is on).
inline void MajorityFilterBiomes(std::vector<Biome>& biome, int w, int h, int iterations,
                                 const BiomeSmoothingParams& params = {}) {
    const size_t N = (w > 0 && h > 0) ? (size_t)w * (size_t)h : 0u;
    if (iterations <= 0 || N == 0 || biome.size() != N) return;

    std::vector<Biome> tmp(N);

    constexpr size_t K = biome_count();

    for (int it = 0; it < iterations; ++it) {
        tmp = biome;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int i = detail::idx(x, y, w);
                const Biome cur = tmp[(size_t)i];

                if (params.majorityRespectsLocks && detail::locked(cur, params)) {
                    biome[(size_t)i] = cur;
                    continue;
                }

                std::array<int, K> counts{};
                counts.fill(0);

                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (!detail::inb(nx, ny, w, h)) continue;
                        const Biome nb = tmp[(size_t)detail::idx(nx, ny, w)];
                        const size_t bi = (size_t)to_underlying(nb);
                        if (bi < K) counts[bi] += 1;
                    }
                }

                // pick mode; ties break toward current biome first, then lowest enum value
                int bestCount = -1;
                Biome bestBiome = cur;

                {
                    const size_t ci = (size_t)to_underlying(cur);
                    bestCount = (ci < K) ? counts[ci] : -1;
                    bestBiome = cur;
                }

                for (size_t bi = 0; bi < K; ++bi) {
                    const int c = counts[bi];
                    if (c > bestCount) {
                        bestCount = c;
                        bestBiome = static_cast<Biome>((std::uint8_t)bi);
                    }
                }

                if (params.majorityRespectsLocks && detail::locked(bestBiome, params)) {
                    // Prevent locked-biome "paint" into non-locked cells.
                    biome[(size_t)i] = cur;
                } else {
                    biome[(size_t)i] = bestBiome;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// RemoveSmallBiomeIslands
//------------------------------------------------------------------------------
// Merges connected regions smaller than `minRegionSize` into their most common
// neighbor biome. Returns number of cells changed.
inline int RemoveSmallBiomeIslands(std::vector<Biome>& biome, int w, int h,
                                  BiomeSmoothingParams params = {}) {
    const size_t N = (w > 0 && h > 0) ? (size_t)w * (size_t)h : 0u;
    if (N == 0 || biome.size() != N) return 0;

    if (params.minRegionSize <= 0 || params.maxMergePasses <= 0) return 0;

    int totalChanged = 0;

    std::array<int, 16> deltas{};
    int nd = 0;
    detail::neighbor_deltas(params.eightConnected, deltas, nd);

    constexpr size_t K = biome_count();

    for (int pass = 0; pass < params.maxMergePasses; ++pass) {
        const BiomeRegionMap R = LabelBiomeRegions(biome, w, h, params.eightConnected);
        if (R.regions.empty()) break;

        bool changedThisPass = false;

        // Apply into a copy to avoid order-dependence.
        std::vector<Biome> next = biome;

        for (const BiomeRegion& reg : R.regions) {
            if (reg.size >= params.minRegionSize) continue;
            if (detail::locked(reg.biome, params)) continue;

            std::array<int, K> borderCounts{};
            borderCounts.fill(0);

            // Count neighboring biome types around this region's boundary
            for (int kCell = 0; kCell < reg.cellCount; ++kCell) {
                const int v = R.cells[(size_t)(reg.cellOffset + kCell)];
                const int vx = v % w;
                const int vy = v / w;

                for (int k = 0; k < nd; ++k) {
                    const int nx = vx + deltas[2 * k + 0];
                    const int ny = vy + deltas[2 * k + 1];
                    if (!detail::inb(nx, ny, w, h)) continue;

                    const int ni = detail::idx(nx, ny, w);
                    if (R.regionId[(size_t)ni] == reg.id) continue;

                    const Biome nb = biome[(size_t)ni];
                    const size_t bi = (size_t)to_underlying(nb);
                    if (bi < K) borderCounts[bi] += 1;
                }
            }

            // Pick the most common neighbor biome (ties -> lowest enum value).
            int bestCount = -1;
            Biome bestBiome = reg.biome;

            for (size_t bi = 0; bi < K; ++bi) {
                const int c = borderCounts[bi];
                if (c > bestCount) {
                    bestCount = c;
                    bestBiome = static_cast<Biome>((std::uint8_t)bi);
                }
            }

            if (bestCount <= 0) continue; // no neighbors?
            if (bestBiome == reg.biome) continue;

            // Apply replacement
            for (int kCell = 0; kCell < reg.cellCount; ++kCell) {
                const int v = R.cells[(size_t)(reg.cellOffset + kCell)];
                next[(size_t)v] = bestBiome;
            }

            totalChanged += reg.size;
            changedThisPass = true;
        }

        if (!changedThisPass) break;
        biome.swap(next);
    }

    return totalChanged;
}

//------------------------------------------------------------------------------
// PostProcessBiomes
//------------------------------------------------------------------------------
// One-stop call: majority filter (optional) then remove small islands.
// Returns number of cells changed by island removal.
inline int PostProcessBiomes(std::vector<Biome>& biome, int w, int h, BiomeSmoothingParams p = {}) {
    if (p.majorityIters > 0) {
        MajorityFilterBiomes(biome, w, h, p.majorityIters, p);
    }
    return RemoveSmallBiomeIslands(biome, w, h, p);
}

} // namespace procgen
