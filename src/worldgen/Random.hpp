// src/worldgen/Random.hpp
#pragma once
#include <cstdint>
#include <limits>

namespace colony::worldgen {

// Minimal PCG32 RNG (O'Neill). 32-bit outputs, 64-bit state/stream.
// Good statistical quality and fast for gameplay use.
struct Pcg32 {
  using result_type = uint32_t;

  uint64_t state = 0x853c49e6748fea9bULL;
  uint64_t inc   = 0xda3e39cb94b95bdbULL; // must be odd

  Pcg32() = default;
  explicit Pcg32(uint64_t seed, uint64_t seq = 1u) noexcept { seed_rng(seed, seq); }

  static constexpr result_type min() noexcept { return 0u; }
  static constexpr result_type max() noexcept { return 0xFFFFFFFFu; }

  void seed_rng(uint64_t seed, uint64_t seq = 1u) noexcept;

  // Advance and produce next 32-bit value.
  result_type next() noexcept;
  result_type operator()() noexcept { return next(); }

  // Uniform in [0, bound) without modulo bias (like PCG reference).
  uint32_t next_bounded(uint32_t bound) noexcept;

  // (0,1) single-precision and double-precision
  float  next_float01() noexcept;
  double next_double01() noexcept;
};

// Convenience helpers
inline float randf(Pcg32& rng, float lo, float hi) noexcept {
  return lo + (hi - lo) * rng.next_float01();
}
inline int randi(Pcg32& rng, int lo, int hi) noexcept {
  if (hi <= lo) return lo;
  const uint32_t span = static_cast<uint32_t>(hi - lo + 1);
  return lo + static_cast<int>(rng.next_bounded(span));
}

// Derive a deterministic sub-RNG from a parent stream + salt.
// Useful to shard randomness per-task/tile without cross-correlation.
Pcg32 sub_rng(const Pcg32& parent, uint64_t salt) noexcept;

// Integer overloads to avoid ad-hoc casts at call sites.
inline Pcg32 sub_rng(const Pcg32& parent, int a, int b) noexcept {
  return sub_rng(parent, (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
                           static_cast<uint32_t>(b));
}
inline Pcg32 sub_rng(const Pcg32& parent, int a, int b, int c) noexcept {
  uint64_t hi = (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
                 static_cast<uint32_t>(b);
  uint64_t lo = static_cast<uint32_t>(c);
  // Mix the low word with a Weyl constant to scramble bits.
  return sub_rng(parent, hi ^ (lo * 0x9E3779B97F4A7C15ULL));
}

} // namespace colony::worldgen
