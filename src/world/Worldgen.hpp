#pragma once
// World generation stages interface & utilities (supercharged, header-only, Windows-safe)
//
// Highlights:
// • Windows hygiene: guard against stray MIDL 'small'/'large' and <Windows.h> macro min/max.
// • Stronger noise toolbox: value & Perlin, fBM / ridged / billow, Worley F1(+id) (+ periodic),
//   one-step domain warp. Tileable variants via integer lattice period in X/Y.
// • High-quality random sampling:
//     - AliasTable (Walker/Vose) for O(1) discrete sampling (improved numeric robustness).
//     - PoissonDiskSampler (Bridson) with optional mask/density predicate for 2D blue-noise scatter.
// • Deterministic seeding utilities (splitmix64), per-stage sub-RNGs.
// • DEM analysis:
//     - Horn slope/aspect on regular grids; hillshade utility.
//     - D8 flow with explicit flat handling + accumulation; river cell helper.
// • Filters & remapping:
//     - Separable sliding box blur; 3-pass “almost Gaussian” blur.
//     - Normalize/rescale, clamp/threshold; morphology (dilate/erode/open/close), chamfer distance.
// • GeneratorSettings with common worldgen knobs.
// • Stage registry (topological) with cycle detection + pipeline runner (progress, cancel,
//   per-stage timings, error text).
// • Optional minimal job queue + parallel_for2d (header-only) for parallel chunk builds.
//
// NOTE: Keep heavy stage *implementations* elsewhere to avoid recompiles.
// This header deliberately avoids including <Windows.h> and undefines common macro traps.

#if defined(small)
#  undef small
#endif
#if defined(large)
#  undef large
#endif
#if defined(min)
#  undef min
#endif
#if defined(max)
#  undef max
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Fields.hpp"  // must provide Grid<T> or equivalent
#include "RNG.hpp"     // must provide Pcg32 (Windows build parity)

namespace colony::worldgen {

// =================================================================================================
// Versioning & small math
// =================================================================================================
inline constexpr std::uint32_t kWorldgenHeaderVersion = 5;

inline constexpr float kPi  = 3.14159265358979323846f;
inline constexpr float kTau = 6.28318530717958647692f;
inline constexpr float kInvSqrt2 = 0.70710678118654752440f;

template <class E>
constexpr auto to_underlying(E e) noexcept -> std::underlying_type_t<E> {
    return static_cast<std::underlying_type_t<E>>(e);
}

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    constexpr Vec2(float _x, float _y) : x(_x), y(_y) {}
    constexpr Vec2 operator+(const Vec2& r) const noexcept { return {x + r.x, y + r.y}; }
    constexpr Vec2 operator-(const Vec2& r) const noexcept { return {x - r.x, y - r.y}; }
    constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& r) noexcept { x += r.x; y += r.y; return *this; }
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    Vec3() = default;
    constexpr Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

inline float dot(const Vec2& a, const Vec2& b) noexcept { return a.x*b.x + a.y*b.y; }
inline float length(const Vec2& v) noexcept { return std::sqrt(dot(v,v)); }
inline Vec2  normalize(const Vec2& v) noexcept { float L = length(v); return L>0.f?Vec2{v.x/L,v.y/L}:Vec2{0,0}; }

inline float clamp(float v, float a, float b) noexcept { return v < a ? a : (v > b ? b : v); }
inline float clamp01(float v) noexcept { return clamp(v, 0.f, 1.f); }
inline float saturate(float v) noexcept { return clamp01(v); }
inline float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float inv_lerp(float a, float b, float v) noexcept { return (v - a) / (b - a); }
inline float remap(float inMin, float inMax, float outMin, float outMax, float v) noexcept {
    float t = clamp01(inv_lerp(inMin, inMax, v)); return lerp(outMin, outMax, t);
}
inline float smoothstep(float a, float b, float x) noexcept {
    float t = clamp01((x - a) / (b - a)); return t*t*(3.f - 2.f*t);
}
inline float smootherstep(float a, float b, float x) noexcept {
    float t = clamp01((x - a) / (b - a)); return t*t*t*(t*(t*6.f - 15.f) + 10.f);
}
inline float bias(float b, float x) noexcept { return std::pow(x, std::log2(b)); }        // b in (0,1)
inline float gain(float g, float x) noexcept { return (x < 0.5f) ? 0.5f*bias(1.f - g, 2.f*x)
                                                                 : 1.f - 0.5f*bias(1.f - g, 2.f - 2.f*x); }

// =================================================================================================
// Coordinates, hashing, deterministic mixing
// =================================================================================================
struct ChunkCoord { std::int32_t x = 0, y = 0; };
inline bool operator==(const ChunkCoord& a, const ChunkCoord& b) { return a.x==b.x && a.y==b.y; }
inline bool operator!=(const ChunkCoord& a, const ChunkCoord& b) { return !(a==b); }

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& c) const noexcept {
        auto hx = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x));
        auto hy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.y));
        // 64-bit mix then fold
        hx ^= hy + 0x9e3779b97f4a7c15ULL + (hx<<6) + (hx>>2);
        return static_cast<std::size_t>(hx);
    }
};

namespace detail {
// splitmix64 seed mixer (public-domain style)
inline std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline std::uint64_t hash_combine64(std::uint64_t a, std::uint64_t b) noexcept {
    return splitmix64(a ^ splitmix64(b + 0x9e3779b97f4a7c15ULL));
}
inline std::uint64_t hash_u32(std::uint32_t v) noexcept { return splitmix64(v); }
inline std::uint64_t hash_str(std::string_view s) noexcept {
    // FNV-1a then splitmix64 to decorrelate size & low bits
    std::uint64_t h = 14695981039346656037ull; // 64-bit FNV offset basis
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return splitmix64(h);
}
inline int wrapi(int i, int period) noexcept {
    if (period <= 0) return i;
    int r = i % period; return r < 0 ? r + period : r;
}
} // namespace detail

// =================================================================================================
// World objects & tagging
// =================================================================================================
enum ObjectTag : std::uint32_t {
    ObjTag_None       = 0,
    ObjTag_Vegetation = 1u << 0,
    ObjTag_Rock       = 1u << 1,
    ObjTag_Tree       = 1u << 2,
    ObjTag_Structure  = 1u << 3,
    ObjTag_Loot       = 1u << 4,
    ObjTag_Custom0    = 1u << 5,
    ObjTag_Custom1    = 1u << 6
};

struct ObjectInstance {
    float wx = 0.f, wy = 0.f; // world-space (chunk-local) position (x = Easting, y = Northing)
    std::uint32_t kind = 0;   // e.g., vegetation/rock type id
    float scale = 1.f, rot = 0.f;
    std::uint32_t tags = ObjTag_None; // bitmask of ObjectTag
    float heightOffset = 0.f;         // additive y (for placement on surfaces)
    float tint = 1.f;                 // grayscale tint multiplier (0..1)
    std::uint32_t seed = 0;           // per-instance deterministic seed
};

