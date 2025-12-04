#pragma once
// ============================================================================
// RoadNetworkGenerator.hpp — terrain-aware road & trail generator
// For Colony-Game | C++17 / STL-only
// ============================================================================

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <algorithm>  // reverse, sort
#include <limits>
#include <utility>    // std::move

#include "worldgen/Types.hpp"
#include "worldgen/Common.hpp"

namespace worldgen {

struct RoadParams {
    int   width=0, height=0;

    // Cost surface controls
    float slope_weight       = 8.0f;         // cost += slope01^2 * slope_weight
    float diagonal_cost      = 1.41421356f;  // √2 for 8-neigh
    float water_step_penalty = 12.0f;        // entering water cell
    float river_step_weight  = 6.0f;         // multiplied by normalized river order/flow
    float turn_weight        = 0.25f;        // penalty per (|Δdir| * π/4)

    // Bridge detection (based on water mask)
    int   min_bridge_len_cells = 2;          // contiguous water cells to count a bridge
    bool  mark_fords_when_short = true;

    // Post-processing
    float rdp_epsilon        = 0.85f;        // cells; bigger → straighter roads
    int   chaikin_refinements= 2;            // 0..3 is typical
    bool  chaikin_open_paths = true;         // treat polylines as open

    // RNG (only used for minor tie-breaking)
    uint64_t seed = 0xA11E7EADu;
};

struct Bridge {
    I2 entry, exit; // first land→water and last water→land cells along a segment
    I2 mid;         // midpoint (approx)
    int length_cells=0;
    bool likely_ford=false; // true if short crossing
};

struct RoadResult {
    int W=0, H=0;
    std::vector<uint8_t>  road_mask;   // 1 on road cells
    std::vector<Polyline> roads;       // simplified/smoothed
    std::vector<Bridge>   bridges;     // detected crossings
    // Debug helpers:
    std::vector<float> cost_base;      // per-cell base cost (slope/water/river)
    std::vector<float> slope01;
};

// ------------------------------ internals ------------------------------
namespace detail {

inline std::vector<float> slope01(const std::vector<float>& h,int W,int H){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){
        x=std::clamp(x,0,W-1);
        y=std::clamp(y,0,H-1);
        return h[index3(x,y,W)];
    };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[index3(x,y,W)] = g; gmax = std::max(gmax, g);
    }
    for(float& v : s) v/=gmax;
    return s;
}

// Ramer–Douglas–Peucker simplification (open polyline)
inline void rdp(const std::vector<I2>& in, float eps, std::vector<I2>& out){
    if (in.size()<=2){ out = in; return; }
    struct Seg{int a,b;};
    std::vector<char> keep(in.size(), 0);
    keep.front() = keep.back() = 1;

    auto dist = [&](const I2& p, const I2& a, const I2& b)->float{
        float x=p.x, y=p.y, x1=a.x, y1=a.y, x2=b.x, y2=b.y;
        float A=x-x1, B=y-y1, C=x2-x1, D=y2-y1;
        float dot = A*C + B*D;
        float len2 = C*C + D*D;
        float t = len2? std::clamp(dot/len2, 0.f, 1.f) : 0.f;
        float dx = x1 + t*C - x, dy = y1 + t*D - y;
        return std::sqrt(dx*dx+dy*dy);
    };

    std::vector<Seg> stack; stack.push_back({0,(int)in.size()-1});
    while(!stack.empty()){
        auto [a,b] = stack.back(); stack.pop_back();
        float maxd=-1.f; int m=-1;
        for(int i=a+1;i<b;++i){
            float d=dist(in[i], in[a], in[b]);
            if (d>maxd){ maxd=d; m=i; }
        }
        if (maxd>eps){
            stack.push_back({a,m}); stack.push_back({m,b});
            keep[(size_t)m]=1;
        }
    }
    out.clear(); out.reserve(in.size());
    for(size_t i=0;i<in.size();++i) if(keep[i]) out.push_back(in[i]);
}

