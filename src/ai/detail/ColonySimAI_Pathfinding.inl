// ------------------------------ Pathfinding ------------------------------
struct Path { std::vector<Vec2i> points; bool empty() const { return points.empty(); } void clear(){ points.clear(); } };

struct PathCacheEntry {
    std::vector<Vec2i> pts;
    uint64_t gridStamp = 0;
    uint64_t lastUsed = 0;
};

class Pathfinder {
public:
    explicit Pathfinder(const Grid* g) : g_(g) {}
    void setDiagonal(bool allow){ allowDiag_ = allow; }
    void setMaxSearch(int nodes){ maxSearch_ = nodes; }
    void setDynamicBlocker(const std::function<bool(const Vec2i&)>& f){ isBlocked_ = f; }

    // Returns optimal (or best-effort) path. Caches results until grid stamp changes.
    Path find(const Vec2i& start, const Vec2i& goal, uint64_t timeStamp=0) {
        Path path;
        if(!g_->inBounds(start) || !g_->inBounds(goal)){
            return path;
        }
        if(start==goal){ path.points.push_back(start); return path; }

        // Path cache lookup
        auto key = std::make_pair(start, goal);
        auto it = cache_.find(key);
        if(it!=cache_.end() && it->second.gridStamp==g_->stamp()){
            it->second.lastUsed = timeStamp;
            path.points = it->second.pts; return path;
        }

        // A* with optional simple JPS pruning
        struct Node{ Vec2i p; int g=0, f=0; Vec2i parent{-999,-999}; };
        auto h = [&](const Vec2i& a){ return (allowDiag_ ? a.chebyshev(goal) : a.manhattan(goal)) * 10; };

        struct PQE{ int f; uint64_t id; Vec2i p; };
        struct PQCmp{ bool operator()(const PQE& a, const PQE& b) const { return a.f > b.f; }};
        std::priority_queue<PQE, std::vector<PQE>, PQCmp> open;
        std::unordered_map<Vec2i, Node, Hasher> all;
        std::unordered_map<Vec2i, int, Hasher> openG;

        auto passable = [&](const Vec2i& p)->bool{
            if(isBlocked_ && isBlocked_(p) && p!=goal) return false;
            return g_->walkable(p) || p==goal;
        };

        auto pushOpen = [&](const Vec2i& p, int g, const Vec2i& parent){
            Node n{p,g,g + h(p), parent};
            all[p] = n;
            openG[p] = g;
            open.push(PQE{n.f, counter_++, p});
        };

        pushOpen(start, 0, {-999,-999});

        int expanded = 0;
        while(!open.empty()){
            auto cur = open.top(); open.pop();
            auto itn = all.find(cur.p);
            if(itn==all.end()) continue;
            Node node = itn->second;
            if (node.f != cur.f) continue;

            if (++expanded > maxSearch_) break;
            if (node.p == goal){
                // Reconstruct
                std::vector<Vec2i> rev;
                Vec2i p = node.p;
                while(p.x!=-999){
                    rev.push_back(p);
                    p = all[p].parent;
                }
                std::reverse(rev.begin(), rev.end());
                path.points = std::move(rev);
                smooth(path, passable);
                // store cache
                ensureCacheBudget();
                cache_[key] = PathCacheEntry{path.points, g_->stamp(), timeStamp};
                return path;
            }

            auto visitNeighbor = [&](const Vec2i& np, int stepCost){
                if(!g_->inBounds(np) || !passable(np)) return;
                // Avoid cutting corners
                if (allowDiag_ && np.x!=node.p.x && np.y!=node.p.y){
                    Vec2i a{np.x, node.p.y}, b{node.p.x, np.y};
                    if(!passable(a) || !passable(b)) return;
                }
                int cost = stepCost + g_->moveCost(np);
                int tentative = node.g + cost;
                auto og = openG.find(np);
                if(og==openG.end() || tentative < og->second) pushOpen(np, tentative, node.p);
            };

#if COLONY_SIM_ENABLE_JPS
            // Simple pruning: prefer straight jumps when possible (not full JPS but reduces branching)
            static const int DIRS8[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            for(int d=0; d<(allowDiag_?8:4); ++d){
                Vec2i dir{DIRS8[d][0], DIRS8[d][1]};
                Vec2i np = node.p + dir;
                if(!g_->inBounds(np) || !passable(np)) continue;
                // jump until forced neighbor or blocked
                int step = (dir.x!=0 && dir.y!=0) ? 14 : 10;
                Vec2i curp = np;
                while(true){
                    if(!g_->inBounds(curp) || !passable(curp)) break;
                    // forced neighbor detection (very simplified)
                    if(allowDiag_){
                        bool forced = false;
                        if(dir.x!=0 && dir.y!=0){
                            Vec2i a{curp.x-dir.x, curp.y};
                            Vec2i b{curp.x, curp.y-dir.y};
                            if(!passable(a) || !passable(b)) forced = true;
                        }
                        if(forced || curp==goal){
                            visitNeighbor(curp, step);
                            break;
                        }
                    } else {
                        // 4-dir: check side blocks
                        if(curp==goal){ visitNeighbor(curp, step); break; }
                    }
                    // continue jump
                    curp = curp + dir;
                    step += (dir.x!=0 && dir.y!=0) ? 14 : 10;
                }
            }
#else
            // Vanilla neighbors
            const auto neigh4 = g_->neighbors4(node.p);
            const auto neigh8 = g_->neighbors8(node.p);
            const auto& neigh = allowDiag_ ? neigh8 : neigh4;
            for(const auto& np: neigh){
                int step = (np.x!=node.p.x && np.y!=node.p.y) ? 14 : 10;
                visitNeighbor(np, step);
            }
#endif
        }
        return path; // empty if failed
    }

    void clearCache(){ cache_.clear(); }

private:
    template<typename PassableFn>
    void smooth(Path& p, PassableFn passable) const {
        if(p.points.size()<3) return;
        std::vector<Vec2i> out;
        out.push_back(p.points.front());
        size_t k = 2;
        while(k < p.points.size()){
            Vec2i a = out.back();
            Vec2i b = p.points[k];
            if(hasLineOfSight(a,b, passable)){
                // skip middle point
            } else {
                out.push_back(p.points[k-1]);
            }
            ++k;
        }
        out.push_back(p.points.back());
        p.points.swap(out);
    }
    template<typename PassableFn>
    bool hasLineOfSight(Vec2i a, Vec2i b, PassableFn passable) const {
        int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
        int sx = (a.x < b.x) ? 1 : -1;
        int sy = (a.y < b.y) ? 1 : -1;
        int err = dx - dy;
        while(true){
            if(!passable(a)) return false;
            if(a==b) break;
            int e2 = err*2;
            if(e2 > -dy){ err -= dy; a.x += sx; }
            if(e2 <  dx){ err += dx; a.y += sy; }
        }
        return true;
    }
    void ensureCacheBudget(){
        if(cache_.size()<COLONY_SIM_PATHCACHE_MAX) return;
        // evict least-recently-used ~10%
        std::vector<std::pair<std::pair<Vec2i,Vec2i>, PathCacheEntry>> vec(cache_.begin(), cache_.end());
        std::nth_element(vec.begin(), vec.begin()+vec.size()/10, vec.end(),
            [](auto& a, auto& b){ return a.second.lastUsed < b.second.lastUsed; });
        for(size_t i=0;i<vec.size()/10;++i) cache_.erase(vec[i].first);
    }

    const Grid* g_;
    bool allowDiag_ = true;
    int maxSearch_ = 20000;
    uint64_t counter_ = 0;

    std::function<bool(const Vec2i&)> isBlocked_;
    std::unordered_map<std::pair<Vec2i,Vec2i>, PathCacheEntry, PairHasher<Vec2i,Vec2i>> cache_;
};