// =================================================================================================
// Stage ids & names
// =================================================================================================
enum class StageId : std::uint32_t {
    BaseElevation = 1,
    Climate       = 2,
    Hydrology     = 3,
    Biome         = 4,
    Scatter       = 5,
    // optional/future:
    Erosion       = 6,
    Roads         = 7,
    Settlements   = 8
};

inline constexpr const char* stage_name(StageId id) noexcept {
    switch (id) {
        case StageId::BaseElevation: return "BaseElevation";
        case StageId::Climate:       return "Climate";
        case StageId::Hydrology:     return "Hydrology";
        case StageId::Biome:         return "Biome";
        case StageId::Scatter:       return "Scatter";
        case StageId::Erosion:       return "Erosion";
        case StageId::Roads:         return "Roads";
        case StageId::Settlements:   return "Settlements";
        default:                     return "Unknown";
    }
}

// =================================================================================================
// Chunk payload
// =================================================================================================
struct WorldChunk {
    ChunkCoord coord{};
    // Core fields (size set by settings)
    Grid<float>        height;      // meters
    Grid<float>        temperature; // Celsius
    Grid<float>        moisture;    // 0..1
    Grid<float>        flow;        // river flow accumulation (cells)
    Grid<std::uint8_t> biome;       // biome id
    std::vector<ObjectInstance> objects;
};

// =================================================================================================
// Generator settings (enriched)
// =================================================================================================
struct GeneratorSettings {
    // Seeding
    std::uint64_t worldSeed = 0xC01DCAFEULL;

    // Spatial resolution
    int   cellsPerChunk   = 128;     // resolution (NxN)
    float cellSizeMeters  = 1.0f;    // world meters per cell

    // Feature toggles
    bool enableHydrology  = true;
    bool enableScatter    = true;
    bool enableErosion    = false;   // optional erosion stage

    // Base terrain knobs
    float baseElevationScale = 1.0f; // amplitude multiplier
    float baseElevationFreq  = 1.0f; // inverse feature size (1 = baseline)
    float seaLevel           = 0.0f; // meters

    // Climate knobs
    float temperatureLapseRate = -0.0065f; // degC per meter
    float baseMoistureBias     = 0.0f;     // add to normalized moisture

    // Hydrology knobs
    float riverFlowThreshold   = 40.0f;    // cells of contributing area to be a river
    float evaporationRate      = 0.005f;   // per step (0..1)

    // Scatter knobs
    int   maxScatterPerCell    = 1;        // cap to avoid spam
    float scatterDensity       = 0.02f;    // expected per cell (before capping)

    // Execution
    int   threadBudget         = 0;        // 0 = auto (hardware_concurrency)
};

// =================================================================================================
// Context (coords, seeds, sub-RNGs)
// =================================================================================================
struct StageContext {
    const GeneratorSettings& settings;
    const ChunkCoord         chunk;
    Pcg32&                   rng;      // RNG for this stage/chunk (provided by caller)
    WorldChunk&              out;      // read/write access to fields

    // ---- coordinate helpers ----
    int   cells()      const noexcept { return settings.cellsPerChunk; }
    float cellSize()   const noexcept { return settings.cellSizeMeters; }
    Vec2  chunk_origin_world() const noexcept {
        const float span = cellSize() * static_cast<float>(cells());
        return { static_cast<float>(chunk.x) * span,
                 static_cast<float>(chunk.y) * span };
    }
    Vec2  cell_origin_world(int cx, int cy) const noexcept {
        Vec2 org = chunk_origin_world();
        return { org.x + static_cast<float>(cx) * cellSize(),
                 org.y + static_cast<float>(cy) * cellSize() };
    }
    Vec2  cell_center_world(int cx, int cy) const noexcept {
        Vec2 o = cell_origin_world(cx, cy);
        const float h = 0.5f * cellSize();
        return { o.x + h, o.y + h };
    }

    // ---- deterministic seeds ----
    std::uint64_t chunk_seed() const noexcept {
        using detail::hash_combine64; using detail::hash_u32;
        const auto s0 = hash_combine64(hash_u32(static_cast<std::uint32_t>(chunk.x)),
                                       hash_u32(static_cast<std::uint32_t>(chunk.y)));
        return hash_combine64(static_cast<std::uint64_t>(settings.worldSeed), s0);
    }
    std::uint64_t stage_seed(StageId id) const noexcept {
        using detail::hash_combine64;
        return hash_combine64(chunk_seed(), static_cast<std::uint64_t>(to_underlying(id)));
    }
    std::uint64_t sub_seed(StageId id, std::string_view tag) const noexcept {
        using detail::hash_combine64; using detail::hash_str;
        return hash_combine64(stage_seed(id), hash_str(tag));
    }
    // Fresh Pcg32 derived from a tag
    Pcg32 sub_rng(StageId id, std::string_view tag) const noexcept {
        const auto s = sub_seed(id, tag);
        return Pcg32{ static_cast<std::uint32_t>(s), static_cast<std::uint32_t>(s >> 32) };
    }
};

// =================================================================================================
// Noise & random sampling helpers
// =================================================================================================
namespace noise {

// 2D integer hash -> [0,1)
inline float hash01(int x, int y, std::uint32_t seed) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x27d4eb2dU
                    ^ static_cast<std::uint32_t>(y) * 0x85ebca6bU
                    ^ seed * 0x9e3779b9U;
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return (h & 0x00FFFFFFu) / float(0x01000000u);
}

inline float smooth(float t) noexcept { return t*t*(3.f - 2.f*t); }
inline float fade(float t) noexcept { return t*t*t*(t*(t*6.f - 15.f) + 10.f); } // Perlin fade

// Value noise 2D (optionally tileable with integer period in lattice cells)
inline float value2D(float fx, float fy, std::uint32_t seed, int periodX=0, int periodY=0) noexcept {
    const int x0i = (int)std::floor(fx), y0i = (int)std::floor(fy);
    const int x1i = x0i + 1,             y1i = y0i + 1;
    const int x0 = detail::wrapi(x0i, periodX), x1 = detail::wrapi(x1i, periodX);
    const int y0 = detail::wrapi(y0i, periodY), y1 = detail::wrapi(y1i, periodY);
    float tx = smooth(fx - (float)x0i), ty = smooth(fy - (float)y0i);
    float v00 = hash01(x0,y0,seed);
    float v10 = hash01(x1,y0,seed);
    float v01 = hash01(x0,y1,seed);
    float v11 = hash01(x1,y1,seed);
    float a = v00 + (v10 - v00)*tx;
    float b = v01 + (v11 - v01)*tx;
    return a + (b - a)*ty; // [0,1]
}