// One pass of Chaikin (open polyline). Repeat `refinements` times.
inline std::vector<I2> chaikin_open(const std::vector<I2>& in){
    if (in.size()<3) return in;
    std::vector<I2> out; out.reserve(in.size()*2);
    out.push_back(in.front());
    for(size_t i=1;i+1<in.size(); ++i){
        I2 p=in[i], n=in[i+1];
        I2 q{ (int)std::lround(0.75*p.x + 0.25*n.x),
              (int)std::lround(0.75*p.y + 0.25*n.y) };
        I2 r{ (int)std::lround(0.25*p.x + 0.75*n.x),
              (int)std::lround(0.25*p.y + 0.75*n.y) };
        out.push_back(q); out.push_back(r);
    }
    out.push_back(in.back());
    return out;
}

// A* to the nearest cell in a target mask (any 1-cell is a goal).
struct Node { int x,y,dir; float f,g; };
struct QCmp { bool operator()(const Node& a,const Node& b) const { return a.f>b.f; } };

inline bool astar_to_mask(const I2& start,
                          const std::vector<uint8_t>& goalMask,
                          const std::vector<float>& baseCost,
                          const std::vector<uint8_t>* waterMask,
                          const std::vector<float>* river01,
                          const RoadParams& P,
                          std::vector<I2>& outPath)
{
    const int W=P.width, H=P.height;
    if (!inb(start.x,start.y,W,H)) return false;

    static const int dx[8]={ 1, 1, 0,-1,-1,-1, 0, 1};
    static const int dy[8]={ 0, 1, 1, 1, 0,-1,-1,-1};

    auto Hfun = [&](int, int)->float{ return 0.0f; }; // admissible lower bound

    const size_t N=(size_t)W*H;
    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<int>   came(N, -1);
    std::vector<int>   cameDir(N, -1);
    std::priority_queue<Node,std::vector<Node>,QCmp> open;

    size_t si=index3(start.x,start.y,W);
    g[si]=0.f;
    open.push(Node{start.x,start.y,-1, /*f*/0.f, /*g*/0.f});

    while(!open.empty()){
        Node cur = open.top(); open.pop();
        size_t ci=index3(cur.x,cur.y,W);

        if (goalMask[ci]){ // reconstruct
            outPath.clear();
            int idx = (int)ci;
            while (idx!=-1){
                int x = idx%W, y=idx/W;
                outPath.push_back(I2{x,y});
                idx = came[(size_t)idx];
            }
            std::reverse(outPath.begin(), outPath.end()); // two-iterator form. :contentReference[oaicite:1]{index=1}
            return true;
        }

        // --- C4244-SAFE STEP COST (Option A) ---
        auto step_cost = [&](int dir) -> float {
            // diagonals are odd indices: 1,3,5,7
            return (dir & 1) ? P.diagonal_cost : 1.0f;
        };

        for(int k=0;k<8;++k){
            int nx=cur.x+dx[k], ny=cur.y+dy[k];
            if(!inb(nx,ny,W,H)) continue;
            size_t ni=index3(nx,ny,W);

            const float step = step_cost(k);           // float by construction
            const float base = baseCost[ni];

            // water penalty
            float water = 0.0f;
            if (waterMask && (*waterMask)[ni]) water += P.water_step_penalty;

            // river penalty (normalized 0..1)
            const float riv = (river01? (*river01)[ni] : 0.0f) * P.river_step_weight;

            // turn penalty (prefer smooth roads)
            float turn = 0.0f;
            if (cur.dir!=-1){
                int d = std::abs(k - cur.dir); d = std::min(d, 8-d);
                turn = P.turn_weight * static_cast<float>(d);
            }

            const float tentative = g[ci] + step + base + water + riv + turn;

            if (tentative < g[ni]){
                g[ni] = tentative;
                came[ni] = (int)ci;
                cameDir[ni] = k;
                const float f = tentative + Hfun(nx,ny);
                open.push(Node{nx,ny,k,f,tentative});
            }
        }
    }
    return false;
}

} // namespace detail

// --------------------------------- API ---------------------------------

struct RoadSites {
    std::vector<I2> hubs;     // seed nodes (e.g., town center(s)); initialize the network here
    std::vector<I2> targets;  // resources / POIs to connect
};

