#include "Poisson.h"
#include <random>
#include <array>
std::vector<P2> Poisson2D(float W, float H, float r, uint32_t seed, int k)
{
    std::mt19937 rng(seed);
    float cell = r / std::sqrt(2.f);
    int gw = int(std::ceil(W / cell)), gh = int(std::ceil(H / cell));
    std::vector<int> grid(gw*gh, -1);
    std::vector<P2> samples; samples.reserve(4096);
    std::vector<int> active;

    auto gridAt=[&](int gx,int gy)->int&{ return grid[gy*gw+gx]; };
    auto fits=[&](const P2& p){
        if (p.x<0||p.y<0||p.x>=W||p.y>=H) return false;
        int gx=int(p.x/cell), gy=int(p.y/cell);
        for(int y=max(0,gy-2); y<=min(gh-1,gy+2); ++y)
        for(int x=max(0,gx-2); x<=min(gw-1,gx+2); ++x){
            int i = gridAt(x,y); if (i<0) continue;
            if (d2(samples[i],p) < r*r) return false;
        }
        return true;
    };

    std::uniform_real_distribution<float> Ux(0,W), Uy(0,H), Ua(0,6.28318f), Ur(r,2*r);
    // Seed
    P2 s{Ux(rng), Uy(rng)}; samples.push_back(s);
    gridAt(int(s.x/cell), int(s.y/cell)) = 0;
    active.push_back(0);

    while(!active.empty()){
        std::uniform_int_distribution<int> Ui(0,(int)active.size()-1);
        int idx = Ui(rng); P2 base = samples[active[idx]];
        bool found=false;
        for(int t=0;t<k;t++){
            float a=Ua(rng), rr=Ur(rng);
            P2 c{ base.x + std::cos(a)*rr, base.y + std::sin(a)*rr };
            if (fits(c)){
                samples.push_back(c);
                gridAt(int(c.x/cell), int(c.y/cell)) = (int)samples.size()-1;
                active.push_back((int)samples.size()-1);
                found=true; break;
            }
        }
        if(!found){ active[idx]=active.back(); active.pop_back(); }
    }
    return samples;
}
