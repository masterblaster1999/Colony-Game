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
#include <cstdio>
#include <cstdint>
#include <limits>
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

// Numpad / keypad
inline constexpr std::uint32_t kVK_NUMPAD0  = 0x60;
inline constexpr std::uint32_t kVK_NUMPAD1  = 0x61;
inline constexpr std::uint32_t kVK_NUMPAD2  = 0x62;
inline constexpr std::uint32_t kVK_NUMPAD3  = 0x63;
inline constexpr std::uint32_t kVK_NUMPAD4  = 0x64;
inline constexpr std::uint32_t kVK_NUMPAD5  = 0x65;
inline constexpr std::uint32_t kVK_NUMPAD6  = 0x66;
inline constexpr std::uint32_t kVK_NUMPAD7  = 0x67;
inline constexpr std::uint32_t kVK_NUMPAD8  = 0x68;
inline constexpr std::uint32_t kVK_NUMPAD9  = 0x69;
inline constexpr std::uint32_t kVK_MULTIPLY = 0x6A;
inline constexpr std::uint32_t kVK_ADD      = 0x6B;
inline constexpr std::uint32_t kVK_SEPARATOR = 0x6C;
inline constexpr std::uint32_t kVK_SUBTRACT = 0x6D;
inline constexpr std::uint32_t kVK_DECIMAL  = 0x6E;
inline constexpr std::uint32_t kVK_DIVIDE   = 0x6F;
inline constexpr std::uint32_t kVK_NUMLOCK  = 0x90;

inline constexpr std::uint32_t kVK_F1  = 0x70;
inline constexpr std::uint32_t kVK_F2  = 0x71;
inline constexpr std::uint32_t kVK_F24 = 0x87;


