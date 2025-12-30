#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "game/Role.hpp"

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

    // Plan-only: marks an existing built tile for deconstruction.
    Remove,

    // Natural obstacle/resource: can be chopped (via Demolish plan) for wood.
    Tree,
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

    // True if the current built tile was produced by completing a plan (i.e., player-built),
    // as opposed to being part of the pre-seeded starting area / borders.
    //
    // This lets deconstruction refund wood without allowing "free wood" from the seeded tiles.
    bool builtFromPlan = false;

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

    // -----------------------------------------------------------------
    // Farm simulation (prototype)
    // -----------------------------------------------------------------
    // Farm growth progress in [0, 1]. Only meaningful when built == Farm.
    // 0 = newly planted, 1 = ready to harvest.
    float farmGrowth = 0.0f;

    // Reservation for harvesting (separate from plan reservations).
    // -1 = unreserved.
    int farmReservedBy = -1;

    // -----------------------------------------------------------------
    // Loose resources (prototype)
    // -----------------------------------------------------------------
    // Wood dropped on the ground. Colonists with Hauling capability can
    // move this to a Stockpile, where it becomes usable by the global
    // inventory (m_inv.wood).
    int looseWood = 0;

    // Reservation to avoid multiple colonists hauling the same stack.
    // -1 = unreserved.
    int looseWoodReservedBy = -1;
};

struct Colonist {
    int id = 0;

    // Position in tile coordinates (e.g. 10.5f means center of tile x=10).
    float x = 0.5f;
    float y = 0.5f;

    // -----------------------------------------------------------------
    // Needs (prototype)
    // -----------------------------------------------------------------
    // Personal food reserve ("stomach") in the same units as Inventory::food.
    // The world drains this over time; when it gets low, colonists will seek
    // food and refill it from the global inventory.
    float personalFood = 0.0f;

    // -----------------------------------------------------------------
    // Role / progression (prototype)
    // -----------------------------------------------------------------
    // Role influences what jobs a colonist will take automatically (capabilities)
    // and can apply movement/work multipliers.
    //
    // role.level / role.xp are updated as colonists complete work.
    RoleComponent role{};

    // -----------------------------------------------------------------
    // Player control
    // -----------------------------------------------------------------
    // Drafted colonists do not take autonomous build/harvest jobs.
    // They can still eat when hungry, but otherwise wait for manual orders.
    bool drafted = false;

    // -----------------------------------------------------------------
    // Job / pathing
    // -----------------------------------------------------------------
    enum class JobKind : std::uint8_t {
        None = 0,
        BuildPlan,
        Harvest,
        Eat,
        HaulWood,
        // Player-issued "go here" order (requires drafted).
        ManualMove,
    };

    JobKind jobKind = JobKind::None;

    // Job / pathing
    bool hasJob = false;
    int targetX = 0;
    int targetY = 0;

    std::vector<colony::pf::IVec2> path;
    std::size_t pathIndex = 0;

    // Seconds remaining for the "Eat" job once the colonist has arrived.
    float eatWorkRemaining = 0.0f;

    // Seconds remaining for the "Harvest" job once the colonist has arrived.
    float harvestWorkRemaining = 0.0f;

    // -----------------------------------------------------------------
    // Hauling (prototype)
    // -----------------------------------------------------------------
    // Amount of wood currently being carried by this colonist.
    int   carryingWood = 0;

    // HaulWood is a 2-stage job:
    //   1) walk to a loose-wood tile (haulPickupX/Y) and pick it up
    //   2) walk to a stockpile tile (haulDropX/Y) and deposit it
    int   haulPickupX = 0;
    int   haulPickupY = 0;
    int   haulDropX   = 0;
    int   haulDropY   = 0;
    bool  haulingToDropoff = false;

    // Seconds remaining for pickup/drop work once the colonist has arrived.
    float haulWorkRemaining = 0.0f;
};

enum class PlacePlanResult : std::uint8_t {
    Ok = 0,
    OutOfBounds,
    NoChange,
    NotEnoughWood,
};

[[nodiscard]] const char* PlacePlanResultName(PlacePlanResult r) noexcept;

// Direct (player-issued) colonist orders.
enum class OrderResult : std::uint8_t {
    Ok = 0,
    InvalidColonist,
    NotDrafted,
    OutOfBounds,
    TargetBlocked,
    NoPath,
    TargetNotPlanned,
    TargetReserved,
    TargetNotFarm,
    TargetNotHarvestable,
};

