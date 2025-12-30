#include "game/proto/ProtoWorld.h"

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

// -----------------------------------------------------------------------------
// Colonist role helpers (prototype)
// -----------------------------------------------------------------------------

[[nodiscard]] bool HasCap(const colony::proto::Colonist& c, Capability cap) noexcept
{
    return HasAny(c.role.caps(), cap);
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
    default: return "?";
    }
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
    default: return 0;
    }
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

    // Border walls for orientation.
    for (int x = 0; x < m_w; ++x)
    {
        cell(x, 0).built = TileType::Wall;
        cell(x, m_h - 1).built = TileType::Wall;
    }
    for (int y = 0; y < m_h; ++y)
    {
        cell(0, y).built = TileType::Wall;
        cell(m_w - 1, y).built = TileType::Wall;
    }

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

            Cell& c = cell(x, y);
            if (c.built != TileType::Empty)
                continue;

            c.built = TileType::Tree;
            c.builtFromPlan = false;
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

        const float maxPersonalFood = static_cast<float>(std::max(0.0, colonistMaxPersonalFood));
        c.personalFood = maxPersonalFood;
        c.jobKind = Colonist::JobKind::None;
    
if (c.hasJob && c.jobKind == Colonist::JobKind::HaulWood)
{
    // Release the pickup reservation so other haulers can take it.
    if (inBounds(c.haulPickupX, c.haulPickupY))
    {
        Cell& src = cell(c.haulPickupX, c.haulPickupY);
        if (src.looseWoodReservedBy == c.id)
            src.looseWoodReservedBy = -1;
    }

    // If the colonist is carrying wood, drop it near their current tile.
    if (c.carryingWood > 0)
    {
        const int tx = clampi(posToTile(c.x), 0, m_w - 1);
        const int ty = clampi(posToTile(c.y), 0, m_h - 1);
        dropLooseWoodNear(tx, ty, c.carryingWood);
        c.carryingWood = 0;
    }

    c.haulingToDropoff = false;
    c.haulWorkRemaining = 0.0f;
}

    c.hasJob = false;
        c.eatWorkRemaining = 0.0f;
        c.harvestWorkRemaining = 0.0f;

    c.haulWorkRemaining = 0.0f;
    c.carryingWood = 0;
    c.haulingToDropoff = false;

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

    // Allow job assignment immediately after a reset.
    m_jobAssignCooldown = 0.0;
    m_harvestAssignCooldown = 0.0;
    m_haulAssignCooldown = 0.0;
    m_treeSpreadAccum = 0.0;
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
    cancelJob(*c);
    return OrderResult::Ok;
}

OrderResult World::OrderColonistMove(int colonistId, int targetX, int targetY) noexcept
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

    cancelJob(*c);

    c->targetX = targetX;
    c->targetY = targetY;
    c->jobKind = Colonist::JobKind::ManualMove;
    c->hasJob = true;
    c->path.clear();
    c->pathIndex = 0;

    if (!computePathToTile(*c, targetX, targetY))
    {
        cancelJob(*c);
        return OrderResult::NoPath;
    }
    return OrderResult::Ok;
}

OrderResult World::OrderColonistBuild(int colonistId, int planX, int planY) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;
    if (!c->drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(planX, planY))
        return OrderResult::OutOfBounds;

    Cell& target = cell(planX, planY);
    if (target.planned == TileType::Empty || target.planned == target.built)
        return OrderResult::TargetNotPlanned;
    if (target.reservedBy != -1 && target.reservedBy != colonistId)
        return OrderResult::TargetReserved;

    cancelJob(*c);

    const int sx = posToTile(c->x);
    const int sy = posToTile(c->y);

    std::vector<colony::pf::IVec2> p;
    if (!computePathToAdjacentFrom(sx, sy, planX, planY, p))
    {
        cancelJob(*c);
        return OrderResult::NoPath;
    }

    target.reservedBy = colonistId;
    c->targetX = planX;
    c->targetY = planY;
    c->jobKind = Colonist::JobKind::BuildPlan;
    c->hasJob = true;
    c->path = std::move(p);
    c->pathIndex = 0;
    return OrderResult::Ok;
}

OrderResult World::OrderColonistHarvest(int colonistId, int farmX, int farmY) noexcept
{
    Colonist* c = findColonistById(colonistId);
    if (!c)
        return OrderResult::InvalidColonist;
    if (!c->drafted)
        return OrderResult::NotDrafted;
    if (!inBounds(farmX, farmY))
        return OrderResult::OutOfBounds;

    Cell& farm = cell(farmX, farmY);
    if (farm.built != TileType::Farm)
        return OrderResult::TargetNotFarm;
    if (farm.farmGrowth < 1.0f)
        return OrderResult::TargetNotHarvestable;
    if (farm.farmReservedBy != -1 && farm.farmReservedBy != colonistId)
        return OrderResult::TargetReserved;

    cancelJob(*c);

    const int sx = posToTile(c->x);
    const int sy = posToTile(c->y);

    std::vector<colony::pf::IVec2> p;
    if (!computePathToAdjacentFrom(sx, sy, farmX, farmY, p))
    {
        cancelJob(*c);
        return OrderResult::NoPath;
    }

    farm.farmReservedBy = colonistId;
    c->targetX = farmX;
    c->targetY = farmY;
    c->jobKind = Colonist::JobKind::Harvest;
    c->hasJob = true;
    c->path = std::move(p);
    c->pathIndex = 0;
    c->harvestWorkRemaining = static_cast<float>(std::max(0.0, farmHarvestDurationSeconds));
    return OrderResult::Ok;
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
    m_treeSpreadAccum = 0.0;
}

