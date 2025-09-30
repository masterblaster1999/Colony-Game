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

// 1) Base elevation (fBm on a large scale + domain-like warp via secondary fbm)
void BaseElevationStage::generate(StageContext& ctx) {
    const int N = ctx.out.height.width();
    // Scale world coordinates so each chunk stitches nicely without visible seams
    // (Seams are mainly avoided by using deterministic seeds and continuous fbm.)
    const float baseScale = 0.005f; // lower = wider landmasses

    // Two seeds to introduce domain-like warp
    const std::uint32_t sA = static_cast<std::uint32_t>(ctx.rng.nextUInt32());
    const std::uint32_t sB = static_cast<std::uint32_t>(ctx.rng.nextUInt32());

    for (int y=0; y<N; ++y) {
        for (int x=0; x<N; ++x) {
            // Worldspace (continuous across chunks)
            const float wx = (ctx.chunkX * N + x) * ctx.settings.cellSizeMeters * baseScale;
            const float wy = (ctx.chunkY * N + y) * ctx.settings.cellSizeMeters * baseScale;

            // subtle warp
            const float warpX = fbm2D(wx, wy, sB, 3, 2.03f, 0.5f) * 2.f - 1.f;
            const float warpY = fbm2D(wx + 1000.f, wy - 1000.f, sB ^ 0xBEEFCAFEu, 3, 2.07f, 0.5f) * 2.f - 1.f;

            float e = fbm2D(wx + 1.7f*warpX, wy + 1.7f*warpY, sA, 5, 2.01f, 0.5f);
            e = std::pow(e, 1.2f); // lift mountains a bit
            // Normalize to ~meters (optional — keep normalized 0..1 for now)
            ctx.out.height.at(x,y) = e; // 0..1
        }
    }
}

// 2) Climate (temperature & moisture via lat + elevation + noise)
void ClimateStage::generate(StageContext& ctx) {
    const int N = ctx.out.height.width();

    // Temperature from "latitude" (y in world units) + elevation lapse rate
    const float tempSeaLevelEquator = 30.f;   // C
    const float tempSeaLevelPole    = -10.f;  // C
    const float lapsePerUnitElev    = 12.f;   // C lost from lowlands->high peaks (since height is 0..1)

    const std::uint32_t sT = ctx.rng.nextUInt32();
    const std::uint32_t sM = ctx.rng.nextUInt32();

    for (int y=0; y<N; ++y) {
        for (int x=0; x<N; ++x) {
            const float lat = ((ctx.chunkY * N + y) / 8192.0f);     // fake "latitude" wrap
            const float latT = lerp(tempSeaLevelEquator, tempSeaLevelPole, std::fmod(std::abs(lat), 1.0f));
            const float elev = ctx.out.height.at(x,y);                // 0..1

            float tNoise = fbm2D(x * 0.01f, y * 0.01f, sT, 3, 2.15f, 0.5f) * 2.f - 1.f;
            float mNoise = fbm2D(x * 0.01f, y * 0.01f, sM, 4, 2.08f, 0.5f);

            float T = latT - elev * lapsePerUnitElev + tNoise * 2.0f;
            float M = std::clamp(mNoise * (1.f - elev * 0.5f), 0.f, 1.f);

            ctx.out.temperature.at(x,y) = T;
            ctx.out.moisture.at(x,y)    = M;
        }
    }
}

