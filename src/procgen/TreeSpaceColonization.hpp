// src/procgen/TreeSpaceColonization.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <random>
#include <cmath>
#include <limits>
#include <algorithm>

namespace colony::procgen {

struct Branch {
    int parent = -1;      // index of parent branch (-1 for root)
    float x=0, y=0;       // 2D plan; extend to 3D by adding z if needed
};

struct TreeParams {
    int   attractors = 600;   // number of attractor points in canopy
    float canopyRadius = 60;  // radius of canopy disk
    float killRadius   = 4.0f;// remove attractor if branch within this radius
    float influenceR   = 16.0f;// attractors affect branches within this radius
    float step         = 2.8f;// branch segment length per iteration
    int   maxIters     = 1500;
    uint64_t seed      = 777;
};

// simple 2D point
struct Pt { float x,y; };

static inline float dot(float ax,float ay,float bx,float by){ return ax*bx+ay*by; }

static inline std::vector<Branch> grow_tree(const Pt& root, const TreeParams& P){
    std::mt19937_64 rng(P.seed);
    std::uniform_real_distribution<float> U(0.f,1.f);

    // 1) scatter attractors in a disk around root
    std::vector<Pt> A; A.reserve(P.attractors);
    for (int i=0;i<P.attractors;i++){
        float r = P.canopyRadius * std::sqrt(U(rng));
        float a = 6.2831853f * U(rng);
        A.push_back({root.x + r*std::cos(a), root.y + r*std::sin(a)});
    }

    // 2) initial trunk/root branch
    std::vector<Branch> B; B.reserve(P.attractors*2);
    B.push_back(Branch{-1, root.x, root.y});

    // 3) iterate growth
    int it=0;
    while (!A.empty() && it++ < P.maxIters){
        // (a) map attractors to their nearest branch within influenceR
        std::vector<std::vector<int>> influences(B.size());
        std::vector<int> toErase; toErase.reserve(A.size()/4);

        for (int ai=0; ai<(int)A.size(); ++ai){
            float bestD2=std::numeric_limits<float>::max();
            int best=-1;
            for (int bi=0; bi<(int)B.size(); ++bi){
                float dx=A[ai].x - B[bi].x, dy=A[ai].y - B[bi].y;
                float d2=dx*dx+dy*dy;
                if (d2 < P.killRadius*P.killRadius){ best=-2; break; } // kill
                if (d2 < P.influenceR*P.influenceR && d2<bestD2){ bestD2=d2; best=bi; }
            }
            if (best==-2) toErase.push_back(ai);
            else if (best>=0) influences[best].push_back(ai);
        }

        // erase consumed attractors (descending order)
        std::sort(toErase.begin(), toErase.end()); toErase.erase(std::unique(toErase.begin(),toErase.end()), toErase.end());
        for (int k=(int)toErase.size()-1;k>=0;--k) A.erase(A.begin()+toErase[k]);

        // (b) for each influenced branch, grow a new segment toward the *average* direction of its attractors
        size_t newCount=0;
        for (int bi=0; bi<(int)B.size(); ++bi){
            auto& list = influences[bi];
            if (list.empty()) continue;
            float vx=0, vy=0;
            for (int ai : list){
                vx += (A[ai].x - B[bi].x);
                vy += (A[ai].y - B[bi].y);
            }
            float len=std::sqrt(vx*vx+vy*vy);
            if (len<1e-5f) continue;
            vx/=len; vy/=len;
            Branch nb; nb.parent = bi;
            nb.x = B[bi].x + vx*P.step;
            nb.y = B[bi].y + vy*P.step;
            B.push_back(nb); newCount++;
        }
        if (newCount==0) break; // no branch influenced this round
    }
    return B;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Example:
// auto tree = colony::procgen::grow_tree({100,140}, {.canopyRadius=70,.seed=42});
// Branches in 'tree' form polylines; render as cylinders/lines or use as resource art.
#endif
