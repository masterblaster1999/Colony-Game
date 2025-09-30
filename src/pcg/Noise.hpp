#pragma once
#include <cstdint>
#include <vector>
#include "SeededRng.hpp"

namespace pcg {

struct Perlin {
    std::vector<int> p; // 512
    explicit Perlin(uint64_t seed = 0);

    // 2D/3D Perlin in [-1,1]
    float noise(float x, float y) const;
    float noise(float x, float y, float z) const;

    // fractal Brownian motion
    float fbm(float x, float y, int octaves, float lacunarity, float gain) const;
    float fbm(float x, float y, float z, int octaves, float lacunarity, float gain) const;
};

} // namespace pcg
