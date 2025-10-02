#pragma once
#include <cstdint>
#include "StageContext.hpp"   // for ChunkCoord (assumes this declares ChunkCoord)
#include "StagesTypes.hpp"    // for StageId

namespace colony::worldgen {

// Sebastiano Vigna's SplitMix64 mixer (public domain).
// Good avalanche; ideal for combining worldSeed, coord, stage id. 
constexpr inline std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

constexpr inline std::uint64_t StageSeed(std::uint64_t worldSeed,
                                         ChunkCoord c,
                                         StageId id) noexcept
{
    // Fold chunk coordinates and stage id into worldSeed, then mix.
    // (Casting to 32-bit before shifting keeps behavior stable if ChunkCoord is signed.)
    const std::uint64_t hx = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x));
    const std::uint64_t hy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.y));
    std::uint64_t h = worldSeed ^ (hx << 32) ^ hy;
    h ^= static_cast<std::uint64_t>(id);
    return splitmix64(h);
}

} // namespace colony::worldgen
