#include "game/proto/ProtoWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

PlacePlanResult World::placePlan(int x, int y, TileType plan)
{
    if (!inBounds(x, y))
        return PlacePlanResult::OutOfBounds;

    Cell& c = cell(x, y);

    // Treat "planning to the already-built" state as a no-op.
    const TileType oldPlan = (c.planned == TileType::Empty) ? c.built : c.planned;

    if (oldPlan == plan)
        return PlacePlanResult::NoChange;

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
        c.workRemaining = 0.0f;
        c.reservedBy = -1;
    }
    else
    {
        c.planned = plan;
        c.workRemaining = TileBuildTimeSeconds(plan);
        c.reservedBy = -1;
    }

    return PlacePlanResult::Ok;
}

void World::clearAllPlans()
{
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
            c.workRemaining = 0.0f;
            c.reservedBy = -1;
        }
    }

    for (Colonist& c : m_colonists)
        cancelJob(c);
}

int World::plannedCount() const noexcept
{
    int n = 0;
    for (const Cell& c : m_cells)
    {
        if (c.planned != TileType::Empty && c.planned != c.built)
            ++n;
    }
    return n;
}

int World::builtCount(TileType t) const noexcept
{
    int n = 0;
    for (const Cell& c : m_cells)
    {
        if (c.built == t)
            ++n;
    }
    return n;
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
    assignJobs();

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

void World::assignJobs()
{
    // Fast exit.
    if (plannedCount() == 0)
        return;

    for (Colonist& c : m_colonists)
    {
        if (c.hasJob)
            continue;

        const int sx = static_cast<int>(std::floor(c.x));
        const int sy = static_cast<int>(std::floor(c.y));

        // If we're currently on an invalid tile (should not happen), idle.
        if (!inBounds(sx, sy) || !m_nav.passable(sx, sy))
            continue;

        int bestX = -1;
        int bestY = -1;
        std::vector<colony::pf::IVec2> bestPath;
        std::size_t bestLen = std::numeric_limits<std::size_t>::max();

        // Naive scan: find the closest reachable plan.
        // (Good enough for a small proto grid.)
        for (int y = 0; y < m_h; ++y)
        {
            for (int x = 0; x < m_w; ++x)
            {
                Cell& cellRef = cell(x, y);
                if (cellRef.planned == TileType::Empty || cellRef.planned == cellRef.built)
                    continue;
                if (cellRef.reservedBy != -1)
                    continue;

                Colonist tmp = c;
                if (!computePathToAdjacent(tmp, x, y))
                    continue;

                const std::size_t len = tmp.path.size();
                if (len == 0)
                    continue;

                if (len < bestLen)
                {
                    bestLen = len;
                    bestX = x;
                    bestY = y;
                    bestPath = std::move(tmp.path);

                    // Early-out if it's basically right here.
                    if (bestLen <= 2)
                        break;
                }
            }
            if (bestLen <= 2)
                break;
        }

        if (bestX < 0)
            continue;

        // Reserve the plan for this colonist.
        Cell& target = cell(bestX, bestY);
        target.reservedBy = c.id;

        c.hasJob = true;
        c.targetX = bestX;
        c.targetY = bestY;
        c.path = std::move(bestPath);
        c.pathIndex = 0;
    }
}

bool World::computePathToAdjacent(Colonist& c, int targetX, int targetY)
{
    c.path.clear();
    c.pathIndex = 0;

    if (!inBounds(targetX, targetY))
        return false;

    const int sx = static_cast<int>(std::floor(c.x));
    const int sy = static_cast<int>(std::floor(c.y));

    if (!inBounds(sx, sy))
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

        const colony::pf::Path p = astar.find_path({sx, sy}, {nx, ny});
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

    c.path = std::move(best);
    c.pathIndex = 0;
    return true;
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
    c.built = c.planned;
    c.planned = TileType::Empty;
    c.workRemaining = 0.0f;
    c.reservedBy = -1;

    syncNavCell(targetX, targetY);
}

} // namespace colony::proto
