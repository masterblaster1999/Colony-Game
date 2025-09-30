// src/worldgen/StageContext.hpp
#pragma once
#include <cstdint>
#include "worldgen/Random.hpp" // Pcg32 + sub_rng helpers

namespace colony::worldgen {

// Forward declarations to avoid heavy includes and cycles here.
struct GeneratorSettings;
struct WorldChunk;

struct StageContext {
  // Convenience: dimensions of the current chunk grid.
  int width  = 0;
  int height = 0;

  // Read-only generator settings for this run.
  const GeneratorSettings& settings;

  // Chunk coordinate (copied into PODs to avoid including the type here).
  int chunkX = 0;
  int chunkY = 0;

  // RNG stream for this stage.
  Pcg32 rng;

  // Writable reference to the chunk payload being generated.
  WorldChunk& out;

  // Templated ctor avoids requiring complete types at header-parse time.
  // It will be instantiated only where Coord/WorldChunk are defined (e.g., WorldGen.cpp).
  template <class Coord>
  StageContext(const GeneratorSettings& s, const Coord& coord, Pcg32 r, WorldChunk& o) noexcept
    : settings(s), chunkX(coord.x), chunkY(coord.y), rng(r), out(o)
  {
    // Infer dimensions from the chunk's height grid.
    // (Fields.hpp defines Grid::width()/height().)
    width  = out.height.width();
    height = out.height.height();
  }

  // Deterministic child streams (sugar).
  Pcg32 sub_rng(uint64_t salt) const noexcept { return colony::worldgen::sub_rng(rng, salt); }
  Pcg32 sub_rng(int a, int b) const noexcept  { return colony::worldgen::sub_rng(rng, a, b); }
  Pcg32 sub_rng(int a, int b, int c) const noexcept { return colony::worldgen::sub_rng(rng, a, b, c); }

  // Back-compat with any call sites that might use the old names.
  Pcg32 sub_rng2(int a, int b) const noexcept { return colony::worldgen::sub_rng(rng, a, b); }
  Pcg32 sub_rng3(int a, int b, int c) const noexcept { return colony::worldgen::sub_rng(rng, a, b, c); }
};

} // namespace colony::worldgen
