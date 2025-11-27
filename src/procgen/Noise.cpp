#include "procgen/Noise.h"
#include <cmath>

namespace procgen {

float PerlinNoise::noise(float x, float y) const {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf);
    float v = fade(yf);

    int A = p[X] + Y;
    int B = p[X + 1] + Y;

    float n00 = grad(p[A], xf,     yf);
    float n10 = grad(p[B], xf - 1, yf);
    float n01 = grad(p[A + 1], xf,     yf - 1);
    float n11 = grad(p[B + 1], xf - 1, yf - 1);

    float x1 = lerp(u, n00, n10);
    float x2 = lerp(u, n01, n11);
    return lerp(v, x1, x2);
}

float PerlinNoise::fbm(float x, float y, int octaves, float lac, float gain) const {
    float amp = 0.5f, freq = 1.0f, sum = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * noise(x * freq, y * freq);
        freq *= lac;
        amp *= gain;
    }
    return sum; // not normalized, approx [-1,1]
}

float PerlinNoise::ridged(float x, float y, int octaves, float lac, float gain) const {
    float sum = 0.f, amp = 0.5f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        float n = noise(x * freq, y * freq);
        n = 1.0f - std::fabs(n); // ridges
        sum += n * amp;
        freq *= lac;
        amp *= gain;
    }
    // map roughly to [0,1]
    return std::max(0.f, std::min(1.f, sum));
}

void PerlinNoise::domainWarp(float& x, float& y, float amplitude, float baseFreq, int octaves) const {
    float wx = fbm(x * baseFreq, y * baseFreq, octaves, 2.0f, 0.5f);
    float wy = fbm((x + 37.1f) * baseFreq, (y - 91.7f) * baseFreq, octaves, 2.0f, 0.5f);
    x += amplitude * wx;
    y += amplitude * wy;
}

} // namespace procgen
