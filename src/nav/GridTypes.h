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
        // 32-bit mix
        uint64_t k = (static_cast<uint32_t>(c.x) << 32) ^ static_cast<uint32_t>(c.y);
        // SplitMix64-ish
        k += 0x9e3779b97f4a7c15ull;
        k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ull;
        k = (k ^ (k >> 27)) * 0x94d049bb133111ebull;
        k =  k ^ (k >> 31);
        return static_cast<size_t>(k);
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
