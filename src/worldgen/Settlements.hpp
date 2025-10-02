// src/worldgen/Settlements.hpp
// One-file starter: settlements → roads → buildings → rooms (deterministic).
// C++17 / header-only. No external deps. Windows-friendly.
//
// How to use (minimal):
//   #include "Settlements.hpp"
//   using namespace colony::worldgen;
//   Grid2D<float> height(W, H, 0.0f);      // optional
//   Grid2D<uint8_t> water(W, H, 0);        // 1 = water, optional
//   SettlementGenerator gen(/*seed=*/0xC0FFEEuLL);
//   gen.setHeightmap(height);
//   gen.setWaterMask(water);
//   auto plan = gen.generate({W/2, H/2}, /*townRadius=*/64);
//
// Render roads from plan.roads; footprints from plan.buildings; rooms from plan.rooms.
// The plan.tilemap contains rasterized ROAD / BUILDING / ROOM for quick debug.
//
// Notes:
// - Keep coordinates integer grid (tile world).
// - Replace A* cost with your own slope/wetland/population fields as needed.
// - Swap BSP rooms with WFC tiles later if you want style variety.

#pragma once
#include <cstdint>
#include <vector>
#include <queue>
#include <array>
#include <limits>
#include <cmath>
#include <optional>
#include <algorithm>
#include <functional>

namespace colony::worldgen {

// -------------------------------------------------------------
// Small helpers
// -------------------------------------------------------------
template<typename T>
struct Grid2D {
    int w = 0, h = 0;
    std::vector<T> v;

    Grid2D() = default;
    Grid2D(int W, int H, const T& init = T{}) : w(W), h(H), v(size_t(W)*size_t(H), init) {}

    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    T& at(int x, int y) { return v[size_t(y)*size_t(w) + size_t(x)]; }
    const T& at(int x, int y) const { return v[size_t(y)*size_t(w) + size_t(x)]; }
    void fill(const T& t) { std::fill(v.begin(), v.end(), t); }
};

static inline float lerp(float a, float b, float t){ return a + (b-a)*t; }
static inline float clampf(float x, float a, float b){ return x < a ? a : (x > b ? b : x); }

struct IVec2 { int x=0, y=0; };
struct Rect  { int x=0, y=0, w=0, h=0; };

// -------------------------------------------------------------
// Deterministic PCG32 (O'Neill) minimal
// -------------------------------------------------------------
struct Pcg32 {
    uint64_t state = 0x853c49e6748fea9bull;
    uint64_t inc   = 0xda3e39cb94b95bdbull;

    explicit Pcg32(uint64_t seed=0xDEADBEEFCAFEBABEull, uint64_t seq=0x9E3779B97F4A7C15ull){
        state = 0u; inc = (seq<<1u)|1u; nextU32(); state += seed; nextU32();
    }
    uint32_t nextU32(){
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ull + inc;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-int(rot)) & 31));
    }
    float next01(){ return (nextU32() >> 8) * (1.0f/16777216.0f); } // 24-bit mantissa
    int   range(int lo, int hi){ // inclusive lo..hi
        uint32_t span = uint32_t(hi - lo + 1);
        return lo + int(nextU32() % span);
    }
    bool  chance(float p){ return next01() < p; }
};

// -------------------------------------------------------------
// Tiles
// -------------------------------------------------------------
enum : uint8_t {
    TILE_EMPTY    = 0,
    TILE_ROAD     = 1,
    TILE_BUILDING = 2,
    TILE_ROOM     = 3,
    TILE_WATER    = 4,
};

struct RoadSegment { IVec2 a, b; };
struct Building    { Rect box;      };
struct Room        { Rect box;      };

struct SettlementPlan {
    std::vector<RoadSegment> roads;
    std::vector<Rect>        lots;      // pre-building lots (optional, may match buildings)
    std::vector<Building>    buildings;
    std::vector<Room>        rooms;
    Grid2D<uint8_t>          tilemap;   // debug raster (roads/buildings/rooms/water)
};

// -------------------------------------------------------------
// A* pathfinding on 8-connected grid with custom cost
// -------------------------------------------------------------
struct AStarParams {
    float slopePenalty = 20.0f;   // cost multiplier per unit height delta (tune)
    float waterPenalty = 1000.0f; // add this if stepping on water (discourage heavily)
    bool  allowDiag    = true;
};

