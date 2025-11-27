#include "procgen/Erosion.h"
#include <random>
#include <algorithm>
#include <cmath>

namespace procgen {

static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline int   idx(int x,int y,int w){ return y*w + x; }

static float sample(const std::vector<float>& h, int w, int hgt, float x, float y) {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    ix = std::max(0, std::min(w-2, ix));
    iy = std::max(0, std::min(hgt-2, iy));
    const float h00 = h[idx(ix,iy,w)];
    const float h10 = h[idx(ix+1,iy,w)];
    const float h01 = h[idx(ix,iy+1,w)];
    const float h11 = h[idx(ix+1,iy+1,w)];
    const float hx0 = h00 + fx*(h10 - h00);
    const float hx1 = h01 + fx*(h11 - h01);
    return hx0 + fy*(hx1 - hx0);
}

static void addHeight(std::vector<float>& h, int w, int hgt, int x, int y, float val) {
    if (x>=0 && y>=0 && x<w && y<hgt) {
        h[idx(x,y,w)] += val;
    }
}

void applyHydraulicErosion(std::vector<float>& height, int w, int hgt,
                           uint32_t seed, const ErosionParams& p)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> ux(0.0f, (float)(w - 1));
    std::uniform_real_distribution<float> uy(0.0f, (float)(hgt - 1));

    for (int di = 0; di < p.dropletCount; ++di) {
        float x = ux(rng);
        float y = uy(rng);
        float dirX = 0.f, dirY = 0.f;
        float speed = p.initialSpeed;
        float water = p.initialWater;
        float sediment = 0.f;

        for (int step = 0; step < p.maxSteps; ++step) {
            // Height and gradient via bilinear sampling
            float h0 = sample(height, w, hgt, x, y);
            float hx = sample(height, w, hgt, x + 1, y) - h0;
            float hy = sample(height, w, hgt, x, y + 1) - h0;

            // Update direction (inertia)
            dirX = dirX * p.inertia - hx * (1.f - p.inertia);
            dirY = dirY * p.inertia - hy * (1.f - p.inertia);

            // Normalize direction
            float len = std::sqrt(dirX*dirX + dirY*dirY);
            if (len != 0.f) { dirX /= len; dirY /= len; }

            // Move
            x += dirX;
            y += dirY;

            // Stop if out of bounds
            if (x < 1 || y < 1 || x >= w - 2 || y >= hgt - 2) break;

            float h1 = sample(height, w, hgt, x, y);
            float dh = h1 - h0;

            // Compute sediment capacity
            float slope = -dh;
            float capacity = std::max(slope, p.minSlope) * speed * water * p.sedimentCapacityFactor;

            if (sediment > capacity) {
                // deposit
                float amount = (sediment - capacity) * p.depositSpeed;
                sediment -= amount;

                int ix = (int)std::floor(x);
                int iy = (int)std::floor(y);
                float fx = x - ix, fy = y - iy;

                // distribute to 4 neighbors (bilinear)
                addHeight(height, w, hgt, ix,     iy,     amount*(1-fx)*(1-fy));
                addHeight(height, w, hgt, ix + 1, iy,     amount*(fx)*(1-fy));
                addHeight(height, w, hgt, ix,     iy + 1, amount*(1-fx)*(fy));
                addHeight(height, w, hgt, ix + 1, iy + 1, amount*(fx)*(fy));
            } else {
                // erode
                float amount = std::min((capacity - sediment) * p.erodeSpeed, 0.3f);
                sediment += amount;

                int ix = (int)std::floor(x);
                int iy = (int)std::floor(y);

                // gather weights from 3x3 kernel around (ix,iy)
                float wsum = 0.f;
                float weights[9];
                int   xs[9], ys[9];
                int c = 0;
                for (int oy=-1; oy<=1; ++oy) for (int ox=-1; ox<=1; ++ox) {
                    int xx = ix + ox, yy = iy + oy;
                    if (xx<0 || yy<0 || xx>=w || yy>=hgt) { weights[c]=0; xs[c]=xx; ys[c]=yy; ++c; continue; }
                    float d = std::max(0.f, 1.0f - std::sqrt((float)(ox*ox + oy*oy)));
                    weights[c] = d; wsum += d; xs[c]=xx; ys[c]=yy; ++c;
                }
                if (wsum > 0.f) {
                    for (int i=0;i<9;++i) {
                        if (xs[i]<0) continue;
                        float take = amount * (weights[i]/wsum);
                        height[idx(xs[i], ys[i], w)] = std::max(0.f, height[idx(xs[i], ys[i], w)] - take);
                    }
                }
            }

            // update water/speed
            speed = std::sqrt(std::max(0.f, speed*speed + dh * p.gravity));
            water *= (1.f - p.evaporateSpeed);
            if (water < 0.01f) break;
        }
    }

    // normalize [0..1] (post-erosion)
    auto [mnIt, mxIt] = std::minmax_element(height.begin(), height.end());
    float mn = *mnIt, mx = *mxIt, inv = (mx > mn) ? 1.f/(mx-mn) : 1.f;
    for (float& v : height) v = (v - mn) * inv;
}

} // namespace procgen

/* References:
   Beneš & Forsbach, "Visual Simulation of Hydraulic Erosion" (2002).
   Various droplet-based real-time erosion implementations in graphics literature/blogs.
*/
