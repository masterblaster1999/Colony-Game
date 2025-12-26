#pragma once
#include "platform/win/WinCommon.h"

#include <memory>

#include "DxDevice.h"

// Thin Win32 window wrapper + message loop for the current prototype.
// Keyboard shortcuts (see AppWindow_WndProc_Input.cpp):
//   - Esc       : Quit
//   - F1        : Toggle in-game panels (ImGui)
//   - F2        : Toggle in-game help (ImGui)
//   - F3        : Show runtime hotkeys (MessageBox)
//   - V         : Toggle VSync
//   - F11       : Toggle borderless fullscreen
//   - Alt+Enter : Toggle borderless fullscreen
//   - F10       : Toggle frame pacing stats in title bar (PresentMon-style summary)
//   - F9        : Toggle RAWINPUT mouse (drag deltas)
//   - F8        : Cycle DXGI max frame latency (1..16)
//   - F7        : Toggle pause-when-unfocused
//   - F6        : Cycle FPS cap when VSync is OFF (∞ / 60 / 120 / 144 / 165 / 240)
//   - Shift+F6  : Cycle background FPS cap (∞ / 5 / 10 / 30 / 60)
class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    bool Create(HINSTANCE hInst, int nCmdShow, int width, int height);
    int  MessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    // Split WndProc handling into focused units (Window vs Input) to keep files manageable.
    LRESULT HandleMsg_Window(HWND, UINT, WPARAM, LPARAM, bool& handled);
    LRESULT HandleMsg_Input(HWND, UINT, WPARAM, LPARAM, bool& handled);

    void ToggleVsync();
    void ToggleFullscreen();
    void CycleMaxFpsWhenVsyncOff();
    void CycleMaxFpsWhenUnfocused();
    void ShowHotkeysHelp();
    void UpdateTitle(); // includes FPS (once computed), vsync + fullscreen state

    HWND    m_hwnd = nullptr;
    DxDevice m_gfx;

    bool m_vsync = true;

    UINT m_width = 1280;
    UINT m_height = 720;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
