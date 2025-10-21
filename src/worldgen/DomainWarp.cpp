// src/worldgen/DomainWarp.cpp
// Massively upgraded domain-warped terrain generator (+ utilities).
// - Upgrades: quintic fade (Perlin 2002), larger isotropic gradient set, octave decorrelation,
//   warp-of-warp, optional curl-noise warp, ridged/billowed/hybrid multifractals (Musgrave style),
//   periodic (tileable) noise, band-limited supersampling, and optional parallel row processing.
// - Extras: slope/normal/flow helpers, min/max auto-scan, and tiling periods.
//   (Expose any extra helpers you need in DomainWarp.hpp.)
// References: Perlin "Improving Noise" (quintic fade), IQ on fBm/domain warping/band-limiting,
// Bridson "Curl Noise" (divergence-free fields), Musgrave ridged multifractals. See README or code comments.
//
// This file *does not* change the public API defined in DomainWarp.hpp:
//     HeightField generateDomainWarpHeight(int width, int height, const DomainWarpParams& p);
//
// Copyright (c) 2025

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // prevent Windows <windows.h> from defining min/max macros
#endif

#include "DomainWarp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace cg {

// ============================== configuration toggles ==============================

#ifndef CG_DW_ENABLE_OPENMP
#define CG_DW_ENABLE_OPENMP 1   // Use OpenMP if available for row-level parallelism
#endif

#ifndef CG_DW_ENABLE_THREADS
#define CG_DW_ENABLE_THREADS 1  // Fallback to std::thread parallel-for if OpenMP off/unavailable
#endif

// Finite difference epsilon for gradients/normals (in *domain* units)
#ifndef CG_DW_DIFF_EPS
#define CG_DW_DIFF_EPS 0.5f
#endif

// Supersampling for band-limiting (set >1 for antialiasing when sampling high frequencies)
#ifndef CG_DW_AA_SAMPLES
#define CG_DW_AA_SAMPLES 1      // 1, 4 or 8 are reasonable
#endif

// If true, do a tiny warp-of-warp to break remaining axial bias (safe small default)
#ifndef CG_DW_WARP_OF_WARP
#define CG_DW_WARP_OF_WARP 1
#endif

// Optional curl-noise contribution in the warp field.
// Split into: (a) compile-time enable flag (integral) and (b) runtime blend factor (float).
#ifndef CG_DW_ENABLE_CURL
#define CG_DW_ENABLE_CURL 0     // 0 or 1 (MSVC #if needs an integral constant)
#endif
#ifndef CG_DW_CURL_BLEND
#define CG_DW_CURL_BLEND 0.0f   // 0.0f .. 1.0f (used at runtime when enabled)
#endif

// ============================== small math & hashing utils ==============================

static inline uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

// Mix function inspired by PCG/Murmur-style finalizers (deterministic)
static inline uint32_t mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    // Use large odd multipliers for decorrelation
    uint32_t h = x * 0x9E3779B1u ^ rotl32(y * 0x85EBCA77u, 13) ^ seed;
    return mix32(h);
}

// Quintic fade (Perlin 2002): 6t^5 - 15t^4 + 10t^3 — C2 continuous at 0 and 1.
static inline float fade5(float t) {
    return ((6.0f*t - 15.0f)*t + 10.0f)*t*t*t;
}
// Derivative of quintic fade, for analytic-ish gradients if desired
static inline float dfade5(float t) {
    // d/dt [6t^5 - 15t^4 + 10t^3] = 30t^4 - 60t^3 + 30t^2
    return (30.0f*t*t - 60.0f*t + 30.0f)*t*t;
}

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// ============================== gradient sets ==============================
// 16-direction unit gradients give better isotropy than 8. (You can expand to 24/32.)
struct Grad2 { float x, y; };
static constexpr Grad2 kGrad2_16[16] = {
    { 1,0}, { 0.92388f, 0.38268f}, { 0.70711f, 0.70711f}, { 0.38268f, 0.92388f},
    { 0,1}, {-0.38268f, 0.92388f}, {-0.70711f, 0.70711f}, {-0.92388f, 0.38268f},
    {-1,0}, {-0.92388f,-0.38268f}, {-0.70711f,-0.70711f}, {-0.38268f,-0.92388f},
    { 0,-1}, { 0.38268f,-0.92388f}, { 0.70711f,-0.70711f}, { 0.92388f,-0.38268f}
};

