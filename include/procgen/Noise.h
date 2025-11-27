#pragma once
#include <vector>
#include <cstdint>
#include <random>
#include <algorithm>
#include <cmath>

namespace procgen {

// Classic Perlin (improved variant-inspired), 2D only here.
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 0) {
        p.resize(256);
        for (int i = 0; i < 256; ++i) p[i] = i;
        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);
        p.insert(p.end(), p.begin(), p.end()); // 512
    }

    // Single octave in [-1,1]
    float noise(float x, float y) const;

    // Fractal Brownian motion (fBM) in [-1,1]
    float fbm(float x, float y, int octaves, float lacunarity, float gain) const;

    // Ridged multifractal in [0,1]
    float ridged(float x, float y, int octaves, float lacunarity, float gain) const;

    // Domain warp: modifies x,y in-place using low-octave fbm fields.
    void domainWarp(float& x, float& y, float amplitude, float baseFreq, int octaves) const;

private:
    std::vector<int> p;

    static inline float fade(float t) { return t*t*t*(t*(t*6 - 15) + 10); }
    static inline float lerp(float t, float a, float b) { return a + t*(b - a); }
    static inline float grad(int h, float x, float y) {
        // 12 gradient directions
        const int hh = h & 7;
        const float u = (hh < 4) ? x : y;
        const float v = (hh < 4) ? y : x;
        return ((hh & 1) ? -u : u) + ((hh & 2) ? -2.f*v : 2.f*v) * 0.5f;
    }
};

} // namespace procgen