// Improved Perlin-style gradient noise (2D, hash-based gradients), tileable via period.
inline Vec2 grad_from_hash(std::uint32_t h) noexcept {
    // 8 directions on the unit circle
    switch (h & 7u) {
        default: case 0: return { 1, 0};
        case 1: return {-1, 0};
        case 2: return { 0, 1};
        case 3: return { 0,-1};
        case 4: return { kInvSqrt2, kInvSqrt2};
        case 5: return {-kInvSqrt2, kInvSqrt2};
        case 6: return { kInvSqrt2,-kInvSqrt2};
        case 7: return {-kInvSqrt2,-kInvSqrt2};
    }
}
inline float perlin2D(float fx, float fy, std::uint32_t seed, int periodX=0, int periodY=0) noexcept {
    const int x0i = (int)std::floor(fx), y0i = (int)std::floor(fy);
    const int x1i = x0i + 1,             y1i = y0i + 1;
    const int x0 = detail::wrapi(x0i, periodX), x1 = detail::wrapi(x1i, periodX);
    const int y0 = detail::wrapi(y0i, periodY), y1 = detail::wrapi(y1i, periodY);

    float dx = fx - (float)x0i, dy = fy - (float)y0i;
    float u = fade(dx), v = fade(dy);

    auto h00 = grad_from_hash((std::uint32_t)detail::splitmix64(detail::hash_combine64(seed, (std::uint64_t)((x0<<16) ^ y0))));
    auto h10 = grad_from_hash((std::uint32_t)detail::splitmix64(detail::hash_combine64(seed, (std::uint64_t)((x1<<16) ^ y0))));
    auto h01 = grad_from_hash((std::uint32_t)detail::splitmix64(detail::hash_combine64(seed, (std::uint64_t)((x0<<16) ^ y1))));
    auto h11 = grad_from_hash((std::uint32_t)detail::splitmix64(detail::hash_combine64(seed, (std::uint64_t)((x1<<16) ^ y1))));
    float n00 = dot(h00, {dx,    dy    });
    float n10 = dot(h10, {dx-1.f,dy    });
    float n01 = dot(h01, {dx,    dy-1.f});
    float n11 = dot(h11, {dx-1.f,dy-1.f});
    float nx0 = n00 + (n10 - n00)*u;
    float nx1 = n01 + (n11 - n01)*u;
    return nx0 + (nx1 - nx0)*v; // ~[-1,1]
}

// Fractals built from any scalar noise function
inline float fbm2D(float fx, float fy, std::uint32_t seed,
                   int octaves=5, float lac=2.0f, float gain=0.5f,
                   float (*basis)(float,float,std::uint32_t,int,int)=perlin2D,
                   int periodX=0, int periodY=0) noexcept {
    float amp = 0.5f, sum = 0.f, norm = 0.f;
    for (int i=0;i<octaves;i++) {
        sum  += amp * basis(fx, fy, seed + (std::uint32_t)i*131u, periodX, periodY);
        norm += amp;
        fx *= lac; fy *= lac; amp *= gain;
        if (periodX) periodX *= 2;
        if (periodY) periodY *= 2;
    }
    return norm>0.f ? sum/norm : 0.f;
}
inline float billow2D(float fx, float fy, std::uint32_t seed,
                      int octaves=5, float lac=2.0f, float gain_ = 0.5f,
                      float (*basis)(float,float,std::uint32_t,int,int)=perlin2D,
                      int periodX=0, int periodY=0) noexcept {
    float amp = 0.5f, sum = 0.f, norm = 0.f;
    for (int i=0;i<octaves;i++) {
        float n = 2.f * std::abs(basis(fx, fy, seed + (std::uint32_t)i*733u, periodX, periodY)) - 1.f;
        sum  += amp * n;
        norm += amp;
        fx *= lac; fy *= lac; amp *= gain_;
        if (periodX) periodX *= 2;
        if (periodY) periodY *= 2;
    }
    return norm>0.f ? sum/norm : 0.f;
}
inline float ridged2D(float fx, float fy, std::uint32_t seed,
                      int octaves=5, float lac=2.0f, float gain_ = 0.5f,
                      float (*basis)(float,float,std::uint32_t,int,int)=perlin2D,
                      int periodX=0, int periodY=0) noexcept {
    float amp = 0.5f, sum = 0.f, norm = 0.f;
    for (int i=0;i<octaves;i++) {
        float n = 1.f - std::abs(basis(fx, fy, seed + (std::uint32_t)i*977u, periodX, periodY));
        n *= n; // sharpen ridges
        sum  += amp * n;
        norm += amp;
        fx *= lac; fy *= lac; amp *= gain_;
        if (periodX) periodX *= 2;
        if (periodY) periodY *= 2;
    }
    return norm>0.f ? sum/norm : 0.f;
}

// Domain warp (one step)
inline Vec2 warp2D(Vec2 p, std::uint32_t seed, float amp=0.5f, float freq=1.0f, int periodX=0, int periodY=0) noexcept {
    float dx = fbm2D(p.x*freq, p.y*freq, seed ^ 0x243F6A88u, 4, 2.0f, 0.5f, perlin2D, periodX, periodY);
    float dy = fbm2D(p.x*freq, p.y*freq, seed ^ 0x85A308D3u, 4, 2.0f, 0.5f, perlin2D, periodX, periodY);
    return { p.x + (dx)*amp, p.y + (dy)*amp };
}

// Worley (cellular) noise: returns F1 distance (Euclidean) and a hashed cell id
struct WorleyF1 { float f1=0.f; std::uint32_t id=0; };
inline WorleyF1 worleyF1(float fx, float fy, std::uint32_t seed) noexcept {
    int xi = (int)std::floor(fx), yi = (int)std::floor(fy);
    float best = 1e30f; std::uint32_t bestId = 0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        int cx = xi + dx, cy = yi + dy;
        float jx = hash01(cx,cy,seed ^ 0xA53u);
        float jy = hash01(cx,cy,seed ^ 0x5A3u);
        float px = (float)cx + jx;
        float py = (float)cy + jy;
        float d2 = (fx-px)*(fx-px) + (fy-py)*(fy-py);
        if (d2 < best) { best = d2; bestId = (std::uint32_t)(cx*73856093 ^ cy*19349663) ^ seed; }
    }
    return { std::sqrt(best), bestId };
}

