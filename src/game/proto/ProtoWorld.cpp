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
    default: return "?";
    }
}

bool TileIsWalkable(TileType t) noexcept
{
    return t != TileType::Wall;
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

    // Allow job assignment immediately after a reset.
    m_jobAssignCooldown = 0.0;
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

PlacePlanResult World::placePlan(int x, int y, TileType plan, std::uint8_t planPriority)
{
    if (!inBounds(x, y))
        return PlacePlanResult::OutOfBounds;

    Cell& c = cell(x, y);

    // Clamp priority into the supported range (0..3).
    if (planPriority > 3u)
        planPriority = 3u;

    const bool wasActivePlan = (c.planned != TileType::Empty && c.planned != c.built);

    // Special-case: 'Empty' means "clear any existing plan" (not "plan to demolish built tiles").
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
}

void World::CancelAllJobsAndClearReservations() noexcept
{
    // Clear reservations first so any stale reservedBy markers are removed.
    for (Cell& c : m_cells)
        c.reservedBy = -1;

    for (Colonist& c : m_colonists)
        cancelJob(c);

    // Allow immediate re-assignment after bulk edits (undo/redo, clear plans, load).
    m_jobAssignCooldown = 0.0;
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

    // Resource simulation (very rough).
    {
        const int farms = builtCount(TileType::Farm);
        m_inv.food += static_cast<float>(farmFoodPerSecond * static_cast<double>(farms) * dtSeconds);

        const double eat = foodPerColonistPerSecond * static_cast<double>(m_colonists.size()) * dtSeconds;
        m_inv.food = clampf(m_inv.food - static_cast<float>(eat), 0.0f, 1.0e9f);
    }

    // Job assignment before stepping.
    assignJobs(dtSeconds);

    for (Colonist& c : m_colonists)
    {
        stepColonist(c, dtSeconds);
        stepConstructionIfReady(c, dtSeconds);
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

    // If nobody is idle, clear the throttle so the next idle colonist is assigned immediately.
    bool anyIdle = false;
    for (const Colonist& c : m_colonists)
    {
        if (!c.hasJob)
        {
            anyIdle = true;
            break;
        }
    }
    if (!anyIdle)
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
        c.targetX = targetX;
        c.targetY = targetY;
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

    // If the plan was deleted/finished, drop the job.
    const Cell& t = cell(c.targetX, c.targetY);
    if (t.planned == TileType::Empty || t.planned == t.built)
    {
        cancelJob(c);
        return;
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
        // Try to recompute.
        if (!computePathToAdjacent(c, c.targetX, c.targetY))
        {
            // Can't reach currently; leave unreserved so another colonist might.
            Cell& target = cell(c.targetX, c.targetY);
            if (target.reservedBy == c.id)
                target.reservedBy = -1;
            cancelJob(c);
        }
        return;
    }

    // Walk along the path.
    const float speed = static_cast<float>(std::max(0.1, colonistWalkSpeed));

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
}

void World::stepConstructionIfReady(Colonist& c, double dtSeconds)
{
    if (!c.hasJob)
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

    const double work = std::max(0.05, buildWorkPerSecond) * dtSeconds;
    target.workRemaining -= static_cast<float>(work);

    applyPlanIfComplete(c.targetX, c.targetY);

    // If completed, drop job.
    if (target.planned == TileType::Empty || target.planned == target.built)
        cancelJob(c);
}

void World::cancelJob(Colonist& c) noexcept
{
    if (c.hasJob)
    {
        // If we owned a reservation on the target tile, release it.
        if (inBounds(c.targetX, c.targetY))
        {
            Cell& t = cell(c.targetX, c.targetY);
            if (t.reservedBy == c.id)
                t.reservedBy = -1;
        }
    }

    c.hasJob = false;
    c.path.clear();
    c.pathIndex = 0;
}

void World::applyPlanIfComplete(int targetX, int targetY) noexcept
{
    if (!inBounds(targetX, targetY))
        return;

    Cell& c = cell(targetX, targetY);
    if (c.planned == TileType::Empty || c.planned == c.built)
        return;

    if (c.workRemaining > 0.0f)
        return;

    // Commit.
    const TileType oldBuilt = c.built;
    const TileType newBuilt = c.planned;
    c.built = newBuilt;
    c.planned = TileType::Empty;
    c.planPriority = 0;
    c.workRemaining = 0.0f;
    c.reservedBy = -1;

    // Remove from active plan cache.
    planCacheRemove(targetX, targetY);

    // Update built-count cache.
    if (oldBuilt != newBuilt)
        builtCountAdjust(oldBuilt, newBuilt);

    syncNavCell(targetX, targetY);
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



// -----------------------------------------------------------------------------


} // namespace colony::proto
