#include "platform/win32/RawMouseInput.h"

#include <windowsx.h> // GET_WHEEL_DELTA_WPARAM

#include <vector>

namespace colony::appwin::win32 {

void RawMouseInput::Register(HWND hwnd) noexcept
{
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
    rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE

    // Keep INPUTSINK so we continue receiving WM_INPUT even while captured; we still
    // gate processing by focus/capture in WM_INPUT to avoid background movement.
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;

    m_rawRegistered = (RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE);
}

// -----------------------------------------------------------------------------
// Focus / capture
// -----------------------------------------------------------------------------

void RawMouseInput::OnSetFocus() noexcept
{
    m_hasFocus = true;
    m_hasPos = false; // avoid huge delta after refocus
}

void RawMouseInput::OnKillFocus(HWND hwnd) noexcept
{
    m_hasFocus = false;
    ClearStateAndCapture(hwnd);
}

void RawMouseInput::OnActivateApp(HWND hwnd, bool active) noexcept
{
    if (active) {
        m_hasFocus = true;
        m_hasPos = false;
    } else {
        m_hasFocus = false;
        ClearStateAndCapture(hwnd);
    }
}

void RawMouseInput::OnCaptureChanged(HWND hwnd, HWND newCapture) noexcept
{
    // If another window gained capture, clear our internal button state to avoid "stuck dragging".
    if (newCapture != hwnd) {
        ClearStateAndCapture(hwnd);
    }
}

void RawMouseInput::OnCancelMode(HWND hwnd) noexcept
{
    ClearStateAndCapture(hwnd);
}

bool RawMouseInput::InputActive(HWND hwnd) const noexcept
{
    // Either we have focus, or we still own capture (dragging outside client).
    return m_hasFocus || (GetCapture() == hwnd);
}

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------

void RawMouseInput::BeginCapture(HWND hwnd) noexcept
{
    SetCapture(hwnd);
}

void RawMouseInput::ClearStateAndCapture(HWND hwnd) noexcept
{
    m_buttons.left = false;
    m_buttons.right = false;
    m_buttons.middle = false;
    m_hasPos = false;

    if (GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

void RawMouseInput::MaybeEndCapture(HWND hwnd) noexcept
{
    if (!(m_buttons.left || m_buttons.right || m_buttons.middle)) {
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
    }
}

void RawMouseInput::OnLButtonDown(HWND hwnd, int x, int y) noexcept
{
    m_buttons.left = true;
    BeginCapture(hwnd);
    m_lastX = x;
    m_lastY = y;
    m_hasPos = true;
}

void RawMouseInput::OnLButtonUp(HWND hwnd) noexcept
{
    m_buttons.left = false;
    MaybeEndCapture(hwnd);
}

void RawMouseInput::OnRButtonDown(HWND hwnd, int x, int y) noexcept
{
    m_buttons.right = true;
    BeginCapture(hwnd);
    m_lastX = x;
    m_lastY = y;
    m_hasPos = true;
}

void RawMouseInput::OnRButtonUp(HWND hwnd) noexcept
{
    m_buttons.right = false;
    MaybeEndCapture(hwnd);
}

void RawMouseInput::OnMButtonDown(HWND hwnd, int x, int y) noexcept
{
    m_buttons.middle = true;
    BeginCapture(hwnd);
    m_lastX = x;
    m_lastY = y;
    m_hasPos = true;
}

void RawMouseInput::OnMButtonUp(HWND hwnd) noexcept
{
    m_buttons.middle = false;
    MaybeEndCapture(hwnd);
}

// -----------------------------------------------------------------------------
// Mouse move / wheel
// -----------------------------------------------------------------------------

bool RawMouseInput::OnMouseMove(HWND hwnd, int x, int y, LONG& outDx, LONG& outDy) noexcept
{
    outDx = 0;
    outDy = 0;

    if (m_hasPos) {
        const LONG dx = static_cast<LONG>(x - m_lastX);
        const LONG dy = static_cast<LONG>(y - m_lastY);

        if ((m_buttons.left || m_buttons.right || m_buttons.middle) && InputActive(hwnd)) {
            // If raw input is registered, prefer WM_INPUT deltas and avoid double-applying.
            if (!m_rawRegistered) {
                outDx = dx;
                outDy = dy;
            }
        }
    }

    m_lastX = x;
    m_lastY = y;
    m_hasPos = true;

    return (outDx != 0 || outDy != 0);
}

int RawMouseInput::OnMouseWheel(WPARAM wParam) noexcept
{
    return GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
}

// -----------------------------------------------------------------------------
// Raw input (WM_INPUT)
// -----------------------------------------------------------------------------

bool RawMouseInput::OnRawInput(HWND hwnd, HRAWINPUT hRawInput, LONG& outDx, LONG& outDy) noexcept
{
    outDx = 0;
    outDy = 0;

    // Only process raw input when the window is active or owns capture,
    // and only while dragging (buttons down). This avoids background movement
    // and reduces per-message overhead.
    if (!InputActive(hwnd) || !(m_buttons.left || m_buttons.right || m_buttons.middle)) {
        return false;
    }

    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) {
        return false;
    }

    // Reuse a static buffer to avoid heap churn at high WM_INPUT rates.
    static std::vector<BYTE> s_buffer;
    if (s_buffer.size() < size) {
        s_buffer.resize(size);
    }

    const UINT copied = GetRawInputData(
        hRawInput,
        RID_INPUT,
        s_buffer.data(),
        &size,
        sizeof(RAWINPUTHEADER)
    );

    if (copied != size) {
        return false;
    }

    auto* raw = reinterpret_cast<RAWINPUT*>(s_buffer.data());
    if (raw->header.dwType != RIM_TYPEMOUSE) {
        return false;
    }

    outDx = raw->data.mouse.lLastX;
    outDy = raw->data.mouse.lLastY;
    return (outDx != 0 || outDy != 0);
}

} // namespace colony::appwin::win32
