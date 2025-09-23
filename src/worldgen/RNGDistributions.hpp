// src/worldgen/RNGDistributions.hpp
#pragma once
#include <cmath>
#include "RNGCore.hpp"

namespace cg::worldgen {

struct NormalCache {
    bool   has_spare = false;
    double spare     = 0.0;
};

[[nodiscard]] inline double normal(RNG256& rng, NormalCache& cache, double mean=0.0, double stddev=1.0) noexcept {
    if (cache.has_spare) { cache.has_spare=false; return mean + stddev*cache.spare; }
    double u,v,s;
    do {
        u = 2.0*rng.next_double01()-1.0;
        v = 2.0*rng.next_double01()-1.0;
        s = u*u+v*v;
    } while (s>=1.0 || s==0.0);
    const double m = std::sqrt(-2.0*std::log(s)/s);
    cache.spare     = v*m;
    cache.has_spare = true;
    return mean + stddev*(u*m);
}

inline void normal2(RNG256& rng, double mean, double stddev, double& z0, double& z1) noexcept {
    double u,v,s;
    do {
        u = 2.0*rng.next_double01()-1.0;
        v = 2.0*rng.next_double01()-1.0;
        s = u*u+v*v;
    } while (s>=1.0 || s==0.0);
    const double m = std::sqrt(-2.0*std::log(s)/s);
    z0 = mean + stddev*(u*m);
    z1 = mean + stddev*(v*m);
}

[[nodiscard]] inline double exponential(RNG256& rng, double lambda) noexcept {
    double u = rng.next_double_open_open();
    return -std::log(u)/lambda;
}

[[nodiscard]] inline bool bernoulli(RNG256& rng, double p=0.5) noexcept { return rng.next_bool(p); }

[[nodiscard]] inline double uniform(RNG256& rng, double a, double b) noexcept { return rng.uniform(a,b); }
[[nodiscard]] inline float  uniform(RNG256& rng, float  a, float  b) noexcept { return rng.uniform(a,b); }

} // namespace cg::worldgen
