#include "game/proto/ProtoWorld.h"

#include "colony/pathfinding/JPS.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <string>



namespace colony::proto {

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] int clampi(int v, int lo, int hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] int posToTile(float p) noexcept
{
    return static_cast<int>(std::floor(p));
}

[[nodiscard]] bool TileIsRoomSpace(TileType t) noexcept
{
    switch (t)
    {
    case TileType::Empty:
    case TileType::Floor:
    case TileType::Farm:
    case TileType::Stockpile:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] float TileNavCost(TileType t) noexcept
{
    // NOTE: Costs must be >= 1.0f to keep the octile heuristic admissible.
    //       Lower costs would let A* overestimate and break optimality.
    switch (t)
    {
    case TileType::Farm:      return 1.25f; // crops / uneven ground
    case TileType::Stockpile: return 1.10f; // clutter
    case TileType::Door:      return 1.05f; // opening
    default:                  return 1.00f;
    }
}

[[nodiscard]] std::uint64_t PackPlanKey(int x, int y) noexcept
{
    // Pack into a sortable key (Y-major) without assuming small map sizes.
    // Lowest Y/X sorts first.
    const std::uint64_t ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x));
    const std::uint64_t uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
    return (uy << 32) | ux;
}

[[nodiscard]] int UnpackPlanX(std::uint64_t k) noexcept
{
    return static_cast<int>(static_cast<std::uint32_t>(k & 0xFFFF'FFFFu));
}

[[nodiscard]] int UnpackPlanY(std::uint64_t k) noexcept
{
    return static_cast<int>(static_cast<std::uint32_t>((k >> 32) & 0xFFFF'FFFFu));
}

// Food target key packs a type rank into the top bit so we can deterministically
// prefer Stockpiles over Farms when distances tie (used by the eat distance field).
constexpr std::uint64_t kFoodRankBit = (1ull << 63);

[[nodiscard]] std::uint64_t PackFoodKey(int x, int y, bool isStockpile) noexcept
{
    const std::uint64_t base = PackPlanKey(x, y);
    return isStockpile ? base : (base | kFoodRankBit);
}

[[nodiscard]] int UnpackFoodX(std::uint64_t k) noexcept
{
    return UnpackPlanX(k);
}

[[nodiscard]] int UnpackFoodY(std::uint64_t k) noexcept
{
    return UnpackPlanY(k & ~kFoodRankBit);
}

void ExpandSparsePath(const std::vector<colony::pf::IVec2>& in,
                      std::vector<colony::pf::IVec2>& out)
{
    out.clear();
    if (in.empty())
        return;

    out.reserve(in.size() * 2);
    out.push_back(in.front());

    for (std::size_t i = 1; i < in.size(); ++i)
    {
        colony::pf::IVec2 cur = out.back();
        const colony::pf::IVec2 tgt = in[i];

        const int stepX = (tgt.x > cur.x) ? 1 : (tgt.x < cur.x) ? -1 : 0;
        const int stepY = (tgt.y > cur.y) ? 1 : (tgt.y < cur.y) ? -1 : 0;

        // JPS should only return straight or perfect-diagonal segments. However,
        // we keep this robust by stepping each axis independently until we reach
        // the target.
        while (cur.x != tgt.x || cur.y != tgt.y)
        {
            if (cur.x != tgt.x) cur.x += stepX;
            if (cur.y != tgt.y) cur.y += stepY;
            out.push_back(cur);
        }
    }
}

[[nodiscard]] bool ValidateDensePath(const colony::pf::GridMap& nav,
                                    int w,
                                    int h,
                                    const std::vector<colony::pf::IVec2>& path) noexcept
{
    if (path.empty())
        return false;

    for (const auto& p : path)
    {
        if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h)
            return false;
        if (!nav.passable(p.x, p.y))
            return false;
    }

    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const auto a = path[i - 1];
        const auto b = path[i];
        const int dx = b.x - a.x;
        const int dy = b.y - a.y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1)
            return false;
        if (dx == 0 && dy == 0)
            return false;
        if (!nav.can_step(a.x, a.y, dx, dy))
            return false;
    }

    return true;
}

[[nodiscard]] float DensePathCost(const colony::pf::GridMap& nav,
                                 const std::vector<colony::pf::IVec2>& path) noexcept
{
    if (path.size() < 2)
        return 0.0f;

    float cost = 0.0f;
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const auto a = path[i - 1];
        const auto b = path[i];
        const int dx = b.x - a.x;
        const int dy = b.y - a.y;
        cost += nav.step_cost(a.x, a.y, dx, dy);
    }
    return cost;
}

[[nodiscard]] bool ComputePathAlgo(const colony::pf::GridMap& nav,
                                  PathAlgo algo,
                                  int startX,
                                  int startY,
                                  int targetX,
                                  int targetY,
                                  std::vector<colony::pf::IVec2>& outPath)
{
    outPath.clear();

    if (algo == PathAlgo::JumpPointSearch)
    {
        colony::pf::JPS jps(nav);
        const colony::pf::Path p = jps.find_path({startX, startY}, {targetX, targetY});
        if (p.points.empty())
            return false;

        ExpandSparsePath(p.points, outPath);
        return !outPath.empty();
    }

    colony::pf::AStar astar(nav);
    const colony::pf::Path p = astar.find_path({startX, startY}, {targetX, targetY});
    outPath = p.points;
    return !outPath.empty();
}

// -----------------------------------------------------------------------------
// Colonist role helpers (prototype)
// -----------------------------------------------------------------------------

[[nodiscard]] bool HasCap(const colony::proto::Colonist& c, Capability cap) noexcept
{
    return HasAny(c.role.caps(), cap);
}

// Work priority helpers (prototype).
//
// Priorities use the range:
//   0 = Off, 1 = Highest ... 4 = Lowest
//
// For convenience, we treat Off as "infinite" priority.
static constexpr int kWorkPrioOff = 9999;

[[nodiscard]] int WorkPrioEff(std::uint8_t p) noexcept
{
    return (p == 0u) ? kWorkPrioOff : static_cast<int>(p);
}

[[nodiscard]] int BestWorkPrio(const colony::proto::Colonist& c,
                              bool buildAvailable,
                              bool farmAvailable,
                              bool haulAvailable) noexcept
{
    int best = kWorkPrioOff;

    if (buildAvailable && HasCap(c, Capability::Building))
        best = std::min(best, WorkPrioEff(c.workPrio.build));
    if (farmAvailable && HasCap(c, Capability::Farming))
        best = std::min(best, WorkPrioEff(c.workPrio.farm));
    if (haulAvailable && HasCap(c, Capability::Hauling))
        best = std::min(best, WorkPrioEff(c.workPrio.haul));

    return best;
}

[[nodiscard]] float LevelMoveBonus(const colony::proto::Colonist& c) noexcept
{
    const int lvl = std::max(1, static_cast<int>(c.role.level));
    // Small, linear progression. Kept conservative so the simulation stays readable.
    const float bonus = 1.0f + 0.01f * static_cast<float>(lvl - 1);
    return clampf(bonus, 0.5f, 2.0f);
}

[[nodiscard]] float LevelWorkBonus(const colony::proto::Colonist& c) noexcept
{
    const int lvl = std::max(1, static_cast<int>(c.role.level));
    const float bonus = 1.0f + 0.02f * static_cast<float>(lvl - 1);
    return clampf(bonus, 0.5f, 2.5f);
}

[[nodiscard]] float EffectiveMoveMult(const colony::proto::Colonist& c) noexcept
{
    const float base = std::max(0.05f, c.role.move());
    return clampf(base * LevelMoveBonus(c), 0.1f, 5.0f);
}

[[nodiscard]] float EffectiveWorkMult(const colony::proto::Colonist& c) noexcept
{
    const float base = std::max(0.05f, c.role.work());
    return clampf(base * LevelWorkBonus(c), 0.1f, 6.0f);
}

[[nodiscard]] std::uint32_t XpForPlanCompletion(TileType plan) noexcept
{
    // Reward a blend of time + material cost so walls/farms feel "bigger" than floors.
    const float t   = std::max(0.05f, TileBuildTimeSeconds(plan));
    const int   mat = std::max(0, TileWoodCost(plan));

    const float score = t * 20.0f + static_cast<float>(mat) * 6.0f;
    const int xp = std::max(1, static_cast<int>(std::lround(score)));
    return static_cast<std::uint32_t>(clampi(xp, 1, 1000));
}

[[nodiscard]] std::uint32_t XpForHarvest(float yieldFood) noexcept
{
    const float y = std::max(0.0f, yieldFood);
    const int xp = std::max(1, static_cast<int>(std::lround(10.0f + y)));
    return static_cast<std::uint32_t>(clampi(xp, 1, 1000));
}

} // namespace

const char* TileTypeName(TileType t) noexcept
{
    switch (t)
    {
    case TileType::Empty: return "Empty";
    case TileType::Floor: return "Floor";
    case TileType::Wall: return "Wall";
    case TileType::Farm: return "Farm";
    case TileType::Stockpile: return "Stockpile";
    case TileType::Remove: return "Demolish";
    case TileType::Tree: return "Tree";
    case TileType::Door: return "Door";
    default: return "?";
    }
}

const char* PathAlgoName(PathAlgo a) noexcept
{
    switch (a)
    {
    case PathAlgo::AStar: return "AStar";
    case PathAlgo::JumpPointSearch: return "JPS";
    default: return "?";
    }
}

PathAlgo PathAlgoFromName(std::string_view s) noexcept
{
    if (s == "AStar" || s == "astar" || s == "A*" || s == "A-Star")
        return PathAlgo::AStar;
    if (s == "JPS" || s == "jps" || s == "JumpPointSearch" || s == "jump-point-search")
        return PathAlgo::JumpPointSearch;
    return PathAlgo::AStar;
}

bool TileIsWalkable(TileType t) noexcept
{
    return t != TileType::Wall && t != TileType::Tree;
}

int TileWoodCost(TileType t) noexcept
{
    switch (t)
    {
    case TileType::Empty: return 0;
    case TileType::Floor: return 1;
    case TileType::Wall: return 2;
    case TileType::Farm: return 3;
    case TileType::Stockpile: return 1;
    case TileType::Remove: return 0;
    case TileType::Tree: return 0;
    case TileType::Door: return 1;
    default: return 0;
    }
}

int PlanDeltaWoodCost(const Cell& c, TileType plan) noexcept
{
    // Keep this logic in sync with World::placePlan.
    if (plan == TileType::Remove && c.built == TileType::Empty)
        plan = TileType::Empty;

    // Clearing a plan refunds the old planned material cost.
    if (plan == TileType::Empty)
    {
        if (c.planned == TileType::Empty)
            return 0;
        return -TileWoodCost(c.planned);
    }

    const TileType oldPlan = (c.planned == TileType::Empty) ? c.built : c.planned;
    if (oldPlan == plan)
        return 0;

    // Delta-cost the plan swap, but do not refund built tiles (handled by placePlan when demolishing).
    const int oldCost = TileWoodCost(c.planned);
    const int newCost = (plan == c.built) ? 0 : TileWoodCost(plan);
    return newCost - oldCost;
}

bool PlanWouldChange(const Cell& c, TileType plan, std::uint8_t planPriority) noexcept
{
    if (planPriority > 3u)
        planPriority = 3u;

    // Match the "Remove on empty built" special-case from placePlan.
    if (plan == TileType::Remove && c.built == TileType::Empty)
        plan = TileType::Empty;

    // Clearing plan.
    if (plan == TileType::Empty)
        return c.planned != TileType::Empty;

    const TileType oldPlan = (c.planned == TileType::Empty) ? c.built : c.planned;
    if (oldPlan == plan)
    {
        // Only an active plan carries a priority.
        if (c.planned != TileType::Empty && c.planned != c.built)
            return c.planPriority != planPriority;
        return false;
    }

    // Different plan tile always mutates the cell.
    return true;
}


float TileBuildTimeSeconds(TileType t) noexcept
{
    switch (t)
    {
    case TileType::Empty: return 0.15f;
    case TileType::Floor: return 0.40f;
    case TileType::Wall: return 0.80f;
    case TileType::Farm: return 1.25f;
    case TileType::Stockpile: return 0.55f;
    case TileType::Remove: return 0.65f;
    case TileType::Tree: return 0.90f;
    case TileType::Door: return 0.70f;
    default: return 0.50f;
    }
}

const char* PlacePlanResultName(PlacePlanResult r) noexcept
{
    switch (r)
    {
    case PlacePlanResult::Ok: return "Ok";
    case PlacePlanResult::OutOfBounds: return "Out of bounds";
    case PlacePlanResult::NoChange: return "No change";
    case PlacePlanResult::NotEnoughWood: return "Not enough wood";
    default: return "?";
    }
}

const char* OrderResultName(OrderResult r) noexcept
{
    switch (r)
    {
    case OrderResult::Ok: return "Ok";
    case OrderResult::InvalidColonist: return "Invalid colonist";
    case OrderResult::NotDrafted: return "Colonist not drafted";
    case OrderResult::OutOfBounds: return "Out of bounds";
    case OrderResult::TargetBlocked: return "Target blocked";
    case OrderResult::NoPath: return "No path";
    case OrderResult::TargetNotPlanned: return "No active plan";
    case OrderResult::TargetReserved: return "Target reserved";
    case OrderResult::TargetNotFarm: return "Not a farm";
    case OrderResult::TargetNotHarvestable: return "Farm not harvestable";
    default: return "?";
    }
}

World::World() : World(64, 64, 1) {}

World::World(int w, int h, std::uint32_t seed)
{
    reset(w, h, seed);
}

void World::reset(int w, int h, std::uint32_t seed)
{
    m_w = std::max(1, w);
    m_h = std::max(1, h);

    m_rng = std::mt19937(seed);

    m_cells.assign(static_cast<std::size_t>(m_w * m_h), {});

    // A small starting patch of floor in the center.
    const int cx = m_w / 2;
    const int cy = m_h / 2;
    for (int y = cy - 3; y <= cy + 3; ++y)
    {
        for (int x = cx - 3; x <= cx + 3; ++x)
        {
            if (!inBounds(x, y))
                continue;
            cell(x, y).built = TileType::Floor;
        }
    }

    // Seed a small starting stockpile in the middle so the new hunger system
    // always has a reachable "eat" location on fresh worlds.
    if (inBounds(cx, cy))
        cell(cx, cy).built = TileType::Stockpile;

    // Random scatter of rocks (walls) to make pathfinding visible.
    // Guard against tiny worlds: dist(1, w-2) becomes invalid when w <= 2.
    if (m_w > 2 && m_h > 2)
    {
        std::uniform_int_distribution<int> distX(1, m_w - 2);
        std::uniform_int_distribution<int> distY(1, m_h - 2);
        for (int i = 0; i < (m_w * m_h) / 60; ++i)
        {
            const int x = distX(m_rng);
            const int y = distY(m_rng);
            // Avoid the central start area.
            if (std::abs(x - cx) < 6 && std::abs(y - cy) < 6)
                continue;
            cell(x, y).built = TileType::Wall;
        }
    }

    // Random scatter of trees (forestry resource).
    if (m_w > 2 && m_h > 2)
    {
        std::uniform_int_distribution<int> distX(1, m_w - 2);
        std::uniform_int_distribution<int> distY(1, m_h - 2);

        // Slightly denser than rocks, but still sparse enough to keep navigation readable.
        for (int i = 0; i < (m_w * m_h) / 35; ++i)
        {
            const int x = distX(m_rng);
            const int y = distY(m_rng);

            // Avoid the central start area.
            if (std::abs(x - cx) < 6 && std::abs(y - cy) < 6)
                continue;

            Cell& cc = cell(x, y);
            if (cc.built != TileType::Empty)
                continue;

            cc.built = TileType::Tree;
            cc.builtFromPlan = false;
        }
    }

    // Fresh inventory.
    m_inv.wood = 60;
    m_inv.food = 20.0f;

    // Spawn a few colonists near the center.
    m_colonists.clear();
    for (int i = 0; i < 5; ++i)
    {
        Colonist c;
        c.id = i;
        c.x = static_cast<float>(cx) + 0.5f + static_cast<float>((i % 2) - 1) * 0.5f;
        c.y = static_cast<float>(cy) + 0.5f + static_cast<float>((i / 2) - 1) * 0.5f;

        // Player-control state.
        c.drafted = false;

        // Start everyone as Workers with role-default work priorities.
        c.role.set(RoleId::Worker);
        c.workPrio = DefaultWorkPriorities(c.role.role);

        // Start everyone with a full personal food reserve.
        const float maxPersonalFood = static_cast<float>(std::max(0.0, colonistMaxPersonalFood));
        c.personalFood = maxPersonalFood;

        // Clear job/path state.
        c.hasJob = false;
        c.jobKind = Colonist::JobKind::None;
        c.targetX = 0;
        c.targetY = 0;
        c.path.clear();
        c.pathIndex = 0;
        c.eatWorkRemaining = 0.0f;
        c.harvestWorkRemaining = 0.0f;

        // Hauling state.
        c.carryingWood = 0;
        c.haulPickupX = 0;
        c.haulPickupY = 0;
        c.haulDropX = 0;
        c.haulDropY = 0;
        c.haulingToDropoff = false;
        c.haulWorkRemaining = 0.0f;

        m_colonists.push_back(std::move(c));
    }

    // Build nav map.
    m_nav = colony::pf::GridMap({m_w, m_h});
    syncAllNav();

    // Build plan cache (should be empty on reset, but keep it correct even if
    // future changes introduce pre-seeded plans).
    rebuildPlannedCache();

    // Build built-count cache.
    rebuildBuiltCounts();

    // Build farm cache (for growth + harvest jobs).
    rebuildFarmCache();

    // Build loose-wood cache.
    rebuildLooseWoodCache();

    // Build room cache (indoors/outdoors).
    rebuildRooms();

    // Allow job assignment immediately after a reset.
    m_jobAssignCooldown = 0.0;
    m_harvestAssignCooldown = 0.0;
    m_haulAssignCooldown = 0.0;
    m_treeSpreadAccum = 0.0;

    // Fresh world, fresh counters.
    ResetPathStats();
}

