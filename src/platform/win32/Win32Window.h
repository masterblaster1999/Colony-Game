#pragma once

#include "platform/win/WinCommon.h"

namespace colony::appwin::win32 {

// Register a Win32 window class. If the class already exists, this is treated
// as success.
bool RegisterWindowClass(HINSTANCE hInst, const wchar_t* className, WNDPROC wndProc) noexcept;

// Create a DPI-aware Win32 window for a desired client size.
//
// The caller supplies wndProc + userPtr; the wndProc should handle WM_NCCREATE
// and store userPtr (lpCreateParams) somewhere (e.g. GWLP_USERDATA).
HWND CreateDpiAwareWindow(HINSTANCE hInst,
                          const wchar_t* className,
                          const wchar_t* title,
                          int clientWidth,
                          int clientHeight,
                          WNDPROC wndProc,
                          void* userPtr) noexcept;

// Apply the suggested rectangle from WM_DPICHANGED.
void ApplyDpiSuggestedRect(HWND hwnd, const RECT* prcNewWindow) noexcept;

// Helper that implements a borderless-fullscreen toggle.
//
// This mirrors the behavior in the current prototype AppWindow.
class BorderlessFullscreen {
public:
    void InitFromCurrent(HWND hwnd) noexcept;

    [[nodiscard]] bool IsFullscreen() const noexcept { return m_fullscreen; }

    void Toggle(HWND hwnd) noexcept;

private:
    bool  m_fullscreen = false;
    DWORD m_windowStyle = 0;
    DWORD m_windowExStyle = 0;

    // Windowed-mode placement/state to restore when leaving borderless fullscreen.
    //
    // Using WINDOWPLACEMENT means we restore the correct "maximized" state and the
    // correct *work-area* sizing (taskbar) instead of just slamming the raw
    // monitor rectangle back into SetWindowPos.
    WINDOWPLACEMENT m_windowPlacement{ sizeof(WINDOWPLACEMENT) };
    bool            m_hasWindowPlacement = false;

    // Fallback for safety (used if GetWindowPlacement fails for some reason).
    RECT  m_windowRect{};
};

} // namespace colony::appwin::win32
