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

