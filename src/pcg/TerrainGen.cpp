#include "TerrainGen.hpp"
#include "Rivers.hpp"
#include <cmath>
#include <algorithm>

namespace pcg {

static inline int idx(int x, int y, int N) { return y*N + x; }
static inline float clamp01(float v){ return std::max(0.0f, std::min(1.0f, v)); }

TerrainChunk generate_terrain(uint64_t worldSeed, ChunkCoord cc, float cellSize, const TerrainParams& tp) {
    TerrainChunk out{};
    out.size = tp.size;
    out.cellSize = cellSize;
    int N = tp.size;

    out.height.resize(N*N);
    out.temp.resize(N*N);
    out.moisture.resize(N*N);
    out.flow.assign(N*N, 0.0f);
    out.rivers.assign(N*N, 0u);
    out.biomes.resize(N*N);

    // Seeded noises
    Perlin n_elev(hash_ns(worldSeed, cc.cx, cc.cy, "elev"));
    Perlin n_cont(hash_ns(worldSeed, cc.cx, cc.cy, "continent"));
    Perlin n_temp(hash_ns(worldSeed, cc.cx, cc.cy, "temp"));
    Perlin n_moist(hash_ns(worldSeed, cc.cx, cc.cy, "moist"));

    // Coordinates in world meters (or your unit)
    const float baseX = cc.cx * (N * cellSize);
    const float baseY = cc.cy * (N * cellSize);

    // 1) Heightfield
    for (int y=0; y<N; ++y) {
        for (int x=0; x<N; ++x) {
            float wx = baseX + x * cellSize;
            float wy = baseY + y * cellSize;

            float c = n_cont.fbm(wx*tp.continentFreq, wy*tp.continentFreq, 4, 2.0f, 0.5f);
            c = (c*0.5f + 0.5f); // 0..1 continents
            float elev = n_elev.fbm(wx*tp.scale, wy*tp.scale, tp.octaves, tp.lacunarity, tp.gain);
            // ridge: turn abs noise into ridges
            float ridge = 1.0f - std::abs(elev);
            elev = (1.0f - tp.ridgeWeight) * elev + tp.ridgeWeight * ridge;

            float h = tp.baseHeight + tp.elevationAmp * (c * elev);
            out.height[idx(x,y,N)] = h;
        }
    }

    // 2) Climate fields (very lightweight model)
    // Temperature decreases with elevation; add latitudinal gradient via world Y
    float worldY = (cc.cy * N + N*0.5f) * cellSize; // crude proxy; replace w/ real latitude if you have it
    for (int y=0; y<N; ++y) {
        for (int x=0; x<N; ++x) {
            int i = idx(x,y,N);
            float h = out.height[i];
            float tNoise = n_temp.fbm((baseX+x*cellSize)*tp.scale*0.7f, (baseY+y*cellSize)*tp.scale*0.7f, 4, 2.05f, 0.5f);
            float mNoise = n_moist.fbm((baseX+x*cellSize)*tp.scale*0.9f, (baseY+y*cellSize)*tp.scale*0.9f, 4, 1.95f, 0.5f);

            float t = 0.7f - tp.tempLapseRate * (h) + 0.1f * tNoise - 0.00000005f * worldY; // tweak
            float m = 0.5f + 0.3f * mNoise + tp.moistureBias;

            out.temp[i] = clamp01(t*0.5f + 0.5f);
            out.moisture[i] = clamp01(m*0.5f + 0.5f);
        }
    }

    // 3) Flow & rivers
    compute_flow_accumulation(out.height, N, N, out.flow);
    carve_rivers(out.height, out.flow, N, N, cellSize, /*flowThresh*/ 120.0f, out.rivers);

    // 4) Biomes
    BiomeParams bp{};
    for (int i=0;i<N*N;++i) {
        out.biomes[i] = classify_biome(out.temp[i], out.moisture[i], bp);
    }
    return out;
}

} // namespace pcg
