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

