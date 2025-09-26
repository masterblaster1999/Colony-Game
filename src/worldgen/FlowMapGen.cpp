#include "FlowMapGen.hpp"
#include <algorithm>
#include <cmath>

namespace cg {

static const int DX8[8] = { +1,+1, 0,-1,-1,-1, 0,+1 };
static const int DY8[8] = {  0,-1,-1,-1, 0,+1,+1,+1 };

FlowMap buildFlowMapFromD8(const std::vector<uint8_t>& dir,
                           const std::vector<float>&   acc,
                           int W,int H)
{
    FlowMap F; F.w=W; F.h=H; F.rgba.assign(size_t(W)*H*4, 255u);

    // Normalize accumulation to [0,1] (log-ish mapping helps)
    float aMin=1e30f, aMax=0.f;
    for (float v: acc){ if (v>0){ aMin = std::min(aMin, v); aMax = std::max(aMax, v);} }
    if (!(aMax > aMin)) { aMin=0.f; aMax=1.f; }
    float invRange = (aMax > aMin) ? 1.f/(aMax - aMin) : 1.f;

    auto pack = [&](int x,int y, float2 d, float s){
        uint8_t R = (uint8_t)std::round( clamp((d.x*0.5f+0.5f)*255.f, 0.f, 255.f) );
        uint8_t G = (uint8_t)std::round( clamp((d.y*0.5f+0.5f)*255.f, 0.f, 255.f) );
        uint8_t B = (uint8_t)std::round( clamp(s*255.f, 0.f, 255.f) );
        size_t i = (size_t)(y*W + x)*4;
        F.rgba[i+0]=R; F.rgba[i+1]=G; F.rgba[i+2]=B; F.rgba[i+3]=255;
    };

    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            int i = y*W + x;
            uint8_t k = dir[i];
            float2 d = float2(0,0);
            if (k != 255u){
                d = normalize(float2((float)DX8[k], (float)DY8[k]));
            }
            float a = acc[i];
            float s = (a>0) ? std::clamp((a - aMin)*invRange, 0.f, 1.f) : 0.f;
            // Optional log tone-map
            s = std::clamp(log2(1.0f + 15.0f*s)/4.0f, 0.f, 1.f);
            pack(x,y, d, s);
        }
    }
    return F;
}

} // namespace cg
