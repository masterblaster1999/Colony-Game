#include "Biomes.hpp"
#include <algorithm>
#include <cmath>

namespace cg {

static inline int I(int x,int y,int W){ return y*W + x; }
template <class T> static inline T clampv(T v, T lo, T hi){ return v<lo?lo:(v>hi?hi:v); }

const char* biomeName(BiomeId id){
    switch(id){
    case BiomeId::Ocean: return "Ocean";
    case BiomeId::Lake: return "Lake";
    case BiomeId::Desert: return "Desert";
    case BiomeId::Shrubland: return "Shrubland";
    case BiomeId::Savanna: return "Savanna";
    case BiomeId::Grassland: return "Grassland";
    case BiomeId::TemperateForest: return "TemperateForest";
    case BiomeId::BorealForest: return "BorealForest";
    case BiomeId::Tundra: return "Tundra";
    case BiomeId::TropicalRainforest: return "TropicalRainforest";
    case BiomeId::Alpine: return "Alpine";
    default: return "Unknown";
    }
}

// ---- Whittaker-like classification (coarse, tunable) ----
BiomeId classifyBiome(float tC, float pMM, float z,
                      float seaLevel, const BiomeParams& B)
{
    // Alpine overrides at high elevation
    if (z - seaLevel >= B.alpineHeight) return BiomeId::Alpine;

    // Temperature bands
    bool cold = (tC <  B.t_cold);
    bool cool = (tC >= B.t_cold && tC <  B.t_cool);
    bool warm = (tC >= B.t_cool && tC <  B.t_warm);
    [[maybe_unused]] const bool hot  = (tC >= B.t_warm);

    // Precip bands
    bool veryDry = (pMM <  B.p_dry);
    bool semiarid= (pMM >= B.p_dry     && pMM <  B.p_semiarid);
    bool subhumid= (pMM >= B.p_semiarid&& pMM <  B.p_subhumid);
    bool humid   = (pMM >= B.p_subhumid&& pMM <  B.p_humid);
    bool perHumid= (pMM >= B.p_humid);

    if (cold) {
        if (veryDry || semiarid) return BiomeId::Tundra;
        return BiomeId::BorealForest;
    }
    if (cool) {
        if (veryDry) return BiomeId::Shrubland;
        if (semiarid) return BiomeId::Grassland;
        if (subhumid || humid) return BiomeId::TemperateForest;
        return BiomeId::TemperateForest;
    }
    if (warm) {
        if (veryDry) return BiomeId::Desert;
        if (semiarid) return BiomeId::Savanna;
        if (subhumid) return BiomeId::Grassland;
        if (humid || perHumid) return BiomeId::TemperateForest;
    }
    // hot
    if (veryDry) return BiomeId::Desert;
    if (semiarid) return BiomeId::Savanna;
    if (perHumid) return BiomeId::TropicalRainforest;
    return BiomeId::Grassland;
}

// Helper: normalize 4 weights so they sum to 1 (and clamp)
static inline void normalize4(float& r,float& g,float& b,float& a){
    r=clampv(r,0.f,1.f); g=clampv(g,0.f,1.f); b=clampv(b,0.f,1.f); a=clampv(a,0.f,1.f);
    float s = r+g+b+a; if (s>1e-6f){ r/=s; g/=s; b/=s; a/=s; }
}

BiomeOutputs buildBiomes(const HeightField& temperatureC,
                         const HeightField& precipUnits,
                         const HeightField& heightZ,
                         float seaLevel,
                         float xyScale, float zScale,
                         const BiomeParams& B)
{
    const int W=heightZ.w, H=heightZ.h;
    BiomeOutputs out;
    out.w=W; out.h=H;
    out.biomeId.assign(size_t(W)*H, uint8_t(BiomeId::Grassland));
    out.maskGrass = HeightField(W,H);
    out.maskRock  = HeightField(W,H);
    out.maskSand  = HeightField(W,H);
    out.maskSnow  = HeightField(W,H);

    // Slope (radians) from your DomainWarp utility; convert to degrees
    std::vector<float> slopeMap = computeSlopeMap(heightZ, xyScale, zScale);
    auto slopeDeg = [&](int x,int y){
        float s = slopeMap[I(x,y,W)];
        return s * 57.2957795f; // rad -> deg
    };

    // Pass 1: classify biomes
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            float z  = heightZ.at(x,y);
            if (z <= seaLevel) {
                out.biomeId[I(x,y,W)] = uint8_t(BiomeId::Ocean);
                continue;
            }
            float tC = temperatureC.at(x,y);
            float pU = precipUnits.at(x,y);
            float pMM = pU * B.precipScaleToMm;
            BiomeId b = classifyBiome(tC, pMM, z, seaLevel, B);
            out.biomeId[I(x,y,W)] = uint8_t(b);
        }
    }

    // Pass 2: build terrain splat masks (R=rock, G=grass, B=sand, A=snow)
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            float z   = heightZ.at(x,y);
            uint8_t id = out.biomeId[I(x,y,W)];
            if (id == uint8_t(BiomeId::Ocean)) {
                out.maskRock.at(x,y)=0; out.maskGrass.at(x,y)=0; out.maskSand.at(x,y)=0; out.maskSnow.at(x,y)=0;
                continue;
            }

            float tC  = temperatureC.at(x,y);
            float pMM = precipUnits.at(x,y) * B.precipScaleToMm;
            float sd  = slopeDeg(x,y);

            // Base weights from biome
            float wRock=0, wGrass=0, wSand=0, wSnow=0;

            switch (BiomeId(id)) {
            case BiomeId::Desert:             wSand=0.7f; wGrass=0.2f; wRock=0.1f; break;
            case BiomeId::Shrubland:          wGrass=0.5f; wSand=0.3f; wRock=0.2f; break;
            case BiomeId::Savanna:            wGrass=0.6f; wSand=0.2f; wRock=0.2f; break;
            case BiomeId::Grassland:          wGrass=0.75f; wRock=0.15f; wSand=0.10f; break;
            case BiomeId::TemperateForest:    wGrass=0.7f; wRock=0.2f;  wSand=0.1f;  break;
            case BiomeId::BorealForest:       wGrass=0.55f;wRock=0.35f; wSand=0.1f;  break;
            case BiomeId::Tundra:             wGrass=0.35f;wRock=0.45f; wSand=0.2f;  break;
            case BiomeId::TropicalRainforest: wGrass=0.8f; wRock=0.15f; wSand=0.05f; break;
            case BiomeId::Alpine:             wRock=0.6f; wGrass=0.2f;  wSand=0.2f;  break;
            default:                           wGrass=0.6f; wRock=0.3f;  wSand=0.1f;  break;
            }

            // Rock from slope
            float rockSlope = clampv((sd - B.rockSlopeStartDeg) / (B.rockSlopeFullDeg - B.rockSlopeStartDeg), 0.f, 1.f);
            wRock = clampv(wRock + 0.5f*rockSlope, 0.f, 1.f);
            wGrass *= (1.0f - 0.4f*rockSlope);

            // Sand near sea level (beach) or very low precip
            float beach = 1.0f - clampv(std::abs(z - seaLevel)/B.beachHeightRange, 0.f, 1.f);
            float aridity = clampv((B.p_dry - pMM) / B.p_dry, 0.f, 1.f);
            wSand = clampv(wSand + 0.6f*beach + 0.4f*aridity, 0.f, 1.f);
            wGrass *= (1.0f - 0.3f*beach);

            // Snow from cold temps and high elevation
            float tempSnow = clampv((B.snowTempC - tC) / 10.0f, 0.f, 1.f);
            float elevSnow = (z - (seaLevel + B.snowHeightBonus)) > 0 ? 0.5f : 0.0f;
            wSnow = clampv(wSnow + tempSnow + elevSnow, 0.f, 1.f);
            // Reduce grass/sand under heavy snow
            float snowMask = wSnow;
            wGrass *= (1.0f - 0.7f*snowMask);
            wSand  *= (1.0f - 0.7f*snowMask);

            // Normalize
            normalize4(wRock, wGrass, wSand, wSnow);

            out.maskRock.at(x,y)  = wRock;
            out.maskGrass.at(x,y) = wGrass;
            out.maskSand.at(x,y)  = wSand;
            out.maskSnow.at(x,y)  = wSnow;
        }
    }

    return out;
}

} // namespace cg
