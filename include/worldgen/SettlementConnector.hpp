#pragma once
// ============================================================================
// SettlementConnector.hpp — auto-route tracks from settlement centers
//  → nearest water access (shoreline) and into RoadNetworkGenerator.
//
// Dependencies: header-only, STL-only. It can optionally include your
// "worldgen/RoadNetworkGenerator.hpp" to route centers into the road network.
// ============================================================================

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

#include "worldgen/RoadNetworkGenerator.hpp" // from previous step

namespace worldgen {

struct I2 { int x=0, y=0; };

struct ConnectorParams {
    int   width=0, height=0;

    // Cost surface (center→shore tracks)
    float slope_weight       = 6.5f;         // cost += slope^2 * slope_weight
    float water_step_penalty = 50.0f;        // large → avoid stepping into water
    float diagonal_cost      = 1.41421356f;  // 8-neigh step cost

    // Post-process (for the short tracks to water)
    float rdp_epsilon        = 0.75f;        // Douglas–Peucker epsilon (cells)
    int   chaikin_refinements= 1;            // 0..2 is typical for short tracks

    // When building hubs from an existing road_mask, sample a subset:
    int   road_hub_stride    = 8;            // pick every Nth road cell as a hub
};

struct Polyline { std::vector<I2> pts; };

struct WaterAccess {
    I2   landing = {0,0};     // land cell adjacent to water that path touches
    I2   nearest_shore = {0,0};
    int  path_len_cells = 0;
    Polyline path;            // center → landing
};

struct ConnectorResult {
    // Shoreline masks
    std::vector<uint8_t> land_shore_mask;    // W*H (land cells touching water)
    std::vector<uint8_t> water_shore_mask;   // W*H (water cells touching land)

    // For each center
    std::vector<WaterAccess> to_water;

    // Output from RoadNetworkGenerator (centers routed into the network)
    RoadResult roads;

    // Convenience: merged mask = short tracks + road network
    std::vector<uint8_t> merged_path_mask;   // W*H
};

// ----------------------------- internals -----------------------------
namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

inline std::vector<float> slope01(const std::vector<float>& h,int W,int H){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[I(x,y,W)] = g; gmax = std::max(gmax, g);
    }
    for(float& v : s) v/=gmax;
    return s;
}

inline void rdp(const std::vector<I2>& in, float eps, std::vector<I2>& out){
    if (in.size()<=2){ out=in; return; }
    struct Seg{int a,b;};
    auto dist = [&](const I2& p,const I2& a,const I2& b)->float{
        float x=p.x,y=p.y, x1=a.x,y1=a.y, x2=b.x,y2=b.y;
        float A=x-x1,B=y-y1,C=x2-x1,D=y2-y1;
        float dot=A*C+B*D, len2=C*C+D*D;
        float t=len2? std::clamp(dot/len2,0.f,1.f) : 0.f;
        float dx=x1+t*C-x, dy=y1+t*D-y;
        return std::sqrt(dx*dx+dy*dy);
    };
    std::vector<char> keep(in.size(),0); keep.front()=keep.back()=1;
    std::vector<Seg> st; st.push_back({0,(int)in.size()-1});
    while(!st.empty()){
        auto [a,b]=st.back(); st.pop_back();
        float md=-1.f; int m=-1;
        for(int i=a+1;i<b;++i){ float d=dist(in[i],in[a],in[b]); if(d>md){md=d;m=i;} }
        if (md>eps){ st.push_back({a,m}); st.push_back({m,b}); keep[(size_t)m]=1; }
    }
    out.clear(); for(size_t i=0;i<in.size();++i) if (keep[i]) out.push_back(in[i]);
}

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

// Build shoreline masks: land cells adjacent to water (land_shore) and the
// corresponding water cells (water_shore) — 8-neighborhood morphological edge.
inline void build_shore_masks(const std::vector<uint8_t>& water,int W,int H,
                              std::vector<uint8_t>& land_shore,
                              std::vector<uint8_t>& water_shore)
{
    land_shore.assign((size_t)W*H,0u);
    water_shore.assign((size_t)W*H,0u);
    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        bool w = (water[i]!=0);
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            size_t j=I(nx,ny,W);
            if (w && !water[j]){ water_shore[i]=1u; land_shore[j]=1u; }
        }
    }
}

// A* to the nearest cell in goalMask (any 1=goal). Heuristic=0 → Dijkstra-safe.
// (Classic A* / least-cost raster routing.)
struct Node { int x,y,dir; float f,g; };
struct QCmp { bool operator()(const Node& a,const Node& b) const { return a.f>b.f; } };

