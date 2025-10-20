#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include "WinUtils.h"

namespace win {

std::string utf8_from_wstring(std::wstring_view w) {
    if (w.empty()) return {};
    // First call to get required size (no NUL added by API)
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                     static_cast<int>(w.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(),
                          static_cast<int>(w.size()),
                          out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring wstring_from_utf8(std::string_view u8) {
    if (u8.empty()) return {};
    int size = ::MultiByteToWideChar(CP_UTF8, 0, u8.data(),
                                     static_cast<int>(u8.size()),
                                     nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, u8.data(),
                          static_cast<int>(u8.size()),
                          out.data(), size);
    return out;
}

} // namespace win
