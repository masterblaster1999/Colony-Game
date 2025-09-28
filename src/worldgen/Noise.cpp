// src/worldgen/Noise.cpp
#include "worldgen/Noise.hpp"
#include <cmath>
#include <cstdint>

namespace colony::worldgen::noise {
namespace {

// Ken Perlin-style fade curve
inline float fade(float t) noexcept { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

// Quintic smoothstep between a and b
inline float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

// 2D integer hash -> [0, 255]
inline uint32_t hash2i(int x, int y, uint32_t seed) noexcept {
  uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u
             ^ static_cast<uint32_t>(y) * 0xd8163841u
             ^ seed * 0xcb1ab31fu;
  // final avalanche (xorshift-like)
  h ^= h >> 16; h *= 0x7feb352du;
  h ^= h >> 15; h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

// Map 8-bit hash to a pseudo-random unit gradient on the circle.
inline void grad2(uint32_t h, float& gx, float& gy) noexcept {
  // 16 hashed directions
  static constexpr float kPi = 3.14159265358979323846f;
  float ang = (h & 15u) * (2.f * kPi / 16.f);
  gx = std::cos(ang);
  gy = std::sin(ang);
}

// Dot product between gradient at corner (ix,iy) and offset (x-ix, y-iy)
inline float dotgrid(int ix, int iy, float x, float y, uint32_t seed) noexcept {
  float gx, gy; grad2(hash2i(ix, iy, seed), gx, gy);
  return gx * (x - ix) + gy * (y - iy);
}

} // namespace

float perlin2D(float x, float y, uint32_t seed) noexcept {
  const int x0 = static_cast<int>(std::floor(x));
  const int x1 = x0 + 1;
  const int y0 = static_cast<int>(std::floor(y));
  const int y1 = y0 + 1;

  const float sx = fade(x - x0);
  const float sy = fade(y - y0);

  const float n00 = dotgrid(x0, y0, x, y, seed);
  const float n10 = dotgrid(x1, y0, x, y, seed);
  const float n01 = dotgrid(x0, y1, x, y, seed);
  const float n11 = dotgrid(x1, y1, x, y, seed);

  const float ix0 = lerp(n00, n10, sx);
  const float ix1 = lerp(n01, n11, sx);
  return lerp(ix0, ix1, sy); // ~[-1,1]
}

float fbm2D(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) noexcept {
  float amp = 0.5f;
  float sum = 0.0f;
  float fx = x, fy = y;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * perlin2D(fx, fy, seed + static_cast<uint32_t>(i * 131));
    fx *= lacunarity;
    fy *= lacunarity;
    amp *= gain;
  }
  return sum; // not normalized intentionally; caller can scale/offset
}

float value2D(float x, float y, uint32_t seed) noexcept {
  const int xi = static_cast<int>(std::floor(x));
  const int yi = static_cast<int>(std::floor(y));
  const float tx = x - xi;
  const float ty = y - yi;
  const float sx = fade(tx);
  const float sy = fade(ty);

  auto v = [&](int px, int py) {
    // map hash to [0,1]
    return (hash2i(px, py, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f);
  };

  const float n00 = v(xi,   yi  );
  const float n10 = v(xi+1, yi  );
  const float n01 = v(xi,   yi+1);
  const float n11 = v(xi+1, yi+1);

  const float ix0 = lerp(n00, n10, sx);
  const float ix1 = lerp(n01, n11, sx);
  return lerp(ix0, ix1, sy);
}

} // namespace colony::worldgen::noise
