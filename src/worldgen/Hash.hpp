#pragma once
#include <cstdint>
#include <utility>
#include "SeedHash.hpp"

namespace colony::worldgen {

// Combine into a per-stage stream (state, inc) for PCG32
inline std::pair<std::uint64_t, std::uint64_t>
derive_pcg_seed(std::uint64_t worldSeed, std::int64_t cx, std::int64_t cy, std::uint64_t stageId) noexcept {
    using colony::worldgen::detail::splitmix64;

    const std::uint64_t a = splitmix64(worldSeed ^ 0x6a09e667f3bcc909ull);
    const std::uint64_t b = splitmix64(static_cast<std::uint64_t>(cx) ^ 0xbb67ae8584caa73bull);
    const std::uint64_t c = splitmix64(static_cast<std::uint64_t>(cy) ^ 0x3c6ef372fe94f82bull);
    const std::uint64_t d = splitmix64(stageId ^ 0xa54ff53a5f1d36f1ull);

    const std::uint64_t state  = splitmix64(a ^ (b << 1) ^ (c << 7) ^ (d << 13));
    const std::uint64_t stream = splitmix64(d ^ (a << 17) ^ (b << 9) ^ (c << 3));
    // PCG requires stream increment to be odd; we enforce that in the RNG ctor by (stream<<1)|1.
    return { state, stream };
}

} // namespace colony::worldgen
