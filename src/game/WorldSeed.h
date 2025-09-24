#pragma once
#include <cstdint>
#include "core/Random.h"

namespace worldseed {

// A stable, non-zero default (FNV-1a offset basis).
constexpr uint64_t kDefaultSeed = 1469598103934665603ull;

struct Streams {
    uint64_t terrain;
    uint64_t biome;
    uint64_t scatter;
    uint64_t pathing;
    uint64_t loot;
    uint64_t audio;
};

// Order of precedence for the starting seed:
// 1) Environment variable COLONY_SEED (decimal)
// 2) res/config/default.ini   (line "seed=<uint64>")
// 3) %LOCALAPPDATA%/ColonyGame/config.ini (line "seed=<uint64>")
// 4) kDefaultSeed
uint64_t loadOrDefault();

// Persist the last used seed to %LOCALAPPDATA%/ColonyGame/config.ini
void saveLastUsed(uint64_t seed);

// High-entropy seed for "Random" button / F3
uint64_t randomSeed();

// Deterministically derive subsystem seeds from a root seed.
// (Use these to seed noise, placement, AI, loot, etc.)
Streams derive(uint64_t root);

// Utility: construct a PRNG from a derived sub-seed.
inline rnd::Xoshiro256pp makeRng(uint64_t subSeed) {
    return rnd::Xoshiro256pp::fromSeed(subSeed);
}

} // namespace worldseed
