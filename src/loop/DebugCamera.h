#pragma once

#include <cstdint>

namespace colony::appwin {

struct DebugCameraState {
    float yaw   = 0.f;
    float pitch = 0.f;
    float panX  = 0.f;
    float panY  = 0.f;
    float zoom  = 1.f;
};

// Tiny "placeholder" camera controller used by the current AppWindow prototype.
//
// It intentionally does NOT depend on the rest of the engine/game so it can be
// replaced later by your real camera + input system.
class DebugCameraController {
public:
    // Zoom clamp for the prototype camera.
    //
    // NOTE:
    // The World ImGui view also clamps to the same range so zooming via wheel
    // and setting zoom directly (e.g. UI) behave consistently.
    static constexpr float kMinZoom = 0.05f;
    static constexpr float kMaxZoom = 20.0f;

    // Apply a drag delta.
    //  - orbit = left button (yaw/pitch)
    //  - pan   = middle OR right button
    // Returns true if the state changed.
    bool ApplyDrag(long dx, long dy, bool orbit, bool pan) noexcept;

    // Apply wheel detents (positive or negative). Returns true if state changed.
    bool ApplyWheelDetents(int detents) noexcept;

    // Apply a direct pan delta in the controller's "world" units.
    // Useful for keyboard movement (WASD) without fabricating pixel deltas.
    bool ApplyPan(float dx, float dy) noexcept;

    // Apply a multiplicative zoom factor (1.0f = no change).
    // Useful for continuous keyboard zoom (Q/E).
    bool ApplyZoomFactor(float factor) noexcept;

    // Set absolute zoom (useful for UI-controlled zoom).
    bool SetZoom(float zoom) noexcept;

    [[nodiscard]] const DebugCameraState& State() const noexcept { return m_state; }

private:
    void ClampPitch() noexcept;

    DebugCameraState m_state{};
};

} // namespace colony::appwin
