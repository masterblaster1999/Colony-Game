// src/worldgen/StageContext.hpp
#pragma once
#include <cstdint>
#include "worldgen/Random.hpp"

namespace colony::worldgen {

// Minimal world-gen context available to all stages.
// Add more fields as your stages require (dimensions, maps, settings, etc.).
struct StageContext {
  int     width  = 0;
  int     height = 0;

  // Base RNG for the entire worldgen invocation (seeded by the caller).
  Pcg32   rng{};

  // Deterministic child streams per coordinate/task to keep stages independent.
  Pcg32 sub_rng(uint64_t salt) const noexcept { return colony::worldgen::sub_rng(rng, salt); }
  Pcg32 sub_rng2(int a, int b) const noexcept { return colony::worldgen::sub_rng(rng, a, b); }
  Pcg32 sub_rng3(int a, int b, int c) const noexcept { return colony::worldgen::sub_rng(rng, a, b, c); }
};

} // namespace colony::worldgen
