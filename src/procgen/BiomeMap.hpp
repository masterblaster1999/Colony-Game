// src/procgen/BiomeMap.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <random>
#include <limits>

namespace colony::procgen {

// ----------------------------- noise utils (header-only) -----------------------------
struct RNG {
    uint64_t s;
    explicit RNG(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    // SplitMix64
    uint64_t next() { uint64_t z = (s += 0x9e3779b97f4a7c15ULL); z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL; z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL; return z ^ (z >> 31); }
    double uniform01() { return (next() >> 11) * (1.0/9007199254740992.0); } // [0,1)
};
static inline uint32_t hash2i(int x, int y, uint32_t seed) {
    uint32_t h = seed;
    h ^= 0x9e3779b9u + (uint32_t)x + (h<<6) + (h>>2);
    h ^= 0x7f4a7c15u + (uint32_t)y + (h<<6) + (h>>2);
    h ^= (h<<13); h ^= (h>>17); h ^= (h<<5);
    return h;
}
static inline float fade(float t) { // quintic
    return t*t*t*(t*(t*6 - 15) + 10);
}
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// coherent value noise on integer lattice
static inline float value2D(float x, float y, uint32_t seed=1337) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float tx = x - xi, ty = y - yi;
    auto v = [&](int X,int Y){ return (hash2i(X,Y,seed) & 0xffffff) * (1.f/16777215.f); };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    float sx = fade(tx), sy = fade(ty);
    float ix0 = lerp(v00, v10, sx);
    float ix1 = lerp(v01, v11, sx);
    return lerp(ix0, ix1, sy); // [0,1]
}
static inline float fbm2D(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed) {
    float amp = 0.5f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i=0;i<octaves;i++) {
        sum += amp * (value2D(x*freq, y*freq, seed+i*10103u)*2.f - 1.f);
        norm += amp;
        amp *= gain; freq *= lacunarity;
    }
    return sum / std::max(1e-6f, norm); // ~[-1,1]
}
// domain warping: warp coord by a secondary fbm
static inline void warp2D(float& x, float& y, float scale, float strength, uint32_t seed) {
    float wx = fbm2D(x*scale + 17.0f, y*scale + 17.0f, 3, 2.0f, 0.5f, seed ^ 0x6b5f);
    float wy = fbm2D(x*scale + 51.0f, y*scale + 51.0f, 3, 2.0f, 0.5f, seed ^ 0x93a1);
    x += strength * wx;
    y += strength * wy;
}

// ----------------------------- biome generation -----------------------------
enum class Biome : uint8_t {
    Ocean, Beach, Grassland, Forest, Desert, Savanna, Taiga, Tundra, Swamp, Mountain, Snow
};

struct BiomeParams {
    float heightScale   = 0.004f; // base height noise frequency
    float tempScale     = 0.0018f;
    float moistScale    = 0.0018f;
    float warpScale     = 0.005f; // domain-warp scale
    float warpStrength  = 12.0f;  // pixels to warp cooordinates
    float seaLevel      = 0.0f;   // height threshold for sea (normalized h)
    float beachBand     = 0.03f;  // near the sea -> beach
    int   octaves       = 5;
    float lacunarity    = 2.0f;
    float gain          = 0.5f;
    float tempBiasC     = 12.0f;  // base climate (Celsius) before lat modulation
    float tempRangeC    = 18.0f;  // seasonal/lat spread
};

// Compute normalized fields: height in [-1,1], temp/moist in [0,1]
struct BiomeFields {
    std::vector<float> height, temp, moist;
};

struct BiomeMap {
    int width=0, height=0;
    std::vector<Biome> cells; // size = w*h
    Biome& at(int x,int y){ return cells[y*width+x]; }
    const Biome& at(int x,int y) const { return cells[y*width+x]; }
};

