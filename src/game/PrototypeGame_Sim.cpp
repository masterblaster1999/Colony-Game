#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace colony::game {

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::uint32_t MakeSeed() noexcept
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint32_t>(now);
}

} // namespace

PrototypeGame::Impl::Impl()
    : world(64, 64, 0xC0FFEEu)
{
    // Load bindings at startup (logs errors but doesn't hard-fail).
    (void)loadBindings();

    // Center the camera on the world.
    const float cx = std::max(0.0f, static_cast<float>(world.width()) * 0.5f);
    const float cy = std::max(0.0f, static_cast<float>(world.height()) * 0.5f);
    (void)camera.ApplyPan(cx, cy);
    (void)camera.ApplyZoomFactor(1.0f);
}

void PrototypeGame::Impl::resetWorld()
{
    // A reset replaces world state; don't allow an old queued autosave to write after this.
    invalidatePendingAutosaves();

    const std::uint32_t seed = worldResetUseRandomSeed ? MakeSeed() : worldResetSeed;
    worldResetSeed = seed;
    world.reset(worldResetW, worldResetH, seed);

    clearPlanHistory();

    // Recenter camera.
    const DebugCameraState& s = camera.State();
    const float cx            = std::max(0.0f, static_cast<float>(world.width()) * 0.5f);
    const float cy            = std::max(0.0f, static_cast<float>(world.height()) * 0.5f);
    (void)camera.ApplyPan(cx - s.panX, cy - s.panY);

    simAccumulator = 0.0;
    paused         = false;
    simSpeed       = 1.f;

    setStatus("World reset", 2.0f);
}

bool PrototypeGame::Impl::Update(float dtSeconds, bool uiWantsKeyboard, bool /*uiWantsMouse*/) noexcept
{
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.f)
        return false;

    dtSeconds = clampf(dtSeconds, 0.f, 0.25f);

    // Track real-time playtime for save metadata.
    playtimeSeconds += static_cast<double>(dtSeconds);

    // Auto status fade
    if (statusTtl > 0.f) {
        statusTtl = std::max(0.f, statusTtl - dtSeconds);
        if (statusTtl == 0.f)
            statusText.clear();
    }

    // Background save completions (update status UI if needed).
    pollAsyncSaves();

    // Hot reload input bindings
    pollBindingHotReload(dtSeconds);

    // Keyboard camera pan/zoom
    const bool cameraChanged = updateCameraKeyboard(dtSeconds, uiWantsKeyboard);

    // Simulation (fixed-step)
    if (!paused) {
        const double scaled = static_cast<double>(dtSeconds) * static_cast<double>(simSpeed);
        simAccumulator += scaled;

        int steps = 0;
        while (simAccumulator >= fixedDt && steps < maxCatchupSteps) {
            world.tick(fixedDt);
            simAccumulator -= fixedDt;
            ++steps;
        }

        if (steps == maxCatchupSteps && simAccumulator >= fixedDt) {
            // Drop extra time if we fell behind.
            simAccumulator = std::fmod(simAccumulator, fixedDt);
        }
    }

    // Autosave is based on real time (not simulation-scaled time).
    if (autosaveEnabled && autosaveIntervalSeconds > 0.f)
    {
        autosaveAccumSeconds += dtSeconds;
        if (autosaveAccumSeconds >= autosaveIntervalSeconds)
        {
            autosaveAccumSeconds = 0.f;
            (void)autosaveWorld();
        }
    }

    return cameraChanged;
}

} // namespace colony::game
