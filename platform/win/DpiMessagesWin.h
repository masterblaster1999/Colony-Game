// platform/win/DpiMessagesWin.h
//
// Drop-in DPI message handling for Win32 WndProc.
// - Handles WM_DPICHANGED (Per-Monitor / Per-Monitor V2)
// - Applies the suggested window rectangle via SetWindowPos (recommended behavior)
// - Tracks current DPI + scale factor for your UI/render code
//
// References:
//   - WM_DPICHANGED message docs (suggested RECT in lParam; resize/reposition your window) :
//     https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged
//   - High DPI desktop app guidance (Per-Monitor v2; app must resize itself) :
//     https://learn.microsoft.com/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>

namespace windpi {

constexpr UINT kDpiDefault = 96u;

// Simple DPI state you can store per-window (or as a static for one window).
struct DpiState {
    UINT  dpi   = kDpiDefault; // 96 = 100%
    float scale = 1.0f;        // dpi / 96.0f
};

inline constexpr float ScaleFromDpi(UINT dpi) noexcept {
    return static_cast<float>(dpi) / static_cast<float>(kDpiDefault);
}

inline int DipToPx(int dip, UINT dpi) noexcept {
    return ::MulDiv(dip, static_cast<int>(dpi), static_cast<int>(kDpiDefault));
}

inline int PxToDip(int px, UINT dpi) noexcept {
    return ::MulDiv(px, static_cast<int>(kDpiDefault), static_cast<int>(dpi));
}

// Query DPI for a window (Win10+). If something goes wrong, fall back to 96.
inline UINT GetDpiForHwnd(HWND hwnd) noexcept {
    // GetDpiForWindow is available on Win10+ and in modern Windows SDKs.
    const UINT dpi = ::GetDpiForWindow(hwnd);
    return dpi ? dpi : kDpiDefault;
}

// Call this once after creating the HWND (or in WM_CREATE) so you start with correct DPI.
inline void InitFromHwnd(HWND hwnd, DpiState& io) noexcept {
    io.dpi   = GetDpiForHwnd(hwnd);
    io.scale = ScaleFromDpi(io.dpi);
}

inline void ApplySuggestedRect(HWND hwnd, const RECT& suggested,
                               UINT swpFlags = (SWP_NOZORDER | SWP_NOACTIVATE)) noexcept {
    ::SetWindowPos(hwnd, nullptr,
                   suggested.left,
                   suggested.top,
                   suggested.right  - suggested.left,
                   suggested.bottom - suggested.top,
                   swpFlags);
}

// Optional callback you can use to rebuild fonts, resize UI, rebuild swapchain, etc.
using DpiChangedCallback = void(*)(HWND hwnd, const DpiState& state, void* user) noexcept;

// Try-handle DPI-related messages.
// Returns true if handled and sets outResult.
inline bool TryHandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                            DpiState& ioState, LRESULT& outResult,
                            bool applySuggestedWindowRect = true,
                            DpiChangedCallback onChanged = nullptr,
                            void* user = nullptr) noexcept
{
    switch (msg) {
    case WM_DPICHANGED: {
        // wParam: LOWORD = xDpi, HIWORD = yDpi (normally identical)
        const UINT dpiX = LOWORD(wParam);
        const UINT dpiY = HIWORD(wParam);
        const UINT newDpi = (dpiY != 0u) ? dpiY : (dpiX != 0u ? dpiX : kDpiDefault);

        ioState.dpi   = newDpi;
        ioState.scale = ScaleFromDpi(newDpi);

        // lParam: pointer to suggested RECT for window in *screen coordinates*.
        // Recommended: apply it using SetWindowPos so physical window size stays consistent.
        if (applySuggestedWindowRect && lParam != 0) {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            ApplySuggestedRect(hwnd, *suggested);
        }

        if (onChanged) {
            onChanged(hwnd, ioState, user);
        }

        outResult = 0;
        return true;
    }

    default:
        return false;
    }
}

} // namespace windpi
