#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include "IGridMap.h"
#include "JPS.h"
#include "Heuristics.h"

namespace colony::nav {

struct ClusterKey { int32_t cx=0, cy=0; };
struct ClusterKeyHash {
    size_t operator()(const ClusterKey& k) const noexcept {
        return (static_cast<uint64_t>(k.cx) << 32) ^ static_cast<uint32_t>(k.cy);
    }
};
inline bool operator==(const ClusterKey& a, const ClusterKey& b) { return a.cx==b.cx && a.cy==b.cy; }

struct PortalId {
    int32_t id=-1;
    bool operator==(const PortalId& o) const noexcept { return id==o.id; }
};
struct PortalIdHash {
    size_t operator()(const PortalId& p) const noexcept { return static_cast<size_t>(p.id); }
};

struct Portal {
    PortalId id;
    Coord pos;         // exact tile on the grid
    ClusterKey cluster;
    // neighbor portals in the abstract graph (id + cost)
    // computed lazily; intra-cluster edges cached here
    std::vector<std::pair<PortalId, float>> edges;
};

struct ClusterGridSettings {
    int32_t clusterW = 32;
    int32_t clusterH = 32;
    DiagonalPolicy diagonals = DiagonalPolicy::AllowedIfNoCut;
    int32_t portalStride = 4; // sample every N cells along borders to limit portal count
};

class ClusterGrid {
public:
    ClusterGrid(const IGridMap& map, ClusterGridSettings s);

    ClusterKey KeyFor(const Coord& c) const;
    // Enumerate or build portals lazily
    const std::vector<PortalId>& PortalsInCluster(const ClusterKey& k);
    const Portal& GetPortal(PortalId id) const { return portals_.at(id.id); }
    Portal& GetPortal(PortalId id) { return portals_.at(id.id); }

    // Returns abstract path (as raw coordinates: start, portals..., goal)
    std::optional<Path> FindPath(const Coord& start, const Coord& goal);

private:
    const IGridMap& m_;
    ClusterGridSettings s_;
    int32_t clustersX_=0, clustersY_=0;

    std::vector<Portal> portals_;
    std::unordered_map<ClusterKey, std::vector<PortalId>, ClusterKeyHash> clusterToPortals_;
    bool portalsBuilt_ = false;

    // Cache which clusters already have their intra-cluster portal edges built.
    // This avoids repeating O(P^2) scans per query once a cluster has been processed.
    std::unordered_set<ClusterKey, ClusterKeyHash> intraEdgesBuilt_;

    void BuildAllPortals(); // step-1: simple global construction
    void AddPortalPair(const Coord& a, const Coord& b); // border pair creates 2 portals and a cross-edge
    // Compute/ensure intra-cluster portal edges exist (uses JPS with bbox on that cluster)
    void EnsureIntraClusterEdges(const ClusterKey& k);

    // High-level A* over portals (plus start/goal temporary portals)
    std::optional<std::vector<Coord>> HighLevelPlan(const Coord& start, const Coord& goal);

    // Refinement with JPS between waypoints
    std::optional<Path> RefineWithJPS(const std::vector<Coord>& waypoints);
};

} // namespace colony::nav