[[nodiscard]] const char* OrderResultName(OrderResult r) noexcept;

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

    // ------------------------------------------------------------
    // Player orders
    // ------------------------------------------------------------
    // Drafted colonists do not take autonomous build/harvest jobs.
    // They can still eat when hungry.
    bool SetColonistDrafted(int colonistId, bool drafted) noexcept;

    // Sets a colonist's role (prototype).
    // If the colonist is currently doing a job that the new role cannot perform
    // autonomously, the job is cancelled so it can be reassigned.
    bool SetColonistRole(int colonistId, RoleId role) noexcept;

    // Cancels any current job (and releases reservations). Does not change drafted state.
    OrderResult CancelColonistJob(int colonistId) noexcept;

    // Issue direct orders. Requires the colonist to be drafted.
    // Build/Harvest orders also respect reservations.
    OrderResult OrderColonistMove(int colonistId, int targetX, int targetY) noexcept;
    OrderResult OrderColonistBuild(int colonistId, int planX, int planY) noexcept;
    OrderResult OrderColonistHarvest(int colonistId, int farmX, int farmY) noexcept;

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
    [[nodiscard]] int harvestableFarmCount() const noexcept;

    // Total wood dropped on the ground (not yet in the global inventory).
    [[nodiscard]] int looseWoodTotal() const noexcept { return m_looseWoodTotal; }

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

    // Farming (prototype): farms grow over time and must be harvested by colonists.
    // When a farm reaches full growth (farmGrowth == 1), a colonist can harvest it
    // to convert the crop into food in the global inventory.
    //
    // farmGrowDurationSeconds: seconds for a farm to go from 0 -> 1 growth.
    // farmHarvestYieldFood:    food produced by one harvest.
    // farmHarvestDurationSeconds: time a colonist spends harvesting after arriving.
    double farmGrowDurationSeconds    = 40.0;
    double farmHarvestYieldFood       = 10.0;
    double farmHarvestDurationSeconds = 1.0;


    // Forestry (prototype): Trees are natural obstacles that can be chopped for wood.
    // Over time, trees can spread into nearby empty tiles (simple regrowth).
    //
    // treeChopYieldWood: wood added to inventory when a tree is removed (Demolish) or built over.
    // treeSpreadAttemptsPerSecond: how many random empty tiles are considered per second for growth.
    // treeSpreadChancePerAttempt: probability of growth on a valid candidate (adjacent to an existing tree).
    int treeChopYieldWood = 4;
    double treeSpreadAttemptsPerSecond  = 2.5;
    double treeSpreadChancePerAttempt   = 0.15;

    double foodPerColonistPerSecond = 0.05;

    // Hunger/eating (prototype): colonists maintain a personal food reserve.
    // When personalFood drops below colonistEatThresholdFood, they seek food.
    double colonistMaxPersonalFood     = 6.0;
    double colonistEatThresholdFood    = 2.0;
    double colonistEatDurationSeconds  = 1.5;

    // Hauling (prototype): loose wood is dropped on the ground and must be hauled
    // to a Stockpile before it becomes usable by the colony inventory.
    //
    // haulCarryCapacity: base number of wood units a colonist can carry (role carryBonus is added).
    // haulPickupDurationSeconds / haulDropoffDurationSeconds: time spent picking up / depositing.
    int    haulCarryCapacity           = 25;
    double haulPickupDurationSeconds   = 0.25;
    double haulDropoffDurationSeconds  = 0.25;

