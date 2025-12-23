#include "loop/DebugCamera.h"

namespace colony::appwin {

void DebugCameraController::ClampPitch() noexcept
{
    if (m_state.pitch > 89.f)
        m_state.pitch = 89.f;
    if (m_state.pitch < -89.f)
        m_state.pitch = -89.f;
}

bool DebugCameraController::ApplyDrag(long dx, long dy, bool orbit, bool pan) noexcept
{
    if (dx == 0 && dy == 0)
        return false;

    if (orbit)
    {
        // LMB drag = orbit
        m_state.yaw   += static_cast<float>(dx) * 0.15f;
        m_state.pitch += static_cast<float>(dy) * 0.15f;
        ClampPitch();
        return true;
    }

    if (pan)
    {
        // MMB/RMB drag = pan
        m_state.panX += static_cast<float>(dx) * 0.02f;
        m_state.panY += static_cast<float>(dy) * 0.02f;
        return true;
    }

    return false;
}

bool DebugCameraController::ApplyWheelDetents(int detents) noexcept
{
    if (detents == 0)
        return false;

    m_state.zoom *= (1.0f + 0.10f * static_cast<float>(detents));
    if (m_state.zoom < 0.1f) m_state.zoom = 0.1f;
    if (m_state.zoom > 10.f) m_state.zoom = 10.f;
    return true;
}

} // namespace colony::appwin
