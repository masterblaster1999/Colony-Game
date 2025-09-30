// src/worldgen/StageContext.hpp
#pragma once
#include <cstdint>
#include <type_traits>
#include "worldgen/Random.hpp" // Pcg32 + sub_rng helpers

namespace colony::worldgen {

// Forward declarations to avoid heavy includes and cycles here.
struct GeneratorSettings;
struct WorldChunk;
struct ChunkCoord;

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

  // Declaration only; the definition lives in StageContext.cpp where ChunkCoord/WorldChunk are fully known.
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
