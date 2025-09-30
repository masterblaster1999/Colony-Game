#include "SeededRng.hpp"

namespace {
inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
}

namespace pcg {

Rng Rng::from_seed(uint64_t seed) {
    uint64_t x = seed ? seed : 0x106689d45497fdb5ULL;
    Rng r{};
    r.s[0] = splitmix64(x);
    r.s[1] = splitmix64(x);
    r.s[2] = splitmix64(x);
    r.s[3] = splitmix64(x);
    return r;
}

uint64_t Rng::next_u64() {
    const uint64_t result = rotl(s[1] * 5ull, 7) * 9ull;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1];
    s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = rotl(s[3], 45);
    return result;
}

uint32_t Rng::next_u32() { return static_cast<uint32_t>(next_u64() >> 32); }
double   Rng::next01()   { return (next_u64() >> 11) * (1.0 / (1ull << 53)); }
float    Rng::next01f()  { return static_cast<float>(next01()); }

uint64_t Rng::jump() {
    static const uint64_t JUMP[] = { 0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
                                     0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL };
    uint64_t s0=0, s1=0, s2=0, s3=0;
    for (uint64_t jp : JUMP) {
        for (int b=0; b<64; ++b) {
            if (jp & (1ull << b)) { s0 ^= s[0]; s1 ^= s[1]; s2 ^= s[2]; s3 ^= s[3]; }
            next_u64();
        }
    }
    s[0]=s0; s[1]=s1; s[2]=s2; s[3]=s3;
    return s0 ^ s1 ^ s2 ^ s3;
}

uint64_t Rng::long_jump() {
    static const uint64_t LJUMP[] = { 0x76e15d3efefdcbbfULL, 0xc5004e441c522fb3ULL,
                                      0x77710069854ee241ULL, 0x39109bb02acbe635ULL };
    uint64_t s0=0, s1=0, s2=0, s3=0;
    for (uint64_t jp : LJUMP) {
        for (int b=0; b<64; ++b) {
            if (jp & (1ull << b)) { s0 ^= s[0]; s1 ^= s[1]; s2 ^= s[2]; s3 ^= s[3]; }
            next_u64();
        }
    }
    s[0]=s0; s[1]=s1; s[2]=s2; s[3]=s3;
    return s0 ^ s1 ^ s2 ^ s3;
}

int Rng::rangei(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t r = next_u32();
    uint32_t span = static_cast<uint32_t>(hi - lo + 1);
    return lo + (r % span);
}
float  Rng::rangef(float lo, float hi)   { return lo + (hi - lo) * next01f(); }
double Rng::ranged(double lo, double hi) { return lo + (hi - lo) * next01(); }

} // namespace pcg
