// HPAStar.hpp
// Header-only Hierarchical A* (HPA*) pathfinding for grid-based maps.
// License: MIT (attribution appreciated).
//
// NOTE: Extended design notes were moved out of this header to keep compile units lean.
// See: HPAStar.md (same folder) for background, limitations, and next-step ideas.
#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/*
This file implements a compact, dependency-free Hierarchical A* (HPA*) engine
optimized for grid-based colony/settlement games. It provides:
  - A tile grid with 4- or 8-neighborhood movement
  - Automatic clustering into fixed-size sectors (configurable)
  - Portals extracted along cluster borders
  - High-level A* over the abstraction graph, then localized refinement
  - Dynamic obstacle updates with incremental invalidation (simplified demo strategy)

Integration steps:
  1) Keep this file in your repo, e.g. src/path/HPAStar.hpp
  2) Ensure your target can include src/ (or the folder containing this header).
  3) #include "path/HPAStar.hpp" and use colony::path::HPAStar
*/

namespace colony { namespace path {

struct Vec2i {
    int x = 0;
    int y = 0;
    constexpr Vec2i() = default;
    constexpr Vec2i(int X, int Y): x(X), y(Y) {}
    friend bool operator==(const Vec2i& a, const Vec2i& b) { return a.x==b.x && a.y==b.y; }
    friend bool operator!=(const Vec2i& a, const Vec2i& b) { return !(a==b); }
};

struct Vec2iHash {
    size_t operator()(const Vec2i& v) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(v.x))<<32) ^ static_cast<uint32_t>(v.y);
    }
};

enum class Heuristic : uint8_t {
    Manhattan4,  // 4-neighborhood
    Octile8      // 8-neighborhood with diagonal cost sqrt(2)
};

struct GridMap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> blocked; // 1 = blocked, 0 = free

    GridMap() = default;
    GridMap(int w, int h): width(w), height(h), blocked(size_t(w*h), 0) {}

    bool in_bounds(int x, int y) const noexcept {
        return (x>=0 && y>=0 && x<width && y<height);
    }
    bool is_blocked(int x, int y) const noexcept {
        return blocked[size_t(y*width + x)] != 0;
    }
    void set_blocked(int x, int y, bool b) noexcept {
        blocked[size_t(y*width + x)] = b ? 1u : 0u;
    }
    void fill(bool b) { std::fill(blocked.begin(), blocked.end(), b ? 1u : 0u); }
};

struct NeighborPolicy {
    bool allow_diag = true;
    bool corner_cut = false; // if false, diagonal step requires both orthogonal neighbors passable
};


struct CostModel {
    float step_cost = 1.0f;
    float diag_cost = 1.41421356237f; // sqrt(2)
};

struct Cluster {
    int id = -1;
    int x0=0, y0=0; // top-left
    int w=0, h=0;   // dimensions (<= cluster_size)
    bool anyWalkable = false;
};

struct Portal {
    int id = -1;
    Vec2i a; // tile on border
    Vec2i b; // tile on border (optional for 2-wide portals); for simplicity we treat single-tiles
    int clusterA = -1;
    int clusterB = -1;
};

struct AGNode {
    int id = -1;
    Vec2i pos; // representative (tile-space)
    int cluster = -1;
};

struct AGEdge {
    int from = -1;
    int to   = -1;
    float w  = 1.0f;
};

class AbstractionGraph {
public:
    std::vector<AGNode> nodes;
    std::vector<AGEdge> edges;
    std::vector<std::vector<int>> adj;

    int add_node(const Vec2i& p, int cluster){
        int id = static_cast<int>(nodes.size());
        nodes.push_back({id,p,cluster});
        adj.emplace_back();
        return id;
    }
    void add_edge(int u, int v, float w){
        edges.push_back({u,v,w});
        adj[u].push_back(static_cast<int>(edges.size()-1));
    }
    void clear(){ nodes.clear(); edges.clear(); adj.clear(); }
};

struct AStarNode {
    int id;
    float g;
    float f;
    int parent;
};

struct AStarCmp { bool operator()(const AStarNode& a, const AStarNode& b) const { return a.f > b.f; } };