// Periodic Worley (tileable on integer cell periods)
inline WorleyF1 worleyF1_periodic(float fx, float fy, std::uint32_t seed, int periodX, int periodY) noexcept {
    int xi = (int)std::floor(fx), yi = (int)std::floor(fy);
    float best = 1e30f; std::uint32_t bestId = 0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        int cx = detail::wrapi(xi + dx, periodX);
        int cy = detail::wrapi(yi + dy, periodY);
        float jx = hash01(cx,cy,seed ^ 0xA53u);
        float jy = hash01(cx,cy,seed ^ 0x5A3u);
        float px = (float)cx + jx;
        float py = (float)cy + jy;
        float d2 = (fx-px)*(fx-px) + (fy-py)*(fy-py);
        if (d2 < best) { best = d2; bestId = (std::uint32_t)(cx*73856093 ^ cy*19349663) ^ seed; }
    }
    return { std::sqrt(best), bestId };
}

} // namespace noise

// =================================================================================================
// Alias table for O(1) discrete sampling (Walker 1974; Vose 1991)
// =================================================================================================
class AliasTable {
public:
    AliasTable() = default;
    explicit AliasTable(const std::vector<float>& weights) { build(weights); }

    void build(const std::vector<float>& w) {
        const size_t n = w.size();
        prob_.assign(n, 0.0f); alias_.assign(n, 0u);
        if (n == 0) return;

        // Normalize to mean 1.0; treat non-positive weights as zero.
        long double sum = 0.0L;
        for (float v : w) if (v > 0.f && std::isfinite(v)) sum += (long double)v;
        std::vector<long double> scaled(n, 0.0L);
        if (sum > 0.0L) for (size_t i=0;i<n;i++) scaled[i] = (w[i] > 0.f ? (w[i] * (long double)n / sum) : 0.0L);

        std::vector<size_t> small; small.reserve(n);
        std::vector<size_t> large; large.reserve(n);
        for (size_t i=0;i<n;i++) ((scaled[i] < 1.0L) ? small : large).push_back(i);

        while (!small.empty() && !large.empty()) {
            const size_t s = small.back(); small.pop_back();
            const size_t l = large.back(); large.pop_back();
            prob_[s]  = (float)scaled[s];         // threshold in [0,1)
            alias_[s] = (std::uint32_t)l;         // aliased index
            scaled[l] = (scaled[l] + scaled[s]) - 1.0L;
            ((scaled[l] < 1.0L) ? small : large).push_back(l);
        }
        for (size_t i : large) prob_[i] = 1.0f;
        for (size_t i : small) prob_[i] = 1.0f;
    }

    template<class URNG>
    std::uint32_t sample(URNG& rng) const {
        if (prob_.empty()) return 0;
        const std::uint32_t n = (std::uint32_t)prob_.size();
        const std::uint32_t i = rng.next_u32() % n;
        // use full 32 bits of randomness for the comparison to reduce bias
        const float r = (float)((rng.next_u32()) * (1.0/4294967296.0)); // [0,1)
        return (r < prob_[i]) ? i : alias_[i];
    }

    size_t size() const noexcept { return prob_.size(); }

private:
    std::vector<float> prob_;
    std::vector<std::uint32_t> alias_;
};

// =================================================================================================
// Poisson-disk sampler (Bridson 2007) with optional mask/density predicate
// =================================================================================================
struct PoissonDiskSampler {
    // maskOrDensity: optional predicate returning [0..1] "accept prob" at world position
    //   signature: float(Vec2 p) — if null, uniform acceptance.
    static std::vector<Vec2> generate(float radius, Vec2 minP, Vec2 maxP,
                                      Pcg32& rng, int k=30,
                                      std::function<float(Vec2)> maskOrDensity = {}) {
        std::vector<Vec2> out;
        if (!(radius > 0.f) || !(maxP.x > minP.x) || !(maxP.y > minP.y)) return out;

        const float cell = radius / std::sqrt(2.f);
        int gw = std::max(1, (int)std::ceil((maxP.x - minP.x) / cell));
        int gh = std::max(1, (int)std::ceil((maxP.y - minP.y) / cell));
        std::vector<int> grid((size_t)gw * gh, -1);

        auto to_grid = [&](const Vec2& p) {
            int gx = (int)((p.x - minP.x)/cell);
            int gy = (int)((p.y - minP.y)/cell);
            gx = std::clamp(gx, 0, gw-1);
            gy = std::clamp(gy, 0, gh-1);
            return std::pair<int,int>{gx,gy};
        };
        auto fits = [&](const Vec2& p)->bool{
            auto [gx,gy] = to_grid(p);
            const int x0 = std::max(0, gx-2), x1 = std::min(gw-1, gx+2);
            const int y0 = std::max(0, gy-2), y1 = std::min(gh-1, gy+2);
            for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
                int idx = grid[(size_t)y*gw + x];
                if (idx>=0 && length(p - out[(size_t)idx]) < radius) return false;
            }
            return true;
        };
        auto rand01 = [&](Pcg32& r)->float { return (float)((r.next_u32()) * (1.0/4294967296.0)); };
        auto rand_uniform = [&](float a, float b)->float { return a + (b-a)*rand01(rng); };
        auto accept_at    = [&](Vec2 p)->bool {
            if (!maskOrDensity) return true;
            const float pr = std::clamp(maskOrDensity(p), 0.f, 1.f);
            return rand01(rng) <= pr;
        };

        // initial point: rejection-sample until accepted (guard with attempts)
        const int maxInit = 128;
        int tries = 0;
        Vec2 p0{};
        do {
            p0 = { rand_uniform(minP.x, maxP.x), rand_uniform(minP.y, maxP.y) };
        } while (!accept_at(p0) && (++tries < maxInit));
        if (tries >= maxInit && !accept_at(p0)) return out;

        out.push_back(p0);
        auto [g0x,g0y] = to_grid(p0);
        grid[(size_t)g0y*gw + g0x] = 0;

        std::vector<int> active; active.push_back(0);
        while (!active.empty()) {
            int ai = (int)(rng.next_u32() % active.size());
            int idx = active[(size_t)ai];
            Vec2 base = out[(size_t)idx];
            bool found = false;
            for (int t=0; t<k; ++t) {
                float ang = rand_uniform(0.f, kTau);
                float rad = radius * (1.f + rand01(rng));
                Vec2 cand = base + Vec2{ std::cos(ang), std::sin(ang) } * rad;
                if (cand.x < minP.x || cand.x >= maxP.x || cand.y < minP.y || cand.y >= maxP.y) continue;
                if (!accept_at(cand) || !fits(cand)) continue;
                int newIdx = (int)out.size();
                out.push_back(cand);
                auto [gx,gy] = to_grid(cand);
                grid[(size_t)gy*gw + gx] = newIdx;
                active.push_back(newIdx);
                found = true;
            }
            if (!found) { active[(size_t)ai] = active.back(); active.pop_back(); }
        }
        return out;
    }
};

