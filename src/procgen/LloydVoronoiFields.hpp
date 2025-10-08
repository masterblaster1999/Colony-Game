// src/procgen/LloydVoronoiFields.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <random>
#include <limits>
#include <utility>
#include <cmath>
#include <algorithm>

namespace colony::procgen {

struct LloydParams {
    int sites = 64;
    int iterations = 3;
    uint64_t seed = 2024;
};

struct VoronoiResult {
    std::vector<int> labels;                 // size W*H, site index [0..sites-1]
    std::vector<std::pair<float,float>> S;   // relaxed site positions
    std::vector<std::vector<int>> adjacency; // site adjacency graph
};

static inline int idx(int x,int y,int W){ return y*W+x; }

static inline VoronoiResult lloyd_relax(int W,int H, const LloydParams& P){
    std::mt19937_64 rng(P.seed);
    std::uniform_real_distribution<float> ux(0,(float)W), uy(0,(float)H);
    VoronoiResult R;
    R.S.resize(P.sites);
    for (int i=0;i<P.sites;i++) R.S[i] = {ux(rng), uy(rng)};

    R.labels.resize(W*H,0);

    for (int it=0; it<std::max(1,P.iterations); ++it){
        // assign
        for (int y=0;y<H;y++){
            for (int x=0;x<W;x++){
                int best=0; float bestD=std::numeric_limits<float>::max();
                for (int s=0;s<P.sites;s++){
                    float dx = x - R.S[s].first, dy = y - R.S[s].second;
                    float d = dx*dx+dy*dy;
                    if (d<bestD){ bestD=d; best=s; }
                }
                R.labels[idx(x,y,W)] = best;
            }
        }
        // recompute centroids
        std::vector<double> sx(P.sites,0), sy(P.sites,0); std::vector<int> cnt(P.sites,0);
        for (int y=0;y<H;y++) for (int x=0;x<W;x++){
            int s = R.labels[idx(x,y,W)];
            sx[s]+=x; sy[s]+=y; cnt[s]++;
        }
        for (int s=0;s<P.sites;s++){
            if (cnt[s]){ R.S[s].first=(float)(sx[s]/cnt[s]); R.S[s].second=(float)(sy[s]/cnt[s]); }
        }
    }

    // adjacency: two sites are neighbors if any 4-neighbor pixels differ
    R.adjacency.assign(P.sites, {});
    auto add_edge=[&](int a,int b){
        if (a==b || a<0 || b<0) return;
        if (std::find(R.adjacency[a].begin(),R.adjacency[a].end(),b)==R.adjacency[a].end())
            R.adjacency[a].push_back(b);
        if (std::find(R.adjacency[b].begin(),R.adjacency[b].end(),a)==R.adjacency[b].end())
            R.adjacency[b].push_back(a);
    };
    for (int y=0;y<H;y++) for (int x=0;x<W;x++){
        int s = R.labels[idx(x,y,W)];
        if (x+1<W) add_edge(s, R.labels[idx(x+1,y,W)]);
        if (y+1<H) add_edge(s, R.labels[idx(x,y+1,W)]);
    }
    return R;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// auto V = colony::procgen::lloyd_relax(W,H, {.sites=50,.iterations=4,.seed=9});
// Use V.labels to color fields; V.adjacency to build region graph.
#endif
