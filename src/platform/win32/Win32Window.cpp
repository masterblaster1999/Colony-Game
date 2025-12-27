#include "platform/win32/Win32Window.h"

#include <shellscalingapi.h> // AdjustWindowRectExForDpi, GetDpiForSystem
#include <dwmapi.h>

#pragma comment(lib, "Shcore.lib")

// DwmSetWindowAttribute
#pragma comment(lib, "dwmapi.lib")

namespace colony::appwin::win32 {

bool RegisterWindowClass(HINSTANCE hInst, const wchar_t* className, WNDPROC wndProc) noexcept
{
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;

    if (RegisterClassExW(&wc))
        return true;

    const DWORD err = GetLastError();
    return (err == ERROR_CLASS_ALREADY_EXISTS);
}

HWND CreateDpiAwareWindow(HINSTANCE hInst,
                          const wchar_t* className,
                          const wchar_t* title,
                          int clientWidth,
                          int clientHeight,
                          WNDPROC wndProc,
                          void* userPtr) noexcept
{
    if (!RegisterWindowClass(hInst, className, wndProc))
        return nullptr;

    RECT r{ 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };

    // High-DPI aware rect sizing for the client area.
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());

    return CreateWindowExW(
        0,
        className,
        title,
        // Create hidden; caller decides when/how to show (prevents "flash" when
        // we need to apply saved placement before first ShowWindow).
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        r.right - r.left,
        r.bottom - r.top,
        nullptr,
        nullptr,
        hInst,
        userPtr);
}

void ApplyDpiSuggestedRect(HWND hwnd, const RECT* prcNewWindow) noexcept
{
    if (!hwnd || !prcNewWindow)
        return;

    SetWindowPos(
        hwnd,
        nullptr,
        prcNewWindow->left,
        prcNewWindow->top,
        prcNewWindow->right - prcNewWindow->left,
        prcNewWindow->bottom - prcNewWindow->top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}

void BorderlessFullscreen::InitFromCurrent(HWND hwnd) noexcept
{
    if (!hwnd)
        return;

    m_windowStyle   = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    m_windowExStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    GetWindowRect(hwnd, &m_windowRect);

    m_windowPlacement.length = sizeof(WINDOWPLACEMENT);
    m_hasWindowPlacement = (GetWindowPlacement(hwnd, &m_windowPlacement) != FALSE);
    m_fullscreen = false;
}

void BorderlessFullscreen::Toggle(HWND hwnd) noexcept
{
    if (!hwnd)
        return;

    if (!m_fullscreen)
    {
        // Save windowed placement.
        m_windowStyle   = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        m_windowExStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        GetWindowRect(hwnd, &m_windowRect);

        m_windowPlacement.length = sizeof(WINDOWPLACEMENT);
        m_hasWindowPlacement = (GetWindowPlacement(hwnd, &m_windowPlacement) != FALSE);

        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            // Borderless fullscreen.
            const DWORD newStyle = (m_windowStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;

            // Reduce the amount of compositor/window-manager animation work
            // during fullscreen transitions. (Best-effort: ignored on older
            // Windows builds.)
            {
                const BOOL disableTransitions = TRUE;
                (void)DwmSetWindowAttribute(
                    hwnd,
                    DWMWA_TRANSITIONS_FORCEDISABLED,
                    &disableTransitions,
                    sizeof(disableTransitions)
                );
            }

            SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(newStyle));
            SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>(m_windowExStyle));

            SetWindowPos(
                hwnd,
                HWND_TOP,
                mi.rcMonitor.left,
                mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );

            m_fullscreen = true;
        }
    }
    else
    {
        // Re-enable compositor transitions (best-effort).
        {
            const BOOL disableTransitions = FALSE;
            (void)DwmSetWindowAttribute(
                hwnd,
                DWMWA_TRANSITIONS_FORCEDISABLED,
                &disableTransitions,
                sizeof(disableTransitions)
            );
        }

        // Restore windowed placement.
        SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(m_windowStyle));
        SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>(m_windowExStyle));

        if (m_hasWindowPlacement)
        {
            // Restores proper maximized state and work-area sizing.
            (void)SetWindowPlacement(hwnd, &m_windowPlacement);
            SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
        }
        else
        {
            // Fallback: raw rect restore.
            SetWindowPos(
                hwnd,
                nullptr,
                m_windowRect.left,
                m_windowRect.top,
                m_windowRect.right - m_windowRect.left,
                m_windowRect.bottom - m_windowRect.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
        }

        m_fullscreen = false;
    }
}

} // namespace colony::appwin::win32