// =================================================================================================
// Filters & grid utilities (pointer-based, row-major width*height)
// =================================================================================================
namespace filters {

// Sliding-window box blur (radius r) – separable
inline void box_blur_h(float* dst, const float* src, int w, int h, int r) {
    if (r <= 0) { std::copy(src, src + (size_t)w*h, dst); return; }
    const float inv = 1.f / float(2*r + 1);
    for (int y=0; y<h; ++y) {
        const int row = y*w;
        float acc = 0.f;
        // prime with clamped left edge
        for (int i=-r; i<=r; ++i) acc += src[row + std::clamp(i,0,w-1)];
        dst[row + 0] = acc * inv;
        for (int x=1; x<w; ++x) {
            const int xl = std::clamp(x-r-1, 0, w-1);
            const int xr = std::clamp(x+r,   0, w-1);
            acc += src[row + xr] - src[row + xl];
            dst[row + x] = acc * inv;
        }
    }
}
inline void box_blur_v(float* dst, const float* src, int w, int h, int r) {
    if (r <= 0) { std::copy(src, src + (size_t)w*h, dst); return; }
    const float inv = 1.f / float(2*r + 1);
    for (int x=0; x<w; ++x) {
        float acc = 0.f;
        // prime with clamped top edge
        for (int i=-r; i<=r; ++i) acc += src[std::clamp(i,0,h-1)*w + x];
        dst[0*w + x] = acc * inv;
        for (int y=1; y<h; ++y) {
            const int yu = std::clamp(y-r-1, 0, h-1);
            const int yd = std::clamp(y+r,   0, h-1);
            acc += src[yd*w + x] - src[yu*w + x];
            dst[y*w + x] = acc * inv;
        }
    }
}
// three-pass “almost Gaussian” (sigma in pixels); tmp and buf must be w*h
inline std::array<int,3> radii_for_sigma(float sigma) {
    // Three equal boxes approximate Gaussian; radius roughly sigma*sqrt(3)
    int r = std::max(1, (int)std::floor(sigma * 1.7320508f));
    return {r,r,r};
}
inline void gaussian_approx3(float* tmp, float* buf, float* data, int w, int h, float sigma) {
    auto radii = radii_for_sigma(sigma);
    for (int k=0;k<3;k++) {
        box_blur_h(tmp, data, w, h, radii[k]);
        box_blur_v(buf, tmp,  w, h, radii[k]);
        std::copy(buf, buf + (size_t)w*h, data);
    }
}

// Utility transforms
inline void normalize01(float* data, int w, int h) {
    const size_t n = (size_t)w*h;
    if (n==0) return;
    float mn = std::numeric_limits<float>::infinity();
    float mx = -mn;
    for (size_t i=0;i<n;i++){ mn = std::min(mn, data[i]); mx = std::max(mx, data[i]); }
    if (mx <= mn) { std::fill(data, data+n, 0.f); return; }
    const float inv = 1.f/(mx-mn);
    for (size_t i=0;i<n;i++) data[i] = (data[i]-mn)*inv;
}
inline void rescale(float* data, int w, int h, float a, float b) {
    const size_t n = (size_t)w*h;
    for (size_t i=0;i<n;i++) data[i] = a + (b-a)*data[i];
}
inline void threshold(float* data, int w, int h, float t, float lo=0.f, float hi=1.f) {
    const size_t n = (size_t)w*h;
    for (size_t i=0;i<n;i++) data[i] = (data[i] >= t) ? hi : lo;
}

// Morphological ops (binary in/out, values treated as 0/1)
inline void dilate(uint8_t* dst, const uint8_t* src, int w, int h, bool use8=true) {
    static const int dx8[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy8[8]={0,1,1, 1, 0,-1,-1,-1};
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        uint8_t v = src[(size_t)y*w+x];
        if (v) { dst[(size_t)y*w+x]=1; continue; }
        bool any=false;
        for (int k=0;k<(use8?8:4);k++){
            int xn=x+(use8?dx8[k]:dx8[k*2]); int yn=y+(use8?dy8[k]:dy8[k*2]);
            if (xn<0||xn>=w||yn<0||yn>=h) continue;
            if (src[(size_t)yn*w+xn]) { any=true; break; }
        }
        dst[(size_t)y*w+x] = any?1:0;
    }
}
inline void erode(uint8_t* dst, const uint8_t* src, int w, int h, bool use8=true) {
    static const int dx8[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy8[8]={0,1,1, 1, 0,-1,-1,-1};
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        uint8_t v = src[(size_t)y*w+x];
        if (!v) { dst[(size_t)y*w+x]=0; continue; }
        bool all=true;
        for (int k=0;k<(use8?8:4);k++){
            int xn=x+(use8?dx8[k]:dx8[k*2]); int yn=y+(use8?dy8[k]:dy8[k*2]);
            if (xn<0||xn>=w||yn<0||yn>=h) continue;
            if (!src[(size_t)yn*w+xn]) { all=false; break; }
        }
        dst[(size_t)y*w+x] = all?1:0;
    }
}

// Composite morphology
inline void open(uint8_t* tmp, uint8_t* dst, const uint8_t* src, int w, int h, bool use8=true) {
    erode(tmp, src, w, h, use8);
    dilate(dst, tmp, w, h, use8);
}
inline void close(uint8_t* tmp, uint8_t* dst, const uint8_t* src, int w, int h, bool use8=true) {
    dilate(tmp, src, w, h, use8);
    erode(dst, tmp, w, h, use8);
}

// Chamfer distance transform (approximate Euclidean), 4 or 8 connected
inline void distance_field(float* dst, const uint8_t* mask, int w, int h, bool use8=true) {
    const float INF = 1e9f;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) dst[(size_t)y*w+x] = mask[(size_t)y*w+x]?0.f:INF;
    // forward pass
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        float d = dst[(size_t)y*w+x];
        if (x>0)   d = std::min(d, dst[(size_t)y*w+(x-1)] + 1.f);
        if (y>0)   d = std::min(d, dst[(size_t)(y-1)*w+x] + 1.f);
        if (use8) {
            if (x>0 && y>0) d = std::min(d, dst[(size_t)(y-1)*w+(x-1)] + 1.41421356f);
            if (x+1<w && y>0) d = std::min(d, dst[(size_t)(y-1)*w+(x+1)] + 1.41421356f);
        }
        dst[(size_t)y*w+x]=d;
    }
    // backward pass
    for (int y=h-1;y>=0;y--) for (int x=w-1;x>=0;x--) {
        float d = dst[(size_t)y*w+x];
        if (x+1<w) d = std::min(d, dst[(size_t)y*w+(x+1)] + 1.f);
        if (y+1<h) d = std::min(d, dst[(size_t)(y+1)*w+x] + 1.f);
        if (use8) {
            if (x+1<w && y+1<h) d = std::min(d, dst[(size_t)(y+1)*w+(x+1)] + 1.41421356f);
            if (x>0   && y+1<h) d = std::min(d, dst[(size_t)(y+1)*w+(x-1)] + 1.41421356f);
        }
        dst[(size_t)y*w+x]=d;
    }
}

