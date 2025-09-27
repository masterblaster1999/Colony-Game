#pragma once
#include <cstdint>
#include <cmath>

namespace colony::worldgen {

// Minimal PCG32 (local shim for worldgen)
struct Pcg32 {
    std::uint64_t state = 0u;
    std::uint64_t inc   = 0u;

    Pcg32(std::uint32_t initstate, std::uint32_t initseq) noexcept {
        state = 0u;
        inc   = (std::uint64_t(initseq) << 1u) | 1u;
        next_u32();
        state += initstate;
        next_u32();
    }

    std::uint32_t next_u32() noexcept {
        std::uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-int(rot)) & 31));
    }

    std::uint32_t nextUInt32() noexcept { return next_u32(); }
    float nextFloat01() noexcept { return (next_u32() & 0x00FFFFFFu) / 16777216.0f; }
};

} // namespace colony::worldgen
