// File: platform/win/win_utf.h
//
// Tiny Windows-only UTF conversion helpers.
// - UTF-16 (Windows wide) <-> UTF-8 conversions
// - Header-only, inline functions
// - No exceptions; returns empty on conversion failure
//
// Usage (example):
//   #include "win_utf.h"
//   std::wstring w = L"C:\\Users\\Jörg\\AppData";
//   std::string  u8 = narrow_utf8(w);  // UTF-8
//
//   std::string s = "こんにちは";
//   std::wstring w2 = widen_utf16(s);  // UTF-16
//
//   // For filesystem::path on Windows (native wide):
//   std::filesystem::path p = L"C:\\ゲーム\\Colony";
//   std::string u8path = narrow_utf8(p); // UTF-8 string for logging, etc.

#pragma once

#if !defined(_WIN32)
#  error "win_utf.h is Windows-only. Include this file only in Windows builds."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <string_view>
#include <filesystem>
#include <cwchar>   // wcslen
#include <cstring>  // strlen
#include <climits>  // INT_MAX

// Sanity check: on Windows, wchar_t is 2 bytes (UTF-16 code unit).
static_assert(sizeof(wchar_t) == 2, "This helper expects Windows wchar_t (2 bytes).");

// -----------------------------
// UTF-16 (wide) -> UTF-8 (narrow)
// -----------------------------
inline std::string narrow_utf8(std::wstring_view w)
{
    if (w.empty()) return {};

    // Avoid int overflow on very large inputs
    if (w.size() > static_cast<size_t>(INT_MAX)) {
        w = { w.data(), static_cast<size_t>(INT_MAX) };
    }

    constexpr UINT cp = CP_UTF8;
    DWORD flags = WC_ERR_INVALID_CHARS;

    // First pass: get required size
    int needed = ::WideCharToMultiByte(
        cp, flags,
        w.data(), static_cast<int>(w.size()),
        nullptr, 0, nullptr, nullptr
    );

    if (needed <= 0) {
        // Fallback without strict validation (maps ill-formed sequences to '?')
        flags = 0;
        needed = ::WideCharToMultiByte(
            cp, flags,
            w.data(), static_cast<int>(w.size()),
            nullptr, 0, nullptr, nullptr
        );
        if (needed <= 0) return {};
    }

    std::string out(static_cast<size_t>(needed), '\0');

    // Second pass: actual conversion
    int written = ::WideCharToMultiByte(
        cp, flags,
        w.data(), static_cast<int>(w.size()),
        out.data(), needed, nullptr, nullptr
    );

    if (written > 0 && written != needed) {
        out.resize(static_cast<size_t>(written));
    }
    return out;
}

// Convenience overloads
inline std::string narrow_utf8(const std::wstring& w) {
    return narrow_utf8(std::wstring_view{ w.data(), w.size() });
}
inline std::string narrow_utf8(const wchar_t* w) {
    return w ? narrow_utf8(std::wstring_view{ w, std::wcslen(w) }) : std::string{};
}
inline std::string narrow_utf8(const std::filesystem::path& p) {
    // On Windows, native() is std::wstring
    const auto& w = p.native();
    return narrow_utf8(std::wstring_view{ w.data(), w.size() });
}

// -----------------------------
// UTF-8 (narrow) -> UTF-16 (wide)
// -----------------------------
inline std::wstring widen_utf16(std::string_view s)
{
    if (s.empty()) return {};

    if (s.size() > static_cast<size_t>(INT_MAX)) {
        s = { s.data(), static_cast<size_t>(INT_MAX) };
    }

    constexpr UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;

    // First pass: required size
    int needed = ::MultiByteToWideChar(
        cp, flags,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0
    );

    if (needed <= 0) {
        // Fallback without strict validation
        flags = 0;
        needed = ::MultiByteToWideChar(
            cp, flags,
            s.data(), static_cast<int>(s.size()),
            nullptr, 0
        );
        if (needed <= 0) return {};
    }

    std::wstring out(static_cast<size_t>(needed), L'\0');

    // Second pass: actual conversion
    int written = ::MultiByteToWideChar(
        cp, flags,
        s.data(), static_cast<int>(s.size()),
        out.data(), needed
    );

    if (written > 0 && written != needed) {
        out.resize(static_cast<size_t>(written));
    }
    return out;
}

// Convenience overloads
inline std::wstring widen_utf16(const std::string& s) {
    return widen_utf16(std::string_view{ s.data(), s.size() });
}
inline std::wstring widen_utf16(const char* s) {
    return s ? widen_utf16(std::string_view{ s, std::strlen(s) }) : std::wstring{};
}
