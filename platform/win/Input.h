// platform/win/Input.h
#pragma once
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>   // header only; we dynamically load the DLL
#include <array>
#include <cstdint>
#include <string>
#include <optional>

namespace Colony::Win
{
    // Simple per-frame keyboard/mouse/gamepad state for polling-style input.
    struct KeyboardState
    {
        std::array<bool, 256> down{};     // VK_* keys
        std::array<bool, 256> pressed{};  // was pressed this frame
        std::array<bool, 256> released{}; // was released this frame
        void ClearEdges() { pressed.fill(false); released.fill(false); }
        void Reset() { down.fill(false); ClearEdges(); }
    };

    struct MouseState
    {
        // Relative raw deltas for this frame (Raw Input)
        long dx = 0, dy = 0;
        int  wheel = 0; // wheel steps accumulated this frame
        bool left = false, right = false, middle = false, x1 = false, x2 = false;

        void BeginFrame() { dx = dy = 0; wheel = 0; }
        void Reset() { dx = dy = 0; wheel = 0; left = right = middle = x1 = x2 = false; }
    };

    struct GamepadState
    {
        bool connected = false;
        // Raw XInput values
        XINPUT_STATE raw{};
        // Normalized [-1,1] with deadzone applied
        float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
        float lt = 0.f, rt = 0.f; // [0,1]
        uint16_t buttons = 0;
    };

    class Input
    {
    public:
        Input() = default;
        ~Input();

        // Register for Raw Input and prepare XInput. Call once after window creation.
        // If captureInBackground=true uses RIDEV_INPUTSINK to receive WM_INPUT while inactive.
        bool Initialize(HWND hwnd, bool useRawMouse = true, bool useRawKeyboard = true, bool captureInBackground = false);

        // Message hook: call this by registering with Window::AddMsgListener.
        // Returns true if handled (except WM_INPUT still requires DefWindowProc in window).
        bool HandleMessage(HWND, UINT msg, WPARAM wParam, LPARAM lParam);

        // Per-frame updates
        void BeginFrame();      // clear per-frame deltas/edges
        void UpdateGamepads();  // poll XInput pads

        // Accessors
        const KeyboardState& Keyboard() const { return m_keyboard; }
        const MouseState&    Mouse() const    { return m_mouse; }
        const GamepadState&  Pad(int index) const { return m_gamepads[index]; }

        // Utility
        void SetDeadZones(short leftThumb = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                          short rightThumb = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
                          unsigned char trigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

        void ResetOnFocusLost();

    private:
        // --- Raw Input helpers ---
        bool RegisterForRawInput(HWND hwnd, bool mouse, bool keyboard, bool inputSink);
        void ProcessRawInput(HRAWINPUT hRawInput);

        void HandleRawKeyboard(const RAWKEYBOARD& rk);
        void HandleRawMouse(const RAWMOUSE& rm);

        // Map VK_SHIFT to VK_LSHIFT/VK_RSHIFT using scancode (needed with Raw Input)
        static UINT DistinguishLeftRightShift(UINT vk, USHORT makeCode, USHORT flags);

        // --- XInput dynamic loader ---
        void LoadXInput();
        void UnloadXInput();

        using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
        using XInputSetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);

        HMODULE           m_xinputDll = nullptr;
        XInputGetStateFn  m_XInputGetState = nullptr;
        XInputSetStateFn  m_XInputSetState = nullptr;

        // State
        KeyboardState m_keyboard{};
        MouseState    m_mouse{};
        std::array<GamepadState, XUSER_MAX_COUNT> m_gamepads{};
        short         m_leftDead  = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        short         m_rightDead = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        unsigned char m_trigDead  = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    };
}