// 3) Hydrology (super-light flow accumulation; good enough for rivers)
void HydrologyStage::generate(StageContext& ctx) {
    const int N = ctx.out.height.width();
    auto& H = ctx.out.height;
    auto& F = ctx.out.flow;
    F.fill(0.f);

    // Single-pass "downslope" accumulation (approximate)
    // For more realism you can do multi-iteration with priority queues, but this is a start.
    std::vector<std::pair<int,int>> order; order.reserve(N*N);
    for (int y=0; y<N; ++y) for (int x=0; x<N; ++x) order.emplace_back(x,y);
    std::sort(order.begin(), order.end(), [&](auto a, auto b){
        return H.at(a.first, a.second) > H.at(b.first, b.second);
    });

    constexpr int dx[8] = {-1,0,1,-1,1,-1,0,1};
    constexpr int dy[8] = {-1,-1,-1,0,0,1,1,1};

    for (auto [x,y] : order) {
        // rain contribution (can vary by biome later)
        float flux = 0.002f + F.at(x,y);
        // Send to steepest neighbor
        float bestDrop = 0.f; int bx = -1, by = -1;
        const float h = H.at(x,y);
        for (int k=0; k<8; ++k) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            float drop = h - H.at(nx,ny);
            if (drop > bestDrop) { bestDrop = drop; bx = nx; by = ny; }
        }
        if (bx >= 0) {
            F.at(bx,by) += flux;
            // Slight channel carving for main rivers
            if (flux > 0.02f)
                H.at(x,y) = std::max(0.f, H.at(x,y) - bestDrop * 0.05f);
        }
    }
}

// 4) Biome classification (T, M) -> id (tiny Whittaker-like grid)
void BiomeStage::generate(StageContext& ctx) {
    const int N = ctx.out.height.width();
    auto& T = ctx.out.temperature;
    auto& M = ctx.out.moisture;
    auto& B = ctx.out.biome;

    auto classify = [](float tempC, float moist)->std::uint8_t {
        // Example mapping (0..8). Customize later.
        if (moist < 0.2f)        return (tempC > 20.f) ? 1 /*Desert*/ : 2 /*Cold Steppe*/;
        if (moist < 0.45f)       return (tempC > 15.f) ? 3 /*Savanna*/: 4 /*Shrubland*/;
        if (moist < 0.7f)        return (tempC > 5.f)  ? 5 /*Temperate Forest*/ : 6 /*Boreal*/;
        return (tempC > 0.f)     ? 7 /*Rainforest*/ : 8 /*Tundra*/;
    };

    for (int y=0; y<N; ++y)
        for (int x=0; x<N; ++x)
            B.at(x,y) = classify(T.at(x,y), M.at(x,y));
}

// 5) Scatter (blue-noise-ish via RNG rejection & biome weights)
void ScatterStage::generate(StageContext& ctx) {
    auto& B = ctx.out.biome;
    const int N = B.width();
    auto& objs = ctx.out.objects;
    objs.clear();

    // Simple per-biome density table (instances per 10k cells)
    std::array<float, 10> density{};
    density[1] = 0.2f;  // Desert: sparse
    density[3] = 1.2f;  // Savanna: scattered trees
    density[5] = 2.2f;  // Temperate forest
    density[6] = 1.5f;  // Boreal
    density[7] = 2.5f;  // Rainforest
    density[8] = 0.6f;  // Tundra shrubs

    const float areaK = (N*N) / 10000.0f;
    // Very lightweight dart-throwing + min distance per biome
    for (int by=0; by<N; ++by) {
        for (int bx=0; bx<N; ++bx) {
            std::uint8_t biome = B.at(bx,by);
            float want = density[biome] * areaK;
            // Draw a few Bernoulli trials per cell (coarse density control)
            for (int t=0; t<2; ++t) {
                if (ctx.rng.nextFloat01() < (want * 0.02f)) {
                    ObjectInstance o{};
                    o.wx = (bx + ctx.rng.nextFloat01()) * ctx.settings.cellSizeMeters;
                    o.wy = (by + ctx.rng.nextFloat01()) * ctx.settings.cellSizeMeters;
                    o.kind = biome; // placeholder: one mesh per biome for now
                    o.scale = 0.75f + ctx.rng.nextFloat01()*0.75f;
                    o.rot   = ctx.rng.nextFloat01() * 6.2831853f;
                    objs.emplace_back(o);
                }
            }
        }
    }
}

} // namespace colony::worldgen
