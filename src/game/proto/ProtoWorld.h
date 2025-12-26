#pragma once

#include <cstdint>
#include <random>
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
    PlacePlanResult placePlan(int x, int y, TileType plan);

    void clearAllPlans();

    [[nodiscard]] int plannedCount() const noexcept;
    [[nodiscard]] int builtCount(TileType t) const noexcept;

    // Simulation tick (fixed dt). dt is seconds.
    void tick(double dtSeconds);

    // Tuning knobs (can be edited live from UI)
    double buildWorkPerSecond = 1.0; // work units/sec
    double colonistWalkSpeed = 3.0;  // tiles/sec

    double farmFoodPerSecond = 0.25;
    double foodPerColonistPerSecond = 0.05;

private:
    [[nodiscard]] std::size_t idx(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y * m_w + x);
    }

    void syncNavCell(int x, int y) noexcept;
    void syncAllNav() noexcept;

    void assignJobs();
    bool computePathToAdjacent(Colonist& c, int targetX, int targetY);

    void stepColonist(Colonist& c, double dtSeconds);
    void stepConstructionIfReady(Colonist& c, double dtSeconds);
    void cancelJob(Colonist& c) noexcept;

    void applyPlanIfComplete(int targetX, int targetY) noexcept;

    int m_w = 0;
    int m_h = 0;

    std::vector<Cell> m_cells;
    Inventory m_inv{};

    std::vector<Colonist> m_colonists;
    colony::pf::GridMap m_nav;

    std::mt19937 m_rng{};
};

} // namespace colony::proto
