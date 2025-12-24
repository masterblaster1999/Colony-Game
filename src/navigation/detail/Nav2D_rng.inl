// ============ RNG for tie-breaks in non-critical choices ============
struct SplitMix64 {
    uint64_t s;
    explicit SplitMix64(uint64_t seed = NAV2D_SEED) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double next01() { return (next() >> 11) * (1.0/9007199254740992.0); }
};

