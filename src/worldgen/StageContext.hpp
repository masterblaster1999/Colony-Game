// src/worldgen/StageContext.hpp
#pragma once
#include <cstdint>
#include <type_traits>
#include <cstddef>                 // for std::size_t
#include "worldgen/GeneratorSettings.hpp"  // interface-only config
#include "worldgen/Random.hpp"             // Pcg32 + sub_rng helpers

namespace colony::worldgen {

// Forward declarations to avoid heavy includes and cycles here (keep light).
struct WorldChunk;
struct ChunkCoord;

// Forward-declare the cell payload type used by stages.
// NOTE: We intentionally keep Cell incomplete here so this header stays light.
// Any code that needs to *touch* Cell's definition should include the real header
// (e.g., "worldgen/Cell.hpp") in the corresponding .cpp.
struct Cell;

// Interface-only context passed to worldgen stages.
// NOTE: No code here should touch WorldChunk's internals; keep all such logic in the .cpp.
struct StageContext {
  // Convenience: dimensions of the current chunk grid (filled by the .cpp ctor).
  int width  = 0;
  int height = 0;

  // Read-only generator settings for this run.
  const GeneratorSettings& settings;

  // Chunk coordinate copied into PODs to avoid including the type here.
  int chunkX = 0;
  int chunkY = 0;

  // RNG stream for this stage.
  Pcg32 rng;

  // Writable reference to the chunk payload being generated.
  WorldChunk& out;

  // Some call sites reference ctx.chunk; make it an alias of `out`.
  WorldChunk& chunk;

  // -------------------------------------------------------------------
  // Kept lightweight and header-only for fast includes (Windows/MSVC):
  //   - chunk_origin_world
  //   - cellSize
  //   - cells / cells_count  (replaces std::span<Cell>; safe with incomplete Cell)
  //   - sub_seed
  //   - chunk_seed
  // -------------------------------------------------------------------

  // Minimal 2D float vector without introducing heavy math headers.
  struct Float2 {
    float x{};
    float y{};
  };

  // World-space origin of the current chunk (meters/units).
  Float2 chunk_origin_world{};

  // Cell size in world units (e.g., meters per grid cell).
  std::uint32_t cellSize = 0;

  // Borrowed view of the chunk's cell buffer (row-major).
  // We avoid std::span<Cell> here because Cell is incomplete in this header.
  // In .cpp files that include the real Cell definition, you may do:
  //   #include <span>
  //   std::span<Cell> view{cells, cells_count};
  Cell*        cells       = nullptr;
  std::size_t  cells_count = 0;

  // Deterministic seeds for sub-stages and per-chunk variation.
  std::uint64_t sub_seed   = 0;
  std::uint64_t chunk_seed = 0;

  // Declaration only; the definition lives in StageContext.cpp
  // where ChunkCoord/WorldChunk and Cell are fully known.
  StageContext(const GeneratorSettings& s, const ChunkCoord& coord, Pcg32 r, WorldChunk& o) noexcept;

  // Deterministic child streams (sugar).
  inline Pcg32 sub_rng(std::uint64_t salt) const noexcept { return colony::worldgen::sub_rng(rng, salt); }
  inline Pcg32 sub_rng(int a, int b) const noexcept        { return colony::worldgen::sub_rng(rng, a, b); }
  inline Pcg32 sub_rng(int a, int b, int c) const noexcept { return colony::worldgen::sub_rng(rng, a, b, c); }

  // Back-compat with any call sites that might use the old names.
  inline Pcg32 sub_rng2(int a, int b) const noexcept        { return colony::worldgen::sub_rng(rng, a, b); }
  inline Pcg32 sub_rng3(int a, int b, int c) const noexcept { return colony::worldgen::sub_rng(rng, a, b, c); }

  // Overload that accepts a strongly-typed stage enum + ASCII tag without depending on its definition here.
  // Works for any scoped or unscoped enum type.
  template <typename E, typename = std::enable_if_t<std::is_enum<E>::value>>
  inline Pcg32 sub_rng(E stage, const char* tag) const noexcept {
    // FNV-1a 32-bit hash of tag (deterministic and fast)
    std::uint32_t h = 2166136261u;
    if (tag) {
      const unsigned char* p = reinterpret_cast<const unsigned char*>(tag);
      while (*p) { h ^= *p++; h *= 16777619u; }
    }
    using U = std::underlying_type_t<E>;
    const std::uint32_t s32 = static_cast<std::uint32_t>(static_cast<U>(stage));
    // Reuse existing two-int salt path; Random.hpp mixes further (SplitMix/Weyl in sub_rng).
    return colony::worldgen::sub_rng(rng, static_cast<int>(s32), static_cast<int>(h));
  }
};

} // namespace colony::worldgen
