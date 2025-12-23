#pragma once

// src/input/InputBindingParse.h
// ----------------------------
// Header-only helpers for parsing human-readable input binding tokens.
//
// Why header-only?
//  - Keeps the parser easy to unit test (tests just include this file).
//  - Avoids adding a new library target just for config parsing utilities.
//
// Design:
//  - Does *not* include <windows.h>.
//  - Uses a small curated set of Win32 virtual-key code constants.
//  - Uses "kVK_*" names to avoid collisions with Win32 macros like VK_SHIFT.

#include "input/InputEvent.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace colony::input::bindings {

// --- Win32 virtual-key codes (subset) -----------------------------------------------------------
// Reference: WinUser.h virtual-key codes.
// We hardcode the values we need to keep this parser platform-agnostic.
inline constexpr std::uint32_t kVK_BACK    = 0x08; // Backspace
inline constexpr std::uint32_t kVK_TAB     = 0x09;
inline constexpr std::uint32_t kVK_RETURN  = 0x0D; // Enter

inline constexpr std::uint32_t kVK_SHIFT   = 0x10;
inline constexpr std::uint32_t kVK_CONTROL = 0x11;
inline constexpr std::uint32_t kVK_MENU    = 0x12; // Alt

inline constexpr std::uint32_t kVK_ESCAPE  = 0x1B;

inline constexpr std::uint32_t kVK_SPACE   = 0x20;
inline constexpr std::uint32_t kVK_PRIOR   = 0x21; // Page Up
inline constexpr std::uint32_t kVK_NEXT    = 0x22; // Page Down
inline constexpr std::uint32_t kVK_END     = 0x23;
inline constexpr std::uint32_t kVK_HOME    = 0x24;
inline constexpr std::uint32_t kVK_LEFT    = 0x25;
inline constexpr std::uint32_t kVK_UP      = 0x26;
inline constexpr std::uint32_t kVK_RIGHT   = 0x27;
inline constexpr std::uint32_t kVK_DOWN    = 0x28;
inline constexpr std::uint32_t kVK_INSERT  = 0x2D;
inline constexpr std::uint32_t kVK_DELETE  = 0x2E;

inline constexpr std::uint32_t kVK_LSHIFT   = 0xA0;
inline constexpr std::uint32_t kVK_RSHIFT   = 0xA1;
inline constexpr std::uint32_t kVK_LCONTROL = 0xA2;
inline constexpr std::uint32_t kVK_RCONTROL = 0xA3;
inline constexpr std::uint32_t kVK_LMENU    = 0xA4;
inline constexpr std::uint32_t kVK_RMENU    = 0xA5;

inline constexpr std::uint32_t kVK_F1  = 0x70;
inline constexpr std::uint32_t kVK_F24 = 0x87;

// --- Small string helpers ----------------------------------------------------------------------

