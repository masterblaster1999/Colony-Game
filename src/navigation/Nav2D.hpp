#pragma once
// Nav2D.hpp - single-header navigation for colony sims
// C++17+, header-only. Drop into src/navigation/Nav2D.hpp and add src/ to your include path.
// Features (toggle with macros below):
// - Grid with dynamic block/cost + revision counter (serialization support)
// - A* (4/8-way), no-corner-cutting option
// - Jump Point Search (JPS) for uniform-cost grids
// - LRU path cache (keyed by start/goal/flags + grid revision)
// - LOS smoothing
// - Deterministic tie-breaking; optional seeded randomness for tiebreaks in crowd rules
// - Flow fields: single and multi-source, blending with scalar hazard fields, gradient sampling
// - D* Lite incremental re-planning on terrain change (notify deltas for O(k log n))
// - HPA*: clusters + portals + abstract search + stitching (rebuild on revision threshold)
// - Simple grid-density crowd avoidance with lateral bias
// - Debug: ASCII dumps for paths/flow; optional ImGui overlay hooks
// - Lightweight profiling counters (expansions, heap ops, cache hits)
// - Header-embedded tests under NAV2D_TESTS
//
// This file is intentionally self-contained. You can enable/disable blocks by setting macros
// before including the header or via compiler flags (-DNAME=VALUE).

#include <vector>
#include <queue>
#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <cmath>
#include <algorithm>
#include <functional>
#include <cassert>
#include <optional>
#include <utility>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>

//////////////////////////////////////////////////////////////
// Feature flags (override with -D on your build if desired)
#ifndef NAV2D_ENABLE_JPS
#define NAV2D_ENABLE_JPS 1
#endif
#ifndef NAV2D_ENABLE_CACHE
#define NAV2D_ENABLE_CACHE 1
#endif
#ifndef NAV2D_ENABLE_FLOWFIELD
#define NAV2D_ENABLE_FLOWFIELD 1
#endif
#ifndef NAV2D_ENABLE_DSTARLITE
#define NAV2D_ENABLE_DSTARLITE 1
#endif
#ifndef NAV2D_ENABLE_HPA
#define NAV2D_ENABLE_HPA 1
#endif
#ifndef NAV2D_ENABLE_DEBUG
#define NAV2D_ENABLE_DEBUG 1
#endif
#ifndef NAV2D_ENABLE_IMGUI
#define NAV2D_ENABLE_IMGUI 0
#endif
#ifndef NAV2D_DETERMINISTIC
#define NAV2D_DETERMINISTIC 1
#endif
#ifndef NAV2D_CACHE_CAPACITY
#define NAV2D_CACHE_CAPACITY 128
#endif
#ifndef NAV2D_SEED
#define NAV2D_SEED 0xC0FFEE1234ULL
#endif

#if NAV2D_ENABLE_IMGUI
#include <imgui.h>
#endif

namespace nav2d {

// ============ Small helpers ============
inline int sgn(int v) { return (v > 0) - (v < 0); }
struct Dir { int dx, dy; };
static constexpr Dir DIR4[4]  = {{1,0},{-1,0},{0,1},{0,-1}};
static constexpr Dir DIR8[8]  = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
static constexpr float DIAG = 1.41421356f;

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell& o) const noexcept { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const noexcept { return !(*this == o); }
};
struct Rect {
    int x=0,y=0,w=0,h=0;
    bool contains(int px,int py) const noexcept {
        return px>=x && py>=y && px<x+w && py<y+h;
    }
    bool intersects(const Rect& r) const noexcept {
        return !(x+w<=r.x || r.x+r.w<=x || y+h<=r.y || r.y+r.h<=y);
    }
};

struct SearchParams {
    bool allow_diagonal       = true;
    bool allow_corner_cutting = false;  // if false, forbids diagonal squeeze through blocked corners
#if NAV2D_ENABLE_JPS
    bool prefer_jps           = true;   // only used if grid is uniform-cost
#endif
#if NAV2D_ENABLE_CACHE
    bool use_cache            = true;   // path cache
#endif
#if NAV2D_ENABLE_HPA
    bool use_hpa              = false;  // try HPA* for long paths
    int  hpa_cluster_size     = 16;     // tile size of a cluster
    uint64_t hpa_rebuild_threshold = 64;// rebuild portals when revision jumps past this
#endif
    float heuristic_weight    = 1.0f;   // 1.0 = admissible
    unsigned max_expansions   = 0;      // 0 = unlimited
};

struct PathResult {
    bool success = false;
    float cost   = 0.0f;
    std::vector<Cell> path;             // start->goal inclusive
};

inline float octile(int dx, int dy) {
    const int m = std::min(dx, dy), M = std::max(dx, dy);
    return (float)m * DIAG + (float)(M - m);
}
inline float hCost(Cell a, Cell b, bool diag) {
    const int dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
    return diag ? octile(dx, dy) : (float)(dx + dy);
}

// ============ RNG for tie-breaks in non-critical choices ============
struct SplitMix64 {
    uint64_t s;
    explicit SplitMix64(uint64_t seed = NAV2D_SEED) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double next01() { return (next() >> 11) * (1.0/9007199254740992.0); }
};

// ============ Grid ============

class Grid {
public:
    Grid() { reset(1,1); }
    Grid(int w, int h) { reset(w, h); }

    void reset(int w, int h) {
        _w = std::max(1, w); _h = std::max(1, h);
        _blocked.assign((size_t)_w * _h, uint8_t{0});
        _cost.assign((size_t)_w * _h, uint16_t{1});
        _rev = 1;
        _everNonUnitCost = false;
    }

    int  width()  const noexcept { return _w; }
    int  height() const noexcept { return _h; }

    bool inBounds(int x, int y) const noexcept {
        return (unsigned)x < (unsigned)_w && (unsigned)y < (unsigned)_h;
    }

    bool isBlocked(int x, int y) const noexcept {
        assert(inBounds(x,y));
        return _blocked[idx(x,y)] != 0;
    }

    void setBlocked(int x, int y, bool blocked) noexcept {
        assert(inBounds(x,y));
        uint8_t& b = _blocked[idx(x,y)];
        uint8_t nb = blocked ? 1u : 0u;
        if (b != nb) { b = nb; ++_rev; }
    }

    uint16_t moveCost(int x, int y) const noexcept {
        assert(inBounds(x,y));
        return _cost[idx(x,y)];
    }

    void setMoveCost(int x, int y, uint16_t c) noexcept {
        assert(inBounds(x,y));
        uint16_t nc = std::max<uint16_t>(1, c);
        uint16_t& ref = _cost[idx(x,y)];
        if (ref != nc) {
            ref = nc; ++_rev;
            if (nc != 1) _everNonUnitCost = true;
        }
    }

    bool uniformCostEver() const noexcept { return !_everNonUnitCost; } // conservative

    size_t idx(int x, int y) const noexcept { return (size_t)y * _w + x; }
    int    xof(size_t i) const noexcept { return (int)(i % _w); }
    int    yof(size_t i) const noexcept { return (int)(i / _w); }

    uint64_t revision() const noexcept { return _rev; }

    // --- Serialization ---
    void serialize(std::ostream& os) const {
        os.write((const char*)&_w, sizeof(_w));
        os.write((const char*)&_h, sizeof(_h));
        os.write((const char*)&_rev, sizeof(_rev));
        os.write((const char*)&_everNonUnitCost, sizeof(_everNonUnitCost));
        os.write((const char*)_blocked.data(), _blocked.size()*sizeof(uint8_t));
        os.write((const char*)_cost.data(), _cost.size()*sizeof(uint16_t));
    }
    bool deserialize(std::istream& is) {
        int w=0,h=0; uint64_t rv=0; bool enu=false;
        if (!is.read((char*)&w, sizeof(w))) return false;
        if (!is.read((char*)&h, sizeof(h))) return false;
        if (!is.read((char*)&rv, sizeof(rv))) return false;
        if (!is.read((char*)&enu, sizeof(enu))) return false;
        reset(w,h);
        _rev = rv;
        _everNonUnitCost = enu;
        if (!is.read((char*)_blocked.data(), _blocked.size()*sizeof(uint8_t))) return false;
        if (!is.read((char*)_cost.data(), _cost.size()*sizeof(uint16_t))) return false;
        return true;
    }

private:
    int _w = 1, _h = 1;
    std::vector<uint8_t>  _blocked;
    std::vector<uint16_t> _cost;
    uint64_t _rev = 0;
    bool _everNonUnitCost = false;
};

// ============ D* Lite (incremental), standalone engine ============
#if NAV2D_ENABLE_DSTARLITE
class DStarLite {
public:
    explicit DStarLite(const Grid* g=nullptr) : _g(g) {}
    void attach(const Grid* g) { _g = g; _lastSeenRev = 0; _initialized = false; }

    PathResult replan(Cell start, Cell goal, const SearchParams& sp) {
        PathResult out;
        if (!_g) return out;
        if (!_g->inBounds(start.x,start.y) || !_g->inBounds(goal.x,goal.y)) return out;
        if (_g->isBlocked(goal.x,goal.y)) return out;
        if (start == goal) { out.success = true; out.path = {start}; return out; }

        if (!_initialized || _W != _g->width() || _H != _g->height() || goal != _goalCell) {
            initialize(start, goal, sp);
        } else {
            setStart(start);
            _sp = sp;
            if (_g->revision() != _lastSeenRev) {
                initialize(start, goal, sp); // safe fallback
            }
        }
        computeShortestPath();
        out = buildPath();
        _lastSeenRev = _g->revision();
        return out;
    }