class AStar {
public:
    template<class SuccessorFn, class HeuristicFn>
    static bool search(int start, int goal, SuccessorFn succ, HeuristicFn h, std::vector<int>& out_path){
        if(start==goal){ out_path.clear(); out_path.push_back(start); return true; }
        std::priority_queue<AStarNode,std::vector<AStarNode>,AStarCmp> open;
        std::unordered_map<int,float> g; g.reserve(1024);
        std::unordered_map<int,int> parent; parent.reserve(1024);
        open.push({start,0.0f,h(start,goal),-1}); g[start]=0.0f; parent[start]=-1;
        std::unordered_set<int> closed; closed.reserve(1024);
        while(!open.empty()){
            AStarNode cur = open.top(); open.pop();
            if(closed.count(cur.id)) continue;
            closed.insert(cur.id);
            if(cur.id==goal){
                out_path.clear(); int v = cur.id;
                while(v!=-1){ out_path.push_back(v); v = parent[v]; }
                std::reverse(out_path.begin(), out_path.end());
                return true;
            }
            succ(cur.id, [&](int nxt, float w){
                if(closed.count(nxt)) return;
                float tentative = g[cur.id] + w;
                auto it = g.find(nxt);
                if(it==g.end() || tentative < it->second){
                    g[nxt] = tentative;
                    parent[nxt] = cur.id;
                    float fn = tentative + h(nxt,goal);
                    open.push({nxt, tentative, fn, cur.id});
                }
            });
        }
        return false;
    }
};

struct PathResult {
    std::vector<Vec2i> points;
    float length = 0.0f;
    bool success = false;
};

class HPAStar {
public:
    struct Params {
        int cluster_size = 16;
        NeighborPolicy neighbors;
        CostModel cost;
        Heuristic heuristic = Heuristic::Octile8;
    };

private:
    GridMap grid_;
    Params  params_;
    std::vector<Cluster> clusters_;
    AbstractionGraph graph_;

    // Map from tile to abstraction node indices (portal nodes and special S/G nodes)
    std::unordered_map<int,int> tile_to_node_;

public:
    HPAStar() = default;
    explicit HPAStar(const GridMap& g) { reset(g, Params{}); }
    explicit HPAStar(const GridMap& g, const Params& p) { reset(g, p); }

    void reset(const GridMap& g) { reset(g, Params{}); }

    void reset(const GridMap& g, const Params& p){
        grid_ = g; params_ = p;
        rebuild_abstraction();
    }

    const GridMap& grid() const noexcept { return grid_; }
    const Params& params() const noexcept { return params_; }

    // Update a tile and incrementally fix caches (simplified: rebuild affected clusters).
    void set_blocked(int x, int y, bool b){
        if(!grid_.in_bounds(x,y)) return;
        bool before = grid_.is_blocked(x,y);
        if(before==b) return;
        grid_.set_blocked(x,y,b);
        // For simplicity, rebuild abstraction for the tile's cluster and neighbors.
        int cid = cluster_id_of(x,y);
        invalidate_cluster(cid);
        // neighbor clusters along borders may be affected:
        if(x % params_.cluster_size == 0 && x>0) invalidate_cluster(cluster_id_of(x-1,y));
        if((x+1) % params_.cluster_size == 0 && x+1<grid_.width) invalidate_cluster(cluster_id_of(x+1,y));
        if(y % params_.cluster_size == 0 && y>0) invalidate_cluster(cluster_id_of(x,y-1));
        if((y+1) % params_.cluster_size == 0 && y+1<grid_.height) invalidate_cluster(cluster_id_of(x,y+1));
    }

