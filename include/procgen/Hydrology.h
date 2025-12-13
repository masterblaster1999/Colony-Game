#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <deque>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace procgen {

namespace detail {

template <class T>
constexpr T Clamp(T v, T lo, T hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// Fast deterministic 32-bit hash (good enough for tie-break jitter).
constexpr uint32_t Hash32(uint32_t x) {
    x += 0x9e3779b9u;
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

inline float U01(uint32_t x) {
    // Convert to [0,1). (24 bits is plenty for small jitter.)
    return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

// Percentile from a COPY of values (so we can nth_element).
inline float PercentileCopy(std::vector<float> v, float p01) {
    if (v.empty()) {
        return 0.0f;
    }
    p01 = Clamp(p01, 0.0f, 1.0f);
    const size_t k = static_cast<size_t>(std::floor(p01 * static_cast<float>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

template <class HeightContainer>
inline float Percentile(const HeightContainer& values, float p01) {
    std::vector<float> copy;
    copy.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        copy.push_back(static_cast<float>(values[i]));
    }
    return PercentileCopy(std::move(copy), p01);
}

} // namespace detail

/// Water classification (fits in 1 byte per tile).
enum class WaterKind : uint8_t {
    None = 0,
    Sea  = 1,
    River = 2,
    Lake = 3,
};

struct HydrologySettings {
    /// Seed used only for tie-breaking on flats (tiny jitter).
    uint32_t seed = 1337u;

    /// Sea level computed as this percentile of the input height distribution.
    /// Example: 0.12 => lowest ~12% of tiles are "sea".
    float seaLevelPercentile = 0.12f;

    /// Fraction of land tiles (height > sea) to classify as "river" based on flow accumulation.
    /// Example: 0.02 => top ~2% highest-accumulation land tiles become rivers.
    float riverCellFraction = 0.02f;

    /// Fraction of *sink* land tiles (local minima) to promote into lakes.
    float lakeSinkFraction = 0.002f;

    /// Radius (in tiles) to expand a lake around a chosen sink.
    int lakeExpandRadius = 2;

    /// Max visual width class for rivers (1..maxWidth). Width is derived from accumulation.
    int maxWidth = 8;

    /// Width mapping exponent.
    /// < 1.0 makes more medium rivers; > 1.0 makes mostly thin rivers with a few thick ones.
    float widthExponent = 0.55f;

    /// Flat tie-break jitter as a fraction of (maxHeight-minHeight).
    /// Tiny value avoids "large flat areas => everywhere is a sink" without visibly changing terrain.
    float flatJitterFraction = 1e-4f;

    /// Multi-source moisture falloff distance (in tiles) used by ComputeMoisture().
    float moistureFalloff = 32.0f;

    /// Whether sea tiles should act as water sources for moisture.
    bool includeSeaInMoisture = true;

    /// Use 8-neighborhood for moisture distance (diagonal steps count as 1). If false, uses 4-neighborhood.
    bool moistureUse8Way = false;
};

struct HydrologyResult {
    int w = 0;
    int h = 0;

    float seaLevel = 0.0f;

    /// D8 downslope direction for each tile:
    ///   -2 = sea tile
    ///   -1 = sink/no outflow
    ///  0..7 = direction index (E, NE, N, NW, W, SW, S, SE)
    std::vector<int8_t> flowDir;

    /// Flow accumulation (arbitrary units) per tile.
    std::vector<float> accumulation;

    /// Water classification per tile.
    std::vector<WaterKind> water;

    /// River width class (0 for non-river, otherwise 1..maxWidth).
    std::vector<uint8_t> riverWidth;

    inline int Idx(int x, int y) const { return y * w + x; }
};

inline int NeighborIndex(int w, int h, int x, int y, int dir) {
    // D8 direction order (clockwise): E, NE, N, NW, W, SW, S, SE
    static constexpr int dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static constexpr int dy[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };
    const int nx = x + dx[dir];
    const int ny = y + dy[dir];
    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
        return -1;
    }
    return ny * w + nx;
}

/// Generate a hydrology solution from a heightfield:
///   1) D8 flow direction (with tiny deterministic jitter to break flats)
///   2) Flow accumulation (topologically, no cycles)
///   3) River classification from accumulation percentile
///   4) Optional lakes from high-accumulation sinks
///
/// Height values can be any scale (not required to be 0..1); sea level and thresholds are percentile-based.
template <class HeightContainer>
inline HydrologyResult GenerateHydrology(int w, int h, const HeightContainer& height, const HydrologySettings& s = {}) {
    HydrologyResult out;
    out.w = w;
    out.h = h;

    if (w <= 0 || h <= 0) {
        return out;
    }
    const size_t N = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (height.size() != N) {
        return out;
    }

    // Compute height range (for jitter amplitude).
    float minH = std::numeric_limits<float>::infinity();
    float maxH = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < N; ++i) {
        const float v = static_cast<float>(height[i]);
        minH = std::min(minH, v);
        maxH = std::max(maxH, v);
    }
    const float rangeH = std::max(0.0f, maxH - minH);
    const float jitterAmp = rangeH * std::max(0.0f, s.flatJitterFraction);

    // Sea level from percentile of raw heights.
    out.seaLevel = detail::Percentile(height, s.seaLevelPercentile);

    out.flowDir.assign(N, static_cast<int8_t>(-1));
    out.accumulation.assign(N, 0.0f);
    out.water.assign(N, WaterKind::None);
    out.riverWidth.assign(N, 0);

    // Helper: jittered effective height for tie-breaking.
    auto effH = [&](size_t idx) -> float {
        const float base = static_cast<float>(height[idx]);
        if (jitterAmp <= 0.0f) {
            return base;
        }
        const uint32_t h32 = detail::Hash32(s.seed ^ static_cast<uint32_t>(idx));
        const float r = detail::U01(h32) * 2.0f - 1.0f; // [-1, 1)
        return base + r * jitterAmp;
    };

    // Compute D8 flow direction.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const float raw = static_cast<float>(height[i]);

            if (raw <= out.seaLevel) {
                out.flowDir[i] = static_cast<int8_t>(-2); // sea
                out.water[i] = WaterKind::Sea;
                continue;
            }

            const float cur = effH(i);

            int bestDir = -1;
            float best = cur;

            for (int d = 0; d < 8; ++d) {
                const int ni = NeighborIndex(w, h, x, y, d);
                if (ni < 0) {
                    continue;
                }
                const size_t nidx = static_cast<size_t>(ni);
                const float nhRaw = static_cast<float>(height[nidx]);

                if (nhRaw <= out.seaLevel) {
                    // Let land flow directly into sea if it's the lowest neighbor.
                    // Tiny bias helps drainage "find the ocean" when heights are very flat.
                    const float nhEff = effH(nidx) - jitterAmp * 0.25f;
                    if (nhEff < best) {
                        best = nhEff;
                        bestDir = d;
                    }
                    continue;
                }

                const float nh = effH(nidx);
                if (nh < best) {
                    best = nh;
                    bestDir = d;
                }
            }

            // Only accept a direction if it strictly decreases effective height.
            if (bestDir >= 0 && best < cur) {
                out.flowDir[i] = static_cast<int8_t>(bestDir);
            } else {
                out.flowDir[i] = static_cast<int8_t>(-1); // sink
            }
        }
    }

    // Build indegree for topological accumulation.
    std::vector<int> indeg(N, 0);

    auto dstIndex = [&](size_t i) -> int {
        const int8_t dir = out.flowDir[i];
        if (dir < 0) {
            return -1;
        }
        const int x = static_cast<int>(i % static_cast<size_t>(w));
        const int y = static_cast<int>(i / static_cast<size_t>(w));
        return NeighborIndex(w, h, x, y, static_cast<int>(dir));
    };

    for (size_t i = 0; i < N; ++i) {
        const int di = dstIndex(i);
        if (di >= 0) {
            indeg[static_cast<size_t>(di)]++;
        }
    }

    // Initialize "rainfall": 1 unit on land, 0 on sea.
    for (size_t i = 0; i < N; ++i) {
        out.accumulation[i] = (out.flowDir[i] == static_cast<int8_t>(-2)) ? 0.0f : 1.0f;
    }

    std::deque<size_t> q;
    for (size_t i = 0; i < N; ++i) {
        if (indeg[i] == 0) {
            q.push_back(i);
        }
    }

    while (!q.empty()) {
        const size_t i = q.front();
        q.pop_front();

        const int di = dstIndex(i);
        if (di >= 0) {
            const size_t d = static_cast<size_t>(di);
            out.accumulation[d] += out.accumulation[i];
            indeg[d]--;
            if (indeg[d] == 0) {
                q.push_back(d);
            }
        }
    }

    // Choose river threshold by top-fraction of land accumulation.
    std::vector<float> landAcc;
    landAcc.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        if (out.flowDir[i] != static_cast<int8_t>(-2)) { // not sea
            landAcc.push_back(out.accumulation[i]);
        }
    }