inline RoadResult GenerateRoadNetwork(
    const std::vector<float>& height01, int W, int H,
    const RoadSites& sites, const RoadParams& P_in = {},
    const std::vector<uint8_t>* water_mask   /*W*H*/ = nullptr,
    const std::vector<float>*   river_order01/*W*H*/ = nullptr)
{
    RoadParams P = P_in; P.width=W; P.height=H;
    RoadResult R; R.W=W; R.H=H;
    const size_t N=(size_t)W*H;
    if ((int)height01.size()!=W*H || W<2 || H<2) return R;

    // 1) Terrain → slope → base cost
    R.slope01 = detail::slope01(height01, W,H);
    R.cost_base.assign(N, 1.0f);
    for(size_t i=0;i<N;++i){
        float s = R.slope01[i];
        R.cost_base[i] += P.slope_weight * (s*s);
        if (water_mask && (*water_mask)[i]) R.cost_base[i] += 0.0f; // keep water penalty separate
        if (river_order01) R.cost_base[i] += 0.0f; // river penalty applied per-step
    }

    // 2) Seed the network mask with hubs (goals for the first routes)
    R.road_mask.assign(N, 0u);
    auto mark = [&](const I2& p){ if(detail::inb(p.x,p.y,W,H)) R.road_mask[detail::index3(p.x,p.y,W)] = 1u; };
    for (auto h : sites.hubs) mark(h);

    // 3) Connect each target to the nearest existing network (A* to goal set)
    auto connect_one = [&](const I2& start){
        std::vector<I2> path;
        bool ok = detail::astar_to_mask(start, R.road_mask, R.cost_base, water_mask, river_order01, P, path);
        if (!ok) return; // unreachable; skip silently

        // Detect bridges/fords and update mask
        bool inWater=false; int wlen=0; I2 entry{};
        for (size_t i=0;i<path.size(); ++i){
            I2 p = path[i];
            size_t pi = detail::index3(p.x,p.y,W);
            R.road_mask[pi]=1u;

            bool water = (water_mask && (*water_mask)[pi]);
            if (water && !inWater){ inWater=true; wlen=1; entry=p; }
            else if (water && inWater){ ++wlen; }
            else if (!water && inWater){ // leaving water
                inWater=false;
                if (wlen >= P.min_bridge_len_cells){
                    I2 exit = p;
                    I2 mid  = path[i - wlen/2];
                    R.bridges.push_back(Bridge{entry, exit, mid, wlen, /*likely_ford*/ wlen <= 3});
                }else if(P.mark_fords_when_short){
                    I2 exit = p;
                    I2 mid  = path[i - std::max(1, wlen/2)];
                    R.bridges.push_back(Bridge{entry, exit, mid, wlen, /*likely_ford*/ true});
                }
            }
        }

        // 4) Simplify & smooth → store as road polyline
        Polyline pl; pl.pts = std::move(path); // <utility> enables this 1-arg move.
        if (P.rdp_epsilon > 0.0f){
            std::vector<I2> simp; detail::rdp(pl.pts, P.rdp_epsilon, simp); pl.pts.swap(simp);
        }
        for (int r=0; r<P.chaikin_refinements; ++r) pl.pts = detail::chaikin_open(pl.pts);

        R.roads.push_back(std::move(pl));
        // extend goal set with the new road cells
        for (const auto& q : R.roads.back().pts) R.road_mask[detail::index3(q.x,q.y,W)] = 1u;
    };

    // If no hub, seed with first target
    if (sites.hubs.empty() && !sites.targets.empty()){
        mark(sites.targets.front());
    }

    // Greedy order: farther targets first reduces redundant segments
    std::vector<I2> targets = sites.targets;
    auto sqrDistToAnyHub = [&](const I2& t){
        long best = std::numeric_limits<long>::max();
        for (auto h : sites.hubs){ long dx=t.x-h.x, dy=t.y-h.y; best = std::min(best, dx*dx+dy*dy); }
        if (sites.hubs.empty()){ best = 0; }
        return best;
    };
    std::sort(targets.begin(), targets.end(),
              [&](const I2& a,const I2& b){ return sqrDistToAnyHub(a) > sqrDistToAnyHub(b); });

    for (const auto& t : targets) connect_one(t);

    return R;
}

} // namespace worldgen