// Bilinear sampler helpers (pointer-based)
inline float sample_bilinear(const float* data, int w, int h, float fx, float fy) {
    fx = clamp(fx, 0.0f, (float)(w-1)); fy = clamp(fy, 0.0f, (float)(h-1));
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0+1, w-1), y1 = std::min(y0+1, h-1);
    float tx = fx - x0, ty = fy - y0;
    float a = data[(size_t)y0*w + x0]*(1-tx) + data[(size_t)y0*w + x1]*tx;
    float b = data[(size_t)y1*w + x0]*(1-tx) + data[(size_t)y1*w + x1]*tx;
    return a*(1-ty) + b*ty;
}

} // namespace filters

// =================================================================================================
// Terrain analysis, flow, hydrology
// =================================================================================================
namespace dem {

// Horn (1981) slope/aspect on a regular grid with spacing 'dx' (meters)
inline void slope_aspect(const float* z, int w, int h, float dx,
                         std::vector<float>& outSlopeDeg, std::vector<float>& outAspectRad) {
    outSlopeDeg.assign((size_t)w*h, 0.f);
    outAspectRad.assign((size_t)w*h, 0.f);
    auto at = [&](int x,int y)->float {
        x = std::clamp(x,0,w-1); y = std::clamp(y,0,h-1);
        return z[(size_t)y*w + x];
    };
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        float z1 = at(x-1,y-1), z2 = at(x,y-1), z3 = at(x+1,y-1);
        float z4 = at(x-1,y  ),             z6 = at(x+1,y  );
        float z7 = at(x-1,y+1), z8 = at(x,y+1), z9 = at(x+1,y+1);
        float dzdx = ( (z3 + 2*z6 + z9) - (z1 + 2*z4 + z7) ) / (8.f*dx);
        float dzdy = ( (z7 + 2*z8 + z9) - (z1 + 2*z2 + z3) ) / (8.f*dx);
        float s = std::atan(std::sqrt(dzdx*dzdx + dzdy*dzdy)); // slope (rad)
        float a = std::atan2(dzdx, dzdy); // aspect (rad): CCW from +Y
        outSlopeDeg[(size_t)y*w + x] = s * (180.f / kPi);
        outAspectRad[(size_t)y*w + x] = a;
    }
}

// Simple hillshade (azimuth degrees from +Y clockwise, altitude degrees from horizon)
inline void hillshade(const float* z, int w, int h, float dx,
                      float azimuthDeg, float altitudeDeg,
                      std::vector<float>& outShade) {
    std::vector<float> slopeDeg, aspectRad;
    slope_aspect(z, w, h, dx, slopeDeg, aspectRad);
    outShade.assign((size_t)w*h, 0.f);
    const float az = azimuthDeg * (kPi/180.f);
    const float alt = altitudeDeg * (kPi/180.f);
    for (int i=0;i<w*h;i++) {
        float s = slopeDeg[(size_t)i] * (kPi/180.f);
        float a = aspectRad[(size_t)i];
        float hs = std::cos(alt) * std::cos(s) + std::sin(alt) * std::sin(s) * std::cos(az - a);
        outShade[(size_t)i] = clamp(hs, 0.f, 1.f);
    }
}

// D8 flow direction (one receiver) with flat handling + accumulation.
// Returns (flow accumulation in "cell count", dirIndex [0..7 or -1], in-degree per cell).
struct FlowField { std::vector<float> accum; std::vector<int8_t> dir; std::vector<uint16_t> indeg; };

inline FlowField d8_flow_accum(const float* height, int w, int h, float flatJitter = 1e-6f) {
    auto idx = [w](int x,int y){ return (size_t)y*w + x; };
    auto inb = [&](int x,int y){ return x>=0 && x<w && y>=0 && y<h; };
    static const int dx[8] = {1,1,0,-1,-1,-1,0,1};
    static const int dy[8] = {0,1,1, 1, 0,-1,-1,-1};
    static const float dist[8] = {1, 1.41421356f,1,1.41421356f,1,1.41421356f,1,1.41421356f};

    std::vector<int8_t> dir((size_t)w*h, -1);
    std::vector<uint16_t> indeg((size_t)w*h, 0);
    std::vector<float> accum((size_t)w*h, 1.f); // each cell contributes itself

    // choose receiver per cell (steepest downslope), break flats with tiny jitter
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        float z0 = height[idx(x,y)];
        float bestSlope = -std::numeric_limits<float>::infinity();
        int bestK = -1;
        for (int k=0;k<8;k++) {
            int xn = x + dx[k], yn = y + dy[k];
            if (!inb(xn,yn)) continue;
            float dz = z0 - height[idx(xn,yn)];
            float s  = (dz + ((std::abs(dz)<1e-12f)?flatJitter:0.f)) / dist[k];
            if (s > bestSlope) { bestSlope = s; bestK = k; }
        }
        dir[idx(x,y)] = (int8_t)bestK;
        if (bestK >= 0) indeg[idx(x + dx[bestK], y + dy[bestK])] += 1;
    }

    // Kahn's topological order over downslope graph
    std::queue<std::pair<int,int>> q;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) if (indeg[idx(x,y)] == 0) q.emplace(x,y);

    while (!q.empty()) {
        auto [x,y] = q.front(); q.pop();
        const int i = (int)idx(x,y);
        const int8_t k = dir[i];
        if (k >= 0) {
            const int xn = x + dx[k], yn = y + dy[k];
            const int j  = (int)idx(xn,yn);
            accum[(size_t)j] += accum[(size_t)i];
            if (--indeg[(size_t)j] == 0) q.emplace(xn,yn);
        }
    }

    return { std::move(accum), std::move(dir), std::move(indeg) };
}

inline bool river_cell(float accum, float thresholdCells) noexcept {
    return accum >= thresholdCells;
}

} // namespace dem

// =================================================================================================
// Simple erosion (local utilities; keep heavy sims out of this header)
// =================================================================================================
namespace erosion {

// Thermal (talus) relaxation single iteration.
inline void thermal_step(float* height, int w, int h, float talusAngleDeg=30.f, float carry=0.5f) {
    const float talus = std::tan(talusAngleDeg * (kPi/180.f));
    std::vector<float> delta((size_t)w*h, 0.f);
    static const int dx[8] = {1,1,0,-1,-1,-1,0,1};
    static const int dy[8] = {0,1,1, 1, 0,-1,-1,-1};
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        float z = height[(size_t)y*w + x];
        float totalGive = 0.f;
        float gives[8] = {};
        int count=0;
        for (int k=0;k<8;k++) {
            int xn=x+dx[k], yn=y+dy[k];
            if (xn<0||xn>=w||yn<0||yn>=h) continue;
            float dz = z - height[(size_t)yn*w + xn];
            if (dz > 0.f) {
                float s = dz / ((k%2)? 1.41421356f : 1.f);
                if (s > talus) {
                    float amount = carry * (s - talus);
                    gives[k] = amount;
                    totalGive += amount;
                    count++;
                }
            }
        }
        if (totalGive > 0.f) {
            for (int k=0;k<8;k++) if (gives[k]>0.f) {
                int xn=x+dx[k], yn=y+dy[k];
                delta[(size_t)yn*w + xn] += gives[k] / (float)count;
            }
            delta[(size_t)y*w + x] -= totalGive;
        }
    }
    for (size_t i=0;i<(size_t)w*h;i++) height[i] += delta[i];
}

