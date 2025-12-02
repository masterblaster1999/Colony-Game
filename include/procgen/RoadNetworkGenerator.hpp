#pragma once
// ============================================================================
// RoadNetworkGenerator.hpp — terrain-aware roads + bridges for Colony-Game
// C++17 / STL-only, header-only.
//
// Inputs (grid):
//  • height01: W×H normalized height in [0,1]
//  • optional waterMask: W×H (1=water)
//  • optional flowDirD8: W×H D8 codes {0..7} else 255 (no flow)  [for river crossing tags]
//  • optional flowAccum: W×H uint32_t contributing-area values   [for river crossing tags]
//  • settlements: list of (x,y, kind)
//
// Outputs:
//  • polylines: vector of road centerline points (grid coords)
//  • crossings: ford/bridge markers with length & where
//  • debug: cost grid, slope grid
//
// Core ideas (concepts; implementation here is original):
//  • Least-cost path over a raster cost surface (as in GIS "Cost Path/Distance").
//  • Slope-dependent travel cost shaped after Tobler's hiking function.
//  • A* path reconstruction with an admissible Euclidean heuristic.
//  • Kruskal MST on pairwise least-cost distances builds a small, global network.
//  • River crossings identified using D8/flow-accumulation thresholds.
// ----------------------------------------------------------------------------

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>
#include <numeric>

namespace procgen {

struct Settlement {
    int x=0, y=0;
    enum Kind : uint8_t { TOWN=0, HAMLET=1, OUTPOST=2 } kind = TOWN;
};

struct Crossing {
    int x0=0, y0=0, x1=0, y1=0; // inclusive run through water
    int cells=0;                // length in cells
    enum Type : uint8_t { FORD=0, BRIDGE=1 } type = FORD;
};

struct RoadParams {
    // Sea & masks
    float sea_level = 0.50f;          // used if waterMask not provided

    // Cost model (units are relative; only ratios matter)
    float slope_weight   = 2.0f;      // higher → steeper cells much more expensive
    float water_penalty  = 80.0f;     // additive penalty when traversing water cell
    float river_penalty  = 40.0f;     // extra penalty if flowAccum >= river_threshold
    float diagonal_cost  = 1.41421356f;
    float min_cell_cost  = 1.0f;      // keeps A* heuristic admissible

    // River definition (if flowAccum provided)
    uint32_t river_threshold = 150;   // >= → treat as river for penalty/crossing tag

    // Crossing classification
    int ford_max_run   = 2;           // <= cells through water → FORD, else BRIDGE

    // Network
    bool connect_all   = true;        // if false, only connect towns+hamlets
    uint64_t seed      = 0xC0FFEEu;   // reserved for future tie-break randomness
};

struct RoadNetwork {
    int width=0, height=0;
    std::vector<std::vector<std::pair<int,int>>> polylines; // per-road polyline
    std::vector<Crossing> crossings;                        // bridge/ford tags
    std::vector<float> slope01;                             // debug: 0..1
    std::vector<float> cost;                                // debug: per-cell cost
};

// ----------------------------- internals ------------------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

// central-difference slope in normalized units, then rescale to [0,1]
inline std::vector<float> slope01(const std::vector<float>& h,int W,int H){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[I(x,y,W)]=g; gmax=std::max(gmax,g);
    }
    for (float& v : s) v/=gmax;
    return s;
}

// shape reminiscent of Tobler's hiking function: high cost on steep slopes,
// slight preference for mild descent; returns multiplier >= 1
inline float slope_cost(float s01){
    // map [0,1] slope -> "speed" proxy like exp(-k*|m+0.05|)
    // then invert to cost (1/speed). Constants chosen for gameplay feel.
    float m = 1.8f*s01 - 0.9f;                 // crude gradient proxy ~[-0.9..0.9]
    float speed = std::exp(-3.5f*std::fabs(m + 0.05f)); // cf. Tobler form
    speed = std::max(0.15f, speed);            // clamp
    return 1.0f / speed;                       // higher cost on steeper ground
}

// build cost raster
inline std::vector<float> build_cost(const std::vector<float>& h,int W,int H,
                                     const std::vector<uint8_t>* waterMask,
                                     const std::vector<uint32_t>* flowAccum,
                                     const RoadParams& P,
                                     std::vector<float>& outSlope01)
{
    outSlope01 = slope01(h,W,H);
    std::vector<float> c((size_t)W*H, P.min_cell_cost);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        bool water = waterMask ? ((*waterMask)[i]!=0) : (h[i] <= P.sea_level);
        float base = P.min_cell_cost + P.slope_weight * slope_cost(outSlope01[i]);
        if (water) base += P.water_penalty;
        if (flowAccum && (*flowAccum)[i] >= P.river_threshold) base += P.river_penalty;
        c[i] = base;
    }
    return c;
}