bool World::inBounds(int x, int y) const noexcept
{
    return x >= 0 && y >= 0 && x < m_w && y < m_h;
}

const Cell& World::cell(int x, int y) const noexcept
{
    // NOTE: caller is expected to bounds-check.
    return m_cells[idx(x, y)];
}

Cell& World::cell(int x, int y) noexcept
{
    // NOTE: caller is expected to bounds-check.
    return m_cells[idx(x, y)];
}

Colonist* World::findColonistById(int colonistId) noexcept
{
    for (Colonist& c : m_colonists)
        if (c.id == colonistId)
            return &c;
    return nullptr;
}

const Colonist* World::findColonistById(int colonistId) const noexcept
{
    for (const Colonist& c : m_colonists)
        if (c.id == colonistId)
            return &c;
    return nullptr;
}

bool World::SetColonistDrafted(int colonistId, bool drafted) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return false;

    if (c->drafted == drafted)
        return true;

    c->drafted = drafted;

    // Drafting immediately stops whatever the colonist was doing.
    // Jobs will be reassigned once undrafted (or via direct orders while drafted).
    if (drafted)
        cancelJob(*c);

    return true;
}

bool World::SetColonistRole(int colonistId, RoleId role) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return false;

    if (c->role.role == role)
        return true;

    c->role.set(role);

    // Sanitize persisted fields.
    if (c->role.level < 1)
        c->role.level = 1;

    // If the colonist is currently doing a job that the new role would not
    // take autonomously, cancel it so it can be reassigned.
    if (c->hasJob && !c->drafted)
    {
        if (c->jobKind == Colonist::JobKind::BuildPlan && !HasCap(*c, Capability::Building))
            cancelJob(*c);
        else if (c->jobKind == Colonist::JobKind::Harvest && !HasCap(*c, Capability::Farming))
            cancelJob(*c);
        else if (c->jobKind == Colonist::JobKind::HaulWood && !HasCap(*c, Capability::Hauling))
            cancelJob(*c);
    }

    return true;
}

OrderResult World::CancelColonistJob(int colonistId) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;

    // "Stop" cancels the active job and clears any queued manual orders.
    c->manualQueue.clear();
    cancelJob(*c);
    return OrderResult::Ok;
}

OrderResult World::startManualMove(Colonist& c, int targetX, int targetY) noexcept
{
    if (!c.drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(targetX, targetY))
        return OrderResult::OutOfBounds;
    if (!m_nav.passable(targetX, targetY))
        return OrderResult::TargetBlocked;

    // Robustness: ensure we aren't carrying a stale job state.
    cancelJob(c);

    c.targetX = targetX;
    c.targetY = targetY;
    c.jobKind = Colonist::JobKind::ManualMove;
    c.hasJob = true;

    if (!computePathToTile(c, targetX, targetY))
    {
        cancelJob(c);
        return OrderResult::NoPath;
    }

    return OrderResult::Ok;
}

OrderResult World::startManualBuild(Colonist& c, int planX, int planY) noexcept
{
    if (!c.drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(planX, planY))
        return OrderResult::OutOfBounds;

    Cell& target = cell(planX, planY);
    if (target.planned == TileType::Empty || target.planned == target.built)
        return OrderResult::TargetNotPlanned;
    if (target.reservedBy != -1 && target.reservedBy != c.id)
        return OrderResult::TargetReserved;

    // Find a path to any adjacent tile.
    const int sx = posToTile(c.x);
    const int sy = posToTile(c.y);

    std::vector<colony::pf::IVec2> p;
    if (!computePathToAdjacentFrom(sx, sy, planX, planY, p))
        return OrderResult::NoPath;

    cancelJob(c);

    target.reservedBy = c.id;
    c.targetX = planX;
    c.targetY = planY;
    c.jobKind = Colonist::JobKind::BuildPlan;
    c.hasJob = true;
    c.path = std::move(p);
    c.pathIndex = 0;

    return OrderResult::Ok;
}

OrderResult World::startManualHarvest(Colonist& c, int farmX, int farmY) noexcept
{
    if (!c.drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(farmX, farmY))
        return OrderResult::OutOfBounds;

    Cell& farm = cell(farmX, farmY);
    if (farm.built != TileType::Farm)
        return OrderResult::TargetNotFarm;
    if (farm.farmReservedBy != -1 && farm.farmReservedBy != c.id)
        return OrderResult::TargetReserved;

    // Find a path to any adjacent tile.
    const int sx = posToTile(c.x);
    const int sy = posToTile(c.y);

    std::vector<colony::pf::IVec2> p;
    if (!computePathToAdjacentFrom(sx, sy, farmX, farmY, p))
        return OrderResult::NoPath;

    cancelJob(c);

    farm.farmReservedBy = c.id;
    c.targetX = farmX;
    c.targetY = farmY;
    c.jobKind = Colonist::JobKind::Harvest;
    c.hasJob = true;
    c.path = std::move(p);
    c.pathIndex = 0;
    c.harvestWorkRemaining = static_cast<float>(std::max(0.0, farmHarvestDurationSeconds));

    return OrderResult::Ok;
}

void World::tryStartQueuedManualOrders(Colonist& c) noexcept
{
    if (!c.drafted)
        return;
    if (c.hasJob)
        return;
    if (c.manualQueue.empty())
        return;

    // If hungry, let the eat system take over first.
    const float threshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));
    if (threshold > 0.0f && c.personalFood <= threshold)
        return;

    // Drain invalid orders from the front until we either start one or hit a soft failure.
    int guard = 0;
    while (!c.manualQueue.empty() && guard++ < 32)
    {
        const Colonist::ManualOrder& o = c.manualQueue.front();
        OrderResult r = OrderResult::Ok;

        switch (o.kind)
        {
        case Colonist::ManualOrder::Kind::Move:
            r = startManualMove(c, o.x, o.y);
            break;
        case Colonist::ManualOrder::Kind::Build:
            r = startManualBuild(c, o.x, o.y);
            break;
        case Colonist::ManualOrder::Kind::Harvest:
            r = startManualHarvest(c, o.x, o.y);
            break;
        default:
            r = OrderResult::Ok;
            break;
        }

        if (r == OrderResult::Ok)
            return; // order started (front remains the in-progress order)

        // Hard-fail cases should be dropped so the queue doesn't stall forever.
        bool drop = false;
        switch (o.kind)
        {
        case Colonist::ManualOrder::Kind::Move:
            drop = (r == OrderResult::OutOfBounds || r == OrderResult::TargetBlocked);
            break;
        case Colonist::ManualOrder::Kind::Build:
            drop = (r == OrderResult::OutOfBounds || r == OrderResult::TargetNotPlanned);
            break;
        case Colonist::ManualOrder::Kind::Harvest:
            drop = (r == OrderResult::OutOfBounds || r == OrderResult::TargetNotFarm);
            break;
        default:
            drop = true;
            break;
        }

        if (drop)
        {
            c.manualQueue.erase(c.manualQueue.begin());
            continue;
        }

        // Soft failure (reserved/no path/not ready) -> keep it and retry later.
        break;
    }
}

void World::completeQueuedManualOrder(Colonist& c) noexcept
{
    if (c.manualQueue.empty())
        return;

    const Colonist::ManualOrder& o = c.manualQueue.front();
    bool match = false;

    switch (o.kind)
    {
    case Colonist::ManualOrder::Kind::Move:
        match = (c.jobKind == Colonist::JobKind::ManualMove && c.targetX == o.x && c.targetY == o.y);
        break;
    case Colonist::ManualOrder::Kind::Build:
        match = (c.jobKind == Colonist::JobKind::BuildPlan && c.targetX == o.x && c.targetY == o.y);
        break;
    case Colonist::ManualOrder::Kind::Harvest:
        match = (c.jobKind == Colonist::JobKind::Harvest && c.targetX == o.x && c.targetY == o.y);
        break;
    default:
        match = false;
        break;
    }

    if (match)
        c.manualQueue.erase(c.manualQueue.begin());
}

OrderResult World::OrderColonistMove(int colonistId, int targetX, int targetY, bool queue) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;
    if (!c->drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(targetX, targetY))
        return OrderResult::OutOfBounds;
    if (!m_nav.passable(targetX, targetY))
        return OrderResult::TargetBlocked;

    if (queue)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Move;
        o.x = targetX;
        o.y = targetY;
        c->manualQueue.push_back(o);

        // If idle, start immediately.
        tryStartQueuedManualOrders(*c);
        return OrderResult::Ok;
    }

    // Replace any existing queue with this single order.
    c->manualQueue.clear();

    const OrderResult r = startManualMove(*c, targetX, targetY);
    if (r == OrderResult::Ok)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Move;
        o.x = targetX;
        o.y = targetY;
        c->manualQueue.push_back(o);
    }
    return r;
}

OrderResult World::OrderColonistBuild(int colonistId, int planX, int planY, bool queue) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;
    if (!c->drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(planX, planY))
        return OrderResult::OutOfBounds;

    const Cell& target = cell(planX, planY);
    if (target.planned == TileType::Empty || target.planned == target.built)
        return OrderResult::TargetNotPlanned;

    if (queue)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Build;
        o.x = planX;
        o.y = planY;
        c->manualQueue.push_back(o);

        // If idle, start immediately (may soft-fail and wait).
        tryStartQueuedManualOrders(*c);
        return OrderResult::Ok;
    }

    // Replace any existing queue with this single order.
    c->manualQueue.clear();

    const OrderResult r = startManualBuild(*c, planX, planY);
    if (r == OrderResult::Ok)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Build;
        o.x = planX;
        o.y = planY;
        c->manualQueue.push_back(o);
    }
    return r;
}

OrderResult World::OrderColonistHarvest(int colonistId, int farmX, int farmY, bool queue) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;
    if (!c->drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(farmX, farmY))
        return OrderResult::OutOfBounds;

    const Cell& farm = cell(farmX, farmY);
    if (farm.built != TileType::Farm)
        return OrderResult::TargetNotFarm;
    if (!queue && farm.farmGrowth < 1.0f)
        return OrderResult::TargetNotHarvestable;

    if (queue)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Harvest;
        o.x = farmX;
        o.y = farmY;
        c->manualQueue.push_back(o);

        // If idle, start immediately.
        tryStartQueuedManualOrders(*c);
        return OrderResult::Ok;
    }

    // Replace any existing queue with this single order.
    c->manualQueue.clear();

    const OrderResult r = startManualHarvest(*c, farmX, farmY);
    if (r == OrderResult::Ok)
    {
        Colonist::ManualOrder o;
        o.kind = Colonist::ManualOrder::Kind::Harvest;
        o.x = farmX;
        o.y = farmY;
        c->manualQueue.push_back(o);
    }
    return r;
}

PlacePlanResult World::placePlan(int x, int y, TileType plan, std::uint8_t planPriority)
{
    if (!inBounds(x, y))
        return PlacePlanResult::OutOfBounds;

    Cell& c = cell(x, y);

    // Clamp priority into the supported range (0..3).
    if (planPriority > 3u)
        planPriority = 3u;

    const bool wasActivePlan = (c.planned != TileType::Empty && c.planned != c.built);

    // Special-case: "Remove" is a deconstruction plan for existing built tiles.
    // If the tile is already empty, treat this as "erase plan" so colonists never
    // get assigned meaningless deconstruction work.
    if (plan == TileType::Remove && c.built == TileType::Empty)
    {
        if (c.planned == TileType::Empty)
            return PlacePlanResult::NoChange;

        // Refund the previous plan cost (prototype-friendly).
        m_inv.wood += TileWoodCost(c.planned);

        c.planned = TileType::Empty;
        c.planPriority = 0;
        c.workRemaining = 0.0f;
        c.reservedBy = -1;

        if (wasActivePlan)
            planCacheRemove(x, y);

        return PlacePlanResult::Ok;
    }


    // Special-case: 'Empty' means "clear any existing plan".
    // Use TileType::Remove to deconstruct existing built tiles.
    // This keeps right-drag erase from creating a meaningless (Empty) plan on already-built cells.
    if (plan == TileType::Empty)
    {
        if (c.planned == TileType::Empty)
            return PlacePlanResult::NoChange;

        // Refund the previous plan cost (prototype-friendly).
        m_inv.wood += TileWoodCost(c.planned);

        c.planned = TileType::Empty;
        c.planPriority = 0;
        c.workRemaining = 0.0f;
        c.reservedBy = -1;

        if (wasActivePlan)
            planCacheRemove(x, y);

        return PlacePlanResult::Ok;
    }

    // Treat "planning to the already-built" state as a no-op.
    const TileType oldPlan = (c.planned == TileType::Empty) ? c.built : c.planned;

    // If the plan type is unchanged, we may still want to change priority.
    if (oldPlan == plan)
    {
        // Only active plans have priority.
        if (c.planned != TileType::Empty && c.planned != c.built)
        {
            if (c.planPriority != planPriority)
            {
                c.planPriority = planPriority;
                return PlacePlanResult::Ok;
            }
        }
        return PlacePlanResult::NoChange;
    }

    // Delta-cost the plan swap, but do not refund built tiles.
    const int oldCost = TileWoodCost(c.planned);
    const int newCost = (plan == c.built) ? 0 : TileWoodCost(plan);

    const int delta = newCost - oldCost;
    if (delta > 0 && m_inv.wood < delta)
        return PlacePlanResult::NotEnoughWood;

    m_inv.wood -= std::max(0, delta);
    m_inv.wood += std::max(0, -delta);

    // Update/clear plan.
    if (plan == c.built)
    {
        c.planned = TileType::Empty;
        c.planPriority = 0;
        c.workRemaining = 0.0f;
        c.reservedBy = -1;
    }
    else
    {
        c.planned = plan;
        c.planPriority = planPriority;
        c.workRemaining = TileBuildTimeSeconds(plan);
        c.reservedBy = -1;
    }

    const bool isActivePlan = (c.planned != TileType::Empty && c.planned != c.built);
    if (wasActivePlan && !isActivePlan)
        planCacheRemove(x, y);
    else if (!wasActivePlan && isActivePlan)
        planCacheAdd(x, y);

    return PlacePlanResult::Ok;
}

void World::clearAllPlans()
{
    // Refund and clear all plans.
    //
    // Even though we keep an active-plan cache, clearing is cheap and this
    // keeps us robust if the cache ever becomes stale during experimentation.
    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            Cell& c = cell(x, y);
            if (c.planned != TileType::Empty && c.planned != c.built)
            {
                // Refund the plan cost (prototype-friendly).
                m_inv.wood += TileWoodCost(c.planned);
            }
            c.planned = TileType::Empty;
            c.planPriority = 0;
            c.workRemaining = 0.0f;
            c.reservedBy = -1;
            c.farmReservedBy = -1;
        c.looseWoodReservedBy = -1;
        }
    }

    // Clear plan cache.
    m_plannedCells.clear();
    if (!m_plannedIndex.empty())
        std::fill(m_plannedIndex.begin(), m_plannedIndex.end(), -1);

    for (Colonist& c : m_colonists)
        cancelJob(c);

    // Allow immediate assignment after clearing plans.
    m_jobAssignCooldown = 0.0;
    m_harvestAssignCooldown = 0.0;
    m_haulAssignCooldown = 0.0;
    m_treeSpreadAccum = 0.0;
}

void World::CancelAllJobsAndClearReservations() noexcept
{
    // Clear reservations first so any stale reservation markers are removed.
    for (Cell& c : m_cells)
    {
        c.reservedBy = -1;
        c.farmReservedBy = -1;
        c.looseWoodReservedBy = -1;
    }

    for (Colonist& c : m_colonists)
        cancelJob(c);

    // Allow immediate re-assignment after bulk edits (undo/redo, clear plans, load).
    m_jobAssignCooldown = 0.0;
    m_harvestAssignCooldown = 0.0;
    m_haulAssignCooldown = 0.0;
    m_treeSpreadAccum = 0.0;
}

int World::plannedCount() const noexcept
{
    return static_cast<int>(m_plannedCells.size());
}

int World::builtCount(TileType t) const noexcept
{
    const std::size_t i = static_cast<std::size_t>(t);
    if (i >= m_builtCounts.size())
        return 0;
    return m_builtCounts[i];
}