    void notifyChangedCells(const std::vector<Cell>& changed) {
        if (!_g || !_initialized) return;
        for (Cell c : changed) {
            if (!_g->inBounds(c.x,c.y)) continue;
            size_t ui = idx(c);
            updateVertex(ui);
            forEachNeighbor(ui, [&](size_t vi, float /*cost*/){ updateVertex(vi); });
        }
        computeShortestPath();
        _lastSeenRev = _g->revision();
    }

    // lightweight metrics
    struct Stats { uint64_t pops=0, pushes=0, updates=0; } stats;
    void resetStats(){ stats = {}; }

private:
    struct Key { float k1, k2; };
    struct QItem { Key k; size_t i; };
    struct KeyCmp {
        bool operator()(const QItem& a, const QItem& b) const noexcept {
            if (a.k.k1 > b.k.k1) return true; if (a.k.k1 < b.k.k1) return false;
            if (a.k.k2 > b.k.k2) return true; if (a.k.k2 < b.k.k2) return false;
            return a.i > b.i;
        }
    };
    static constexpr float INF = std::numeric_limits<float>::infinity();
    static constexpr float EPS = 1e-6f;

    const Grid* _g = nullptr;
    int _W = 0, _H = 0; size_t _N = 0;
    std::vector<float> _gval, _rhs;
    std::priority_queue<QItem,std::vector<QItem>,KeyCmp> _open;
    float _km = 0.0f;
    size_t _sStart = (size_t)-1, _sLast = (size_t)-1, _sGoal = (size_t)-1;
    Cell _goalCell{};
    SearchParams _sp{};
    bool _initialized = false;
    uint64_t _lastSeenRev = 0;

    inline size_t idx(Cell c) const { return _g->idx(c.x,c.y); }
    inline Cell   cell(size_t i) const { return Cell{ _g->xof(i), _g->yof(i) }; }

    void initialize(Cell start, Cell goal, const SearchParams& sp) {
        _W=_g->width(); _H=_g->height(); _N=(size_t)_W*_H;
        _gval.assign(_N, INF); _rhs.assign(_N, INF);
        _sp = sp; _goalCell = goal; _sGoal=idx(goal); _sStart=idx(start); _sLast=_sStart; _km=0.0f;
        _open = {}; _rhs[_sGoal] = 0.0f; push(_sGoal, calculateKey(_sGoal));
        _initialized = true; _lastSeenRev = _g->revision(); stats = {};
    }
    void setStart(Cell s) {
        size_t ns = idx(s);
        if (ns != _sStart) {
            _km += _sp.heuristic_weight * hCost(cell(_sLast), cell(ns), _sp.allow_diagonal);
            _sLast = ns; _sStart = ns;
        }
    }
    template<class F> void forEachNeighbor(size_t u, F&& fn) const {
        int x=_g->xof(u), y=_g->yof(u);
        auto consider=[&](int nx,int ny,float base){
            if (!_g->inBounds(nx,ny) || _g->isBlocked(nx,ny)) return;
            if (_sp.allow_diagonal && base>1.1f && !_sp.allow_corner_cutting){
                int dx=nx-x, dy=ny-y;
                if (_g->isBlocked(x+dx,y) || _g->isBlocked(x,y+dy)) return;
            }
            float c = base * (float)_g->moveCost(nx,ny);
            fn(_g->idx(nx,ny), c);
        };
        consider(x+1,y,1.0f); consider(x-1,y,1.0f); consider(x,y+1,1.0f); consider(x,y-1,1.0f);
        if (_sp.allow_diagonal){
            consider(x+1,y+1,DIAG); consider(x-1,y+1,DIAG); consider(x+1,y-1,DIAG); consider(x-1,y-1,DIAG);
        }
    }
    DStarLite::Key calculateKey(size_t u) const {
        float m = std::min(_gval[u], _rhs[u]);
        float h = _sp.heuristic_weight * hCost(cell(_sStart), cell(u), _sp.allow_diagonal);
        return Key{ m + h + _km, m };
    }
    void push(size_t u, Key k) { _open.push(QItem{k,u}); ++stats.pushes; }
    void updateVertex(size_t u) {
        if (u != _sGoal) {
            float best = INF;
            forEachNeighbor(u, [&](size_t v, float c){ best = std::min(best, c + _gval[v]); });
            _rhs[u] = best;
        }
        push(u, calculateKey(u)); ++stats.updates;
    }
    void computeShortestPath() {
        auto need=[&](){
            if (_open.empty()) return false;
            const Key top = _open.top().k, sK = calculateKey(_sStart);
            if (top.k1 < sK.k1 - EPS) return true;
            if (top.k1 > sK.k1 + EPS) return false;
            if (top.k2 < sK.k2 - EPS) return true;
            if (std::fabs(_rhs[_sStart] - _gval[_sStart]) > EPS) return true;
            return false;
        };
        while (need()){
            auto qi=_open.top(); _open.pop(); ++stats.pops;
            size_t u=qi.i; Key kOld=qi.k, kNew=calculateKey(u);
            if (kOld.k1 > kNew.k1 + 1e-6f || std::fabs(kOld.k2 - kNew.k2) > 1e-6f) { push(u,kNew); continue; }
            if (_gval[u] > _rhs[u] + 1e-6f) {
                _gval[u] = _rhs[u];
                forEachNeighbor(u, [&](size_t p, float){ updateVertex(p); });
            } else {
                _gval[u] = INF; updateVertex(u);
                forEachNeighbor(u, [&](size_t p, float){ updateVertex(p); });
            }
        }
    }
    PathResult buildPath() const {
        PathResult out;
        if (std::isinf(_rhs[_sStart])) return out;
        size_t cur=_sStart; out.path.push_back(cell(cur)); float total=0.0f; size_t cap=_N*4;
        while (cur != _sGoal && cap--){
            float best=INF, bestC=0.0f; size_t bestN=cur;
            forEachNeighbor(cur, [&](size_t v, float c){
                float cand = c + _gval[v];
                if (cand + 1e-6f < best) { best=cand; bestN=v; bestC=c; }
            });
            if (bestN==cur || std::isinf(best)) { out.path.clear(); return out; }
            cur=bestN; out.path.push_back(cell(cur)); total += bestC;
        }
        if (cur != _sGoal) { out.path.clear(); return out; }
        out.success=true; out.cost=total; return out;
    }
};
#endif // NAV2D_ENABLE_DSTARLITE

// ============ Planner (A*, JPS, cache, smoothing, flow, D*, HPA*) ============
class Planner {
public:
    explicit Planner(const Grid* g = nullptr) : _g(g)
#if NAV2D_ENABLE_DSTARLITE
    , _dstar(g)
#endif
    {
#if NAV2D_ENABLE_CACHE
        setCacheCapacity(NAV2D_CACHE_CAPACITY);
#endif
    }
    void attach(const Grid* g) {
        _g = g;
#if NAV2D_ENABLE_DSTARLITE
        _dstar.attach(g);
#endif
#if NAV2D_ENABLE_HPA
        _hpa.attach(g);
#endif
    }

    // ---------- One-shot path (A* / JPS / HPA) ----------
    PathResult findPath(Cell start, Cell goal, const SearchParams& sp = {}) {
        PathResult out;
        if (!precheck(start, goal, out)) return out;

#if NAV2D_ENABLE_CACHE
        if (sp.use_cache) {
            if (auto cached = _cacheGet(start, goal, sp)) {
                ++stats.cacheHits;
                out = *cached;
                if (out.success) smooth(out);
                return out;
            }
            ++stats.cacheMisses;
        }
#endif

#if NAV2D_ENABLE_HPA
        if (sp.use_hpa) {
            if (_hpa.maybeRebuild(sp.hpa_cluster_size, sp.hpa_rebuild_threshold)) {/*rebuilt*/}
            out = _hpa.findPath(start, goal, sp);
            if (out.success) {
                smooth(out);
#if NAV2D_ENABLE_CACHE
                if (sp.use_cache) _cachePut(start, goal, sp, out, false);
#endif
                return out;
            }
            // fallback to A*/JPS if HPA couldn't find a route
        }
#endif

        if (shouldUseJPS(sp)) {
#if NAV2D_ENABLE_JPS
            out = findPathJPS(start, goal, sp);
            if (out.success) smooth(out);
#  if NAV2D_ENABLE_CACHE
            if (sp.use_cache) _cachePut(start, goal, sp, out, false);
#  endif
            return out;
#endif
        }

        out = findPathAStar(start, goal, sp);
        if (out.success) smooth(out);
#if NAV2D_ENABLE_CACHE
        if (sp.use_cache) _cachePut(start, goal, sp, out, false);
#endif
        return out;
    }

