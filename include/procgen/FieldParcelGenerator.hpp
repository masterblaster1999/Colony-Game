#pragma once
// ============================================================================
// FieldParcelGenerator.hpp — header-only farmland parcel generator for grids
// For Colony-Game (C++17 / STL-only)
//
// INPUTS
//  • height01: W×H float array in [0,1] (sea in [0, sea_level])
//  • Optional fertility/rainfall: can bias where arable land appears (defaults OK)
//
// OUTPUTS
//  • parcel_id: int per cell,  -1 for non-arable, [0..num_parcels-1] otherwise
//  • centroids: parcel centers (x,y) in grid coords
//  • areas:     cell counts per parcel
//  • edge_mask: 0/1 grid for “hedgerows/fences” along parcel borders
//
// Techniques (concepts; code here is original):
//  • Weighted Voronoi (power diagram) controls average region size via weights
//  • Lloyd (CVT) relaxation moves seeds to region centroids for tidier shapes
//    (See Aurenhammer on power diagrams; Du–Faber–Gunzburger on CVT.)
// ----------------------------------------------------------------------------

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

namespace procgen {

struct FieldParams {
    int      width = 0, height = 0;
    float    sea_level = 0.50f;       // normalized water line
    float    max_slope = 0.30f;       // arable if local slope <= this (0..~1)
    int      target_parcels = 180;    // fields to seed (512² map: ~120–240)
    uint32_t seed = 0xC0FFEEu;

    // Seeding & shaping
    float    min_spacing   = 14.0f;   // base min spacing between parcel seeds (cells)
    float    max_spacing   = 24.0f;   // upper bound for variable spacing
    float    spacing_gamma = 0.5f;    // <1 packs seeds more in good land
    int      lloyd_iters   = 2;       // 0–3 recommended
    int      smooth_border_passes = 1;// majority-filter passes on raster borders

    // Parcel size control (power weights): >1 → larger average parcels
    float    weight_scale = 1.0f;     // global multiplier
    float    min_weight   = 0.6f;     // clamp to keep range sane
    float    max_weight   = 1.8f;

    // Edge mask / hedgerow thickness (in cells; visualize borders)
    int      edge_thickness = 1;

    // Arability mixing (if you provide a fertility layer)
    float    fertility_bias = 0.5f;   // 0..1, how much fertility influences arable score
};

struct FieldParcels {
    int width=0, height=0, num_parcels=0;

