#pragma once
#include <cstdint>
#include <functional>

namespace platform::win {

// Small, Windows-flavored callback table for a Win32 message pump.
// This header is intentionally lightweight: it does *not* include <Windows.h>.
// Use plain ints for virtual key codes and mouse buttons.
struct Callbacks {
    // Window / lifecycle
    std::function<void()>         on_quit;
    std::function<void(int, int)> on_resize; // width, height

    // Mouse
    std::function<void(int, int)>             on_mouse_move;   // x, y
    std::function<void(int, int, int)>        on_mouse_wheel;  // x, y, delta
    std::function<void(int, bool, int, int)>  on_mouse_button; // vkButton, down?, x, y

    // Keyboard
    std::function<void(unsigned, int)> on_key;  // vk, repeatCount
    std::function<void(wchar_t)>       on_char; // UTF-16 code unit
};

} // namespace platform::win
