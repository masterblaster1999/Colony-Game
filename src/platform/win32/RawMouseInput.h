#pragma once

#include "platform/win/WinCommon.h"

namespace colony::appwin::win32 {

struct MouseButtons {
    bool left   = false;
    bool right  = false;
    bool middle = false;

    // Extra mouse buttons (typically mouse4/mouse5).
    bool x1     = false;
    bool x2     = false;
};

// Raw mouse input helper for the prototype AppWindow.
//
// Responsibilities:
//   - Register for WM_INPUT mouse events
//   - Track focus/capture and button state
//   - Produce drag deltas from either cursor movement (WM_MOUSEMOVE) or
//     high-resolution raw input (WM_INPUT)
class RawMouseInput {
public:
    void Register(HWND hwnd) noexcept;

    // Enable/disable WM_INPUT raw mouse deltas at runtime.
    //
    // When disabled, the helper falls back to cursor-based deltas (WM_MOUSEMOVE)
    // while dragging.
    void SetEnabled(HWND hwnd, bool enabled) noexcept;

    // Focus / capture
    void OnSetFocus() noexcept;
    void OnKillFocus(HWND hwnd) noexcept;
    void OnActivateApp(HWND hwnd, bool active) noexcept;
    void OnCaptureChanged(HWND hwnd, HWND newCapture) noexcept;
    void OnCancelMode(HWND hwnd) noexcept;

    // Buttons
    void OnLButtonDown(HWND hwnd, int x, int y) noexcept;
    void OnLButtonUp(HWND hwnd) noexcept;
    void OnRButtonDown(HWND hwnd, int x, int y) noexcept;
    void OnRButtonUp(HWND hwnd) noexcept;
    void OnMButtonDown(HWND hwnd, int x, int y) noexcept;
    void OnMButtonUp(HWND hwnd) noexcept;

    void OnXButtonDown(HWND hwnd, bool x1, int x, int y) noexcept;
    void OnXButtonUp(HWND hwnd, bool x1) noexcept;

    // Cursor-based delta. Returns true and outputs dx/dy if the delta should be applied.
    bool OnMouseMove(HWND hwnd, int x, int y, LONG& outDx, LONG& outDy) noexcept;

    // Raw delta. Returns true and outputs dx/dy if the delta should be applied.
    bool OnRawInput(HWND hwnd, HRAWINPUT hRawInput, LONG& outDx, LONG& outDy) noexcept;

    // Mouse wheel detents (120-based, can be negative)
    int OnMouseWheel(WPARAM wParam) noexcept;

    [[nodiscard]] bool InputActive(HWND hwnd) const noexcept;
    [[nodiscard]] bool RawRegistered() const noexcept { return m_rawRegistered; }
    [[nodiscard]] MouseButtons Buttons() const noexcept { return m_buttons; }

private:
    void BeginCapture(HWND hwnd) noexcept;
    void MaybeEndCapture(HWND hwnd) noexcept;
    void ClearStateAndCapture(HWND hwnd) noexcept;

    MouseButtons m_buttons{};
    bool m_hasFocus = true;
    bool m_rawRegistered = false;

    int  m_lastX = 0;
    int  m_lastY = 0;
    bool m_hasPos = false;
};

} // namespace colony::appwin::win32
