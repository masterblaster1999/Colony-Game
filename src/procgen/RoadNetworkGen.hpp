// src/procgen/RoadNetworkGen.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <random>
#include <limits>
#include <algorithm>

namespace colony::procgen {

struct RoadNode { float x,y; };
struct RoadEdge { int a,b; };
struct RoadNetwork { std::vector<RoadNode> nodes; std::vector<RoadEdge> edges; };

struct RoadParams {
    float step = 12.0f;          // segment length (tiles/units)
    float snapDist = 9.0f;       // connect to existing node if within this distance
    float maxSlope = 0.12f;      // reject moves above this local slope
    float slopeWeight = 4.0f;    // cost weight for slope
    float waterPenalty = 100.0f; // +cost if move crosses water
    float curveWeight = 0.3f;    // penalize turning too sharply
    int   proposals = 5;         // angles to try each growth step
    int   maxSegments = 8000;    // safety
    uint64_t seed = 1337;
};

struct HeightField { const float* h=nullptr; int W=0,H=0; };
struct MaskField   { const uint8_t* m=nullptr; int W=0,H=0; }; // e.g., water=1

static inline bool inside(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
static inline int idx(int x,int y,int W){ return y*W+x; }
static inline float sample(const HeightField& Hf, float x, float y){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    if (!inside(xi,yi,Hf.W,Hf.H)) return 0.f;
    return Hf.h[idx(xi,yi,Hf.W)];
}
static inline bool water_line(const MaskField& Mf, float x0,float y0,float x1,float y1){
    if (!Mf.m) return false;
    int steps = (int)std::ceil(std::hypot(x1-x0,y1-y0));
    for (int i=0;i<=steps;i++){
        float t = steps? (float)i/steps : 0.f;
        int x=(int)std::round(x0 + (x1-x0)*t);
        int y=(int)std::round(y0 + (y1-y0)*t);
        if (inside(x,y,Mf.W,Mf.H) && Mf.m[idx(x,y,Mf.W)]) return true;
    }
    return false;
}
static inline float local_slope(const HeightField& Hf, float x0,float y0,float x1,float y1){
    float dz = sample(Hf,x1,y1) - sample(Hf,x0,y0);
    float dxy = std::hypot(x1-x0,y1-y0);
    return dxy>1e-5f ? std::abs(dz)/dxy : 0.f;
}

static inline int nearest_node(const RoadNetwork& G, float x,float y, float& outDist2){
    int best=-1; outDist2=std::numeric_limits<float>::max();
    for (int i=0;i<(int)G.nodes.size(); ++i){
        float dx=G.nodes[i].x-x, dy=G.nodes[i].y-y;
        float d2=dx*dx+dy*dy; if (d2<outDist2){ outDist2=d2; best=i; }
    }
    return best;
}

static inline RoadNetwork grow_roads(const std::vector<RoadNode>& seeds,
                                     const HeightField& Hf, const MaskField& water,
                                     const RoadParams& P)
{
    RoadNetwork G; G.nodes = seeds;
    for (int i=1;i<(int)seeds.size();++i) G.edges.push_back({i-1,i}); // optional: connect seeds linearly
    std::vector<int> front; front.reserve(seeds.size());
    for (int i=0;i<(int)seeds.size();++i) front.push_back(i);

    std::mt19937_64 rng(P.seed);
    std::uniform_real_distribution<float> angJit(-0.8f, 0.8f);

    int segments = 0;
    while (!front.empty() && segments < P.maxSegments){
        int fi = front.back(); front.pop_back();
        RoadNode from = G.nodes[fi];

        // approximate previous heading from incident edge (or random if none)
        float prevAng = 0.f; bool havePrev=false;
        for (auto& e: G.edges){
            if (e.b==fi){ havePrev=true; prevAng = std::atan2(from.y - G.nodes[e.a].y, from.x - G.nodes[e.a].x); break; }
            if (e.a==fi){ havePrev=true; prevAng = std::atan2(G.nodes[e.b].y - from.y, G.nodes[e.b].x - from.x); break; }
        }
        if (!havePrev){ prevAng = angJit(rng); }

        // propose several headings around prevAng
        float bestCost = std::numeric_limits<float>::infinity();
        float bestAng = prevAng;
        for (int p=0;p<P.proposals;p++){
            float ang = prevAng + angJit(rng);
            float nx = from.x + std::cos(ang)*P.step;
            float ny = from.y + std::sin(ang)*P.step;

            float slope = local_slope(Hf, from.x,from.y, nx,ny);
            if (slope > P.maxSlope) continue;

            float curve = std::abs(ang - prevAng);
            float cost = P.slopeWeight*slope + P.curveWeight*curve;
            if (water_line(water, from.x,from.y, nx,ny)) cost += P.waterPenalty;

            if (cost < bestCost){ bestCost=cost; bestAng=ang; }
        }
        if (!std::isfinite(bestCost)) continue; // no valid growth here

        float nx = from.x + std::cos(bestAng)*P.step;
        float ny = from.y + std::sin(bestAng)*P.step;

        // snap to existing node if close
        float d2=0.f; int ni = nearest_node(G,nx,ny,d2);
        if (ni>=0 && d2 < P.snapDist*P.snapDist){
            // create junction if not already connected
            if (ni!=fi) G.edges.push_back({fi,ni});
        } else {
            int newId = (int)G.nodes.size();
            G.nodes.push_back({nx,ny});
            G.edges.push_back({fi,newId});
            front.push_back(newId);
        }
        segments++;
    }
    return G;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// HeightField hf{height.data(), W,H}; MaskField water{waterMask.data(), W,H};
// std::vector<RoadNode> towns = {{50,80},{180,60}};
// RoadParams rp; auto network = grow_roads(towns, hf, water, rp);
#endif