[[nodiscard]]
inline bool IsWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]]
inline std::string_view Trim(std::string_view s) noexcept
{
    while (!s.empty() && IsWhitespace(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsWhitespace(s.back())) s.remove_suffix(1);
    return s;
}

[[nodiscard]]
inline std::string ToLowerCopy(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

[[nodiscard]]
inline std::vector<std::string_view> Split(std::string_view s, char delim)
{
    std::vector<std::string_view> out;
    while (true)
    {
        const std::size_t pos = s.find(delim);
        if (pos == std::string_view::npos)
        {
            out.push_back(s);
            break;
        }
        out.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
    return out;
}

// Parses a single token like "W", "Shift", "MouseLeft", "F5", "Esc".
// Returns the unified input code (keyboard VK codes 0..255, or mouse codes >= kMouseCodeBase).
[[nodiscard]]
inline std::optional<std::uint32_t> ParseInputCodeToken(std::string_view token) noexcept
{
    const std::string t = ToLowerCopy(Trim(token));
    if (t.empty())
        return std::nullopt;

    // Single character: treat as ASCII key. Normalize to uppercase.
    if (t.size() == 1)
    {
        const unsigned char c = static_cast<unsigned char>(t[0]);
        if (std::isalnum(c))
            return static_cast<std::uint32_t>(std::toupper(c));
    }

    // Function keys: F1..F24
    if (t.size() >= 2 && t[0] == 'f')
    {
        int n = 0;
        for (std::size_t i = 1; i < t.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (!std::isdigit(c)) { n = 0; break; }
            n = (n * 10) + (t[i] - '0');
        }
        if (n >= 1 && n <= 24)
            return static_cast<std::uint32_t>(kVK_F1 + static_cast<std::uint32_t>(n - 1));
    }

    // Arrow keys
    if (t == "up" || t == "arrowup") return kVK_UP;
    if (t == "down" || t == "arrowdown") return kVK_DOWN;
    if (t == "left" || t == "arrowleft") return kVK_LEFT;
    if (t == "right" || t == "arrowright") return kVK_RIGHT;

    // Common named keys
    if (t == "space" || t == "spacebar") return kVK_SPACE;
    if (t == "esc" || t == "escape") return kVK_ESCAPE;
    if (t == "tab") return kVK_TAB;
    if (t == "enter" || t == "return") return kVK_RETURN;
    if (t == "backspace" || t == "bksp" || t == "bs") return kVK_BACK;

    if (t == "insert" || t == "ins") return kVK_INSERT;
    if (t == "delete" || t == "del") return kVK_DELETE;
    if (t == "home") return kVK_HOME;
    if (t == "end") return kVK_END;
    if (t == "pageup" || t == "pgup" || t == "prior") return kVK_PRIOR;
    if (t == "pagedown" || t == "pgdn" || t == "next") return kVK_NEXT;

    // Modifiers
    if (t == "shift") return kVK_SHIFT;
    if (t == "lshift" || t == "leftshift") return kVK_LSHIFT;
    if (t == "rshift" || t == "rightshift") return kVK_RSHIFT;

    if (t == "ctrl" || t == "control") return kVK_CONTROL;
    if (t == "lctrl" || t == "leftctrl" || t == "lcontrol" || t == "leftcontrol") return kVK_LCONTROL;
    if (t == "rctrl" || t == "rightctrl" || t == "rcontrol" || t == "rightcontrol") return kVK_RCONTROL;

    if (t == "alt" || t == "menu") return kVK_MENU;
    if (t == "lalt" || t == "leftalt" || t == "lmenu" || t == "leftmenu") return kVK_LMENU;
    if (t == "ralt" || t == "rightalt" || t == "rmenu" || t == "rightmenu") return kVK_RMENU;

    // Mouse buttons (mapped into the unified input code space)
    if (t == "mouseleft" || t == "lmb" || t == "mouse1") return colony::input::kMouseButtonLeft;
    if (t == "mouseright" || t == "rmb" || t == "mouse2") return colony::input::kMouseButtonRight;
    if (t == "mousemiddle" || t == "mmb" || t == "mouse3") return colony::input::kMouseButtonMiddle;
    if (t == "mousex1" || t == "x1" || t == "mouse4" || t == "mb4") return colony::input::kMouseButtonX1;
    if (t == "mousex2" || t == "x2" || t == "mouse5" || t == "mb5") return colony::input::kMouseButtonX2;

    return std::nullopt;
}

// Parses a chord string like: "Shift+W" or "Shift+MouseLeft".
// Returns a sorted, de-duplicated list of input codes.
[[nodiscard]]
inline bool ParseChordString(std::string_view chordStr, std::vector<std::uint32_t>& outCodes) noexcept
{
    outCodes.clear();
    chordStr = Trim(chordStr);
    if (chordStr.empty())
        return false;

    const auto parts = Split(chordStr, '+');
    for (auto part : parts)
    {
        part = Trim(part);
        if (part.empty())
            continue;

        const auto code = ParseInputCodeToken(part);
        if (!code)
            return false;

        outCodes.push_back(*code);
    }

    if (outCodes.empty())
        return false;

    std::sort(outCodes.begin(), outCodes.end());
    outCodes.erase(std::unique(outCodes.begin(), outCodes.end()), outCodes.end());
    return !outCodes.empty();
}

} // namespace colony::input::bindings
