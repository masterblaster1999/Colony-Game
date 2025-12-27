#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "colony/pathfinding/AStar.hpp"
#include "colony/pathfinding/GridMap.hpp"

namespace colony::proto {

// Extremely small, gameplay-forward "proto" world:
//  - 2D tile grid
//  - player places *plans* (blueprints)
//  - colonists pathfind to an adjacent tile and construct them
//  - simple food production/consumption

enum class TileType : std::uint8_t {
    Empty = 0,
    Floor,
    Wall,
    Farm,
    Stockpile,
};

[[nodiscard]] const char* TileTypeName(TileType t) noexcept;
[[nodiscard]] bool TileIsWalkable(TileType t) noexcept;
[[nodiscard]] int TileWoodCost(TileType t) noexcept;
[[nodiscard]] float TileBuildTimeSeconds(TileType t) noexcept;

struct Inventory {
    int wood = 50;
    float food = 20.0f;
};

struct Cell {
    TileType built = TileType::Empty;
    TileType planned = TileType::Empty;

    // Plan priority (prototype).
    //  - 0 = lowest (displayed as 1)
    //  - 3 = highest (displayed as 4)
    // Only meaningful while planned != Empty && planned != built.
    std::uint8_t planPriority = 0;

    // Remaining construction work for the planned tile.
    // Only meaningful while planned != built.
    float workRemaining = 0.0f;

    // Simple reservation to avoid multiple colonists picking the same plan.
    // -1 = unreserved
    int reservedBy = -1;
};

struct Colonist {
    int id = 0;

    // Position in tile coordinates (e.g. 10.5f means center of tile x=10).
    float x = 0.5f;
    float y = 0.5f;

    // Job / pathing
    bool hasJob = false;
    int targetX = 0;
    int targetY = 0;

    std::vector<colony::pf::IVec2> path;
    std::size_t pathIndex = 0;
};

enum class PlacePlanResult : std::uint8_t {
    Ok = 0,
    OutOfBounds,
    NoChange,
    NotEnoughWood,
};

[[nodiscard]] const char* PlacePlanResultName(PlacePlanResult r) noexcept;

class World {
public:
    World();
    explicit World(int w, int h, std::uint32_t seed = 1);

    void reset(int w, int h, std::uint32_t seed = 1);

    [[nodiscard]] int width() const noexcept { return m_w; }
    [[nodiscard]] int height() const noexcept { return m_h; }

    [[nodiscard]] bool inBounds(int x, int y) const noexcept;

    [[nodiscard]] const Cell& cell(int x, int y) const noexcept;
    [[nodiscard]] Cell& cell(int x, int y) noexcept;

    [[nodiscard]] const Inventory& inventory() const noexcept { return m_inv; }
    [[nodiscard]] Inventory& inventory() noexcept { return m_inv; }

    [[nodiscard]] const std::vector<Colonist>& colonists() const noexcept { return m_colonists; }
    [[nodiscard]] std::vector<Colonist>& colonists() noexcept { return m_colonists; }

    [[nodiscard]] const colony::pf::GridMap& nav() const noexcept { return m_nav; }

    // Plan placement. Plans are blueprints and are built by colonists over time.
    // Costs are applied as a delta between old plan and new plan.
    PlacePlanResult placePlan(int x, int y, TileType plan, std::uint8_t planPriority = 0);

    void clearAllPlans();

    // Clears all colonist jobs and all per-cell reservations.
    // Useful after player edits plans (undo/redo) so colonists don't keep walking
    // toward a plan that no longer exists.
    void CancelAllJobsAndClearReservations() noexcept;

    [[nodiscard]] int plannedCount() const noexcept;
    [[nodiscard]] int builtCount(TileType t) const noexcept;

    // Simulation tick (fixed dt). dt is seconds.
    void tick(double dtSeconds);

