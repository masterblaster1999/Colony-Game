#include "HPAStar.h"

namespace hpa {

// ------------------------ Small utilities ------------------------
static inline float manhattan(Point a, Point b) {
    return float(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}
static inline float octile(Point a, Point b) {
    float dx = float(std::abs(a.x - b.x));
    float dy = float(std::abs(a.y - b.y));
    // cost(orth)=1, cost(diag)=sqrt(2) if uniform; for weighted, heuristic remains admissible-ish if scaled ~1
    const float D  = 1.0f;
    const float D2 = 1.41421356237f;
    return (dx > dy) ? (D*dx + (D2 - D)*dy) : (D*dy + (D2 - D)*dx);
}

float HPAStar::heuristicGrid(Point a, Point b) const {
    return P.allowDiagonal ? octile(a,b) : manhattan(a,b);
}

int HPAStar::clusterIndexFromCell(int x, int y) const {
    const int cx = x / P.clusterSize;
    const int cy = y / P.clusterSize;
    if (cx < 0 || cy < 0 || cx >= m_numX || cy >= m_numY) return -1;
    return cy * m_numX + cx;
}

Rect HPAStar::clusterBounds(int cx, int cy) const {
    const int x0 = cx * P.clusterSize;
    const int y0 = cy * P.clusterSize;
    const int x1 = std::min(x0 + P.clusterSize, m_grid.width());
    const int y1 = std::min(y0 + P.clusterSize, m_grid.height());
    return Rect{x0,y0,x1,y1};
}

// ------------------------ Public API ------------------------
void HPAStar::rebuildAll() {
    clearAll();
    m_numX = (m_grid.width()  + P.clusterSize - 1) / P.clusterSize;
    m_numY = (m_grid.height() + P.clusterSize - 1) / P.clusterSize;
    buildClusters();
    buildEntrancesAndPortals();
    // Precompute intra-edges for each cluster
    for (int i=0; i<(int)m_clusters.size(); ++i) {
        buildIntraEdgesForCluster(i);
    }
}

void HPAStar::rebuildClusterAt(int x, int y) {
    const int idx = clusterIndexFromCell(x,y);
    if (idx < 0) return;
    // Remove cluster portals & intra edges, rebuild entrances on borders with neighbors, then intra edges
    // For simplicity we rebuild entrances for this cluster and all 4 neighbors (cheap).
    // 1) Remove existing portals for these clusters
    std::array<int,5> cands{};
    int cx = idx % m_numX, cy = idx / m_numX;
    int n = 0;
    auto pushIfValid = [&](int cx, int cy){ if(cx>=0&&cy>=0&&cx<m_numX&&cy<m_numY) cands[n++] = cy*m_numX+cx; };
    pushIfValid(cx,cy);
    pushIfValid(cx-1,cy);
    pushIfValid(cx+1,cy);
    pushIfValid(cx,cy-1);
    pushIfValid(cx,cy+1);
    std::unordered_set<NodeId> toRemove;
    for (int i=0;i<n;++i) for (auto nid : m_clusters[cands[i]].portals) toRemove.insert(nid);

    // Compact nodes & adj by marking removed; in a production engine, you'd free/compact more carefully.
    // Simpler approach: rebuild all portals in those five clusters: erase and rebuild local pieces.
    // Remove edges from/into those nodes
    for (auto nid : toRemove) {
        for (auto& nbrs : m_adj) {
            nbrs.erase(std::remove_if(nbrs.begin(), nbrs.end(),
                        [&](const Edge& e){ return e.to == nid; }), nbrs.end());
        }
        m_adj[nid].clear();
    }

    // Remove nodes by leaving gaps (keeps IDs stable). Clear cluster lists.
    for (int i=0;i<n;++i) m_clusters[cands[i]].portals.clear();

    // Rebuild entrances between candidate clusters
    for (int i=0;i<n;++i) {
        int a = cands[i];
        int ax = a % m_numX, ay = a / m_numX;
        // neighbors
        if (ax+1<m_numX) linkInterEdgesBetween(a, ay*m_numX + (ax+1));
        if (ax-1>=0)     linkInterEdgesBetween(a, ay*m_numX + (ax-1));
        if (ay+1<m_numY) linkInterEdgesBetween(a + m_numX, a);
        if (ay-1>=0)     linkInterEdgesBetween(a - m_numX, a);
    }
    // Rebuild intra edges for affected clusters
    for (int i=0;i<n;++i) buildIntraEdgesForCluster(cands[i]);
}

void HPAStar::rebuildRegion(const Rect& r) {
    int cx0 = std::max(0, r.x0 / P.clusterSize);
    int cy0 = std::max(0, r.y0 / P.clusterSize);
    int cx1 = std::min(m_numX-1, (r.x1-1) / P.clusterSize);
    int cy1 = std::min(m_numY-1, (r.y1-1) / P.clusterSize);
    for (int cy=cy0; cy<=cy1; ++cy) for (int cx=cx0; cx<=cx1; ++cx) {
        const Rect cb = clusterBounds(cx, cy);
        rebuildClusterAt(cb.x0, cb.y0);
    }
}

Path HPAStar::findPath(Point start, Point goal) {
    Path result;
    if (!inBounds(start.x,start.y) || !inBounds(goal.x,goal.y) ||
        !m_grid.passable(start.x,start.y) || !m_grid.passable(goal.x,goal.y))
        return result;

    if (clusterIndexFromCell(start.x,start.y) == clusterIndexFromCell(goal.x,goal.y)) {
        // Same cluster: solve locally
        float c=0.f; std::vector<Point> pts;
        if (localSearch(clusterBounds(start.x / P.clusterSize, start.y / P.clusterSize),
                        start, goal, c, &pts)) {
            if (P.smoothPath) {
                // try to greedily shortcut
                std::vector<Point> smooth;
                smooth.reserve(pts.size());
                size_t i=0;
                while (i<pts.size()) {
                    size_t j = pts.size()-1;
                    for (; j>i+1; --j) {
                        // line-of-sight check in localSearch bounds
                        Rect b = clusterBounds(start.x / P.clusterSize, start.y / P.clusterSize);
                        // reuse localSearch's LOS? We'll inline a quick LOS here using grid sampling
                        bool ok = true;
                        int x0=pts[i].x, y0=pts[i].y, x1=pts[j].x, y1=pts[j].y;
                        int dx = std::abs(x1-x0), dy = std::abs(y1-y0);
                        int sx = x0 < x1 ? 1 : -1;
                        int sy = y0 < y1 ? 1 : -1;
                        int err = dx - dy;
                        int x=x0,y=y0;
                        while (true) {
                            if (!b.contains(x,y) || !m_grid.passable(x,y)) { ok=false; break; }
                            if (x==x1 && y==y1) break;
                            int e2 = 2*err;
                            if (e2 > -dy) { err -= dy; x += sx; }
                            if (e2 <  dx) { err += dx; y += sy; }
                        }
                        if (ok) { smooth.push_back(pts[i]); i=j; break; }
                    }
                    if (j==i+1 || i==pts.size()-1) { smooth.push_back(pts[i]); ++i; }
                }
                smooth.push_back(goal);
                result.points = std::move(smooth);
                result.cost   = c; // cost not exact after smoothing; recompute if you need exactness
                result.found  = true;
                return result;
            }
            result.points = std::move(pts);
            result.cost   = c;
            result.found  = true;
            return result;
        }
        return result;
    }

    // Compute start/goal portal connections
    const int sIdx = clusterIndexFromCell(start.x,start.y);
    const int gIdx = clusterIndexFromCell(goal.x,goal.y);
    if (sIdx < 0 || gIdx < 0) return result;

    // Virtual start edges: local search from start -> each portal in start cluster
    std::vector<std::pair<NodeId,float>> startEdges;
    startEdges.reserve(m_clusters[sIdx].portals.size());
    for (auto nid : m_clusters[sIdx].portals) {
        const auto& pn = m_nodes[nid];
        float c=0.f;
        if (!localSearch(m_clusters[sIdx].bounds, start, pn.cell, c, nullptr)) continue;
        startEdges.emplace_back(nid, c);
    }
    if (startEdges.empty()) return result;

    // Goal portal set
    std::unordered_set<NodeId> goalPortals;
    for (auto nid : m_clusters[gIdx].portals) {
        goalPortals.insert(nid);
    }
    if (goalPortals.empty()) return result;

    std::vector<NodeId> visitOrder;
    std::unordered_map<NodeId,NodeId> parent;
    if (!abstractAStar(startEdges, goalPortals, goal, visitOrder, parent)) return result;

    // Reconstruct portal chain from chosen goal portal back to (one of) start portals
    NodeId goalChosen = NodeId(-1);
    for (NodeId v : visitOrder) { if (goalPortals.count(v)) { goalChosen = v; break; } }
    if (goalChosen == NodeId(-1)) return result;

    std::vector<NodeId> chain;
    for (NodeId cur = goalChosen; parent.count(cur); cur = parent[cur]) {
        chain.push_back(cur);
    }
    std::reverse(chain.begin(), chain.end());

    // Refine
    float C=0.f; std::vector<Point> pts;
    if (!refinePath(start, goal, chain, pts, C)) return result;
    result.points = std::move(pts);
    result.cost   = C;
    result.found  = true;
    return result;
}

// ------------------------ Build pipeline ------------------------
void HPAStar::clearAll() {
    m_nodes.clear();
    m_adj.clear();
    m_clusters.clear();
    m_intraPathCache.clear();
}

void HPAStar::buildClusters() {
    m_clusters.resize(m_numX * m_numY);
    for (int cy=0; cy<m_numY; ++cy) {
        for (int cx=0; cx<m_numX; ++cx) {
            Cluster cl;
            cl.bounds = clusterBounds(cx, cy);
            cl.portals.clear();
            m_clusters[cy*m_numX + cx] = std::move(cl);
        }
    }
}

void HPAStar::buildEntrancesAndPortals() {
    // Reset node graph
    m_adj.assign(1, {}); // we will ignore index 0 (keep NodeId start from 1) -> simpler "missing" checks
    m_nodes.assign(1, PortalNode{0, {0,0}, -1}); // dummy node 0

    // For each pair of neighbors, detect entrances, place portals and inter-edges
    for (int cy=0; cy<m_numY; ++cy) for (int cx=0; cx<m_numX; ++cx) {
        const int a = cy*m_numX + cx;
        if (cx+1 < m_numX) linkInterEdgesBetween(a, a+1);
        if (cy+1 < m_numY) linkInterEdgesBetween(a, a+m_numX);
    }
}

void HPAStar::linkInterEdgesBetween(int aIdx, int bIdx) {
    if (aIdx<0 || bIdx<0 || aIdx==(int)m_clusters.size() || bIdx==(int)m_clusters.size())
        return;
    std::vector<EntranceSeg> segs;
    detectEntrancesBetween(aIdx, bIdx, segs);
    for (auto& s : segs) placePortalsForEntrance(aIdx, bIdx, s);
}

void HPAStar::detectEntrancesBetween(int aIdx, int bIdx, std::vector<EntranceSeg>& out) {
    out.clear();
    const Rect A = m_clusters[aIdx].bounds;
    const Rect B = m_clusters[bIdx].bounds;

    // Determine if horizontal neighbors (sharing vertical border) or vertical neighbors
    bool horizontal = (A.y0 == B.y0 && A.y1 == B.y1 && (A.x1 == B.x0 || B.x1 == A.x0));
    bool vertical   = (A.x0 == B.x0 && A.x1 == B.x1 && (A.y1 == B.y0 || B.y1 == A.y0));
    if (!horizontal && !vertical) return;

    if (horizontal) {
        // A is left, B is right (assume A.x1 == B.x0)
        int xL = A.x1 - 1;
        int xR = B.x0;
        int runStart = -1;
        for (int y=A.y0; y<A.y1; ++y) {
            bool open = inBounds(xL,y) && inBounds(xR,y) && m_grid.passable(xL,y) && m_grid.passable(xR,y);
            if (open) {
                if (runStart < 0) runStart = y;
            } else {
                if (runStart >= 0) {
                    out.push_back(EntranceSeg{runStart, y-1, /*vertical=*/true, /*fixed=*/xR});
                    runStart = -1;
                }
            }
        }
        if (runStart >= 0) out.push_back(EntranceSeg{runStart, A.y1-1, true, xR});
    } else if (vertical) {
        // A is top, B is bottom (assume A.y1 == B.y0)
        int yT = A.y1 - 1;
        int yB = B.y0;
        int runStart = -1;
        for (int x=A.x0; x<A.x1; ++x) {
            bool open = inBounds(x,yT) && inBounds(x,yB) && m_grid.passable(x,yT) && m_grid.passable(x,yB);
            if (open) {
                if (runStart < 0) runStart = x;
            } else {
                if (runStart >= 0) {
                    out.push_back(EntranceSeg{runStart, x-1, /*vertical=*/false, /*fixed=*/yB});
                    runStart = -1;
                }
            }
        }
        if (runStart >= 0) out.push_back(EntranceSeg{runStart, A.x1-1, false, yB});
    }
}

void HPAStar::placePortalsForEntrance(int aIdx, int bIdx, const EntranceSeg& seg) {
    if (seg.t0 > seg.t1) return;
    const int L = seg.t1 - seg.t0 + 1;

    auto makeNode = [&](Point p, int clusterIdx)->NodeId{
        NodeId id = (NodeId)m_nodes.size();
        m_nodes.push_back(PortalNode{id, p, clusterIdx});
        if (id >= m_adj.size()) m_adj.resize(id+1);
        m_clusters[clusterIdx].portals.push_back(id);
        return id;
    };

    auto connect = [&](NodeId u, NodeId v, float w, bool inter){
        m_adj[u].push_back(Edge{v,w,inter});
        m_adj[v].push_back(Edge{u,w,inter});
    };

    // Select 1 or 2 "representative" portal locations along the entrance
    std::vector<int> ts;
    if (L <= P.entranceSplitThresh) {
        ts.push_back((seg.t0 + seg.t1) / 2);
    } else {
        ts.push_back(seg.t0); ts.push_back(seg.t1);
    }

    for (int t : ts) {
        // Two portal nodes: one owned by cluster A, one by cluster B, placed on each side of the border
        if (seg.vertical) {
            // vertical entrance along x = seg.fixed, sweep t in y
            // A is left of B
            const Rect A = m_clusters[aIdx].bounds;
            const Rect B = m_clusters[bIdx].bounds;
            // identify which is left/right
            bool Aleft = (A.x1 == B.x0);
            int xA = Aleft ? (A.x1 - 1) : (A.x0);
            int xB = Aleft ? (B.x0)     : (B.x1 - 1);
            Point aP{xA, t}, bP{xB, t};
            NodeId na = makeNode(aP, aIdx);
            NodeId nb = makeNode(bP, bIdx);
            // inter-edge crossing cost (avg of entering costs; adjust if you have per-edge cost rules)
            float w = 0.5f*(m_grid.cost(aP.x,aP.y) + m_grid.cost(bP.x,bP.y));
            connect(na, nb, w, /*inter=*/true);
        } else {
            // horizontal entrance along y = seg.fixed, sweep t in x
            const Rect A = m_clusters[aIdx].bounds;
            const Rect B = m_clusters[bIdx].bounds;
            bool Atop = (A.y1 == B.y0);
            int yA = Atop ? (A.y1 - 1) : (A.y0);
            int yB = Atop ? (B.y0)     : (B.y1 - 1);
            Point aP{t, yA}, bP{t, yB};
            NodeId na = makeNode(aP, aIdx);
            NodeId nb = makeNode(bP, bIdx);
            float w = 0.5f*(m_grid.cost(aP.x,aP.y) + m_grid.cost(bP.x,bP.y));
            connect(na, nb, w, /*inter=*/true);
        }
    }
}

void HPAStar::buildIntraEdgesForCluster(int cidx) {
    if (cidx < 0) return;
    const auto& C = m_clusters[cidx];
    const auto& B = C.bounds;
    // all-pairs (portal in cluster)
    for (size_t i=0;i<C.portals.size();++i) {
        NodeId aId = C.portals[i];
        const auto& a = m_nodes[aId];
        for (size_t j=i+1;j<C.portals.size();++j) {
            NodeId bId = C.portals[j];
            const auto& b = m_nodes[bId];

            float w=0.f; std::vector<Point> pth;
            if (!localSearch(B, a.cell, b.cell, w, P.storeIntraPaths ? &pth : nullptr))
                continue;

            // connect undirected intra edge
            m_adj[aId].push_back(Edge{bId, w, /*inter*/false});
            m_adj[bId].push_back(Edge{aId, w, /*inter*/false});

            if (P.storeIntraPaths) {
                IntraKey k1{aId,bId}, k2{bId,aId};
                m_intraPathCache[k1] = pth;
                std::reverse(pth.begin(), pth.end());
                m_intraPathCache[k2] = std::move(pth);
            }
        }
    }
}

// ------------------------ Local A* inside a cluster ------------------------
struct NodeRec {
    int x,y;
    float g,f;
    int   parentX,parentY;
};
struct PQItem { float f; int x,y; };
struct PQCmp { bool operator()(const PQItem& a, const PQItem& b) const { return a.f > b.f; } };

bool HPAStar::localSearch(const Rect& bounds, Point s, Point g, float& outCost, std::vector<Point>* outPath) {
    if (!bounds.contains(s.x,s.y) || !bounds.contains(g.x,g.y)) return false;
    auto idx = [&](int x,int y){ return (y-bounds.y0) * bounds.width() + (x-bounds.x0); };
    const int W = bounds.width(), H = bounds.height();

    std::vector<float> G(W*H, std::numeric_limits<float>::infinity());
    std::vector<char>  visited(W*H, 0);
    std::vector<NodeRec> parent(W*H);
    std::priority_queue<PQItem,std::vector<PQItem>,PQCmp> pq;

    auto push = [&](int x,int y, float g, float f, int px,int py){
        int id = idx(x,y);
        if (g < G[id]) {
            G[id] = g;
            parent[id] = NodeRec{x,y,g,f,px,py};
            pq.push(PQItem{f,x,y});
        }
    };
    auto h = [&](int x,int y){ return heuristicGrid(Point{x,y}, g); };

    push(s.x,s.y, 0.f, h(s.x,s.y), -1,-1);

    static const std::array<std::pair<int,int>,8> K8 {{
        {+1,0},{-1,0},{0,+1},{0,-1},{+1,+1},{+1,-1},{-1,+1},{-1,-1}
    }};
    static const std::array<std::pair<int,int>,4> K4 {{
        {+1,0},{-1,0},{0,+1},{0,-1}
    }};
    const auto& K = P.allowDiagonal ? K8 : K4;

    while (!pq.empty()) {
        PQItem cur = pq.top(); pq.pop();
        int x=cur.x, y=cur.y;
        int id = idx(x,y);
        if (visited[id]) continue;
        visited[id]=1;

        if (x==g.x && y==g.y) {
            outCost = G[id];
            if (outPath) {
                std::vector<Point> rev;
                int cx=x, cy=y;
                while (cx!=-1) {
                    rev.push_back(Point{cx,cy});
                    NodeRec pr = parent[idx(cx,cy)];
                    cx = pr.parentX; cy = pr.parentY;
                }
                std::reverse(rev.begin(), rev.end());
                *outPath = std::move(rev);
            }
            return true;
        }

        for (auto [dx,dy] : K) {
            int nx=x+dx, ny=y+dy;
            if (!bounds.contains(nx,ny) || !m_grid.passable(nx,ny)) continue;

            // forbid "corner cutting" when diagonal
            if (P.allowDiagonal && dx!=0 && dy!=0) {
                if (!m_grid.passable(x+dx,y) || !m_grid.passable(x,y+dy)) continue;
            }

            float step = (dx==0 || dy==0) ? 1.0f : 1.41421356237f;
            // weight entering the next cell
            step *= m_grid.cost(nx,ny);
            float ng = G[id] + step;
            push(nx,ny, ng, ng + h(nx,ny), x,y);
        }
    }
    return false;
}

// ------------------------ Abstract A* ------------------------
bool HPAStar::abstractAStar(const std::vector<std::pair<NodeId,float>>& startEdges,
                            const std::unordered_set<NodeId>& goalPortals,
                            Point goalCell,
                            std::vector<NodeId>& outVisitOrder,
                            std::unordered_map<NodeId, NodeId>& outParent)
{
    struct QItem { NodeId v; float f; };
    struct Cmp { bool operator()(const QItem&a, const QItem&b) const { return a.f>b.f; } };
    std::priority_queue<QItem,std::vector<QItem>,Cmp> pq;

    std::unordered_map<NodeId,float> G;
    auto h = [&](NodeId v){ return heuristicGrid(m_nodes[v].cell, goalCell); };

    // Seed from virtual start with provided edges
    for (auto [to,w] : startEdges) {
        float f = w + h(to);
        if (!G.count(to) || w < G[to]) {
            G[to]=w;
            outParent.erase(to); // parent = virtual start (none)
            pq.push(QItem{to, f});
        }
    }

    std::unordered_set<NodeId> closed;
    outVisitOrder.clear();
    outParent.clear();

    while (!pq.empty()) {
        auto [u,fu] = pq.top(); pq.pop();
        if (closed.count(u)) continue;
        closed.insert(u);
        outVisitOrder.push_back(u);

        if (goalPortals.count(u)) {
            // reached a goal portal
            return true;
        }

        for (const auto& e : m_adj[u]) {
            NodeId v = e.to;
            float w  = e.w;
            float ng = G[u] + w;
            if (!G.count(v) || ng < G[v]) {
                G[v]=ng;
                outParent[v]=u;
                pq.push(QItem{v, ng + h(v)});
            }
        }
    }
    return false;
}

// ------------------------ Refinement ------------------------
bool HPAStar::refinePath(Point start, Point goal,
                         const std::vector<NodeId>& portalSequence,
                         std::vector<Point>& outPoints, float& outCost)
{
    outPoints.clear();
    outCost = 0.f;

    if (portalSequence.empty()) return false;
    NodeId a = portalSequence.front();
    // connect start -> first portal within start cluster
    float c=0.f; std::vector<Point> p;
    const int sIdx = clusterIndexFromCell(start.x,start.y);
    if (!localSearch(m_clusters[sIdx].bounds, start, m_nodes[a].cell, c, &p)) return false;
    outCost += c;
    outPoints.insert(outPoints.end(), p.begin(), p.end());

    // walk through portals
    for (size_t i=0;i+1<portalSequence.size();++i) {
        NodeId u = portalSequence[i], v = portalSequence[i+1];
        const auto& nu = m_nodes[u];
        const auto& nv = m_nodes[v];

        if (nu.clusterIdx == nv.clusterIdx) {
            // Intra-cluster: use cached path if available, else local search
            std::vector<Point> seg;
            auto it = m_intraPathCache.find(IntraKey{u,v});
            if (it != m_intraPathCache.end()) {
                seg = it->second;
                // compute weight (approx) by summing unit steps * costs
                // For speed we just reuse precomputed abstract weight via edge lookup:
                float w = 0.f;
                for (const auto& e : m_adj[u]) if (e.to==v) { w=e.w; break; }
                outCost += w;
            } else {
                float w=0.f;
                if (!localSearch(m_clusters[nu.clusterIdx].bounds, nu.cell, nv.cell, w, &seg))
                    return false;
                outCost += w;
            }
            // Append but avoid duplicating first point (already present)
            if (!outPoints.empty() && !seg.empty() && outPoints.back().x==seg.front().x && outPoints.back().y==seg.front().y) {
                outPoints.insert(outPoints.end(), seg.begin()+1, seg.end());
            } else {
                outPoints.insert(outPoints.end(), seg.begin(), seg.end());
            }
        } else {
            // Crossing border: just step from u.cell to v.cell (neighbors across boundary)
            outPoints.push_back(nv.cell);
            // cost: average; keep consistent with inter-edge cost
            outCost += 0.5f*(m_grid.cost(nu.cell.x,nu.cell.y) + m_grid.cost(nv.cell.x,nv.cell.y));
        }
    }

    // last portal -> goal
    NodeId last = portalSequence.back();
    const int gIdx = clusterIndexFromCell(goal.x,goal.y);
    float w2=0.f; std::vector<Point> tail;
    if (!localSearch(m_clusters[gIdx].bounds, m_nodes[last].cell, goal, w2, &tail)) return false;
    // avoid duplicate first
    if (!outPoints.empty() && !tail.empty() && outPoints.back().x==tail.front().x && outPoints.back().y==tail.front().y) {
        outPoints.insert(outPoints.end(), tail.begin()+1, tail.end());
    } else {
        outPoints.insert(outPoints.end(), tail.begin(), tail.end());
    }
    outCost += w2;
    return true;
}

} // namespace hpa
