#include "HPACluster.h"

#include <queue>
#include <unordered_map>

namespace colony::nav {

ClusterGrid::ClusterGrid(const IGridMap& map, ClusterGridSettings s)
    : m_(map), s_(s)
{
    clustersX_ = (m_.Width()  + s_.clusterW - 1) / s_.clusterW;
    clustersY_ = (m_.Height() + s_.clusterH - 1) / s_.clusterH;
}

ClusterKey ClusterGrid::KeyFor(const Coord& c) const {
    return { c.x / s_.clusterW, c.y / s_.clusterH };
}

const std::vector<PortalId>& ClusterGrid::PortalsInCluster(const ClusterKey& k) {
    if (!portalsBuilt_) BuildAllPortals();
    return clusterToPortals_[k];
}

void ClusterGrid::AddPortalPair(const Coord& a, const Coord& b) {
    PortalId ida{ static_cast<int32_t>(portals_.size()) };
    portals_.push_back(Portal{ ida, a, KeyFor(a), {} });
    clusterToPortals_[KeyFor(a)].push_back(ida);

    PortalId idb{ static_cast<int32_t>(portals_.size()) };
    portals_.push_back(Portal{ idb, b, KeyFor(b), {} });
    clusterToPortals_[KeyFor(b)].push_back(idb);

    // Cross-edge (bidirectional) with minimal cost to step across border
    float w = kCostStraight + m_.ExtraCost(b.x,b.y);
    portals_[ida.id].edges.emplace_back(idb, w);
    portals_[idb.id].edges.emplace_back(ida, w);
}

void ClusterGrid::BuildAllPortals() {
    portalsBuilt_ = true;
    // For each vertical cluster border, if two adjacent cells are passable, create portal pairs (sampled)
    for (int cy = 0; cy < clustersY_; ++cy) {
        int y0 = cy * s_.clusterH;
        int y1 = std::min(y0 + s_.clusterH - 1, m_.Height()-1);
        for (int cx = 0; cx < clustersX_-1; ++cx) {
            int xR = (cx+1) * s_.clusterW; // right border between [cx] and [cx+1]
            for (int y = y0; y <= y1; y += s_.portalStride) {
                Coord a{ xR-1, y }, b{ xR, y };
                if (InBounds(m_, a.x,a.y) && InBounds(m_, b.x,b.y)
                    && m_.IsPassable(a.x,a.y) && m_.IsPassable(b.x,b.y)) {
                    AddPortalPair(a,b);
                }
            }
        }
    }
    // Horizontal borders
    for (int cx = 0; cx < clustersX_; ++cx) {
        int x0 = cx * s_.clusterW;
        int x1 = std::min(x0 + s_.clusterW - 1, m_.Width()-1);
        for (int cy = 0; cy < clustersY_-1; ++cy) {
            int yB = (cy+1) * s_.clusterH;
            for (int x = x0; x <= x1; x += s_.portalStride) {
                Coord a{ x, yB-1 }, b{ x, yB };
                if (InBounds(m_, a.x,a.y) && InBounds(m_, b.x,b.y)
                    && m_.IsPassable(a.x,a.y) && m_.IsPassable(b.x,b.y)) {
                    AddPortalPair(a,b);
                }
            }
        }
    }
}

void ClusterGrid::EnsureIntraClusterEdges(const ClusterKey& k) {
    // If we've already built the intra-cluster edges for this cluster, we're done.
    // (Edges are cached on the Portal objects and never duplicated.)
    if (intraEdgesBuilt_.contains(k)) {
        return;
    }

    const auto& plist = PortalsInCluster(k);
    if (plist.size() <= 1) {
        // Even if there is nothing to connect, remember we processed this cluster so
        // repeated queries don't keep re-checking it.
        intraEdgesBuilt_.insert(k);
        return;
    }

    // Build bbox for this cluster
    Coord minB{ k.cx * s_.clusterW, k.cy * s_.clusterH };
    Coord maxB{ std::min(minB.x + s_.clusterW - 1, m_.Width()-1),
                std::min(minB.y + s_.clusterH - 1, m_.Height()-1) };

    JPSOptions opt;
    opt.diagonals = s_.diagonals;
    opt.bboxMin = minB; opt.bboxMax = maxB; opt.hasBBox = true;

    // Connect each pair if no edge exists yet (sparse, O(P^2) per cluster but P is small due to stride)
    for (size_t i=0;i<plist.size();++i) {
        for (size_t j=i+1;j<plist.size();++j) {
            auto& pa = portals_[plist[i].id];
            auto& pb = portals_[plist[j].id];

            bool alreadyAB = false;
            for (auto& e : pa.edges) if (e.first.id == pb.id.id) { alreadyAB = true; break; }
            if (alreadyAB) continue;

            auto p = FindPathJPS(m_, pa.pos, pb.pos, opt);
            if (!p) continue; // if cluster interior split by obstacles

            // weight = actual path length
            float w = 0.0f;
            for (size_t t=1;t<p->points.size();++t) {
                int dx = std::abs(p->points[t].x - p->points[t-1].x);
                int dy = std::abs(p->points[t].y - p->points[t-1].y);
                w += (dx==1 && dy==1) ? kCostDiagonal : kCostStraight;
                w += m_.ExtraCost(p->points[t].x, p->points[t].y);
            }
            pa.edges.emplace_back(pb.id, w);
            pb.edges.emplace_back(pa.id, w);
        }
    }

    // Mark cluster as processed so future queries don't rescan all portal pairs.
    intraEdgesBuilt_.insert(k);
}

std::optional<std::vector<Coord>> ClusterGrid::HighLevelPlan(const Coord& start, const Coord& goal) {
    // If same cluster, no need for abstract plan
    if (KeyFor(start) == KeyFor(goal)) {
        return std::vector<Coord>{ start, goal };
    }

    // Ensure portals exist for the clusters on the route (global build for step-1)
    if (!portalsBuilt_) BuildAllPortals();

    // IMPORTANT:
    // The previous implementation appended temporary "start" and "goal" portals into `portals_`
    // and permanently added edges from real portals to those temporary nodes.
    // Since the temp node indices are reused across calls, that caused:
    //   - stale edge weights (computed for a previous query's start/goal)
    //   - duplicate edges accumulating per query (unbounded memory growth)
    //   - incorrect high-level plans (can select portals unreachable from the current start/goal)
    // This implementation keeps the portal graph immutable and injects per-query temp edges locally.

    // Temporary node ids (negative so they never collide with real portal indices).
    constexpr int32_t kTmpStart = -1;
    constexpr int32_t kTmpGoal  = -2;

    struct Edge { int32_t to; float w; };

    // Extra per-query edges for the two temp nodes.
    std::vector<Edge> startEdges;
    std::vector<Edge> goalEdges;
    // Portal -> temp edges (used so the search can reach the goal temp node).
    std::unordered_map<int32_t, std::vector<Edge>> extraPortalEdges;

    auto nodePos = [&](int32_t id) -> Coord {
        if (id == kTmpStart) return start;
        if (id == kTmpGoal)  return goal;
        return portals_.at(static_cast<size_t>(id)).pos;
    };

    auto pathCost = [&](const Path& pth) -> float {
        float w = 0.0f;
        for (size_t t = 1; t < pth.points.size(); ++t) {
            const int dx = std::abs(pth.points[t].x - pth.points[t-1].x);
            const int dy = std::abs(pth.points[t].y - pth.points[t-1].y);
            w += (dx == 1 && dy == 1) ? kCostDiagonal : kCostStraight;
            w += m_.ExtraCost(pth.points[t].x, pth.points[t].y);
        }
        return w;
    };

    auto connectTempToCluster = [&](int32_t tempId,
                                   const Coord& tempPos,
                                   const ClusterKey& ck,
                                   std::vector<Edge>& outTempEdges)
    {
        const auto& plist = PortalsInCluster(ck);
        if (plist.empty()) return;

        EnsureIntraClusterEdges(ck);

        // Restrict these connections to the temp node's cluster so we get a true
        // intra-cluster cost (and avoid leaving the cluster via other portals).
        Coord minB{ ck.cx * s_.clusterW, ck.cy * s_.clusterH };
        Coord maxB{ std::min(minB.x + s_.clusterW - 1, m_.Width()  - 1),
                    std::min(minB.y + s_.clusterH - 1, m_.Height() - 1) };

        JPSOptions opt;
        opt.diagonals = s_.diagonals;
        opt.bboxMin = minB;
        opt.bboxMax = maxB;
        opt.hasBBox = true;

        for (auto pid2 : plist) {
            const Coord& ppos = portals_[pid2.id].pos;
            auto seg = FindPathJPS(m_, tempPos, ppos, opt);
            if (!seg) continue;
            const float w = pathCost(*seg);

            outTempEdges.push_back(Edge{ pid2.id, w });
            extraPortalEdges[pid2.id].push_back(Edge{ tempId, w });
        }
    };

    const ClusterKey startC = KeyFor(start);
    const ClusterKey goalC  = KeyFor(goal);

    connectTempToCluster(kTmpStart, start, startC, startEdges);
    connectTempToCluster(kTmpGoal,  goal,  goalC,  goalEdges);

    // If either endpoint can't connect to any portal in its cluster, the abstract graph
    // can't bridge clusters (Navigator will fall back to JPS/A*).
    if (startEdges.empty() || goalEdges.empty()) {
        return std::nullopt;
    }

    // A* on portal graph + 2 temp nodes.
    struct QN { int32_t id; float g; float f; };
    struct Cmp { bool operator()(const QN& a, const QN& b) const noexcept { return a.f > b.f; } };

    std::priority_queue<QN, std::vector<QN>, Cmp> open;
    std::unordered_map<int32_t, float> gScore;
    std::unordered_map<int32_t, int32_t> parent;

    auto H = [&](int32_t a, int32_t b) -> float {
        return Octile(nodePos(a), nodePos(b));
    };

    gScore[kTmpStart] = 0.0f;
    open.push(QN{ kTmpStart, 0.0f, H(kTmpStart, kTmpGoal) });

    bool found = false;
    while (!open.empty()) {
        const auto cur = open.top();
        open.pop();

        if (cur.id == kTmpGoal) {
            found = true;
            break;
        }

        // Skip stale queue entries.
        auto itBest = gScore.find(cur.id);
        if (itBest != gScore.end() && cur.g > itBest->second) {
            continue;
        }

        auto relax = [&](int32_t to, float w) {
            const float ng = cur.g + w;
            auto it = gScore.find(to);
            if (it == gScore.end() || ng < it->second) {
                gScore[to] = ng;
                parent[to] = cur.id;
                open.push(QN{ to, ng, ng + H(to, kTmpGoal) });
            }
        };

        if (cur.id == kTmpStart) {
            for (const auto& e : startEdges) relax(e.to, e.w);
            continue;
        }

        if (cur.id < 0) {
            // Goal temp node (or other temp): no need to expand.
            continue;
        }

        // Real portal node.
        const Portal& p = portals_.at(static_cast<size_t>(cur.id));
        EnsureIntraClusterEdges(p.cluster);

        for (const auto& [to, w] : p.edges) {
            relax(to.id, w);
        }
        if (auto itExtra = extraPortalEdges.find(cur.id); itExtra != extraPortalEdges.end()) {
            for (const auto& e : itExtra->second) {
                relax(e.to, e.w);
            }
        }
    }

    if (!found) {
        return std::nullopt;
    }

    // Reconstruct portal/waypoint sequence: start, portals..., goal.
    std::vector<int32_t> seq;
    for (int32_t at = kTmpGoal;;) {
        seq.push_back(at);
        if (at == kTmpStart) break;
        auto it = parent.find(at);
        if (it == parent.end()) {
            // Shouldn't happen, but keep this robust.
            return std::nullopt;
        }
        at = it->second;
    }
    std::reverse(seq.begin(), seq.end());

    std::vector<Coord> waypoints;
    waypoints.reserve(seq.size());
    for (int32_t id : seq) {
        waypoints.push_back(nodePos(id));
    }
    return waypoints;
}

std::optional<Path> ClusterGrid::RefineWithJPS(const std::vector<Coord>& wps) {
    if (wps.size() < 2) return std::nullopt;
    Path result;
    result.points.push_back(wps.front());
    for (size_t i=1;i<wps.size();++i) {
        JPSOptions opt; opt.diagonals = s_.diagonals;
        auto seg = FindPathJPS(m_, wps[i-1], wps[i], opt);
        if (!seg) return std::nullopt;
        // append but skip first point to avoid duplicates
        result.points.insert(result.points.end(), seg->points.begin()+1, seg->points.end());
    }
    return result;
}

std::optional<Path> ClusterGrid::FindPath(const Coord& start, const Coord& goal) {
    if (!InBounds(m_, start.x, start.y) || !InBounds(m_, goal.x, goal.y)) return std::nullopt;
    if (!m_.IsPassable(start.x, start.y) || !m_.IsPassable(goal.x, goal.y)) return std::nullopt;

    // Same-cluster fast path: just JPS
    if (KeyFor(start) == KeyFor(goal)) {
        JPSOptions opt; opt.diagonals = s_.diagonals;
        return FindPathJPS(m_, start, goal, opt);
    }

    auto wps = HighLevelPlan(start, goal);
    if (!wps) return std::nullopt;
    return RefineWithJPS(*wps);
}

} // namespace colony::nav
