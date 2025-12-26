#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <cmath>

namespace colony::game {

bool PrototypeGame::Impl::updateCameraKeyboard(float dtSeconds, bool uiWantsKeyboard) noexcept
{
    if (uiWantsKeyboard)
        return false;

    bool cameraChanged = false;

    const float zoom = std::max(0.25f, camera.State().zoom);
    float moveSpeed  = 20.0f / zoom;

    // Speed boost modifier (Shift by default).
    if (input.IsDown(colony::input::Action::SpeedBoost))
        moveSpeed *= 3.0f;

    const colony::input::MovementAxes axes = input.GetMovementAxes();

    if (axes.x != 0.f || axes.y != 0.f)
        cameraChanged |= camera.ApplyPan(axes.x * moveSpeed * dtSeconds, axes.y * moveSpeed * dtSeconds);

    // For the prototype, we use vertical movement (Q/E) as a continuous zoom.
    if (axes.z != 0.f) {
        const float zoomSpeed = 1.0f;
        cameraChanged |= camera.ApplyZoomFactor(std::pow(2.0f, axes.z * zoomSpeed * dtSeconds));
    }

    return cameraChanged;
}

} // namespace colony::game
