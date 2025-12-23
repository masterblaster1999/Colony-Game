// src/worldgen/stages/BaseElevation.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/WorldChunk.hpp"  // needed: we access ctx.out.* members (WorldChunk)
#include "worldgen/Noise.hpp"       // noise::fbm2D
#include <algorithm>                // std::clamp
#include <cmath>                    // std::floor, std::pow
#include <cstdint>

namespace colony::worldgen {

void BaseElevationStage::generate(StageContext& ctx)
{
    auto& H = ctx.out.height;
    const int N = H.width();

    // Scale for continent-sized features; smaller = larger landmasses.
    constexpr float baseScale = 0.005f;

    // Per-chunk deterministic seeds (consistent across builds).
    const std::uint32_t sA = ctx.rng.nextUInt32();
    const std::uint32_t sB = ctx.rng.nextUInt32();

    // Map noise::fbm2D (~[-1,1]) into [0,1] in a deterministic, octave-aware way.
    const auto fbm01 = [](float x, float y,
                          std::uint32_t seed,
                          int octaves,
                          float lacunarity,
                          float gain) noexcept -> float
    {
        const float n = noise::fbm2D(x, y, seed, octaves, lacunarity, gain);

        // noise::fbm2D uses amp=0.5 and multiplies by gain each octave.
        float norm = 0.0f;
        float amp = 0.5f;
        for (int i = 0; i < octaves; ++i) {
            norm += amp;
            amp *= gain;
        }

        const float v = (norm > 0.0f) ? (n / norm) : 0.0f; // ~[-1,1]
        return std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);     // -> [0,1]
    };

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            // Global (stitching) coordinates: keep seams invisible between chunks
            const float gx = static_cast<float>(ctx.out.coord.x * N + x);
            const float gy = static_cast<float>(ctx.out.coord.y * N + y);
            const float fx = gx * baseScale;
            const float fy = gy * baseScale;

            // Domain-like warp via two auxiliary fbm fields
            const float wx = fbm01(fx * 2.0f,       fy * 2.0f,       sA, 3, 2.0f, 0.5f) * 2.f - 1.f;
            const float wy = fbm01(fx * 2.0f + 100, fy * 2.0f - 50,  sB, 3, 2.0f, 0.5f) * 2.f - 1.f;

            float h = fbm01(fx + 0.25f * wx, fy + 0.25f * wy,
                            sA ^ 0x9E3779B9u, 5, 2.0f, 0.5f);
            h = std::clamp(h, 0.0f, 1.0f);

            // Slight contrast shaping: push seas down, peaks up a little
            h = std::pow(h, 1.5f);

            H.at(x, y) = h;
        }
    }
}

} // namespace colony::worldgen
