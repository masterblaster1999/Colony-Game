// src/worldgen/Noise.hpp
#pragma once
#include <cstdint>

namespace colony::worldgen::noise {

// Lightweight gradient/value-style noise for CPU worldgen.
// These return values in roughly [-1, 1] unless noted.

float perlin2D(float x, float y, uint32_t seed) noexcept;
float fbm2D(float x, float y, uint32_t seed,
            int octaves, float lacunarity = 2.0f, float gain = 0.5f) noexcept;

// Stable hash-based "value noise" if you prefer sharper features.
// Returns in [0, 1].
float value2D(float x, float y, uint32_t seed) noexcept;

} // namespace colony::worldgen::noise
