#pragma once

#include <cstdint>

namespace colony::input {

// Lightweight input event used by the prototype app layer.
//
// Design goals:
//   - Trivial POD type (easy to store in fixed-size buffers)
//   - Does not depend on Win32 types (so it can be reused in tests/tools)
//   - Small set of events for now; can grow as the game needs it.

enum class InputEventType : std::uint8_t {
    MouseDelta = 0,
    MouseWheel = 1,
    KeyDown    = 2,
    KeyUp      = 3,
    WindowResize = 4,
};

enum MouseButtonsMask : std::uint8_t {
    MouseLeft   = 1u << 0,
    MouseRight  = 1u << 1,
    MouseMiddle = 1u << 2,
};

// NOTE: Keep this POD/trivial. It is intentionally "wide" so we can avoid unions/variants.
struct InputEvent {
    InputEventType type = InputEventType::MouseDelta;

    // MouseDelta
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint8_t buttons = 0; // MouseButtonsMask

    // MouseWheel
    std::int32_t wheelDetents = 0;

    // KeyDown/KeyUp
    std::uint32_t key = 0; // Virtual-key code (Win32), but stored as an engine-agnostic integer.
    bool alt = false;
    bool repeat = false;

    // WindowResize
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace colony::input
