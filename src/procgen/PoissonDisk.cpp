#include "procgen/PoissonDisk.h"
#include <cmath>
#include <array>

namespace procgen {

struct GridCell { int idx = -1; };

std::vector<FV2> poisson_disk_2d(float r, int k, uint32_t seed) {
    // Unit square [0,1]^2
    const float cell = r / std::sqrt(2.0f);
    const int gw = int(std::ceil(1.0f / cell));
    const int gh = int(std::ceil(1.0f / cell));
    std::vector<GridCell> grid(gw*gh);
    std::vector<FV2> samples, active;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    auto grid_idx = [&](float x, float y){ return int(y/cell)*gw + int(x/cell); };
    auto in_annulus = [&](const FV2& a, const FV2& b){ 
        float dx=a.x-b.x, dy=a.y-b.y; return (dx*dx+dy*dy) < r*r; };

    // initial point
    FV2 p0{U(rng), U(rng)};
    samples.push_back(p0); active.push_back(p0);
    grid[grid_idx(p0.x,p0.y)].idx = 0;

    std::uniform_real_distribution<float> Ur(0.0f, r);
    std::uniform_real_distribution<float> Uang(0.0f, 6.2831853f);

    while(!active.empty()){
        std::uniform_int_distribution<size_t> Ui(0, active.size()-1);
        FV2 p = active[Ui(rng)];
        bool found = false;
        for (int i=0;i<k;++i){
            float rr = r * (1.0f + Ur(rng)/r); // between r and 2r
            float ang = Uang(rng);
            FV2 q { p.x + rr*std::cos(ang), p.y + rr*std::sin(ang) };
            if (q.x < 0 || q.x > 1 || q.y < 0 || q.y > 1) continue;

            int gx = int(q.x / cell), gy = int(q.y / cell);
            bool ok = true;
            for (int yy = std::max(0, gy-2); yy <= std::min(gh-1, gy+2) && ok; ++yy){
                for (int xx = std::max(0, gx-2); xx <= std::min(gw-1, gx+2); ++xx){
                    int gi = yy*gw + xx;
                    int si = grid[gi].idx; if (si<0) continue;
                    if (in_annulus(q, samples[si])) { ok = false; break; }
                }
            }
            if (ok){
                int idx = (int)samples.size();
                samples.push_back(q);
                active.push_back(q);
                grid[gy*gw+gx].idx = idx;
                found = true;
                break;
            }
        }
        if (!found){
            // remove p
            for (size_t i=0;i<active.size();++i) if (active[i].x==p.x&&active[i].y==p.y) { active.erase(active.begin()+i); break; }
        }
    }
    return samples;
}

} // namespace procgen