    // ---------- Incremental re-plan (D* Lite) ----------
#if NAV2D_ENABLE_DSTARLITE
    PathResult replan(Cell start, Cell goal, const SearchParams& sp = {}) {
        PathResult out;
        if (!_g) return out;
        out = _dstar.replan(start, goal, sp);
        if (out.success) smooth(out);
        return out;
    }
    void notifyTerrainChanged(const std::vector<Cell>& changed) {
        _dstar.attach(_g);
        _dstar.notifyChangedCells(changed);
    }
#endif

    // ---------- Smoothing ----------
    void smooth(PathResult& pr) const {
        if (!_g || pr.path.size() < 3) return;
        std::vector<Cell> out;
        out.reserve(pr.path.size());
        size_t anchor = 0, k = pr.path.size()-1;
        out.push_back(pr.path[0]);
        for (size_t j = 1; j <= k; ++j) {
            if (!hasLineOfSight(pr.path[anchor], pr.path[j])) {
                out.push_back(pr.path[j-1]);
                anchor = j-1;
            }
        }
        out.push_back(pr.path.back());
        pr.path.swap(out);
    }

    // ---------- Flow fields ----------
#if NAV2D_ENABLE_FLOWFIELD
    struct FlowField {
        std::vector<float> dist;     // distance/cost to target(s)
        std::vector<uint8_t> dir8;   // 0..7 index into DIR8, 255 for none
        int w=0, h=0;
        bool valid() const { return w>0 && h>0 && dist.size()==(size_t)w*h; }
        Cell step(Cell from) const {
            if (!valid()) return from;
            size_t i = (size_t)from.y * w + from.x;
            if (i >= dir8.size() || dir8[i] == 255) return from;
            Dir d = DIR8[dir8[i]];
            return Cell{from.x + d.dx, from.y + d.dy};
        }
    };

    // Single target
    FlowField computeFlowField(Cell target, bool allow_diagonal = true, const std::vector<float>* extraScalar = nullptr, float extraW = 0.0f) const {
        std::vector<Cell> seeds{target};
        return computeFlowFieldMulti(seeds, allow_diagonal, extraScalar, extraW);
    }

    // Multi-source (e.g. multiple stockpiles). Optional extra scalar per-cell cost blended into edges.
    FlowField computeFlowFieldMulti(const std::vector<Cell>& targets, bool allow_diagonal = true,
                                    const std::vector<float>* extraScalar = nullptr, float extraW = 0.0f) const {
        FlowField ff; if (!_g) return ff;
        const int W=_g->width(), H=_g->height(); const size_t N=(size_t)W*H;
        ff.w=W; ff.h=H; ff.dist.assign(N, std::numeric_limits<float>::infinity()); ff.dir8.assign(N,255);
        using QN = std::pair<float,size_t>;
        std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;

        auto seed=[&](Cell c){
            if (!_g->inBounds(c.x,c.y) || _g->isBlocked(c.x,c.y)) return;
            size_t i=_g->idx(c.x,c.y); ff.dist[i]=0.0f; pq.push({0.0f, i});
        };
        for (auto c: targets) seed(c);

        auto relax = [&](size_t from, int nx, int ny, float step){
            if (!_g->inBounds(nx,ny) || _g->isBlocked(nx,ny)) return;
            size_t ni=_g->idx(nx,ny);
            float add = step * (float)_g->moveCost(nx,ny);
            if (extraScalar && ni < extraScalar->size()) add += extraW * (*extraScalar)[ni];
            float nd = ff.dist[from] + add;
            if (nd + 1e-6f < ff.dist[ni]) { ff.dist[ni]=nd; pq.push({nd,ni}); }
        };

        while (!pq.empty()){
            auto [cd, ci]=pq.top(); pq.pop(); if (cd > ff.dist[ci]) continue;
            int cx=_g->xof(ci), cy=_g->yof(ci);
            for (int k=0;k<4;++k){ Dir d=DIR4[k]; relax(ci, cx+d.dx, cy+d.dy, 1.0f); }
            if (allow_diagonal){
                for (int k=4;k<8;++k){
                    Dir d=DIR8[k];
                    if (_g->isBlocked(cx + d.dx, cy) || _g->isBlocked(cx, cy + d.dy)) continue; // corner rule
                    relax(ci, cx+d.dx, cy+d.dy, DIAG);
                }
            }
        }

        // Fill dir8: point to neighbor with smallest dist
        for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
            size_t i=_g->idx(x,y); if (_g->isBlocked(x,y)) { ff.dir8[i]=255; continue; }
            float best=ff.dist[i]; uint8_t bestk=255;
            auto consider=[&](int nx,int ny,uint8_t k){ if (!_g->inBounds(nx,ny)) return; size_t ni=_g->idx(nx,ny);
                if (ff.dist[ni] + 1e-6f < best) { best=ff.dist[ni]; bestk=k; } };
            for (uint8_t k=0;k<4;++k){ Dir d=DIR4[k]; consider(x+d.dx,y+d.dy,k); }
            if (allow_diagonal) for (uint8_t k=4;k<8;++k){ Dir d=DIR8[k]; consider(x+d.dx,y+d.dy,k); }
            ff.dir8[i]=bestk;
        }
        return ff;
    }

    // Blend a base flow field with a scalar hazard field: dist' = w1*dist + w2*hazard; recompute dir8 on blended cost
    FlowField blendFlowField(const FlowField& base, const std::vector<float>& hazard, float w1, float w2, bool allow_diagonal=true) const {
        FlowField ff=base; if (!base.valid()) return ff;
        const size_t N=ff.dist.size();
        ff.dist.resize(std::min(N, hazard.size()));
        for (size_t i=0;i<ff.dist.size();++i) ff.dist[i] = w1*base.dist[i] + w2*hazard[i];
        // rebuild dir8 on blended "dist"
        auto get=[&](int x,int y)->float{
            if (x<0||y<0||x>=ff.w||y>=ff.h) return std::numeric_limits<float>::infinity();
            return ff.dist[(size_t)y*ff.w + x];
        };
        for (int y=0;y<ff.h;++y) for (int x=0;x<ff.w;++x) {
            if (_g && _g->isBlocked(x,y)) { ff.dir8[(size_t)y*ff.w+x]=255; continue; }
            float best=get(x,y); uint8_t bestk=255;
            auto consider=[&](int nx,int ny,uint8_t k){
                float v=get(nx,ny); if (v + 1e-6f < best) { best=v; bestk=k; }
            };
            for (uint8_t k=0;k<4;++k){ Dir d=DIR4[k]; consider(x+d.dx,y+d.dy,k); }
            if (allow_diagonal) for (uint8_t k=4;k<8;++k){ Dir d=DIR8[k]; consider(x+d.dx,y+d.dy,k); }
            ff.dir8[(size_t)y*ff.w+x]=bestk;
        }
        return ff;
    }

    // Gradient of the flow field's distance at a cell (for steering)
    struct Grad2 { float gx=0.0f, gy=0.0f; };
    Grad2 sampleGradient(const FlowField& ff, Cell c) const {
        Grad2 g{}; if (!ff.valid() || c.x<=0 || c.y<=0 || c.x>=ff.w-1 || c.y>=ff.h-1) return g;
        auto val=[&](int x,int y){ return ff.dist[(size_t)y*ff.w+x]; };
        g.gx = 0.5f*(val(c.x+1,c.y) - val(c.x-1,c.y));
        g.gy = 0.5f*(val(c.x,c.y+1) - val(c.x,c.y-1));
        return g;
    }

    // ASCII dump of a flow field's directions
    std::string debugDumpFlow(const FlowField& ff) const {
        if (!ff.valid()) return {};
        static const char* sym8 = "><^vNE SW"; // we’ll map manually below
        auto sym=[&](uint8_t k)->char{
            switch (k){
                case 0: return '>'; case 1: return '<'; case 2: return 'v'; case 3: return '^';
                case 4: return '\\'; case 5: return '/'; case 6: return '/'; case 7: return '\\';
                default: return '.';
            }
        };
        std::ostringstream oss;
        for (int y=0;y<ff.h;++y){
            for (int x=0;x<ff.w;++x){
                if (_g && _g->isBlocked(x,y)) { oss << '#'; continue; }
                oss << sym(ff.dir8[(size_t)y*ff.w+x]);
            }
            oss << '\n';
        }
        return oss.str();
    }
