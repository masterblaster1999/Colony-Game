#pragma once
#include <windows.h>

#include <memory>

#include "DxDevice.h"

// Thin Win32 window wrapper + message loop for the current prototype.
// Keyboard shortcuts (see AppWindow.cpp):
//   - Esc      : Quit
//   - V        : Toggle VSync
//   - F11      : Toggle borderless fullscreen
//   - Alt+Enter: Toggle borderless fullscreen
//   - F1       : Toggle debug overlay (ImGui)
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

    void ToggleVsync();
    void ToggleFullscreen();
    void ToggleOverlay();
    void UpdateTitle(); // includes FPS (once computed), vsync + fullscreen state

    // Debounced autosave uses this to avoid hammering the disk during resizing
    // or rapid toggle spam.
    void MarkSettingsDirty();

    void MarkSettingsDirty();

    HWND    m_hwnd = nullptr;
    DxDevice m_gfx;

    bool m_vsync = true;

    UINT m_width = 1280;
    UINT m_height = 720;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
