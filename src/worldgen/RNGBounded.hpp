// src/worldgen/RNGBounded.hpp
#pragma once
#include <cstdint>
#include <type_traits>
#if defined(_MSC_VER)
  #include <intrin.h>
#endif
#include "RNGCore.hpp"

namespace cg::worldgen {

[[nodiscard]] inline uint64_t mulhi_u64(uint64_t a, uint64_t b) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    return __umulh(a,b);
#elif defined(_MSC_VER) && defined(_M_ARM64)
    unsigned __int64 hi; (void)_umul128(a,b,&hi); return hi;
#else
    return (uint64_t)(((__uint128_t)a*(__uint128_t)b)>>64);
#endif
}

// Lemire (2018): unbiased fast mapping to [0,bound)
[[nodiscard]] inline uint64_t next_u64_below(RNG256& rng, uint64_t bound) noexcept {
    if (bound==0) return 0;
    for(;;){
        uint64_t x = rng.next_u64();
#if defined(_MSC_VER)
        unsigned __int64 hi, lo = _umul128(x,bound,&hi);
        if (lo < bound) {
            uint64_t t = (0 - bound) % bound;
            if (lo < t) continue;
        }
        return hi;
#else
        __uint128_t m = (__uint128_t)x * (__uint128_t)bound;
        uint64_t lo = (uint64_t)m;
        if (lo < bound) {
            uint64_t t = (0 - bound) % bound;
            if (lo < t) continue;
        }
        return (uint64_t)(m>>64);
#endif
    }
}

template<class Int>
[[nodiscard]] inline std::enable_if_t<std::is_integral_v<Int>, Int>
uniform_int(RNG256& rng, Int lo, Int hi) noexcept {
    if constexpr (std::is_signed_v<Int>) {
        using U = std::make_unsigned_t<Int>;
        U range = (U)( (std::make_unsigned_t<Int>)hi - (U)lo ) + U{1};
        U pick  = (U)next_u64_below(rng, (uint64_t)range);
        return (Int)((U)lo + pick);
    } else {
        uint64_t range = (uint64_t)(hi - lo) + 1ull;
        return (Int)(lo + (Int)next_u64_below(rng, range));
    }
}

} // namespace cg::worldgen