#endif // FLOWFIELD

    // ---------- Crowd avoidance ----------
    struct CrowdField {
        int w=0,h=0;
        std::vector<float> density; // decaying occupancy
        float decay=0.85f;
        void reset(int W,int H){ w=W; h=H; density.assign((size_t)W*H, 0.0f); }
        void beginFrame(){ for (auto& v : density) v *= decay; }
        void stamp(Cell c, float amount=1.0f){
            if ((unsigned)c.x < (unsigned)w && (unsigned)c.y < (unsigned)h)
                density[(size_t)c.y*w + c.x] += amount;
        }
        float at(Cell c) const {
            if ((unsigned)c.x < (unsigned)w && (unsigned)c.y < (unsigned)h)
                return density[(size_t)c.y*w + c.x];
            return 1e9f;
        }
    };

    // Choose a step near 'desired' minimizing density penalty; deterministic tiebreak
    Cell avoidCrowd(Cell current, Cell desired, const CrowdField& cf, float densityWeight=1.0f) const {
        if (!_g) return current;
        struct Cand { Cell c; float score; };
        std::array<Cand,5> cands{};
        cands[0] = { desired, scoreStep(current, desired, cf, densityWeight) };
        cands[1] = { Cell{current.x + sgn(desired.x-current.x), current.y}, scoreStep(current, {current.x + sgn(desired.x-current.x), current.y}, cf, densityWeight) };
        cands[2] = { Cell{current.x, current.y + sgn(desired.y-current.y)}, scoreStep(current, {current.x, current.y + sgn(desired.y-current.y)}, cf, densityWeight) };
        cands[3] = { Cell{current.x + sgn(desired.x-current.x), current.y - sgn(desired.y-current.y)}, scoreStep(current, {current.x + sgn(desired.x-current.x), current.y - sgn(desired.y-current.y)}, cf, densityWeight) };
        cands[4] = { Cell{current.x - sgn(desired.x-current.x), current.y + sgn(desired.y-current.y)}, scoreStep(current, {current.x - sgn(desired.x-current.x), current.y + sgn(desired.y-current.y)}, cf, densityWeight) };
        // sort by score; stable to keep determinism
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b){ return a.score < b.score; });
        for (const auto& c : cands){
            if (c.c == current) continue;
            if (_g->inBounds(c.c.x,c.c.y) && !_g->isBlocked(c.c.x,c.c.y)) return c.c;
        }
        return current;
    }

#if NAV2D_ENABLE_IMGUI
    // Optional ImGui overlay: draw obstacles and path
    void debugDrawImGui(const PathResult* pr=nullptr, float cellSize=8.0f, ImU32 colGrid=IM_COL32(80,80,80,60),
                        ImU32 colWall=IM_COL32(200,60,60,255), ImU32 colPath=IM_COL32(80,200,80,255)) const {
        if (!_g) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 origin = ImGui::GetMainViewport()->Pos;
        for (int y=0;y<_g->height();++y) for (int x=0;x<_g->width();++x){
            ImVec2 p0(origin.x + x*cellSize, origin.y + y*cellSize);
            ImVec2 p1(p0.x + cellSize, p0.y + cellSize);
            dl->AddRect(p0, p1, colGrid);
            if (_g->isBlocked(x,y)) dl->AddRectFilled(p0,p1,colWall);
        }
        if (pr && pr->success){
            for (size_t i=1;i<pr->path.size();++i){
                ImVec2 a(origin.x + pr->path[i-1].x*cellSize + cellSize*0.5f,
                         origin.y + pr->path[i-1].y*cellSize + cellSize*0.5f);
                ImVec2 b(origin.x + pr->path[i].x*cellSize + cellSize*0.5f,
                         origin.y + pr->path[i].y*cellSize + cellSize*0.5f);
                dl->AddLine(a,b,colPath,2.0f);
            }
        }
    }
#endif

    // ---------- Debug stats ----------
    struct Stats {
        uint64_t astarExpansions=0, jpsExpansions=0, heapPushes=0;
        uint64_t cacheHits=0, cacheMisses=0;
#if NAV2D_ENABLE_HPA
        uint64_t hpaNodeExpansions=0;
#endif
#if NAV2D_ENABLE_DSTARLITE
        uint64_t dstarPops=0, dstarPushes=0;
#endif
    } stats;
    void resetStats(){ stats = {}; }

#if NAV2D_ENABLE_DEBUG
    std::string debugDumpPath(const PathResult& pr) const {
        if (!_g) return {};
        std::vector<std::string> canvas(_g->height(), std::string(_g->width(), '.'));
        for (int y=0; y<_g->height(); ++y) for (int x=0; x<_g->width(); ++x)
            if (_g->isBlocked(x,y)) canvas[y][x] = '#';
        for (auto c : pr.path) if (_g->inBounds(c.x,c.y)) canvas[c.y][c.x] = '*';
        std::ostringstream oss; for (auto& row: canvas) oss << row << '\n'; return oss.str();
    }
#endif

    // ---------- Cache controls ----------
#if NAV2D_ENABLE_CACHE
    void setCacheCapacity(size_t cap) {
        _cache.capacity = std::max<size_t>(cap, 0);
        _cacheEvictIfNeeded();
    }
#endif

private:
    // ---------- A* -----------
    struct Node {
        float g = std::numeric_limits<float>::infinity();
        float f = std::numeric_limits<float>::infinity();
        size_t parent = invalid;
        bool   open = false;
        bool   closed = false;
    };

    const Grid* _g = nullptr;
    std::vector<Node> _nodes;
    std::vector<size_t> _open;

    static constexpr size_t invalid = std::numeric_limits<size_t>::max();

    struct Cmp {
        const std::vector<Node>* n;
        bool operator()(size_t a, size_t b) const noexcept {
            const auto& A = (*n)[a]; const auto& B = (*n)[b];
            if (A.f > B.f) return true; if (A.f < B.f) return false;
#if NAV2D_DETERMINISTIC
            return a > b;
#else
            return false;
#endif
        }
    };

    bool precheck(Cell start, Cell goal, PathResult& out) const {
        if (!_g) return false;
        if (!_g->inBounds(start.x,start.y) || !_g->inBounds(goal.x,goal.y)) return false;
        if (_g->isBlocked(goal.x,goal.y)) return false;
        if (start == goal) { out.success = true; out.path = {start}; return false; }
        return true;
    }

    void pushOpen(size_t i){ _open.push_back(i); std::push_heap(_open.begin(), _open.end(), Cmp{&_nodes}); ++stats.heapPushes; }
    size_t popOpen(){ std::pop_heap(_open.begin(), _open.end(), Cmp{&_nodes}); size_t i=_open.back(); _open.pop_back(); return i; }

    template<class F>
    void forEachNeighbor(int x, int y, bool diag, F&& f) const {
        if (_g->inBounds(x+1,y)) f(x+1,y,1.0f);
        if (_g->inBounds(x-1,y)) f(x-1,y,1.0f);
        if (_g->inBounds(x,y+1)) f(x,y+1,1.0f);
        if (_g->inBounds(x,y-1)) f(x,y-1,1.0f);
        if (!diag) return;
        if (_g->inBounds(x+1,y+1)) f(x+1,y+1,DIAG);
        if (_g->inBounds(x-1,y+1)) f(x-1,y+1,DIAG);
        if (_g->inBounds(x+1,y-1)) f(x+1,y-1,DIAG);
        if (_g->inBounds(x-1,y-1)) f(x-1,y-1,DIAG);
    }

    PathResult findPathAStar(Cell start, Cell goal, const SearchParams& sp) {
        PathResult out;
        const int W=_g->width(), H=_g->height(); const size_t N=(size_t)W*H;
        _nodes.assign(N, Node{}); _open.clear(); _open.reserve(256);
        size_t sIdx=_g->idx(start.x,start.y), gIdx=_g->idx(goal.x,goal.y);
        Node& sN=_nodes[sIdx]; sN.g=0.0f; sN.f=sp.heuristic_weight*hCost(start,goal,sp.allow_diagonal); sN.parent=sIdx; sN.open=true; pushOpen(sIdx);
        unsigned expansions=0;
        while (!_open.empty()){
            size_t cur=popOpen(); Node& cn=_nodes[cur]; if (cn.closed) continue; cn.closed=true; ++stats.astarExpansions;
            if (cur==gIdx){ reconstruct(gIdx, sIdx, out); return out; }
            if (sp.max_expansions && ++expansions > sp.max_expansions){ reconstruct(cur, sIdx, out); return out; }
            int cx=_g->xof(cur), cy=_g->yof(cur);
            forEachNeighbor(cx,cy,sp.allow_diagonal,[&](int nx,int ny,float step){
                if (!_g->inBounds(nx,ny) || _g->isBlocked(nx,ny)) return;
                if (!sp.allow_corner_cutting && step>1.1f){
                    int dx=nx-cx, dy=ny-cy; if (_g->isBlocked(cx+dx,cy) || _g->isBlocked(cx,cy+dy)) return;
                }
                size_t ni=_g->idx(nx,ny); Node& nn=_nodes[ni]; if (nn.closed) return;
                float moveCost = step * (float)_g->moveCost(nx,ny);
                float tentative = cn.g + moveCost;
                if (!nn.open || tentative < nn.g){
                    nn.g=tentative; nn.parent=cur;
                    float h=sp.heuristic_weight*hCost(Cell{nx,ny},goal,sp.allow_diagonal);
                    nn.f=nn.g+h; if (!nn.open){ nn.open=true; pushOpen(ni);} else pushOpen(ni);
                }
            });
        }
        return out;
    }

    void reconstruct(size_t goal, size_t start, PathResult& out) const {
        out.path.clear(); out.cost = _nodes[goal].g; size_t cur=goal;
        while (true){
            out.path.push_back(Cell{_g->xof(cur), _g->yof(cur)});
            if (cur==start) break; cur=_nodes[cur].parent; if (cur==invalid){ out.path.clear(); out.success=false; return; }
        }
        std::reverse(out.path.begin(), out.path.end()); out.success=true;
    }

    bool hasLineOfSight(Cell a, Cell b) const {
        int x0=a.x,y0=a.y,x1=b.x,y1=b.y; int dx=std::abs(x1-x0), sx=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy,x=x0,y=y0;
        auto blocked=[&](int gx,int gy){ return !_g->inBounds(gx,gy) || _g->isBlocked(gx,gy); };
        while (true){ if (blocked(x,y)) return false; if (x==x1 && y==y1) break; int e2=2*err; if (e2>=dy){ err += dy; x += sx; } if (e2<=dx){ err += dx; y += sy; } }
        return true;
    }

    bool shouldUseJPS(const SearchParams& sp) const {
#if NAV2D_ENABLE_JPS
        return sp.prefer_jps && _g && _g->uniformCostEver();
#else
        (void)sp; return false;
#endif
    }

    // ---------- JPS -----------
