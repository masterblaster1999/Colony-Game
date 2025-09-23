// src/worldgen/RNGCore.hpp
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <chrono>
#include <thread>

namespace cg::worldgen {

// ---------- constants ----------
constexpr double CG_PI      = 3.141592653589793238462643383279502884;
constexpr double CG_TWO_PI  = 2.0 * CG_PI;
constexpr float  CG_PIF     = 3.14159265358979323846f;
constexpr float  CG_TWO_PIF = 2.0f * CG_PIF;

// ---------- bit ops ----------
[[nodiscard]] inline uint64_t rotl64(uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
}

// ---------- splitmix64 (public domain) ----------
[[nodiscard]] inline uint64_t splitmix64_step(uint64_t& x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// ---------- FNV-1a 64 hashing (for seed-from-bytes) ----------
[[nodiscard]] inline uint64_t fnv1a64(const void* data, size_t n) noexcept {
    constexpr uint64_t offset = 14695981039346656037ull;
    constexpr uint64_t prime  = 1099511628211ull;
    uint64_t h = offset;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= (uint64_t)p[i]; h *= prime; }
    return h;
}

// ---------- xoshiro256** core ----------
class RNG256 {
public:
    using result_type = uint64_t;

    RNG256() noexcept { seed(0xCBF29CE484222325ull); }
    explicit RNG256(uint64_t seed_value) noexcept { seed(seed_value); }

    inline void seed(uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3) noexcept {
        state_[0]=s0; state_[1]=s1; state_[2]=s2; state_[3]=s3;
        if ((state_[0]|state_[1]|state_[2]|state_[3])==0ull) {
            uint64_t x = 1; for (auto& v: state_) v = splitmix64_step(x);
        }
    }
    inline void seed(uint64_t seed_value) noexcept {
        uint64_t x = seed_value;
        for (auto& v: state_) v = splitmix64_step(x);
        if ((state_[0]|state_[1]|state_[2]|state_[3])==0ull) state_[0]=1;
    }
    inline void seed_bytes(const void* data, size_t n) noexcept { seed(fnv1a64(data,n)); }
    inline void seed_string(std::string_view sv) noexcept { seed_bytes(sv.data(), sv.size()); }

    [[nodiscard]] inline uint64_t next_u64() noexcept {
        const uint64_t result = rotl64(state_[1]*5ull,7) * 9ull;
        const uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0]; state_[3] ^= state_[1];
        state_[1] ^= state_[2]; state_[0] ^= state_[3];
        state_[2] ^= t; state_[3] = rotl64(state_[3],45);
        return result;
    }
    [[nodiscard]] inline uint32_t next_u32() noexcept { return (uint32_t)(next_u64()>>32); }
    [[nodiscard]] inline uint64_t operator()() noexcept { return next_u64(); }
    inline void discard(uint64_t n) noexcept { while (n--) (void)next_u64(); }

    inline void jump() noexcept {
        static constexpr uint64_t J[4]{
            0x180ec6d33cfd0abaULL,0xd5a61266f0c9392cULL,0xa9582618e03fc9aaULL,0x39abdc4529b1661cULL};
        do_jump_(J);
    }
    inline void long_jump() noexcept {
        static constexpr uint64_t L[4]{
            0x76e15d3efefdcbbfULL,0xc5004e441c522fb3ULL,0x77710069854ee241ULL,0x39109bb02acbe635ULL};
        do_jump_(L);
    }

    // [0,1) reals
    [[nodiscard]] inline double next_double01() noexcept { return (next_u64()>>11)*(1.0/9007199254740992.0); }
    [[nodiscard]] inline float  next_float01()  noexcept { return (float)((next_u64()>>40)*(1.0/16777216.0)); }
    [[nodiscard]] inline double next_double_open_open() noexcept {
        double x; do { x=next_double01(); } while (x<=0.0 || x>=1.0); return x;
    }
    [[nodiscard]] inline float  next_float_open_open() noexcept {
        float x; do { x=next_float01(); } while (x<=0.f || x>=1.f); return x;
    }
    [[nodiscard]] inline double uniform(double a,double b) noexcept { return a+(b-a)*next_double01(); }
    [[nodiscard]] inline float  uniform(float a,float b)   noexcept { return a+(b-a)*next_float01(); }
    [[nodiscard]] inline bool   next_bool(double p=0.5)    noexcept { if(p<=0) return false; if(p>=1) return true; return next_double01()<p; }

    // serialization
    inline void serialize(uint64_t out[4]) const noexcept { std::memcpy(out,state_.data(),sizeof(state_)); }
    inline void deserialize(const uint64_t in[4]) noexcept {
        std::memcpy(state_.data(),in,sizeof(state_)); if((state_[0]|state_[1]|state_[2]|state_[3])==0) state_[0]=1;
    }

    // fork a deterministic substream
    [[nodiscard]] inline RNG256 fork(uint64_t stream_id) const noexcept {
        uint64_t x = stream_id ^ state_[0] ^ rotl64(state_[1],13) ^ rotl64(state_[2],27) ^ rotl64(state_[3],41);
        RNG256 r; r.seed(splitmix64_step(x),splitmix64_step(x),splitmix64_step(x),splitmix64_step(x)); return r;
    }

    // thread-local default RNG (non-deterministic)
    static inline RNG256& tls_rng() {
        thread_local RNG256 g = []{
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
            RNG256 r; r.seed(0xA24BAED4963EE407ull ^ (uint64_t)now ^ ((uint64_t)tid<<1)); return r;
        }();
        return g;
    }

private:
    std::array<uint64_t,4> state_{0x123456789abcdef0ull,0xfedcba9876543210ull,0x0f1e2d3c4b5a6978ull,0x87654321deadbeefull};

    inline void do_jump_(const uint64_t poly[4]) noexcept {
        uint64_t s0=0,s1=0,s2=0,s3=0;
        for(int i=0;i<4;++i) for(int b=0;b<64;++b) { if(poly[i]&(1ull<<b)){ s0^=state_[0]; s1^=state_[1]; s2^=state_[2]; s3^=state_[3]; } (void)next_u64(); }
        state_[0]=s0; state_[1]=s1; state_[2]=s2; state_[3]=s3;
    }
};

} // namespace cg::worldgen