static inline Biome pick_biome(float h, float t01, float m01, const BiomeParams& P) {
    if (h < P.seaLevel) return Biome::Ocean;
    if (h < P.seaLevel + P.beachBand) return Biome::Beach;
    // mountains / snow by elevation
    if (h > 0.65f) return (t01 < 0.35f ? Biome::Snow : Biome::Mountain);

    // Whittaker-like classification on T (cold->hot) × M (dry->wet)
    if (t01 < 0.25f) {
        if (m01 < 0.4f) return Biome::Tundra;
        else return Biome::Taiga;
    } else if (t01 < 0.6f) {
        if (m01 < 0.35f) return Biome::Grassland;
        else if (m01 < 0.7f) return Biome::Forest;
        else return Biome::Swamp;
    } else {
        if (m01 < 0.3f) return Biome::Desert;
        else if (m01 < 0.6f) return Biome::Savanna;
        else return Biome::Forest;
    }
}

static inline BiomeFields generate_fields(int W, int H, uint64_t seed, const BiomeParams& P) {
    BiomeFields F{std::vector<float>(W*H), std::vector<float>(W*H), std::vector<float>(W*H)};
    const uint32_t s32 = (uint32_t)(seed ^ (seed>>32));
    for (int y=0;y<H;y++){
        for (int x=0;x<W;x++){
            float X=(float)x, Y=(float)y;
            float hx=X, hy=Y; warp2D(hx,hy,P.warpScale,P.warpStrength,s32^0x11);
            float tx=X, ty=Y; warp2D(tx,ty,P.warpScale*0.65f,P.warpStrength*0.5f,s32^0x22);
            float mx=X, my=Y; warp2D(mx,my,P.warpScale*0.8f,P.warpStrength*0.5f,s32^0x33);

            float h = fbm2D(hx*P.heightScale, hy*P.heightScale, P.octaves,P.lacunarity,P.gain,s32^0xA1);
            float t = value2D(tx*P.tempScale, ty*P.tempScale, s32^0xB2); // 0..1
            float m = value2D(mx*P.moistScale, my*P.moistScale, s32^0xC3); // 0..1

            F.height[y*W+x] = h;
            // crude latitude gradient (cooler toward top/bottom)
            float lat = 1.0f - 2.0f*std::abs((y+0.5f)/H - 0.5f); // 0 at poles, 1 at equator
            float tempC = P.tempBiasC + P.tempRangeC*(lat - 0.5f) + (t-0.5f)*10.0f + h*4.0f;
            float t01 = std::clamp((tempC + 20.0f)/50.0f, 0.0f, 1.0f);
            F.temp[y*W+x] = t01;
            F.moist[y*W+x] = m;
        }
    }
    return F;
}

static inline BiomeMap generate_biomes(int W, int H, uint64_t seed, const BiomeParams& P=BiomeParams{}) {
    BiomeMap M{W,H,std::vector<Biome>(W*H)};
    const auto F = generate_fields(W,H,seed,P);
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        float h = F.height[y*W+x];
        Biome b = pick_biome(h, F.temp[y*W+x], F.moist[y*W+x], P);
        M.at(x,y) = b;
    }
    return M;
}

// Debug color (RGBA8) for simple visualization
static inline std::array<uint8_t,4> biome_color(Biome b) {
    switch(b){
        case Biome::Ocean:     return {  8, 64,160,255};
        case Biome::Beach:     return {238,221,170,255};
        case Biome::Grassland: return { 80,170, 80,255};
        case Biome::Forest:    return { 34,139, 34,255};
        case Biome::Desert:    return {224,200,120,255};
        case Biome::Savanna:   return {189,183,107,255};
        case Biome::Taiga:     return { 46,139, 87,255};
        case Biome::Tundra:    return {176,196,222,255};
        case Biome::Swamp:     return { 47, 79, 47,255};
        case Biome::Mountain:  return {130,130,130,255};
        case Biome::Snow:      return {245,245,245,255};
        default:               return {255,  0,255,255};
    }
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// quick usage:
// auto map = colony::procgen::generate_biomes(512,512,12345);
// draw pixels using biome_color(map.cells[i]);
#endif