// Helper for readability when referring to function keys in code.
[[nodiscard]] inline constexpr std::uint32_t VK_F(std::uint32_t n) noexcept
{
    return (n >= 1 && n <= 24) ? (kVK_F1 + (n - 1)) : 0u;
}
static_assert(VK_F(1) == kVK_F1);
static_assert(VK_F(2) == kVK_F2);
static_assert(VK_F(24) == kVK_F24);
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

    const std::string_view tv(t);

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


    // Numpad / keypad
    if (t == "numlock") return kVK_NUMLOCK;

    auto parseKeypadSuffix = [](std::string_view rest) noexcept -> std::optional<std::uint32_t>
    {
        if (rest.empty())
            return std::nullopt;

        // Digits: 0..9
        if (rest.size() == 1)
        {
            const char c = rest[0];
            if (c >= '0' && c <= '9')
                return kVK_NUMPAD0 + static_cast<std::uint32_t>(c - '0');
        }

        // Operations
        if (rest == "add" || rest == "plus") return kVK_ADD;
        if (rest == "subtract" || rest == "minus") return kVK_SUBTRACT;
        if (rest == "multiply" || rest == "mul" || rest == "asterisk") return kVK_MULTIPLY;
        if (rest == "divide" || rest == "div" || rest == "slash") return kVK_DIVIDE;
        if (rest == "decimal" || rest == "dot" || rest == "period") return kVK_DECIMAL;
        if (rest == "separator") return kVK_SEPARATOR;

        return std::nullopt;
    };

    // Accept a few common prefixes:
    //   Numpad0..9 / NumpadAdd / NumpadSubtract / ...
    //   Num0..9    / NumAdd    / ...
    //   KP0..9     / KPAdd     / KPPlus / ...
    if (tv.rfind("numpad", 0) == 0)
    {
        if (auto v = parseKeypadSuffix(tv.substr(6)))
            return *v;
    }

    if (tv.rfind("num", 0) == 0)
    {
        if (auto v = parseKeypadSuffix(tv.substr(3)))
            return *v;
    }

    if (tv.rfind("kp", 0) == 0)
    {
        if (auto v = parseKeypadSuffix(tv.substr(2)))
            return *v;
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

    // Mouse wheel (impulse-style bindings)
    if (t == "wheelup" || t == "mwheelup" || t == "mousewheelup" || t == "scrollup")
        return colony::input::kMouseWheelUp;
    if (t == "wheeldown" || t == "mwheeldown" || t == "mousewheeldown" || t == "scrolldown")
        return colony::input::kMouseWheelDown;

    // Hex virtual-key tokens (round-trip for InputCodeToToken fallback).
    // Accepts (case-insensitive):
    //   VK_0x1B   (InputCodeToToken fallback format)
    //   0x1B
    //
    // NOTE: This only supports keyboard VK codes (0..255). Mouse codes have dedicated tokens.

    auto parseHexU32 = [](std::string_view hex) noexcept -> std::optional<std::uint32_t>
    {
        if (hex.empty())
            return std::nullopt;

        std::uint32_t v = 0;
        for (char c : hex)
        {
            unsigned nib = 0;
            if (c >= '0' && c <= '9')
                nib = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                nib = 10u + static_cast<unsigned>(c - 'a');
            else if (c >= 'A' && c <= 'F')
                nib = 10u + static_cast<unsigned>(c - 'A');
            else
                return std::nullopt;

            // Prevent overflow for absurdly long tokens.
            if (v > (std::numeric_limits<std::uint32_t>::max)() / 16u)
                return std::nullopt;

            v = (v * 16u) + static_cast<std::uint32_t>(nib);
        }
        return v;
    };

    if (tv.rfind("vk_0x", 0) == 0)
    {
        if (auto v = parseHexU32(tv.substr(5)))
        {
            if (*v <= 0xFFu)
                return *v;
        }
        return std::nullopt;
    }

    if (tv.rfind("0x", 0) == 0)
    {
        if (auto v = parseHexU32(tv.substr(2)))
        {
            if (*v <= 0xFFu)
                return *v;
        }
        return std::nullopt;
    }

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

// Converts a unified input code back into a human-friendly token suitable for
// config files / UI.
//
// NOTE: This is not intended to be a perfect round-trip for every possible VK
// code, but it covers the project's supported tokens.
[[nodiscard]]
inline std::string InputCodeToToken(std::uint32_t code)
{
    // Mouse buttons / wheel.
    switch (code) {
    case colony::input::kMouseButtonLeft:   return "MouseLeft";
    case colony::input::kMouseButtonRight:  return "MouseRight";
    case colony::input::kMouseButtonMiddle: return "MouseMiddle";
    case colony::input::kMouseButtonX1:     return "MouseX1";
    case colony::input::kMouseButtonX2:     return "MouseX2";
    case colony::input::kMouseWheelUp:      return "WheelUp";
    case colony::input::kMouseWheelDown:    return "WheelDown";
    default: break;
    }


    // Numpad / keypad
    if (code == kVK_NUMLOCK) return "NumLock";

    if (code >= kVK_NUMPAD0 && code <= kVK_NUMPAD9) {
        const std::uint32_t d = (code - kVK_NUMPAD0);
        return "Numpad" + std::to_string(d);
    }

    if (code == kVK_MULTIPLY) return "NumpadMultiply";
    if (code == kVK_ADD) return "NumpadAdd";
    if (code == kVK_SEPARATOR) return "NumpadSeparator";
    if (code == kVK_SUBTRACT) return "NumpadSubtract";
    if (code == kVK_DECIMAL) return "NumpadDecimal";
    if (code == kVK_DIVIDE) return "NumpadDivide";
    // Function keys.
    if (code >= kVK_F1 && code <= kVK_F24) {
        const std::uint32_t n = (code - kVK_F1) + 1;
        return "F" + std::to_string(n);
    }

    // Single printable alnum keys.
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string(1, static_cast<char>(code));
    }

    // Arrow keys.
    if (code == kVK_UP) return "Up";
    if (code == kVK_DOWN) return "Down";
    if (code == kVK_LEFT) return "Left";
    if (code == kVK_RIGHT) return "Right";

    // Common named keys.
    if (code == kVK_SPACE) return "Space";
    if (code == kVK_ESCAPE) return "Esc";
    if (code == kVK_TAB) return "Tab";
    if (code == kVK_RETURN) return "Enter";
    if (code == kVK_BACK) return "Backspace";
    if (code == kVK_INSERT) return "Insert";
    if (code == kVK_DELETE) return "Delete";
    if (code == kVK_HOME) return "Home";
    if (code == kVK_END) return "End";
    if (code == kVK_PRIOR) return "PageUp";
    if (code == kVK_NEXT) return "PageDown";

    // Modifiers.
    if (code == kVK_SHIFT) return "Shift";
    if (code == kVK_LSHIFT) return "LShift";
    if (code == kVK_RSHIFT) return "RShift";

    if (code == kVK_CONTROL) return "Ctrl";
    if (code == kVK_LCONTROL) return "LCtrl";
    if (code == kVK_RCONTROL) return "RCtrl";

    if (code == kVK_MENU) return "Alt";
    if (code == kVK_LMENU) return "LAlt";
    if (code == kVK_RMENU) return "RAlt";

    // Fallback: preserve as hex.
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "VK_0x%02X", static_cast<unsigned int>(code & 0xFFu));
    return std::string(buf);
}

} // namespace colony::input::bindings
