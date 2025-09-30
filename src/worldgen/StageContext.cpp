#include "StageContext.hpp"
#include "Stages.hpp"     // for StageId definition
#include "SeedHash.hpp"   // for detail::splitmix64
#include <cstdint>
#include <cstring>

namespace colony::worldgen {

using colony::worldgen::detail::splitmix64;

StageContext::StageContext(const GeneratorSettings& s,
                           const Coord&            coord,
                           Pcg32                   parent,
                           WorldChunk&             chunkRef) noexcept
: settings(s), chunkCoord(coord), rng(parent), chunk(chunkRef) {}

// FNV-1a 32-bit for tag hashing; deterministic & fast
static inline uint32_t fnv1a_32(const char* str) noexcept {
    uint32_t h = 2166136261u;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
        h ^= *p; h *= 16777619u;
    }
    return h;
}

Pcg32 StageContext::sub_rng(StageId stage, const char* tag) const noexcept {
    // mix stage id and tag into a 64-bit salt, then use the existing free function sub_rng(parent, salt)
    const auto stage_u = static_cast<uint64_t>(static_cast<unsigned>(stage));
    const auto tag_u   = static_cast<uint64_t>(fnv1a_32(tag));
    const uint64_t salt = splitmix64((stage_u << 32) ^ tag_u);
    return sub_rng(rng, static_cast<int>(salt >> 32), static_cast<int>(salt & 0xFFFFFFFFu));
}

// --- Back-compat accessors expected by Stages.hpp ---
// Implement these to mirror whatever the stages used to read directly.
Coord StageContext::chunk_origin_world() const noexcept {
    // Example; adjust to your actual math
    return {/*x=*/chunkCoord.x * settings.chunk_world_span.x,
            /*y=*/chunkCoord.y * settings.chunk_world_span.y};
}

int StageContext::cellSize() const noexcept {
    return settings.cell_size; // or wherever you keep it
}

} // namespace colony::worldgen
