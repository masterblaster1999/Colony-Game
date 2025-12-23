// src/worldgen/stages/Climate.cpp
// Make this TU self‑sufficient like the other stages.
#include "worldgen/WorldGen.hpp"
#include "worldgen/StageContext.hpp"
#include "worldgen/WorldChunk.hpp"     // ensure WorldChunk is fully defined for ctx.out.*
#include "worldgen/Noise.hpp"          // noise::fbm2D
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace colony::worldgen {

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
            const float gx = static_cast<float>(ctx.out.coord.x * N + x);
            const float gy = static_cast<float>(ctx.out.coord.y * N + y);
            const float fx = gx * kScale;
            const float fy = gy * kScale;

            // Temperature: base + noise - altitude penalty - crude latitude falloff
            const float tempNoise = fbm01(fx, fy, sT, 4, 2.0f, 0.5f) - 0.5f; // [-0.5,0.5]
            const float elev      = H.at(x, y); // 0..1
            const float latTerm   = std::abs(std::sin(gy * 0.0005f)); // pseudo-latitude in [0,1]

            float tempC = 30.0f * (1.0f - elev) + tempNoise * 10.0f - latTerm * 10.0f;
            T.at(x, y) = tempC;

            // Moisture: noise + bonus in valleys (inversely proportional to height)
            const float moistNoise = fbm01(fx * 1.4f + 37.0f, fy * 1.4f - 19.0f, sM, 4, 2.0f, 0.5f);
            const float moist = std::clamp(0.6f * moistNoise + 0.4f * (1.0f - elev), 0.0f, 1.0f);
            M.at(x, y) = moist;
        }
    }
}

} // namespace colony::worldgen
