// platform/win/win_common.hpp
#pragma once

#ifndef NOMINMAX
#  define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif

#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace win {

// Make the process per‑monitor DPI aware (V2) if available, fallback to system DPI.
// This should be called before creating any HWND.
inline void make_per_monitor_dpi_aware() noexcept {
    // Avoid link‑time dependency on latest SDK APIs.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(void*);
        auto set_ctx = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_ctx) {
            // Constant value per MS docs; available on Win 10 Creators Update+.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((void*)-4)
#endif
            set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    // Older fallback
    ::SetProcessDPIAware();
}

// Optional: ensure console uses UTF‑8 if you print anything there.
inline void set_console_utf8() noexcept {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}

// UTF‑8 <-> UTF‑16 helpers for file/Windows API boundaries.
inline std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return L"";
    int need = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return L"";
    std::wstring w(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                          s.data(), static_cast<int>(s.size()), w.data(), need);
    return w;
}

inline std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    int need = ::WideCharToMultiByte(CP_UTF8, 0,
                                     w.data(), static_cast<int>(w.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string s(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0,
                          w.data(), static_cast<int>(w.size()),
                          s.data(), need, nullptr, nullptr);
    return s;
}

} // namespace win
