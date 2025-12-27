#include "game/editor/PlanHistory.h"

#include <algorithm>

namespace colony::game::editor {

namespace {

constexpr int kWoodSentinel = 1'000'000'000;

} // namespace

void PlanHistory::Clear() noexcept
{
    m_active.reset();
    m_undo.clear();
    m_redo.clear();
}

void PlanHistory::BeginCommand(int woodBefore)
{
    m_active.emplace();
    m_active->woodBefore = woodBefore;
    m_active->woodAfter  = woodBefore;
    m_active->edits.clear();
}

void PlanHistory::RecordChange(int x, int y, TileSnapshot before, TileSnapshot after)
{
    if (!m_active)
        return;

    // Merge duplicates: keep the first "before", update the final "after".
    for (TileEdit& e : m_active->edits)
    {
        if (e.x == x && e.y == y)
        {
            e.after = after;
            return;
        }
    }

    TileEdit e;
    e.x = x;
    e.y = y;
    e.before = before;
    e.after = after;
    m_active->edits.push_back(std::move(e));
}

bool PlanHistory::CommitCommand(int woodAfter)
{
    if (!m_active)
        return false;

    m_active->woodAfter = woodAfter;

    if (m_active->edits.empty())
    {
        m_active.reset();
        return false;
    }

    // New commit invalidates redo.
    m_redo.clear();

    m_undo.push_back(std::move(*m_active));
    m_active.reset();

    // Trim oldest.
    if (m_undo.size() > m_maxCommands)
        m_undo.erase(m_undo.begin(), m_undo.begin() + static_cast<std::ptrdiff_t>(m_undo.size() - m_maxCommands));

    return true;
}

void PlanHistory::CancelCommand() noexcept
{
    m_active.reset();
}

bool PlanHistory::Undo(colony::proto::World& world)
{
    if (m_undo.empty())
        return false;

    Command cmd = std::move(m_undo.back());
    m_undo.pop_back();

    const bool ok = apply(world, cmd, /*useAfter=*/false);
    if (ok)
        m_redo.push_back(std::move(cmd));

    return ok;
}

bool PlanHistory::Redo(colony::proto::World& world)
{
    if (m_redo.empty())
        return false;

    Command cmd = std::move(m_redo.back());
    m_redo.pop_back();

    const bool ok = apply(world, cmd, /*useAfter=*/true);
    if (ok)
        m_undo.push_back(std::move(cmd));

    return ok;
}

bool PlanHistory::apply(colony::proto::World& world, const Command& cmd, bool useAfter)
{
    if (cmd.edits.empty())
        return false;

    // We use World::placePlan (to keep caches consistent) but force success by
    // temporarily giving ourselves "infinite" wood. We then restore the final
    // wood count from the command.
    auto& inv = world.inventory();
    const int desiredWood = useAfter ? cmd.woodAfter : cmd.woodBefore;

    const int oldWood = inv.wood;
    inv.wood = kWoodSentinel;

    for (const TileEdit& e : cmd.edits)
    {
        if (!world.inBounds(e.x, e.y))
            continue;

        const TileSnapshot s = useAfter ? e.after : e.before;
        (void)world.placePlan(e.x, e.y, s.planned, s.planPriority);

        // Restore workRemaining exactly (placePlan resets it to a default).
        colony::proto::Cell& c = world.cell(e.x, e.y);
        c.planPriority = s.planPriority;
        c.workRemaining = s.workRemaining;
        c.reservedBy = -1;
    }

    inv.wood = desiredWood;

    // Force re-assignment.
    world.CancelAllJobsAndClearReservations();

    // (Just in case) avoid restoring the sentinel if something went wrong.
    if (inv.wood == kWoodSentinel)
        inv.wood = oldWood;

    return true;
}

} // namespace colony::game::editor
