#include "procgen/Rivers.h"
#include <queue>
#include <algorithm>
#include <random>
#include <cmath>

namespace procgen {

static IV2 steepest_descent(const Heightmap& h, int x, int y){
    static const int dx[8]={-1,0,1,-1,1,-1,0,1};
    static const int dy[8]={-1,-1,-1,0,0,1,1,1};
    float best = h.at(x,y);
    int bx=x, by=y;
    for(int k=0;k<8;++k){
        int nx=x+dx[k], ny=y+dy[k];
        if(!in_bounds(nx,ny,h.width,h.height)) continue;
        float v = h.at(nx,ny);
        if (v < best) { best=v; bx=nx; by=ny; }
    }
    return {bx,by};
}

// Return N highest points as candidate sources.
static std::vector<IV2> top_points(const Heightmap& h, int n){
    n = std::max(1, std::min(n, h.width*h.height));
    struct S { float v; int x,y; };
    std::vector<S> A; A.reserve(h.width*h.height);
    for (int y=0;y<h.height;++y) for (int x=0;x<h.width;++x) A.push_back({h.at(x,y),x,y});
    std::nth_element(A.begin(), A.end()-n, A.end(),
        [](const S& a,const S& b){ return a.v < b.v; });
    std::vector<IV2> out;
    for (int i=0;i<n;++i){ auto &s = A[A.size()-1-i]; out.push_back({s.x,s.y}); }
    return out;
}

std::vector<River> generate_rivers(Heightmap& h, float seaLevel, const RiverParams& p){
    // choose candidates from highlands
    int candidates = p.maxRivers*4;
    auto peaks = top_points(h, candidates);

    std::vector<River> rivers; rivers.reserve(p.maxRivers);

    for (IV2 s : peaks){
        if ((int)rivers.size() >= p.maxRivers) break;

        // skip sources too close to sea
        if (h.at(s.x,s.y) < seaLevel + 0.05f) continue;

        River rv;
        rv.path.reserve(1024);
        IV2 cur = s;
        rv.path.push_back(cur);

        for (int step=0; step<p.maxLen; ++step){
            IV2 nxt = steepest_descent(h, cur.x, cur.y);
            if (nxt.x == cur.x && nxt.y == cur.y) break; // pit / plateau

            rv.path.push_back(nxt);

            // Stop at ocean
            if (h.at(nxt.x,nxt.y) <= seaLevel) { cur = nxt; break; }

            cur = nxt;
        }

        if (rv.path.size() > 32) {
            // Carve river bed (simple subtractive carve with Gaussian-ish kernel)
            for (IV2 c : rv.path){
                for (int oy=-1; oy<=1; ++oy){
                    for (int ox=-1; ox<=1; ++ox){
                        int nx=c.x+ox, ny=c.y+oy;
                        if(!in_bounds(nx,ny,h.width,h.height)) continue;
                        float w = (ox==0 && oy==0) ? 1.0f : 0.5f;
                        h.at(nx,ny) = std::max(0.0f, h.at(nx,ny) - p.carveDepth * w);
                    }
                }
            }
            rivers.push_back(std::move(rv));
        }
    }
    h.normalize();
    return rivers;
}

} // namespace procgen
