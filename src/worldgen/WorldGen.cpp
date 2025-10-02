// src/worldgen/WorldGen.cpp
#include "WorldGen.hpp"
#include "Hash.hpp"
#include "RNG.hpp"
#include "Math.hpp"               // <-- use inline lerp/smoothstep from header
#include <cmath>
#include <algorithm> // std::clamp
#include <array>
#include <vector>
#include <utility>
#include <cstdint>

namespace colony::worldgen {

// -----------------------------------------------------------------------------
// PATCH NOTE:
//   We added a *named RNG stream* for the Scatter stage ("SCATTER1") so that it
//   draws from its own deterministic stream instead of depending only on the
//   stage id. If you keep a central list of stream names in WorldSeed.cpp,
//   mirror this constant there as well.
// -----------------------------------------------------------------------------
namespace {
    // ASCII-packed tag: "SCATTER1"
    static constexpr std::uint64_t STREAM_SCATTER = 0x5343415454455231ull;

    // Decide which RNG stream to use for a given stage.
    // New: ScatterStage gets its own named stream; others fall back to st->id().
    static inline std::uint64_t select_stream_for_stage(const StagePtr& st) {
        if (dynamic_cast<const ScatterStage*>(st.get()))
            return STREAM_SCATTER;          // dedicated, stable stream for scatter
        return static_cast<std::uint64_t>(st->id()); // default
    }
}

// -------------------- tiny helpers --------------------
// NOTE: Local lerp/smoothstep removed to avoid ODR/linkage clashes.
// Use the inline/constexpr implementations declared in Math.hpp.

// Hash-based value noise (no external deps). Deterministic & tileable via seeds.
static inline std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static inline float valNoise2D(int xi, int yi, std::uint32_t s) {
    std::uint32_t h = hash32(static_cast<std::uint32_t>(xi) * 374761393u
                           + static_cast<std::uint32_t>(yi) * 668265263u
                           + s * 362437u);
    return (h >> 8) * (1.0f / 16777216.0f); // [0,1)
}

static float fbm2D(float fx, float fy, std::uint32_t seed, int octaves, float lacunarity, float gain) {
    float amp = 0.5f, freq = 1.0f, sum = 0.f, norm = 0.f;
    for (int o=0; o<octaves; ++o) {
        int x0 = static_cast<int>(std::floor(fx * freq));
        int y0 = static_cast<int>(std::floor(fy * freq));
        float tx = fx * freq - x0;
        float ty = fy * freq - y0;

        float v00 = valNoise2D(x0,   y0,   seed + o * 1013904223u);
        float v10 = valNoise2D(x0+1, y0,   seed + o * 1013904223u);
        float v01 = valNoise2D(x0,   y0+1, seed + o * 1013904223u);
        float v11 = valNoise2D(x0+1, y0+1, seed + o * 1013904223u);

        float vx0 = lerp(v00, v10, smoothstep(0.f, 1.f, tx));
        float vx1 = lerp(v01, v11, smoothstep(0.f, 1.f, tx));
        float v   = lerp(vx0, vx1, smoothstep(0.f, 1.f, ty));

        sum  += v * amp;
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return sum / std::max(norm, 1e-6f);
}

// -------------------- WorldGenerator --------------------

WorldGenerator::WorldGenerator(GeneratorSettings s) : settings_(s) {
    // Default pipeline
    stages_.emplace_back(std::make_unique<BaseElevationStage>());
    stages_.emplace_back(std::make_unique<ClimateStage>());
    if (settings_.enableHydrology)
        stages_.emplace_back(std::make_unique<HydrologyStage>());
    stages_.emplace_back(std::make_unique<BiomeStage>());
    if (settings_.enableScatter)
        stages_.emplace_back(std::make_unique<ScatterStage>());
}

void WorldGenerator::clearStages() { stages_.clear(); }
void WorldGenerator::addStage(StagePtr stage) { stages_.emplace_back(std::move(stage)); }

WorldChunk WorldGenerator::makeEmptyChunk_(ChunkCoord coord) const {
    WorldChunk c{};
    c.coord = coord;
    const int N = settings_.cellsPerChunk;
    c.height.resize(N, N);
    c.temperature.resize(N, N);
    c.moisture.resize(N, N);
    c.flow.resize(N, N);
    c.biome.resize(N, N);
    return c;
}

void WorldGenerator::run_(WorldChunk& chunk, std::uint64_t worldSeed) const {
    for (const auto& st : stages_) {
        // PATCH: use a named per-stage stream where applicable (e.g., SCATTER1)
        const std::uint64_t streamName = select_stream_for_stage(st);
        auto [state, stream] = derive_pcg_seed(
            worldSeed, chunk.coord.x, chunk.coord.y, streamName
        );
        Pcg32 rng(state, stream);
        StageContext ctx{ settings_, chunk.coord, rng, chunk };
        st->generate(ctx);
    }
}

WorldChunk WorldGenerator::generate(ChunkCoord coord) const {
    WorldChunk c = makeEmptyChunk_(coord);
    run_(c, settings_.worldSeed);
    return c;
}

WorldChunk WorldGenerator::generate(ChunkCoord coord, std::uint64_t altWorldSeed) const {
    WorldChunk c = makeEmptyChunk_(coord);
    run_(c, altWorldSeed);
    return c;
}

// -------------------- Default Stages --------------------

// [moved] BaseElevationStage::generate(...) is now in src/worldgen/stages/BaseElevation.cpp
// [moved] ClimateStage::generate(...) is now in src/worldgen/stages/Climate.cpp
// [moved] HydrologyStage::generate(...) is now in src/worldgen/stages/Hydrology.cpp
// [moved] BiomeStage::generate(...) is now in src/worldgen/stages/Biome.cpp
// [moved] ScatterStage::generate(...) is now in src/worldgen/stages/Scatter.cpp

} // namespace colony::worldgen
