#include "game/PrototypeGame_Impl.h"

#include <string>

namespace colony::game {

void PrototypeGame::Impl::clearPlanHistory() noexcept
{
    planHistory.Clear();
    lastPaintX = std::numeric_limits<int>::min();
    lastPaintY = std::numeric_limits<int>::min();
    rectPaintActive = false;
}

bool PrototypeGame::Impl::undoPlans()
{
    if (!planHistory.CanUndo())
        return false;

    const bool ok = planHistory.Undo(world);
    if (ok)
    {
        setStatus("Undo", 1.5f);
        lastPaintX = std::numeric_limits<int>::min();
        lastPaintY = std::numeric_limits<int>::min();
        rectPaintActive = false;
    }
    return ok;
}

bool PrototypeGame::Impl::redoPlans()
{
    if (!planHistory.CanRedo())
        return false;

    const bool ok = planHistory.Redo(world);
    if (ok)
    {
        setStatus("Redo", 1.5f);
        lastPaintX = std::numeric_limits<int>::min();
        lastPaintY = std::numeric_limits<int>::min();
        rectPaintActive = false;
    }
    return ok;
}

} // namespace colony::game
