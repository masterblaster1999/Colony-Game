// src/terrain/DeterministicRNG.hpp
#pragma once
#include <cstdint>
#include <type_traits>
#include <bit>          // C++20: std::rotr (fallback provided below if unavailable)

// -----------------------------------------------------------------------------
// Helper: two's-complement "negate" for unsigned types without using unary minus
// This avoids MSVC warning C4146 ("unary minus applied to unsigned") while
// keeping exact wraparound semantics for rotates, masks, etc.
template <class UInt>
[[nodiscard]] constexpr UInt neg_u(UInt x) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "neg_u expects an unsigned type");
    return UInt(0) - x; // well-defined wraparound for unsigned
}

// Helper: 32-bit rotate-right. Prefer std::rotr if available (C++20 <bit>), else
// fall back to a well-defined expression that uses neg_u for the left shift count.
[[nodiscard]] constexpr std::uint32_t rotr32(std::uint32_t x, std::uint32_t r) noexcept {
#if defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L
    return std::rotr(x, static_cast<int>(r)); // header <bit>
#else
    r &= 31u;
    return (x >> r) | (x << (neg_u(r) & 31u));
#endif
}

// -----------------------------------------------------------------------------
// Lightweight PCG32 (Melissa O'Neill). Deterministic across platforms with
// fixed-width integers. See the PCG family paper and site.
struct PCG32 {
    // Default stream & state used in the PCG reference examples.
    std::uint64_t state{ UINT64_C(0x853c49e6748fea9b) };
    std::uint64_t inc  { UINT64_C(0xda3e39cb94b95bdb) }; // MUST be odd

    PCG32() = default;
    explicit PCG32(std::uint64_t initstate,
                   std::uint64_t initseq = UINT64_C(0xda3e39cb94b95bdb)) noexcept {
        seed(initstate, initseq);
    }

    // Standard PCG seeding sequence.
    void seed(std::uint64_t initstate,
              std::uint64_t initseq = UINT64_C(0xda3e39cb94b95bdb)) noexcept {
        state = 0u;
        inc   = (initseq << 1u) | 1u;   // force odd
        (void)next();
        state += initstate;
        (void)next();
    }

    // Core step: xorshift + rotate right by high bits of state.
    [[nodiscard]] std::uint32_t next() noexcept {
        const std::uint64_t oldstate = state;
        // LCG step (mod 2^64) with PCG multiplier
        state = oldstate * UINT64_C(6364136223846793005) + (inc | UINT64_C(1));

        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);

        // Use std::rotr when available; otherwise safe fallback using neg_u.
        return rotr32(xorshifted, rot);
    }

    // Alias to match generator call style: rng()
    [[nodiscard]] std::uint32_t operator()() noexcept { return next(); }

    // [0,1) single-precision float using top 24 bits.
    [[nodiscard]] float nextFloat01() noexcept {
        return (next() >> 8) * (1.0f / 16777216.0f);
    }

    // Unbiased bounded integer in [0, bound) using rejection sampling.
    [[nodiscard]] std::uint32_t nextBounded(std::uint32_t bound) noexcept {
        // From PCG "bounded rand" recipe: avoid bias by trimming the range.
        // C4146-safe form: use neg_u instead of unary minus on an unsigned.
        const std::uint32_t threshold = neg_u(bound) % bound;
        for (;;) {
            const std::uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }
};