int World::roomIdAt(int x, int y) const noexcept
{
    if (!inBounds(x, y))
        return -1;

    const std::size_t i = idx(x, y);
    if (i >= m_roomIds.size())
        return -1;

    return m_roomIds[i];
}

bool World::tileIndoors(int x, int y) const noexcept
{
    const int rid = roomIdAt(x, y);
    const RoomInfo* info = roomInfoById(rid);
    return info ? info->indoors : false;
}

const World::RoomInfo* World::roomInfoById(int roomId) const noexcept
{
    if (roomId < 0)
        return nullptr;

    const std::size_t i = static_cast<std::size_t>(roomId);
    if (i >= m_rooms.size())
        return nullptr;

    return &m_rooms[i];
}

bool World::DebugSetBuiltTile(int x, int y, TileType built, bool builtFromPlan) noexcept
{
    if (!inBounds(x, y))
        return false;

    Cell& c = cell(x, y);
    const TileType oldBuilt = c.built;

    if (oldBuilt == built && c.builtFromPlan == builtFromPlan)
        return true;

    // Remove from caches that depend on built tile type.
    if (oldBuilt == TileType::Farm && built != TileType::Farm)
        farmCacheRemove(x, y);
    if (oldBuilt != TileType::Farm && built == TileType::Farm)
        farmCacheAdd(x, y);

    // Clear any active plan state for this tile.
    if (c.planned != TileType::Empty && c.planned != oldBuilt)
        planCacheRemove(x, y);

    c.planned = TileType::Empty;
    c.planPriority = 0;
    c.workRemaining = 0.0f;
    c.reservedBy = -1;

    // Update built counts + derived dirty flags.
    builtCountAdjust(oldBuilt, built);

    c.built = built;
    c.builtFromPlan = builtFromPlan;

    // Reset farm state when directly editing.
    if (built == TileType::Farm)
    {
        if (oldBuilt != TileType::Farm)
            c.farmGrowth = 0.0f;
        c.farmReservedBy = -1;
    }
    else
    {
        c.farmGrowth = 0.0f;
        c.farmReservedBy = -1;
    }

    // Clear other reservation state that might become invalid.
    c.looseWoodReservedBy = -1;

    syncNavCell(x, y);
    return true;
}

void World::DebugRebuildRoomsNow() noexcept
{
    if (m_roomsDirty)
        rebuildRooms();
}

void World::rebuildRooms() noexcept
{
    m_roomsDirty = false;

    const int w = m_w;
    const int h = m_h;

    if (w <= 0 || h <= 0)
    {
        m_roomIds.clear();
        m_rooms.clear();
        m_indoorsRoomCount = 0;
        m_indoorsTileCount = 0;
        return;
    }

    const std::size_t n = static_cast<std::size_t>(w * h);

    m_roomIds.assign(n, -1);
    m_rooms.clear();
    m_indoorsRoomCount = 0;
    m_indoorsTileCount = 0;

    std::vector<colony::pf::IVec2> stack;
    stack.reserve(256);

    std::vector<std::uint32_t> doorAdj;
    doorAdj.reserve(64);

    int nextId = 0;

    constexpr int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t flat = idx(x, y);

            if (m_roomIds[flat] != -1)
                continue;

            if (!TileIsRoomSpace(cell(x, y).built))
                continue;

            RoomInfo info;
            info.id = nextId;
            info.minX = x;
            info.maxX = x;
            info.minY = y;
            info.maxY = y;

            bool touchesBorder = (x == 0 || y == 0 || x == w - 1 || y == h - 1);
            int area = 0;
            int perimeter = 0;
            doorAdj.clear();

            stack.clear();
            stack.push_back({x, y});
            m_roomIds[flat] = nextId;

            while (!stack.empty())
            {
                const colony::pf::IVec2 p = stack.back();
                stack.pop_back();

                ++area;

                info.minX = std::min(info.minX, p.x);
                info.minY = std::min(info.minY, p.y);
                info.maxX = std::max(info.maxX, p.x);
                info.maxY = std::max(info.maxY, p.y);

                if (p.x == 0 || p.y == 0 || p.x == w - 1 || p.y == h - 1)
                    touchesBorder = true;

                for (const auto& d : kDirs)
                {
                    const int nx = p.x + d[0];
                    const int ny = p.y + d[1];

                    if (!inBounds(nx, ny))
                    {
                        // Room-space tiles cannot reach out-of-bounds unless they sit on the border,
                        // but count this edge for completeness.
                        ++perimeter;
                        continue;
                    }

                    const TileType nb = cell(nx, ny).built;
                    if (!TileIsRoomSpace(nb))
                    {
                        // Boundary edge contributes to the room perimeter.
                        ++perimeter;

                        // Track adjacent doors for room stats/inspector UI.
                        if (nb == TileType::Door)
                            doorAdj.push_back(static_cast<std::uint32_t>(idx(nx, ny)));

                        continue;
                    }

                    const std::size_t nf = idx(nx, ny);

                    if (m_roomIds[nf] != -1)
                        continue;

                    m_roomIds[nf] = nextId;
                    stack.push_back({nx, ny});
                }
            }

            info.area = area;
            info.perimeter = perimeter;

            if (!doorAdj.empty())
            {
                std::sort(doorAdj.begin(), doorAdj.end());
                doorAdj.erase(std::unique(doorAdj.begin(), doorAdj.end()), doorAdj.end());
                info.doorCount = static_cast<int>(doorAdj.size());
            }
            else
            {
                info.doorCount = 0;
            }

            info.indoors = !touchesBorder;

            m_rooms.push_back(info);

            if (info.indoors)
            {
                ++m_indoorsRoomCount;
                m_indoorsTileCount += area;
            }

            ++nextId;
        }
    }
}

int World::harvestableFarmCount() const noexcept
{
    int count = 0;
    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& c = cell(pos.x, pos.y);
        if (c.built == TileType::Farm && c.farmGrowth >= 1.0f)
            ++count;
    }
    return count;
}

void World::rebuildBuiltCounts() noexcept
{
    m_builtCounts.fill(0);
    for (const Cell& c : m_cells)
    {
        const std::size_t i = static_cast<std::size_t>(c.built);
        if (i < m_builtCounts.size())
            ++m_builtCounts[i];
    }
}

void World::builtCountAdjust(TileType oldBuilt, TileType newBuilt) noexcept
{
    const std::size_t io = static_cast<std::size_t>(oldBuilt);
    const std::size_t in = static_cast<std::size_t>(newBuilt);
    if (io < m_builtCounts.size())
        m_builtCounts[io] = std::max(0, m_builtCounts[io] - 1);
    if (in < m_builtCounts.size())
        ++m_builtCounts[in];

    // Room topology only changes when a tile transitions between open-space and a boundary.
    //
    // However, we also track room statistics that depend on boundary *type* (e.g. door counts),
    // so mark rooms dirty when doors are added/removed as well.
    if (TileIsRoomSpace(oldBuilt) != TileIsRoomSpace(newBuilt) ||
        oldBuilt == TileType::Door || newBuilt == TileType::Door)
    {
        m_roomsDirty = true;
    }
}

void World::tick(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;

    const float dtF = static_cast<float>(dtSeconds);

    // -----------------------------------------------------------------
    // Farming (prototype)
    // -----------------------------------------------------------------
    // Farms grow over time. Once fully grown (farmGrowth == 1), a colonist can
    // harvest the farm to produce food (see stepHarvestIfReady).
    {
        const float growDur  = static_cast<float>(std::max(0.0, farmGrowDurationSeconds));
        const float growStep = (growDur > 0.0001f) ? (dtF / growDur) : 1.0f;

        for (const colony::pf::IVec2& p : m_farmCells)
        {
            Cell& c = cell(p.x, p.y);
            if (c.built != TileType::Farm)
                continue;

            if (c.farmGrowth < 1.0f)
                c.farmGrowth = clampf(c.farmGrowth + growStep, 0.0f, 1.0f);
            else
                c.farmGrowth = 1.0f;
        }
    }

    
    // -----------------------------------------------------------------
    // Forestry (prototype): tree spread/regrowth
    // -----------------------------------------------------------------
    {
        const double rate   = std::max(0.0, treeSpreadAttemptsPerSecond);
        const double chance = std::clamp(treeSpreadChancePerAttempt, 0.0, 1.0);

        if (rate <= 0.0 || chance <= 0.0 || m_w <= 2 || m_h <= 2)
        {
            // Avoid unbounded accumulation while disabled.
            m_treeSpreadAccum = 0.0;
        }
        else
        {
            // Keep forests from completely overtaking the world.
            const int maxTrees = std::max(0, (m_w * m_h) / 5);
            if (maxTrees == 0 || builtCount(TileType::Tree) < maxTrees)
            {
                m_treeSpreadAccum += dtSeconds * rate;
                int attempts = static_cast<int>(m_treeSpreadAccum);

                // Prevent pathological hitches on big dt spikes.
                attempts = std::min(attempts, 256);

                if (attempts > 0)
                {
                    m_treeSpreadAccum -= static_cast<double>(attempts);

                    std::uniform_int_distribution<int> distX(1, m_w - 2);
                    std::uniform_int_distribution<int> distY(1, m_h - 2);
                    std::uniform_real_distribution<double> dist01(0.0, 1.0);

                    for (int i = 0; i < attempts; ++i)
                    {
                        const int x = distX(m_rng);
                        const int y = distY(m_rng);

                        Cell& t = cell(x, y);

                        if (t.built != TileType::Empty)
                            continue;
                        if (t.planned != TileType::Empty)
                            continue;
                        if (t.looseWood > 0)
                            continue;

                        // Don't grow a tree on top of a colonist.
                        bool occupied = false;
                        for (const Colonist& col : m_colonists)
                        {
                            const int cx = static_cast<int>(std::floor(col.x));
                            const int cy = static_cast<int>(std::floor(col.y));
                            if (cx == x && cy == y)
                            {
                                occupied = true;
                                break;
                            }
                        }
                        if (occupied)
                            continue;

                        // Require adjacency to an existing tree.
                        bool adjacentTree = false;
                        for (int dy = -1; dy <= 1 && !adjacentTree; ++dy)
                        {
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                if (dx == 0 && dy == 0)
                                    continue;

                                const int nx = x + dx;
                                const int ny = y + dy;

                                if (!inBounds(nx, ny))
                                    continue;

                                if (cell(nx, ny).built == TileType::Tree)
                                {
                                    adjacentTree = true;
                                    break;
                                }
                            }
                        }
                        if (!adjacentTree)
                            continue;

                        if (dist01(m_rng) > chance)
                            continue;

                        // Grow a new tree.
                        t.built = TileType::Tree;
                        t.builtFromPlan = false;
                        t.farmGrowth = 0.0f;
                        t.reservedBy = -1;
                        t.farmReservedBy = -1;

                        builtCountAdjust(TileType::Empty, TileType::Tree);
                        syncNavCell(x, y);
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // Needs: personal hunger
    // -----------------------------------------------------------------
    const float eatRate = static_cast<float>(std::max(0.0, foodPerColonistPerSecond));

    for (Colonist& c : m_colonists)
        c.personalFood = std::max(0.0f, c.personalFood - eatRate * dtF);

    // If a colonist is getting hungry, preempt non-eat jobs so they can
    // go look for food.
    const float eatThreshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));
    if (eatThreshold > 0.0f)
    {
        for (Colonist& c : m_colonists)
        {
            if (c.personalFood > eatThreshold || !c.hasJob)
                continue;
            if (c.jobKind == Colonist::JobKind::BuildPlan || c.jobKind == Colonist::JobKind::ManualMove || c.jobKind == Colonist::JobKind::HaulWood)
                cancelJob(c);
        }
    }

    // -----------------------------------------------------------------
    // Job assignment (hungry first, then harvesting, then construction)
    // -----------------------------------------------------------------
    assignEatJobs(dtSeconds);

// Drafted colonists with queued manual orders should claim targets before the
// autonomous job assignment runs.
for (Colonist& c : m_colonists)
    tryStartQueuedManualOrders(c);

assignHarvestJobs(dtSeconds);

    assignJobs(dtSeconds);
    assignHaulJobs(dtSeconds);

    for (Colonist& c : m_colonists)
    {
        stepColonist(c, dtSeconds);
        stepConstructionIfReady(c, dtSeconds);
        stepHarvestIfReady(c, dtSeconds);
        stepHaulIfReady(c, dtSeconds);
        stepEatingIfReady(c, dtSeconds);
    }

    if (m_roomsDirty)
        rebuildRooms();
}

void World::syncNavCell(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;

    const TileType built = cell(x, y).built;

    // NOTE: passable is an int in the public API.
    m_nav.set_walkable(x, y, TileIsWalkable(built) ? 1 : 0);

    const float cost = navUseTerrainCosts ? TileNavCost(built) : 1.0f;
    m_nav.set_tile_cost(x, y, cost);

    // Any local nav edit invalidates the cached stockpile distance field.
    m_stockpileFieldDirty = true;
    m_stockpileFieldCachedStamp = 0u;

    // Any local nav edit invalidates the cached food distance field.
    m_foodFieldDirty = true;
    m_foodFieldCachedStamp = 0u;
}

void World::syncAllNav() noexcept
{
    for (int y = 0; y < m_h; ++y)
        for (int x = 0; x < m_w; ++x)
            syncNavCell(x, y);

    // A full nav rebuild invalidates any cached paths (even if the topology is the
    // same, traversal costs might have changed).
    ClearPathCache();
}

void World::ClearPathCache() noexcept
{
    m_pathCache.clear();
    m_pathCacheLru.clear();
}

void World::ResetPathStats() noexcept
{
    m_pathStats = {};
}

std::size_t World::pathCacheSize() const noexcept
{
    return m_pathCache.size();
}

World::PathfindStats World::pathStats() const noexcept
{
    return m_pathStats;
}

bool World::SetNavTerrainCostsEnabled(bool enabled) noexcept
{
    if (navUseTerrainCosts == enabled)
        return false;

    navUseTerrainCosts = enabled;
    syncAllNav(); // also clears the path cache
    return true;
}

bool World::SetPathAlgo(PathAlgo algo) noexcept
{
    if (pathAlgo == algo)
        return false;

    pathAlgo = algo;
    ClearPathCache();
    return true;
}

bool World::SetPathCacheEnabled(bool enabled) noexcept
{
    if (pathCacheEnabled == enabled)
        return false;

    pathCacheEnabled = enabled;
    if (!pathCacheEnabled)
        ClearPathCache();
    return true;
}

bool World::SetPathCacheMaxEntries(int maxEntries) noexcept
{
    maxEntries = clampi(maxEntries, 0, 16384);
    if (pathCacheMaxEntries == maxEntries)
        return false;

    pathCacheMaxEntries = maxEntries;

    if (pathCacheMaxEntries <= 0)
    {
        ClearPathCache();
        return true;
    }

    while (m_pathCache.size() > static_cast<std::size_t>(pathCacheMaxEntries) && !m_pathCacheLru.empty())
    {
        const PathCacheKey oldKey = m_pathCacheLru.back();
        m_pathCacheLru.pop_back();
        (void)m_pathCache.erase(oldKey);
        ++m_pathStats.evicted;
    }
    return true;
}

bool World::findPathToNearestAvailablePlan(int startX, int startY,
                                          int& outPlanX, int& outPlanY,
                                          std::vector<colony::pf::IVec2>& outPath,
                                          int requiredPriority) const
{
    outPlanX = -1;
    outPlanY = -1;
    outPath.clear();

    if (m_plannedCells.empty())
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    // Helper: does this walkable "work tile" touch an active, unreserved plan?
    auto findAdjacentPlan = [&](int wx, int wy, int& planX, int& planY) -> bool
    {
        constexpr int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
        for (const auto& d : kDirs)
        {
            const int px = wx + d[0];
            const int py = wy + d[1];
            if (!inBounds(px, py))
                continue;

            const Cell& c = cell(px, py);
            if (c.planned == TileType::Empty || c.planned == c.built)
                continue;
            if (requiredPriority >= 0 && static_cast<int>(c.planPriority) != requiredPriority)
                continue;
            if (c.reservedBy != -1)
                continue;

            planX = px;
            planY = py;
            return true;
        }
        return false;
    };

    // Dijkstra to the nearest work tile adjacent to any available plan.
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const std::size_t n  = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_nearestStamp[id] == stamp) ? m_nearestDist[id] : kInf;
    };

    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId p) {
        m_nearestStamp[id]  = stamp;
        m_nearestDist[id]   = d;
        m_nearestParent[id] = p;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator<(const QN& o) const { return d > o.d; } // min-heap
    };

    std::priority_queue<QN> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);

        int planX = -1;
        int planY = -1;
        if (findAdjacentPlan(C.x, C.y, planX, planY))
        {
            outPlanX = planX;
            outPlanY = planY;

            // Reconstruct path: start -> current
            std::vector<colony::pf::IVec2> rev;
            colony::pf::NodeId t = cur.id;
            while (t != colony::pf::kInvalid)
            {
                if (m_nearestStamp[t] != stamp)
                    break;

                rev.push_back(colony::pf::from_id(t, w));
                if (t == sid)
                    break;
                t = m_nearestParent[t];
            }

            if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
                return false;

            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return !outPath.empty();
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    return false;
}

