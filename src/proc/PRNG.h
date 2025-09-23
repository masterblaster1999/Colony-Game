// src/proc/PRNG.h
// Header-only RNG utilities for Colony-Game (C++17, MSVC/Clang/GCC)
// Engines: SplitMix64 (seed/hash), PCG32 (advance), xoshiro256++ (jump/long_jump)
// Distributions: uniform int/real, Bernoulli, exponential, Gaussian, triangular
// Sampling: unit disk/sphere, cosine hemisphere
// Utils: Fisher-Yates shuffle, reservoir sampling, Vose alias table
//
// References (see calling site for discussion):
// - PCG family & paper: https://www.pcg-random.org/  (Melissa O'Neill)
// - PCG paper PDF:      https://www.cs.hmc.edu/tr/hmc-cs-2014-0905.pdf
// - xoshiro256++ ref:   https://prng.di.unimi.it/xoshiro256plusplus.c  (Blackman & Vigna)
// - SplitMix64 seeding guidance: https://prng.di.unimi.it/  (seed with a *different* kind of generator)
// - Lemire's unbiased bounded ints: https://arxiv.org/abs/1805.10941
//
// IMPORTANT: Not cryptographically secure.

#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>
#include <array>
#include <limits>
#include <cmath>
#include <algorithm>

#if defined(_MSC_VER)
  #include <intrin.h>
#endif

namespace colony::proc {

// ---------------------------------------------------------------
// Bit utilities (C++17-friendly rotl)
// ---------------------------------------------------------------
constexpr inline uint32_t rotl32(uint32_t x, int r) noexcept {
    return (x << (r & 31)) | (x >> ((32 - r) & 31));
}
constexpr inline uint64_t rotl64(uint64_t x, int r) noexcept {
    return (x << (r & 63)) | (x >> ((64 - r) & 63));
}

// 128-bit multiply: returns (hi, lo)
struct Mul128 { uint64_t hi, lo; };
inline Mul128 umul128(uint64_t a, uint64_t b) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    uint64_t hi;
    uint64_t lo = _umul128(a, b, &hi);
    return {hi, lo};
#else
    __uint128_t p = ( (__uint128_t)a ) * ( (__uint128_t)b );
    return { (uint64_t)(p >> 64), (uint64_t)p };
#endif
}

// ---------------------------------------------------------------
// SplitMix64 - excellent mixer; great for seeding & hashing
// (Sebastiano Vigna’s reference mix, often used to seed xoshiro)
// ---------------------------------------------------------------
struct SplitMix64 {
    uint64_t x;
    explicit constexpr SplitMix64(uint64_t seed = 0x853c49e6748fea9bULL) noexcept : x(seed) {}

    // Next 64 bits
    inline uint64_t next_u64() noexcept {
        uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    inline uint32_t next_u32() noexcept { return uint32_t(next_u64() >> 32); }

    inline void seed(uint64_t s) noexcept { x = s; }

    // Stateless mixing helpers (useful as 64-bit hash)
    static inline uint64_t mix(uint64_t v) noexcept {
        v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
        v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
        return v ^ (v >> 31);
    }
};

// Seed sequence: expand a single 64-bit seed into N 64-bit seeds
inline std::vector<uint64_t> seed_sequence64(uint64_t seed, size_t n) {
    SplitMix64 sm(seed ? seed : 0x9E3779B97F4A7C15ULL);
    std::vector<uint64_t> out(n);
    for (size_t i=0;i<n;++i) out[i] = sm.next_u64();
    return out;
}

// ---------------------------------------------------------------
// PCG32 (XSH-RR) – 64-bit LCG state, 32-bit output
// - stream parameterization via 'inc'
// - advance(delta) in O(log delta)
// Ref: O'Neill's PCG paper & site
// ---------------------------------------------------------------
struct PCG32 {
    uint64_t state{0x853c49e6748fea9bULL};
    uint64_t inc  {0xda3e39cb94b95bdbULL}; // must be odd

    PCG32() = default;
    PCG32(uint64_t seed, uint64_t seq = 0xda3e39cb94b95bdbULL) noexcept { seed_seq(seed, seq); }

    inline void seed_seq(uint64_t seed, uint64_t seq) noexcept {
        state = 0u;
        inc = (seq << 1u) | 1u;
        next_u32();
        state += seed;
        next_u32();
    }