#if NAV2D_ENABLE_JPS
    PathResult findPathJPS(Cell start, Cell goal, const SearchParams& sp) {
        PathResult out;
        const int W=_g->width(), H=_g->height(); const size_t N=(size_t)W*H;
        _nodes.assign(N, Node{}); _open.clear(); _open.reserve(256);
        const size_t sIdx=_g->idx(start.x,start.y), gIdx=_g->idx(goal.x,goal.y);
        Node& sN=_nodes[sIdx]; sN.g=0.0f; sN.f=sp.heuristic_weight*hCost(start,goal,sp.allow_diagonal); sN.parent=sIdx; sN.open=true; pushOpen(sIdx);
        unsigned expansions=0;

        auto dirFromParent=[&](size_t cur)->Dir{
            size_t p=_nodes[cur].parent; if (p==cur || p==invalid) return Dir{0,0};
            int cx=_g->xof(cur), cy=_g->yof(cur), px=_g->xof(p), py=_g->yof(p);
            return Dir{ sgn(cx-px), sgn(cy-py) };
        };
        auto identifySuccessors=[&](size_t cur)->void{
            int cx=_g->xof(cur), cy=_g->yof(cur);
            Dir pd = dirFromParent(cur);
            std::vector<Dir> neighs; getNeighborsPruned(cx,cy,pd,sp.allow_diagonal,sp.allow_corner_cutting,neighs);
            for (auto nd : neighs) {
                Cell jp = jump(Cell{cx,cy}, nd, goal, sp);
                if (jp.x==-9999) continue;
                size_t ji=_g->idx(jp.x,jp.y);
                float d = distChebyshev(Cell{cx,cy}, jp);
                float ng = _nodes[cur].g + d;
                Node& nn=_nodes[ji]; if (nn.closed) continue;
                if (!nn.open || ng < nn.g){
                    nn.g=ng; nn.parent=cur;
                    nn.f=ng + sp.heuristic_weight*hCost(jp,goal,sp.allow_diagonal);
                    if (!nn.open){ nn.open=true; pushOpen(ji);} else pushOpen(ji);
                }
            }
        };

        while (!_open.empty()){
            size_t cur=popOpen(); Node& cn=_nodes[cur]; if (cn.closed) continue; cn.closed=true; ++stats.jpsExpansions;
            if (cur==gIdx){ reconstruct(gIdx, sIdx, out); return out; }
            if (sp.max_expansions && ++expansions > sp.max_expansions){ reconstruct(cur, sIdx, out); return out; }
            identifySuccessors(cur);
        }
        return out;
    }
    static float distChebyshev(Cell a, Cell b) {
        int dx=std::abs(a.x-b.x), dy=std::abs(a.y-b.y); int m=std::min(dx,dy), M=std::max(dx,dy);
        return (float)m*DIAG + (float)(M-m);
    }
    void getNeighborsPruned(int x, int y, Dir pd, bool allow_diag, bool no_corner_cut, std::vector<Dir>& out) const {
        out.clear();
        auto passable=[&](int nx,int ny){ return _g->inBounds(nx,ny) && !_g->isBlocked(nx,ny); };
        if (pd.dx==0 && pd.dy==0) {
            for (int k=0;k<4;++k){ Dir d=DIR4[k]; if (passable(x+d.dx,y+d.dy)) out.push_back(d); }
            if (allow_diag) for (int k=4;k<8;++k){
                Dir d=DIR8[k]; if (!passable(x+d.dx,y+d.dy)) continue;
                if (no_corner_cut && (!passable(x+d.dx,y) || !passable(x,y+d.dy))) continue;
                out.push_back(d);
            }
            return;
        }
        int dx=pd.dx, dy=pd.dy;
        if (dx!=0 && dy!=0) {
            if (passable(x+dx,y) && passable(x+dx,y+dy)) out.push_back(Dir{dx,0});
            if (passable(x,y+dy) && passable(x+dy,y+dy)) out.push_back(Dir{0,dy});
            if (allow_diag && passable(x+dx,y+dy) && (!no_corner_cut || (passable(x+dx,y) && passable(x,y+dy)))) out.push_back(Dir{dx,dy});
            if (allow_diag){
                if (!passable(x-dx,y) && passable(x-dx,y+dy)) out.push_back(Dir{-dx,dy});
                if (!passable(x,y-dy) && passable(x+dx,y-dy)) out.push_back(Dir{dx,-dy});
            }
        } else {
            if (dx==0 && dy!=0){
                if (passable(x,y+dy)) out.push_back(Dir{0,dy});
                if (allow_diag){
                    if (!passable(x+1,y) && passable(x+1,y+dy)) out.push_back(Dir{1,dy});
                    if (!passable(x-1,y) && passable(x-1,y+dy)) out.push_back(Dir{-1,dy});
                }
            } else if (dy==0 && dx!=0){
                if (passable(x+dx,y)) out.push_back(Dir{dx,0});
                if (allow_diag){
                    if (!passable(x,y+1) && passable(x+dx,y+1)) out.push_back(Dir{dx,1});
                    if (!passable(x,y-1) && passable(x+dx,y-1)) out.push_back(Dir{dx,-1});
                }
            }
        }
    }
    Cell jump(Cell from, Dir d, Cell goal, const SearchParams& sp) const {
        int x=from.x + d.dx, y=from.y + d.dy;
        auto passable=[&](int nx,int ny){ return _g->inBounds(nx,ny) && !_g->isBlocked(nx,ny); };
        if (!passable(x,y)) return Cell{-9999,-9999};
        Cell here{x,y}; if (here==goal) return here;
        if (d.dx!=0 && d.dy!=0){
            if ((passable(x - d.dx, y + d.dy) && !passable(x - d.dx, y)) ||
                (passable(x + d.dx, y - d.dy) && !passable(x, y - d.dy))) return here;
            if (jump(here, Dir{d.dx,0}, goal, sp).x != -9999) return here;
            if (jump(here, Dir{0,d.dy}, goal, sp).x != -9999) return here;
        } else {
            if (d.dx!=0){
                if ((passable(x + d.dx, y + 1) && !passable(x, y + 1)) ||
                    (passable(x + d.dx, y - 1) && !passable(x, y - 1))) return here;
            } else if (d.dy!=0){
                if ((passable(x + 1, y + d.dy) && !passable(x + 1, y)) ||
                    (passable(x - 1, y + d.dy) && !passable(x - 1, y))) return here;
            }
        }
        if (sp.allow_corner_cutting || d.dx==0 || d.dy==0 ||
            (passable(x - d.dx, y) && passable(x, y - d.dy))) {
            return jump(here, d, goal, sp);
        }
        return Cell{-9999,-9999};
    }
#endif // JPS

    // ---------- Path Cache -----------
#if NAV2D_ENABLE_CACHE
    struct PathCacheKey {
        uint64_t rev; int sx,sy,gx,gy; uint8_t flags;
        bool operator==(const PathCacheKey& o) const noexcept {
            return rev==o.rev && sx==o.sx && sy==o.sy && gx==o.gx && gy==o.gy && flags==o.flags;
        }
    };
    struct PCKeyHash {
        size_t operator()(const PathCacheKey& k) const noexcept {
            uint64_t x=k.rev;
            auto mix=[&](uint64_t v){ x ^= v + 0x9e3779b97f4a7c15ULL + (x<<6) + (x>>2); };
            mix(((uint64_t)(uint32_t)k.sx<<32) | (uint32_t)k.sy);
            mix(((uint64_t)(uint32_t)k.gx<<32) | (uint32_t)k.gy);
            mix(k.flags);
            return (size_t)x;
        }
    };
    struct CacheEntry { PathCacheKey key; PathResult value; };
    struct LRU {
        size_t capacity = NAV2D_CACHE_CAPACITY;
        std::list<CacheEntry> list; // front = most recent
        std::unordered_map<PathCacheKey, std::list<CacheEntry>::iterator, PCKeyHash> map;
    } _cache;
    uint8_t _flagsFromParams(const SearchParams& sp) const {
        uint8_t f = 0;
        if (sp.allow_diagonal)       f |= 1<<0;
        if (sp.allow_corner_cutting) f |= 1<<1;
#if NAV2D_ENABLE_JPS
        if (sp.prefer_jps)           f |= 1<<2;
#endif
#if NAV2D_ENABLE_HPA
        if (sp.use_hpa)              f |= 1<<3;
#endif
        return f;
    }
    std::optional<PathResult> _cacheGet(Cell s, Cell g, const SearchParams& sp) {
        if (!_g) return std::nullopt;
        PathCacheKey k{_g->revision(), s.x,s.y,g.x,g.y, _flagsFromParams(sp)};
        auto it = _cache.map.find(k);
        if (it == _cache.map.end()) return std::nullopt;
        _cache.list.splice(_cache.list.begin(), _cache.list, it->second);
        return it->second->value;
    }
    void _cachePut(Cell s, Cell g, const SearchParams& sp, const PathResult& pr, bool) {
        if (!_g || _cache.capacity==0) return;
        PathCacheKey k{_g->revision(), s.x,s.y,g.x,g.y, _flagsFromParams(sp)};
        auto it = _cache.map.find(k);
        if (it != _cache.map.end()) {
            it->second->value = pr;
            _cache.list.splice(_cache.list.begin(), _cache.list, it->second);
            return;
        }
        _cacheEvictIfNeeded(1);
        _cache.list.push_front(CacheEntry{k, pr});
        _cache.map.emplace(k, _cache.list.begin());
    }
    void _cacheEvictIfNeeded(size_t extra=0) {
        while (_cache.map.size() + extra > _cache.capacity && !_cache.list.empty()) {
            auto it = std::prev(_cache.list.end());
            _cache.map.erase(it->key);
            _cache.list.pop_back();
        }
    }