    // ---------------------------------------------------------------------
    // Persistence (prototype)
    // ---------------------------------------------------------------------
    // Save/load to a JSON file. Intended for the prototype game only.
    //
    // On load, derived caches (nav + planned-cache) are rebuilt and colonist
    // jobs/paths are cleared.
    [[nodiscard]] bool SaveJson(const std::filesystem::path& path, std::string* outError = nullptr) const noexcept;
    [[nodiscard]] bool LoadJson(const std::filesystem::path& path, std::string* outError = nullptr) noexcept;

    // Tuning knobs (can be edited live from UI)
    double buildWorkPerSecond = 1.0; // work units/sec
    double colonistWalkSpeed = 3.0;  // tiles/sec

    double farmFoodPerSecond = 0.25;
    double foodPerColonistPerSecond = 0.05;

private:
    static constexpr std::size_t kTileTypeCount = static_cast<std::size_t>(TileType::Stockpile) + 1u;

    [[nodiscard]] std::size_t idx(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y * m_w + x);
    }

    void syncNavCell(int x, int y) noexcept;
    void syncAllNav() noexcept;

    // ---------------------------------------------------------------------
    // Plan tracking
    // ---------------------------------------------------------------------
    // The prototype started with a naive full-grid scan for plannedCount() and
    // job assignment. That becomes expensive as the grid grows. We keep a small
    // index of *active* plans (planned != Empty && planned != built) to:
    //   - make plannedCount() O(1)
    //   - avoid scanning every tile for job assignment
    //
    // Implementation details:
    //   - m_plannedCells stores the list of active-plan coordinates.
    //   - m_plannedIndex maps a flattened cell index -> index into m_plannedCells
    //     (or -1 if the cell is not currently an active plan).
    void rebuildPlannedCache() noexcept;
    void planCacheAdd(int x, int y) noexcept;
    void planCacheRemove(int x, int y) noexcept;

    // Job assignment: assign idle colonists to nearby unreserved plans.
    // Throttled to avoid doing expensive path searches every tick when no jobs can be found.
    void assignJobs(double dtSeconds);

    // Finds a path from (startX,startY) to the nearest walkable "work tile" that is
    // adjacent to an available plan.
    //
    // If requiredPriority >= 0, only plans with that planPriority are considered.
    [[nodiscard]] bool findPathToNearestAvailablePlan(int startX, int startY,
                                                      int& outPlanX, int& outPlanY,
                                                      std::vector<colony::pf::IVec2>& outPath,
                                                      int requiredPriority) const;

    static constexpr double kJobAssignIntervalSeconds = 0.20;

    bool computePathToAdjacent(Colonist& c, int targetX, int targetY);
    bool computePathToAdjacentFrom(int startX, int startY,
                                   int targetX, int targetY,
                                   std::vector<colony::pf::IVec2>& outPath) const;

    void stepColonist(Colonist& c, double dtSeconds);
    void stepConstructionIfReady(Colonist& c, double dtSeconds);
    void cancelJob(Colonist& c) noexcept;

    void applyPlanIfComplete(int targetX, int targetY) noexcept;

    // Cached counts for *built* tiles. This avoids full-grid scans in tick/UI.
    void rebuildBuiltCounts() noexcept;
    void builtCountAdjust(TileType oldBuilt, TileType newBuilt) noexcept;

    int m_w = 0;
    int m_h = 0;

    std::vector<Cell> m_cells;
    Inventory m_inv{};

    std::vector<Colonist> m_colonists;
    colony::pf::GridMap m_nav;

    // Active plan cache.
    std::vector<colony::pf::IVec2> m_plannedCells;
    std::vector<int>              m_plannedIndex;

    std::array<int, kTileTypeCount> m_builtCounts{};

    double m_jobAssignCooldown = 0.0;

    // Scratch buffers for nearest-plan search (Dijkstra / uniform-cost search).
    // Reused across calls to avoid per-call allocations and O(w*h) clears.
    mutable std::vector<float> m_nearestDist;
    mutable std::vector<colony::pf::NodeId> m_nearestParent;
    mutable std::vector<std::uint32_t> m_nearestStamp;
    mutable std::uint32_t m_nearestStampValue = 1;

    std::mt19937 m_rng{};
};

} // namespace colony::proto
