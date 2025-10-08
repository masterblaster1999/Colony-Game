// src/procgen/CaveGenCA.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <random>
#include <queue>
#include <algorithm>
#include <limits>

namespace colony::procgen {

struct CaveParams {
    int width = 128, height = 128;
    float initialWallChance = 0.45f; // 0..1
    int steps = 5;                   // iterations of CA
    int birthLimit = 5;              // if empty and >= birthLimit neighbors -> wall
    int survivalLimit = 4;           // if wall and >= survivalLimit neighbors -> wall
    bool borderWalls = true;         // force outer rim = wall
    int minMainRegion = 64;          // minimum tiles for a region to be considered main
    uint64_t seed = 12345;
};

static inline int idx(int x,int y,int W){ return y*W+x; }

static inline int count_wall_neighbors(const std::vector<uint8_t>& m, int W,int H, int x,int y){
    int c=0;
    for (int dy=-1; dy<=1; ++dy)
    for (int dx=-1; dx<=1; ++dx){
        if (dx==0 && dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (nx<0||ny<0||nx>=W||ny>=H) { c++; continue; } // out of bounds counts as wall
        c += (m[idx(nx,ny,W)]!=0);
    }
    return c;
}

static inline std::vector<uint8_t> step_ca(const std::vector<uint8_t>& m, int W,int H, int birthLimit, int survivalLimit){
    std::vector<uint8_t> out(m.size(),0);
    for (int y=0;y<H;y++){
        for (int x=0;x<W;x++){
            int n = count_wall_neighbors(m,W,H,x,y);
            if (m[idx(x,y,W)]) out[idx(x,y,W)] = (n >= survivalLimit) ? 1 : 0;
            else               out[idx(x,y,W)] = (n >= birthLimit) ? 1 : 0;
        }
    }
    return out;
}

static inline void force_border(std::vector<uint8_t>& m, int W,int H){
    for (int x=0;x<W;x++){ m[idx(x,0,W)] = 1; m[idx(x,H-1,W)] = 1; }
    for (int y=0;y<H;y++){ m[idx(0,y,W)] = 1; m[idx(W-1,y,W)] = 1; }
}

// Flood fill to find largest connected open region (0 = open, 1 = wall). Non‑main regions become walls.
static inline void keep_largest_region(std::vector<uint8_t>& m, int W,int H, int minMain=64){
    std::vector<int> comp(W*H,-1);
    int compId=0; int bestId=-1; int bestSize=0;
    auto inside=[&](int x,int y){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; };

    for (int y=0;y<H;y++) for (int x=0;x<W;x++){
        int i=idx(x,y,W); if (m[i]||comp[i]!=-1) continue;
        int size=0; std::queue<std::pair<int,int>> q; q.emplace(x,y); comp[i]=compId;
        while(!q.empty()){
            auto [cx,cy]=q.front(); q.pop(); size++;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx){
                if (std::abs(dx)+std::abs(dy)!=1) continue; // 4-neigh
                int nx=cx+dx, ny=cy+dy; if(!inside(nx,ny)) continue;
                int j=idx(nx,ny,W); if (m[j]||comp[j]!=-1) continue;
                comp[j]=compId; q.emplace(nx,ny);
            }
        }
        if (size>bestSize){ bestSize=size; bestId=compId; }
        compId++;
    }
    for (int i=0;i<W*H;i++){
        if (m[i]==0 && (comp[i]!=bestId || bestSize<minMain)) m[i]=1;
    }
}

static inline std::vector<uint8_t> generate_cave(const CaveParams& P){
    const int W=P.width, H=P.height;
    std::vector<uint8_t> map(W*H,0);
    std::mt19937_64 rng(P.seed);
    std::bernoulli_distribution wall(P.initialWallChance);

    for (int y=0;y<H;y++) for(int x=0;x<W;x++) map[idx(x,y,W)] = wall(rng) ? 1 : 0;
    if (P.borderWalls) force_border(map,W,H);

    for (int s=0;s<P.steps;s++){
        map = step_ca(map,W,H,P.birthLimit,P.survivalLimit);
        if (P.borderWalls) force_border(map,W,H);
    }
    keep_largest_region(map,W,H,P.minMainRegion);
    if (P.borderWalls) force_border(map,W,H);
    return map; // 1=wall, 0=open
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// auto cave = colony::procgen::generate_cave({.width=200,.height=120,.seed=42});
#endif
