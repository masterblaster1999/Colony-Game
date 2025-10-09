#pragma once
#include <cstdint>

// Lightweight PCG32 (Melissa O'Neill). Deterministic across platforms when
// used with fixed-width integers. See PCG paper/resources. :contentReference[oaicite:2]{index=2}
struct PCG32 {
    uint64_t state{0x853c49e6748fea9bULL};
    uint64_t inc  {0xda3e39cb94b95bdbULL}; // must be odd

    void seed(uint64_t initstate, uint64_t initseq=0xda3e39cb94b95bdbULL) {
        state = 0u; inc = (initseq<<1u)|1u; next(); state += initstate; next();
    }

    uint32_t next() {
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ULL + (inc|1ULL);
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // [0,1)
    float nextFloat01() { return (next() >> 8) * (1.0f / 16777216.0f); }
};
