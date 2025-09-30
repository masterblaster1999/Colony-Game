#pragma once
#include <cstdint>
#include <string_view>
#include "Hash.hpp"

namespace pcg {

// xoshiro256** RNG (fast, high-quality)
struct Rng {
    uint64_t s[4];

    static Rng from_seed(uint64_t seed);
    uint64_t next_u64();             // [0, 2^64-1]
    uint32_t next_u32();             // [0, 2^32-1]
    double   next01();               // [0,1)
    float    next01f();              // [0,1)
    uint64_t jump();                 // 2^128 steps
    uint64_t long_jump();            // 2^192 steps

    int      rangei(int lo, int hi); // inclusive
    float    rangef(float lo, float hi);
    double   ranged(double lo, double hi);
};

inline Rng make_rng(uint64_t worldSeed, int cx, int cy, std::string_view ns) {
    return Rng::from_seed(hash_ns(worldSeed, cx, cy, ns));
}

} // namespace pcg