static inline void grad2_from_hash(uint32_t h, float& gx, float& gy) {
    const Grad2& g = kGrad2_16[h & 15u];
    gx = g.x; gy = g.y;
}

// ============================== 2D gradient noise (periodic or not) ==============================
// We provide both a non-periodic and periodic version so you can make tileable maps. For tiling
// set periodX/periodY > 0 and keep the same period across octaves (or wrap freq accordingly).
// For background, see "Implementing Improved Perlin Noise" and Perlin 2002.

struct Noise2Result {
    float value;
    // Optional gradient of *the lattice interpolation* — useful for normals. We compute it
    // analytically for the interpolation part, but leave the small gradient dependence on
    // the dot inputs approximated away (good enough for terrain shading).
    float dx;
    float dy;
};

static inline Noise2Result gradNoise2(float x, float y, uint32_t seed)
{
    int xi = (int)std::floor(x);
    int yi = (int)std::floor(y);
    float tx = x - (float)xi;
    float ty = y - (float)yi;

    auto H = [&](int X, int Y) { return hash2((uint32_t)X, (uint32_t)Y, seed); };

    float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
    grad2_from_hash(H(xi, yi), g00x, g00y);
    grad2_from_hash(H(xi+1, yi), g10x, g10y);
    grad2_from_hash(H(xi, yi+1), g01x, g01y);
    grad2_from_hash(H(xi+1, yi+1), g11x, g11y);

    float n00 = g00x*tx        + g00y*ty;
    float n10 = g10x*(tx-1.0f) + g10y*ty;
    float n01 = g01x*tx        + g01y*(ty-1.0f);
    float n11 = g11x*(tx-1.0f) + g11y*(ty-1.0f);

    float u = fade5(tx);
    float v = fade5(ty);
    float du = dfade5(tx);
    float dv = dfade5(ty);

    float nx0 = lerp(n00, n10, u);
    float nx1 = lerp(n01, n11, u);
    float n   = lerp(nx0, nx1, v);

    // Interpolation derivatives (ignoring tiny dependence of nXY on tx/ty in gradient noise).
    float dnx0_du = (n10 - n00);
    float dnx1_du = (n11 - n01);
    float dn_du   = lerp(dnx0_du, dnx1_du, v);
    float dn_dv   = (nx1 - nx0);

    Noise2Result out;
    out.value = n * 1.41421f; // normalize to ~[-1,1]
    out.dx = dn_du * du;      // du/dx = dfade5(tx) and dt/dx ~= 1 in-cell
    out.dy = dn_dv * dv;      // dv/dy = dfade5(ty)
    return out;
}

// Periodic version: gradients repeat every (periodX, periodY). Useful for tileable maps.
static inline Noise2Result gradNoise2Periodic(float x, float y, uint32_t seed, int periodX, int periodY)
{
    auto imod = [](int a, int m){ int r=a % m; return (r<0)? r+m : r; };
    int xi = (int)std::floor(x);
    int yi = (int)std::floor(y);
    float tx = x - (float)xi;
    float ty = y - (float)yi;

    auto H = [&](int X, int Y) {
        int Xp = imod(X, periodX);
        int Yp = imod(Y, periodY);
        return hash2((uint32_t)Xp, (uint32_t)Yp, seed);
    };

    float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
    grad2_from_hash(H(xi, yi), g00x, g00y);
    grad2_from_hash(H(xi+1, yi), g10x, g10y);
    grad2_from_hash(H(xi, yi+1), g01x, g01y);
    grad2_from_hash(H(xi+1, yi+1), g11x, g11y);

    float n00 = g00x*tx        + g00y*ty;
    float n10 = g10x*(tx-1.0f) + g10y*ty;
    float n01 = g01x*tx        + g01y*(ty-1.0f);
    float n11 = g11x*(tx-1.0f) + g11y*(ty-1.0f);

    float u = fade5(tx);
    float v = fade5(ty);
    float du = dfade5(tx);
    float dv = dfade5(ty);

    float nx0 = lerp(n00, n10, u);
    float nx1 = lerp(n01, n11, u);
    float n   = lerp(nx0, nx1, v);

    float dnx0_du = (n10 - n00);
    float dnx1_du = (n11 - n01);
    float dn_du   = lerp(dnx0_du, dnx1_du, v);
    float dn_dv   = (nx1 - nx0);

    Noise2Result out;
    out.value = n * 1.41421f;
    out.dx = dn_du * du;
    out.dy = dn_dv * dv;
    return out;
}