// Extremely simple hydraulic “rain & drain” step.
inline void hydraulic_step(float* height, float* water, float* sediment,
                           int w, int h, float rain=0.01f, float evap=0.002f,
                           float erodeK=0.03f, float depositK=0.03f) {
    auto idx = [w](int x,int y){ return (size_t)y*w + x; };
    static const int dx[8] = {1,1,0,-1,-1,-1,0,1};
    static const int dy[8] = {0,1,1, 1, 0,-1,-1,-1};
    std::vector<float> newW((size_t)w*h,0.f), newS((size_t)w*h,0.f);

    // rain
    for (size_t i=0;i<(size_t)w*h;i++) water[i] += rain;

    // one pass D8 transport
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        size_t i = idx(x,y);
        float z = height[i];

        // find lowest neighbor
        int bestK=-1; float bestDz=0.f;
        for (int k=0;k<8;k++) {
            int xn=x+dx[k], yn=y+dy[k]; if (xn<0||xn>=w||yn<0||yn>=h) continue;
            float dz = z - height[idx(xn,yn)];
            if (dz>bestDz) { bestDz=dz; bestK=k; }
        }
        float slope = std::max(0.f, bestDz);
        float capacity = slope * (water[i] + 1e-5f);
        if (sediment[i] > capacity) {
            float deposit = std::min(depositK*(sediment[i]-capacity), sediment[i]);
            sediment[i] -= deposit; height[i] += deposit;
        } else {
            float erode = erodeK*(capacity - sediment[i]);
            height[i] -= erode; sediment[i] += erode;
        }
        // move water+sediment
        water[i] *= (1.f - evap);
        if (bestK>=0) {
            int xn=x+dx[bestK], yn=y+dy[bestK];
            newW[idx(xn,yn)] += water[i];
            newS[idx(xn,yn)] += sediment[i];
        } else {
            newW[i] += water[i];
            newS[i] += sediment[i];
        }
    }
    water.swap(newW);
    sediment.swap(newS);
}

} // namespace erosion

// =================================================================================================
// Stage interface & registry
// =================================================================================================
class IWorldGenStage {
public:
    virtual ~IWorldGenStage() = default;
    virtual StageId      id()   const noexcept = 0;
    virtual const char*  name() const noexcept = 0;
    virtual void         generate(StageContext& ctx) = 0;
};

using StagePtr     = std::unique_ptr<IWorldGenStage>;
using StageFactory = std::function<StagePtr(const GeneratorSettings&)>;

struct StageDescriptor {
    StageId id = StageId::BaseElevation;
    const char* displayName = "";
    std::vector<StageId> dependencies;
    StageFactory factory;
};

struct StageIdHash {
    std::size_t operator()(StageId s) const noexcept {
        return static_cast<std::size_t>(to_underlying(s) * 1469598103934665603ULL);
    }
};

class StageRegistry {
public:
    void register_stage(StageDescriptor desc) {
        reg_[desc.id] = std::move(desc);
    }
    bool contains(StageId id) const { return reg_.find(id) != reg_.end(); }

    // Returns pipeline on success; on error returns empty and fills 'error' with a message.
    std::vector<StagePtr> make_pipeline(const GeneratorSettings& gs,
                                        const std::vector<StageId>& wanted,
                                        std::string* error = nullptr) const {
        enum class Mark : std::uint8_t { None, Temp, Done };
        std::unordered_map<StageId, Mark, StageIdHash> mark;
        std::vector<StageId> order; order.reserve(reg_.size());

        auto dfs = [&](auto&& self, StageId v) -> void {
            auto it = reg_.find(v);
            if (it == reg_.end()) {
                if (error) *error = std::string("Unknown stage requested: ") + stage_name(v);
                throw 1;
            }
            auto m = mark[v];
            if (m == Mark::Done) return;
            if (m == Mark::Temp) {
                if (error) *error = std::string("Cycle detected at stage: ") + stage_name(v);
                throw 1;
            }
            mark[v] = Mark::Temp;
            for (auto dep : it->second.dependencies) self(self, dep);
            mark[v] = Mark::Done;
            order.push_back(v);
        };

        try {
            for (StageId w : wanted) dfs(dfs, w);
        } catch (...) {
            return {};
        }

        std::vector<StagePtr> pipeline;
        pipeline.reserve(order.size());
        std::unordered_set<StageId, StageIdHash> seen;
        for (StageId id : order) if (seen.insert(id).second) {
            const auto& d = reg_.at(id);
            if (d.factory) pipeline.emplace_back(d.factory(gs));
        }
        return pipeline;
    }

private:
    std::unordered_map<StageId, StageDescriptor, StageIdHash> reg_;
};

// =================================================================================================
// Diagnostics & pipeline runner
// =================================================================================================
struct GenerationStats {
    std::uint64_t chunkSeed = 0;
    struct StageTiming { double ms = 0.0; std::uint64_t calls = 0; };
    std::unordered_map<StageId, StageTiming, StageIdHash> timings;
    void add_time(StageId id, double milliseconds) {
        auto& st = timings[id]; st.ms += milliseconds; st.calls++;
    }
};

class ScopedStageTimer {
public:
    ScopedStageTimer(GenerationStats* stats, StageId id) : stats_(stats), id_(id) {
        if (stats_) t0_ = Clock::now();
    }
    ~ScopedStageTimer() {
        if (!stats_) return;
        const auto t1 = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0_).count();
        stats_->add_time(id_, ms);
    }
private:
    using Clock = std::chrono::high_resolution_clock;
    GenerationStats* stats_ = nullptr;
    StageId id_{};
    Clock::time_point t0_{};
};

enum class GenError : std::uint8_t { None=0, StageFailed=1, Cancelled=2 };

struct CancelToken {
    std::atomic<bool> cancel{false};
    void request() noexcept { cancel.store(true, std::memory_order_relaxed); }
    bool is_requested() const noexcept { return cancel.load(std::memory_order_relaxed); }
};