#endif // CACHE

    // ---------- HPA* (Hierarchical Pathfinding) ----------
#if NAV2D_ENABLE_HPA
    class HPA {
    public:
        explicit HPA(const Grid* g=nullptr):_g(g){}
        void attach(const Grid* g){ _g=g; _built=false; }

        bool maybeRebuild(int clusterSize, uint64_t rebuildThreshold) {
            if (!_g) return false;
            if (clusterSize <= 4) clusterSize = 4;
            bool need = (!_built) || (clusterSize != _clusterSize) || (!_built && _g->revision()>0);
            uint64_t rev = _g->revision();
            if (!need && (rev - _lastBuiltRev >= rebuildThreshold)) need = true;
            if (need) { build(clusterSize); return true; }
            return false;
        }

        PathResult findPath(Cell start, Cell goal, const SearchParams& sp) {
            PathResult out;
            if (!_g || !_built) return out;
            if (!_inBounds(start) || !_inBounds(goal)) return out;
            if (_g->isBlocked(goal.x,goal.y)) return out;
            if (start == goal) { out.success=true; out.path={start}; return out; }

            // Map to clusters
            int cs=_clusterSize;
            int scid = clusterIdOf(start), gcid = clusterIdOf(goal);
            if (scid<0 || gcid<0) return out;

            // Build temporary graph nodes for start/goal, connect to local portals
            int startNode = (int)_nodes.size();
            int goalNode  = startNode+1;
            PNode nStart{startNode, start, scid, -1};
            PNode nGoal{goalNode, goal, gcid, -1};
            _tempNodes.clear(); _tempNodes.push_back(nStart); _tempNodes.push_back(nGoal);

            _tempAdj.clear(); _tempAdj.resize(2); // we’ll map temp index 0->start,1->goal to actual node ids lazily

            // Connect start to portals in its cluster
            const auto& S = _clusterPortals[scid];
            for (int pn : S){
                float d = localDistance(start, _nodes[pn].c, sp, _clusters[scid].bounds);
                if (std::isfinite(d)) addTempEdge(0, pn, d);
            }
            // Connect goal
            const auto& G = _clusterPortals[gcid];
            for (int pn : G){
                float d = localDistance(goal, _nodes[pn].c, sp, _clusters[gcid].bounds);
                if (std::isfinite(d)) addTempEdge(1, pn, d);
            }
            // If start/goal have no local portal connectivity, fallback (will fail)
            // Abstract A*
            PathResult abstractPath = abstractSearch(startNode, goalNode, sp);
            if (!abstractPath.success) return out;

            // Stitch segments
            out = stitch(abstractPath, sp);
            return out;
        }

        // Serialization (structure only; distances recomputed on load)
        void serialize(std::ostream& os) const {
            os.write((const char*)&_clusterSize, sizeof(_clusterSize));
            int CW=_cw, CH=_ch; os.write((const char*)&CW, sizeof(CW)); os.write((const char*)&CH, sizeof(CH));
            int Cn=(int)_clusters.size(); os.write((const char*)&Cn, sizeof(Cn));
            for (const auto& c : _clusters){
                os.write((const char*)&c.bounds, sizeof(Rect));
            }
            int Nn=(int)_nodes.size(); os.write((const char*)&Nn, sizeof(Nn));
            for (const auto& n : _nodes){
                os.write((const char*)&n.id,sizeof(n.id));
                os.write((const char*)&n.c,sizeof(n.c));
                os.write((const char*)&n.cluster,sizeof(n.cluster));
                os.write((const char*)&n.crossPeer,sizeof(n.crossPeer));
            }
            // Portal lists per cluster
            for (const auto& v : _clusterPortals){
                int sz=(int)v.size(); os.write((const char*)&sz,sizeof(sz));
                for (int idx : v) os.write((const char*)&idx,sizeof(idx));
            }
        }
        bool deserialize(std::istream& is) {
            if (!_g) return false;
            int cs=0,CW=0,CH=0;
            if (!is.read((char*)&cs,sizeof(cs))) return false;
            if (!is.read((char*)&CW,sizeof(CW))) return false;
            if (!is.read((char*)&CH,sizeof(CH))) return false;
            _clusterSize=cs; _cw=CW; _ch=CH;

            int Cn=0; if (!is.read((char*)&Cn,sizeof(Cn))) return false;
            _clusters.resize(Cn);
            for (auto& c : _clusters) if (!is.read((char*)&c.bounds,sizeof(Rect))) return false;

            int Nn=0; if (!is.read((char*)&Nn,sizeof(Nn))) return false;
            _nodes.resize(Nn);
            for (auto& n : _nodes){
                if (!is.read((char*)&n.id,sizeof(n.id))) return false;
                if (!is.read((char*)&n.c,sizeof(n.c))) return false;
                if (!is.read((char*)&n.cluster,sizeof(n.cluster))) return false;
                if (!is.read((char*)&n.crossPeer,sizeof(n.crossPeer))) return false;
            }

            _clusterPortals.resize(Cn);
            for (auto& v : _clusterPortals){
                int sz=0; if (!is.read((char*)&sz,sizeof(sz))) return false;
                v.resize(sz);
                for (int i=0;i<sz;++i) if (!is.read((char*)&v[i],sizeof(v[i]))) return false;
            }

            // Rebuild adjacency distances
            rebuildAdjacency();
            _lastBuiltRev = _g->revision(); _built=true;
            return true;
        }

        // lightweight exposition for debug/profile
        struct PNode { int id; Cell c; int cluster; int crossPeer; };
        struct Cluster { Rect bounds; };
        const std::vector<Cluster>& clusters() const { return _clusters; }
        const std::vector<PNode>& nodes() const { return _nodes; }

    private:
        const Grid* _g=nullptr;
        bool _built=false;
        uint64_t _lastBuiltRev=0;
        int _clusterSize=16;
        int _cw=0,_ch=0;

        struct Edge { int to; float w; };
        std::vector<Cluster> _clusters;
        std::vector<PNode>   _nodes;               // portal nodes (both sides)
        std::vector<std::vector<Edge>> _adj;       // adjacency among _nodes
        std::vector<std::vector<int>>  _clusterPortals; // node indices per cluster

        // temp nodes/edges during query (start/goal virtual nodes)
        std::vector<PNode> _tempNodes;
        std::vector<std::vector<Edge>> _tempAdj;

        bool _inBounds(Cell c) const { return _g->inBounds(c.x,c.y) && !_g->isBlocked(c.x,c.y); }

        void build(int clusterSize) {
            _clusterSize = std::max(4, clusterSize);
            _cw = (_g->width()  + _clusterSize - 1)/_clusterSize;
            _ch = (_g->height() + _clusterSize - 1)/_clusterSize;
            _clusters.clear(); _clusters.reserve(_cw*_ch);
            for (int cy=0; cy<_ch; ++cy){
                for (int cx=0; cx<_cw; ++cx){
                    Rect r; r.x=cx*_clusterSize; r.y=cy*_clusterSize;
                    r.w=std::min(_clusterSize, _g->width()  - r.x);
                    r.h=std::min(_clusterSize, _g->height() - r.y);
                    _clusters.push_back(Cluster{r});
                }
            }
            _nodes.clear(); _adj.clear(); _clusterPortals.assign(_clusters.size(), {});
            // Extract portals: compress contiguous passable runs along inter-cluster borders
            extractPortals();
            // Build adjacency: inter-cluster (portal pair) + intra-cluster (pairwise local distances)
            rebuildAdjacency();
            _built = true;
            _lastBuiltRev = _g->revision();
        }

        int clusterId(int cx, int cy) const {
            if (cx<0||cy<0||cx>=_cw||cy>=_ch) return -1;
            return cy*_cw + cx;
        }
        int clusterIdOf(Cell c) const {
            int cx = std::min(_cw-1, std::max(0, c.x / _clusterSize));
            int cy = std::min(_ch-1, std::max(0, c.y / _clusterSize));
            int id = clusterId(cx,cy);
            if (id<0) return -1;
            if (!_clusters[id].bounds.contains(c.x,c.y)) return -1;
            return id;
        }

        void extractPortals() {
            // Horizontal borders between vertically adjacent clusters
            for (int cy=0; cy<_ch-1; ++cy){
                for (int cx=0; cx<_cw; ++cx){
                    int a = clusterId(cx,cy), b = clusterId(cx,cy+1);
                    const Rect& ra = _clusters[a].bounds; const Rect& rb = _clusters[b].bounds;
                    int yTop = ra.y + ra.h - 1, yBottom = rb.y; // adjacent rows
                    int x0 = ra.x, x1 = ra.x + ra.w - 1;
                    int runStart=-1;
                    for (int x=x0; x<=x1; ++x){
                        bool ok = _inBounds({x,yTop}) && _inBounds({x,yBottom});
                        if (ok && runStart==-1) runStart = x;
                        if ((!ok || x==x1) && runStart!=-1){
                            int runEnd = ok ? x : x-1;
                            int mid = (runStart + runEnd)/2;
                            // Create a portal pair at mid
                            int idA = (int)_nodes.size(); _nodes.push_back(PNode{idA, {mid,yTop}, a, -1});
                            int idB = (int)_nodes.size(); _nodes.push_back(PNode{idB, {mid,yBottom}, b, -1});
                            _nodes[idA].crossPeer = idB; _nodes[idB].crossPeer = idA;
                            _clusterPortals[a].push_back(idA);
                            _clusterPortals[b].push_back(idB);
                            runStart=-1;
                        }
                    }
                }
            }
            // Vertical borders between horizontally adjacent clusters
            for (int cy=0; cy<_ch; ++cy){
                for (int cx=0; cx<_cw-1; ++cx){
                    int a = clusterId(cx,cy), b = clusterId(cx+1,cy);
                    const Rect& ra = _clusters[a].bounds; const Rect& rb = _clusters[b].bounds;
                    int xRight = ra.x + ra.w - 1, xLeft = rb.x;
                    int y0 = ra.y, y1 = ra.y + ra.h - 1;
                    int runStart=-1;
                    for (int y=y0; y<=y1; ++y){
                        bool ok = _inBounds({xRight,y}) && _inBounds({xLeft,y});
                        if (ok && runStart==-1) runStart = y;
                        if ((!ok || y==y1) && runStart!=-1){
                            int runEnd = ok ? y : y-1;
                            int mid = (runStart + runEnd)/2;
                            int idA = (int)_nodes.size(); _nodes.push_back(PNode{idA, {xRight,mid}, a, -1});
                            int idB = (int)_nodes.size(); _nodes.push_back(PNode{idB, {xLeft,mid},  b, -1});
                            _nodes[idA].crossPeer = idB; _nodes[idB].crossPeer = idA;
                            _clusterPortals[a].push_back(idA);
                            _clusterPortals[b].push_back(idB);
                            runStart=-1;
                        }
                    }
                }
            }
        }

        void rebuildAdjacency() {
            _adj.assign(_nodes.size(), {});
            // Inter-cluster edges: connect cross peers
            for (const auto& n : _nodes){
                if (n.crossPeer >= 0) {
                    // orthogonal step across the boundary
                    float step = 1.0f * (float)_g->moveCost(_nodes[n.crossPeer].c.x, _nodes[n.crossPeer].c.y);
                    _adj[n.id].push_back(Edge{ n.crossPeer, step });
                    _adj[n.crossPeer].push_back(Edge{ n.id, step });
                }
            }
            // Intra-cluster edges: pairwise local distances among portals within the same cluster
            SearchParams sp; sp.allow_diagonal=true; sp.allow_corner_cutting=false;
            for (int cid=0; cid<(int)_clusters.size(); ++cid){
                const auto& plist = _clusterPortals[cid];
                for (size_t i=0;i<plist.size();++i){
                    for (size_t j=i+1;j<plist.size();++j){
                        int a = plist[i], b = plist[j];
                        float d = localDistance(_nodes[a].c, _nodes[b].c, sp, _clusters[cid].bounds);
                        if (std::isfinite(d)) {
                            _adj[a].push_back(Edge{b,d});
                            _adj[b].push_back(Edge{a,d});
                        }
                    }
                }
            }
        }

        // A* restricted inside a cluster rect (for local distances and stitching)
        float localDistance(Cell s, Cell g, const SearchParams& sp, const Rect& restrictRect) const {
            if (!_g->inBounds(s.x,s.y) || !_g->inBounds(g.x,g.y)) return std::numeric_limits<float>::infinity();
            if (!restrictRect.contains(s.x,s.y) || !restrictRect.contains(g.x,g.y)) return std::numeric_limits<float>::infinity();
            if (_g->isBlocked(g.x,g.y)) return std::numeric_limits<float>::infinity();

            const int W=_g->width(), H=_g->height(); const size_t N=(size_t)W*H;
            _tmpNodes.assign(N, TmpNode{}); _heap.clear();
            size_t si=_g->idx(s.x,s.y), gi=_g->idx(g.x,g.y);
            TmpNode& sn=_tmpNodes[si]; sn.g=0.0f; sn.f=sp.heuristic_weight*hCost(s,g,sp.allow_diagonal); sn.parent=si; sn.open=true; pushTmp(si);

            while (!_heap.empty()){
                size_t cur=popTmp(); TmpNode& cn=_tmpNodes[cur]; if (cn.closed) continue; cn.closed=true;
                if (cur==gi) return cn.g;
                int cx=_g->xof(cur), cy=_g->yof(cur);
                auto consider=[&](int nx,int ny,float step){
                    if (!_g->inBounds(nx,ny) || !restrictRect.contains(nx,ny) || _g->isBlocked(nx,ny)) return;
                    if (!sp.allow_corner_cutting && step>1.1f){
                        int dx=nx-cx, dy=ny-cy; if (_g->isBlocked(cx+dx,cy) || _g->isBlocked(cx,cy+dy)) return;
                    }
                    size_t ni=_g->idx(nx,ny); TmpNode& nn=_tmpNodes[ni]; if (nn.closed) return;
                    float c = step * (float)_g->moveCost(nx,ny); float ng = cn.g + c;
                    if (!nn.open || ng < nn.g){
                        nn.g=ng; nn.parent=cur; float h=sp.heuristic_weight*hCost({nx,ny}, g, sp.allow_diagonal); nn.f=ng+h;
                        if (!nn.open){ nn.open=true; pushTmp(ni);} else pushTmp(ni);
                    }
                };
                consider(cx+1,cy,1.0f); consider(cx-1,cy,1.0f); consider(cx,cy+1,1.0f); consider(cx,cy-1,1.0f);
                if (sp.allow_diagonal){ consider(cx+1,cy+1,DIAG); consider(cx-1,cy+1,DIAG); consider(cx+1,cy-1,DIAG); consider(cx-1,cy-1,DIAG); }
            }
            return std::numeric_limits<float>::infinity();
        }

        // Abstract A* over nodes + temp nodes
        PathResult abstractSearch(int startNodeId, int goalNodeId, const SearchParams& sp) {
            // Build a merged adjacency on the fly: permanent nodes (_nodes/_adj) plus temp nodes (start/goal -> edges to permanent)
            const int baseN = (int)_nodes.size();
            const int totalN = baseN + (int)_tempNodes.size();
            std::vector<float> g(totalN, std::numeric_limits<float>::infinity()), f(totalN, std::numeric_limits<float>::infinity());
            std::vector<int> parent(totalN, -1), open(totalN,0), closed(totalN,0);

            auto idxOf=[&](int nid)->int{
                if (nid < baseN) return nid;
                // temp nodes are assigned contiguous ids after base nodes
                if (nid==startNodeId) return baseN + 0;
                if (nid==goalNodeId)  return baseN + 1;
                return -1;
            };
            auto cellOf=[&](int idx)->Cell{
                if (idx < baseN) return _nodes[idx].c;
                return _tempNodes[idx - baseN].c;
            };
            auto neighbors=[&](int u, std::vector<Edge>& out){
                out.clear();
                if (u < baseN) {
                    out = _adj[u];
                } else {
                    // temp 0=start, 1=goal
                    int tidx = u - baseN;
                    out = _tempAdj[tidx];
                }
            };

            // translate virtual start/goal node ids into indices:
            int s = idxOf(startNodeId), gidx = idxOf(goalNodeId);
            if (s<0 || gidx<0) { PathResult pr; return pr; }

            struct PQN{ float f; int id; }; struct PQCmp{ bool operator()(const PQN& a, const PQN& b)const{return a.f>b.f;} };
            std::priority_queue<PQN,std::vector<PQN>,PQCmp> pq;

            g[s]=0.0f; f[s]=sp.heuristic_weight * hCost(cellOf(startNodeId), cellOf(goalNodeId), true); open[s]=1; pq.push({f[s], s});

            std::vector<Edge> neigh;
            while (!pq.empty()){
                int u=pq.top().id; pq.pop(); if (closed[u]) continue; closed[u]=1;
                if (u==gidx) break;
#if NAV2D_ENABLE_DEBUG
                // nothing additional for now
#endif
                neighbors(u, neigh);
                for (const auto& e : neigh){
                    int v = idxOf(e.to);
                    if (v<0) continue;
                    float cand = g[u] + e.w;
                    if (!open[v] || cand < g[v]){
                        g[v]=cand; parent[v]=u;
                        f[v]=g[v] + sp.heuristic_weight*hCost(cellOf(v<baseN?_nodes[v].id: _tempNodes[v-baseN].id),
                                                             cellOf(goalNodeId), true);
                        open[v]=1; pq.push({f[v], v});
                    }
                }
            }

            PathResult ar; if (!closed[gidx]) return ar; // fail
            // reconstruct abstract route
            std::vector<int> seq; for (int cur=gidx; cur!=-1; cur=parent[cur]) seq.push_back(cur);
            std::reverse(seq.begin(), seq.end());
            // encode as PathResult of node-cells
            ar.success=true; ar.cost=g[gidx]; ar.path.clear(); ar.path.reserve(seq.size());
            for (int id : seq) ar.path.push_back(cellOf(id));
            return ar;
        }

        // Convert abstract node path to full cell path via local A* segments
        PathResult stitch(const PathResult& abstractRoute, const SearchParams& sp) const {
            PathResult out; if (!abstractRoute.success) return out;
            out.success=true; out.cost=0.0f; out.path.clear();
            if (abstractRoute.path.size() < 2) return abstractRoute;

            Cell cur = abstractRoute.path.front();
            out.path.push_back(cur);
            for (size_t i=1;i<abstractRoute.path.size();++i){
                Cell nxt = abstractRoute.path[i];
                int ccid = clusterIdOf(cur), ncid = clusterIdOf(nxt);
                Rect r = (ccid>=0 && (_clusters[ccid].bounds.contains(cur.x,cur.y))) ? _clusters[ccid].bounds : _clusters[ncid].bounds;
                // if different clusters and are adjacent boundary cells, just step across
                if (ccid!=ncid && (std::abs(cur.x-nxt.x)+std::abs(cur.y-nxt.y)==1)){
                    out.path.push_back(nxt);
                    out.cost += 1.0f * (float)_g->moveCost(nxt.x,nxt.y);
                    cur = nxt;
                    continue;
                }
                // else, run restricted local A*
                float d = 0.0f;
                std::vector<Cell> seg = localPath(cur, nxt, sp, r, d);
                if (seg.empty()) { out.success=false; out.path.clear(); out.cost=0.0f; return out; }
                // Append, skipping first (already present)
                for (size_t k=1;k<seg.size();++k) out.path.push_back(seg[k]);
                out.cost += d;
                cur = nxt;
            }
            return out;
        }

        std::vector<Cell> localPath(Cell s, Cell g, const SearchParams& sp, const Rect& r, float& costOut) const {
            costOut = 0.0f;
            const int W=_g->width(), H=_g->height(); const size_t N=(size_t)W*H;
            _tmpNodes.assign(N, TmpNode{}); _heap.clear();
            size_t si=_g->idx(s.x,s.y), gi=_g->idx(g.x,g.y);
            TmpNode& sn=_tmpNodes[si]; sn.g=0.0f; sn.f=sp.heuristic_weight*hCost(s,g,sp.allow_diagonal); sn.parent=si; sn.open=true; pushTmp(si);

            while (!_heap.empty()){
                size_t cur=popTmp(); TmpNode& cn=_tmpNodes[cur]; if (cn.closed) continue; cn.closed=true;
                if (cur==gi){
                    std::vector<Cell> path; path.reserve(64);
                    size_t c=gi; costOut = cn.g;
                    while (true){
                        path.push_back(Cell{_g->xof(c), _g->yof(c)});
                        if (c==si) break;
                        c=_tmpNodes[c].parent; if (c==(size_t)-1){ path.clear(); return path; }
                    }
                    std::reverse(path.begin(), path.end());
                    return path;
                }
                int cx=_g->xof(cur), cy=_g->yof(cur);
                auto consider=[&](int nx,int ny,float step){
                    if (!_g->inBounds(nx,ny) || !r.contains(nx,ny) || _g->isBlocked(nx,ny)) return;
                    if (!sp.allow_corner_cutting && step>1.1f){
                        int dx=nx-cx, dy=ny-cy; if (_g->isBlocked(cx+dx,cy) || _g->isBlocked(cx,cy+dy)) return;
                    }
                    size_t ni=_g->idx(nx,ny); TmpNode& nn=_tmpNodes[ni]; if (nn.closed) return;
                    float c = step * (float)_g->moveCost(nx,ny); float ng = cn.g + c;
                    if (!nn.open || ng < nn.g){
                        nn.g=ng; nn.parent=cur; float h=sp.heuristic_weight*hCost({nx,ny}, g, sp.allow_diagonal); nn.f=ng+h;
                        if (!nn.open){ nn.open=true; pushTmp(ni);} else pushTmp(ni);
                    }
                };
                consider(cx+1,cy,1.0f); consider(cx-1,cy,1.0f); consider(cx,cy+1,1.0f); consider(cx,cy-1,1.0f);
                if (sp.allow_diagonal){ consider(cx+1,cy+1,DIAG); consider(cx-1,cy+1,DIAG); consider(cx+1,cy-1,DIAG); consider(cx-1,cy-1,DIAG); }
            }
            return {};
        }

        // temp edge helpers
        void addTempEdge(int tempIndex/*0=start,1=goal*/, int toNode/*permanent*/, float w){
            if ((int)_tempAdj.size() <= tempIndex) _tempAdj.resize(tempIndex+1);
            _tempAdj[tempIndex].push_back(Edge{ toNode, w });
        }

        // tiny local A* state
        struct TmpNode { float g=std::numeric_limits<float>::infinity(); float f=std::numeric_limits<float>::infinity(); size_t parent=(size_t)-1; bool open=false; bool closed=false; };
        std::vector<TmpNode> _tmpNodes;
        std::vector<size_t>  _heap;
        struct TmpCmp {
            HPA* self; bool operator()(size_t a, size_t b) const noexcept {
                const auto& A = self->_tmpNodes[a], &B = self->_tmpNodes[b];
                if (A.f > B.f) return true; if (A.f < B.f) return false;
                return a > b;
            }
        };
        void pushTmp(size_t i){ _heap.push_back(i); std::push_heap(_heap.begin(), _heap.end(), TmpCmp{const_cast<HPA*>(this)}); }
        size_t popTmp(){ std::pop_heap(_heap.begin(), _heap.end(), TmpCmp{this}); size_t i=_heap.back(); _heap.pop_back(); return i; }
    }; // class HPA
    HPA _hpa{nullptr};