    inline uint32_t next_u32() noexcept {
        uint64_t old = state;
        // 64-bit LCG
        state = old * 6364136223846793005ULL + inc;
        // XSH-RR output (xorshift; random rotate)
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-int(rot)) & 31));
    }

    inline uint64_t next_u64() noexcept {
        // Two 32-bit outputs compose a 64-bit
        uint64_t hi = next_u32();
        uint64_t lo = next_u32();
        return (hi << 32) | lo;
    }

    // Jump ahead by 'delta' steps (O(log delta)) – from PCG docs.
    inline void advance(uint64_t delta) noexcept {
        uint64_t cur_mult = 6364136223846793005ULL;
        uint64_t cur_plus = inc;
        uint64_t acc_mult = 1ULL;
        uint64_t acc_plus = 0ULL;

        while (delta > 0) {
            if (delta & 1) {
                acc_mult *= cur_mult;
                acc_plus = acc_plus * cur_mult + cur_plus;
            }
            cur_plus = (cur_mult + 1) * cur_plus;
            cur_mult *= cur_mult;
            delta >>= 1u;
        }
        state = acc_mult * state + acc_plus;
    }
};

// ---------------------------------------------------------------
// xoshiro256++ 1.0 – 256-bit state, fast & high quality
// - jump() : 2^128 steps; long_jump() : 2^192 steps
// - seed via SplitMix64 to fill 4x64 state (non-zero)
// Ref: Blackman & Vigna reference implementation
// ---------------------------------------------------------------
struct Xoshiro256PP {
    uint64_t s[4]{0,0,0,0};

    Xoshiro256PP() = default;
    explicit Xoshiro256PP(uint64_t seed) noexcept { seed_with_splitmix(seed); }

    inline void seed_with_splitmix(uint64_t seed) noexcept {
        SplitMix64 sm(seed);
        s[0]=sm.next_u64(); s[1]=sm.next_u64(); s[2]=sm.next_u64(); s[3]=sm.next_u64();
        // avoid all-zero state
        if ((s[0]|s[1]|s[2]|s[3]) == 0) s[0] = 0x1ULL;
    }

    inline uint64_t next_u64() noexcept {
        const uint64_t result = rotl64(s[0] + s[3], 23) + s[0];
        const uint64_t t = s[1] << 17;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;
        s[3] = rotl64(s[3], 45);
        return result;
    }
    inline uint32_t next_u32() noexcept { return (uint32_t)(next_u64() >> 32); }

    // 2^128 step jump
    inline void jump() noexcept {
        static constexpr uint64_t J[] = {
            0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
            0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
        };
        uint64_t t0=0,t1=0,t2=0,t3=0;
        for (int i=0;i<4;i++) {
            for (int b=0;b<64;b++) {
                if (J[i] & (1ULL << b)) { t0^=s[0]; t1^=s[1]; t2^=s[2]; t3^=s[3]; }
                next_u64();
            }
        }
        s[0]=t0; s[1]=t1; s[2]=t2; s[3]=t3;
    }

    // 2^192 step long jump
    inline void long_jump() noexcept {
        static constexpr uint64_t LJ[] = {
            0x76e15d3efefdcbbfULL, 0xc5004e441c522fb3ULL,
            0x77710069854ee241ULL, 0x39109bb02acbe635ULL
        };
        uint64_t t0=0,t1=0,t2=0,t3=0;
        for (int i=0;i<4;i++) {
            for (int b=0;b<64;b++) {
                if (LJ[i] & (1ULL << b)) { t0^=s[0]; t1^=s[1]; t2^=s[2]; t3^=s[3]; }
                next_u64();
            }
        }
        s[0]=t0; s[1]=t1; s[2]=t2; s[3]=t3;
    }
};

