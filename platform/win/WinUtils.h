#pragma once
#include <string>
#include <string_view>
#include <windows.h>

namespace win {

// Wide → UTF-8 (std::string)
inline std::string to_utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                  wide.data(), static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                          wide.data(), static_cast<int>(wide.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

// UTF-8 → Wide (std::wstring)
inline std::wstring to_wide(std::string_view u8) {
    if (u8.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  u8.data(), static_cast<int>(u8.size()),
                                  nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                          u8.data(), static_cast<int>(u8.size()),
                          out.data(), n);
    return out;
}

} // namespace win
