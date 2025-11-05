#include "procgen/WorldGen.h"
#include "procgen/Noise.h"
#include <cmath>
#include <algorithm>

namespace procgen {

static float ridge(float n, float sharp){ // n ~ [-1,1] -> [0,1] ridged
    float r = 1.0f - std::fabs(n);
    return std::pow(std::max(0.0f, r), sharp);
}

GeneratedWorld WorldGenerator::generate(const WorldGenParams& p) {
    Perlin per(p.seed);
    GeneratedWorld w;
    w.elevation = Heightmap(p.width, p.height, 0.0f);
    w.moisture.assign(p.width*p.height, 0.0f);
    w.temperatureC.assign(p.width*p.height, 0.0f);
    w.biomes.assign(p.width*p.height, (uint8_t)Biome::Ocean);

    // Elevation with domain warp + fBm + ridges
    for (int y=0; y<p.height; ++y){
        for (int x=0; x<p.width; ++x){
            float nx = (float)x, ny = (float)y;

            float base = per.fbm2(nx, ny, p.octaves, p.lacunarity, p.gain, p.baseFreq);
            float warped = per.warped2(nx, ny, p.baseFreq, p.warpAmp, p.warpFreq);
            float mtn = ridge(warped, p.ridgeSharpness);

            float elev = 0.55f*base + 0.45f*(mtn*2.0f - 1.0f); // combine
            w.elevation.at(x,y) = elev;
        }
    }
    w.elevation.normalize();

    // Moisture: independent fBm
    for (int y=0; y<p.height; ++y){
        for (int x=0; x<p.width; ++x){
            float m = 0.5f + 0.5f*per.fbm2((float)x, (float)y, 5, 2.0f, 0.5f, 0.0045f);
            // Oceans bias nearby land to be wetter
            float e = w.elevation.at(x,y);
            if (e < p.seaLevel + 0.02f) m = std::min(1.0f, m + 0.15f);
            w.moisture[y*p.width + x] = m;
        }
    }

    // Temperature: latitude gradient + noise, cooler at higher elevation
    float equator = p.height * 0.5f;
    for (int y=0; y<p.height; ++y){
        float lat = std::abs((y - equator) / (equator)); // 0 at equator, 1 at pole
        for (int x=0; x<p.width; ++x){
            float baseC = 25.0f*(1.0f - lat) + (-5.0f)*lat; // approx range 25C..-5C
            float elev = w.elevation.at(x,y);
            baseC -= 20.0f * std::pow(std::max(0.f, elev - p.seaLevel), 1.2f); // lapse with height
            float n = 1.5f * per.noise(x*0.01f, y*0.01f, 17.0f);
            w.temperatureC[y*p.width + x] = baseC + n;
        }
    }

    // Biome classification
    for (int y=0; y<p.height; ++y){
        for (int x=0; x<p.width; ++x){
            float e = w.elevation.at(x,y);
            float m = w.moisture[y*p.width + x];
            float t = w.temperatureC[y*p.width + x];
            if (e < p.seaLevel) w.biomes[y*p.width + x] = (uint8_t)Biome::Ocean;
            else w.biomes[y*p.width + x] = (uint8_t)classify_biome(e, m, t);
        }
    }
    return w;
}

} // namespace procgen
