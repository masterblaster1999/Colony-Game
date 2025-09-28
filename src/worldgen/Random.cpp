// src/worldgen/Random.cpp
#include "worldgen/Random.hpp"

namespace colony::worldgen {
namespace {

// SplitMix64 step (public-domain style mixer), good for seeding/diffusion.
inline uint64_t splitmix64(uint64_t x) noexcept {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

} // namespace

void Pcg32::seed_rng(uint64_t seed, uint64_t seq) noexcept {
  state = 0u;
  inc   = (seq << 1u) | 1u;    // force odd
  next();                      // transition once
  state += seed;
  next();
}

Pcg32::result_type Pcg32::next() noexcept {
  const uint64_t old = state;
  state = old * 6364136223846793005ULL + (inc | 1u);
  const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
  const uint32_t rot        = static_cast<uint32_t>(old >> 59u);
  return (xorshifted >> rot) | (xorshifted << ((-static_cast<int>(rot)) & 31));
}

uint32_t Pcg32::next_bounded(uint32_t bound) noexcept {
  if (bound == 0u) return 0u;
  const uint64_t threshold = (static_cast<uint64_t>(-bound)) % bound;
  for (;;) {
    uint32_t r = next();
    if (r >= threshold) return r % bound;
  }
}

float Pcg32::next_float01() noexcept {
  // Use high 24 bits for a uniform in [0,1). Avoid 0 and 1 endpoints.
  return (next() >> 8) * (1.0f / 16777216.0f);
}

double Pcg32::next_double01() noexcept {
  // 53-bit mantissa -> [0,1)
  const uint64_t u = (static_cast<uint64_t>(next()) << 32) | next();
  return (u >> 11) * (1.0 / static_cast<double>(UINT64_C(1) << 53));
}

Pcg32 sub_rng(const Pcg32& parent, uint64_t salt) noexcept {
  // Derive a unique seed/seq from parent's internal state and salt.
  const uint64_t s0 = splitmix64(parent.state ^ (parent.inc + 0x9E3779B97F4A7C15ULL));
  const uint64_t s1 = splitmix64(s0 ^ salt);
  Pcg32 out;
  out.seed_rng(s0, s1 | 1u);   // keep seq odd
  return out;
}

} // namespace colony::worldgen
