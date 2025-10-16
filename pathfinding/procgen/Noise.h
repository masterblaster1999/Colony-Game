// pathfinding/procgen/Noise.h
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace colony::pathfinding::procgen {

struct RNG {
    uint32_t state;
    explicit RNG(uint32_t seed) : state(seed) {}
    inline uint32_t next() {
        // PCG-XSH-RR-ish single-step hash; fast and good enough for procgen
        uint32_t x = state;
        x ^= x >> 17; x *= 0xed5ad4bbU; x ^= x >> 11; x *= 0xac4c1b51U; x ^= x >> 15; x *= 0x31848babU; x ^= x >> 14;
        state = x;
        return x;
    }
};

inline uint32_t hash2(uint32_t seed, int x, int y) {
    uint32_t h = seed;
    h ^= 0x9e3779b9u + static_cast<uint32_t>(x) + (static_cast<uint32_t>(y) << 6) + (static_cast<uint32_t>(y) >> 2);
    // final avalanche
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return h;
}

inline float rand01(uint32_t h) {
    return (h & 0x00FFFFFFu) / 16777215.0f; // [0,1]
}

inline float fade(float t) { return t * t * (3.0f - 2.0f * t); } // smootherstep-ish

// 2D "value noise" (grid of random values, bilerp + smoothstep)
// See: differences vs. Perlin gradient noise. :contentReference[oaicite:4]{index=4}
inline float valueNoise2D(float x, float y, uint32_t seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float xf = x - static_cast<float>(xi);
    float yf = y - static_cast<float>(yi);

    float v00 = rand01(hash2(seed, xi,     yi));
    float v10 = rand01(hash2(seed, xi + 1, yi));
    float v01 = rand01(hash2(seed, xi,     yi + 1));
    float v11 = rand01(hash2(seed, xi + 1, yi + 1));

    float u = fade(xf), v = fade(yf);
    float i1 = v00 + (v10 - v00) * u;
    float i2 = v01 + (v11 - v01) * u;
    return i1 + (i2 - i1) * v; // [0,1]
}

// fBm over value noise (octaves/lacunarity/gain)
inline float fbm2D(float x, float y, uint32_t seed, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) {
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 1013u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / std::max(0.0001f, norm); // [0,1]
}

} // namespace colony::pathfinding::procgen
