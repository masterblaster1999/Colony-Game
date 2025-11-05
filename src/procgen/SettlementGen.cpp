#include "procgen/SettlementGen.h"
#include "procgen/PoissonDisk.h"
#include <queue>
#include <cmath>
#include <algorithm>
#include <random>

namespace procgen {

static float tile_cost(const Heightmap& h, int x, int y, float slopeCost){
    // cost rises with slope; water impassable
    float c = 1.0f;
    float e = h.at(x,y);
    static const int dx[4]={-1,1,0,0};
    static const int dy[4]={0,0,-1,1};
    float maxSlope=0.0f;
    for(int k=0;k<4;++k){
        int nx=x+dx[k], ny=y+dy[k];
        if(!in_bounds(nx,ny,h.width,h.height)) continue;
        maxSlope = std::max(maxSlope, std::abs(e - h.at(nx,ny)));
    }
    return c + slopeCost*maxSlope;
}

static std::vector<IV2> a_star_path(const Heightmap& h, IV2 a, IV2 b, float slopeCost){
    struct Node { int x,y; float g,f; int px,py; };
    auto key = [&](int x,int y){ return y*h.width + x; };

    std::vector<float> g(h.width*h.height, 1e30f);
    std::vector<IV2> parent(h.width*h.height, {-1,-1});

    auto heuristic = [&](int x,int y){ return std::hypot(float(x-b.x), float(y-b.y)); };

    auto cmp = [](const Node& A,const Node& B){ return A.f > B.f; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);
    g[key(a.x,a.y)] = 0.0f;
    pq.push({a.x,a.y,0.0f, heuristic(a.x,a.y), -1,-1});

    static const int DX[4]={-1,1,0,0};
    static const int DY[4]={0,0,-1,1};

    while(!pq.empty()){
        Node n = pq.top(); pq.pop();
        if (n.x==b.x && n.y==b.y) break;

        for(int k=0;k<4;++k){
            int nx=n.x+DX[k], ny=n.y+DY[k];
            if(!in_bounds(nx,ny,h.width,h.height)) continue;
            float cost = tile_cost(h, nx, ny, slopeCost);
            float ng = n.g + cost;
            int ki = key(nx,ny);
            if (ng < g[ki]){
                g[ki]=ng; parent[ki]={n.x,n.y};
                pq.push({nx,ny,ng, ng + heuristic(nx,ny), n.x,n.y});
            }
        }
    }

    std::vector<IV2> path; IV2 cur=b;
    while(!(cur.x==a.x && cur.y==a.y) && in_bounds(cur.x,cur.y,h.width,h.height)){
        path.push_back(cur);
        cur = parent[key(cur.x,cur.y)];
        if (cur.x<0) break;
    }
    path.push_back(a);
    std::reverse(path.begin(), path.end());
    return path;
}

Settlement generate_settlement(const Heightmap& elev, float seaLevel, const SettlementParams& p){
    Settlement out;
    // pick candidate town sites on flat ground above sea level
    auto pts = poisson_disk_2d(0.06f, 30, p.seed);
    std::vector<IV2> sites; sites.reserve(p.targetSites);
    for (auto v : pts){
        int x = (int)(v.x * (elev.width - 1));
        int y = (int)(v.y * (elev.height - 1));
        if (elev.at(x,y) <= seaLevel+0.01f) continue;
        // prefer flatter areas
        float cost = tile_cost(elev, x, y, p.slopeCost);
        if (cost < 2.0f) sites.push_back({x,y});
        if ((int)sites.size()>=p.targetSites) break;
    }

    // connect sites with a tree of roads
    if (!sites.empty()){
        IV2 root = sites.front();
        std::vector<bool> used(sites.size(), false);
        used[0]=true;
        for (size_t i=1;i<sites.size();++i){
            // connect next site to nearest used site
            size_t bestJ=0; float bestD=1e30f;
            for (size_t j=0;j<i;++j) if (used[j]){
                float d = std::hypot(float(sites[i].x - sites[j].x), float(sites[i].y - sites[j].y));
                if (d < bestD){ bestD=d; bestJ=j; }
            }
            auto path = a_star_path(elev, sites[i], sites[bestJ], p.slopeCost);
            out.roads.push_back({ path });
            used[i]=true;
        }
    }

    // generate simple plots along roads
    for (const auto& r : out.roads){
        for (IV2 c : r.cells){
            if (elev.at(c.x,c.y) <= seaLevel+0.01f) continue;
            // very naive 1x1 plots adjacent to the road cell
            static const int dx[4]={-1,1,0,0};
            static const int dy[4]={0,0,-1,1};
            for (int k=0;k<4;++k){
                int nx=c.x+dx[k], ny=c.y+dy[k];
                if(!in_bounds(nx,ny,elev.width,elev.height)) continue;
                if (elev.at(nx,ny) > seaLevel+0.01f){
                    out.plots.push_back({{nx,ny},1,1});
                }
            }
        }
    }

    return out;
}

} // namespace procgen