std::uint32_t World::buildPlanField(int requiredPriority) const
{
    // Multi-source Dijkstra: start from *all* walkable tiles adjacent to any
    // unreserved plan matching requiredPriority.
    //
    // This lets assignJobs() find nearest plans for many colonists with a
    // single Dijkstra instead of one per colonist.

    if (m_plannedCells.empty())
        return 0u;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return 0u;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    const std::size_t n = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_planFieldDist.size() != n)
    {
        m_planFieldDist.assign(n, 0.0f);
        m_planFieldParent.assign(n, colony::pf::kInvalid);
        m_planFieldStamp.assign(n, 0u);
        m_planFieldPlanKey.assign(n, 0u);
        m_planFieldStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_planFieldStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_planFieldStamp.begin(), m_planFieldStamp.end(), 0u);
        stamp = 1u;
    }
    m_planFieldStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_planFieldStamp[id] == stamp) ? m_planFieldDist[id] : kInf;
    };

    auto getPlanKey = [&](colony::pf::NodeId id) -> std::uint64_t {
        return (m_planFieldStamp[id] == stamp) ? m_planFieldPlanKey[id] : std::numeric_limits<std::uint64_t>::max();
    };

    auto setNode = [&](colony::pf::NodeId id,
                       float d,
                       colony::pf::NodeId parent,
                       std::uint64_t planKey)
    {
        m_planFieldStamp[id]   = stamp;
        m_planFieldDist[id]    = d;
        m_planFieldParent[id]  = parent;
        m_planFieldPlanKey[id] = planKey;
    };

    struct QN {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    std::uint64_t sourcesAdded = 0;

    auto tryAddSource = [&](int wx, int wy, int planX, int planY)
    {
        if (!inBounds(wx, wy) || !m_nav.passable(wx, wy))
            return;

        const colony::pf::NodeId wid = colony::pf::to_id(wx, wy, w);
        const std::uint64_t pkey = PackPlanKey(planX, planY);

        const float oldD = getDist(wid);
        const std::uint64_t oldKey = getPlanKey(wid);

        // Keep the closest source; break ties deterministically by plan key (Y-major).
        if (0.0f < oldD || (oldD == 0.0f && pkey < oldKey))
        {
            setNode(wid, 0.0f, colony::pf::kInvalid, pkey);
            open.push({0.0f, wid});
            ++sourcesAdded;
        }
    };

    // Seed sources.
    constexpr int kAdj4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (const auto& pos : m_plannedCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& c = cell(pos.x, pos.y);
        if (c.planned == TileType::Empty || c.planned == c.built)
            continue;
        if (requiredPriority >= 0 && static_cast<int>(c.planPriority) != requiredPriority)
            continue;
        if (c.reservedBy != -1)
            continue;

        for (const auto& d : kAdj4)
        {
            const int wx = pos.x + d[0];
            const int wy = pos.y + d[1];
            tryAddSource(wx, wy, pos.x, pos.y);
        }
    }

    if (open.empty())
        return 0u;

    ++m_pathStats.buildFieldComputed;
    m_pathStats.buildFieldSources += sourcesAdded;

    // Classic Dijkstra expansion.
    constexpr int DIRS = 8;
    static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d != getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const std::uint64_t curKey = m_planFieldPlanKey[cur.id];

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            const float oldD = getDist(nid);
            if (nd < oldD)
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
            else if (nd == oldD && curKey < getPlanKey(nid))
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
        }
    }

    return stamp;
}

bool World::queryPlanField(std::uint32_t stamp,
                           int startX, int startY,
                           int& outPlanX, int& outPlanY,
                           std::vector<colony::pf::IVec2>& outPath) const
{
    outPlanX = -1;
    outPlanY = -1;
    outPath.clear();

    if (stamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    if (static_cast<std::size_t>(sid) >= n)
        return false;

    if (m_planFieldStamp.size() != n || m_planFieldStamp[sid] != stamp)
        return false;

    const std::uint64_t pkey = m_planFieldPlanKey[sid];
    outPlanX = UnpackPlanX(pkey);
    outPlanY = UnpackPlanY(pkey);

    colony::pf::NodeId t = sid;
    while (t != colony::pf::kInvalid)
    {
        if (m_planFieldStamp[t] != stamp)
            break;
        outPath.push_back(colony::pf::from_id(t, w));
        t = m_planFieldParent[t];
    }

    if (outPath.empty() || outPath.front().x != startX || outPath.front().y != startY)
        return false;

    return true;
}

std::uint32_t World::buildStockpileField() const
{
    // If there are no stockpiles, the hauling system cannot route to a dropoff.
    if (builtCount(TileType::Stockpile) <= 0)
    {
        m_stockpileFieldDirty = false;
        m_stockpileFieldCachedStamp = 0;
        return 0u;
    }

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return 0u;

    const std::size_t n = static_cast<std::size_t>(w * h);

    // Reuse the last computed field when nothing relevant has changed.
    if (!m_stockpileFieldDirty && m_stockpileFieldCachedStamp != 0u &&
        m_stockpileFieldStamp.size() == n)
    {
        return m_stockpileFieldCachedStamp;
    }

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_stockpileFieldDist.size() != n)
    {
        m_stockpileFieldDist.assign(n, 0.0f);
        m_stockpileFieldParent.assign(n, colony::pf::kInvalid);
        m_stockpileFieldStamp.assign(n, 0u);
        m_stockpileFieldStockpileKey.assign(n, std::numeric_limits<std::uint64_t>::max());
        m_stockpileFieldStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_stockpileFieldStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_stockpileFieldStamp.begin(), m_stockpileFieldStamp.end(), 0u);
        stamp = 1u;
    }
    m_stockpileFieldStampValue = stamp;

    constexpr float kInf = std::numeric_limits<float>::infinity();

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_stockpileFieldStamp[id] == stamp) ? m_stockpileFieldDist[id] : kInf;
    };
    auto getKey = [&](colony::pf::NodeId id) -> std::uint64_t {
        return (m_stockpileFieldStamp[id] == stamp) ? m_stockpileFieldStockpileKey[id]
                                                    : std::numeric_limits<std::uint64_t>::max();
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent, std::uint64_t skey) {
        m_stockpileFieldStamp[id] = stamp;
        m_stockpileFieldDist[id] = d;
        m_stockpileFieldParent[id] = parent;
        m_stockpileFieldStockpileKey[id] = skey;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    std::uint64_t sourcesAdded = 0;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Cell& c = cell(x, y);
            if (c.built != TileType::Stockpile)
                continue;

            if (!m_nav.passable(x, y))
                continue;

            const colony::pf::NodeId id = colony::pf::to_id(x, y, w);
            const std::uint64_t skey = PackPlanKey(x, y);

            const float oldD = getDist(id);
            const std::uint64_t oldKey = getKey(id);

            // If this tile is already a source, keep the lowest key for determinism.
            if (0.0f < oldD || (oldD == 0.0f && skey < oldKey))
            {
                setNode(id, 0.0f, colony::pf::kInvalid, skey);
                open.push({0.0f, id});
                ++sourcesAdded;
            }
        }
    }

    if (open.empty())
    {
        m_stockpileFieldDirty = false;
        m_stockpileFieldCachedStamp = 0;
        return 0u;
    }

    ++m_pathStats.haulStockpileFieldComputed;
    m_pathStats.haulStockpileFieldSources += sourcesAdded;

    constexpr int DIRS = 8;
    static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const std::uint64_t curKey = m_stockpileFieldStockpileKey[cur.id];

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            const float oldD = getDist(nid);
            if (nd < oldD)
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
            else if (nd == oldD && curKey < getKey(nid))
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
        }
    }

    m_stockpileFieldDirty = false;
    m_stockpileFieldCachedStamp = stamp;
    return stamp;
}

bool World::queryStockpileField(std::uint32_t stamp,
                               int startX, int startY,
                               int& outStockX, int& outStockY,
                               std::vector<colony::pf::IVec2>& outPath) const
{
    outStockX = -1;
    outStockY = -1;
    outPath.clear();

    if (stamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    if (static_cast<std::size_t>(sid) >= n)
        return false;

    if (m_stockpileFieldStamp.size() != n || m_stockpileFieldStamp[sid] != stamp)
        return false;

    const std::uint64_t skey = m_stockpileFieldStockpileKey[sid];
    outStockX = UnpackPlanX(skey);
    outStockY = UnpackPlanY(skey);

    colony::pf::NodeId t = sid;
    while (t != colony::pf::kInvalid)
    {
        if (m_stockpileFieldStamp[t] != stamp)
            break;
        outPath.push_back(colony::pf::from_id(t, w));
        t = m_stockpileFieldParent[t];
    }

    if (outPath.empty() || outPath.front().x != startX || outPath.front().y != startY)
        return false;

    ++m_pathStats.haulStockpileFieldUsed;
    return true;
}

float World::stockpileFieldDistAt(std::uint32_t stamp, int x, int y) const noexcept
{
    constexpr float kInf = std::numeric_limits<float>::infinity();

    if (stamp == 0u)
        return kInf;

    if (!inBounds(x, y))
        return kInf;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return kInf;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId id = colony::pf::to_id(x, y, w);
    if (static_cast<std::size_t>(id) >= n)
        return kInf;

    if (m_stockpileFieldStamp.size() != n || m_stockpileFieldStamp[id] != stamp)
        return kInf;

    return m_stockpileFieldDist[id];
}

std::uint32_t World::buildHaulPickupField(std::uint32_t stockpileStamp) const
{
    if (stockpileStamp == 0u)
        return 0u;

    if (m_looseWoodCells.empty() || m_looseWoodTotal <= 0)
        return 0u;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return 0u;

    const std::size_t n = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_haulFieldDist.size() != n)
    {
        m_haulFieldDist.assign(n, 0.0f);
        m_haulFieldParent.assign(n, colony::pf::kInvalid);
        m_haulFieldStamp.assign(n, 0u);
        m_haulFieldWoodKey.assign(n, std::numeric_limits<std::uint64_t>::max());
        m_haulFieldStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_haulFieldStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_haulFieldStamp.begin(), m_haulFieldStamp.end(), 0u);
        stamp = 1u;
    }
    m_haulFieldStampValue = stamp;

    constexpr float kInf = std::numeric_limits<float>::infinity();

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_haulFieldStamp[id] == stamp) ? m_haulFieldDist[id] : kInf;
    };
    auto getKey = [&](colony::pf::NodeId id) -> std::uint64_t {
        return (m_haulFieldStamp[id] == stamp) ? m_haulFieldWoodKey[id]
                                               : std::numeric_limits<std::uint64_t>::max();
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent, std::uint64_t wkey) {
        m_haulFieldStamp[id] = stamp;
        m_haulFieldDist[id] = d;
        m_haulFieldParent[id] = parent;
        m_haulFieldWoodKey[id] = wkey;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    std::uint64_t sourcesAdded = 0;

    // Sources: all unreserved loose-wood tiles that can reach a stockpile.
    for (const auto& pos : m_looseWoodCells)
    {
        const int x = pos.x;
        const int y = pos.y;
        if (!inBounds(x, y))
            continue;

        const Cell& c = cell(x, y);
        if (c.looseWood <= 0)
            continue;
        if (c.looseWoodReservedBy != -1)
            continue;
        if (!m_nav.passable(x, y))
            continue;

        const float dropDist = stockpileFieldDistAt(stockpileStamp, x, y);
        if (!std::isfinite(dropDist))
            continue; // unreachable to any stockpile

        const colony::pf::NodeId id = colony::pf::to_id(x, y, w);
        const std::uint64_t wkey = PackPlanKey(x, y);

        const float oldD = getDist(id);
        const std::uint64_t oldKey = getKey(id);

        if (dropDist < oldD || (dropDist == oldD && wkey < oldKey))
        {
            setNode(id, dropDist, colony::pf::kInvalid, wkey);
            open.push({dropDist, id});
            ++sourcesAdded;
        }
    }

    if (open.empty())
        return 0u;

    ++m_pathStats.haulPickupFieldComputed;
    m_pathStats.haulPickupFieldSources += sourcesAdded;

    constexpr int DIRS = 8;
    static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const std::uint64_t curKey = m_haulFieldWoodKey[cur.id];

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            const float oldD = getDist(nid);
            if (nd < oldD)
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
            else if (nd == oldD && curKey < getKey(nid))
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
        }
    }

    return stamp;
}

bool World::queryHaulPickupField(std::uint32_t haulStamp,
                                int startX, int startY,
                                int& outWoodX, int& outWoodY,
                                std::vector<colony::pf::IVec2>& outPath) const
{
    outWoodX = -1;
    outWoodY = -1;
    outPath.clear();

    if (haulStamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    if (static_cast<std::size_t>(sid) >= n)
        return false;

    if (m_haulFieldStamp.size() != n || m_haulFieldStamp[sid] != haulStamp)
        return false;

    const std::uint64_t wkey = m_haulFieldWoodKey[sid];
    outWoodX = UnpackPlanX(wkey);
    outWoodY = UnpackPlanY(wkey);

    colony::pf::NodeId t = sid;
    while (t != colony::pf::kInvalid)
    {
        if (m_haulFieldStamp[t] != haulStamp)
            break;
        outPath.push_back(colony::pf::from_id(t, w));
        t = m_haulFieldParent[t];
    }

    if (outPath.empty() || outPath.front().x != startX || outPath.front().y != startY)
        return false;

    return true;
}

std::uint32_t World::buildFoodField() const
{
    // Cached multi-source Dijkstra: start from *all* walkable tiles adjacent to
    // any built food source (Stockpile/Farm).
    //
    // This accelerates assignEatJobs() by replacing per-colonist Dijkstra searches
    // with a single shared distance field (reused until the navigation grid changes).

    if (builtCount(TileType::Stockpile) <= 0 && builtCount(TileType::Farm) <= 0)
    {
        m_foodFieldDirty = false;
        m_foodFieldCachedStamp = 0u;
        return 0u;
    }

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return 0u;

    const std::size_t n = static_cast<std::size_t>(w * h);

    // Reuse the last computed field when nothing relevant has changed.
    if (!m_foodFieldDirty && m_foodFieldCachedStamp != 0u && m_foodFieldStamp.size() == n)
        return m_foodFieldCachedStamp;

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_foodFieldDist.size() != n)
    {
        m_foodFieldDist.assign(n, 0.0f);
        m_foodFieldParent.assign(n, colony::pf::kInvalid);
        m_foodFieldStamp.assign(n, 0u);
        m_foodFieldFoodKey.assign(n, std::numeric_limits<std::uint64_t>::max());
        m_foodFieldStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_foodFieldStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_foodFieldStamp.begin(), m_foodFieldStamp.end(), 0u);
        stamp = 1u;
    }
    m_foodFieldStampValue = stamp;

    constexpr float kInf = std::numeric_limits<float>::infinity();

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_foodFieldStamp[id] == stamp) ? m_foodFieldDist[id] : kInf;
    };
    auto getKey = [&](colony::pf::NodeId id) -> std::uint64_t {
        return (m_foodFieldStamp[id] == stamp) ? m_foodFieldFoodKey[id]
                                               : std::numeric_limits<std::uint64_t>::max();
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent, std::uint64_t fkey) {
        m_foodFieldStamp[id] = stamp;
        m_foodFieldDist[id] = d;
        m_foodFieldParent[id] = parent;
        m_foodFieldFoodKey[id] = fkey;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    std::uint64_t sourcesAdded = 0;

    auto tryAddSource = [&](int wx, int wy, int foodX, int foodY, bool isStockpile) {
        if (!inBounds(wx, wy) || !m_nav.passable(wx, wy))
            return;

        const colony::pf::NodeId id = colony::pf::to_id(wx, wy, w);
        const std::uint64_t fkey = PackFoodKey(foodX, foodY, isStockpile);

        const float oldD = getDist(id);
        const std::uint64_t oldKey = getKey(id);

        // Keep the closest source; break ties deterministically by key,
        // with Stockpiles preferred over Farms (see PackFoodKey).
        if (0.0f < oldD || (oldD == 0.0f && fkey < oldKey))
        {
            setNode(id, 0.0f, colony::pf::kInvalid, fkey);
            open.push({0.0f, id});
            ++sourcesAdded;
        }
    };

    // Seed sources: any walkable tile adjacent to a built food source.
    constexpr int kAdj4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const TileType b = cell(x, y).built;
            if (b != TileType::Stockpile && b != TileType::Farm)
                continue;

            const bool isStockpile = (b == TileType::Stockpile);

            for (const auto& d : kAdj4)
            {
                const int wx = x + d[0];
                const int wy = y + d[1];
                tryAddSource(wx, wy, x, y, isStockpile);
            }
        }
    }

    if (open.empty())
    {
        m_foodFieldDirty = false;
        m_foodFieldCachedStamp = 0u;
        return 0u;
    }

    ++m_pathStats.eatFieldComputed;
    m_pathStats.eatFieldSources += sourcesAdded;

    // Classic Dijkstra expansion.
    constexpr int DIRS = 8;
    static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const std::uint64_t curKey = m_foodFieldFoodKey[cur.id];

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            const float oldD = getDist(nid);
            if (nd < oldD)
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
            else if (nd == oldD && curKey < getKey(nid))
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
        }
    }

    m_foodFieldDirty = false;
    m_foodFieldCachedStamp = stamp;
    return stamp;
}

