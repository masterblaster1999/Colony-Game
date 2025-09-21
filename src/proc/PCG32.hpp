#pragma once
#include <cstdint>
#include <cstring>

// Minimal PCG32 (Melissa O'Neill). Good quality & tiny. See pcg-random.org
// (Apache 2.0 licensed references). :contentReference[oaicite:6]{index=6}
struct PCG32 {
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t inc   = 0xda3e39cb94b95bdbULL;

    PCG32(uint64_t seed=0x4d595df4d0f33173ULL, uint64_t seq=1ULL) {
        state = 0; inc = (seq<<1u)|1u; nextU32(); state += seed; nextU32();
    }
    uint32_t nextU32() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = old >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-int)rot & 31));
    }
    float next01() { return (nextU32() >> 8) * (1.0f / 16777216.0f); }
    void  advance(uint64_t delta) { // jump ahead (O(log n))
        uint64_t cur_mult = 6364136223846793005ULL, cur_plus = inc, acc_mult = 1, acc_plus = 0;
        while (delta) {
            if (delta & 1) { acc_mult *= cur_mult; acc_plus = acc_plus * cur_mult + cur_plus; }
            cur_plus = (cur_mult + 1) * cur_plus; cur_mult *= cur_mult; delta >>= 1;
        }
        state = acc_mult * state + acc_plus;
    }
};
