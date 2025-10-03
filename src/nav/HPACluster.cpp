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
    const auto& plist = PortalsInCluster(k);
    if (plist.size() <= 1) return;

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
}

std::optional<std::vector<Coord>> ClusterGrid::HighLevelPlan(const Coord& start, const Coord& goal) {
    // If same cluster, no need for abstract plan
    if (KeyFor(start) == KeyFor(goal)) {
        return std::vector<Coord>{ start, goal };
    }

    // Ensure portals exist for the clusters on the route (global build for step-1)
    if (!portalsBuilt_) BuildAllPortals();

    // Create two temporary portals for start/goal and connect to their cluster portals (with JPS distances)
    PortalId sId{ static_cast<int32_t>(portals_.size()) };
    PortalId gId{ static_cast<int32_t>(portals_.size()+1) };
    portals_.push_back(Portal{ sId, start, KeyFor(start), {} });
    portals_.push_back(Portal{ gId, goal,  KeyFor(goal),  {} });

    auto conTmp = [&](PortalId pid) {
        auto& p = portals_[pid.id];
        const auto& plist = PortalsInCluster(p.cluster);
        EnsureIntraClusterEdges(p.cluster);

        // bbox: cluster of this portal
        Coord minB{ p.cluster.cx * s_.clusterW, p.cluster.cy * s_.clusterH };
        Coord maxB{ std::min(minB.x + s_.clusterW - 1, m_.Width()-1),
                    std::min(minB.y + s_.clusterH - 1, m_.Height()-1) };
        JPSOptions opt; opt.diagonals = s_.diagonals; opt.bboxMin=minB; opt.bboxMax=maxB; opt.hasBBox=true;

        // connect temporary node to all cluster portals
        for (auto pid2 : plist) {
            auto pth = FindPathJPS(m_, p.pos, portals_[pid2.id].pos, opt);
            if (!pth) continue;
            float w = 0.0f;
            for (size_t t=1;t<pth->points.size();++t) {
                int dx = std::abs(pth->points[t].x - pth->points[t-1].x);
                int dy = std::abs(pth->points[t].y - pth->points[t-1].y);
                w += (dx==1 && dy==1) ? kCostDiagonal : kCostStraight;
                w += m_.ExtraCost(pth->points[t].x, pth->points[t].y);
            }
            portals_[pid.id].edges.emplace_back(pid2, w);
            portals_[pid2.id].edges.emplace_back(pid, w);
        }
    };
    conTmp(sId);
    conTmp(gId);

    // Also ensure intra-cluster edges exist for all clusters (step-1: simple but safe)
    for (int cy=0; cy<clustersY_; ++cy)
        for (int cx=0; cx<clustersX_; ++cx)
            EnsureIntraClusterEdges({cx,cy});

    // A* on portals graph
    struct QN { PortalId id; float g, f; };
    struct Cmp { bool operator()(const QN& a, const QN& b) const { return a.f > b.f; } };
    std::priority_queue<QN, std::vector<QN>, Cmp> open;
    std::unordered_map<int32_t, float> gScore;
    std::unordered_map<int32_t, int32_t> parent;
    auto H = [&](const PortalId& a, const PortalId& b){
        return Octile(portals_[a.id].pos, portals_[b.id].pos);
    };

    gScore[sId.id] = 0.0f;
    open.push({ sId, 0.0f, H(sId, gId) });

    bool found=false;
    while (!open.empty()) {
        auto cur = open.top(); open.pop();
        if (cur.id.id == gId.id) { found=true; break; }
        for (auto [to, w] : portals_[cur.id.id].edges) {
            float ng = cur.g + w;
            auto it = gScore.find(to.id);
            if (it == gScore.end() || ng < it->second) {
                gScore[to.id] = ng;
                parent[to.id] = cur.id.id;
                open.push({ to, ng, ng + H(to, gId) });
            }
        }
    }

    if (!found) {
        // cleanup temps
        portals_.pop_back(); portals_.pop_back();
        return std::nullopt;
    }

    // reconstruct portal sequence
    std::vector<Coord> waypoints;
    int at = gId.id;
    waypoints.push_back(portals_[at].pos);
    while (at != sId.id) {
        at = parent.at(at);
        waypoints.push_back(portals_[at].pos);
    }
    std::reverse(waypoints.begin(), waypoints.end());

    // cleanup temps but keep cached edges
    portals_.pop_back(); portals_.pop_back();

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
