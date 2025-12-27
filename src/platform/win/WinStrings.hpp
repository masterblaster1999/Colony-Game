#pragma once
// WinStrings.hpp — Windows UTF‑8 / Wide helpers + char8_t bridge (header‑only, Windows‑only)

#ifndef _WIN32
#error "WinStrings.hpp is Windows-only."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <string>
#include <string_view>
#include <cstring>   // std::strlen
#include <cassert>

// ---------- UTF-8 <-> Wide ----------
inline std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (need <= 0) return {};
    std::wstring w(need, L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), need);
    return w;
}
inline std::string WideToUtf8(std::wstring_view ws) {
    if (ws.empty()) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(need, '\0');
    (void)::WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), need, nullptr, nullptr);
    return out;
}

// ---------- char8_t bridges (byte-preserving; assumes /utf-8) ----------
inline std::u8string ToU8(std::string_view s) {
    return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size());
}
inline std::string ToString(std::u8string_view u8) {
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Convenience for literals
inline std::u8string U8Lit(const char* s) {
    return std::u8string(reinterpret_cast<const char8_t*>(s), std::strlen(s));
}

// ---------- Bridge operators so "literal" + u8string works ----------
inline std::u8string operator+(const char* a, const std::u8string& b) {
    std::u8string r = U8Lit(a);
    r += b;
    return r;
}
inline std::u8string operator+(const std::u8string& a, const char* b) {
    std::u8string r = a;
    r += U8Lit(b);
    return r;
}
inline std::u8string operator+(const std::string& a, const std::u8string& b) {
    return ToU8(a) + b;
}
inline std::u8string operator+(const std::u8string& a, const std::string& b) {
    return a + ToU8(b);
}

// Optional: helpers to go wide directly from u8
inline std::wstring U8ToWide(std::u8string_view u8) { return Utf8ToWide(ToString(u8)); }
inline std::u8string WideToU8(std::wstring_view ws) { return ToU8(WideToUtf8(ws)); }
