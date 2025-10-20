#pragma once
#include <string>
#include <string_view>

namespace win {
// UTF-16 (wchar_t) -> UTF-8 (std::string)
std::string utf8_from_wstring(std::wstring_view w);

// UTF-8 (std::string_view) -> UTF-16 (std::wstring)
std::wstring wstring_from_utf8(std::string_view u8);
} // namespace win