    PathResult find_path(Vec2i start, Vec2i goal){
        PathResult pr;
        pr.success = false; pr.length = 0.0f; pr.points.clear();
        if(!grid_.in_bounds(start.x,start.y) || !grid_.in_bounds(goal.x,goal.y)) return pr;
        if(grid_.is_blocked(start.x,start.y) || grid_.is_blocked(goal.x,goal.y)) return pr;

        // If both points are in same cluster, do local A* directly for speed.
        int cs = params_.cluster_size;
        int csa = cluster_id_of(start.x,start.y);
        int csb = cluster_id_of(goal.x,goal.y);
        if(csa==csb){
            pr.points = astar_grid(start, goal);
            if(!pr.points.empty()){ pr.success = true; pr.length = polyline_length(pr.points); }
            return pr;
        }

        // Otherwise, do hierarchical: create temporary nodes for S,G, connect to nearest portals, run A*, refine.
        int nStart = add_temporary_node(start);
        int nGoal  = add_temporary_node(goal);
        connect_node_to_cluster_portals(nStart);
        connect_node_to_cluster_portals(nGoal);

        std::vector<int> node_path;
        auto succ = [&](int u, auto emit){ for(int ei : graph_.adj[u]){ const auto& e = graph_.edges[ei]; emit(e.to, e.w); } };
        auto heu  = [&](int u, int v){ const auto &A=graph_.nodes[u].pos, &B=graph_.nodes[v].pos; return h(A,B); };
        bool ok = AStar::search(nStart, nGoal, succ, heu, node_path);
        if(!ok){ remove_temporary_node(nStart); remove_temporary_node(nGoal); return pr; }

        // Refine: for each pair of consecutive abstraction nodes, run local grid A* and append.
        std::vector<Vec2i> full; full.push_back(start);
        for(size_t i=1;i<node_path.size();++i){
            Vec2i A = graph_.nodes[node_path[i-1]].pos;
            Vec2i B = graph_.nodes[node_path[i]].pos;
            auto seg = astar_grid(A,B);
            if(seg.size()<2){ full.clear(); break; }
            // append excluding first to avoid duplicates
            full.insert(full.end(), seg.begin()+1, seg.end());
        }
        remove_temporary_node(nStart); remove_temporary_node(nGoal);
        if(full.size()>=2){ pr.success = true; pr.points = std::move(full); pr.length = polyline_length(pr.points); }
        return pr;
    }

    // Serialize basic grid and parameters to a string (simple text format).
    std::string serialize() const {
        std::ostringstream os;
        os << "HPASTAR 1\n";
        os << grid_.width << ' ' << grid_.height << '\n';
        os << params_.cluster_size << ' ' << int(params_.neighbors.allow_diag) << ' ' << int(params_.neighbors.corner_cut) << '\n';
        for(int y=0;y<grid_.height;++y){
            for(int x=0;x<grid_.width;++x){ os << (grid_.is_blocked(x,y)?'#':'.'); }
            os << '\n';
        }
        return os.str();
    }

    // Deserialize from text; resets internal state.
    void deserialize(const std::string& s){
        std::istringstream is(s); std::string magic; is >> magic;
        if(magic!="HPASTAR") throw std::runtime_error("bad magic");
        int ver; is >> ver; (void)ver;
        int w,h; is >> w >> h; GridMap g(w,h);
        int cs, ad, cc; is >> cs >> ad >> cc;
        std::string line; std::getline(is,line);
        for(int y=0;y<h;++y){
            std::getline(is,line);
            for(int x=0;x<w;++x){ g.set_blocked(x,y, line[x]=='#'); }
        }
        Params p; p.cluster_size=cs; p.neighbors.allow_diag=!!ad; p.neighbors.corner_cut=!!cc;
        reset(g,p);
    }

private:
    float h(const Vec2i& a, const Vec2i& b) const noexcept {
        if(params_.heuristic==Heuristic::Manhattan4){
            return float(std::abs(a.x-b.x) + std::abs(a.y-b.y));
        } else {
            // Octile distance for 8-neighborhood
            int dx = std::abs(a.x-b.x), dy = std::abs(a.y-b.y);
            int dmin = std::min(dx,dy), dmax = std::max(dx,dy);
            return params_.cost.diag_cost * float(dmin) + params_.cost.step_cost * float(dmax-dmin);
        }
    }

    float polyline_length(const std::vector<Vec2i>& pts) const noexcept {
        float len = 0.0f;
        for(size_t i=1;i<pts.size();++i){
            int dx = pts[i].x - pts[i-1].x; int dy = pts[i].y - pts[i-1].y;
            if(dx==0 || dy==0) len += params_.cost.step_cost * float(std::abs(dx)+std::abs(dy));
            else len += params_.cost.diag_cost;
        }
        return len;
    }

    int cluster_id_of(int x, int y) const noexcept {
        int cs = params_.cluster_size;
        int cx = x / cs; int cy = y / cs;
        int nx = (grid_.width + cs - 1)/cs;
        return cy*nx + cx;
    }

    void invalidate_cluster(int cid){ (void)cid; rebuild_abstraction(); } // simple strategy for now

