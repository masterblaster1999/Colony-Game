#pragma once
#include <cstdint>
#include <string_view>
#include <functional>

namespace pcg {

// SplitMix64: good for seeding xoshiro family
inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// 64-bit mix (Murmur/CityHash-style)
inline uint64_t hash64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^ (x >> 33);
}

inline uint64_t hash_ns(uint64_t worldSeed, int cx, int cy, std::string_view ns) {
    uint64_t h = hash64((uint64_t(cx) << 32) ^ uint64_t(cy));
    h ^= hash64(std::hash<std::string_view>{}(ns));
    return worldSeed ^ h;
}

} // namespace pcg