bool World::queryFoodField(std::uint32_t stamp,
                           int startX, int startY,
                           int& outFoodX, int& outFoodY,
                           std::vector<colony::pf::IVec2>& outPath) const
{
    outFoodX = -1;
    outFoodY = -1;
    outPath.clear();

    if (stamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    if (static_cast<std::size_t>(sid) >= n)
        return false;

    if (m_foodFieldStamp.size() != n || m_foodFieldStamp[sid] != stamp)
        return false;

    const std::uint64_t fkey = m_foodFieldFoodKey[sid];
    outFoodX = UnpackFoodX(fkey);
    outFoodY = UnpackFoodY(fkey);

    colony::pf::NodeId t = sid;
    while (t != colony::pf::kInvalid)
    {
        if (m_foodFieldStamp[t] != stamp)
            break;
        outPath.push_back(colony::pf::from_id(t, w));
        t = m_foodFieldParent[t];
    }

    if (outPath.empty() || outPath.front().x != startX || outPath.front().y != startY)
        return false;

    return true;
}

std::uint32_t World::buildHarvestField() const
{
    // Multi-source Dijkstra: start from *all* walkable tiles adjacent to any
    // unreserved harvestable farm.
    //
    // This accelerates assignHarvestJobs() by replacing per-colonist Dijkstra searches
    // with a single shared distance field.

    if (m_farmCells.empty())
        return 0u;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return 0u;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    const std::size_t n = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_harvestFieldDist.size() != n)
    {
        m_harvestFieldDist.assign(n, 0.0f);
        m_harvestFieldParent.assign(n, colony::pf::kInvalid);
        m_harvestFieldStamp.assign(n, 0u);
        m_harvestFieldFarmKey.assign(n, std::numeric_limits<std::uint64_t>::max());
        m_harvestFieldStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_harvestFieldStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_harvestFieldStamp.begin(), m_harvestFieldStamp.end(), 0u);
        stamp = 1u;
    }
    m_harvestFieldStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_harvestFieldStamp[id] == stamp) ? m_harvestFieldDist[id] : kInf;
    };
    auto getKey = [&](colony::pf::NodeId id) -> std::uint64_t {
        return (m_harvestFieldStamp[id] == stamp) ? m_harvestFieldFarmKey[id]
                                                  : std::numeric_limits<std::uint64_t>::max();
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent, std::uint64_t farmKey) {
        m_harvestFieldStamp[id] = stamp;
        m_harvestFieldDist[id] = d;
        m_harvestFieldParent[id] = parent;
        m_harvestFieldFarmKey[id] = farmKey;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    std::uint64_t sourcesAdded = 0;

    auto tryAddSource = [&](int wx, int wy, int farmX, int farmY) {
        if (!inBounds(wx, wy) || !m_nav.passable(wx, wy))
            return;

        const colony::pf::NodeId id = colony::pf::to_id(wx, wy, w);
        const std::uint64_t fkey = PackPlanKey(farmX, farmY);

        const float oldD = getDist(id);
        const std::uint64_t oldKey = getKey(id);

        // Keep the closest source; break ties deterministically by farm key (Y-major).
        if (0.0f < oldD || (oldD == 0.0f && fkey < oldKey))
        {
            setNode(id, 0.0f, colony::pf::kInvalid, fkey);
            open.push({0.0f, id});
            ++sourcesAdded;
        }
    };

    // Seed sources.
    constexpr int kAdj4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& c = cell(pos.x, pos.y);
        if (c.built != TileType::Farm)
            continue;
        if (c.farmGrowth < 1.0f)
            continue;
        if (c.farmReservedBy != -1)
            continue;

        for (const auto& d : kAdj4)
        {
            const int wx = pos.x + d[0];
            const int wy = pos.y + d[1];
            tryAddSource(wx, wy, pos.x, pos.y);
        }
    }

    if (open.empty())
        return 0u;

    ++m_pathStats.harvestFieldComputed;
    m_pathStats.harvestFieldSources += sourcesAdded;

    // Classic Dijkstra expansion.
    constexpr int DIRS = 8;
    static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const std::uint64_t curKey = m_harvestFieldFarmKey[cur.id];

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            const float oldD = getDist(nid);
            if (nd < oldD)
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
            else if (nd == oldD && curKey < getKey(nid))
            {
                setNode(nid, nd, cur.id, curKey);
                open.push({nd, nid});
            }
        }
    }

    return stamp;
}

bool World::queryHarvestField(std::uint32_t stamp,
                              int startX, int startY,
                              int& outFarmX, int& outFarmY,
                              std::vector<colony::pf::IVec2>& outPath) const
{
    outFarmX = -1;
    outFarmY = -1;
    outPath.clear();

    if (stamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    if (static_cast<std::size_t>(sid) >= n)
        return false;

    if (m_harvestFieldStamp.size() != n || m_harvestFieldStamp[sid] != stamp)
        return false;

    const std::uint64_t fkey = m_harvestFieldFarmKey[sid];
    outFarmX = UnpackPlanX(fkey);
    outFarmY = UnpackPlanY(fkey);

    colony::pf::NodeId t = sid;
    while (t != colony::pf::kInvalid)
    {
        if (m_harvestFieldStamp[t] != stamp)
            break;
        outPath.push_back(colony::pf::from_id(t, w));
        t = m_harvestFieldParent[t];
    }

    if (outPath.empty() || outPath.front().x != startX || outPath.front().y != startY)
        return false;

    return true;
}

bool World::findPathToNearestFoodSource(int startX, int startY,
                                       int& outFoodX, int& outFoodY,
                                       std::vector<colony::pf::IVec2>& outPath) const
{
    outFoodX = -1;
    outFoodY = -1;
    outPath.clear();

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    // Quick reject: if there are no food sources at all, don't do a full search.
    if (builtCount(TileType::Stockpile) == 0 && builtCount(TileType::Farm) == 0)
        return false;

    // Helper: does this walkable "work tile" touch a built food source?
    auto findAdjacentFood = [&](int wx, int wy, int& foodX, int& foodY) -> bool
    {
        constexpr int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

        // Prefer stockpiles if multiple sources are adjacent.
        for (const auto& d : kDirs)
        {
            const int px = wx + d[0];
            const int py = wy + d[1];
            if (!inBounds(px, py))
                continue;

            const TileType b = cell(px, py).built;
            if (b == TileType::Stockpile)
            {
                foodX = px;
                foodY = py;
                return true;
            }
        }

        for (const auto& d : kDirs)
        {
            const int px = wx + d[0];
            const int py = wy + d[1];
            if (!inBounds(px, py))
                continue;

            const TileType b = cell(px, py).built;
            if (b == TileType::Farm)
            {
                foodX = px;
                foodY = py;
                return true;
            }
        }

        return false;
    };

    // Dijkstra to the nearest work tile adjacent to any food source.
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const std::size_t n  = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        return (m_nearestStamp[id] == stamp) ? m_nearestDist[id] : kInf;
    };

    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId p) {
        m_nearestStamp[id]  = stamp;
        m_nearestDist[id]   = d;
        m_nearestParent[id] = p;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator<(const QN& o) const { return d > o.d; } // min-heap
    };

    std::priority_queue<QN> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        if (cur.d > getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);

        int foodX = -1;
        int foodY = -1;
        if (findAdjacentFood(C.x, C.y, foodX, foodY))
        {
            outFoodX = foodX;
            outFoodY = foodY;

            // Reconstruct path: start -> current
            std::vector<colony::pf::IVec2> rev;
            colony::pf::NodeId t = cur.id;
            while (t != colony::pf::kInvalid)
            {
                if (m_nearestStamp[t] != stamp)
                    break;

                rev.push_back(colony::pf::from_id(t, w));
                if (t == sid)
                    break;
                t = m_nearestParent[t];
            }

            if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
                return false;

            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return !outPath.empty();
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    return false;

}