struct PipelineCallbacks {
    // Called between stages: progress in [0..1], current stage id/name and optional message
    std::function<void(float /*progress*/, StageId /*id*/, const char* /*name*/)> onProgress;
    // Called if a stage throws; message is best-effort
    std::function<void(StageId, const char*)>        onError;
};

class WorldGenerationPipeline {
public:
    WorldGenerationPipeline() = default;
    explicit WorldGenerationPipeline(std::vector<StagePtr> stages) : stages_(std::move(stages)) {}
    std::size_t size()  const noexcept { return stages_.size(); }
    bool        empty() const noexcept { return stages_.empty(); }

    GenError run_all(StageContext& ctx, GenerationStats* stats = nullptr,
                     const CancelToken* cancel = nullptr,
                     const PipelineCallbacks* cbs = nullptr,
                     std::string* outError = nullptr) const {
        if (stats) stats->chunkSeed = ctx.chunk_seed();
        const float invN = (size() > 0) ? (1.0f / float(size())) : 1.f;
        for (size_t i=0;i<stages_.size();++i) {
            const auto& s = stages_[i];
            if (cancel && cancel->is_requested()) return GenError::Cancelled;
            if (cbs && cbs->onProgress) cbs->onProgress(float(i)*invN, s->id(), s->name());
            ScopedStageTimer timer(stats, s->id());
            try {
                s->generate(ctx);
            } catch (const std::exception& e) {
                if (outError) *outError = e.what();
                if (cbs && cbs->onError) cbs->onError(s->id(), e.what());
                return GenError::StageFailed;
            } catch (...) {
                if (outError) *outError = "Unknown stage failure";
                if (cbs && cbs->onError) cbs->onError(s->id(), "Unknown stage failure");
                return GenError::StageFailed;
            }
        }
        if (cbs && cbs->onProgress) cbs->onProgress(1.f, StageId::BaseElevation, "done");
        return GenError::None;
    }

    void push_back(StagePtr s) { stages_.emplace_back(std::move(s)); }

private:
    std::vector<StagePtr> stages_;
};

// =================================================================================================
// Minimal job system for parallel chunk generation (header-only)
// =================================================================================================
class JobQueue {
public:
    using Job = std::function<void()>;

    explicit JobQueue(int threads = 0) {
        int n = threads > 0 ? threads : (int)std::max(1u, std::thread::hardware_concurrency());
        workers_.reserve((size_t)n);
        for (int i=0;i<n;i++) workers_.emplace_back([this] { worker_loop(); });
    }
    ~JobQueue() {
        { std::lock_guard<std::mutex> lock(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    void enqueue(Job j) {
        { std::lock_guard<std::mutex> lock(m_); q_.push(std::move(j)); }
        cv_.notify_one();
    }
    void wait_idle() {
        std::unique_lock<std::mutex> lock(m_);
        idle_cv_.wait(lock, [this]{ return q_.empty() && active_ == 0; });
    }

    // Parallel for over a 2D rectangular domain [0..H)×[0..W) with coarse chunking.
    template <class Fn>
    void parallel_for2d(int h, int w, int tileH, int tileW, Fn fn) {
        if (workers_.empty() || (int)workers_.size() == 1) {
            for (int y=0; y<h; y+=tileH) for (int x=0; x<w; x+=tileW) fn(y, x, std::min(tileH, h - y), std::min(tileW, w - x));
            return;
        }
        std::atomic<int> nextY{0};
        auto worker = [&] {
            for (;;) {
                int y0 = nextY.fetch_add(tileH);
                if (y0 >= h) break;
                for (int x=0; x<w; x+=tileW) {
                    fn(y0, x, std::min(tileH, h - y0), std::min(tileW, w - x));
                }
            }
        };
        int tasks = std::max(1, h / std::max(1, tileH));
        for (int t=0; t<tasks; ++t) enqueue(worker);
        wait_idle();
    }

private:
    void worker_loop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this]{ return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                job = std::move(q_.front()); q_.pop();
                active_++;
            }
            job();
            {
                std::lock_guard<std::mutex> lock(m_);
                active_--;
                if (q_.empty() && active_ == 0) idle_cv_.notify_all();
            }
        }
    }
    std::vector<std::thread> workers_;
    std::queue<Job> q_;
    std::mutex m_;
    std::condition_variable cv_, idle_cv_;
    int  active_ = 0;
    bool stop_   = false;
};

// =================================================================================================
// Object scatter convenience (Poisson disk) and biome table scaffold
// =================================================================================================
inline float rand01(Pcg32& rng) noexcept { return (float)(rng.next_u32() * (1.0/4294967296.0)); }
inline float rand_range(Pcg32& rng, float a, float b) noexcept { return a + (b-a)*rand01(rng); }

// Uniform or mask/density-based scatter across the whole chunk
inline std::vector<ObjectInstance>
scatter_objects(const StageContext& ctx, StageId sid, float minDistanceMeters,
                std::uint32_t kindId, std::uint32_t tags, int maxCount = -1,
                std::function<float(Vec2)> maskOrDensity = {}) {
    Vec2 org = ctx.chunk_origin_world();
    const float span = ctx.cellSize() * (float)ctx.cells();
    auto localRng = ctx.sub_rng(sid, "scatter");
    auto pts = PoissonDiskSampler::generate(
        std::max(0.01f, minDistanceMeters), org, {org.x+span, org.y+span}, localRng, 30, std::move(maskOrDensity));

    std::vector<ObjectInstance> out;
    out.reserve(pts.size());
    const int cap = (maxCount < 0) ? (int)pts.size() : std::min<int>(maxCount, (int)pts.size());
    for (int i=0;i<cap;i++) {
        const Vec2 p = pts[(size_t)i];
        ObjectInstance inst{};
        inst.wx = p.x; inst.wy = p.y;
        inst.kind = kindId;
        inst.tags = tags;
        inst.scale = 0.85f + 0.3f * rand01(localRng);
        inst.rot   = rand_range(localRng, 0.f, kTau);
        inst.seed  = (std::uint32_t)ctx.sub_seed(sid, "scatter_item_"+std::to_string(i));
        out.push_back(inst);
    }
    return out;
}

// Simple biome lookup: threshold bins on temperature & moisture.
struct BiomeTable {
    int tempBands = 4;
    int moistBands = 4;
    std::vector<std::uint8_t> id; // size tempBands*moistBands
    std::uint8_t resolve(float tempC, float moisture01) const noexcept {
        auto clampi = [](int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); };
        // map temp into bands [-20..40C] as an example
        float tNorm = clamp01((tempC + 20.f) / 60.f);
        float mNorm = clamp01(moisture01);
        int ti = clampi(int(tNorm * tempBands), 0, tempBands-1);
        int mi = clampi(int(mNorm * moistBands), 0, moistBands-1);
        if (id.empty()) return 0;
        return id[(size_t)ti*moistBands + mi];
    }
};

} // namespace colony::worldgen
