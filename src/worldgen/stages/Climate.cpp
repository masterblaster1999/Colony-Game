// src/worldgen/stages/Climate.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/Math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace colony::worldgen {

namespace {
    // same helpers as BaseElevation; duplicated by design (TU-local)
    static inline std::uint32_t hash32(std::uint32_t x) noexcept {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
    }
    static inline float valNoise2D(int xi, int yi, std::uint32_t s) noexcept {
        const std::uint32_t h = hash32(static_cast<std::uint32_t>(xi) * 374761393u
                                     + static_cast<std::uint32_t>(yi) * 668265263u
                                     + s * 362437u);
        return static_cast<float>((h >> 8) * (1.0 / 16777216.0));
    }
    static float fbm2D(float fx, float fy, std::uint32_t seed,
                       int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) noexcept
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
}

void ClimateStage::generate(StageContext& ctx)
{
    auto& H = ctx.out.height;
    auto& T = ctx.out.temperature;
    auto& M = ctx.out.moisture;

    const int N = H.width();

    const std::uint32_t sT = ctx.rng.nextUInt32();
    const std::uint32_t sM = ctx.rng.nextUInt32();

    // Global scale used by the climate fields
    constexpr float kScale = 0.0025f;

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float gx = static_cast<float>(ctx.out.coord.x * N + x);
            const float gy = static_cast<float>(ctx.out.coord.y * N + y);
            const float fx = gx * kScale;
            const float fy = gy * kScale;

            // Temperature: base + noise - altitude penalty - crude latitude falloff
            const float tempNoise = fbm2D(fx, fy, sT, 4, 2.0f, 0.5f) - 0.5f; // [-0.5,0.5]
            const float elev      = H.at(x, y); // 0..1
            const float latTerm   = std::abs(std::sin(gy * 0.0005f)); // pseudo-latitude in [0,1]

            float tempC = 30.0f * (1.0f - elev) + tempNoise * 10.0f - latTerm * 10.0f;
            T.at(x, y) = tempC;

            // Moisture: noise + bonus in valleys (inversely proportional to height)
            const float moistNoise = fbm2D(fx * 1.4f + 37.0f, fy * 1.4f - 19.0f, sM, 4, 2.0f, 0.5f);
            const float moist = std::clamp(0.6f * moistNoise + 0.4f * (1.0f - elev), 0.0f, 1.0f);
            M.at(x, y) = moist;
        }
    }
}

} // namespace colony::worldgen
