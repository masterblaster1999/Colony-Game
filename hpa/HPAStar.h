#pragma once
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <limits>
#include <cstdint>
#include <functional>
#include <assert.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

// --------------------------- Public map interface ---------------------------
struct IGrid {
    virtual ~IGrid() = default;
    virtual int width()  const = 0;
    virtual int height() const = 0;

    // Return true if cell (x,y) is within the world and can be traversed.
    virtual bool passable(int x, int y) const = 0;

    // Traversal cost for entering cell (x,y). For uniform grids, return 1.0f.
    virtual float cost(int x, int y) const = 0;
};

// --------------------------- HPA* implementation ---------------------------
namespace hpa {

struct Point { int x{0}, y{0}; };
struct Rect  { int x0, y0, x1, y1;  // [x0,x1) x [y0,y1)
    bool contains(int x,int y) const { return x>=x0 && x<x1 && y>=y0 && y<y1; }
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
};

struct Path {
    std::vector<Point> points;
    float cost{std::numeric_limits<float>::infinity()};
    bool  found{false};
};

struct HPAParams {
    int   clusterSize          = 32;  // typical: 16/32
    bool  allowDiagonal        = true;
    int   entranceSplitThresh  = 5;   // <=T: one portal; >T: two portals (per side)
    bool  smoothPath           = true;
    bool  storeIntraPaths      = true; // cache refinement paths inside clusters
};

class HPAStar {
public:
    HPAStar(const IGrid& grid, HPAParams params)
        : m_grid(grid), P(params)
    {
        rebuildAll();
    }

    // Rebuild whole abstraction (call if cluster size or global settings changed)
    void rebuildAll();

    // Rebuild the cluster that contains (x,y) and its borders to neighbors.
    void rebuildClusterAt(int x, int y);

    // Rebuild a rectangular region (in cells) - useful for bulk edits.
    void rebuildRegion(const Rect& r);

    // Top-level query
    Path findPath(Point start, Point goal);

private:
    // ------------------ Types for the abstract graph ------------------
    using NodeId = uint32_t;

    struct PortalNode {
        NodeId   id;
        Point    cell;        // location on the grid (inside its cluster)
        int      clusterIdx;  // owner cluster
    };

    struct Edge {
        NodeId to;
        float  w;
        bool   interCluster;  // false = intra (same cluster), true = crossing border
    };

    struct Cluster {
        Rect bounds;
        std::vector<NodeId> portals; // portal nodes that belong to this cluster
    };

    struct IntraKey {
        NodeId a, b;
        bool operator==(const IntraKey& o) const noexcept {
            return (a==o.a && b==o.b);
        }
    };
    struct IntraKeyHash {
        size_t operator()(const IntraKey& k) const noexcept {
            return (size_t(k.a) << 32) ^ size_t(k.b);
        }
    };

    // ------------------ Core data ------------------
    const IGrid&              m_grid;
    HPAParams                 P;
    int                       m_numX{0}, m_numY{0};
    std::vector<Cluster>      m_clusters;   // size = m_numX*m_numY
    std::vector<PortalNode>   m_nodes;      // all portal nodes
    std::vector<std::vector<Edge>> m_adj;   // adjacency list (abstract graph)

    // Optional cache of intra-cluster low-level paths used during refinement
    std::unordered_map<IntraKey, std::vector<Point>, IntraKeyHash> m_intraPathCache;

    // ------------------ Helpers ------------------
    int  clusterIndexFromCell(int x, int y) const;
    Rect clusterBounds(int cx, int cy) const;

    // Build steps
    void clearAll();
    void buildClusters();
    void buildEntrancesAndPortals();
    void buildIntraEdgesForCluster(int cidx);
    void linkInterEdgesBetween(int aIdx, int bIdx);

    // Entrances for a shared border: scan boundary and return contiguous segments
    // Each segment is in terms of boundary coordinate t in [t0, t1], horizontal or vertical
    struct EntranceSeg { int t0, t1; bool vertical; int fixed; /* x or y along border */ };
    void detectEntrancesBetween(int aIdx, int bIdx, std::vector<EntranceSeg>& out);

    // Add 1 or 2 portals per entrance *on each side* following threshold rule
    void placePortalsForEntrance(int aIdx, int bIdx, const EntranceSeg& seg);

    // Intra-cluster shortest path restricted to cluster bounds (A* on grid subsection)
    bool localSearch(const Rect& bounds, Point s, Point g, float& outCost, std::vector<Point>* outPath);

    // Abstract A*: from "start virtual node" connected to start-cluster portals
    // to ANY goal-cluster portal (we'll attach virtual start edges at runtime)
    bool abstractAStar(const std::vector<std::pair<NodeId,float>>& startEdges,
                       const std::unordered_set<NodeId>& goalPortals,
                       Point goalCell,
                       std::vector<NodeId>& outCameFromOrder,
                       std::unordered_map<NodeId, NodeId>& outParent);

    // Refinement: stitch low-level paths between portal sequence
    bool refinePath(Point start, Point goal,
                    const std::vector<NodeId>& portalSequence,
                    std::vector<Point>& outPoints, float& outCost);

    // Utility
    inline float heuristicGrid(Point a, Point b) const;   // octile / manhattan
    inline bool  inBounds(int x,int y) const {
        return x>=0 && y>=0 && x<m_grid.width() && y<m_grid.height();
    }

    // Temporary mapping from NodeId to per-query edge list (for start-virtual)
    struct TempEdge { NodeId to; float w; };
};

} // namespace hpa
