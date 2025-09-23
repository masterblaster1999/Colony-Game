// src/worldgen/RNGUtils.hpp
#pragma once
#include <algorithm>
#include <iterator>
#include <string_view>
#include <vector>
#include "RNGCore.hpp"
#include "RNGBounded.hpp"

namespace cg::worldgen {

// Fisher–Yates
template<class It>
inline void shuffle(RNG256& rng, It first, It last) {
    using diff = typename std::iterator_traits<It>::difference_type;
    diff n = std::distance(first,last);
    if (n<=1) return;
    for (diff i=n-1; i>0; --i) {
        auto j = (diff)next_u64_below(rng, (uint64_t)(i+1));
        std::iter_swap(first+i, first+j);
    }
}

// Reservoir sample k distinct indices from [0,n)
template<class OutIt>
inline void sample_k_without_replacement(RNG256& rng, uint64_t n, uint64_t k, OutIt out) {
    if (k>n) k=n;
    std::vector<uint64_t> res; res.reserve((size_t)k);
    for (uint64_t i=0;i<n;++i){
        if (i<k) res.push_back(i);
        else {
            uint64_t j = next_u64_below(rng, i+1);
            if (j<k) res[(size_t)j] = i;
        }
    }
    std::copy(res.begin(), res.end(), out);
}

[[nodiscard]] inline RNG256 rng_from_string(std::string_view sv) {
    RNG256 r; r.seed_string(sv); return r;
}

} // namespace cg::worldgen