    std::vector<int>         parcel_id;   // size W*H; -1 for non-arable
    std::vector<uint8_t>     edge_mask;   // size W*H; 1 where borders/hedgerows
    std::vector<std::pair<float,float>> centroids; // per parcel
    std::vector<int>         areas;       // per parcel
};

namespace detail {

inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

// ---------- slope & helpers ----------
inline std::vector<float> slope_grid(const std::vector<float>& h,int W,int H){
    std::vector<float> s((size_t)W*H,0.0f);
    auto Ht=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[idx(x,y,W)];
    };
    float maxv=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Ht(x+1,y)-Ht(x-1,y));
        float gy=0.5f*(Ht(x,y+1)-Ht(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[idx(x,y,W)]=g; if(g>maxv) maxv=g;
    }
    for(float& v : s) v/=maxv; // normalize to [0,1]
    return s;
}

// ---------- tiny value-noise (for variable spacing) ----------
inline uint32_t hash32(uint32_t x){ x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float hash01(uint32_t x){ return (hash32(x)&0xFFFFFFu)/16777215.0f; }
inline float value2(float x,float y,uint32_t seed){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float tx=x-xi, ty=y-yi;
    auto v=[&](int X,int Y){ return hash01(hash32((uint32_t)(X*73856093u ^ Y*19349663u ^ seed))); };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    float i0=v00 + (v10-v00)*tx;
    float i1=v01 + (v11-v01)*tx;
    return i0 + (i1-i0)*ty;
}

// ---------- power diagram assignment (weighted Voronoi) ----------
struct Seed { int x=0,y=0; float w=1.0f; }; // weight controls parcel size
inline std::vector<int> assign_power(const std::vector<Seed>& S,int W,int H,
                                     const std::vector<uint8_t>& arable)
{
    const size_t N=(size_t)W*H; std::vector<int> owner(N,-1);
    std::vector<float> best(N, std::numeric_limits<float>::infinity());
    for(size_t si=0; si<S.size(); ++si){
        const Seed& s=S[si]; const float wsq=s.w*s.w;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            size_t i=idx(x,y,W); if(!arable[i]) continue;
            float dx=float(x-s.x), dy=float(y-s.y);
            float d=dx*dx+dy*dy - wsq;
            if(d<best[i]){ best[i]=d; owner[i]=(int)si; }
        }
    }
    return owner;
}

// ---------- centroid / area ----------
inline void recompute_centroids(const std::vector<int>& owner,int W,int H,
                                int S, std::vector<std::pair<float,float>>& C,
                                std::vector<int>& A)
{
    C.assign((size_t)S,{0,0}); A.assign((size_t)S,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        int o=owner[idx(x,y,W)]; if(o<0) continue;
        C[(size_t)o].first += (float)x; C[(size_t)o].second += (float)y; A[(size_t)o]++;
    }
    for(int i=0;i<S;++i) if(A[(size_t)i]>0){
        C[(size_t)i].first /= (float)A[(size_t)i];
        C[(size_t)i].second/= (float)A[(size_t)i];
    }
}

// ---------- raster border / hedgerow mask ----------
inline std::vector<uint8_t> make_edge_mask(const std::vector<int>& owner,int W,int H,int thick){
    std::vector<uint8_t> m((size_t)W*H,0);
    auto mark=[&](int x,int y){ if(inb(x,y,W,H)) m[idx(x,y,W)]=1; };
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        int o=owner[idx(x,y,W)]; if(o<0) continue;
        bool edge=false;
        for(int dy=-1; dy<=1 && !edge; ++dy)
        for(int dx=-1; dx<=1 && !edge; ++dx){
            if(dx==0 && dy==0) continue;
            int nx=x+dx, ny=y+dy; if(!inb(nx,ny,W,H)) continue;
            if(owner[idx(nx,ny,W)]!=o) edge=true;
        }
        if(edge){
            for(int ry=-thick; ry<=thick; ++ry)
            for(int rx=-thick; rx<=thick; ++rx)
                if(std::max(std::abs(rx),std::abs(ry))<=thick) mark(x+rx,y+ry);
        }
    }
    return m;
}

// ---------- tiny majority filter to tidy jaggies ----------
inline void majority_filter(std::vector<int>& owner,int W,int H){
    std::vector<int> out = owner;
    for(int y=1;y<H-1;++y) for(int x=1;x<W-1;++x){
        int i=idx(x,y,W); int o=owner[i]; if(o<0) continue;
        int cnt=0;
        for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx){
            if(dx==0&&dy==0) continue; if(owner[idx(x+dx,y+dy,W)]==o) cnt++;
        }
        if(cnt <= 3){ // few neighbors with same id → smooth to strongest neighbor
            int bestId=o, bestCount=0;
            for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx){
                if(dx==0&&dy==0) continue;
                int idn = owner[idx(x+dx,y+dy,W)]; if(idn<0) continue;
                int c=0; for(int sy=-1; sy<=1; ++sy) for(int sx=-1; sx<=1; ++sx)
                    if(inb(x+dx+sx,y+dy+sy,W,H) && owner[idx(x+dx+sx,y+dy+sy,W)]==idn) c++;
                if(c>bestCount){ bestCount=c; bestId=idn; }
            }
            out[i]=bestId;
        }
    }
    owner.swap(out);
}

} // namespace detail

