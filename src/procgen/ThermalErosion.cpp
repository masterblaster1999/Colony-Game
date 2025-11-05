#include "procgen/ThermalErosion.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace procgen {

void thermal_erosion(Heightmap& h, const ThermalParams& p) {
    if (h.width == 0 || h.height == 0) return;

    std::vector<float> delta(h.width*h.height, 0.0f);
    static const int dx[8]={-1,0,1,-1,1,-1,0,1};
    static const int dy[8]={-1,-1,-1,0,0,1,1,1};

    for (int it=0; it<p.iterations; ++it){
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int y=1; y<h.height-1; ++y){
            for (int x=1; x<h.width-1; ++x){
                float c = h.at(x,y);
                float total = 0.0f;
                float slopes[8]; int n=0;
                for (int k=0;k<8;++k){
                    float s = c - h.at(x+dx[k],y+dy[k]);
                    slopes[n++] = s;
                    total += std::max(0.0f, s - p.talus);
                }
                if (total <= 0.0f) continue;
                float movedTotal = 0.0f;
                for (int k=0;k<8;++k){
                    float s = slopes[k];
                    float excess = std::max(0.0f, s - p.talus);
                    if (excess <= 0.0f) continue;
                    float moved = p.amount * (excess / total) * p.talus;
                    int nx = x+dx[k], ny = y+dy[k];
                    delta[h.idx(nx,ny)] += moved;
                    movedTotal += moved;
                }
                delta[h.idx(x,y)] -= movedTotal;
            }
        }
        for (size_t i=0;i<delta.size();++i) h.data[i] = std::max(0.0f, h.data[i] + delta[i]);
    }
    h.normalize();
}

} // namespace procgen