bool World::findPathToNearestHarvestableFarm(int startX, int startY,
                                            int& outFarmX, int& outFarmY,
                                            std::vector<colony::pf::IVec2>& outPath) const
{
    outFarmX = -1;
    outFarmY = -1;
    outPath.clear();

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    // Quick reject: if there are no harvestable farms at all, don't do a full search.
    bool anyHarvestable = false;
    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& c = cell(pos.x, pos.y);
        if (c.built == TileType::Farm && c.farmGrowth >= 1.0f && c.farmReservedBy == -1)
        {
            anyHarvestable = true;
            break;
        }
    }

    if (!anyHarvestable)
        return false;

    // Helper: does this walkable "work tile" touch a harvestable farm?
    auto findAdjacentHarvestableFarm = [&](int wx, int wy, int& farmX, int& farmY) -> bool {
        constexpr int DIRS = 4;
        static const int dx[DIRS] = { 1, -1, 0, 0 };
        static const int dy[DIRS] = { 0,  0, 1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            const int px = wx + dx[dir];
            const int py = wy + dy[dir];
            if (!inBounds(px, py))
                continue;

            const Cell& c = cell(px, py);
            if (c.built == TileType::Farm && c.farmGrowth >= 1.0f && c.farmReservedBy == -1)
            {
                farmX = px;
                farmY = py;
                return true;
            }
        }

        return false;
    };

    // Dijkstra to the nearest work tile adjacent to any harvestable farm.
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const std::size_t n  = static_cast<std::size_t>(w * h);

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        if (m_nearestStamp[id] != stamp)
            return kInf;
        return m_nearestDist[id];
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent) {
        m_nearestStamp[id]  = stamp;
        m_nearestDist[id]   = d;
        m_nearestParent[id] = parent;
    };

    struct QN {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        // Skip if this is an outdated entry.
        if (cur.d != getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);

        // Check if current tile is adjacent to a harvestable farm.
        int farmX = -1;
        int farmY = -1;
        if (findAdjacentHarvestableFarm(C.x, C.y, farmX, farmY))
        {
            outFarmX = farmX;
            outFarmY = farmY;

            // Reconstruct path: start -> current
            std::vector<colony::pf::IVec2> rev;
            colony::pf::NodeId t = cur.id;
            while (t != colony::pf::kInvalid)
            {
                if (m_nearestStamp[t] != stamp)
                    break;

                rev.push_back(colony::pf::from_id(t, w));
                if (t == sid)
                    break;

                t = m_nearestParent[t];
            }

            if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
                return false;

            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return !outPath.empty();
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    return false;
}


bool World::findPathToNearestLooseWood(int startX, int startY,
                                       int& outWoodX, int& outWoodY,
                                       std::vector<colony::pf::IVec2>& outPath) const
{
    outPath.clear();
    outWoodX = -1;
    outWoodY = -1;

    if (!inBounds(startX, startY))
        return false;

    if (m_looseWoodCells.empty())
        return false;

    const int w = m_w;
    const int h = m_h;
    const std::size_t n = static_cast<std::size_t>(w * h);

    constexpr float kInf = std::numeric_limits<float>::infinity();

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        if (m_nearestStamp[id] != stamp)
            return kInf;
        return m_nearestDist[id];
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent) {
        m_nearestStamp[id] = stamp;
        m_nearestDist[id] = d;
        m_nearestParent[id] = parent;
    };

    struct QN {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        // Skip if this is an outdated entry.
        if (cur.d != getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const Cell& cc = cell(C.x, C.y);

        if (cc.looseWood > 0 && cc.looseWoodReservedBy == -1)
        {
            outWoodX = C.x;
            outWoodY = C.y;

            // Reconstruct path: start -> current
            std::vector<colony::pf::IVec2> rev;
            colony::pf::NodeId t = cur.id;
            while (t != colony::pf::kInvalid)
            {
                if (m_nearestStamp[t] != stamp)
                    break;

                rev.push_back(colony::pf::from_id(t, w));
                if (t == sid)
                    break;

                t = m_nearestParent[t];
            }

            if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
                return false;

            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return !outPath.empty();
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    return false;
}

bool World::findPathToBestLooseWoodForDelivery(std::uint32_t stockpileStamp,
                                              int startX, int startY,
                                              int& outWoodX, int& outWoodY,
                                              std::vector<colony::pf::IVec2>& outPath) const
{
    outPath.clear();
    outWoodX = -1;
    outWoodY = -1;

    if (stockpileStamp == 0u)
        return false;

    if (!inBounds(startX, startY) || !m_nav.passable(startX, startY))
        return false;

    if (m_looseWoodCells.empty() || m_looseWoodTotal <= 0)
        return false;

    const int w = m_w;
    const int h = m_h;
    if (w <= 0 || h <= 0)
        return false;

    const std::size_t n = static_cast<std::size_t>(w * h);
    constexpr float kInf = std::numeric_limits<float>::infinity();

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        if (m_nearestStamp[id] != stamp)
            return kInf;
        return m_nearestDist[id];
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent) {
        m_nearestStamp[id] = stamp;
        m_nearestDist[id] = d;
        m_nearestParent[id] = parent;
    };

    struct QN
    {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    float bestCombined = kInf;
    colony::pf::NodeId bestId = colony::pf::kInvalid;
    std::uint64_t bestKey = std::numeric_limits<std::uint64_t>::max();

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        // Skip if this is an outdated entry.
        if (cur.d != getDist(cur.id))
            continue;

        // Because dropDist >= 0, no future candidate can beat bestCombined once
        // the frontier distance exceeds it.
        if (cur.d > bestCombined)
            break;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const Cell& cc = cell(C.x, C.y);

        if (cc.looseWood > 0 && cc.looseWoodReservedBy == -1)
        {
            const float dropDist = stockpileFieldDistAt(stockpileStamp, C.x, C.y);
            if (std::isfinite(dropDist))
            {
                const float combined = cur.d + dropDist;
                const std::uint64_t key = PackPlanKey(C.x, C.y);

                if (combined < bestCombined || (combined == bestCombined && key < bestKey))
                {
                    bestCombined = combined;
                    bestId = cur.id;
                    bestKey = key;
                }
            }
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    if (bestId == colony::pf::kInvalid)
        return false;

    const colony::pf::IVec2 bestPos = colony::pf::from_id(bestId, w);
    outWoodX = bestPos.x;
    outWoodY = bestPos.y;

    // Reconstruct path: start -> best
    std::vector<colony::pf::IVec2> rev;
    colony::pf::NodeId t = bestId;
    while (t != colony::pf::kInvalid)
    {
        if (m_nearestStamp[t] != stamp)
            break;

        rev.push_back(colony::pf::from_id(t, w));
        if (t == sid)
            break;

        t = m_nearestParent[t];
    }

    if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
        return false;

    std::reverse(rev.begin(), rev.end());
    outPath.swap(rev);
    return !outPath.empty();
}

bool World::findPathToNearestStockpile(int startX, int startY,
                                       int& outX, int& outY,
                                       std::vector<colony::pf::IVec2>& outPath) const
{
    outPath.clear();
    outX = -1;
    outY = -1;

    if (!inBounds(startX, startY))
        return false;

    if (builtCount(TileType::Stockpile) <= 0)
        return false;

    const int w = m_w;
    const int h = m_h;
    const std::size_t n = static_cast<std::size_t>(w * h);

    constexpr float kInf = std::numeric_limits<float>::infinity();

    // Scratch buffers (mutable) to avoid per-call allocations and O(n) clears.
    if (m_nearestDist.size() != n)
    {
        m_nearestDist.assign(n, 0.0f);
        m_nearestParent.assign(n, colony::pf::kInvalid);
        m_nearestStamp.assign(n, 0u);
        m_nearestStampValue = 1;
    }

    // Bump generation (stamp 0 means "never visited"). Handle wrap.
    std::uint32_t stamp = m_nearestStampValue + 1u;
    if (stamp == 0u)
    {
        std::fill(m_nearestStamp.begin(), m_nearestStamp.end(), 0u);
        stamp = 1u;
    }
    m_nearestStampValue = stamp;

    auto getDist = [&](colony::pf::NodeId id) -> float {
        if (m_nearestStamp[id] != stamp)
            return kInf;
        return m_nearestDist[id];
    };
    auto setNode = [&](colony::pf::NodeId id, float d, colony::pf::NodeId parent) {
        m_nearestStamp[id] = stamp;
        m_nearestDist[id] = d;
        m_nearestParent[id] = parent;
    };

    struct QN {
        float d;
        colony::pf::NodeId id;
        bool operator>(const QN& o) const { return d > o.d; }
    };
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;

    const colony::pf::NodeId sid = colony::pf::to_id(startX, startY, w);
    setNode(sid, 0.0f, colony::pf::kInvalid);
    open.push({0.0f, sid});

    while (!open.empty())
    {
        const QN cur = open.top();
        open.pop();

        // Skip if this is an outdated entry.
        if (cur.d != getDist(cur.id))
            continue;

        const colony::pf::IVec2 C = colony::pf::from_id(cur.id, w);
        const Cell& cc = cell(C.x, C.y);

        if (cc.built == TileType::Stockpile)
        {
            outX = C.x;
            outY = C.y;

            // Reconstruct path: start -> current
            std::vector<colony::pf::IVec2> rev;
            colony::pf::NodeId t = cur.id;
            while (t != colony::pf::kInvalid)
            {
                if (m_nearestStamp[t] != stamp)
                    break;

                rev.push_back(colony::pf::from_id(t, w));
                if (t == sid)
                    break;

                t = m_nearestParent[t];
            }

            if (rev.empty() || rev.back().x != startX || rev.back().y != startY)
                return false;

            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return !outPath.empty();
        }

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1, -1, 0, 0,  1,  1, -1, -1 };
        static const int dy[DIRS] = { 0,  0, 1, -1, 1, -1,  1, -1 };

        for (int dir = 0; dir < DIRS; ++dir)
        {
            if (!m_nav.can_step(C.x, C.y, dx[dir], dy[dir]))
                continue;

            const int nx = C.x + dx[dir];
            const int ny = C.y + dy[dir];
            if (!inBounds(nx, ny))
                continue;

            const colony::pf::NodeId nid = colony::pf::to_id(nx, ny, w);
            const float nd = cur.d + m_nav.step_cost(C.x, C.y, dx[dir], dy[dir]);

            if (nd < getDist(nid))
            {
                setNode(nid, nd, cur.id);
                open.push({nd, nid});
            }
        }
    }

    return false;
}

void World::assignEatJobs(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;

    const float threshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));
    const float maxFood   = static_cast<float>(std::max(0.0, colonistMaxPersonalFood));

    if (threshold <= 0.0f || maxFood <= 0.0f)
        return;

    // First pass: cancel any non-eat jobs for colonists that are hungry.
    for (Colonist& c : m_colonists)
    {
        if (c.personalFood > threshold || !c.hasJob)
            continue;

        // If a colonist is hungry, let them eat instead of continuing
        // low-priority work or player move orders.
        if (c.jobKind == Colonist::JobKind::BuildPlan || c.jobKind == Colonist::JobKind::ManualMove || c.jobKind == Colonist::JobKind::HaulWood)
            cancelJob(c);
    }

    // Build a shared food distance field once. This field is cached and reused
    // until the navigation grid changes (walls built, etc.).
    const std::uint32_t foodStamp = buildFoodField();
    const float eatDur = static_cast<float>(std::max(0.0, colonistEatDurationSeconds));

    // Second pass: assign eat jobs to hungry, idle colonists.
    for (Colonist& c : m_colonists)
    {
        if (c.hasJob)
            continue;
        if (c.personalFood > threshold)
            continue;

        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
            continue;

        int foodX = -1;
        int foodY = -1;
        std::vector<colony::pf::IVec2> path;

        bool found = false;

        if (foodStamp != 0u && queryFoodField(foodStamp, sx, sy, foodX, foodY, path))
        {
            if (inBounds(foodX, foodY))
            {
                const TileType b = cell(foodX, foodY).built;
                if (b == TileType::Stockpile || b == TileType::Farm)
                    found = true;
            }
        }

        if (found)
        {
            ++m_pathStats.eatFieldAssigned;
        }
        else if (foodStamp != 0u)
        {
            // Field exists but didn't yield a usable result. Fall back to a per-colonist
            // search to preserve behavior and robustness.
            ++m_pathStats.eatFieldFallback;
        }

        if (!found)
        {
            if (foodStamp == 0u || !findPathToNearestFoodSource(sx, sy, foodX, foodY, path))
            {
                // No stockpiles/farms yet (or none reachable). Fall back to eating in-place.
                foodX = sx;
                foodY = sy;
                path.clear();
                path.push_back({sx, sy});
            }
        }

        c.hasJob = true;
        c.jobKind = Colonist::JobKind::Eat;
        c.targetX = foodX;
        c.targetY = foodY;
        c.path = std::move(path);
        c.pathIndex = 0;
        c.eatWorkRemaining = eatDur;
    }
}

void World::assignHarvestJobs(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;

    // Fast exit: no farms.
    if (m_farmCells.empty())
        return;

    // If all harvestable farms are currently reserved, there's nothing to do.
    bool anyUnreserved = false;
    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& c = cell(pos.x, pos.y);
        if (c.built != TileType::Farm)
            continue;
        if (c.farmGrowth < 1.0f)
            continue;
        if (c.farmReservedBy != -1)
            continue;

        anyUnreserved = true;
        break;
    }

    if (!anyUnreserved)
        return;

    // For per-colonist work priorities, determine whether other work types are currently available.
    bool buildWorkAvailable = false;
    for (const auto& pos : m_plannedCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;
        const Cell& pc = cell(pos.x, pos.y);
        if (pc.planned == TileType::Empty || pc.planned == pc.built)
            continue;
        if (pc.reservedBy == -1)
        {
            buildWorkAvailable = true;
            break;
        }
    }

    bool haulWorkAvailable = false;
    if (builtCount(TileType::Stockpile) > 0 && m_looseWoodTotal > 0)
    {
        for (const auto& pos : m_looseWoodCells)
        {
            if (!inBounds(pos.x, pos.y))
                continue;
            const Cell& wc = cell(pos.x, pos.y);
            if (wc.looseWood <= 0)
                continue;
            if (wc.looseWoodReservedBy != -1)
                continue;
            haulWorkAvailable = true;
            break;
        }
    }

    m_harvestAssignCooldown = std::max(0.0, m_harvestAssignCooldown - dtSeconds);
    if (m_harvestAssignCooldown > 0.0)
        return;

    const float eatThreshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));
    const bool foodEmpty = (m_inv.food <= 0.0f);

    if (foodEmpty)
    {
        // With zero food, "Eat" jobs will just wait forever. Cancel them so at
        // least some colonists can go harvest and bootstrap the inventory.
        for (Colonist& c : m_colonists)
        {
            if (c.hasJob && c.jobKind == Colonist::JobKind::Eat)
                cancelJob(c);
        }

        // If nobody is idle *and farm-capable*, preempt a single colonist that can farm.
        // (Important: an idle non-farmer should not block the bootstrap behavior.)
        bool anyIdleFarmer = false;
        for (const Colonist& c : m_colonists)
        {
            if (!c.hasJob && !c.drafted && HasCap(c, Capability::Farming))
            {
                anyIdleFarmer = true;
                break;
            }
        }

        if (!anyIdleFarmer)
        {
            // Prefer cancelling build work on a colonist that can farm (Worker), so it can immediately switch.
            for (Colonist& c : m_colonists)
            {
                if (c.hasJob && !c.drafted && c.jobKind == Colonist::JobKind::BuildPlan && HasCap(c, Capability::Farming))
                {
                    cancelJob(c);
                    break;
                }
            }
        }
    }

    // Build a shared harvest distance field once for this assignment pass.
    const std::uint32_t harvestStamp = buildHarvestField();
    const float harvestDur = static_cast<float>(std::max(0.0, farmHarvestDurationSeconds));

    bool assignedAny = false;

    for (Colonist& c : m_colonists)
    {
        if (c.hasJob)
            continue;

        // Drafted colonists are manually controlled.
        if (c.drafted)
            continue;

        // Role capability gate: only colonists with Farming can take harvest jobs.
        if (!HasCap(c, Capability::Farming))
            continue;

        // If we have food, let hungry colonists eat first.
        if (!foodEmpty && eatThreshold > 0.0f && c.personalFood <= eatThreshold)
            continue;

        // Respect per-colonist work priorities (unless we're bootstrapping from 0 food).
        if (!foodEmpty)
        {
            const int best = BestWorkPrio(c, buildWorkAvailable, /*farmAvailable=*/anyUnreserved, haulWorkAvailable);
            if (best == kWorkPrioOff || WorkPrioEff(c.workPrio.farm) != best)
                continue;
        }

        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
            continue;

        int farmX = -1;
        int farmY = -1;
        std::vector<colony::pf::IVec2> path;

        bool found = false;
        if (harvestStamp != 0u && queryHarvestField(harvestStamp, sx, sy, farmX, farmY, path))
        {
            if (inBounds(farmX, farmY))
            {
                const Cell& farm = cell(farmX, farmY);
                if (farm.built == TileType::Farm && farm.farmGrowth >= 1.0f && farm.farmReservedBy == -1)
                    found = true;
            }
        }

        if (found)
        {
            ++m_pathStats.harvestFieldAssigned;
        }
        else if (harvestStamp != 0u)
        {
            // The field exists but didn't provide a valid/available target (e.g. got reserved).
            ++m_pathStats.harvestFieldFallback;
        }

        if (!found)
        {
            // Fallback to the per-colonist Dijkstra for correctness under dynamic reservations.
            if (harvestStamp == 0u || !findPathToNearestHarvestableFarm(sx, sy, farmX, farmY, path))
                continue;
        }

        if (!inBounds(farmX, farmY))
            continue;

        Cell& farm = cell(farmX, farmY);
        if (farm.built != TileType::Farm || farm.farmGrowth < 1.0f)
            continue;

        // Reserve, since multiple colonists may evaluate this in the same tick.
        if (farm.farmReservedBy != -1)
            continue;
        farm.farmReservedBy = c.id;

        c.hasJob = true;
        c.jobKind = Colonist::JobKind::Harvest;
        c.targetX = farmX;
        c.targetY = farmY;
        c.path = std::move(path);
        c.pathIndex = 0;
        c.harvestWorkRemaining = harvestDur;

        assignedAny = true;
    }

    // If we couldn't assign, retry next tick; otherwise throttle a bit.
    m_harvestAssignCooldown = assignedAny ? kJobAssignIntervalSeconds : 0.0;
}

void World::assignJobs(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;

    // Fast exit: no plans.
    if (m_plannedCells.empty())
        return;

    // If all plans are currently reserved, there's nothing to do.
    // (Avoids running a full path search that cannot possibly succeed.)
    bool anyUnreserved = false;
    std::array<bool, 4> anyUnreservedAtPriority{};

    for (const auto& pos : m_plannedCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;
        const Cell& c = cell(pos.x, pos.y);
        if (c.planned == TileType::Empty || c.planned == c.built)
            continue;
        if (c.reservedBy == -1)
        {
            anyUnreserved = true;
            const int pr = std::min(3, static_cast<int>(c.planPriority));
            anyUnreservedAtPriority[static_cast<std::size_t>(pr)] = true;
        }
    }
    if (!anyUnreserved)
        return;

    // For per-colonist work priorities, determine whether other work types are currently available.
    bool farmWorkAvailable = false;
    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& fc = cell(pos.x, pos.y);
        if (fc.built != TileType::Farm)
            continue;
        if (fc.farmGrowth < 1.0f)
            continue;
        if (fc.farmReservedBy != -1)
            continue;

        farmWorkAvailable = true;
        break;
    }

    bool haulWorkAvailable = false;
    if (builtCount(TileType::Stockpile) > 0 && m_looseWoodTotal > 0)
    {
        for (const auto& pos : m_looseWoodCells)
        {
            if (!inBounds(pos.x, pos.y))
                continue;
            const Cell& wc = cell(pos.x, pos.y);
            if (wc.looseWood <= 0)
                continue;
            if (wc.looseWoodReservedBy != -1)
                continue;

            haulWorkAvailable = true;
            break;
        }
    }

    // Decrement throttle timer.
    m_jobAssignCooldown = std::max(0.0, m_jobAssignCooldown - dtSeconds);

    // If nobody is idle *and eligible to build*, clear the throttle so the next eligible
    // idle colonist is assigned immediately.
    const float eatThreshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));

    bool anyIdleBuilder = false;
    for (const Colonist& c : m_colonists)
    {
        if (!c.hasJob && !c.drafted && HasCap(c, Capability::Building) &&
            (eatThreshold <= 0.0f || c.personalFood > eatThreshold))
        {
            const int best = BestWorkPrio(c, /*buildAvailable=*/anyUnreserved, farmWorkAvailable, haulWorkAvailable);
            if (best != kWorkPrioOff && WorkPrioEff(c.workPrio.build) == best)
            {
                anyIdleBuilder = true;
                break;
            }
        }
    }
    if (!anyIdleBuilder)
    {
        m_jobAssignCooldown = 0.0;
        return;
    }

    // Throttle assignment attempts to avoid CPU spikes when there are many
    // plans but no reachable jobs (or when plans are rapidly edited).
    if (m_jobAssignCooldown > 0.0)
        return;

    m_jobAssignCooldown = kJobAssignIntervalSeconds;

    // ------------------------------------------------------------
    // Multi-source plan distance field
    // ------------------------------------------------------------
    // We build a Dijkstra field once per plan priority and then query it
    // for each idle builder.
    //
    // This avoids doing a full Dijkstra per colonist when many colonists are idle.
    // If the field points at a plan that has been reserved earlier in this same
    // tick, we fall back to the legacy per-colonist search.

    for (int pr = 3; pr >= 0; --pr)
    {
        if (!anyUnreservedAtPriority[static_cast<std::size_t>(pr)])
            continue;

        const std::uint32_t fieldStamp = buildPlanField(pr);
        if (fieldStamp == 0u)
            continue;

        for (Colonist& c : m_colonists)
        {
            if (c.hasJob)
                continue;

            // Drafted colonists are manually controlled.
            if (c.drafted)
                continue;

            // Role capability gate: only colonists with Building can take build plans.
            if (!HasCap(c, Capability::Building))
                continue;

            // Hungry colonists should not pick up construction jobs.
            if (eatThreshold > 0.0f && c.personalFood <= eatThreshold)
                continue;

            // Respect per-colonist work priorities.
            {
                const int best = BestWorkPrio(c, /*buildAvailable=*/anyUnreserved, farmWorkAvailable, haulWorkAvailable);
                if (best == kWorkPrioOff || WorkPrioEff(c.workPrio.build) != best)
                    continue;
            }

            const int sx = static_cast<int>(std::floor(c.x));
            const int sy = static_cast<int>(std::floor(c.y));

            // If we're currently on an invalid tile (should not happen), idle.
            if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
                continue;

            int targetX = -1;
            int targetY = -1;
            std::vector<colony::pf::IVec2> path;

            bool found = queryPlanField(fieldStamp, sx, sy, targetX, targetY, path);
            bool usedFallback = false;

            if (found)
            {
                // The field is built from an unreserved snapshot, but reservations can change
                // while we're assigning. Validate the plan is still buildable.
                if (!inBounds(targetX, targetY))
                {
                    found = false;
                }
                else
                {
                    const Cell& tc = cell(targetX, targetY);
                    if (tc.planned == TileType::Empty || tc.planned == tc.built)
                        found = false;
                    else if (static_cast<int>(std::min<std::uint8_t>(3, tc.planPriority)) != pr)
                        found = false;
                    else if (tc.reservedBy != -1)
                        found = false;
                }
            }

            if (!found)
            {
                // Fallback: per-colonist search for this priority.
                usedFallback = findPathToNearestAvailablePlan(sx, sy, targetX, targetY, path, pr);
                found = usedFallback;
            }

            if (!found)
                continue;

            // Reserve the plan for this colonist (re-check in case it raced).
            if (!inBounds(targetX, targetY))
                continue;

            Cell& target = cell(targetX, targetY);
            if (target.reservedBy != -1)
                continue;
            target.reservedBy = c.id;

            if (usedFallback)
                ++m_pathStats.buildFieldFallback;
            else
                ++m_pathStats.buildFieldAssigned;

            c.hasJob = true;
            c.jobKind = Colonist::JobKind::BuildPlan;
            c.targetX = targetX;
            c.targetY = targetY;
            c.path = std::move(path);
            c.pathIndex = 0;
        }
    }
}



