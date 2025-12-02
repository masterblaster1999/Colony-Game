#pragma once
// ============================================================================
// CoastlineGenerator.hpp — header-only coastal classification for Colony-Game
// C++17 / STL only, no external deps.
//
// What it computes (on a 2D grid):
//   • slope01               — normalized slope from the heightmap
//   • waterMask             — derived or provided (height < sea_level)
//   • coast_mask            — land cells adjacent to water (the shoreline belt)
//   • d2water_land          — distance (cells) from land to nearest water
//   • beach_mask            — gently sloped land within a coastal belt
//   • cliff_mask            — steep coastal land (cliffs/headlands)
//   • dune_mask             — low-slope belt landward of beaches (wind-aware)
//   • harbor_mask           — sheltered shoreline cells (low exposure)
//
// Background (concepts; implementation here is original):
//   • Beaches are gently sloping strips along water.              [NatGeo]
//   • Rocky/cliff coasts are steep coastal escarpments.           [USGS/NPS]
//   • Dunes form landward of beaches via wind-trapped sand.       [USACE, PSU]
//   • Distance-to-boundary via multi-source Dijkstra; a faster
//     linear-time distance transform exists (Felzenszwalb–Huttenlocher).
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>
#include <functional>

namespace worldgen {

struct CoastParams {
    int   width = 0, height = 0;

    // Inputs
    const std::vector<float>*  height01    = nullptr; // normalized [0..1]
    const std::vector<uint8_t>* waterMask  = nullptr; // optional: 1 = water
    const std::vector<uint8_t>* solidMask  = nullptr; // optional: 1 = blocked/immutable (currently unused)

    // Sea & units
    float sea_level = 0.50f;                 // [0..1]
    float meters_per_height_unit = 1200.0f;  // for slope normalization

    // Classification thresholds (tune to your map scale)
    float beach_max_inland_dist = 10.0f;   // cells from water for beaches
    float beach_max_slope       = 0.20f;   // gentle slope threshold

    float cliff_band_dist       = 8.0f;    // cells from water to consider "coastal"
    float cliff_min_slope       = 0.50f;   // steep slope threshold

    float dune_start_dist       = 12.0f;   // cells inland from water (start of dune belt)
    float dune_end_dist         = 32.0f;   // cells inland (end of dune belt)
    float dune_max_slope        = 0.25f;   // dunes prefer gentle slopes

    // Prevailing wind (for dune alignment); need not be unit length.
    float wind_dir_x            = 1.0f;    // e.g., onshore wind from ocean -> land
    float wind_dir_y            = 0.0f;

    // Harbor (shelter) detection
    int   harbor_probe_radius   = 20;      // how far to scan on water for exposure
    float harbor_exposure_max   = 0.35f;   // fraction of open-water directions allowed
    float harbor_land_slope_max = 0.30f;   // require gentle land near shore
};

struct CoastResult {
    int width=0, height=0;

    std::vector<float>   slope01;        // size W*H
    std::vector<uint8_t> waterMask;      // derived if not provided
    std::vector<uint8_t> coast_mask;     // shoreline belt (land cells touching water)
    std::vector<float>   d2water_land;   // land distance-to-water (cells)

    std::vector<uint8_t> beach_mask;     // 0/1
    std::vector<uint8_t> cliff_mask;     // 0/1
    std::vector<uint8_t> dune_mask;      // 0/1
    std::vector<uint8_t> harbor_mask;    // 0/1
};

// ----------------------------- Internals -----------------------------

namespace detail {

inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return x>=0 && y>=0 && x<W && y<H; }

inline std::vector<uint8_t> derive_water(const std::vector<float>& h, int W,int H, float sea){
    std::vector<uint8_t> m((size_t)W*H, 0);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x)
        m[idx(x,y,W)] = (h[idx(x,y,W)] < sea) ? 1u : 0u;
    return m;
}

// normalized slope from central differences (scaled by meters_per_height_unit then normalized)
inline std::vector<float> slope01(const std::vector<float>& h, int W,int H, float metersPerUnit){
    std::vector<float> s((size_t)W*H,0.f);
    auto Ht=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[idx(x,y,W)];
    };
    float maxg=1e-6f;
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        float gx = 0.5f*(Ht(x+1,y)-Ht(x-1,y))*metersPerUnit;
        float gy = 0.5f*(Ht(x,y+1)-Ht(x,y-1))*metersPerUnit;
        float g  = std::sqrt(gx*gx+gy*gy);
        s[idx(x,y,W)] = g; if (g>maxg) maxg=g;
    }
    for (float& v : s) v /= maxg; // normalize [0,1]
    return s;
}

