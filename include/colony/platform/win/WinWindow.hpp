#pragma once

#include <memory>
#include <string>

// Do NOT include <Windows.h> in public headers.
// If you need to expose HWND, forward-declare its underlying struct:
struct HWND__;
using HWND = HWND__*;

namespace colony::win {

// Forward declaration of the implementation (incomplete type).
struct WinWindowState;

/// Lightweight RAII wrapper around a Win32 window (HWND),
/// implemented via PIMPL so this header stays Windows-lean.
class WinWindow {
public:
    WinWindow();                         // defined out-of-line
    ~WinWindow();                        // defined out-of-line (crucial for unique_ptr<T> to incomplete T)

    // Move support (also defined out-of-line so T is complete at point of instantiation)
    WinWindow(WinWindow&&) noexcept;     // out-of-line default
    WinWindow& operator=(WinWindow&&) noexcept; // out-of-line default

    WinWindow(const WinWindow&) = delete;
    WinWindow& operator=(const WinWindow&) = delete;

    // Create a basic overlapped window. hInstance/nCmdShow are standard WinMain params.
    bool create(const wchar_t* title,
                int width, int height,
                void* hInstance,      // pass HINSTANCE; kept as void* to avoid <Windows.h> here
                int nCmdShow);

    void show();
    void set_title(const std::wstring& title);

    // Expose native handle without dragging <Windows.h> into headers.
    HWND hwnd() const noexcept; // returns HWND (typedefed above)

private:
    // Owning pointer to an incomplete type. This is safe in a header.
    std::unique_ptr<WinWindowState> state_;
};

} // namespace colony::win
