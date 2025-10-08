// src/procgen/RiverAndLakeGen.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <queue>
#include <array>
#include <cmath>
#include <limits>
#include <algorithm>

namespace colony::procgen {

struct River {
    std::vector<std::pair<int,int>> cells; // downstream order
    float discharge = 0.0f;                // accumulation at source
};
struct Lake {
    std::vector<std::pair<int,int>> cells;
    float level = 0.0f; // normalized height
};

struct HydroParams {
    float seaLevel       = 0.0f;  // cells <= seaLevel are ocean
    float minRiverAccum  = 150.0f; // threshold to spawn a visible river
    float maxLakeSlope   = 0.01f;  // consider low-slope high-accum areas as lakes
    int   maxRiverLen    = 10000;  // safety cap
};

struct HydroFields {
    std::vector<uint8_t> dir;    // D8 direction index [0..7], 255 = sink/ocean
    std::vector<float>   accum;  // flow accumulation
};

// D8 neighbor offsets (E, NE, N, NW, W, SW, S, SE)
static inline const int dx8[8] = {+1,+1, 0,-1,-1,-1, 0,+1};
static inline const int dy8[8] = { 0,-1,-1,-1, 0,+1,+1,+1};
static inline float dist8(int k){ return (k%2==0)?1.0f:1.41421356f; }

static inline int idx(int x,int y,int W){ return y*W+x; }
static inline bool inside(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }

// Choose steepest descent neighbor; if none lower, pick the smallest-height neighbor (carve)
static inline uint8_t steepest_descent(const std::vector<float>& h, int x,int y,int W,int H, float& outDrop) {
    const int i = idx(x,y,W);
    float ch = h[i];
    int bestK = -1;
    float bestDelta = 0.0f;
    float minNeighbor = std::numeric_limits<float>::infinity();
    int minK = 0;
    for (int k=0;k<8;k++){
        int nx=x+dx8[k], ny=y+dy8[k];
        if (!inside(nx,ny,W,H)) continue;
        float nh = h[idx(nx,ny,W)];
        if (nh < minNeighbor){ minNeighbor = nh; minK = k; }
        float drop = ch - nh;
        if (drop > bestDelta){ bestDelta = drop; bestK = k; }
    }
    if (bestK >= 0){ outDrop = bestDelta; return (uint8_t)bestK; }
    outDrop = 0.0f;
    return (uint8_t)minK; // carve shallow channel out of depressions
}

// Topological accumulation by processing cells from highest to lowest
static inline HydroFields compute_flow(const std::vector<float>& H, int W,int Hh, const HydroParams& P) {
    HydroFields F{std::vector<uint8_t>(W*Hh,255), std::vector<float>(W*Hh,1.0f)}; // unit rainfall per cell
    std::vector<int> order(W*Hh); // sort indices by height desc
    for (int i=0;i<W*Hh;i++) order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){ return H[a] > H[b]; });

    for (int oi=0;oi<(int)order.size();oi++){
        int i = order[oi];
        int x = i%W, y=i/W;
        if (H[i] <= P.seaLevel){ F.dir[i]=255; continue; } // ocean terminates
        float drop=0.0f;
        uint8_t d = steepest_descent(H,x,y,W,Hh,drop);
        int nx=x+dx8[d], ny=y+dy8[d];
        if (!inside(nx,ny,W,Hh)){ F.dir[i]=255; continue; }
        F.dir[i]=d;
        int j = idx(nx,ny,W);
        F.accum[j] += F.accum[i];
    }
    return F;
}

static inline std::vector<River> extract_rivers(const HydroFields& F, const std::vector<float>& H,
                                                int W,int Hh, const HydroParams& P) {
    std::vector<River> out;
    std::vector<uint8_t> visited(W*Hh,0);
    for (int y=0;y<Hh;y++) for (int x=0;x<W;x++){
        int i=idx(x,y,W);
        if (H[i]<=P.seaLevel) continue;
        if (F.accum[i] < P.minRiverAccum) continue;
        if (visited[i]) continue;

        // Follow downstream to ocean or map edge
        River r; r.discharge = F.accum[i];
        int cx=x, cy=y, steps=0;
        while (inside(cx,cy,W,Hh) && steps++ < P.maxRiverLen){
            int k = idx(cx,cy,W);
            r.cells.emplace_back(cx,cy);
            visited[k]=1;
            uint8_t d = F.dir[k];
            if (d==255) break;
            cx += dx8[d]; cy += dy8[d];
            if (!inside(cx,cy,W,Hh)) break;
            if (H[idx(cx,cy,W)]<=P.seaLevel) { r.cells.emplace_back(cx,cy); break; }
        }
        if (r.cells.size() >= 6) out.push_back(std::move(r));
    }
    return out;
}

static inline std::vector<Lake> infer_lakes(const HydroFields& F, const std::vector<float>& H,
                                            int W,int Hh, const HydroParams& P) {
    std::vector<Lake> lakes;
    std::vector<uint8_t> mark(W*Hh,0);
    auto slopeAt = [&](int x,int y)->float{
        float ch = H[idx(x,y,W)]; float maxS=0.f;
        for (int k=0;k<8;k++){ int nx=x+dx8[k], ny=y+dy8[k]; if (!inside(nx,ny,W,Hh)) continue;
            float s = std::abs(ch - H[idx(nx,ny,W)])/dist8(k); if (s>maxS) maxS=s; }
        return maxS;
    };
    for (int y=0;y<Hh;y++) for (int x=0;x<W;x++){
        int i=idx(x,y,W);
        if (H[i]<=P.seaLevel) continue;
        if (mark[i]) continue;
        if (F.accum[i] > P.minRiverAccum*0.5f && slopeAt(x,y) < P.maxLakeSlope){
            // flood fill plateau
            Lake L; float sumH=0.f;
            std::queue<std::pair<int,int>> q; q.emplace(x,y); mark[i]=1;
            while(!q.empty()){
                auto [cx,cy]=q.front(); q.pop();
                int ci=idx(cx,cy,W);
                L.cells.emplace_back(cx,cy);
                sumH += H[ci];
                for (int k=0;k<8;k++){
                    int nx=cx+dx8[k], ny=cy+dy8[k];
                    if (!inside(nx,ny,W,Hh)) continue;
                    int ni=idx(nx,ny,W);
                    if (mark[ni]) continue;
                    if (F.accum[ni] > P.minRiverAccum*0.4f && slopeAt(nx,ny) < P.maxLakeSlope*1.1f){
                        mark[ni]=1; q.emplace(nx,ny);
                    }
                }
            }
            if (L.cells.size()>=15){ L.level = sumH / L.cells.size(); lakes.push_back(std::move(L)); }
        }
    }
    return lakes;
}

// End-to-end helper
static inline void generate_hydrology(const std::vector<float>& height, int W,int Hh,
                                      const HydroParams& P,
                                      HydroFields& outFields,
                                      std::vector<River>& outRivers,
                                      std::vector<Lake>& outLakes)
{
    outFields = compute_flow(height,W,Hh,P);
    outRivers = extract_rivers(outFields,height,W,Hh,P);
    outLakes  = infer_lakes(outFields,height,W,Hh,P);
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// HydroFields F; std::vector<River> R; std::vector<Lake> L;
// generate_hydrology(height, W,H, HydroParams{0.0f, 120.f, 0.015f}, F,R,L);
#endif