    float riverThresh = std::numeric_limits<float>::infinity();
    const float frac = detail::Clamp(s.riverCellFraction, 0.0f, 1.0f);
    if (!landAcc.empty() && frac > 0.0f) {
        // Threshold at percentile (1 - frac): keep top frac.
        riverThresh = detail::PercentileCopy(std::move(landAcc), 1.0f - frac);
    }

    // Mark river cells and compute max accumulation among them.
    float maxRiverAcc = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        if (out.flowDir[i] == static_cast<int8_t>(-2)) {
            continue; // sea
        }
        const float raw = static_cast<float>(height[i]);
        if (raw <= out.seaLevel) {
            continue;
        }
        if (out.accumulation[i] >= riverThresh) {
            out.water[i] = WaterKind::River;
            maxRiverAcc = std::max(maxRiverAcc, out.accumulation[i]);
        }
    }

    // Promote some sinks to lakes based on sink accumulation.
    std::vector<float> sinkAcc;
    std::vector<size_t> sinkIdx;

    for (size_t i = 0; i < N; ++i) {
        if (out.flowDir[i] != static_cast<int8_t>(-1)) {
            continue; // not a sink
        }
        const float raw = static_cast<float>(height[i]);
        if (raw <= out.seaLevel) {
            continue; // sea handled already
        }
        sinkIdx.push_back(i);
        sinkAcc.push_back(out.accumulation[i]);
    }

    float lakeThresh = std::numeric_limits<float>::infinity();
    const float lakeFrac = detail::Clamp(s.lakeSinkFraction, 0.0f, 1.0f);
    if (!sinkAcc.empty() && lakeFrac > 0.0f) {
        lakeThresh = detail::PercentileCopy(std::move(sinkAcc), 1.0f - lakeFrac);
    }

    const int r = std::max(0, s.lakeExpandRadius);
    for (size_t k = 0; k < sinkIdx.size(); ++k) {
        const size_t i = sinkIdx[k];
        if (out.accumulation[i] < lakeThresh) {
            continue;
        }

        const int cx = static_cast<int>(i % static_cast<size_t>(w));
        const int cy = static_cast<int>(i / static_cast<size_t>(w));

        // Expand a small disk; keep inside land.
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) {
                    continue;
                }
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    continue;
                }
                const size_t j = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
                if (out.flowDir[j] == static_cast<int8_t>(-2)) {
                    continue; // don't overwrite sea
                }
                const float raw = static_cast<float>(height[j]);
                if (raw <= out.seaLevel) {
                    continue;
                }
                out.water[j] = WaterKind::Lake;
            }
        }
    }

    // Compute river width classes from accumulation.
    if (maxRiverAcc > 0.0f) {
        const int maxW = std::max(1, s.maxWidth);
        const float exp = std::max(0.01f, s.widthExponent);

        for (size_t i = 0; i < N; ++i) {
            if (out.water[i] != WaterKind::River) {
                continue;
            }
            const float t = detail::Clamp(out.accumulation[i] / maxRiverAcc, 0.0f, 1.0f);
            const float w01 = std::pow(t, exp);
            const int wClass = 1 + static_cast<int>(std::floor(w01 * static_cast<float>(maxW - 1) + 0.5f));
            out.riverWidth[i] = static_cast<uint8_t>(detail::Clamp(wClass, 1, maxW));
        }
    }

    return out;
}

