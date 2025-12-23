#pragma once
#include <windows.h>

#include "DxDevice.h"

// Thin Win32 window wrapper + message loop for the current prototype.
// Keyboard shortcuts (see AppWindow.cpp):
//   - Esc      : Quit
//   - V        : Toggle VSync
//   - F11      : Toggle borderless fullscreen
//   - Alt+Enter: Toggle borderless fullscreen
class AppWindow {
public:
    bool Create(HINSTANCE hInst, int nCmdShow, int width, int height);
    int  MessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void RegisterRawMouse(HWND hwnd);

    void ToggleVsync();
    void ToggleFullscreen();
    void UpdateTitle(); // includes FPS (once computed), vsync + fullscreen state

    HWND    m_hwnd = nullptr;
    DxDevice m_gfx;

    bool m_vsync = true;

    bool  m_fullscreen = false;
    DWORD m_windowStyle = 0;
    DWORD m_windowExStyle = 0;
    RECT  m_windowRect{};

    double m_fps = 0.0;

    UINT m_width = 1280;
    UINT m_height = 720;
};
