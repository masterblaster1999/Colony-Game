#include "game/PrototypeGame.h"

#include "loop/DebugCamera.h"

#include <new>

namespace colony::game {

struct PrototypeGame::Impl {
    colony::appwin::DebugCameraController camera;
};

PrototypeGame::PrototypeGame()
{
    m_impl = new (std::nothrow) Impl{};
}

PrototypeGame::~PrototypeGame()
{
    delete m_impl;
    m_impl = nullptr;
}

bool PrototypeGame::OnInput(std::span<const colony::input::InputEvent> events) noexcept
{
    if (!m_impl)
        return false;

    bool changed = false;

    for (const auto& ev : events)
    {
        using colony::input::InputEventType;
        using colony::input::MouseButtonsMask;

        switch (ev.type)
        {
        case InputEventType::MouseDelta:
        {
            const bool orbit = (ev.buttons & MouseButtonsMask::MouseLeft) != 0;
            const bool pan   = (ev.buttons & (MouseButtonsMask::MouseMiddle | MouseButtonsMask::MouseRight)) != 0;

            if (m_impl->camera.ApplyDrag(static_cast<long>(ev.dx), static_cast<long>(ev.dy), orbit, pan)) {
                changed = true;
            }
            break;
        }

        case InputEventType::MouseWheel:
            if (m_impl->camera.ApplyWheelDetents(static_cast<int>(ev.wheelDetents))) {
                changed = true;
            }
            break;

        default:
            // Ignore for now.
            break;
        }
    }

    return changed;
}

DebugCameraInfo PrototypeGame::GetDebugCameraInfo() const noexcept
{
    DebugCameraInfo out{};
    if (!m_impl)
        return out;

    const auto& s = m_impl->camera.State();
    out.yaw = s.yaw;
    out.pitch = s.pitch;
    out.panX = s.panX;
    out.panY = s.panY;
    out.zoom = s.zoom;
    return out;
}

} // namespace colony::game