private:
    static constexpr std::size_t kTileTypeCount = static_cast<std::size_t>(TileType::Tree) + 1u;

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

    // Cache of built farms (built == Farm) so we can update growth
    // and search for harvest jobs without scanning the entire grid.
    void rebuildFarmCache() noexcept;
    void farmCacheAdd(int x, int y) noexcept;
    void farmCacheRemove(int x, int y) noexcept;

    // Cache of tiles containing loose wood (looseWood > 0).
    void rebuildLooseWoodCache() noexcept;
    void looseWoodCacheAdd(int x, int y) noexcept;
    void looseWoodCacheRemove(int x, int y) noexcept;
    void adjustLooseWood(int x, int y, int delta) noexcept;

    // Job assignment: assign idle colonists to nearby unreserved plans.
    // Throttled to avoid doing expensive path searches every tick when no jobs can be found.
    void assignJobs(double dtSeconds);

    // Assign hungry colonists to the nearest reachable food source (stockpile/farm).
    void assignEatJobs(double dtSeconds);

    // Assign idle colonists to harvest ripe farms.
    void assignHarvestJobs(double dtSeconds);

    // Assign idle colonists to haul loose wood to the nearest stockpile.
    void assignHaulJobs(double dtSeconds);

    // Finds a path from (startX,startY) to the nearest walkable "work tile" that is
    // adjacent to an available plan.
    //
    // If requiredPriority >= 0, only plans with that planPriority are considered.
    [[nodiscard]] bool findPathToNearestAvailablePlan(int startX, int startY,
                                                      int& outPlanX, int& outPlanY,
                                                      std::vector<colony::pf::IVec2>& outPath,
                                                      int requiredPriority) const;

    // Finds a path from (startX,startY) to the nearest walkable tile that is
    // adjacent to a built food source (stockpile/farm).
    [[nodiscard]] bool findPathToNearestFoodSource(int startX, int startY,
                                                   int& outFoodX, int& outFoodY,
                                                   std::vector<colony::pf::IVec2>& outPath) const;


    // Finds a path from (startX,startY) to the nearest walkable tile that is
    // adjacent to a harvestable farm (built == Farm && farmGrowth == 1).
    [[nodiscard]] bool findPathToNearestHarvestableFarm(int startX, int startY,
                                                        int& outFarmX, int& outFarmY,
                                                        std::vector<colony::pf::IVec2>& outPath) const;


// Finds a path from (startX,startY) to the nearest reachable tile that contains
// loose wood (looseWood > 0) that is not reserved.
[[nodiscard]] bool findPathToNearestLooseWood(int startX, int startY,
                                              int& outWoodX, int& outWoodY,
                                              std::vector<colony::pf::IVec2>& outPath) const;

// Finds a path from (startX,startY) to the nearest reachable built Stockpile tile.
[[nodiscard]] bool findPathToNearestStockpile(int startX, int startY,
                                              int& outX, int& outY,
                                              std::vector<colony::pf::IVec2>& outPath) const;

    static constexpr double kJobAssignIntervalSeconds = 0.20;

    [[nodiscard]] Colonist* findColonistById(int colonistId) noexcept;
    [[nodiscard]] const Colonist* findColonistById(int colonistId) const noexcept;

    bool computePathToAdjacent(Colonist& c, int targetX, int targetY);
    bool computePathToAdjacentFrom(int startX, int startY,
                                   int targetX, int targetY,
                                   std::vector<colony::pf::IVec2>& outPath) const;

    bool computePathToTile(Colonist& c, int targetX, int targetY);
    bool computePathToTileFrom(int startX, int startY,
                               int targetX, int targetY,
                               std::vector<colony::pf::IVec2>& outPath) const;

    void stepColonist(Colonist& c, double dtSeconds);
    void stepConstructionIfReady(Colonist& c, double dtSeconds);
    void stepHarvestIfReady(Colonist& c, double dtSeconds);
    void stepEatingIfReady(Colonist& c, double dtSeconds);
    void stepHaulIfReady(Colonist& c, double dtSeconds);
    void cancelJob(Colonist& c) noexcept;

    void applyPlanIfComplete(int targetX, int targetY) noexcept;

    // Drops loose wood on or near a tile. This is used for tree chopping and
    // deconstruction refunds. If no reachable drop tile exists, the wood is
    // added directly to the global inventory as a fallback.
    void dropLooseWoodNear(int x, int y, int amount) noexcept;

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

    // Built farm cache.
    std::vector<colony::pf::IVec2> m_farmCells;
    std::vector<int>              m_farmIndex;

    // Loose wood cache (tiles with looseWood > 0).
    std::vector<colony::pf::IVec2> m_looseWoodCells;
    std::vector<int>              m_looseWoodIndex;
    int                           m_looseWoodTotal = 0;

    std::array<int, kTileTypeCount> m_builtCounts{};

    double m_jobAssignCooldown = 0.0;
    double m_harvestAssignCooldown = 0.0;
    double m_haulAssignCooldown = 0.0;
    // Accumulator for fractional tree spread attempts.
    double m_treeSpreadAccum = 0.0;


    // Scratch buffers for nearest-plan search (Dijkstra / uniform-cost search).
    // Reused across calls to avoid per-call allocations and O(w*h) clears.
    mutable std::vector<float> m_nearestDist;
    mutable std::vector<colony::pf::NodeId> m_nearestParent;
    mutable std::vector<std::uint32_t> m_nearestStamp;
    mutable std::uint32_t m_nearestStampValue = 1;

    std::mt19937 m_rng{};
};

} // namespace colony::proto
