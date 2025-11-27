#include "procgen/WorldGen.h"
#include "procgen/Noise.h"
#include "procgen/Poisson.h"
#include "procgen/Erosion.h"
#include <random>
#include <algorithm>
#include <cmath>

namespace procgen {

static inline int id(int x,int y,int w){ return y*w + x; }
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

static float falloff_island(float nx, float ny) {
    // Circular island falloff (0 at edges, 1 in center)
    float d = std::sqrt(nx*nx + ny*ny);
    float a = 1.0f - clamp01(d);
    // Sharpen edge
    return a*a*(3.f - 2.f*a);
}

static Biome pickBiome(float h, float m, float t, float sea, float beach) {
    if (h < sea) return Biome::Ocean;
    if (h < sea + beach) return Biome::Beach;

    // High mountains / snow
    if (h > 0.83f) return (t < 0.4f) ? Biome::Snow : Biome::Mountain;

    // temperature bands: cold/temperate/hot
    const bool cold = t < 0.33f;
    const bool hot  = t > 0.66f;

    if (cold) {
        if (m < 0.33f) return Biome::Tundra;
        if (m > 0.66f) return Biome::Taiga; // wetter/cooler -> taiga
        return Biome::Taiga;
    } else if (hot) {
        if (m < 0.33f) return Biome::Desert;
        if (m > 0.66f) return Biome::Rainforest;
        return Biome::Savanna;
    } else {
        if (m < 0.33f) return Biome::Grassland;
        if (m > 0.66f) return Biome::Forest;
        return Biome::Grassland;
    }
}

static Color biomeColor(Biome b){
    switch (b){
        case Biome::Ocean:      return {  0, 70,140,255};
        case Biome::Beach:      return {240,220,170,255};
        case Biome::Desert:     return {210,180, 80,255};
        case Biome::Grassland:  return { 80,180, 60,255};
        case Biome::Forest:     return { 30,120, 40,255};
        case Biome::Rainforest: return { 20,140, 60,255};
        case Biome::Savanna:    return {160,180, 60,255};
        case Biome::Taiga:      return { 50,120,100,255};
        case Biome::Tundra:     return {150,160,150,255};
        case Biome::Snow:       return {240,240,250,255};
        case Biome::Mountain:   return {130,130,130,255};
        default: return {255,  0,255,255};
    }
}

WorldData generateWorld(const WorldParams& params) {
    WorldData out;
    out.w = params.width;
    out.h = params.height;
    out.height.resize((size_t)out.w * out.h, 0.f);
    out.moisture.resize(out.height.size(), 0.f);
    out.temperature.resize(out.height.size(), 0.f);
    out.biome.resize(out.height.size(), Biome::Ocean);

    std::mt19937 rng(params.seed);
    PerlinNoise nHeight(params.seed * 2654435761u + 1u);
    PerlinNoise nWarp  (params.seed * 2654435761u + 2u);
    PerlinNoise nMoist (params.seed * 2654435761u + 3u);
    PerlinNoise nTemp  (params.seed * 2654435761u + 4u);

    // --- Height field: fBM + ridges + domain warp
    for (int y=0;y<out.h;++y){
        for (int x=0;x<out.w;++x){
            float fx = (float)x;
            float fy = (float)y;

            float wx = fx, wy = fy;
            if (params.worldWarp > 0.0f) {
                wx = fx; wy = fy;
                nWarp.domainWarp(wx, wy, params.worldWarp, params.freq * 0.5f, params.warpOctaves);
            }

            float f = nHeight.fbm(wx * params.freq, wy * params.freq,
                                  params.octaves, params.lacunarity, params.gain);
            float r = nHeight.ridged(wx * params.freq * 0.5f, wy * params.freq * 0.5f,
                                     4, 2.0f, 0.5f);
            float h = 0.6f*f + 0.4f*(r - 0.3f); // blend

            // Normalize to [0,1] approx
            h = (h * 0.5f) + 0.5f;

            // optional island falloff
            if (params.archipelago) {
                float nx = (x / (float)out.w) * 2.f - 1.f;
                float ny = (y / (float)out.h) * 2.f - 1.f;
                float fall = falloff_island(nx, ny);
                h *= (0.35f + 0.65f * fall); // retain some land
            }

            out.height[id(x,y,out.w)] = clamp01(h);
        }
    }

    // Erosion for rivers/valleys
    if (params.applyErosion) {
        ErosionParams ep;
        ep.dropletCount = params.erosionDroplets;
        ep.maxSteps = params.erosionMaxSteps;
        applyHydraulicErosion(out.height, out.w, out.h, params.seed ^ 0x9e3779b9u, ep);
    }

    // Moisture and temperature
    for (int y=0;y<out.h;++y){
        float lat = std::abs((y / (float)(out.h-1)) * 2.f - 1.f); // 0 equator, 1 poles
        for (int x=0;x<out.w;++x){
            int i = id(x,y,out.w);
            // Moisture: noise + distance-from-ocean heuristic
            float m = nMoist.fbm(x * params.moistureFreq, y * params.moistureFreq, 5, 2.f, 0.55f);
            m = (m * 0.5f) + 0.5f;
            // Simple coastal moistening
            float h = out.height[i];
            if (h < params.seaLevel + params.beachWidth) m = std::min(1.f, m + 0.15f);
            out.moisture[i] = clamp01(m);

            // Temperature: hot at equator, cool at poles and high altitudes
            float t = 1.f - lat; // equator 1, poles 0
            t = t * (1.0f - params.tempPolarBias) + (1.0f - lat) * params.tempPolarBias;
            t += 0.1f * nTemp.noise(x * 0.01f, y * 0.01f);
            // lapse rate with altitude
            t -= params.lapseRate * std::max(0.f, out.height[i] - params.seaLevel);
            out.temperature[i] = clamp01((t * 0.5f) + 0.5f);
        }
    }

    // Biomes
    for (int y=0;y<out.h;++y){
        for (int x=0;x<out.w;++x){
            int i = id(x,y,out.w);
            out.biome[i] = pickBiome(out.height[i], out.moisture[i], out.temperature[i],
                                     params.seaLevel, params.beachWidth);
        }
    }

    // Resources (blue-noise scatter per-biome)
    if (params.placeResources) {
        auto place = [&](Biome b, float radius, ResourceType t){
            // Rough density via radius; sample over entire map, then filter by biome.
            auto pts = poissonDisk(out.w, out.h, radius, params.seed ^ (uint32_t)b);
            for (auto& p : pts) {
                int x = (int)p.x, y = (int)p.y;
                int i = id(x,y,out.w);
                if (out.biome[i] == b) out.resources.push_back({t, x, y});
            }
        };
        // Forest / Rainforest trees
        place(Biome::Forest,     6.0f, ResourceType::Tree);
        place(Biome::Rainforest, 3.5f, ResourceType::Tree);
        // Grassland berries + animals
        place(Biome::Grassland, 12.0f, ResourceType::BerryBush);
        place(Biome::Grassland, 20.0f, ResourceType::Animal);
        // Savanna animals
        place(Biome::Savanna,   18.0f, ResourceType::Animal);
        // Mountains/desert stones & ores
        place(Biome::Mountain,  12.0f, ResourceType::Stone);
        place(Biome::Mountain,  28.0f, ResourceType::OreIron);
        place(Biome::Mountain,  28.0f, ResourceType::OreCopper);
        place(Biome::Desert,    22.0f, ResourceType::Stone);
        // Taiga/taiga trees lighter density
        place(Biome::Taiga,      9.0f, ResourceType::Tree);
    }

    return out;
}

std::vector<uint8_t> makeBiomePreviewRGBA(const WorldData& w){
    std::vector<uint8_t> img((size_t)w.w*w.h*4);
    for (int y=0;y<w.h;++y){
        for (int x=0;x<w.w;++x){
            int i = id(x,y,w.w);
            Color c = biomeColor(w.biome[i]);
            img[i*4+0]=c.r; img[i*4+1]=c.g; img[i*4+2]=c.b; img[i*4+3]=c.a;
        }
    }
    return img;
}

} // namespace procgen
