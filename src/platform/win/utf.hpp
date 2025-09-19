// src/platform/win/utf.hpp
#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace utf {

inline std::wstring to_utf16(const std::string& s_utf8) {
    if (s_utf8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s_utf8.c_str(), -1, nullptr, 0);
    std::wstring out;
    out.resize(static_cast<size_t>(wlen ? wlen - 1 : 0));
    if (wlen > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s_utf8.c_str(), -1, out.data(), wlen);
    }
    return out;
}

inline std::string to_utf8(const std::wstring& s_utf16) {
    if (s_utf16.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s_utf16.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out;
    out.resize(static_cast<size_t>(len ? len - 1 : 0));
    if (len > 1) {
        WideCharToMultiByte(CP_UTF8, 0, s_utf16.c_str(), -1, out.data(), len, nullptr, nullptr);
    }
    return out;
}

} // namespace utf
