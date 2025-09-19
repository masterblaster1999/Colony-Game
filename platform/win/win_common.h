// platform/win/win_common.h
#pragma once

// Keep Windows.h tidy and safe
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <string_view>

namespace win {

// RAII wrapper for HANDLE to avoid leaks on early-return paths.
struct handle {
    HANDLE h{nullptr};
    handle() = default;
    explicit handle(HANDLE hh) : h(hh) {}
    ~handle() { reset(); }

    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;

    handle(handle&& o) noexcept : h(o.h) { o.h = nullptr; }
    handle& operator=(handle&& o) noexcept {
        if (this != &o) { reset(); h = o.h; o.h = nullptr; }
        return *this;
    }

    bool valid() const noexcept { return h && h != INVALID_HANDLE_VALUE; }
    HANDLE get()   const noexcept { return h; }
    HANDLE release() noexcept { HANDLE t = h; h = nullptr; return t; }
    void reset(HANDLE nh = nullptr) noexcept {
        if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
        h = nh;
    }
};

// UTF-8 console setup (idempotent): lets you use std::cout/std::wcout with UTF‑8.
inline void setup_utf8_console() {
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
}

// Narrow <-> wide helpers (UTF‑8)
inline std::wstring widen(std::string_view s) {
    if (s.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

inline std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

// Win32 error message to std::wstring (for good diagnostics).
inline std::wstring error_text(DWORD ec) {
    LPWSTR buf = nullptr;
    DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, ec, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = len ? std::wstring(buf, len) : L"(unknown error)";
    if (buf) ::LocalFree(buf);
    return msg;
}

} // namespace win