/// Multi-source distance-to-water and moisture field.
/// Moisture is 1.0 at water sources and falls off toward 0 as distance increases.
///
/// If `falloff <= 0`, moisture will be a hard 0/1 (water cells => 1, others => 0).
inline std::vector<float> ComputeMoisture(int w, int h,
                                         const std::vector<WaterKind>& water,
                                         float falloff,
                                         bool includeSea = true,
                                         bool use8Way = false) {
    const size_t N = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<int> dist(N, -1);
    std::deque<size_t> q;

    auto isSource = [&](WaterKind k) -> bool {
        if (k == WaterKind::River || k == WaterKind::Lake) return true;
        if (includeSea && k == WaterKind::Sea) return true;
        return false;
    };

    for (size_t i = 0; i < N; ++i) {
        if (isSource(water[i])) {
            dist[i] = 0;
            q.push_back(i);
        }
    }

    static constexpr int dx4[4] = { 1, -1, 0, 0 };
    static constexpr int dy4[4] = { 0, 0, 1, -1 };
    static constexpr int dx8[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static constexpr int dy8[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };

    const int nDirs = use8Way ? 8 : 4;

    while (!q.empty()) {
        const size_t i = q.front();
        q.pop_front();

        const int x = static_cast<int>(i % static_cast<size_t>(w));
        const int y = static_cast<int>(i / static_cast<size_t>(w));

        for (int d = 0; d < nDirs; ++d) {
            const int nx = x + (use8Way ? dx8[d] : dx4[d]);
            const int ny = y + (use8Way ? dy8[d] : dy4[d]);
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                continue;
            }
            const size_t j = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
            if (dist[j] >= 0) {
                continue;
            }
            dist[j] = dist[i] + 1;
            q.push_back(j);
        }
    }

    std::vector<float> moisture(N, 0.0f);
    if (falloff <= 0.0f) {
        for (size_t i = 0; i < N; ++i) {
            moisture[i] = (dist[i] == 0) ? 1.0f : 0.0f;
        }
        return moisture;
    }

    for (size_t i = 0; i < N; ++i) {
        if (dist[i] < 0) {
            moisture[i] = 0.0f;
            continue;
        }
        const float d = static_cast<float>(dist[i]);
        // Smooth exponential falloff.
        moisture[i] = std::exp(-d / falloff);
    }
    return moisture;
}

