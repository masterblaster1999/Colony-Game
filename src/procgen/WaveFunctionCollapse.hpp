// src/procgen/WaveFunctionCollapse.hpp
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <random>
#include <limits>
#include <optional>
#include <string>
#include <algorithm>

namespace colony::procgen {

enum Dir { N=0, E=1, S=2, W=3 };

struct WFCRules {
    // up to 64 tiles; for each tile, a bitmask of allowed neighbors on each side
    // compat[t][dir] is a 64-bit mask where bit u=1 means tile u allowed touching side dir of tile t
    std::vector<std::array<uint64_t,4>> compat;
    std::vector<float> weight; // preference during collapse
};

struct WFCParams {
    int width=32, height=32;
    uint64_t seed=12345;
    int maxSteps=100000; // safety
};

struct WFCResult {
    bool success=false;
    std::vector<uint8_t> tiles; // size W*H, each in [0..numTiles-1]; undefined if !success
};

// Helper: make a tiny "ruins" ruleset (floor/wall/corridor) to get you started.
static inline WFCRules MakeSimpleRuinsRules() {
    enum {FLOOR, WALL, CORR, T, END, COUNT};
    WFCRules R; R.compat.resize(COUNT); R.weight = {3.0f, 2.0f, 1.2f, 0.9f, 0.8f};
    auto mask=[&](std::initializer_list<int> L){ uint64_t m=0; for(int t : L) m |= (1ull<<t); return m; };
    // A minimal, permissive set:
    // FLOOR wants FLOOR/CORR/T/END adjacent; WALL allowed too (rooms carved into ruins)
    for (int d=0; d<4; ++d){
        R.compat[FLOOR][d] = mask({FLOOR,CORR,T,END,WALL});
        R.compat[WALL][d]  = mask({WALL,FLOOR});      // wall next to wall/floor
        R.compat[CORR][d]  = mask({CORR,FLOOR,T,END}); // corridors join rooms & other corridors
        R.compat[T][d]     = mask({CORR,FLOOR,T,END});
        R.compat[END][d]   = mask({CORR,FLOOR,T,WALL}); // dead-ends at walls ok
    }
    return R;
}

static inline int idx(int x,int y,int W){ return y*W+x; }
static inline std::pair<int,int> step_dir(int x,int y, Dir d){ 
    switch(d){ case N: return {x,y-1}; case S: return {x,y+1}; case E: return {x+1,y}; default: return {x-1,y}; }
}

class WFCSolver {
    int W,H,T;
    WFCRules rules;
    std::vector<uint64_t> wave;   // per cell bitmask of allowed tiles
    std::vector<float> entropy;   // Shannon-like (approx); we use count as heuristic
    std::mt19937_64 rng;

public:
    explicit WFCSolver(const WFCParams& P, const WFCRules& R)
        : W(P.width), H(P.height), T((int)R.compat.size()), rules(R),
          wave(W*H, (T==64 ? ~0ull : ((1ull<<T)-1ull))), entropy(W*H, 0.f), rng(P.seed) {}

    // Optional: pre-place a tile at (x,y) before running
    bool seed_observation(int x,int y, uint8_t tile){
        if(!inside(x,y)) return false;
        uint64_t m = 1ull<<tile;
        if ((wave[idx(x,y,W)] & m) == 0) return false;
        wave[idx(x,y,W)] = m;
        return propagate_from(x,y);
    }

    WFCResult run(int maxSteps=100000){
        int steps=0;
        while (steps++ < maxSteps){
            auto c = lowest_entropy_cell();
            if (!c) { // done or contradiction
                if (valid_done()) return {true, collapse_all()};
                return {false,{}};
            }
            auto [cx,cy]=*c;
            if (!observe(cx,cy)) return {false,{}};
            if (!propagate_from(cx,cy)) return {false,{}};
        }
        return {false,{}};
    }

private:
    bool inside(int x,int y) const { return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }

    std::optional<std::pair<int,int>> lowest_entropy_cell(){
        int bestX=-1, bestY=-1; int bestCount=INT_MAX; std::uniform_int_distribution<int> coin(0,1);
        for (int y=0;y<H;y++) for (int x=0;x<W;x++){
            uint64_t m = wave[idx(x,y,W)];
            int c = (int)std::popcount(m);
            if (c<=1) continue; // already observed or impossible
            if (c < bestCount || (c==bestCount && coin(rng))) { bestCount=c; bestX=x; bestY=y; }
        }
        if (bestX<0) return std::nullopt;
        return std::make_pair(bestX,bestY);
    }

    bool observe(int x,int y){
        uint64_t m = wave[idx(x,y,W)];
        if (m==0) return false;
        // Weighted random choice among set bits
        float tot=0.f; for (int t=0;t<T;t++) if (m&(1ull<<t)) tot += rules.weight.empty()?1.f:rules.weight[t];
        std::uniform_real_distribution<float> U(0.f, tot);
        float r=U(rng); int pick=0;
        for (int t=0;t<T;t++){
            if (!(m&(1ull<<t))) continue;
            float w = rules.weight.empty()?1.f:rules.weight[t];
            if ((r-=w)<=0.f){ pick=t; break; }
        }
        wave[idx(x,y,W)] = (1ull<<pick);
        return true;
    }

    bool propagate_from(int sx,int sy){
        std::vector<std::pair<int,int>> q; q.emplace_back(sx,sy);
        while(!q.empty()){
            auto [x,y]=q.back(); q.pop_back();
            uint64_t m = wave[idx(x,y,W)];
            if (m==0) return false;
            // for each neighbor, intersect with compatibility union
            for (int d=0; d<4; ++d){
                auto [nx,ny] = step_dir(x,y,(Dir)d);
                if (!inside(nx,ny)) continue;
                uint64_t allowed=0;
                for (int t=0;t<T;t++) if (m&(1ull<<t)) allowed |= rules.compat[t][d];
                uint64_t& nm = wave[idx(nx,ny,W)];
                uint64_t newMask = nm & allowed;
                if (newMask==0) return false; // contradiction
                if (newMask != nm){ nm = newMask; q.emplace_back(nx,ny); }
            }
        }
        return true;
    }

    bool valid_done() const {
        for (auto m: wave) if (m==0) return false;
        return true;
    }
    std::vector<uint8_t> collapse_all() const {
        std::vector<uint8_t> out(W*H,0);
        for (int i=0;i<W*H;i++){
            uint64_t m = wave[i];
            // choose first set bit (already propagated to singletons in practice)
            out[i] = (uint8_t)std::countr_zero(m);
        }
        return out;
    }
};

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// using namespace colony::procgen;
// WFCRules rules = MakeSimpleRuinsRules();
// WFCSolver solver({.width=64,.height=64,.seed=7}, rules);
// auto res = solver.run(200000);
// if (res.success) { /* res.tiles -> tilemap */ }
#endif