    void rebuild_abstraction(){
        clusters_.clear(); graph_.clear(); tile_to_node_.clear();
        int cs = params_.cluster_size;
        int nx = (grid_.width + cs - 1)/cs;
        int ny = (grid_.height + cs - 1)/cs;
        clusters_.reserve(size_t(nx*ny));
        int id=0;
        for(int cy=0; cy<ny; ++cy){
            for(int cx=0; cx<nx; ++cx){
                Cluster c; c.id=id++; c.x0=cx*cs; c.y0=cy*cs; c.w=std::min(cs, grid_.width - c.x0); c.h=std::min(cs, grid_.height - c.y0); c.anyWalkable=false;
                // check any walkable
                for(int y=0;y<c.h && !c.anyWalkable;++y) for(int x=0;x<c.w;++x){ if(!grid_.is_blocked(c.x0+x,c.y0+y)){ c.anyWalkable=true; break; } }
                clusters_.push_back(c);
            }
        }
        // Create portal nodes along borders between neighboring clusters at walkable tiles
        auto add_portal_between = [&](int cida, int cidb, bool vertical){
            const Cluster& A = clusters_[cida]; const Cluster& B = clusters_[cidb];
            if(vertical){
                int xA = A.x0 + A.w - 1; int xB = B.x0; if(xA+1!=xB) return;
                int y0 = std::max(A.y0,B.y0); int y1 = std::min(A.y0+A.h,B.y0+B.h)-1;
                for(int y=y0; y<=y1; ++y){
                    int ax = xA, ay=y; int bx = xB, by=y;
                    if(!grid_.is_blocked(ax,ay) && !grid_.is_blocked(bx,by)){
                        int u = graph_.add_node({ax,ay}, A.id);
                        int v = graph_.add_node({bx,by}, B.id);
                        graph_.add_edge(u,v,1.0f);
                        graph_.add_edge(v,u,1.0f);
                        tile_to_node_[ay*grid_.width+ax] = u;
                        tile_to_node_[by*grid_.width+bx] = v;
                    }
                }
            } else {
                int yA = A.y0 + A.h - 1; int yB = B.y0; if(yA+1!=yB) return;
                int x0 = std::max(A.x0,B.x0); int x1 = std::min(A.x0+A.w,B.x0+B.w)-1;
                for(int x=x0; x<=x1; ++x){
                    int ax = x, ay=yA; int bx = x, by=yB;
                    if(!grid_.is_blocked(ax,ay) && !grid_.is_blocked(bx,by)){
                        int u = graph_.add_node({ax,ay}, A.id);
                        int v = graph_.add_node({bx,by}, B.id);
                        graph_.add_edge(u,v,1.0f);
                        graph_.add_edge(v,u,1.0f);
                        tile_to_node_[ay*grid_.width+ax] = u;
                        tile_to_node_[by*grid_.width+bx] = v;
                    }
                }
            }
        };

        int nxC = (grid_.width + cs - 1)/cs; int nyC = (grid_.height + cs - 1)/cs;
        auto cid = [&](int cx, int cy){ return cy*nxC + cx; };
        for(int cy=0; cy<nyC; ++cy){
            for(int cx=0; cx<nxC; ++cx){
                if(cx+1<nxC) add_portal_between(cid(cx,cy), cid(cx+1,cy), true);
                if(cy+1<nyC) add_portal_between(cid(cx,cy), cid(cx,cy+1), false);
            }
        }
    }

    // Connect a temporary node at tile to nearby portal nodes within its cluster.
    void connect_node_to_cluster_portals(int nid){
        const Vec2i p = graph_.nodes[nid].pos;
        int cs = params_.cluster_size;
        int cx = p.x / cs; int cy = p.y / cs;
        int nx = (grid_.width + cs - 1)/cs;
        // naive: connect to all abstraction nodes that lie within this cluster
        for(size_t i=0;i<graph_.nodes.size();++i){
            if((int)i==nid) continue;
            const auto& n = graph_.nodes[i];
            int ncx = n.pos.x / cs; int ncy = n.pos.y / cs;
            if(ncx==cx && ncy==cy){
                float w = local_distance(p, n.pos);
                if(std::isfinite(w)){
                    graph_.add_edge(nid, (int)i, w);
                    graph_.add_edge((int)i, nid, w);
                }
            }
        }
    }