void World::assignHaulJobs(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;

    if (m_looseWoodCells.empty() || m_looseWoodTotal <= 0)
        return;

    // Without any stockpiles, hauling has nowhere to deliver to.
    if (builtCount(TileType::Stockpile) <= 0)
        return;

    // Throttle pathfinding work.
    m_haulAssignCooldown = std::max(0.0, m_haulAssignCooldown - dtSeconds);
    if (m_haulAssignCooldown > 0.0)
        return;
    m_haulAssignCooldown = 0.15;

    // Early out if all loose wood stacks are reserved.
    bool haulWorkAvailable = false;
    for (const auto& pos : m_looseWoodCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;
        const Cell& wc = cell(pos.x, pos.y);
        if (wc.looseWood <= 0)
            continue;
        if (wc.looseWoodReservedBy != -1)
            continue;

        haulWorkAvailable = true;
        break;
    }
    if (!haulWorkAvailable)
        return;

    // For per-colonist work priorities, determine whether other work types are currently available.
    bool buildWorkAvailable = false;
    for (const auto& pos : m_plannedCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;
        const Cell& pc = cell(pos.x, pos.y);
        if (pc.planned == TileType::Empty || pc.planned == pc.built)
            continue;
        if (pc.reservedBy == -1)
        {
            buildWorkAvailable = true;
            break;
        }
    }

    bool farmWorkAvailable = false;
    for (const auto& pos : m_farmCells)
    {
        if (!inBounds(pos.x, pos.y))
            continue;

        const Cell& fc = cell(pos.x, pos.y);
        if (fc.built != TileType::Farm)
            continue;
        if (fc.farmGrowth < 1.0f)
            continue;
        if (fc.farmReservedBy != -1)
            continue;

        farmWorkAvailable = true;
        break;
    }

    // Build the shared fields once; per-colonist assignment is O(pathLength).
    const std::uint32_t stockpileStamp = buildStockpileField();
    if (stockpileStamp == 0u)
        return;

    const std::uint32_t haulStamp = buildHaulPickupField(stockpileStamp);
    if (haulStamp == 0u)
        return;

    const float eatThreshold = static_cast<float>(std::max(0.0, colonistEatThresholdFood));

    for (auto& c : m_colonists)
    {
        if (c.drafted)
            continue;

        if (c.hasJob)
            continue;

        if (!HasCap(c, Capability::Hauling))
            continue;

        // Let hungry colonists prioritize eating if there's food available.
        if (eatThreshold > 0.0f && c.personalFood <= eatThreshold && m_inv.food > 0)
            continue;

        // Respect per-colonist work priorities.
        {
            const int best = BestWorkPrio(c, buildWorkAvailable, farmWorkAvailable, /*haulAvailable=*/haulWorkAvailable);
            if (best == kWorkPrioOff || WorkPrioEff(c.workPrio.haul) != best)
                continue;
        }

        const int sx = posToTile(c.x);
        const int sy = posToTile(c.y);

        int woodX = -1;
        int woodY = -1;
        std::vector<colony::pf::IVec2> path;

        bool usedFallback = false;
        bool ok = queryHaulPickupField(haulStamp, sx, sy, woodX, woodY, path);

        if (ok)
        {
            if (!inBounds(woodX, woodY))
            {
                ok = false;
            }
            else
            {
                const Cell& srcC = cell(woodX, woodY);
                if (srcC.looseWood <= 0 || srcC.looseWoodReservedBy != -1)
                    ok = false;
            }
        }

        if (!ok)
        {
            usedFallback = findPathToBestLooseWoodForDelivery(stockpileStamp, sx, sy, woodX, woodY, path);
            ok = usedFallback;
        }

        if (!ok)
            continue;

        if (!inBounds(woodX, woodY))
            continue;

        // Validate (again) and reserve the stack so multiple haulers don't target the same tile.
        Cell& src = cell(woodX, woodY);
        if (src.looseWood <= 0 || src.looseWoodReservedBy != -1)
            continue;

        // Ensure this pile can reach some stockpile (otherwise we'd pick up and then be unable to deliver).
        if (!std::isfinite(stockpileFieldDistAt(stockpileStamp, woodX, woodY)))
            continue;

        src.looseWoodReservedBy = c.id;

        if (usedFallback)
            ++m_pathStats.haulPickupFieldFallback;
        else
            ++m_pathStats.haulPickupFieldAssigned;

        c.hasJob = true;
        c.jobKind = Colonist::JobKind::HaulWood;

        c.carryingWood = 0;
        c.haulingToDropoff = false;
        c.haulPickupX = woodX;
        c.haulPickupY = woodY;
        c.haulDropX = 0;
        c.haulDropY = 0;
        c.haulWorkRemaining = static_cast<float>(std::max(0.0, haulPickupDurationSeconds));

        c.targetX = woodX;
        c.targetY = woodY;

        c.path = std::move(path);
        c.pathIndex = 0;
    }
}

bool World::computePathToAdjacent(Colonist& c, int targetX, int targetY)
{
    c.path.clear();
    c.pathIndex = 0;

    const int sx = static_cast<int>(std::floor(c.x));
    const int sy = static_cast<int>(std::floor(c.y));

    std::vector<colony::pf::IVec2> path;
    if (!computePathToAdjacentFrom(sx, sy, targetX, targetY, path))
        return false;

    c.path.swap(path);
    c.pathIndex = 0;
    return !c.path.empty();
}

bool World::computePathToAdjacentFrom(int startX, int startY,
                                     int targetX, int targetY,
                                     std::vector<colony::pf::IVec2>& outPath) const
{
    outPath.clear();
    ++m_pathStats.reqAdjacent;

    if (!inBounds(startX, startY) || !inBounds(targetX, targetY))
        return false;
    if (!m_nav.passable(startX, startY))
        return false;

    const bool useCache = pathCacheEnabled && pathCacheMaxEntries > 0;
    const PathCacheKey key{ startX, startY, targetX, targetY, 1 };

    auto isValidCached = [&](const std::vector<colony::pf::IVec2>& p) -> bool
    {
        if (!ValidateDensePath(m_nav, m_w, m_h, p))
            return false;
        if (p.front().x != startX || p.front().y != startY)
            return false;
        const auto end = p.back();
        const int manhattan = std::abs(end.x - targetX) + std::abs(end.y - targetY);
        return manhattan == 1;
    };

    if (useCache)
    {
        auto it = m_pathCache.find(key);
        if (it != m_pathCache.end())
        {
            if (isValidCached(it->second.path))
            {
                // Touch LRU.
                m_pathCacheLru.splice(m_pathCacheLru.begin(), m_pathCacheLru, it->second.lruIt);
                outPath = it->second.path;
                ++m_pathStats.hitAdjacent;
                return true;
            }

            // Stale entry.
            m_pathCacheLru.erase(it->second.lruIt);
            m_pathCache.erase(it);
            ++m_pathStats.invalidated;
        }
    }

    // Choose an adjacent walkable tile as the work position.
    // We prefer minimal travel cost (includes terrain multipliers).
    constexpr int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    std::vector<colony::pf::IVec2> best;
    float bestCost = std::numeric_limits<float>::infinity();
    std::size_t bestLen = std::numeric_limits<std::size_t>::max();

    std::vector<colony::pf::IVec2> tmp;

    for (const auto& d : kDirs)
    {
        const int gx = targetX + d[0];
        const int gy = targetY + d[1];
        if (!inBounds(gx, gy) || !m_nav.passable(gx, gy))
            continue;

        if (!ComputePathAlgo(m_nav, pathAlgo, startX, startY, gx, gy, tmp))
            continue;

        if (pathAlgo == PathAlgo::JumpPointSearch)
            ++m_pathStats.computedJps;
        else
            ++m_pathStats.computedAStar;

        const float cost = DensePathCost(m_nav, tmp);
        const std::size_t len = tmp.size();

        if (cost < bestCost || (cost == bestCost && len < bestLen))
        {
            bestCost = cost;
            bestLen = len;
            best = tmp;
        }
    }

    if (best.empty())
        return false;

    outPath.swap(best);

    if (useCache)
    {
        while (m_pathCache.size() >= static_cast<std::size_t>(pathCacheMaxEntries) && !m_pathCacheLru.empty())
        {
            const PathCacheKey oldKey = m_pathCacheLru.back();
            m_pathCacheLru.pop_back();
            (void)m_pathCache.erase(oldKey);
            ++m_pathStats.evicted;
        }

        m_pathCacheLru.push_front(key);
        PathCacheValue v;
        v.path = outPath;
        v.lruIt = m_pathCacheLru.begin();
        m_pathCache.emplace(key, std::move(v));
    }

    return !outPath.empty();
}

bool World::computePathToTile(Colonist& c, int targetX, int targetY)
{
    c.path.clear();
    c.pathIndex = 0;

    const int sx = static_cast<int>(std::floor(c.x));
    const int sy = static_cast<int>(std::floor(c.y));

    std::vector<colony::pf::IVec2> path;
    if (!computePathToTileFrom(sx, sy, targetX, targetY, path))
        return false;

    c.path.swap(path);
    c.pathIndex = 0;
    return !c.path.empty();
}

bool World::computePathToTileFrom(int startX, int startY,
                                 int targetX, int targetY,
                                 std::vector<colony::pf::IVec2>& outPath) const
{
    outPath.clear();
    ++m_pathStats.reqTile;

    if (!inBounds(startX, startY) || !inBounds(targetX, targetY))
        return false;
    if (!m_nav.passable(startX, startY))
        return false;
    if (!m_nav.passable(targetX, targetY))
        return false;

    const bool useCache = pathCacheEnabled && pathCacheMaxEntries > 0;
    const PathCacheKey key{ startX, startY, targetX, targetY, 0 };

    auto isValidCached = [&](const std::vector<colony::pf::IVec2>& p) -> bool
    {
        if (!ValidateDensePath(m_nav, m_w, m_h, p))
            return false;
        if (p.front().x != startX || p.front().y != startY)
            return false;
        const auto end = p.back();
        return end.x == targetX && end.y == targetY;
    };

    if (useCache)
    {
        auto it = m_pathCache.find(key);
        if (it != m_pathCache.end())
        {
            if (isValidCached(it->second.path))
            {
                m_pathCacheLru.splice(m_pathCacheLru.begin(), m_pathCacheLru, it->second.lruIt);
                outPath = it->second.path;
                ++m_pathStats.hitTile;
                return true;
            }

            m_pathCacheLru.erase(it->second.lruIt);
            m_pathCache.erase(it);
            ++m_pathStats.invalidated;
        }
    }

    if (!ComputePathAlgo(m_nav, pathAlgo, startX, startY, targetX, targetY, outPath))
        return false;

    if (pathAlgo == PathAlgo::JumpPointSearch)
        ++m_pathStats.computedJps;
    else
        ++m_pathStats.computedAStar;

    if (useCache)
    {
        while (m_pathCache.size() >= static_cast<std::size_t>(pathCacheMaxEntries) && !m_pathCacheLru.empty())
        {
            const PathCacheKey oldKey = m_pathCacheLru.back();
            m_pathCacheLru.pop_back();
            (void)m_pathCache.erase(oldKey);
            ++m_pathStats.evicted;
        }

        m_pathCacheLru.push_front(key);
        PathCacheValue v;
        v.path = outPath;
        v.lruIt = m_pathCacheLru.begin();
        m_pathCache.emplace(key, std::move(v));
    }

    return !outPath.empty();
}

void World::stepColonist(Colonist& c, double dtSeconds)
{
    if (!c.hasJob)
        return;

    // Validate job.
    if (!inBounds(c.targetX, c.targetY))
    {
        cancelJob(c);
        return;
    }

    // Build jobs must still target an active plan; otherwise drop.
    if (c.jobKind == Colonist::JobKind::BuildPlan)
    {
        const Cell& t = cell(c.targetX, c.targetY);
        if (t.planned == TileType::Empty || t.planned == t.built)
        {
            cancelJob(c);
            return;
        }
    }

    // Manual move jobs must target a passable tile.
    if (c.jobKind == Colonist::JobKind::ManualMove)
    {
        if (!m_nav.passable(c.targetX, c.targetY))
        {
            cancelJob(c);
            return;
        }
    }
    else if (c.jobKind == Colonist::JobKind::HaulWood)
    {
        if (!HasCap(c, Capability::Hauling))
        {
            cancelJob(c);
            return;
        }

        // Validate haul state.
        if (!c.haulingToDropoff)
        {
            if (!inBounds(c.haulPickupX, c.haulPickupY))
            {
                cancelJob(c);
                return;
            }

            const Cell& src = cell(c.haulPickupX, c.haulPickupY);
            if (src.looseWood <= 0 || src.looseWoodReservedBy != c.id)
            {
                cancelJob(c);
                return;
            }
        }
        else
        {
            if (c.carryingWood <= 0)
            {
                cancelJob(c);
                return;
            }
        }
    }

    // If our path is invalidated (walls built), drop it so we'll re-path.

    if (c.pathIndex < c.path.size())
    {
        const colony::pf::IVec2 next = c.path[c.pathIndex];
        if (!inBounds(next.x, next.y) || !m_nav.passable(next.x, next.y))
        {
            // Re-path next tick.
            c.path.clear();
            c.pathIndex = 0;
        }
    }

    if (c.path.empty())
    {
        // Special-case: "eat in place" jobs target the tile we're already on.
        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        if (c.jobKind == Colonist::JobKind::Eat && sx == c.targetX && sy == c.targetY && inBounds(sx, sy))
        {
            c.path.push_back({sx, sy});
            c.pathIndex = 0;
            return;
        }

        // Try to recompute.
        const bool ok = (c.jobKind == Colonist::JobKind::ManualMove || c.jobKind == Colonist::JobKind::HaulWood)
            ? computePathToTile(c, c.targetX, c.targetY)
            : computePathToAdjacent(c, c.targetX, c.targetY);

        if (!ok)
        {
            if (c.jobKind == Colonist::JobKind::BuildPlan)
            {
                // Can't reach currently; leave unreserved so another colonist might.
                Cell& target = cell(c.targetX, c.targetY);
                if (target.reservedBy == c.id)
                    target.reservedBy = -1;
            }
            cancelJob(c);
        }
        return;
    }

    // Walk along the path.
    const float baseSpeed = static_cast<float>(std::max(0.1, colonistWalkSpeed));
    const float speedBase = baseSpeed * EffectiveMoveMult(c);

    double timeLeft = dtSeconds;

    while (timeLeft > 0.0 && c.pathIndex < c.path.size())
    {
        const colony::pf::IVec2 p = c.path[c.pathIndex];
        const float goalX = static_cast<float>(p.x) + 0.5f;
        const float goalY = static_cast<float>(p.y) + 0.5f;

        const float dx = goalX - c.x;
        const float dy = goalY - c.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 1.0e-3f)
        {
            // Snap to the node to avoid accumulating tiny drift errors.
            c.x = goalX;
            c.y = goalY;
            ++c.pathIndex;
            continue;
        }

        // Optional terrain traversal costs (farms/stockpiles/doors) slow movement
        // and are also reflected in the nav step cost.
        const float costMul = navUseTerrainCosts ? std::max(1.0f, m_nav.tile_cost(p.x, p.y)) : 1.0f;
        const float segSpeed = std::max(0.01f, speedBase / costMul);

        const double tNeed = static_cast<double>(dist / segSpeed);
        if (tNeed <= timeLeft)
        {
            c.x = goalX;
            c.y = goalY;
            ++c.pathIndex;
            timeLeft -= tNeed;
            continue;
        }

        const float step = segSpeed * static_cast<float>(timeLeft);
        c.x += dx / dist * step;
        c.y += dy / dist * step;
        break;
    }

    // Player move orders complete when we reach the final path node.
    if (c.jobKind == Colonist::JobKind::ManualMove && c.pathIndex >= c.path.size())
    {
        completeQueuedManualOrder(c);
        cancelJob(c);
    }
}

void World::stepConstructionIfReady(Colonist& c, double dtSeconds)
{
    if (!c.hasJob || c.jobKind != Colonist::JobKind::BuildPlan)
        return;

    if (c.pathIndex < c.path.size())
        return; // still moving

    if (!inBounds(c.targetX, c.targetY))
    {
        cancelJob(c);
        return;
    }

    Cell& target = cell(c.targetX, c.targetY);

    if (target.reservedBy != c.id)
    {
        // Someone else took it (or it was cleared). Drop.
        cancelJob(c);
        return;
    }

    if (target.planned == TileType::Empty || target.planned == target.built)
    {
        target.reservedBy = -1;
        cancelJob(c);
        return;
    }

    const double baseWorkPerSecond = std::max(0.05, buildWorkPerSecond);
    const double work = baseWorkPerSecond * dtSeconds * static_cast<double>(EffectiveWorkMult(c));
    target.workRemaining -= static_cast<float>(work);

    const TileType planBefore = target.planned;
    applyPlanIfComplete(c.targetX, c.targetY);

    // If completed, drop job.
    if (target.planned == TileType::Empty || target.planned == target.built)
    {
        if (planBefore != TileType::Empty)
            c.role.grant_xp(XpForPlanCompletion(planBefore));
        completeQueuedManualOrder(c);
        cancelJob(c);
    }
}

void World::stepHarvestIfReady(Colonist& c, double dtSeconds)
{
    if (!c.hasJob || c.jobKind != Colonist::JobKind::Harvest)
        return;

    if (c.pathIndex < c.path.size())
        return; // still moving

    if (!inBounds(c.targetX, c.targetY))
    {
        cancelJob(c);
        return;
    }

    Cell& farm = cell(c.targetX, c.targetY);
    if (farm.built != TileType::Farm)
    {
        cancelJob(c);
        return;
    }

    if (farm.farmReservedBy != c.id)
    {
        // Someone else took it (or it was cleared).
        cancelJob(c);
        return;
    }

    // If it was harvested/reset before we arrived, just wait here for it to regrow.
    if (farm.farmGrowth < 1.0f)
        return;

    const float dtF = static_cast<float>(dtSeconds);
    c.harvestWorkRemaining = std::max(0.0f, c.harvestWorkRemaining - dtF * EffectiveWorkMult(c));

    if (c.harvestWorkRemaining > 0.0f)
        return;

    const float yield = static_cast<float>(std::max(0.0, farmHarvestYieldFood));
    if (yield > 0.0f)
    {
        m_inv.food += yield;
        m_inv.food = clampf(m_inv.food, 0.0f, 1.0e9f);
    }

    c.role.grant_xp(XpForHarvest(yield));

    // Reset the farm for the next growth cycle.
    farm.farmGrowth = 0.0f;

    completeQueuedManualOrder(c);
    cancelJob(c);
}