struct AStarContext {
    const Grid2D<float>*    height = nullptr;    // optional
    const Grid2D<uint8_t>*  water  = nullptr;    // optional (1 = water)
    AStarParams             p;
};

static inline float heightAt(const Grid2D<float>* h, int x, int y){
    if (!h || !h->in(x,y)) return 0.0f; return h->at(x,y);
}
static inline uint8_t waterAt(const Grid2D<uint8_t>* w, int x, int y){
    if (!w || !w->in(x,y)) return 0; return w->at(x,y);
}

static std::vector<IVec2> aStarPath(const AStarContext& ctx, const IVec2 s, const IVec2 g, int W, int H)
{
    struct Node { int x,y; float g,f; int px,py; };
    const float INF = std::numeric_limits<float>::infinity();

    auto idx = [W](int x,int y){ return size_t(y)*size_t(W)+size_t(x); };

    Grid2D<float> gScore(W,H, INF);
    Grid2D<float> fScore(W,H, INF);
    Grid2D<IVec2> parent(W,H, {-1,-1});
    Grid2D<uint8_t> inOpen(W,H, 0), closed(W,H, 0);

    struct QN {
        int x,y; float f;
        bool operator<(const QN& o) const { return f > o.f; } // min-heap
    };
    std::priority_queue<QN> open;

    auto hdist = [&](int x,int y){
        // Octile distance (admissible on 8-connected grid)
        float dx = std::abs(x - g.x), dy = std::abs(y - g.y);
        float dmin = std::min(dx,dy), dmax = std::max(dx,dy);
        return (dmax - dmin) + 1.41421356f * dmin;
    };

    auto stepCost = [&](int x0,int y0,int x1,int y1){
        float base = (x0==x1 || y0==y1) ? 1.0f : 1.41421356f;
        float dh = std::abs(heightAt(ctx.height,x1,y1) - heightAt(ctx.height,x0,y0));
        float slopeCost = ctx.p.slopePenalty * dh;
        float waterCost = waterAt(ctx.water,x1,y1) ? ctx.p.waterPenalty : 0.0f;
        return base + slopeCost + waterCost;
    };

    if (s.x<0||s.x>=W||s.y<0||s.y>=H || g.x<0||g.x>=W||g.y<0||g.y>=H) return {};

    gScore.at(s.x,s.y)=0.0f; fScore.at(s.x,s.y)=hdist(s.x,s.y);
    open.push({s.x,s.y,fScore.at(s.x,s.y)}); inOpen.at(s.x,s.y)=1;

    const std::array<IVec2,8> N8 = {{{+1,0},{-1,0},{0,+1},{0,-1},{+1,+1},{+1,-1},{-1,+1},{-1,-1}}};
    const std::array<IVec2,4> N4 = {{{+1,0},{-1,0},{0,+1},{0,-1}}};

    auto& N = ctx.p.allowDiag ? N8 : N4;

    while(!open.empty()){
        auto cur = open.top(); open.pop();
        if (closed.at(cur.x,cur.y)) continue;
        closed.at(cur.x,cur.y)=1;
        if (cur.x==g.x && cur.y==g.y) break;

        for (auto d : N){
            int nx = cur.x + d.x, ny = cur.y + d.y;
            if (nx<0||ny<0||nx>=W||ny>=H) continue;
            if (closed.at(nx,ny)) continue;
            float tentative = gScore.at(cur.x,cur.y) + stepCost(cur.x,cur.y,nx,ny);
            if (tentative < gScore.at(nx,ny)){
                parent.at(nx,ny) = {cur.x,cur.y};
                gScore.at(nx,ny) = tentative;
                fScore.at(nx,ny) = tentative + hdist(nx,ny);
                open.push({nx,ny,fScore.at(nx,ny)});
                inOpen.at(nx,ny)=1;
            }
        }
    }

    // Reconstruct
    std::vector<IVec2> path;
    if (parent.at(g.x,g.y).x==-1 && !(s.x==g.x&&s.y==g.y)) {
        return path; // fail
    }
    IVec2 cur = g;
    path.push_back(cur);
    while(!(cur.x==s.x && cur.y==s.y)){
        cur = parent.at(cur.x,cur.y);
        if (cur.x==-1) { path.clear(); return path; }
        path.push_back(cur);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Bresenham fallback line if A* fails
static std::vector<IVec2> lineAA(IVec2 a, IVec2 b){
    std::vector<IVec2> out;
    int x0=a.x,y0=a.y,x1=b.x,y1=b.y;
    int dx=std::abs(x1-x0), sx=x0<x1?1:-1;
    int dy=-std::abs(y1-y0), sy=y0<y1?1:-1;
    int err=dx+dy;
    while(true){
        out.push_back({x0,y0});
        if(x0==x1 && y0==y1) break;
        int e2=2*err;
        if(e2>=dy){ err+=dy; x0+=sx; }
        if(e2<=dx){ err+=dx; y0+=sy; }
    }
    return out;
}

// -------------------------------------------------------------
// Rasterization helpers
// -------------------------------------------------------------
static void drawPathAsRoad(Grid2D<uint8_t>& tiles, const std::vector<IVec2>& pts){
    for (auto p : pts) if (tiles.in(p.x,p.y)) tiles.at(p.x,p.y)=TILE_ROAD;
}
static void drawRect(Grid2D<uint8_t>& t, const Rect& r, uint8_t val){
    for(int y=r.y;y<r.y+r.h;y++)
        for(int x=r.x;x<r.x+r.w;x++)
            if (t.in(x,y)) t.at(x,y)=val;
}

// -------------------------------------------------------------
// Blocks & lots: flood non-road area to identify blocks, then split to lots
// -------------------------------------------------------------
static std::vector<Rect> findBlocks(const Grid2D<uint8_t>& t, const Rect& bounds){
    // Identify connected EMPTY regions (4-neighborhood), return bounding rects as blocks.
    std::vector<Rect> blocks;
    Grid2D<uint8_t> vis(t.w,t.h,0);
    const std::array<IVec2,4> N4 = {{{+1,0},{-1,0},{0,+1},{0,-1}}};

    for(int y=bounds.y; y<bounds.y+bounds.h; ++y){
        for(int x=bounds.x; x<bounds.x+bounds.w; ++x){
            if (!t.in(x,y)) continue;
            if (vis.at(x,y)) continue;
            if (t.at(x,y)!=TILE_EMPTY) { vis.at(x,y)=1; continue; }

            // flood
            int minx=x,maxx=x,miny=y,maxy=y;
            std::vector<IVec2> q; q.push_back({x,y}); vis.at(x,y)=1;
            size_t qi=0;
            while(qi<q.size()){
                auto c=q[qi++]; 
                minx=std::min(minx,c.x); maxx=std::max(maxx,c.x);
                miny=std::min(miny,c.y); maxy=std::max(maxy,c.y);
                for(auto d:N4){
                    int nx=c.x+d.x, ny=c.y+d.y;
                    if (!t.in(nx,ny) || vis.at(nx,ny)) continue;
                    if (nx<bounds.x || ny<bounds.y || nx>=bounds.x+bounds.w || ny>=bounds.y+bounds.h){ vis.at(nx,ny)=1; continue; }
                    if (t.at(nx,ny)==TILE_EMPTY){ vis.at(nx,ny)=1; q.push_back({nx,ny}); }
                    else vis.at(nx,ny)=1;
                }
            }
            Rect R{minx,miny, maxx-minx+1, maxy-miny+1};
            if (R.w>=4 && R.h>=4) blocks.push_back(R);
        }
    }
    return blocks;
}

// Simple axis-aligned subdivision of a block into lot rectangles
static void splitBlockIntoLots(const Rect& block, int minLot, int maxLot, Pcg32& rng, std::vector<Rect>& lotsOut){
    // Recursive split until both dimensions under maxLot; ensure >= minLot
    std::function<void(Rect,int)> rec = [&](Rect r, int depth){
        if (r.w <= maxLot && r.h <= maxLot && r.w >= minLot && r.h >= minLot){ lotsOut.push_back(r); return; }
        bool splitVert = (r.w > r.h);
        if (r.w >= 2*minLot && r.h >= 2*minLot){
            // choose split orientation with slight randomness
            if (rng.chance(0.3f)) splitVert = !splitVert;
        } else if (r.w >= 2*minLot) splitVert = true;
          else if (r.h >= 2*minLot) splitVert = false;
          else { lotsOut.push_back(r); return; }

        if (splitVert){
            int smin = r.x + minLot;
            int smax = r.x + r.w - minLot;
            if (smax<=smin){ lotsOut.push_back(r); return; }
            int sx = rng.range(smin, smax);
            Rect A{r.x, r.y, sx - r.x, r.h};
            Rect B{sx,  r.y, r.x + r.w - sx, r.h};
            rec(A, depth+1); rec(B, depth+1);
        }else{
            int smin = r.y + minLot;
            int smax = r.y + r.h - minLot;
            if (smax<=smin){ lotsOut.push_back(r); return; }
            int sy = rng.range(smin, smax);
            Rect A{r.x, r.y, r.w, sy - r.y};
            Rect B{r.x, sy, r.w, r.y + r.h - sy};
            rec(A, depth+1); rec(B, depth+1);
        }
    };
    rec(block, 0);
}

// -------------------------------------------------------------
// Rooms via BSP inside a building footprint (classic roguelike BSP)
// -------------------------------------------------------------
static void splitRoomsBSP(const Rect& building, int minRoom, int maxRoom, Pcg32& rng, std::vector<Rect>& roomsOut){
    std::function<void(Rect,int)> rec = [&](Rect r, int depth){
        // leave a 1-tile interior margin for walls/corridors if desired
        Rect inner{r.x+1, r.y+1, r.w-2, r.h-2};
        if (inner.w < minRoom || inner.h < minRoom){ return; }

        bool canSplitW = inner.w >= 2*minRoom;
        bool canSplitH = inner.h >= 2*minRoom;
        bool doSplit = canSplitW || canSplitH;
        bool splitVert = canSplitW && (inner.w >= inner.h);

        if (doSplit && (inner.w > maxRoom || inner.h > maxRoom || rng.chance(0.6f))){
            if (!canSplitW) splitVert=false;
            if (!canSplitH) splitVert=true;

            if (splitVert){
                int smin = inner.x + minRoom;
                int smax = inner.x + inner.w - minRoom;
                int sx = rng.range(smin, smax);
                Rect A{r.x, r.y, (sx - r.x)+1, r.h};
                Rect B{sx,  r.y, (r.x + r.w - sx), r.h};
                rec(A, depth+1); rec(B, depth+1);
            }else{
                int smin = inner.y + minRoom;
                int smax = inner.y + inner.h - minRoom;
                int sy = rng.range(smin, smax);
                Rect A{r.x, r.y, r.w, (sy - r.y)+1};
                Rect B{r.x, sy, r.w, (r.y + r.h - sy)};
                rec(A, depth+1); rec(B, depth+1);
            }
        } else {
            // Emit one room occupying the inner rect
            roomsOut.push_back(inner);
        }
    };
    rec(building, 0);
}

// -------------------------------------------------------------
// Generator params + class
// -------------------------------------------------------------
struct SettlementParams {
    // Town footprint
    int   townRadius         = 64;     // approx. radius (tiles)
    Rect  boundsOverride     = {0,0,0,0}; // optional explicit bounds (w/h>0)

    // Path cost tuning
    AStarParams aStar;

    // Lots & buildings
    int   minLot             = 6;
    int   maxLot             = 16;
    int   buildingInset      = 1;      // shrink lot by this to get building
    float lotOccupancy       = 0.8f;   // chance to build on a lot

    // Rooms
    int   minRoom            = 4;
    int   maxRoom            = 10;
};

class SettlementGenerator {
public:
    explicit SettlementGenerator(uint64_t seed = 0xA17B4D9C13ull) : rng(seed) {}

    void setHeightmap(const Grid2D<float>& h){ height = &h; }
    void setWaterMask(const Grid2D<uint8_t>& w){ water = &w; }

    SettlementPlan generate(IVec2 center, int townRadius) {
        SettlementParams p; p.townRadius = townRadius;
        return generate(center, p);
    }

    SettlementPlan generate(IVec2 center, const SettlementParams& P){
        SettlementPlan out;
        // Determine bounds
        Rect bounds;
        if (P.boundsOverride.w>0 && P.boundsOverride.h>0) bounds = P.boundsOverride;
        else bounds = {center.x - P.townRadius, center.y - P.townRadius, P.townRadius*2+1, P.townRadius*2+1};

        // Clamp to available maps if provided
        int W = bounds.w, H = bounds.h;
        out.tilemap = Grid2D<uint8_t>(W,H, TILE_EMPTY);

        // Mark water (debug) if mask provided
        if (water){
            for(int y=0;y<H;y++) for(int x=0;x<W;x++){
                int wx = bounds.x + x, wy = bounds.y + y;
                if (water->in(wx,wy) && water->at(wx,wy)) out.tilemap.at(x,y)=TILE_WATER;
            }
        }

        // --- 1) Primary roads: 4 gates (N,S,E,W) to center using A* ---
        auto clampToBounds = [&](IVec2 p)->IVec2 {
            p.x = std::max(bounds.x, std::min(bounds.x + bounds.w - 1, p.x));
            p.y = std::max(bounds.y, std::min(bounds.y + bounds.h - 1, p.y));
            return p;
        };
        IVec2 C = clampToBounds(center);
        std::array<IVec2,4> gates = {{
            {center.x, center.y - P.townRadius},
            {center.x, center.y + P.townRadius},
            {center.x + P.townRadius, center.y},
            {center.x - P.townRadius, center.y},
        }};
        for(auto& g : gates) g = clampToBounds(g);

        AStarContext actx; actx.height = height; actx.water = water; actx.p = P.aStar;

        auto toLocal = [&](IVec2 w)->IVec2 { return {w.x - bounds.x, w.y - bounds.y}; };

        std::vector<std::vector<IVec2>> roadPaths;
        roadPaths.reserve(gates.size());

        for(auto G : gates){
            auto path = aStarPath(actx, toLocal(G), toLocal(C), W,H);
            if (path.empty()) path = lineAA(toLocal(G), toLocal(C));
            roadPaths.push_back(std::move(path));
        }

        for (auto& pth : roadPaths){
            drawPathAsRoad(out.tilemap, pth);
            for (size_t i=1;i<pth.size();++i){
                RoadSegment seg{{pth[i-1].x + bounds.x, pth[i-1].y + bounds.y},
                                {pth[i].x   + bounds.x, pth[i].y   + bounds.y}};
                out.roads.push_back(seg);
            }
        }

        // --- 2) Blocks & lots near roads ---
        // Expand road thickness a bit for nicer blocks
        auto thicken = [&](int r){
            if (r<=0) return;
            Grid2D<uint8_t> copy = out.tilemap;
            for(int y=0;y<H;y++) for(int x=0;x<W;x++) if (copy.at(x,y)==TILE_ROAD){
                for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
                    int nx=x+dx, ny=y+dy; if (out.tilemap.in(nx,ny)) if (std::abs(dx)+std::abs(dy)<=r) if (out.tilemap.at(nx,ny)==TILE_EMPTY) out.tilemap.at(nx,ny)=TILE_ROAD;
                }
            }
        };
        thicken(1);

        // Blocks are empty spaces within bounds not occupied by roads/water
        auto blocks = findBlocks(out.tilemap, {0,0,W,H});

        // Lots: split each block into lot rectangles
        std::vector<Rect> lotsLocal;
        for (auto& b : blocks) splitBlockIntoLots(b, P.minLot, P.maxLot, rng, lotsLocal);

        // Save lots (world coords)
        for (auto L : lotsLocal){
            out.lots.push_back({L.x + bounds.x, L.y + bounds.y, L.w, L.h});
        }

        // --- 3) Buildings from lots (occupancy) ---
        for (auto L : lotsLocal){
            if (!rng.chance(P.lotOccupancy)) continue;
            Rect B{
                L.x + P.buildingInset,
                L.y + P.buildingInset,
                std::max(0, L.w - 2*P.buildingInset),
                std::max(0, L.h - 2*P.buildingInset)
            };
            if (B.w >= P.minLot-2 && B.h >= P.minLot-2){
                out.buildings.push_back({ {B.x + bounds.x, B.y + bounds.y, B.w, B.h} });
                drawRect(out.tilemap, B, TILE_BUILDING);
            }
        }

        // --- 4) Rooms inside each building (BSP) ---
        for (auto& b : out.buildings){
            Rect localB{ b.box.x - bounds.x, b.box.y - bounds.y, b.box.w, b.box.h };
            std::vector<Rect> rms;
            splitRoomsBSP(localB, P.minRoom, P.maxRoom, rng, rms);
            for (auto& r : rms){
                out.rooms.push_back({ {r.x + bounds.x, r.y + bounds.y, r.w, r.h} });
                drawRect(out.tilemap, r, TILE_ROOM);
            }
        }

        return out;
    }

private:
    const Grid2D<float>*   height = nullptr;
    const Grid2D<uint8_t>* water  = nullptr;
    Pcg32 rng;
};

} // namespace colony::worldgen