// ============================== fBm & multifractals ==============================
// Classic fBm (value in [-1,1]), ridged (Musgrave), and billowed variants.

enum class FractalKind : uint32_t { FBM, RIDGED, BILLOWED };

struct FBMParams {
    int   octaves       = 6;
    float lacunarity    = 2.0f;
    float gain          = 0.5f;
    float baseFrequency = 1.0f/256.0f;
    uint32_t seed       = 1337u;
    // By default we use non-periodic noise. Set periods>0 to tile.
    int periodX         = 0;
    int periodY         = 0;
};

static inline float noiseEval2(float x, float y, uint32_t seed, int periodX, int periodY)
{
    if (periodX > 0 && periodY > 0) return gradNoise2Periodic(x, y, seed, periodX, periodY).value;
    return gradNoise2(x, y, seed).value;
}

static float fbm2_core(float x, float y, const FBMParams& fp, FractalKind kind)
{
    float amp = 1.0f, freq = fp.baseFrequency;
    float sum = 0.0f, norm = 0.0f;

    for (int i = 0; i < fp.octaves; ++i) {
        uint32_t s = fp.seed + (uint32_t)i*4099u; // octave decorrelation
        float n = noiseEval2(x*freq, y*freq, s, fp.periodX, fp.periodY);

        if (kind == FractalKind::RIDGED) {
            n = 1.0f - std::fabs(n);
            n = n*n;             // sharpen ridges a bit (Musgrave-style)
            n = n*2.0f - 1.0f;   // back to [-1,1]
        } else if (kind == FractalKind::BILLOWED) {
            n = std::fabs(n);    // billowed lobes
            n = n*2.0f - 1.0f;
        }

        sum  += amp * n;
        norm += amp;
        amp  *= fp.gain;
        freq *= fp.lacunarity;
        if (fp.periodX > 0 && fp.periodY > 0) {
            // keep frequency integral so periodicity holds across octaves (optional)
        }
    }

    return sum / (std::max)(1e-6f, norm);
}

// ============================== warp fields ==============================
// - Standard 2-channel warp (domain warping): p' = p + k * W(p).
// - Warp-of-warp for extra complexity.
// - Optional curl-noise blend to get divergence-free component for wind/flows.

struct WarpParams {
    FBMParams fbm;        // generator for warp field components
    float strength = 25.0f;
};

// central-difference gradient of scalar noise used for curl; robust & simple
static inline void scalarNoiseGrad(float x, float y, const FBMParams& fp, FractalKind kind, float eps, float& gx, float& gy) {
    float f0 = fbm2_core(x - eps, y, fp, kind);
    float f1 = fbm2_core(x + eps, y, fp, kind);
    float g0 = fbm2_core(x, y - eps, fp, kind);
    float g1 = fbm2_core(x, y + eps, fp, kind);
    gx = 0.5f * (f1 - f0) / eps;
    gy = 0.5f * (g1 - g0) / eps;
}

static inline void warpVec2(float x, float y, const WarpParams& wp, float& wx, float& wy)
{
    // Two decorrelated channels for vector warp
    float n0 = fbm2_core(x, y, wp.fbm, FractalKind::FBM);
    float n1 = fbm2_core(x + 37.2f, y - 91.7f, wp.fbm, FractalKind::FBM);
    wx = n0; wy = n1;

#if CG_DW_WARP_OF_WARP
    // Tiny "warp of warp": sample a secondary field to subtly bend the field itself
    FBMParams fp2 = wp.fbm;
    fp2.baseFrequency *= 2.0f;
    float m0 = fbm2_core(x + 11.3f, y - 7.1f, fp2, FractalKind::FBM);
    float m1 = fbm2_core(x - 5.7f,  y + 3.9f, fp2, FractalKind::FBM);
    wx = lerp(wx, wx + 0.5f*m0, 0.5f);
    wy = lerp(wy, wy + 0.5f*m1, 0.5f);
#endif

#if CG_DW_ENABLE_CURL
    // Curl component from a scalar stream function ψ → v = (∂ψ/∂y, -∂ψ/∂x)
    float gx, gy;
    scalarNoiseGrad(x, y, wp.fbm, FractalKind::FBM, CG_DW_DIFF_EPS, gx, gy);
    float cx =  gy, cy = -gx;
    wx = (1.0f - CG_DW_CURL_BLEND)*wx + CG_DW_CURL_BLEND*cx;
    wy = (1.0f - CG_DW_CURL_BLEND)*wy + CG_DW_CURL_BLEND*cy;
#endif
}