void World::CancelAllJobsAndClearReservations() noexcept
{
    // Clear reservations first so any stale reservation markers are removed.
    for (Cell& c : m_cells)
    {
        c.reservedBy = -1;
        c.farmReservedBy = -1;
    }

    for (Colonist& c : m_colonists)
        cancelJob(c);

    // Allow immediate re-assignment after bulk edits (undo/redo, clear plans, load).
    m_jobAssignCooldown = 0.0;
    m_harvestAssignCooldown = 0.0;
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
}

void World::syncNavCell(int x, int y) noexcept
{
    if (!inBounds(x, y))
        return;

    const TileType built = cell(x, y).built;

    // NOTE: passable is an int in the public API.
    m_nav.set_walkable(x, y, TileIsWalkable(built) ? 1 : 0);
}

void World::syncAllNav() noexcept
{
    for (int y = 0; y < m_h; ++y)
        for (int x = 0; x < m_w; ++x)
            syncNavCell(x, y);
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

        if (!findPathToNearestFoodSource(sx, sy, foodX, foodY, path))
        {
            // No stockpiles/farms yet (or none reachable). Fall back to eating in-place.
            foodX = sx;
            foodY = sy;
            path.clear();
            path.push_back({sx, sy});
        }

        c.hasJob = true;
        c.jobKind = Colonist::JobKind::Eat;
        c.targetX = foodX;
        c.targetY = foodY;
        c.path = std::move(path);
        c.pathIndex = 0;
        c.eatWorkRemaining = static_cast<float>(std::max(0.0, colonistEatDurationSeconds));
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

        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
            continue;

        int farmX = -1;
        int farmY = -1;
        std::vector<colony::pf::IVec2> path;

        if (!findPathToNearestHarvestableFarm(sx, sy, farmX, farmY, path))
            continue;

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
        c.harvestWorkRemaining = static_cast<float>(std::max(0.0, farmHarvestDurationSeconds));

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
            anyIdleBuilder = true;
            break;
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

        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        // If we're currently on an invalid tile (should not happen), idle.
        if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
            continue;

        int targetX = -1;
        int targetY = -1;
        std::vector<colony::pf::IVec2> path;

        bool found = false;
        // Prefer higher priority plans, then nearest within that priority.
        // (Priority range is small; attempting in descending order is cheap and stable.)
        for (int pr = 3; pr >= 0; --pr)
        {
            if (!anyUnreservedAtPriority[static_cast<std::size_t>(pr)])
                continue;

            if (findPathToNearestAvailablePlan(sx, sy, targetX, targetY, path, pr))
            {
                found = true;
                break;
            }
        }

        // Fallback: if we failed to find a reachable plan of any known priority,
        // do a last attempt without filtering (handles corrupted saves/custom tools).
        if (!found)
            found = findPathToNearestAvailablePlan(sx, sy, targetX, targetY, path, /*requiredPriority=*/-1);

        if (!found)
            continue;

        // Reserve the plan for this colonist.
        Cell& target = cell(targetX, targetY);
        target.reservedBy = c.id;

        c.hasJob = true;
        c.jobKind = Colonist::JobKind::BuildPlan;
        c.targetX = targetX;
        c.targetY = targetY;
        c.path = std::move(path);
        c.pathIndex = 0;
    }
}



