// ================================ RNG ========================================

// Valid, deterministic default seed (replaces invalid 0xC01onyULL)
static constexpr uint64_t kDefaultSeed = 0xC01DCAFEULL;

class Rng {
public:
    Rng() : eng_(kDefaultSeed) {}
    explicit Rng(uint64_t seed) : eng_(seed ? seed : kDefaultSeed) {}
    int irange(int lo, int hi) { if (lo>hi) std::swap(lo,hi); std::uniform_int_distribution<int> d(lo,hi); return d(eng_); }
    bool chance(double p)      { if (p<=0.0) return false; if (p>=1.0) return true; std::bernoulli_distribution d(p); return d(eng_); }
    double frand(double a=0.0, double b=1.0) { if (a>b) std::swap(a,b); std::uniform_real_distribution<double> d(a,b); return d(eng_); }
private:
    std::mt19937_64 eng_;
};

