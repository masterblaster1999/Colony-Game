#include "ErosionCPU.hpp"
#include <cmath>
#include <algorithm>

namespace colony::terrain {

// Helpers
static inline int idx(int x,int y,int w){ return y*w + x; }
static inline float clampf(float v,float a,float b){ return std::min(std::max(v,a),b); }

void HydraulicErodeCPU(Heightfield& H, const HydraulicParams& p)
{
    const int W = H.width(), Hh = H.height();
    const int N = W * Hh;

    std::vector<float> water(N, 0.0f);
    std::vector<float> sed  (N, 0.0f);
    std::vector<float> flux (N*4, 0.0f); // 0:+x(right),1:-x(left),2:-y(up),3:+y(down)

    // tiny dithering in rainfall to break symmetry, deterministic via PCG
    PCG32 rng; rng.seed(p.seed ? p.seed : 0xC01OnyULL, 0x9E3779B97F4A7C15ULL);

    auto height = [&](int x, int y)->float& { return H.at(x,y); };
    auto getHeight = [&](int x,int y)->float {
        x = std::clamp(x,0,W-1); y = std::clamp(y,0,Hh-1);
        return H.at(x,y);
    };

    for (int it=0; it<p.iterations; ++it)
    {
        // 1) Add rainfall (with light PRNG jitter for visually nicer patterns)
        for (int y=0;y<Hh;++y){
            for (int x=0;x<W;++x){
                float jitter = (rng.next() & 0xFFu) * (1.0f/255.0f) * 0.25f + 0.875f; // [0.875,1.125)
                water[idx(x,y,W)] += p.rainfall * jitter;
            }
        }

        // 2) Compute outflow fluxes via "virtual pipes" to 4 neighbors.
        //    Limit total outflow to available water.
        for (int y=0;y<Hh;++y){
            for (int x=0;x<W;++x){
                const int i = idx(x,y,W);
                const float h    = height(x,y);
                const float w    = water[i];
                const float Htot = h + w;

                float f[4] = {0,0,0,0};
                float sumPos = 0.0f;

                const float Hx1 = (x+1 < W) ? (height(x+1,y) + water[idx(x+1,y,W)]) : Htot;
                const float Hx0 = (x-1 >=0) ? (height(x-1,y) + water[idx(x-1,y,W)]) : Htot;
                const float Hy1 = (y+1 < Hh)? (height(x,y+1) + water[idx(x,y+1,W)]) : Htot;
                const float Hy0 = (y-1 >=0)? (height(x,y-1) + water[idx(x,y-1,W)]) : Htot;

                float d0 = Htot - Hx1; if (d0>0) { f[0] = p.pipeK * d0; sumPos += f[0]; }
                float d1 = Htot - Hx0; if (d1>0) { f[1] = p.pipeK * d1; sumPos += f[1]; }
                float d2 = Htot - Hy0; if (d2>0) { f[2] = p.pipeK * d2; sumPos += f[2]; } // up
                float d3 = Htot - Hy1; if (d3>0) { f[3] = p.pipeK * d3; sumPos += f[3]; } // down

                // scale if water insufficient
                float scale = (sumPos > w) ? (w / (sumPos + 1e-8f)) : 1.0f;
                flux[i*4+0] = f[0]*scale;
                flux[i*4+1] = f[1]*scale;
                flux[i*4+2] = f[2]*scale;
                flux[i*4+3] = f[3]*scale;
            }
        }

        // 3) Update water with inflow/outflow, crude velocity proxy
        //    (we'll use gradient magnitude as "speed" proxy for capacity).
        std::vector<float> waterNew(N,0.0f);
        for (int y=0;y<Hh;++y){
            for (int x=0;x<W;++x){
                const int i = idx(x,y,W);
                float outSum = flux[i*4+0]+flux[i*4+1]+flux[i*4+2]+flux[i*4+3];

                float inSum = 0.0f;
                if (x>0)     inSum += flux[idx(x-1,y,W)*4+0]; // left -> right
                if (x+1<W)   inSum += flux[idx(x+1,y,W)*4+1]; // right -> left
                if (y>0)     inSum += flux[idx(x,y-1,W)*4+3]; // up -> down
                if (y+1<Hh)  inSum += flux[idx(x,y+1,W)*4+2]; // down -> up

                float w = water[i] + inSum - outSum;
                w = std::max(0.0f, w);
                // evaporation
                w *= (1.0f - p.evaporation);
                waterNew[i] = w;
            }
        }
        water.swap(waterNew);

        // 4) Erode / deposit according to sediment capacity ~ water * slope
        //    Use central differences for slope magnitude (gradient).
        for (int y=0;y<Hh;++y){
            for (int x=0;x<W;++x){
                const int i = idx(x,y,W);
                const float w = water[i];

                float hL = getHeight(x-1,y);
                float hR = getHeight(x+1,y);
                float hU = getHeight(x,y-1);
                float hD = getHeight(x,y+1);

                float dhdx = (hR - hL)*0.5f;
                float dhdy = (hD - hU)*0.5f;
                float slope = std::sqrt(dhdx*dhdx + dhdy*dhdy);

                float capacity = std::max(p.minSlope, slope) * w * p.sedimentCapacityK;

                if (sed[i] > capacity) {
                    float amount = p.depositRate * (sed[i] - capacity);
                    sed[i] -= amount;
                    height(x,y) += amount;
                } else {
                    float amount = p.dissolveRate * (capacity - sed[i]);
                    amount = std::min(amount, height(x,y)); // don't erode below 0
                    sed[i] += amount;
                    height(x,y) -= amount;
                }
            }
        }

        // 5) Simple friction-like damping of sediment to avoid runaway
        for (int i=0;i<N;++i) sed[i] *= (1.0f - p.friction);
    }

    // Final clamp (optional)
    H.clamp(-1e6f, 1e6f);
}

void ThermalErodeCPU(Heightfield& H, const ThermalParams& p)
{
    const int W = H.width(), Hh = H.height();
    const int N = W * Hh;
    std::vector<float> delta(N, 0.0f);

    for (int it=0; it<p.iterations; ++it)
    {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int y=0;y<Hh;++y){
            for (int x=0;x<W;++x){
                const int i = idx(x,y,W);
                const float hc = H.at(x,y);

                struct NInfo { int dx,dy; float h; };
                NInfo n[4] = {
                    {-1,0, (x>0)    ? H.at(x-1,y) : hc},
                    {+1,0, (x+1<W)  ? H.at(x+1,y) : hc},
                    {0,-1, (y>0)    ? H.at(x,y-1) : hc},
                    {0,+1, (y+1<Hh) ? H.at(x,y+1) : hc},
                };
                float total = 0.0f;
                float s[4] = {0,0,0,0};
                for (int k=0;k<4;k++){
                    float slope = hc - n[k].h;
                    float diff  = slope - p.talus;
                    if (diff > 0.0f){ s[k] = diff; total += diff; }
                }
                if (total > 0.0f){
                    for (int k=0;k<4;k++){
                        if (s[k] <= 0.0f) continue;
                        float move = p.carry * (s[k] / total) * total; // = p.carry * s[k]
                        delta[i] -= move;
                        int nx = std::clamp(x + n[k].dx, 0, W-1);
                        int ny = std::clamp(y + n[k].dy, 0, Hh-1);
                        delta[idx(nx,ny,W)] += move;
                    }
                }
            }
        }
        for (int i=0;i<N;++i) H.at(i%W,i/W) += delta[i];
    }
    H.clamp(-1e6f, 1e6f);
}

} // namespace colony::terrain
