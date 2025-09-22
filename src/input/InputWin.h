// src/input/InputWin.h
//
// Windows-only unified input (XInput gamepads + Raw Input mouse/keyboard).
// - Gamepads: dynamic XInput loader (1.4 / 1.3 / 9.1.0), 0..3 pads, rumble w/ timeout.
// - Raw Input: hi-rate mouse deltas, wheel & tilt, multi-device keyboard, device notifications.
// - Cursor helpers: relative mode (hide+confine+recentre), explicit confine, show/hide.
//
// Build: C++17+, Windows only. Link: user32.lib (Raw Input), (no static XInput link required).
//
// IMPORTANT (WM_INPUT): After you forward WM_INPUT to this module, your WndProc should still
// call DefWindowProc for WM_INPUT with RIM_INPUT to let the system clean up buffers. See docs.
//
// Public API is header-only except for the implementation in InputWin.cpp.

#pragma once

#ifndef _WIN32
#  error "InputWin is Windows-only."
#endif

#include <cstdint>
#include <array>
#include <bitset>
#include <string>
#include <chrono>

struct HWND__;
typedef HWND__* HWND;

namespace cg::input {

constexpr int kMaxGamepads = 4;

// ------------------------------- Keyboard/Mouse state -------------------------------

struct KeyboardState {
    // Virtual-key pressed state (VK_*). True = pressed (down).
    std::bitset<256> down{};
    // Optional: per-frame "just pressed"/"just released" can be derived externally
    // by storing previous frame and comparing.
};

struct MouseState {
    // Absolute cursor in client space (pixels). Updated when asked (see trackCursor flag).
    int x = 0, y = 0;

    // Per-frame relative deltas from Raw Input (reset via newFrame()).
    int dx = 0, dy = 0;

    // Accumulated wheel deltas this frame (120 = one detent, per Windows).
    // Horizontal wheel is rarely present but supported.
    int wheel = 0;
    int wheelH = 0;

    bool left   = false;
    bool right  = false;
    bool middle = false;
    bool x1     = false;
    bool x2     = false;

    bool relativeMode = false;     // if true, cursor is hidden + confined + recentered
    bool confined     = false;     // if true, cursor is confined to client rect
};

// ------------------------------- Gamepad state -------------------------------

struct GamepadState {
    bool  connected = false;
    // Buttons as XINPUT_GAMEPAD bitmask (A/B/X/Y, shoulders, dpad, etc).
    uint16_t buttons = 0;
    // Triggers (0..1), Sticks normalized to [-1..1] after dead-zone.
    float lt = 0.0f, rt = 0.0f;
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;

    // Raw values if needed for debugging (not normalized):
    uint8_t  lt_raw = 0, rt_raw = 0;
    int16_t  lx_raw = 0, ly_raw = 0, rx_raw = 0, ry_raw = 0;

    // Optional: reported subtype/caps if available via XInputGetCapabilities (1.4/1.3)
    uint8_t  subtype = 0;
    uint32_t capsFlags = 0;
};

struct InputState {
    KeyboardState keyboard;
    MouseState    mouse;
    std::array<GamepadState, kMaxGamepads> pads{};
};

// ------------------------------- Options -------------------------------

struct RawInputOptions {
    // When true: suppress legacy WM_* mouse/keyboard messages (prevents double events).
    // Only affect mouse/keyboard TLCs per RAWINPUTDEVICE::dwFlags docs.
    bool no_legacy_messages = true;

    // When true: receive WM_INPUT even when window not focused (requires hwndTarget).
    bool background = false;

    // Whether to keep the OS cursor position updated in MouseState (reads via GetCursorPos).
    bool track_cursor = true;
};

struct DeadzoneOptions {
    // Values are normalized thresholds (0..1).
    // Defaults reflect XInput constants (7849/32767, 8689/32767, 30/255).
    float left_stick  = 7849.0f  / 32767.0f;
    float right_stick = 8689.0f  / 32767.0f;
    float trigger     = 30.0f    / 255.0f;

    // If true, perform radial (circular) stick dead-zone; otherwise axial (per-axis).
    bool radial_sticks = true;
};

// ------------------------------- Input system -------------------------------

class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() { shutdown(); }

    // Initialize Raw Input + XInput. You must pass your main HWND.
    bool initialize(HWND hwnd, const RawInputOptions& rio = {}, const DeadzoneOptions& dz = {});

    // Unregister Raw Input, release XInput.
    void shutdown();

    // Handle Windows messages (call this from your WndProc *before* default handling).
    // Returns true if handled by this module. For WM_INPUT, you should still call
    // DefWindowProc afterward (see docs).
    bool processMessage(HWND hwnd, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

    // Frame boundary: reset per-frame mouse deltas & wheels, apply rumble timeouts, poll pads.
    void newFrame();

    // Poll XInput pads now (also called by newFrame). Typically call once per frame.
    void pollGamepads();

    // Rumble (0..1). If duration_ms = 0, it will persist until changed/stopped.
    bool setGamepadVibration(int padIndex, float left, float right, uint32_t duration_ms = 0);
    void stopGamepadVibration(int padIndex);

    // Toggle relative mouse mode (hide+confine+recenter). Returns true on success.
    bool setRelativeMouseMode(HWND hwnd, bool enable);

    // Confine cursor to the client rect of hwnd. Returns true on success.
    bool confineCursorToWindow(HWND hwnd, bool enable);

    // Center cursor in client rect.
    void centerCursor(HWND hwnd);

    // Show or hide the OS cursor. (Careful: ShowCursor uses an internal ref-count.)
    void setCursorVisible(bool visible);

    // Re-register Raw Input (e.g., if focus policy changed).
    bool reregisterRawInput(HWND hwnd);

    // Configure dead-zones at runtime.
    void setDeadzones(const DeadzoneOptions& dz);

    // Read-only snapshot (updated as you call processMessage/newFrame/pollGamepads).
    const InputState& state() const { return state_; }

private:
    struct Rumble {
        uint16_t left = 0, right = 0;
        // 0 means infinite; otherwise end tick (QPC) when to stop.
        std::uint64_t end_tick = 0;
    };

    // --- internals ---
    bool registerRawInput_(HWND hwnd);
    bool unregisterRawInput_();
    bool handleRawInput_(std::intptr_t lParam);
    void handleDeviceChange_(std::uintptr_t wParam, std::intptr_t lParam);

    void pollSinglePad_(int idx);
    void applyStickDeadzone_(float inX, float inY, float dz, bool radial,
                             float& outX, float& outY);
    float applyTriggerDeadzone_(float v, float dz) const;

    // QPC helpers
    static std::uint64_t qpc_now_();
    static std::uint64_t qpc_freq_();

    // data
    InputState         state_{};
    RawInputOptions    rio_{};
    DeadzoneOptions    dz_{};

    std::array<Rumble, kMaxGamepads> rumble_{};
    bool registered_ = false;
};

} // namespace cg::input
