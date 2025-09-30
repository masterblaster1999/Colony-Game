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

  // Initialize the generator. `seq` selects the stream (any value is fine).
  // Internally we force `inc = (seq << 1) | 1` so it is odd as required by PCG.
  inline void seed_rng(uint64_t seed, uint64_t seq = 1u) noexcept {
    state = 0u;
    inc   = (seq << 1u) | 1u;
    next();            // scramble with stream
    state += seed;     // add seed
    next();            // advance once more
  }

  // Advance and produce next 32-bit value (canonical pcg32).
  inline result_type next() noexcept {
    const uint64_t old = state;
    // LCG step
    state = old * 6364136223846793005ULL + inc;

    // Output function (xorshift; rotr)
    const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot        = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-static_cast<int32_t>(rot)) & 31));
  }

  inline result_type operator()() noexcept { return next(); }

  // Uniform in [0, bound) without modulo bias (like PCG reference).
  inline uint32_t next_bounded(uint32_t bound) noexcept {
    if (bound == 0u) return 0u;
    uint64_t m = static_cast<uint64_t>(next()) * static_cast<uint64_t>(bound);
    uint32_t l = static_cast<uint32_t>(m);
    const uint32_t thresh = static_cast<uint32_t>(-bound) % bound;
    if (l < thresh) {
      do {
        m = static_cast<uint64_t>(next()) * static_cast<uint64_t>(bound);
        l = static_cast<uint32_t>(m);
      } while (l < thresh);
    }
    return static_cast<uint32_t>(m >> 32);
  }

  // (0,1) single-precision and double-precision (actually [0,1))
  inline float next_float01() noexcept {
    // Use the top 24 bits to match float mantissa precision.
    const uint32_t v = next() >> 8;                 // 24 random bits
    // 2^-24 = 1 / 16,777,216
    constexpr double INV_2_24 = 1.0 / 16777216.0;
    return static_cast<float>(static_cast<double>(v) * INV_2_24);
  }

  inline double next_double01() noexcept {
    // 53 bits for full double mantissa precision.
    const uint64_t v = (static_cast<uint64_t>(next()) << 32) | next();
    constexpr double INV_2_53 = 1.0 / 9007199254740992.0; // 2^-53
    return static_cast<double>( (v >> 11) ) * INV_2_53;
  }

  // ---------- Backward-compatibility aliases (keep existing callers happy) ----------
  inline uint32_t nextUInt() noexcept { return next(); }
  inline uint32_t nextBounded(uint32_t bound) noexcept { return next_bounded(bound); }
  inline float    nextFloat01() noexcept { return next_float01(); }
  inline double   nextDouble01() noexcept { return next_double01(); }
};

// Convenience helpers
inline float randf(Pcg32& rng, float lo, float hi) noexcept {
  return lo + (hi - lo) * rng.next_float01();
}
inline int randi(Pcg32& rng, int lo, int hi) noexcept {
  if (hi <= lo) return lo;
  const uint32_t span = static_cast<uint32_t>(static_cast<uint64_t>(hi - lo) + 1ull);
  return lo + static_cast<int>(rng.next_bounded(span));
}

// SplitMix64 scrambler (good bit-mixer for seeds)
static inline uint64_t splitmix64(uint64_t x) noexcept {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

// Derive a deterministic sub-RNG from a parent stream + salt.
// Useful to shard randomness per-task/tile without cross-correlation.
inline Pcg32 sub_rng(const Pcg32& parent, uint64_t salt) noexcept {
  // Mix both the parent's state and stream with the salt to form new seed/seq.
  const uint64_t seed = splitmix64(parent.state ^ (salt + 0x9E3779B97F4A7C15ULL));
  const uint64_t seq  = splitmix64(parent.inc   ^ (salt ^ 0xBF58476D1CE4E5B9ULL));
  Pcg32 child;
  child.seed_rng(seed, seq); // seq may be any value; seed_rng makes inc odd.
  return child;
}

// Integer overloads to avoid ad-hoc casts at call sites.
inline Pcg32 sub_rng(const Pcg32& parent, int a, int b) noexcept {
  return sub_rng(parent, (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
                           static_cast<uint32_t>(b));
}
inline Pcg32 sub_rng(const Pcg32& parent, int a, int b, int c) noexcept {
  const uint64_t hi = (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
                       static_cast<uint32_t>(b);
  const uint64_t lo = static_cast<uint32_t>(c);
  // Mix the low word with a Weyl constant to scramble bits.
  return sub_rng(parent, hi ^ (lo * 0x9E3779B97F4A7C15ULL));
}

} // namespace colony::worldgen
