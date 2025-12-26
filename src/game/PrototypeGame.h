#pragma once

#include "input/InputEvent.h"

#include <memory>
#include <span>

namespace colony::game {

// Small temporary game module used by the current executable.
//
// The goal is to keep AppWindow (Win32) responsible for:
//   - translating OS messages into InputEvents
//   - window/system toggles (fullscreen, vsync)
//   - rendering presentation
//
// ...while the "game" owns behavior/state (camera, world state, etc.).

struct DebugCameraInfo {
    float yaw   = 0.f;
    float pitch = 0.f;
    float panX  = 0.f;
    float panY  = 0.f;
    float zoom  = 1.f;
};

class PrototypeGame {
public:
    PrototypeGame();
    ~PrototypeGame();

    PrototypeGame(const PrototypeGame&) = delete;
    PrototypeGame& operator=(const PrototypeGame&) = delete;

    // Consume input events (key up/down, mouse buttons, etc.).
    // Returns true if something changed that should immediately refresh the
    // debug window title (mostly camera/tool state).
    bool OnInput(std::span<const colony::input::InputEvent> events,
                 bool uiWantsKeyboard,
                 bool uiWantsMouse) noexcept;

    // Per-render-frame update: fixed-step simulation, keyboard camera pan/zoom,
    // hot-reload polling, etc.
    bool Update(float dtSeconds, bool uiWantsKeyboard, bool uiWantsMouse) noexcept;

    // Build the prototype's ImGui UI (world viewport, build menu, sim controls).
    // No-op if ImGui is disabled.
    void DrawUI() noexcept;

    // Convenience hotkeys (triggered by the window layer).
    void TogglePanels() noexcept;
    void ToggleHelp() noexcept;
    void ResetWorld() noexcept;

    [[nodiscard]] DebugCameraInfo GetDebugCameraInfo() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace colony::game
