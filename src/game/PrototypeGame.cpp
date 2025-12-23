#include "game/PrototypeGame.h"

#include "input/InputMapper.h"
#include "loop/DebugCamera.h"

#include <chrono>
#include <cmath>
#include <new>

namespace colony::game {

struct PrototypeGame::Impl {
    colony::appwin::DebugCameraController camera;
    colony::input::InputMapper            mapper;

    // For frame-rate independent keyboard movement.
    std::chrono::steady_clock::time_point lastTick{};
    bool                                  hasLastTick = false;
    float                                 keyboardTitleAccum = 0.f;
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

    // Compute a conservative dt (this is a prototype loop; we don't have a proper
    // simulation clock yet). Clamp to avoid huge jumps after pauses/minimize.
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    float dt = 0.f;
    if (m_impl->hasLastTick) {
        dt = std::chrono::duration<float>(now - m_impl->lastTick).count();
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.f) dt = 0.f;
    }
    m_impl->lastTick = now;
    m_impl->hasLastTick = true;

    bool changed = false;

    // Update high-level actions first. If any action transitions happened
    // (pressed/released), refresh the title immediately.
    const bool actionsChanged = m_impl->mapper.Consume(events);
    if (actionsChanged)
        changed = true;

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

    // Continuous keyboard movement (WASD + QE) in camera-relative space.
    //
    // We interpret the existing DebugCameraState as:
    //   - yaw: rotation around vertical axis (degrees)
    //   - panX/panY: "world" translation on the horizontal plane
    //   - zoom: distance/magnification scalar
    //
    // Movement should be frame-rate independent, hence the dt scaling.
    const auto axes = m_impl->mapper.GetMovementAxes();
    const bool anyMove = (axes.x != 0.f) || (axes.y != 0.f) || (axes.z != 0.f);

    if (anyMove && dt > 0.f)
    {
        const auto& s = m_impl->camera.State();
        constexpr float kPi = 3.14159265358979323846f;
        const float yawRad = s.yaw * (kPi / 180.f);
        const float sinY = std::sin(yawRad);
        const float cosY = std::cos(yawRad);

        // Forward when yaw==0 is +Y. Right is +X.
        const float fwdX = sinY;
        const float fwdY = cosY;
        const float rightX = cosY;
        const float rightY = -sinY;

        const bool boost = m_impl->mapper.IsDown(colony::input::Action::SpeedBoost);
        const float speedMul = boost ? 3.0f : 1.0f;

        // Pan speed is "world" units per second. Tune later.
        constexpr float kPanSpeed = 3.0f;
        const float panSpeed = kPanSpeed * speedMul;
        const float worldX = (rightX * axes.x + fwdX * axes.y) * (panSpeed * dt);
        const float worldY = (rightY * axes.x + fwdY * axes.y) * (panSpeed * dt);

        bool movedThisFrame = false;
        if (m_impl->camera.ApplyPan(worldX, worldY)) {
            movedThisFrame = true;
        }

        if (axes.z != 0.f)
        {
            // Exponential zoom is stable (always positive) and feels consistent.
            constexpr float kZoomSpeed = 1.5f; // per second
            const float zoomSpeed = kZoomSpeed * (boost ? 2.0f : 1.0f);
            const float factor = std::exp(axes.z * zoomSpeed * dt);
            if (m_impl->camera.ApplyZoomFactor(factor)) {
                movedThisFrame = true;
            }
        }

        // Avoid spamming SetWindowTextW every frame while keys are held.
        // Update immediately on an action transition (pressed/released), otherwise at ~10Hz.
        if (movedThisFrame && !actionsChanged)
        {
            m_impl->keyboardTitleAccum += dt;
            if (m_impl->keyboardTitleAccum >= 0.10f) {
                changed = true;
                m_impl->keyboardTitleAccum = 0.f;
            }
        }

        if (movedThisFrame && actionsChanged)
        {
            // Reset the accumulator so we don't immediately trigger another forced update.
            m_impl->keyboardTitleAccum = 0.f;
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