void World::stepEatingIfReady(Colonist& c, double dtSeconds)
{
    if (!c.hasJob || c.jobKind != Colonist::JobKind::Eat)
        return;

    if (c.pathIndex < c.path.size())
        return; // still moving

    if (!inBounds(c.targetX, c.targetY))
    {
        cancelJob(c);
        return;
    }

    const float maxFood = static_cast<float>(std::max(0.0, colonistMaxPersonalFood));
    if (maxFood <= 0.0f)
    {
        cancelJob(c);
        return;
    }

    const float need = std::max(0.0f, maxFood - c.personalFood);
    if (need <= 1.0e-4f)
    {
        // Already full.
        cancelJob(c);
        return;
    }

    // No food available yet: keep the eat job and wait.
    if (m_inv.food <= 0.0f)
        return;

    // Eating takes a short amount of time once food is present.
    c.eatWorkRemaining = std::max(0.0f, c.eatWorkRemaining - static_cast<float>(dtSeconds) * EffectiveWorkMult(c));
    if (c.eatWorkRemaining > 0.0f)
        return;

    const float take = std::min(need, m_inv.food);
    if (take <= 0.0f)
        return;

    m_inv.food -= take;
    m_inv.food = clampf(m_inv.food, 0.0f, 1.0e9f);

    c.personalFood = std::min(maxFood, c.personalFood + take);

    // Back to work.
    cancelJob(c);
}


void World::stepHaulIfReady(Colonist& c, double dtSeconds)
{
    if (!c.hasJob || c.jobKind != Colonist::JobKind::HaulWood)
        return;

    // Only do pickup / dropoff when we've arrived at our current target.
    if (c.pathIndex < c.path.size())
        return;

    const int tx = clampi(posToTile(c.x), 0, m_w - 1);
    const int ty = clampi(posToTile(c.y), 0, m_h - 1);
    if (tx != c.targetX || ty != c.targetY)
        return;

    const float dtWork = static_cast<float>(dtSeconds * EffectiveWorkMult(c));
    c.haulWorkRemaining -= dtWork;
    if (c.haulWorkRemaining > 0.0f)
        return;

    // -----------------------------------------------------------------
    // Stage 1: pickup
    // -----------------------------------------------------------------
    if (!c.haulingToDropoff)
    {
        if (!inBounds(c.haulPickupX, c.haulPickupY))
        {
            cancelJob(c);
            return;
        }

        Cell& src = cell(c.haulPickupX, c.haulPickupY);

        // Another colonist may have taken it (or it was invalidated).
        if (src.looseWood <= 0 || src.looseWoodReservedBy != c.id)
        {
            if (src.looseWoodReservedBy == c.id)
                src.looseWoodReservedBy = -1;

            cancelJob(c);
            return;
        }

        const int cap = std::max(1, haulCarryCapacity + static_cast<int>(c.role.carry()));
        const int take = std::min(cap, src.looseWood);

        adjustLooseWood(c.haulPickupX, c.haulPickupY, -take);

        // Release reservation once we've taken our share.
        if (src.looseWoodReservedBy == c.id)
            src.looseWoodReservedBy = -1;

        c.carryingWood = take;

        // Find a stockpile to deliver to (prefer the cached stockpile field; fall back if needed).
        int spX = -1;
        int spY = -1;
        std::vector<colony::pf::IVec2> path;

        const std::uint32_t stockpileStamp = buildStockpileField();
        bool found = queryStockpileField(stockpileStamp, tx, ty, spX, spY, path);

        if (found)
        {
            // Validate the destination is still a stockpile (it could have been deconstructed).
            if (!inBounds(spX, spY) || cell(spX, spY).built != TileType::Stockpile)
                found = false;
        }

        if (!found)
            found = findPathToNearestStockpile(tx, ty, spX, spY, path);

        if (!found)
        {
            // No reachable stockpile; drop what we're carrying and give up.
            dropLooseWoodNear(tx, ty, c.carryingWood);
            c.carryingWood = 0;
            cancelJob(c);
            return;
        }

        c.haulingToDropoff = true;
        c.haulDropX = spX;
        c.haulDropY = spY;

        c.targetX = spX;
        c.targetY = spY;

        c.path = std::move(path);
        c.pathIndex = 0;

        c.haulWorkRemaining = static_cast<float>(std::max(0.0, haulDropoffDurationSeconds));
        return;
    }

    // -----------------------------------------------------------------
    // Stage 2: dropoff
    // -----------------------------------------------------------------
    if (c.carryingWood <= 0)
    {
        cancelJob(c);
        return;
    }

    // If our target stopped being a stockpile (deconstructed), reroute.
    if (cell(tx, ty).built != TileType::Stockpile)
    {
        int spX = -1;
        int spY = -1;
        std::vector<colony::pf::IVec2> path;

        const std::uint32_t stockpileStamp = buildStockpileField();
        bool found = queryStockpileField(stockpileStamp, tx, ty, spX, spY, path);

        if (found)
        {
            if (!inBounds(spX, spY) || cell(spX, spY).built != TileType::Stockpile)
                found = false;
        }

        if (!found)
            found = findPathToNearestStockpile(tx, ty, spX, spY, path);

        if (found)
        {
            c.haulDropX = spX;
            c.haulDropY = spY;
            c.targetX = spX;
            c.targetY = spY;
            c.path = std::move(path);
            c.pathIndex = 0;
            c.haulWorkRemaining = static_cast<float>(std::max(0.0, haulDropoffDurationSeconds));
            return;
        }

        // No stockpile to deliver to; drop it instead.
        dropLooseWoodNear(tx, ty, c.carryingWood);
        c.carryingWood = 0;
        cancelJob(c);
        return;
    }

    // Deposit into global inventory (stockpile is the handoff point).
    m_inv.wood += c.carryingWood;
    c.carryingWood = 0;
    cancelJob(c);
}

void World::cancelJob(Colonist& c) noexcept
{
    if (c.hasJob && c.jobKind == Colonist::JobKind::BuildPlan)
    {
        // If we owned a reservation on the target tile, release it.
        if (inBounds(c.targetX, c.targetY))
        {
            Cell& t = cell(c.targetX, c.targetY);
            if (t.reservedBy == c.id)
                t.reservedBy = -1;
        }
    }

    if (c.hasJob && c.jobKind == Colonist::JobKind::Harvest)
    {
        // Release any harvest reservation we held.
        if (inBounds(c.targetX, c.targetY))
        {
            Cell& t = cell(c.targetX, c.targetY);
            if (t.farmReservedBy == c.id)
                t.farmReservedBy = -1;
        }
    }

    if (c.hasJob && c.jobKind == Colonist::JobKind::HaulWood)
    {
        // Release the pickup reservation so other haulers can take it.
        if (inBounds(c.haulPickupX, c.haulPickupY))
        {
            Cell& src = cell(c.haulPickupX, c.haulPickupY);
            if (src.looseWoodReservedBy == c.id)
                src.looseWoodReservedBy = -1;
        }

        // If the colonist is carrying wood, drop it near their current tile so it can be re-hauled.
        if (c.carryingWood > 0)
        {
            const int tx = clampi(posToTile(c.x), 0, m_w - 1);
            const int ty = clampi(posToTile(c.y), 0, m_h - 1);
            dropLooseWoodNear(tx, ty, c.carryingWood);
        }

        c.carryingWood = 0;
        c.haulingToDropoff = false;
        c.haulWorkRemaining = 0.0f;
        c.haulPickupX = 0;
        c.haulPickupY = 0;
        c.haulDropX = 0;
        c.haulDropY = 0;
    }

    c.hasJob = false;
    c.jobKind = Colonist::JobKind::None;
    c.path.clear();
    c.pathIndex = 0;
    c.eatWorkRemaining = 0.0f;
    c.harvestWorkRemaining = 0.0f;

    // Always clear hauling timers/state when a job is canceled.
    c.haulWorkRemaining = 0.0f;
    c.haulingToDropoff = false;
}

void World::applyPlanIfComplete(int targetX, int targetY) noexcept
{
    if (!inBounds(targetX, targetY))
        return;

    Cell& c = cell(targetX, targetY);

    const TileType plan = c.planned;
    if (plan == TileType::Empty || plan == c.built)
        return;

    const TileType oldBuilt = c.built;

    // "Demolish" is a plan-only marker; it resolves to an Empty built tile.
    const bool isDeconstruct = (plan == TileType::Remove);
    const TileType newBuilt = isDeconstruct ? TileType::Empty : plan;

    int woodToDrop = 0;

    // Deconstruction refund: only refund wood for tiles that were built from a plan
    // (prototype-friendly; prevents turning natural obstacles into infinite resources).
    if (isDeconstruct && oldBuilt != TileType::Empty && c.builtFromPlan)
        woodToDrop += std::max(0, TileWoodCost(oldBuilt));

    // Tree chopping yield (either demolish or building over a tree).
    if (oldBuilt == TileType::Tree && newBuilt != TileType::Tree)
        woodToDrop += std::max(0, treeChopYieldWood);

    // If the tile is about to become non-walkable, any loose wood on it needs to be pushed out.
    const int looseWoodOnTile = c.looseWood;

    // Update derived caches before overwriting the cell.
    if (oldBuilt == TileType::Farm)
        farmCacheRemove(targetX, targetY);

    builtCountAdjust(oldBuilt, newBuilt);

    // Apply the build.
    c.built = newBuilt;
    c.planned = TileType::Empty;
    c.workRemaining = 0.0f;
    c.planPriority = 0;
    c.reservedBy = -1;

    // Any build/deconstruct invalidates hauling reservations for this tile.
    c.looseWoodReservedBy = -1;

    // Track whether the current built tile was produced by a plan.
    // Natural trees are not plan-built; empty tiles are not "built."
    c.builtFromPlan = (newBuilt != TileType::Empty && newBuilt != TileType::Tree);

    // Farm state.
    if (newBuilt == TileType::Farm)
    {
        c.farmGrowth = 0.0f;
        c.farmReservedBy = -1;
        farmCacheAdd(targetX, targetY);
    }
    else
    {
        c.farmGrowth = 0.0f;
        c.farmReservedBy = -1;
    }

    // Update nav locally.
    syncNavCell(targetX, targetY);

    // If the tile is now non-walkable, push out any loose wood so it's not trapped.
    if (!TileIsWalkable(c.built) && looseWoodOnTile > 0)
    {
        adjustLooseWood(targetX, targetY, -looseWoodOnTile);
        woodToDrop += looseWoodOnTile;
    }

    if (woodToDrop > 0)
        dropLooseWoodNear(targetX, targetY, woodToDrop);

    // Remove from planned cache.
    planCacheRemove(targetX, targetY);
}

void World::rebuildPlannedCache() noexcept
{
    m_plannedCells.clear();
    m_plannedIndex.assign(static_cast<std::size_t>(m_w * m_h), -1);

    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            const Cell& c = cell(x, y);
            if (c.planned != TileType::Empty && c.planned != c.built)
                planCacheAdd(x, y);
        }
    }
}

void World::planCacheAdd(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;
    const std::size_t flat = idx(x, y);

    if (flat >= m_plannedIndex.size())
        return;

    if (m_plannedIndex[flat] != -1)
        return; // already tracked

    const int newIndex = static_cast<int>(m_plannedCells.size());
    m_plannedCells.push_back({x, y});
    m_plannedIndex[flat] = newIndex;
}

void World::planCacheRemove(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;
    const std::size_t flat = idx(x, y);

    if (flat >= m_plannedIndex.size())
        return;

    const int index = m_plannedIndex[flat];
    if (index < 0)
        return;

    const int last = static_cast<int>(m_plannedCells.size()) - 1;
    if (index != last)
    {
        const colony::pf::IVec2 moved = m_plannedCells[static_cast<std::size_t>(last)];
        m_plannedCells[static_cast<std::size_t>(index)] = moved;
        const std::size_t movedFlat = idx(moved.x, moved.y);
        if (movedFlat < m_plannedIndex.size())
            m_plannedIndex[movedFlat] = index;
    }

    m_plannedCells.pop_back();
    m_plannedIndex[flat] = -1;
}

void World::rebuildFarmCache() noexcept
{
    m_farmCells.clear();
    m_farmIndex.assign(static_cast<std::size_t>(m_w * m_h), -1);

    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            Cell& c = cell(x, y);
            c.farmReservedBy = -1;

            if (c.built == TileType::Farm)
            {
                c.farmGrowth = clampf(c.farmGrowth, 0.0f, 1.0f);
                farmCacheAdd(x, y);
            }
            else
            {
                c.farmGrowth = 0.0f;
            }
        }
    }
}

void World::farmCacheAdd(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;
    const std::size_t flat = idx(x, y);

    if (flat >= m_farmIndex.size())
        return;

    if (m_farmIndex[flat] != -1)
        return; // already tracked

    const int newIndex = static_cast<int>(m_farmCells.size());
    m_farmCells.push_back({x, y});
    m_farmIndex[flat] = newIndex;
}

void World::farmCacheRemove(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;
    const std::size_t flat = idx(x, y);

    if (flat >= m_farmIndex.size())
        return;

    const int index = m_farmIndex[flat];
    if (index < 0)
        return;

    const int last = static_cast<int>(m_farmCells.size()) - 1;
    if (index != last)
    {
        const colony::pf::IVec2 moved = m_farmCells[static_cast<std::size_t>(last)];
        m_farmCells[static_cast<std::size_t>(index)] = moved;
        const std::size_t movedFlat = idx(moved.x, moved.y);
        if (movedFlat < m_farmIndex.size())
            m_farmIndex[movedFlat] = index;
    }

    m_farmCells.pop_back();
    m_farmIndex[flat] = -1;
}



 // -----------------------------------------------------------------------------



void World::rebuildLooseWoodCache() noexcept
{
    m_looseWoodCells.clear();
    m_looseWoodIndex.assign(static_cast<std::size_t>(m_w * m_h), -1);
    m_looseWoodTotal = 0;

    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            Cell& c = cell(x, y);
            c.looseWoodReservedBy = -1;

            if (c.looseWood <= 0)
            {
                c.looseWood = 0;
                continue;
            }

            // Safety: don't allow loose wood to live on non-walkable tiles; salvage it.
            if (!TileIsWalkable(c.built))
            {
                m_inv.wood += c.looseWood;
                c.looseWood = 0;
                continue;
            }

            m_looseWoodTotal += c.looseWood;
            looseWoodCacheAdd(x, y);
        }
    }
}

void World::looseWoodCacheAdd(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;

    const int idx = y * m_w + x;
    if (idx < 0 || idx >= static_cast<int>(m_looseWoodIndex.size()))
        return;

    if (m_looseWoodIndex[idx] != -1)
        return;

    m_looseWoodIndex[idx] = static_cast<int>(m_looseWoodCells.size());
    m_looseWoodCells.push_back({x, y});
}

void World::looseWoodCacheRemove(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;

    const int idx = y * m_w + x;
    if (idx < 0 || idx >= static_cast<int>(m_looseWoodIndex.size()))
        return;

    const int pos = m_looseWoodIndex[idx];
    if (pos < 0)
        return;

    const int lastPos = static_cast<int>(m_looseWoodCells.size()) - 1;
    if (pos != lastPos)
    {
        const colony::pf::IVec2 last = m_looseWoodCells[lastPos];
        m_looseWoodCells[pos] = last;

        const int lastIdx = last.y * m_w + last.x;
        if (lastIdx >= 0 && lastIdx < static_cast<int>(m_looseWoodIndex.size()))
            m_looseWoodIndex[lastIdx] = pos;
    }

    m_looseWoodCells.pop_back();
    m_looseWoodIndex[idx] = -1;
}

void World::adjustLooseWood(int x, int y, int delta) noexcept
{
    if (!inBounds(x, y))
        return;

    if (delta == 0)
        return;

    Cell& c = cell(x, y);

    const int before = c.looseWood;
    int after = before + delta;
    if (after < 0)
        after = 0;

    if (after == before)
        return;

    c.looseWood = after;
    m_looseWoodTotal += (after - before);

    if (before <= 0 && after > 0)
    {
        // Only track reachable piles.
        if (TileIsWalkable(c.built))
            looseWoodCacheAdd(x, y);
    }
    else if (before > 0 && after <= 0)
    {
        c.looseWoodReservedBy = -1;
        looseWoodCacheRemove(x, y);
    }
}

void World::dropLooseWoodNear(int x, int y, int amount) noexcept
{
    if (amount <= 0)
        return;

    // If the map has no stockpiles yet, keep the early game playable by falling
    // back to the legacy behavior (direct-to-inventory).
    if (builtCount(TileType::Stockpile) <= 0)
    {
        m_inv.wood += amount;
        return;
    }

    auto canDropAt = [&](int tx, int ty) -> bool {
        if (!inBounds(tx, ty))
            return false;

        const Cell& c = cell(tx, ty);
        if (!TileIsWalkable(c.built))
            return false;

        // Avoid dropping onto active (non-trivial) plans so we don't immediately trap the pile.
        if (c.planned != TileType::Empty && c.planned != c.built)
            return false;

        return true;
    };

    // Search a small neighborhood; prefer the original tile.
    const int maxR = 4;
    for (int r = 0; r <= maxR; ++r)
    {
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                const int tx = x + dx;
                const int ty = y + dy;

                if (!canDropAt(tx, ty))
                    continue;

                adjustLooseWood(tx, ty, amount);
                return;
            }
        }
    }

    // Fallback: shouldn't happen often; preserve resources rather than deleting them.
    m_inv.wood += amount;
}

} // namespace colony::proto