// ----------------- A* on 8-neighborhood over a raster cost ------------------

struct Node { int x,y; float f,g; int px,py; }; // px,py: parent
struct NodeCmp { bool operator()(const Node&a,const Node&b) const { return a.f>b.f; } };

inline bool astar(const std::vector<float>& cost,int W,int H,
                  std::pair<int,int> s, std::pair<int,int> t,
                  std::vector<std::pair<int,int>>& outPath,
                  const RoadParams& P)
{
    auto Hidx=[&](int x,int y){ return cost[(size_t)I(x,y,W)]; };
    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    static const float step[8]={1,1.41421356f,1,1.41421356f,1,1.41421356f,1,1.41421356f};

    const size_t N=(size_t)W*H;
    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<int>   parx(N,-1), pary(N,-1), openFlag(N,0), closed(N,0);

    auto Hfun=[&](int x,int y){
        float dx=(float)(t.first-x), dy=(float)(t.second-y);
        float d=std::sqrt(dx*dx+dy*dy);
        return d * P.min_cell_cost; // admissible lower bound
    };

    auto push=[&](int x,int y,float ng,int px,int py, std::priority_queue<Node,std::vector<Node>,NodeCmp>& pq){
        size_t i=I(x,y,W);
        g[i]=ng; parx[i]=px; pary[i]=py;
        float f = ng + Hfun(x,y);
        pq.push(Node{x,y,f,ng,px,py});
        openFlag[i]=1;
    };

    std::priority_queue<Node,std::vector<Node>,NodeCmp> pq;
    push(s.first,s.second, 0.0f, -1,-1, pq);

    while(!pq.empty()){
        Node n = pq.top(); pq.pop();
        size_t ni=I(n.x,n.y,W);
        if (closed[ni]) continue;
        closed[ni]=1;
        if (n.x==t.first && n.y==t.second){
            // reconstruct
            outPath.clear();
            int cx=n.x, cy=n.y;
            while (cx!=-1 && cy!=-1){
                outPath.emplace_back(cx,cy);
                size_t ci=I(cx,cy,W);
                int nx=parx[ci], ny=pary[ci];
                cx=nx; cy=ny;
            }
            std::reverse(outPath.begin(), outPath.end());
            return true;
        }
        for(int k=0;k<8;++k){
            int nx=n.x+dx[k], ny=n.y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            float move = 0.5f*(Hidx(n.x,n.y)+Hidx(nx,ny)) * (k%2? P.diagonal_cost : 1.f);
            float ng = n.g + move;
            size_t ni2=I(nx,ny,W);
            if (closed[ni2] || ng >= g[ni2]) continue;
            push(nx,ny,ng, n.x,n.y,pq);
        }
    }
    return false;
}

// --------------------------- Kruskal MST ------------------------------------

struct Edge { int a,b; float w; };
struct DSU {
    std::vector<int> p, r;
    DSU(int n=0){ reset(n); }
    void reset(int n){ p.resize(n); r.assign(n,0); std::iota(p.begin(),p.end(),0); }
    int find(int x){ return p[x]==x?x:p[x]=find(p[x]); }
    bool unite(int a,int b){ a=find(a); b=find(b); if(a==b) return false; if(r[a]<r[b]) std::swap(a,b); p[b]=a; if(r[a]==r[b]) r[a]++; return true; }
};

inline std::vector<Edge> kruskal(int n, std::vector<Edge> E){
    std::sort(E.begin(),E.end(),[](const Edge&u,const Edge&v){return u.w<v.w;});
    DSU d(n); std::vector<Edge> out; out.reserve(n? (size_t)n-1 : 0);
    for(const auto& e: E) if (d.unite(e.a,e.b)) out.push_back(e);
    return out;
}

} // namespace detail

// ------------------------------- API ----------------------------------------

