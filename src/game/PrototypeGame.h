#pragma once

#include "input/InputEvent.h"

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

    // Consume input events. Returns true if something changed that should
    // immediately refresh the debug window title.
    bool OnInput(std::span<const colony::input::InputEvent> events) noexcept;

    [[nodiscard]] DebugCameraInfo GetDebugCameraInfo() const noexcept;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace colony::game
