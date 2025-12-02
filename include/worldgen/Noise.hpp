#pragma once
#include <cmath>
#include <cstdint>

namespace colony::worldgen {

// Simple 2D fBm scaffolding; replace body with your current noise calls
inline float fbm2D(float x, float y,
                   int octaves = 5, float lacunarity = 2.0f, float gain = 0.5f) noexcept
{
    float amp = 1.0f, freq = 1.0f, sum = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        // sum += perlin2D(x*freq, y*freq) * amp;  // use your noise
        amp *= gain;
        freq *= lacunarity;
    }
    return sum;
}

} // namespace colony::worldgen