    float local_distance(const Vec2i& a, const Vec2i& b){
        auto pts = astar_grid(a,b);
        if(pts.size()<2) return std::numeric_limits<float>::infinity();
        return polyline_length(pts);
    }

    std::vector<Vec2i> astar_grid(Vec2i start, Vec2i goal){
        struct Node { Vec2i p; float g,f; int parent; };
        struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f>b.f; } };
        std::priority_queue<Node,std::vector<Node>,Cmp> open;
        std::unordered_map<int,float> g; g.reserve(4096);
        std::unordered_map<int,int> parent; parent.reserve(4096);
        auto key = [&](int x,int y){ return y*grid_.width + x; };
        auto hfun = [&](const Vec2i& A, const Vec2i& B){ return h(A,B); };
        const int ks = key(start.x,start.y);
        open.push({start,0.0f,hfun(start,goal),-1}); g[ks]=0.0f; parent[ks]=-1;
        std::unordered_set<int> closed; closed.reserve(4096);
        auto try_add = [&](int cx, int cy, int px, int py, float w){
            if(!grid_.in_bounds(cx,cy) || grid_.is_blocked(cx,cy)) return;
            int kk = key(cx,cy); if(closed.count(kk)) return;
            float gg = g[key(px,py)] + w;
            auto it = g.find(kk); if(it==g.end() || gg < it->second){ g[kk]=gg; parent[kk]=key(px,py); float ff = gg + hfun({cx,cy},goal); open.push({{cx,cy},gg,ff,0}); }
        };
        while(!open.empty()){
            Node cur = open.top(); open.pop();
            int kk = key(cur.p.x,cur.p.y);
            if(closed.count(kk)) continue;
            closed.insert(kk);
            if(cur.p==goal){
                std::vector<Vec2i> path; int v = kk;
                while(v!=-1){ int x=v%grid_.width, y=v/grid_.width; path.push_back({x,y}); v=parent[v]; }
                std::reverse(path.begin(), path.end());
                return path;
            }
            // neighbors
            static const int DX8[8] = {1,-1,0,0,1,1,-1,-1};
            static const int DY8[8] = {0,0,1,-1,1,-1,1,-1};
            for(int dir=0; dir<(params_.neighbors.allow_diag?8:4); ++dir){
                int nx = cur.p.x + DX8[dir];
                int ny = cur.p.y + DY8[dir];
                if(!grid_.in_bounds(nx,ny) || grid_.is_blocked(nx,ny)) continue;
                if(dir>=4 && !params_.neighbors.corner_cut){
                    // diagonal requires both orthogonal tiles free
                    int ox1 = cur.p.x + DX8[dir]; int oy1 = cur.p.y;
                    int ox2 = cur.p.x; int oy2 = cur.p.y + DY8[dir];
                    if(grid_.is_blocked(ox1,oy1) || grid_.is_blocked(ox2,oy2)) continue;
                }
                float w = (dir<4) ? params_.cost.step_cost : params_.cost.diag_cost;
                try_add(nx,ny, cur.p.x,cur.p.y, w);
            }
        }
        return {};
    }

    int add_temporary_node(const Vec2i& p){
        int id = graph_.add_node(p, -1);
        tile_to_node_[p.y*grid_.width+p.x] = id;
        return id;
    }
    void remove_temporary_node(int id){
        // For simplicity we won't actually erase nodes to keep indices stable in this demo;
        // in production you'd want a free-list or a separate structure for temps.
        (void)id;
    }
};

// ------------------------- Convenience test helpers (optional) -------------------------
#ifdef COLONY_HPASTAR_ENABLE_TESTS
#include <iostream>
static void hpastar_self_test(){
    using namespace colony::path;
    GridMap g(64,64);
    // Add a simple wall with a gap
    for(int x=0;x<64;++x) g.set_blocked(x,32,true);
    g.set_blocked(31,32,false); g.set_blocked(32,32,false);
    HPAStar::Params p; p.cluster_size=16; p.neighbors.allow_diag=true; p.neighbors.corner_cut=false;
    HPAStar h(g,p);
    auto r = h.find_path({2,2},{60,60});
    std::cout << "Path success=" << r.success << " length="<< r.length << " points="<< r.points.size() << "\n";
}
#endif

}} // namespace colony::path

// For extended documentation, motivation, and follow-up improvements:
//   see HPAStar.md (same folder as this header).
