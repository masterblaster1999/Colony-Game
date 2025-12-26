#include "game/PrototypeGame.h"

#include "game/PrototypeGame_Impl.h"

namespace colony::game {

PrototypeGame::PrototypeGame()
    : m_impl(std::make_unique<Impl>())
{
}

PrototypeGame::~PrototypeGame() = default;

bool PrototypeGame::OnInput(std::span<const colony::input::InputEvent> events,
                            bool uiWantsKeyboard,
                            bool uiWantsMouse) noexcept
{
    return m_impl->OnInput(events, uiWantsKeyboard, uiWantsMouse);
}

bool PrototypeGame::Update(float dtSeconds, bool uiWantsKeyboard, bool uiWantsMouse) noexcept
{
    return m_impl->Update(dtSeconds, uiWantsKeyboard, uiWantsMouse);
}

void PrototypeGame::DrawUI() noexcept
{
    m_impl->DrawUI();
}

void PrototypeGame::TogglePanels() noexcept
{
    m_impl->showPanels = !m_impl->showPanels;
}

void PrototypeGame::ToggleHelp() noexcept
{
    m_impl->showHelp = !m_impl->showHelp;
}

void PrototypeGame::ResetWorld() noexcept
{
    m_impl->resetWorld();
}

DebugCameraInfo PrototypeGame::GetDebugCameraInfo() const noexcept
{
    const DebugCameraState& s = m_impl->camera.State();

    DebugCameraInfo out;
    out.yaw   = s.yaw;
    out.pitch = s.pitch;
    out.panX  = s.panX;
    out.panY  = s.panY;
    out.zoom  = s.zoom;
    return out;
}

} // namespace colony::game
