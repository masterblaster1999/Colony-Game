#include "WfcLayout.hpp"
#include <algorithm>
#include <limits>
#include <cmath>

namespace pcg {

static bool match(const Tile& a, const Tile& b, int dir) {
    // dir: 0=N,1=E,2=S,3=W
    switch (dir) {
        case 0: return (a.north & b.south) != 0;
        case 1: return (a.east  & b.west ) != 0;
        case 2: return (a.south & b.north) != 0;
        default:return (a.west  & b.east ) != 0;
    }
}

static float entropy(const std::vector<int>& poss, const std::vector<Tile>& tiles) {
    // Shannon entropy with tile weights
    float Z=0, ZH=0;
    for (int t : poss) { float w = tiles[t].weight; Z += w; ZH += w*std::log(std::max(1e-6f, w)); }
    return std::log(std::max(1e-6f,Z)) - (ZH / std::max(1e-6f, Z));
}

WfcGrid wfc_generate(const WfcRules& rules, int W, int H, Rng& rng, int maxSteps) {
    WfcGrid g{W,H,{},{}};
    g.possibilities.resize(W*H);
    g.collapsed.assign(W*H, -1);
    std::vector<int> all(rules.tiles.size()); for (int i=0;i<(int)all.size();++i) all[i]=i;
    for (auto& v : g.possibilities) v = all;

    auto idx = [&](int x,int y){return y*W+x;};
    auto inb = [&](int x,int y){return x>=0&&y>=0&&x<W&&y<H;};

    auto observe = [&](){
        // find lowest-entropy cell (ties broken randomly)
        float bestH = std::numeric_limits<float>::infinity(); int bx=-1;
        for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
            int i=idx(x,y);
            if (g.collapsed[i]>=0) continue;
            float e = entropy(g.possibilities[i], rules.tiles);
            // add tiny noise for tiebreaker
            e += (rng.next01f() * 1e-3f);
            if (e < bestH) { bestH = e; bx = i; }
        }
        if (bx<0) return -1; // done
        // pick a tile weighted-random
        auto& poss = g.possibilities[bx];
        float Z=0; for(int t: poss) Z+=rules.tiles[t].weight;
        float r = rng.rangef(0, Z);
        int chosen = poss[0];
        for (int t: poss) { if ((r -= rules.tiles[t].weight) <= 0){ chosen = t; break; } }
        g.collapsed[bx] = chosen;
        poss = {chosen};
        return bx;
    };

    auto propagate = [&](int sx,int sy){
        std::vector<std::pair<int,int>> stack{{sx,sy}};
        while(!stack.empty()){
            auto [x,y] = stack.back(); stack.pop_back();
            int i = idx(x,y);
            for (int d=0; d<4; ++d) {
                int nx = x + (d==1) - (d==3);
                int ny = y + (d==2) - (d==0);
                if (!inb(nx,ny)) continue;
                int j = idx(nx,ny);
                if (g.collapsed[j] >= 0) continue;
                // filter neighbor possibilities by compatibility
                auto& np = g.possibilities[j];
                size_t before = np.size();
                np.erase(std::remove_if(np.begin(), np.end(), [&](int t){
                    // must match at least one tile in i
                    bool ok=false;
                    for (int ti : g.possibilities[i]) if (match(rules.tiles[ti], rules.tiles[t], d)) { ok=true; break; }
                    return !ok;
                }), np.end());
                if (np.empty()) {
                    // contradiction: reset neighbor to all options (simple repair)
                    np = std::vector<int>(rules.tiles.size());
                    for (int k=0;k<(int)np.size();++k) np[k]=k;
                } else if (np.size() != before) {
                    stack.emplace_back(nx,ny);
                }
            }
        }
    };

    int steps=0;
    while (steps++ < maxSteps) {
        int obs = observe();
        if (obs < 0) break; // done
        int x = obs % W, y = obs / W;
        propagate(x,y);
    }
    return g;
}

} // namespace pcg
