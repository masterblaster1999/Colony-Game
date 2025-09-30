#include "Noise.hpp"
#include <cmath>
#include <algorithm>

namespace pcg {

static inline float fade(float t) { return t*t*t*(t*(t*6 - 15) + 10); }
static inline float lerp(float a, float b, float t) { return a + t * (b - a); }
static inline float grad(int hash, float x, float y, float z) {
    int h = hash & 15; float u = h < 8 ? x : y; float v = h < 4 ? y : (h==12||h==14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

Perlin::Perlin(uint64_t seed) {
    p.resize(512);
    std::vector<int> perm(256);
    for (int i=0;i<256;++i) perm[i] = i;
    Rng rng = Rng::from_seed(seed);
    for (int i=255;i>0;--i) std::swap(perm[i], perm[rng.rangei(0,i)]);
    for (int i=0;i<512;++i) p[i] = perm[i & 255];
}

float Perlin::noise(float x, float y) const { return noise(x, y, 0.0f); }

float Perlin::noise(float x, float y, float z) const {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    float u = fade(x), v = fade(y), w = fade(z);

    int A  = p[X  ] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
    int B  = p[X+1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;

    float res =
      lerp( lerp( lerp( grad(p[AA  ], x  , y  , z  ),
                        grad(p[BA  ], x-1, y  , z  ), u),
                  lerp( grad(p[AB  ], x  , y-1, z  ),
                        grad(p[BB  ], x-1, y-1, z  ), u), v),
            lerp( lerp( grad(p[AA+1], x  , y  , z-1),
                        grad(p[BA+1], x-1, y  , z-1), u),
                  lerp( grad(p[AB+1], x  , y-1, z-1),
                        grad(p[BB+1], x-1, y-1, z-1), u), v), w);
    return res; // in [-1,1]
}

float Perlin::fbm(float x, float y, int octaves, float lac, float gain) const {
    float f = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i=0;i<octaves;++i) { f += amp * noise(x*freq, y*freq); freq*=lac; amp*=gain; }
    return f;
}

float Perlin::fbm(float x, float y, float z, int octaves, float lac, float gain) const {
    float f = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i=0;i<octaves;++i) { f += amp * noise(x*freq, y*freq, z*freq); freq*=lac; amp*=gain; }
    return f;
}

} // namespace pcg