void World::assignHaulJobs(double dtSeconds)
{
    if (m_looseWoodCells.empty() || m_looseWoodTotal <= 0)
        return;

    // Without any stockpiles, hauling has nowhere to deliver to.
    if (builtCount(TileType::Stockpile) <= 0)
        return;

    // Throttle pathfinding work.
    m_haulAssignCooldown -= dtSeconds;
    if (m_haulAssignCooldown > 0.0)
        return;
    m_haulAssignCooldown = 0.15;

    const float eatThreshold = static_cast<float>(hungerEatThreshold);

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

        int woodX = -1;
        int woodY = -1;
        std::vector<colony::pf::IVec2> path;
        if (!findPathToNearestLooseWood(posToTile(c.x), posToTile(c.y), woodX, woodY, path))
            continue;

        if (!inBounds(woodX, woodY))
            continue;

        Cell& src = cell(woodX, woodY);
        if (src.looseWood <= 0 || src.looseWoodReservedBy != -1)
            continue;

        // Reserve the stack so multiple haulers don't target the same tile.
        src.looseWoodReservedBy = c.id;

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

    if (!inBounds(targetX, targetY))
        return false;

    if (!inBounds(startX, startY))
        return false;

    colony::pf::AStar astar(m_nav);

    std::vector<colony::pf::IVec2> best;
    std::size_t bestLen = std::numeric_limits<std::size_t>::max();

    // Choose any adjacent walkable tile as the work position.
    constexpr int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (const auto& d : kDirs)
    {
        const int nx = targetX + d[0];
        const int ny = targetY + d[1];

        if (!inBounds(nx, ny))
            continue;
        if (!m_nav.passable(nx, ny))
            continue;

        const colony::pf::Path p = astar.find_path({startX, startY}, {nx, ny});
        if (p.empty())
            continue;

        if (p.points.size() < bestLen)
        {
            bestLen = p.points.size();
            best = p.points;
        }
    }

    if (best.empty())
        return false;

    outPath.swap(best);
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

    if (!inBounds(targetX, targetY))
        return false;
    if (!inBounds(startX, startY))
        return false;
    if (!m_nav.passable(targetX, targetY))
        return false;

    colony::pf::AStar astar(m_nav);
    const colony::pf::Path p = astar.find_path({startX, startY}, {targetX, targetY});
    if (p.empty())
        return false;

    outPath = p.points;
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
    const float speed     = baseSpeed * EffectiveMoveMult(c);

    while (c.pathIndex < c.path.size())
    {
        const colony::pf::IVec2 p = c.path[c.pathIndex];
        const float goalX = static_cast<float>(p.x) + 0.5f;
        const float goalY = static_cast<float>(p.y) + 0.5f;

        const float dx = goalX - c.x;
        const float dy = goalY - c.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 1.0e-3f)
        {
            ++c.pathIndex;
            continue;
        }

        const float step = speed * static_cast<float>(dtSeconds);
        if (step >= dist)
        {
            c.x = goalX;
            c.y = goalY;
            ++c.pathIndex;
            continue;
        }

        c.x += dx / dist * step;
        c.y += dy / dist * step;
        break;
    }

    // Player move orders complete when we reach the final path node.
    if (c.jobKind == Colonist::JobKind::ManualMove && c.pathIndex >= c.path.size())
        cancelJob(c);
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

        // Find a stockpile to deliver to.
        int spX = -1;
        int spY = -1;
        std::vector<colony::pf::IVec2> path;
        if (!findPathToNearestStockpile(tx, ty, spX, spY, path))
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
        if (findPathToNearestStockpile(tx, ty, spX, spY, path))
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

    c.hasJob = false;
    c.jobKind = Colonist::JobKind::None;
    c.path.clear();
    c.pathIndex = 0;
    c.eatWorkRemaining = 0.0f;
    c.harvestWorkRemaining = 0.0f;
}

void World::applyPlanIfComplete(int targetX, int targetY) noexcept
{
    if (!inBounds(targetX, targetY))
        return;

    Cell& c = cell(targetX, targetY);

    const TileType newBuilt = c.planned;
    if (newBuilt == TileType::Empty || newBuilt == c.built)
        return;

    int woodToDrop = 0;

    // If the plan is just "remove" then we refund the wood cost (if any).
    // This is a prototype; we only refund wood for tiles that were built from a plan,
    // so natural trees don't become an infinite wood machine.
    if (newBuilt == TileType::Empty && c.builtFromPlan)
    {
        woodToDrop += std::max(0, TileWoodCost(c.built));
    }

    // If we chopped a tree, yield some wood (tuning). Wood becomes a loose resource
    // that must be hauled to a stockpile before it is usable.
    if (c.built == TileType::Tree && newBuilt != TileType::Tree)
    {
        woodToDrop += std::max(0, treeChopYieldWood);
    }

    // Clear previous built counts.
    if (c.built != TileType::Empty)
    {
        --m_builtCounts[static_cast<int>(c.built)];
        if (c.built == TileType::Farm)
            farmCacheRemove(targetX, targetY);
    }

    // Apply.
    c.built = newBuilt;
    c.planned = TileType::Empty;
    c.workRemaining = 0.0f;
    c.planPriority = 0;
    c.reservedBy = -1;
    c.builtFromPlan = true;

    // Applying a plan invalidates any hauling reservation on this tile.
    c.looseWoodReservedBy = -1;

    if (c.built != TileType::Farm)
    {
        c.farmGrowth = 0.0f;
        c.farmReservedBy = -1;
    }

    // Update built counts.
    ++m_builtCounts[static_cast<int>(c.built)];
    if (c.built == TileType::Farm)
        farmCacheAdd(targetX, targetY);

    // Rebuild nav locally.
    syncNavAt(targetX, targetY);

    // If the tile is now non-walkable, push out any loose wood so it's not trapped.
    if (!TileIsWalkable(c.built) && c.looseWood > 0)
    {
        const int stuck = c.looseWood;
        adjustLooseWood(targetX, targetY, -stuck);
        woodToDrop += stuck;
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
