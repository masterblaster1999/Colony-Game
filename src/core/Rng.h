// src/core/Rng.h
#pragma once
#include <cstdint>

namespace rng {

using Seed = std::uint64_t;

// 64-bit mixing (good for turning IDs into well-scrambled seeds)
inline std::uint64_t mix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Derive a child seed from a parent seed and a stable numeric ID
inline Seed derive(Seed parent, std::uint64_t id) {
    return mix64(parent ^ mix64(id));
}

// Minimal PCG32 (XSH-RR). One 64-bit state + 64-bit stream/sequence.
struct Pcg32 {
    std::uint64_t state = 0;
    std::uint64_t inc   = 0; // must be odd

    Pcg32() = default;
    Pcg32(Seed initstate, Seed sequence = 0) { seed(initstate, sequence); }

    // sequence selects the stream; different sequences are independent
    void seed(Seed initstate, Seed sequence = 0) {
        state = 0;
        // make sure inc is odd; mix the sequence so small integers differ a lot
        inc   = (mix64(sequence) << 1u) | 1u;
        next_u32();              // advance once with zero state
        state += mix64(initstate);
        next_u32();              // advance again with real state
    }

    std::uint32_t next_u32() {
        std::uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        std::uint32_t rot        = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-static_cast<int>(rot)) & 31));
    }

    std::uint64_t next_u64() {
        std::uint64_t hi = static_cast<std::uint64_t>(next_u32());
        std::uint64_t lo = static_cast<std::uint64_t>(next_u32());
        return (hi << 32) | lo;
    }

    // [0,1)
    float next_float01() {
        // use 24 high bits to get an exact float in [0,1)
        return (next_u32() >> 8) * (1.0f / 16777216.0f);
    }

    // [0,1)
    double next_double01() {
        // 53-bit mantissa for double
        return (next_u64() >> 11) * (1.0 / 9007199254740992.0);
    }

    // Uniform on [0, bound) without modulo bias (rejection method)
    std::uint32_t next_bounded(std::uint32_t bound) {
        std::uint32_t threshold = static_cast<std::uint32_t>(-bound) % bound;
        for (;;) {
            std::uint32_t r = next_u32();
            if (r >= threshold) return r % bound;
        }
    }
};

// Convenience: get a dedicated RNG for a child node in your hierarchy
inline Pcg32 make_rng(Seed parentSeed, std::uint64_t childId, std::uint64_t stream = 0) {
    return Pcg32(derive(parentSeed, childId), stream);
}

} // namespace rng
