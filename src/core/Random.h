#pragma once
#include <cstdint>

namespace rnd {

// Thomas Wang / SplitMix64 mixer (used to expand a 64-bit seed)
inline uint64_t splitmix64(uint64_t& x) {
    x += 0x9E3779B97F4A7C15ull;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// xoshiro256++: excellent quality, tiny, fast
struct Xoshiro256pp {
    uint64_t s[4];

    static Xoshiro256pp fromSeed(uint64_t seed) {
        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ull;
        Xoshiro256pp r;
        r.s[0] = splitmix64(x);
        r.s[1] = splitmix64(x);
        r.s[2] = splitmix64(x);
        r.s[3] = splitmix64(x);
        if ((r.s[0] | r.s[1] | r.s[2] | r.s[3]) == 0) r.s[0] = 1; // avoid zero state
        return r;
    }

    static inline uint64_t rotl(uint64_t v, int k) { return (v << k) | (v >> (64 - k)); }

    uint64_t nextU64() {
        const uint64_t result = rotl(s[0] + s[3], 23) + s[0];
        const uint64_t t = s[1] << 17;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    uint32_t nextU32() { return static_cast<uint32_t>(nextU64() >> 32); }

    // [0, 1)
    double next01() {
        const uint64_t x = (nextU64() >> 11);               // 53 bits
        return x * (1.0 / (1ull << 53));                    // double in [0,1)
    }

    uint64_t range(uint64_t loInclusive, uint64_t hiInclusive) {
        const uint64_t span = hiInclusive - loInclusive + 1ull;
        return loInclusive + (nextU64() % span);
    }
};

// One-step avalanche for combining seeds / namespaces
inline uint64_t mix(uint64_t a, uint64_t b) {
    uint64_t x = a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2));
    return splitmix64(x);
}

} // namespace rnd
