// src/worldgen/RNGAlias.hpp
#pragma once

// Windows-specific macro hygiene:
// Some Windows headers (via rpcndr.h) define `small` as `char`, which can
// corrupt identifiers named `small`. Undefine it defensively so this header
// remains safe regardless of include order.
#if defined(_WIN32)
  #ifdef small
    #undef small
  #endif
  #ifdef min
    #undef min
  #endif
  #ifdef max
    #undef max
  #endif
#endif

#include <vector>
#include <cstdint>
#include "RNGCore.hpp"
#include "RNGBounded.hpp"

namespace cg::worldgen {

// O(n) linear scan (good for small n or one-off)
[[nodiscard]] inline size_t weighted_index_linear(RNG256& rng, const std::vector<double>& weights) {
    double sum = 0.0; for (double w: weights) sum += (w>0.0?w:0.0);
    if (!(sum>0.0)) return 0;
    double r = rng.uniform(0.0, sum), acc = 0.0;
    for (size_t i=0;i<weights.size();++i) { double w=(weights[i]>0.0?weights[i]:0.0); acc+=w; if (r<=acc) return i; }
    return weights.size()-1;
}

// Vose/Walker alias table (O(1) sampling, O(n) build)
class AliasTable {
public:
    void build(const std::vector<double>& weights) {
        const size_t n = weights.size(); if (!n) { prob_.clear(); alias_.clear(); return; }
        prob_.assign(n,0.0); alias_.assign(n,0);

        double sum=0.0; for (double w: weights) sum += (w>0.0?w:0.0);
        std::vector<double> scaled(n,0.0);
        if (sum<=0.0) { for(size_t i=0;i<n;++i) scaled[i]=1.0; sum=(double)n; }
        else { for(size_t i=0;i<n;++i) scaled[i]=(weights[i]>0.0?weights[i]:0.0); }
        for(double& v: scaled) v *= ((double)n)/sum;

        // NOTE: renamed to avoid Windows macro collisions (e.g., `small` from rpcndr.h)
        std::vector<size_t> small_bins, large_bins; small_bins.reserve(n); large_bins.reserve(n);
        for (size_t i=0;i<n;++i) (scaled[i]<1.0?small_bins:large_bins).push_back(i);

        while(!small_bins.empty() && !large_bins.empty()){
            size_t s = small_bins.back(); small_bins.pop_back();
            size_t l = large_bins.back();
            prob_[s]  = scaled[s];
            alias_[s] = (uint32_t)l;
            scaled[l] = (scaled[l] + scaled[s]) - 1.0;
            if (scaled[l] < 1.0) { large_bins.pop_back(); small_bins.push_back(l); }
        }
        while(!large_bins.empty()){ prob_[large_bins.back()] = 1.0; large_bins.pop_back(); }
        while(!small_bins.empty()){ prob_[small_bins.back()] = 1.0; small_bins.pop_back(); }
    }

    template<class Rng>
    [[nodiscard]] size_t sample(Rng& rng) const {
        const size_t n = prob_.size(); if(!n) return 0;
        const size_t i = (size_t)next_u64_below(rng, (uint64_t)n);
        return (rng.next_double01() < prob_[i]) ? i : alias_[i];
    }

    [[nodiscard]] size_t size() const noexcept { return prob_.size(); }

private:
    std::vector<double>   prob_;
    std::vector<uint32_t> alias_;
};

} // namespace cg::worldgen