inline RoadNetwork GenerateRoadNetwork(
    const std::vector<float>& height01, int W, int H,
    const std::vector<Settlement>& settlements,
    const RoadParams& P = {},
    const std::vector<uint8_t>* waterMask = nullptr,
    const std::vector<uint8_t>* flowDirD8 = nullptr,        // optional (for tags)
    const std::vector<uint32_t>* flowAccum = nullptr)        // optional (for tags)
{
    RoadNetwork out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N || settlements.empty()) return out;

    // 1) cost & slope
    out.cost = detail::build_cost(height01, W,H, waterMask, flowAccum, P, out.slope01);

    // 2) choose terminals we will connect
    std::vector<int> idx; idx.reserve(settlements.size());
    for(size_t i=0;i<settlements.size();++i){
        if (P.connect_all) idx.push_back((int)i);
        else if (settlements[i].kind != Settlement::OUTPOST) idx.push_back((int)i); // towns+hamlets
    }
    if (idx.size()<2) return out;

    // 3) pairwise least-cost distances (A*); keep only matrix upper triangle
    struct PairDist { int a,b; float w; std::vector<std::pair<int,int>> path; };
    std::vector<PairDist> PD; PD.reserve(idx.size()*idx.size()/2);

    for(size_t ia=0; ia<idx.size(); ++ia){
        const auto& A = settlements[idx[ia]];
        for(size_t ib=ia+1; ib<idx.size(); ++ib){
            const auto& B = settlements[idx[ib]];
            std::vector<std::pair<int,int>> path;
            bool ok = detail::astar(out.cost, W,H, {A.x,A.y}, {B.x,B.y}, path, P);
            if (!ok) continue; // disconnected domains shouldn't happen on maps, skip
            // path cost = sum of moves we actually took
            float g=0.f;
            for(size_t k=1;k<path.size();++k){
                auto [x0,y0]=path[k-1]; auto [x1,y1]=path[k];
                float step = ((x0!=x1)&&(y0!=y1))? P.diagonal_cost:1.f;
                float c = 0.5f*(out.cost[(size_t)detail::I(x0,y0,W)] + out.cost[(size_t)detail::I(x1,y1,W)]) * step;
                g += c;
            }
            PD.push_back(PairDist{(int)ia,(int)ib,g, std::move(path)});
        }
    }
    if (PD.empty()) return out;

    // 4) Build MST on pairwise least-cost distances to get a minimal network
    std::vector<detail::Edge> edges; edges.reserve(PD.size());
    for (const auto& e : PD) edges.push_back({e.a, e.b, e.w});
    auto mst = detail::kruskal((int)idx.size(), edges);

    // 5) Emit roads for edges in MST (reuse the precomputed paths)
    out.polylines.reserve(mst.size());
    for (const auto& e : mst){
        // find stored path
        auto it = std::find_if(PD.begin(), PD.end(), [&](const PairDist& pd){ return pd.a==e.a && pd.b==e.b; });
        if (it == PD.end()) continue;
        out.polylines.push_back(it->path);

        // scan for water runs to tag fords/bridges
        int run=0; int sx=0, sy=0;
        auto isWater=[&](int x,int y){
            size_t i=detail::I(x,y,W);
            bool w = waterMask ? ((*waterMask)[i]!=0) : (height01[i] <= P.sea_level);
            if (!w) return false;
            if (!flowAccum) return true;
            return (*flowAccum)[i] >= P.river_threshold; // treat only river water for tags
        };
        for(size_t k=0;k<it->path.size();++k){
            auto [x,y]=it->path[k];
            if (isWater(x,y)){
                if (run==0){ sx=x; sy=y; }
                run++;
            }else if (run>0){
                Crossing C; C.x0=sx; C.y0=sy; C.x1=it->path[k-1].first; C.y1=it->path[k-1].second;
                C.cells=run; C.type = (run<=P.ford_max_run)? Crossing::FORD : Crossing::BRIDGE;
                out.crossings.push_back(C);
                run=0;
            }
        }
        if (run>0){
            auto [x,y]=it->path.back();
            Crossing C; C.x0=sx; C.y0=sy; C.x1=x; C.y1=y; C.cells=run; C.type = (run<=P.ford_max_run)? Crossing::FORD : Crossing::BRIDGE;
            out.crossings.push_back(C);
        }
    }

    return out;
}

/*
------------------------------------ Usage ------------------------------------

#include "procgen/RoadNetworkGenerator.hpp"

void build_roads(const std::vector<float>& height01, int W, int H,
                 const std::vector<procgen::Settlement>& settlements,
                 const std::vector<uint8_t>* waterMask,
                 const std::vector<uint8_t>* flowDirD8,
                 const std::vector<uint32_t>* flowAccum)
{
    procgen::RoadParams P;
    P.water_penalty = 80.f;       // strong aversion to water
    P.river_threshold = 150;      // tune to your hydrology (accum cells)
    P.ford_max_run = 2;           // <=2 cells → ford, else bridge

    procgen::RoadNetwork net = procgen::GenerateRoadNetwork(
        height01, W, H, settlements, P, waterMask, flowDirD8, flowAccum);

    // Render:
    //  • Draw 'primary' for town↔town or town↔hamlet (thicker), 'secondary' otherwise
    //  • Place bridge/ford props from net.crossings
    //  • Optionally smooth polylines with Chaikin / Douglas–Peucker before meshing
}
*/
} // namespace procgen
