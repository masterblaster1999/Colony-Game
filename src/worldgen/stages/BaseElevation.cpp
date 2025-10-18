// src/worldgen/stages/BaseElevation.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/WorldChunk.hpp"  // needed: we access ctx.out.* members (WorldChunk)
#include "worldgen/Math.hpp"        // lerp(), smoothstep()
#include <algorithm>                // std::clamp
#include <cmath>                    // std::floor, std::pow
#include <cstdint>

namespace colony::worldgen {

// --- local helpers (TU-local; safe to duplicate across stages) ---
namespace {
    static inline std::uint32_t hash32(std::uint32_t x) noexcept {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static inline float valNoise2D(int xi, int yi, std::uint32_t s) noexcept {
        const std::uint32_t h = hash32(static_cast<std::uint32_t>(xi) * 374761393u
                                     + static_cast<std::uint32_t>(yi) * 668265263u
                                     + s * 362437u);
        return static_cast<float>((h >> 8) * (1.0 / 16777216.0)); // [0,1)
    }

    static float fbm2D(float fx, float fy,
                       std::uint32_t seed,
                       int octaves = 5,
                       float lacunarity = 2.0f,
                       float gain = 0.5f) noexcept
    {
        float amp = 0.5f, freq = 1.0f, sum = 0.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            const int x0 = static_cast<int>(std::floor(fx * freq));
            const int y0 = static_cast<int>(std::floor(fy * freq));
            const float tx = fx * freq - static_cast<float>(x0);
            const float ty = fy * freq - static_cast<float>(y0);

            const float v00 = valNoise2D(x0,   y0,   seed + static_cast<std::uint32_t>(o) * 1013904223u);
            const float v10 = valNoise2D(x0+1, y0,   seed + static_cast<std::uint32_t>(o) * 1013904223u);
            const float v01 = valNoise2D(x0,   y0+1, seed + static_cast<std::uint32_t>(o) * 1013904223u);
            const float v11 = valNoise2D(x0+1, y0+1, seed + static_cast<std::uint32_t>(o) * 1013904223u);

            const float vx0 = lerp(v00, v10, smoothstep(0.f, 1.f, tx));
            const float vx1 = lerp(v01, v11, smoothstep(0.f, 1.f, tx));
            const float v   = lerp(vx0, vx1, smoothstep(0.f, 1.f, ty));

            sum  += v * amp;
            norm += amp;
            amp  *= gain;
            freq *= lacunarity;
        }
        return (norm > 0.f) ? (sum / norm) : 0.f;
    }
} // namespace

void BaseElevationStage::generate(StageContext& ctx)
{
    auto& H = ctx.out.height;
    const int N = H.width();

    // Scale for continent-sized features; smaller = larger landmasses.
    constexpr float baseScale = 0.005f;

    // Per-chunk deterministic seeds (consistent across builds).
    const std::uint32_t sA = ctx.rng.nextUInt32();
    const std::uint32_t sB = ctx.rng.nextUInt32();

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            // Global (stitching) coordinates: keep seams invisible between chunks
            const float gx = static_cast<float>(ctx.out.coord.x * N + x);
            const float gy = static_cast<float>(ctx.out.coord.y * N + y);
            const float fx = gx * baseScale;
            const float fy = gy * baseScale;

            // Domain-like warp via two auxiliary fbm fields
            const float wx = fbm2D(fx * 2.0f,       fy * 2.0f,       sA, 3, 2.0f, 0.5f) * 2.f - 1.f;
            const float wy = fbm2D(fx * 2.0f + 100, fy * 2.0f - 50,  sB, 3, 2.0f, 0.5f) * 2.f - 1.f;

            float h = fbm2D(fx + 0.25f * wx, fy + 0.25f * wy,
                            sA ^ 0x9E3779B9u, 5, 2.0f, 0.5f);
            h = std::clamp(h, 0.0f, 1.0f);

            // Slight contrast shaping: push seas down, peaks up a little
            h = std::pow(h, 1.5f);

            H.at(x, y) = h;
        }
    }
}

} // namespace colony::worldgen
