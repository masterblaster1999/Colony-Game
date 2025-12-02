#include "colony/platform/win/WinCommon.h"
#include "colony/platform/win/WinWindow.hpp"

#include <string>
#include <stdexcept>

namespace colony::win {

// The COMPLETE definition lives in the .cpp (private to this TU).
struct WinWindowState {
    HINSTANCE   hinst   = nullptr;
    HWND        hwnd    = nullptr;
    std::wstring className;
};

// Simple window procedure for this sample.
static LRESULT CALLBACK WinWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    default:         return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ----- Ctors/dtor/move: define OUT-OF-LINE so unique_ptr<WinWindowState> sees a complete type -----
WinWindow::WinWindow() = default;
WinWindow::~WinWindow() = default;
WinWindow::WinWindow(WinWindow&&) noexcept = default;
WinWindow& WinWindow::operator=(WinWindow&&) noexcept = default;

// ----- API -----
bool WinWindow::create(const wchar_t* title, int width, int height, void* hInstance, int nCmdShow) {
    state_ = std::make_unique<WinWindowState>();
    state_->hinst = static_cast<HINSTANCE>(hInstance);

    // Register class (per-instance; good enough for engine tools/games)
    state_->className = L"ColonyWinWindowClass";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &WinWindowProc;
    wc.hInstance     = state_->hinst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = state_->className.c_str();
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // Create the actual window
    RECT r{ 0, 0, width, height };
    ::AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    state_->hwnd = ::CreateWindowExW(
        0, state_->className.c_str(), title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, state_->hinst, nullptr);

    if (!state_->hwnd) {
        return false;
    }

    ::ShowWindow(state_->hwnd, nCmdShow);
    ::UpdateWindow(state_->hwnd);
    return true;
}

void WinWindow::show() {
    if (state_ && state_->hwnd) {
        ::ShowWindow(state_->hwnd, SW_SHOW);
        ::UpdateWindow(state_->hwnd);
    }
}

void WinWindow::set_title(const std::wstring& title) {
    if (state_ && state_->hwnd) {
        ::SetWindowTextW(state_->hwnd, title.c_str());
    }
}

HWND WinWindow::hwnd() const noexcept {
    return state_ ? state_->hwnd : nullptr;
}

} // namespace colony::win