/// Convenience wrapper: uses settings defaults for moisture parameters.
inline std::vector<float> ComputeMoisture(const HydrologyResult& hydro, const HydrologySettings& s = {}) {
    return ComputeMoisture(hydro.w, hydro.h, hydro.water, s.moistureFalloff, s.includeSeaInMoisture, s.moistureUse8Way);
}

/// Simple terrain carving: lowers height along rivers/lakes to make channels more visible.
/// `carveDepth` should be small relative to your height scale; width affects depth.
///
/// This is optional: you can also just render/paint water on top of height.
inline void CarveChannels(std::vector<float>& height,
                          const HydrologyResult& hydro,
                          float carveDepth,
                          float bankBlend = 0.25f) {
    if (hydro.w <= 0 || hydro.h <= 0) return;
    const size_t N = static_cast<size_t>(hydro.w) * static_cast<size_t>(hydro.h);
    if (height.size() != N || hydro.water.size() != N || hydro.riverWidth.size() != N) return;
    if (carveDepth <= 0.0f) return;

    int maxRiverW = 0;
    for (uint8_t wClass : hydro.riverWidth) {
        maxRiverW = std::max(maxRiverW, static_cast<int>(wClass));
    }
    maxRiverW = std::max(1, maxRiverW);

    // First pass: carve the main channel.
    for (size_t i = 0; i < N; ++i) {
        if (hydro.water[i] == WaterKind::River) {
            const float w01 = static_cast<float>(std::max<int>(1, hydro.riverWidth[i])) / static_cast<float>(maxRiverW);
            height[i] -= carveDepth * w01;
        } else if (hydro.water[i] == WaterKind::Lake) {
            height[i] -= carveDepth * 0.6f;
        }
    }

    // Second pass: soften banks (very small blur to adjacent tiles).
    if (bankBlend <= 0.0f) return;
    bankBlend = detail::Clamp(bankBlend, 0.0f, 1.0f);

    std::vector<float> copy = height;

    static constexpr int dx[4] = { 1, -1, 0, 0 };
    static constexpr int dy[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < hydro.h; ++y) {
        for (int x = 0; x < hydro.w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(hydro.w) + static_cast<size_t>(x);
            if (hydro.water[i] == WaterKind::None) continue;

            float sum = copy[i];
            int count = 1;

            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d];
                const int ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= hydro.w || ny >= hydro.h) continue;
                const size_t j = static_cast<size_t>(ny) * static_cast<size_t>(hydro.w) + static_cast<size_t>(nx);
                sum += copy[j];
                count++;
            }

            const float avg = sum / static_cast<float>(count);
            height[i] = copy[i] * (1.0f - bankBlend) + avg * bankBlend;
        }
    }
}

} // namespace procgen
