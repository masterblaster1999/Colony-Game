// src/worldgen/SeedHash.hpp
#pragma once
#include <cstdint>

namespace colony::worldgen::detail {

// Public-domain style SplitMix64 mixer (Sebastiano Vigna).
// Good for seeding other RNGs; *not* a stream RNG by itself.
constexpr inline std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

} // namespace colony::worldgen::detail