#endif // HPA

    // ---------- Helpers ----------
    static float scoreStep(Cell cur, Cell cand, const CrowdField& cf, float densityWeight){
        float dx = (float)(cand.x - cur.x), dy = (float)(cand.y - cur.y);
        float stepBias = (dx*dx + dy*dy); // prefer closer to desired direction (<=2)
        float dens = cf.at(cand);
        return stepBias + densityWeight*dens;
    }

#if NAV2D_ENABLE_DSTARLITE
    DStarLite _dstar;
#endif
}; // class Planner

// ========================== Tests ==========================
#ifdef NAV2D_TESTS
#include <random>
static int test_all() {
    using namespace nav2d;
    // Build a test grid with a wall & two doorways
    Grid g(40,20);
    for (int y=0;y<20;++y) { g.setBlocked(20,y,true); }
    g.setBlocked(20,5,false); g.setBlocked(20,12,false); // doors

    Planner p(&g);
    SearchParams sp; sp.allow_diagonal=true; sp.allow_corner_cutting=false;

    // A* path
    auto prA = p.findPath({2,2},{37,17}, sp);
    if (!prA.success) { std::cout << "A* failed\n"; return 1; }

    // JPS should match cost on unit grid
#if NAV2D_ENABLE_JPS
    auto spJ = sp; spJ.prefer_jps=true;
    auto prJ = p.findPath({2,2},{37,17}, spJ);
    if (!prJ.success || std::abs(prJ.cost - prA.cost) > 1e-4f) { std::cout << "JPS mismatch\n"; return 2; }
#endif

    // Flow field to right-bottom corner
#if NAV2D_ENABLE_FLOWFIELD
    auto ff = p.computeFlowField({37,17}, true);
    Cell cur{2,2}; int steps=0; while (cur != Cell{37,17} && steps++ < 500) cur = ff.step(cur);
    if (cur != Cell{37,17}) { std::cout << "Flow field fail\n"; return 3; }
#endif

    // D* Lite replan with door swap
#if NAV2D_ENABLE_DSTARLITE
    auto pr1 = p.replan({2,2},{37,17}, sp);
    if (!pr1.success) { std::cout << "D* initial fail\n"; return 4; }
    // close y=5 door, open y=8
    g.setBlocked(20,5,true); g.setBlocked(20,8,false);
    p.notifyTerrainChanged({{20,5},{20,8}});
    auto pr2 = p.replan({2,2},{37,17}, sp);
    if (!pr2.success) { std::cout << "D* replan fail\n"; return 5; }
#endif

    // HPA* (if large enough, should find a route similar to A*)
#if NAV2D_ENABLE_HPA
    SearchParams spH = sp; spH.use_hpa=true; spH.hpa_cluster_size=8;
    auto prH = p.findPath({2,2},{37,17}, spH);
    if (!prH.success) { std::cout << "HPA* fail\n"; return 6; }
#endif

    std::cout << "OK\n";
    return 0;
}

int main(){ return test_all(); }
#endif // NAV2D_TESTS

} // namespace nav2d
