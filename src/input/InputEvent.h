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
    FocusLost    = 5,
    MouseButtonDown = 6,
    MouseButtonUp   = 7,
};

// Unified input code space used by InputMapper.
//
//  - Keyboard: Win32 virtual-key codes (0..255)
//  - Mouse buttons: codes starting at kMouseCodeBase
constexpr std::uint32_t kKeyboardCodeCount = 256;
constexpr std::uint32_t kMouseCodeBase = kKeyboardCodeCount;

constexpr std::uint32_t kMouseButtonLeft   = kMouseCodeBase + 0;
constexpr std::uint32_t kMouseButtonRight  = kMouseCodeBase + 1;
constexpr std::uint32_t kMouseButtonMiddle = kMouseCodeBase + 2;
constexpr std::uint32_t kMouseButtonX1     = kMouseCodeBase + 3;
constexpr std::uint32_t kMouseButtonX2     = kMouseCodeBase + 4;

constexpr std::uint32_t kInputCodeCount = kMouseCodeBase + 5;

enum MouseButtonsMask : std::uint8_t {
    MouseLeft   = 1u << 0,
    MouseRight  = 1u << 1,
    MouseMiddle = 1u << 2,

    // Extra mouse buttons (typically "Mouse4" / "Mouse5").
    // These are useful for bindable actions (e.g., camera pan/orbit) and for
    // consistent button-state snapshots when producing MouseDelta events.
    MouseX1     = 1u << 3,
    MouseX2     = 1u << 4,
};

// NOTE: Keep this POD/trivial. It is intentionally "wide" so we can avoid unions/variants.
struct InputEvent {
    InputEventType type = InputEventType::MouseDelta;

    // MouseDelta
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint8_t buttons = 0; // MouseButtonsMask snapshot (buttons held for this delta)

    // MouseWheel
    std::int32_t wheelDetents = 0;

    // KeyDown/KeyUp and MouseButtonDown/MouseButtonUp
    //
    // - keyboard: Win32 virtual-key code (0..255)
    // - mouse: one of kMouseButton* codes (>= kMouseCodeBase)
    std::uint32_t key = 0;
    bool alt = false;
    bool repeat = false;

    // WindowResize
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace colony::input
