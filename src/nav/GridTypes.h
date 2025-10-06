#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <limits>

namespace colony::nav {

struct Coord {
    int32_t x = 0, y = 0;
    constexpr bool operator==(const Coord& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Coord& o) const noexcept { return !(*this == o); }
};

struct CoordHash {
    size_t operator()(const Coord& c) const noexcept {
        // Build a 64-bit key (safe on 32/64-bit) and mix.
        const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(c.x));
        const uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(c.y));
        uint64_t k = (ux << 32) | uy;
        // Stronger 64-bit avalanche (SplitMix64)
        k += 0x9e3779b97f4a7c15ull;
        k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ull;
        k = (k ^ (k >> 27)) * 0x94d049bb133111ebull;
        k ^= (k >> 31);
        if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
            // Better entropy when size_t is 32-bit
            return static_cast<size_t>(k ^ (k >> 32));
        } else {
            return static_cast<size_t>(k);
        }
    }
};

inline constexpr float kCostStraight = 1.0f;
inline constexpr float kCostDiagonal = 1.4142135623730951f; // sqrt(2)

enum class DiagonalPolicy : uint8_t {
    Never,          // 4-connected grid
    AllowedIfNoCut, // 8-connected but forbid corner cutting
};

struct Path {
    std::vector<Coord> points;
};

} // namespace colony::nav