// 8-neighbor multi-source Dijkstra distance to a mask (1=candidate sources)
inline std::vector<float> distance_to_mask(const std::vector<uint8_t>& src, int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>; // (dist, index)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

    const int dx[8]={0,1,1,1,0,-1,-1,-1};
    const int dy[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1,1.4142f,1,1.4142f,1,1.4142f,1,1.4142f};

    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if (src[i]) { d[i]=0.f; pq.emplace(0.f,(int)i); }
    }

    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop();
        if (cd>d[i]) continue;
        int x=i%W, y=i/W;
        for (int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k];
            if (!inb(nx,ny,W,H)) continue;
            int j=(int)idx(nx,ny,W);
            float nd = cd + step[k];
            if (nd < d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

// find land cells that touch water (4-neighborhood is crisp for grid-based coastline)
inline std::vector<uint8_t> coast_belt(const std::vector<uint8_t>& water, int W,int H){
    std::vector<uint8_t> c((size_t)W*H,0);
    const int dx4[4]={1,-1,0,0}; const int dy4[4]={0,0,1,-1};
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if (water[i]) continue; // only land cells can be shoreline on the land side
        bool touch=false;
        for (int k=0;k<4;++k){
            int nx=x+dx4[k], ny=y+dy4[k]; if (!inb(nx,ny,W,H)) continue;
            if (water[idx(nx,ny,W)]) { touch=true; break; }
        }
        if (touch) c[i]=1;
    }
    return c;
}

// tiny value noise for dune ridges alignment
inline uint32_t mix32(uint32_t v){ v^=v>>16; v*=0x7feb352dU; v^=v>>15; v*=0x846ca68bU; v^=v>>16; return v; }
inline float hash01(uint32_t h){ return (mix32(h) & 0xFFFFFFu) / float(0xFFFFFFu); }
inline float vnoise(float x,float y,uint32_t seed){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float tx=x-xi, ty=y-yi;
    auto v=[&](int X,int Y){ return hash01(uint32_t(X*73856093u ^ Y*19349663u ^ (int)seed)); };
    auto s=[](float t){ return t*t*(3.f-2.f*t); };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    float a=v00+(v10-v00)*s(tx), b=v01+(v11-v01)*s(tx);
    return a+(b-a)*s(ty);
}

} // namespace detail

// ----------------------------- Entry point -----------------------------