inline bool astar_to_mask(const I2& start,
                          const std::vector<uint8_t>& goalMask,
                          const std::vector<float>& baseCost,
                          const std::vector<uint8_t>* waterMask,
                          const ConnectorParams& P,
                          std::vector<I2>& outPath, I2& reached)
{
    const int W=P.width, H=P.height;
    if (!inb(start.x,start.y,W,H)) return false;

    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    static const float stepC[8]={1,1.41421356f,1,1.41421356f,1,1.41421356f,1,1.41421356f};

    const size_t N=(size_t)W*H;
    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<int>   came(N, -1);
    std::vector<int>   cameDir(N, -1);
    std::priority_queue<Node,std::vector<Node>,QCmp> open;

    size_t si=I(start.x,start.y,W);
    g[si]=0.f;
    open.push(Node{start.x,start.y,-1, /*f*/0.f, /*g*/0.f});

    while(!open.empty()){
        Node cur = open.top(); open.pop();
        size_t ci=I(cur.x,cur.y,W);

        if (goalMask[ci]){
            outPath.clear(); int idx=(int)ci;
            reached = I2{ cur.x, cur.y };
            while (idx!=-1){ int x=idx%W, y=idx/W; outPath.push_back(I2{x,y}); idx=came[(size_t)idx]; }
            std::reverse(outPath.begin(), outPath.end());
            return true;
        }

        for(int k=0;k<8;++k){
            int nx=cur.x+dx[k], ny=cur.y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            size_t ni=I(nx,ny,W);

            float step = stepC[k];
            float base = baseCost[ni];

            float water = 0.f;
            if (waterMask && (*waterMask)[ni]) water += P.water_step_penalty;

            // mild turn smoothing to avoid jaggedness (kept tiny for footpaths)
            float turn = 0.f;
            if (cur.dir!=-1){
                int d = std::abs(k - cur.dir); d = std::min(d, 8-d);
                turn = 0.05f * float(d);
            }

            float tentative = g[ci] + step + base + water + turn;
            if (tentative < g[ni]){
                g[ni] = tentative; came[ni]=(int)ci; cameDir[ni]=k;
                open.push(Node{nx,ny,k, tentative /*+0 heuristic*/, tentative});
            }
        }
    }
    return false;
}

} // namespace detail

// ------------------------------- API ---------------------------------

// Compute tracks from each settlement center to nearest shoreline, then
// route centers into the global road network using your RoadNetworkGenerator.
inline ConnectorResult ConnectSettlementsToWaterAndRoads(
    // terrain & masks
    const std::vector<float>& height01, int W,int H,
    const std::vector<uint8_t>& water_mask,                 // 1=water
    const std::vector<uint8_t>* existing_road_mask = nullptr, // optional
    const std::vector<float>*   river_order01      = nullptr, // optional (0..1)
    // settlements
    const std::vector<I2>& settlement_centers,
    // params
    const ConnectorParams& CP_in = {},
    const RoadParams&      RP_in = {})
{
    ConnectorParams CP = CP_in; CP.width=W; CP.height=H;
    RoadParams RP = RP_in; RP.width=W; RP.height=H;

    ConnectorResult R;
    const size_t N=(size_t)W*H;

    // 1) shoreline masks
    detail::build_shore_masks(water_mask, W,H, R.land_shore_mask, R.water_shore_mask);

    // 2) base slope-derived cost for footpaths
    auto slope = detail::slope01(height01, W,H);
    std::vector<float> baseCost(N, 1.0f);
    for(size_t i=0;i<N;++i) baseCost[i] += CP.slope_weight * (slope[i]*slope[i]);

    // 3) A*: center → nearest shoreline (least-cost path)
    R.to_water.clear(); R.to_water.reserve(settlement_centers.size());
    for (auto c : settlement_centers){
        std::vector<I2> path; I2 reached{c.x,c.y};
        bool ok = detail::astar_to_mask(c, R.land_shore_mask, baseCost, &water_mask, CP, path, reached);
        if (!ok){ R.to_water.push_back(WaterAccess{ reached, reached, 0, Polyline{} }); continue; }

        // simplify & smooth the short track
        std::vector<I2> simp; detail::rdp(path, CP.rdp_epsilon, simp);
        for (int r=0; r<CP.chaikin_refinements; ++r) simp = detail::chaikin_open(simp);

        WaterAccess wa;
        wa.landing = reached;
        wa.nearest_shore = reached;
        wa.path_len_cells = (int)path.size();
        wa.path.pts = std::move(simp);
        R.to_water.push_back(std::move(wa));
    }

    // 4) Build hubs for RoadNetwork: (a) water landings, (b) existing road samples (optional)
    RoadSites sites;
    for (const auto& wa : R.to_water) sites.hubs.push_back(wa.landing);

    if (existing_road_mask){
        int stride = std::max(1, CP.road_hub_stride);
        for (int y=0;y<H;++y) for (int x=0;x<W;++x){
            size_t i=detail::I(x,y,W);
            if ((*existing_road_mask)[i] && ((x+y)%stride==0)) sites.hubs.push_back(I2{x,y});
        }
    }

    // targets are all settlement centers
    sites.targets = settlement_centers;

    // 5) Call your RoadNetworkGenerator to route centers into the network
    R.roads = GenerateRoadNetwork(height01, W,H, sites, RP, /*water*/&water_mask, /*river*/river_order01);

    // 6) Build merged mask (short tracks + road network)
    R.merged_path_mask.assign(N, 0u);
    // seed with roads
    for (auto v : R.roads.road_mask) R.merged_path_mask.push_back(v); // append temp
    if (!R.merged_path_mask.empty()){
        // Fix size if we've wrongly appended; rebuild properly:
        R.merged_path_mask.assign(R.roads.road_mask.begin(), R.roads.road_mask.end());
    }
    // stamp the short tracks
    for (const auto& wa : R.to_water){
        for (const auto& p : wa.path.pts){
            if (detail::inb(p.x,p.y,W,H)) R.merged_path_mask[detail::I(p.x,p.y,W)] = 1u;
        }
    }

    return R;
}

} // namespace worldgen
