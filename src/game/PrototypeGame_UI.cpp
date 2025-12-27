#include "game/PrototypeGame_Impl.h"

namespace colony::game {

void PrototypeGame::Impl::DrawUI() noexcept
{
#if defined(COLONY_WITH_IMGUI)
    drawUI();
#else
    (void)this;
#endif
}

#if defined(COLONY_WITH_IMGUI)

void PrototypeGame::Impl::drawUI()
{
    // World first so it can occupy the available space.
    drawWorldWindow();

    // Auxiliary panels/tools.
    drawPanelsWindow();
    drawBindingsEditorWindow();
    drawHelpWindow();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