inline CoastResult GenerateCoastline(const CoastParams& P)
{
    const int W=P.width, H=P.height; const size_t N=(size_t)W*H;
    CoastResult out; out.width=W; out.height=H;
    if (W<=1 || H<=1 || !P.height01 || P.height01->size()!=N) return out;

    // 1) water mask & slope
    out.waterMask = P.waterMask ? *P.waterMask
                                : detail::derive_water(*P.height01, W,H, P.sea_level);

    out.slope01   = detail::slope01(*P.height01, W,H, P.meters_per_height_unit);

    // 2) coastline belt (land cells touching water)
    out.coast_mask = detail::coast_belt(out.waterMask, W,H);

    // 3) land distance to water (for belts inland of shore)
    //    (Multi-source Dijkstra; if you need more speed, replace with the linear-time EDT.)
    out.d2water_land = detail::distance_to_mask(out.waterMask, W,H);

    // 4) BEACH: gentle slopes near shore
    out.beach_mask.assign(N,0);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        if (out.waterMask[i]) continue; // land only
        if (out.d2water_land[i] <= P.beach_max_inland_dist &&
            out.slope01[i]      <= P.beach_max_slope)
        {
            out.beach_mask[i]=1;
        }
    }

    // 5) CLIFFS: steep coastal band
    out.cliff_mask.assign(N,0);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        if (out.waterMask[i]) continue;
        if (out.d2water_land[i] <= P.cliff_band_dist &&
            out.slope01[i]      >= P.cliff_min_slope)
        {
            out.cliff_mask[i]=1;
        }
    }

    // 6) DUNES: low-slope belt landward of beach, with wind-aligned texture
    out.dune_mask.assign(N,0);
    // normalize wind
    float wl = std::sqrt(P.wind_dir_x*P.wind_dir_x + P.wind_dir_y*P.wind_dir_y);
    float wx = (wl>1e-6f)? P.wind_dir_x/wl : 1.0f;
    float wy = (wl>1e-6f)? P.wind_dir_y/wl : 0.0f;

    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        if (out.waterMask[i]) continue;

        float d = out.d2water_land[i];
        if (d < P.dune_start_dist || d > P.dune_end_dist) continue;
        if (out.slope01[i] > P.dune_max_slope) continue;

        // wind-aligned ridging: project position onto wind direction and feed a low‑freq noise
        float u = 0.05f*(wx*x + wy*y);           // along-wind coordinate
        float v = 0.05f*(-wy*x + wx*y);          // cross-wind
        float ridges = 0.5f + 0.5f*std::sin(6.28318f*u + 1.7f*detail::vnoise(u*0.7f, v*0.7f, 1337u));
        // distance bandpass weight (stronger near the middle of the dune belt)
        float band = 1.0f - std::abs((d - 0.5f*(P.dune_start_dist+P.dune_end_dist)) /
                                     std::max(1e-6f, 0.5f*(P.dune_end_dist-P.dune_start_dist)));
        if (ridges*band > 0.55f) out.dune_mask[i]=1;
    }

    // 7) HARBORS: shoreline cells (coast_mask) that are sheltered (low exposure)
    //    Exposure = fraction of 16 directions that remain water for most of 'probe_radius' cells.
    out.harbor_mask.assign(N,0);
    const int sx[16]={1,1,0,-1,-1,-1,0,1,  2,2,0,-2,-2,-2,0,2};
    const int sy[16]={0,1,1,1,0,-1,-1,-1,  0,1,2, 1, 0,-1,-2,-1};

    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        if (!out.coast_mask[i]) continue;               // land touching water
        if (out.slope01[i] > P.harbor_land_slope_max) continue;

        int open_dirs=0; int checked=0;
        for (int k=0;k<16;++k){
            ++checked;
            bool open=true;
            int px=x, py=y;
            for (int step=0; step<P.harbor_probe_radius; ++step){
                px += sx[k]; py += sy[k];
                if (!detail::inb(px,py,W,H)) break;
                size_t j=detail::idx(px,py,W);
                if (!out.waterMask[j]) { open=false; break; } // land encountered → sheltered in this direction
            }
            if (open) open_dirs++;
        }
        float exposure = (checked>0)? (open_dirs/(float)checked) : 1.0f;
        if (exposure <= P.harbor_exposure_max) out.harbor_mask[i]=1;
    }

    // Optional: If a cell is classified as cliff, suppress beach there to avoid overlap
    for (size_t i=0;i<N;++i) if (out.cliff_mask[i]) out.beach_mask[i]=0;

    return out;
}

/*
------------------------------------ Usage ------------------------------------

#include "worldgen/CoastlineGenerator.hpp"

void add_coasts(const std::vector<float>& height01, int W, int H) {
    worldgen::CoastParams P;
    P.width=W; P.height=H;
    P.height01=&height01;
    P.sea_level = 0.50f;
    P.beach_max_inland_dist = 10.0f;
    P.beach_max_slope       = 0.18f;
    P.cliff_band_dist       = 8.0f;
    P.cliff_min_slope       = 0.52f;
    P.dune_start_dist       = 12.0f;
    P.dune_end_dist         = 28.0f;
    P.wind_dir_x = 1.0f; P.wind_dir_y = 0.0f; // onshore wind from W→E, for example

    worldgen::CoastResult C = worldgen::GenerateCoastline(P);

    // Rendering / gameplay ideas:
    //  • Paint beach tiles where C.beach_mask=1; dunes (taller grass) where C.dune_mask=1.
    //  • Place cliff meshes or steep materials where C.cliff_mask=1.
    //  • Spawn fishing huts/piers at coast cells where C.harbor_mask=1.
    //  • Use C.coast_mask to lay shoreline decals/foam; shade by C.d2water_land for beach width.
}
*/
} // namespace worldgen
