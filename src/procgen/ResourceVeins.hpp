// src/procgen/ResourceVeins.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <random>
#include <array>
#include <string>
#include <algorithm>

namespace colony::procgen {

enum class ResourceType : uint8_t { Ore, Stone, Wood, Clay, Herbs };

struct ResourceNode {
    int x=0, y=0;
    ResourceType type;
    float richness=1.0f; // multiplier
};

struct VeinParams {
    float minDist = 18.0f;   // base Poisson radius (pixels/tiles)
    int   k       = 30;      // # of candidates per active point
    float oreBias = 0.6f;    // 0..1 favors mountains (height field high=ore)
    float riverBias = 0.5f;  // 0..1 favors near rivers for clay/herbs/wood
};

// Optional density mask provider
struct DensityMask {
    // return [0,1] density multiplier for (x,y)
    virtual float sample(int x,int y) const = 0;
    virtual ~DensityMask() = default;
};

// default flat density
struct FlatDensity : DensityMask { float sample(int, int) const override { return 1.0f; } };

static inline float frand(std::mt19937_64& rng){ return std::uniform_real_distribution<float>(0.f,1.f)(rng); }

// 2D Bridson Poisson-disk sampling with optional density mask (naive scaling of minDist by 1/sqrt(density))
static inline std::vector<std::pair<int,int>> poisson_disk(int W,int H, uint64_t seed, float minDist, int k, const DensityMask& mask) {
    std::mt19937_64 rng(seed);
    const float cell = minDist / std::sqrt(2.f);
    const int gx = std::max(1, (int)std::ceil(W / cell));
    const int gy = std::max(1, (int)std::ceil(H / cell));
    std::vector<int> grid(gx*gy, -1);
    std::vector<std::pair<int,int>> samples;
    std::vector<std::pair<int,int>> active;

    auto gridIndex = [&](float fx, float fy){
        int ix=(int)(fx/cell), iy=(int)(fy/cell);
        ix = std::clamp(ix,0,gx-1); iy = std::clamp(iy,0,gy-1);
        return iy*gx+ix;
    };
    auto far_enough = [&](float fx,float fy,float r)->bool{
        int gi = gridIndex(fx,fy);
        int ix = gi % gx, iy = gi / gx;
        for (int yy=std::max(0,iy-2); yy<=std::min(gy-1,iy+2); ++yy)
        for (int xx=std::max(0,ix-2); xx<=std::min(gx-1,ix+2); ++xx){
            int si = grid[yy*gx+xx];
            if (si<0) continue;
            float dx = fx - samples[si].first;
            float dy = fy - samples[si].second;
            if ((dx*dx+dy*dy) < r*r) return false;
        }
        return true;
    };

    // initial point
    std::uniform_real_distribution<float> ux(0, (float)W), uy(0,(float)H);
    float sx = ux(rng), sy = uy(rng);
    samples.emplace_back((int)sx,(int)sy);
    grid[gridIndex(sx,sy)] = 0;
    active.emplace_back((int)sx,(int)sy);

    while(!active.empty()){
        std::uniform_int_distribution<size_t> ui(0, active.size()-1);
        auto [ax,ay] = active[ui(rng)];
        bool found=false;

        // variable density: scale radius by local density
        float d = std::clamp(mask.sample(ax,ay), 0.05f, 2.0f);
        float rLocal = minDist / std::sqrt(std::max(0.05f,d));

        for (int t=0;t<k;t++){
            float ang = frand(rng)*6.2831853f;
            float rad = rLocal*(1.f + frand(rng)); // [r,2r)
            float fx = ax + std::cos(ang)*rad;
            float fy = ay + std::sin(ang)*rad;
            if (fx<0||fy<0||fx>=W||fy>=H) continue;
            if (!far_enough(fx,fy,rLocal)) continue;
            samples.emplace_back((int)fx,(int)fy);
            grid[gridIndex(fx,fy)] = (int)samples.size()-1;
            active.emplace_back((int)fx,(int)fy);
            found=true; break;
        }
        if (!found){
            // remove from active
            auto it = std::find(active.begin(), active.end(), std::pair{ax,ay});
            if (it!=active.end()) active.erase(it);
        }
    }
    return samples;
}

// Build resource nodes influenced by height & rivers
static inline std::vector<ResourceNode> generate_resources(int W,int H, uint64_t seed,
                                                           const std::vector<float>& height, // size W*H normalized [-1,1]
                                                           const std::vector<uint8_t>* riverMask, // optional size W*H (1 near river)
                                                           const VeinParams& P=VeinParams{})
{
    struct MaskImpl : DensityMask {
        int W,H; const std::vector<float>& Ht; const std::vector<uint8_t>* R; VeinParams P;
        float sample(int x,int y) const override {
            float h = Ht[y*W+x]; // [-1,1]
            float nearRiver = (R && (*R)[y*W+x]) ? 1.f : 0.f;
            float oreD   = 0.5f + 0.5f*h;             // high mountains
            float riverD = 0.2f + 0.8f*nearRiver;     // by rivers
            // combine
            return (1.0f + P.oreBias*oreD + P.riverBias*riverD)/3.0f;
        }
    } mask{W,H,height,riverMask,P};

    auto pts = poisson_disk(W,H,seed,P.minDist,P.k,mask);

    std::mt19937_64 rng(seed ^ 0xBEEF);
    std::vector<ResourceNode> out; out.reserve(pts.size());
    for (auto [x,y] : pts){
        // classify by local cues
        float h = height[y*W+x];
        bool nearR = riverMask && (*riverMask)[y*W+x];
        ResourceType type = ResourceType::Stone;
        if (h > 0.5f)                 type = ResourceType::Ore;
        else if (nearR)               type = (frand(rng)<0.55f ? ResourceType::Clay : ResourceType::Herbs);
        else if (h < -0.2f)           type = ResourceType::Wood;
        else                          type = (frand(rng)<0.5f ? ResourceType::Stone : ResourceType::Wood);

        float richness = std::clamp(0.6f + 0.8f*frand(rng) + 0.4f*std::max(0.f,h), 0.4f, 2.0f);
        out.push_back(ResourceNode{x,y,type,richness});
    }
    return out;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Usage:
// auto nodes = generate_resources(W,H,123, height, &riverMask);
#endif