// ---------------------------------------------------------------
// Uniform integers (unbiased), Lemire method
//  - u32: 64-bit multiply, shift
//  - u64: 128-bit multiply (uses __int128 or _umul128)
// Ref: Lemire, "Fast Random Integer Generation in an Interval"
// ---------------------------------------------------------------
template<class Rng>
inline uint32_t uniform_u32(Rng& rng, uint32_t bound) noexcept {
    // Returns in [0, bound)
    // If bound == 0, return full 32-bit (undefined interval), but handle anyway
    if (!bound) return rng.next_u32();
    uint64_t r = rng.next_u32();
    uint64_t m = r * bound;
    uint32_t l = (uint32_t)m;
    if (l < bound) {
        uint32_t t = (uint32_t)(-bound % bound);
        while (l < t) {
            r = rng.next_u32();
            m = r * bound;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
}

template<class Rng>
inline uint64_t uniform_u64(Rng& rng, uint64_t bound) noexcept {
    // Returns in [0, bound)
    if (!bound) return rng.next_u64();
    for (;;) {
        uint64_t r = rng.next_u64();
        Mul128 p = umul128(r, bound);
        if (p.lo >= (-bound % bound)) return p.hi; // accept
    }
}

// Inclusive/exclusive helpers
template<class Rng>
inline uint32_t uniform_u32(Rng& rng, uint32_t lo, uint32_t hi_inclusive) noexcept {
    uint64_t span = uint64_t(hi_inclusive) - lo + 1u;
    return lo + (uint32_t)uniform_u32(rng, (uint32_t)span);
}
template<class Rng>
inline uint64_t uniform_u64(Rng& rng, uint64_t lo, uint64_t hi_inclusive) noexcept {
    __uint128_t span = ( (__uint128_t)hi_inclusive ) - lo + 1;
    // When span fits 64-bit, use bounded; else degrade (very rare)
    if (span <= std::numeric_limits<uint64_t>::max()) {
        return lo + uniform_u64(rng, (uint64_t)span);
    } else {
        // Fallback: rejection on full 64-bit
        uint64_t r;
        do { r = rng.next_u64(); } while (r < lo || r > hi_inclusive);
        return r;
    }
}

// ---------------------------------------------------------------
// Uniform real mapping (bit-exact, [0,1) )
// ---------------------------------------------------------------
inline float u01_float_from_u32(uint32_t x) noexcept {
    // 24 random mantissa bits -> [1.0, 2.0) then minus 1
    union { uint32_t i; float f; } u;
    u.i = (127u << 23) | (x >> 9);
    return u.f - 1.0f;
}
inline double u01_double_from_u64(uint64_t x) noexcept {
    // 53 random mantissa bits -> [1.0, 2.0) then minus 1
    union { uint64_t i; double d; } u;
    u.i = (uint64_t(1023) << 52) | (x >> 12);
    return u.d - 1.0;
}

template<class Rng> inline float  uniform_float01(Rng& rng) noexcept { return u01_float_from_u32(rng.next_u32()); }
template<class Rng> inline double uniform_double01(Rng& rng) noexcept { return u01_double_from_u64(rng.next_u64()); }

template<class Rng> inline float  uniform_float(Rng& rng, float  a, float  b) noexcept { return a + (b-a) * uniform_float01(rng); }
template<class Rng> inline double uniform_double(Rng& rng, double a, double b) noexcept { return a + (b-a) * uniform_double01(rng); }

// ---------------------------------------------------------------
// Basic distributions
// ---------------------------------------------------------------
template<class Rng>
inline bool bernoulli(Rng& rng, double p) noexcept {
    return uniform_double01(rng) < p;
}

template<class Rng>
inline double exponential(Rng& rng, double lambda) noexcept {
    // lambda > 0
    double u = 1.0 - uniform_double01(rng); // (0,1]
    return -std::log(u) / lambda;
}

template<class Rng>
inline double normal_box_muller(Rng& rng, double mean=0.0, double stddev=1.0) noexcept {
    // Uncached Box-Muller (two uniforms per call)
    const double u1 = std::max(1e-16, uniform_double01(rng)); // avoid log(0)
    const double u2 = uniform_double01(rng);
    const double r = std::sqrt(-2.0 * std::log(u1));
    const double th = 6.28318530717958647692 * u2; // 2*pi
    return mean + stddev * (r * std::cos(th));
}

template<class Rng>
inline double triangular(Rng& rng, double a, double b, double c) noexcept {
    // a < c < b, triangular distribution
    double u = uniform_double01(rng);
    double F = (c - a) / (b - a);
    if (u < F) return a + std::sqrt(u * (b - a) * (c - a));
    else       return b - std::sqrt((1 - u) * (b - a) * (b - c));
}

// ---------------------------------------------------------------
// Geometry sampling
// ---------------------------------------------------------------
template<class Rng>
inline std::array<float,2> sample_unit_disk_concentric(Rng& rng) noexcept {
    // Shirley-Chiu concentric mapping
    float u1 = uniform_float(rng, -1.0f, 1.0f);
    float u2 = uniform_float(rng, -1.0f, 1.0f);
    if (u1 == 0 && u2 == 0) return {0,0};
    float r, theta;
    if (std::fabs(u1) > std::fabs(u2)) {
        r = u1; theta = (3.14159265358979323846f/4.f) * (u2/u1);
    } else {
        r = u2; theta = (3.14159265358979323846f/2.f) - (3.14159265358979323846f/4.f) * (u1/u2);
    }
    return { r*std::cos(theta), r*std::sin(theta) };
}

template<class Rng>
inline std::array<float,3> sample_unit_sphere(Rng& rng) noexcept {
    double u = uniform_double01(rng);
    double v = uniform_double01(rng);
    double z = 1.0 - 2.0*u;
    double r = std::sqrt(std::max(0.0, 1.0 - z*z));
    double phi = 6.28318530717958647692 * v;
    return { (float)(r*std::cos(phi)), (float)(r*std::sin(phi)), (float)z };
}

template<class Rng>
inline std::array<float,3> sample_cosine_hemisphere(Rng& rng) noexcept {
    auto d = sample_unit_disk_concentric(rng);
    float x = d[0], y = d[1];
    float z2 = std::max(0.f, 1.f - x*x - y*y);
    return { x, y, std::sqrt(z2) }; // cosine-weighted about +Z
}

// ---------------------------------------------------------------
// Shuffle & reservoir sampling
// ---------------------------------------------------------------
template<class Rng, class It>
inline void shuffle(Rng& rng, It first, It last) {
    // Fisher–Yates with unbiased bounded ints
    using diff_t = typename std::iterator_traits<It>::difference_type;
    diff_t n = last - first;
    for (diff_t i = n - 1; i > 0; --i) {
        uint64_t j = uniform_u64(rng, (uint64_t)(i + 1)); // [0, i]
        std::swap(first[i], first[(diff_t)j]);
    }
}

template<class Rng, class T>
inline std::vector<T> reservoir_sample(Rng& rng, const std::vector<T>& src, size_t k) {
    std::vector<T> r; r.reserve(std::min(k, src.size()));
    size_t i = 0;
    for (; i < std::min(k, src.size()); ++i) r.push_back(src[i]);
    for (; i < src.size(); ++i) {
        uint64_t j = uniform_u64(rng, 0ULL, (uint64_t)i);
        if (j < k) r[(size_t)j] = src[i];
    }
    return r;
}

// ---------------------------------------------------------------
// Vose alias method for weighted discrete sampling
// ---------------------------------------------------------------
struct AliasTable {
    std::vector<float> prob;   // in [0,1]
    std::vector<uint32_t> alias;

    void build(const std::vector<float>& w) {
        const size_t n = w.size();
        prob.assign(n, 0.f);
        alias.assign(n, 0u);
        if (n == 0) return;

        // normalize
        double sum = 0; for (float x: w) sum += std::max(0.f, x);
        if (sum <= 0) { for (size_t i=0;i<n;++i){ prob[i]=1.0f; alias[i]=(uint32_t)i; } return; }

        std::vector<double> p(n);
        for (size_t i=0;i<n;++i) p[i] = (w[i] <= 0 ? 0.0 : (double)w[i]/sum) * n;

        std::vector<uint32_t> small, large;
        small.reserve(n); large.reserve(n);
        for (uint32_t i=0;i<n;++i) ((p[i] < 1.0) ? small : large).push_back(i);

        while (!small.empty() && !large.empty()) {
            uint32_t s = small.back(); small.pop_back();
            uint32_t l = large.back();
            prob[s] = (float)p[s];
            alias[s] = l;
            p[l] = (p[l] + p[s]) - 1.0;
            if (p[l] < 1.0) { large.pop_back(); small.push_back(l); }
        }
        while (!large.empty()) { uint32_t l = large.back(); large.pop_back(); prob[l] = 1.0f; alias[l] = l; }
        while (!small.empty()) { uint32_t s = small.back(); small.pop_back(); prob[s] = 1.0f; alias[s] = s; }
    }

    template<class Rng>
    uint32_t sample(Rng& rng) const {
        if (prob.empty()) return 0;
        uint32_t n = (uint32_t)prob.size();
        uint32_t col = uniform_u32(rng, n);
        float u = uniform_float01(rng);
        return (u < prob[col]) ? col : alias[col];
    }
};

// ---------------------------------------------------------------
// Convenience typedefs & helpers
// ---------------------------------------------------------------
using DefaultFast64 = Xoshiro256PP; // excellent default for 64-bit
using DefaultFast32 = PCG32;        // widely used 32-bit output generator

// Global "one-off" helpers (avoid if you need strict determinism across systems)
inline uint64_t fast_u64(uint64_t seed) {
    Xoshiro256PP rng(seed);
    return rng.next_u64();
}
inline uint32_t fast_u32(uint64_t seed) {
    Xoshiro256PP rng(seed);
    return rng.next_u32();
}

// ---------------------------------------------------------------
// Safety note (in comments):
// These generators (PCG, xoshiro, SplitMix) are designed for
// simulation/procedural content, not cryptography.
// ---------------------------------------------------------------

} // namespace colony::proc
