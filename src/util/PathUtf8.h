#pragma once

// util/PathUtf8.h
// ---------------
// Small helpers for converting std::filesystem::path to UTF-8 std::string.
//
// Motivation:
//  - On Windows, std::filesystem::path is natively wide.
//  - In C++20+, path::u8string() returns std::u8string (char8_t).
//  - Many libraries/UI layers (e.g. ImGui) expect UTF-8 encoded `char*`.
//
// This header provides a tiny, dependency-free bridge that works in both
// C++17 (u8string -> std::string) and C++20+ (u8string -> std::u8string).

#include <filesystem>
#include <string>
#include <string_view>
#include <cstring>

namespace colony::util {

namespace fs = std::filesystem;

#if defined(__cpp_char8_t) && (__cpp_char8_t >= 201811L)

// Convert a u8string_view (char8_t bytes) to a standard UTF-8 std::string.
[[nodiscard]] inline std::string U8ToString(std::u8string_view u8)
{
    std::string out;
    out.resize(u8.size());
    if (!u8.empty())
        std::memcpy(out.data(), u8.data(), u8.size());
    return out;
}

[[nodiscard]] inline std::string PathToUtf8String(const fs::path& p)
{
    const std::u8string u8 = p.u8string();
    return U8ToString(std::u8string_view{u8});
}

#else

[[nodiscard]] inline std::string PathToUtf8String(const fs::path& p)
{
    // Pre-C++20, u8string() already returns std::string.
    return p.u8string();
}

#endif

} // namespace colony::util
