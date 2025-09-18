#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <string_view>

namespace win {
    inline std::string WideToUtf8(std::wstring_view w) {
        if (w.empty()) return {};
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(needed, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), needed, nullptr, nullptr);
        return out;
    }

    inline std::wstring Utf8ToWide(std::string_view s) {
        if (s.empty()) return {};
        const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out(needed, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), needed);
        return out;
    }
}