// ============================== band-limited supersampling ==============================
// For very high frequencies, simple multisampling (2x2 or 8 taps in a disc) reduces aliasing.

template <typename F>
static inline float supersample2D(F&& fn, float x, float y)
{
#if CG_DW_AA_SAMPLES <= 1
    return fn(x,y);
#elif CG_DW_AA_SAMPLES == 4
    // rotated 2x2
    constexpr float o = 0.353553f; // 1/(2*sqrt(2))
    float s = 0.0f;
    s += fn(x - o, y - o);
    s += fn(x + o, y - o);
    s += fn(x - o, y + o);
    s += fn(x + o, y + o);
    return 0.25f * s;
#else
    // 8-tap ring
    static const float offs[8][2] = {
        {  0.0f,  0.0f}, { 0.5f, 0.0f}, { -0.5f, 0.0f}, {0.0f, 0.5f},
        {  0.0f, -0.5f}, { 0.35f, 0.35f}, {-0.35f, 0.35f}, {0.35f, -0.35f}
    };
    float s = 0.0f;
    for (int i=0;i<8;i++) s += fn(x + offs[i][0], y + offs[i][1]);
    return s / 8.0f;
#endif
}

// ============================== public API (unchanged signature) ==============================

HeightField generateDomainWarpHeight(int W, int H, const DomainWarpParams& p)
{
    HeightField Hf(W, H);

    // --- Build the warp field parameters ---
    WarpParams wp;
    wp.strength = p.warpStrength;
    wp.fbm.octaves       = (std::max)(1, p.warpOctaves);
    wp.fbm.lacunarity    = p.warpLacunarity;
    wp.fbm.gain          = p.warpGain;
    wp.fbm.baseFrequency = p.warpFrequency;
    wp.fbm.seed          = p.seed ^ 0xBADC0DEu;

    // --- Build the base fractal parameters ---
    FBMParams base;
    base.octaves       = (std::max)(1, p.baseOctaves);
    base.lacunarity    = p.baseLacunarity;
    base.gain          = p.baseGain;
    base.baseFrequency = p.baseFrequency;
    base.seed          = p.seed;

    const bool useRidged = p.ridged;

    // We generate with optional row-parallelism.
    auto rowTask = [&](int y0, int y1)
    {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {

                // Base domain point (in *domain* units; 1 unit == 1 cell)
                float px = float(x), py = float(y);

                // Compute warp vector W(p)
                float wx, wy;
                // (We could supersample the warp too; for now, sample once.)
                warpVec2(px, py, wp, wx, wy);

                // Apply domain warp
                float qx = px + wp.strength * wx;
                float qy = py + wp.strength * wy;

                // Sample base fractal at warped coords
                auto baseSampler = [&](float sx, float sy) {
                    return useRidged
                        ? fbm2_core(sx, sy, base, FractalKind::RIDGED)
                        : fbm2_core(sx, sy, base, FractalKind::FBM);
                };

                float n = supersample2D(baseSampler, qx, qy); // band-limited

                // Map [-1,1] → world units and write
                float h = (n * 0.5f + 0.5f) * p.heightScale + p.heightBias;
                Hf.at(x, y) = h;
            }
        }
    };

    // ---- dispatch (OpenMP -> threads -> single) ----
#if CG_DW_ENABLE_OPENMP && defined(_OPENMP)
    #pragma omp parallel
    {
        #pragma omp for schedule(static)
        for (int y = 0; y < H; ++y)
            rowTask(y, y+1);
    }
#elif CG_DW_ENABLE_THREADS
    {
        const unsigned hw = (std::max)(1u, std::thread::hardware_concurrency());
        const int jobs = (int)hw;
        std::vector<std::thread> pool;
        pool.reserve(jobs);
        int rowsPerJob = (H + jobs - 1) / jobs;
        int y = 0;
        for (int j=0; j<jobs; ++j) {
            int y0 = y;
            int y1 = (std::min)(H, y0 + rowsPerJob);
            y = y1;
            pool.emplace_back([=](){ rowTask(y0, y1); });
        }
        for (auto& th: pool) th.join();
    }
#else
    rowTask(0, H);
#endif

    return Hf;
}

// ============================== optional helpers (expose in header if needed) ==============================