// ----------------------------------------------------------------------------
// MAIN: Generate field parcels from height + (optional) fertility
// ----------------------------------------------------------------------------
inline FieldParcels GenerateFieldParcels(
    const std::vector<float>& height01, int W, int H,
    const FieldParams& P = {},
    const std::vector<float>* fertility01 /*optional, 0..1*/ = nullptr)
{
    FieldParcels out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if(W<=1 || H<=1 || height01.size()!=N) return out;

    // 1) Find arable cells from slope + land (+ optional fertility)
    auto slope = detail::slope_grid(height01, W, H);
    std::vector<uint8_t> arable(N,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        bool land = (height01[i] > P.sea_level);
        float s   = slope[i];
        float fert = fertility01 ? (*fertility01)[i] : 1.0f;
        float score = (1.0f - s) * (0.5f + P.fertility_bias * (fert - 0.5f));
        arable[i] = (land && s <= P.max_slope && score > 0.2f) ? 1 : 0;
    }
    int arableCount = (int)std::count(arable.begin(), arable.end(), (uint8_t)1);
    if (arableCount == 0) { out.parcel_id.assign(N,-1); out.edge_mask.assign(N,0); return out; }

    // 2) Seed parcels with blue-noise-like spacing; bias to more arable tiles
    std::mt19937 rng(P.seed);
    std::uniform_int_distribution<int> UX(0,W-1), UY(0,H-1);

    auto noise = [&](int x,int y){ return detail::value2(x*0.07f, y*0.07f, P.seed); };
    auto spacing_at = [&](int x,int y){
        float t = noise(x,y); // 0..1
        float r = P.min_spacing + (P.max_spacing - P.min_spacing) * (1.0f - std::pow(t, P.spacing_gamma));
        return std::clamp(r, P.min_spacing, P.max_spacing);
    };

    std::vector<detail::Seed> seeds; seeds.reserve(P.target_parcels);
    std::vector<std::pair<float,float>> placed;  placed.reserve(P.target_parcels);
    std::vector<float>                   placedR; placedR.reserve(P.target_parcels);

    int attempts = P.target_parcels * 300;
    while((int)seeds.size() < P.target_parcels && attempts-- > 0){
        int x = UX(rng), y = UY(rng); size_t i=detail::idx(x,y,W);
        if(!arable[i]) continue;

        float r = spacing_at(x,y);

        bool ok=true;
        for(size_t k=0;k<placed.size();++k){
            float dx=x-placed[k].first, dy=y-placed[k].second;
            float req = std::max(r, placedR[k]);              // variable-radius separation
            if(dx*dx+dy*dy < req*req){ ok=false; break; }
        }
        if(!ok) continue;

        float w = std::clamp(P.weight_scale * (0.8f + 0.6f*noise(x,y)), P.min_weight, P.max_weight);
        seeds.push_back({x,y,w});
        placed.emplace_back((float)x,(float)y);
        placedR.push_back(r);
    }
    if (seeds.empty()) { out.parcel_id.assign(N,-1); out.edge_mask.assign(N,0); return out; }

    // 3) Power-diagram assignment → parcel ownership
    std::vector<int> owner = detail::assign_power(seeds, W, H, arable);

    // 4) Lloyd relaxation (move seeds to centroids, then reassign)
    std::vector<std::pair<float,float>> centroids; std::vector<int> areas;
    for(int it=0; it<P.lloyd_iters; ++it){
        detail::recompute_centroids(owner, W, H, (int)seeds.size(), centroids, areas);
        for(size_t si=0; si<seeds.size(); ++si){
            if(areas[si] > 0){
                seeds[si].x = (int)std::round(centroids[si].first);
                seeds[si].y = (int)std::round(centroids[si].second);
            }
        }
        owner = detail::assign_power(seeds, W, H, arable);
    }

    // 5) Mild border smoothing
    for(int pass=0; pass<P.smooth_border_passes; ++pass) detail::majority_filter(owner, W, H);

    // 6) Pack outputs
    out.num_parcels = (int)seeds.size();
    out.parcel_id.assign(N, -1);
    for(size_t i=0;i<N;++i) out.parcel_id[i] = owner[i];

    detail::recompute_centroids(owner, W, H, (int)seeds.size(), out.centroids, out.areas);
    out.edge_mask = detail::make_edge_mask(owner, W, H, std::max(0,P.edge_thickness));

    return out;
}

/*
-------------------------------- Usage (drop-in) --------------------------------

#include "procgen/FieldParcelGenerator.hpp"

void generate_farmland(const std::vector<float>& height01, int W, int H,
                       const std::vector<float>* fertility01 /*optional*/)
{
    procgen::FieldParams P;
    P.width = W; P.height = H;
    P.sea_level = 0.50f;    // tune to your world
    P.max_slope = 0.28f;    // flatter land becomes arable
    P.target_parcels = std::max(120, (W*H)/5000); // rough scale for 512²–1024²

    procgen::FieldParcels fp = procgen::GenerateFieldParcels(height01, W, H, P, fertility01);

    // Use:
    //  • fp.parcel_id  : paint plots (field overlay), assign ownership, job zones
    //  • fp.edge_mask  : draw hedges/fences/paths; tweak walk speed along edges
    //  • fp.centroids  : seed farmhouses/barns or crop rotation controllers
    //  • fp.areas      : compute yields / work quotas per field
}
*/
} // namespace procgen