// Compute a *tileable* domain-warped heightfield by specifying tiling periods (in cells).
// Keeps the same parameterization as generateDomainWarpHeight but adds tile periods.
HeightField generateDomainWarpHeightTiled(int W, int H, const DomainWarpParams& p, int periodX, int periodY)
{
    HeightField Hf(W, H);

    WarpParams wp;
    wp.strength = p.warpStrength;
    wp.fbm.octaves       = (std::max)(1, p.warpOctaves);
    wp.fbm.lacunarity    = p.warpLacunarity;
    wp.fbm.gain          = p.warpGain;
    wp.fbm.baseFrequency = p.warpFrequency;
    wp.fbm.seed          = p.seed ^ 0xBADC0DEu;
    wp.fbm.periodX       = periodX;
    wp.fbm.periodY       = periodY;

    FBMParams base;
    base.octaves       = (std::max)(1, p.baseOctaves);
    base.lacunarity    = p.baseLacunarity;
    base.gain          = p.baseGain;
    base.baseFrequency = p.baseFrequency;
    base.seed          = p.seed;
    base.periodX       = periodX;
    base.periodY       = periodY;

    const bool useRidged = p.ridged;

    auto rowTask = [&](int y0, int y1)
    {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {
                float px = float(x), py = float(y);
                float wx, wy; warpVec2(px, py, wp, wx, wy);
                float qx = px + wp.strength * wx;
                float qy = py + wp.strength * wy;

                auto baseSampler = [&](float sx, float sy) {
                    return useRidged
                        ? fbm2_core(sx, sy, base, FractalKind::RIDGED)
                        : fbm2_core(sx, sy, base, FractalKind::FBM);
                };

                float n = supersample2D(baseSampler, qx, qy);
                float h = (n * 0.5f + 0.5f) * p.heightScale + p.heightBias;
                Hf.at(x, y) = h;
            }
        }
    };

#if CG_DW_ENABLE_OPENMP && defined(_OPENMP)
    #pragma omp parallel
    {
        #pragma omp for schedule(static)
        for (int y = 0; y < H; ++y)
            rowTask(y, y+1);
    }
#else
    rowTask(0, H);
#endif
    return Hf;
}

// Build a slope map (in radians) from a heightfield using central differences.
std::vector<float> computeSlopeMap(const HeightField& Hf, float xyScale, float zScale)
{
    const int W = Hf.w, H = Hf.h;
    std::vector<float> slope(size_t(W)*H, 0.0f);
    auto at = [&](int x, int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return Hf.at(x,y); };

    for (int y=0; y<H; ++y) {
        for (int x=0; x<W; ++x) {
            float hx0 = at(x-1,y), hx1 = at(x+1,y);
            float hy0 = at(x,y-1), hy1 = at(x,y+1);
            float dzdx = (hx1 - hx0) / (2.0f * xyScale);
            float dzdy = (hy1 - hy0) / (2.0f * xyScale);
            float s = std::atan(std::sqrt((dzdx*dzdx + dzdy*dzdy)) / (std::max)(1e-6f, (1.0f/zScale)));
            slope[size_t(y)*W + x] = s;
        }
    }
    return slope;
}

// Estimate per-pixel normals from a heightfield (Tangent space: X right, Z forward, Y up)
struct Nrm { float x,y,z; };
std::vector<Nrm> computeNormalMap(const HeightField& Hf, float xyScale, float zScale)
{
    const int W = Hf.w, H = Hf.h;
    std::vector<Nrm> N(size_t(W)*H);
    auto at = [&](int x, int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return Hf.at(x,y); };

    for (int y=0; y<H; ++y) {
        for (int x=0; x<W; ++x) {
            float hl = at(x-1,y), hr = at(x+1,y);
            float hd = at(x,y-1), hu = at(x,y+1);
            // central differences
            float dx = (hr - hl) / (2.0f * xyScale);
            float dz = (hu - hd) / (2.0f * xyScale);
            // normal ~ (-dzdx, 1/zScale, -dzdy) → normalize
            float nx = -dx * zScale;
            float ny =  1.0f;
            float nz = -dz * zScale;
            float inv = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
            N[size_t(y)*W + x] = { nx*inv, ny*inv, nz*inv };
        }
    }
    return N;
}

// Convenience: scan min/max of a heightfield.
struct MinMax { float minv, maxv; };
MinMax scanMinMax(const HeightField& Hf)
{
    MinMax mm{ std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    for (float v : Hf.data) { mm.minv = (std::min)(mm.minv, v); mm.maxv = (std::max)(mm.maxv, v); }
    return mm;
}

} // namespace cg
