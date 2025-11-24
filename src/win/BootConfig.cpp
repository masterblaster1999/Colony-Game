// src/win/BootConfig.cpp
//
// Windows-only boot configuration reader with robust Unicode handling.
//
// Changes vs. previous version:
//  - Reads environment variables with a dynamic buffer (no 256-char limit).
//  - Parses numeric values from wide strings directly (std::stod(wstring)).
//  - Adds robust boolean parsing (1/0, true/false, yes/no, on/off).
//  - Adds Trim() and WideToUtf8() helpers.
//  - Adds debug logging of the parsed config via OutputDebugStringA.
//  - Optionally uses your unified Win headers when available to avoid macro noise.

#ifdef __has_include
#  if __has_include("platform/win/WinHeaders.h")
#    include "platform/win/WinHeaders.h"
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <Windows.h>
#  endif
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

#include <string>
#include <string_view>
#include <cwctype>    // iswspace, towlower
#include <stdexcept>  // std::stod
#include <cstdio>     // std::snprintf

// If this struct lives in a header elsewhere in your tree, keep this in sync.
struct BootConfig {
  std::wstring presentMode;   // "flip_discard" | "blt" | ...
  bool   allowTearing = false;
  double fixedDtMs    = 0.0;  // 0 = variable
  double maxFrameMs   = 250.0;
};

namespace
{
    // Trim leading/trailing ASCII/Unicode whitespace.
    static std::wstring Trim(std::wstring s)
    {
        std::size_t i = 0;
        std::size_t j = s.size();

        while (i < j && iswspace(static_cast<wint_t>(s[i]))) {
            ++i;
        }
        while (j > i && iswspace(static_cast<wint_t>(s[j - 1]))) {
            --j;
        }
        return s.substr(i, j - i);
    }

    // Robust environment reader (wide). Handles arbitrary sizes.
    // Per GetEnvironmentVariableW docs: first call with nullptr to get required size,
    // then call again to retrieve the string. Return empty on not found.
    static std::wstring ReadEnvW(const wchar_t* key)
    {
        DWORD required = ::GetEnvironmentVariableW(key, nullptr, 0);
        if (required == 0) {
            // Not present or empty. Treat as "not set".
            return std::wstring();
        }

        // 'required' includes the terminating null.
        std::wstring buf;
        buf.resize(required);
        DWORD written = ::GetEnvironmentVariableW(key, buf.data(), required);
        if (written == 0) {
            return std::wstring();
        }
        buf.resize(written); // drop the null terminator
        return buf;
    }

    // Convert UTF-16 (Windows wide) -> UTF-8 using Win32 API.
    // Useful for logging to narrow systems or serializing to UTF-8.
    static std::string WideToUtf8(std::wstring_view w)
    {
        if (w.empty()) {
            return {};
        }

        // First pass: get required size in bytes (no terminator is written for explicit length).
        int n = ::WideCharToMultiByte(
            CP_UTF8,
            0, // or WC_ERR_INVALID_CHARS for strictness on Vista+
            w.data(),
            static_cast<int>(w.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (n <= 0) {
            return {};
        }

        std::string out;
        out.resize(static_cast<std::size_t>(n));

        // Second pass: perform the conversion.
        int n2 = ::WideCharToMultiByte(
            CP_UTF8,
            0, // or WC_ERR_INVALID_CHARS
            w.data(),
            static_cast<int>(w.size()),
            out.data(),
            n,
            nullptr,
            nullptr
        );
        if (n2 != n) {
            // Defensive: if API returns a different size for the second call, shrink if needed.
            if (n2 > 0 && n2 < n) {
                out.resize(static_cast<std::size_t>(n2));
            }
        }

        return out;
    }

    // Case-insensitive boolean parse from wide strings.
    // Accepts: 1/0, true/false, yes/no, on/off.
    static bool TryParseBoolW(const std::wstring& s, bool& out) noexcept
    {
        std::wstring t = Trim(s);
        for (auto& ch : t) {
            ch = static_cast<wchar_t>(towlower(ch));
        }

        if (t == L"1" || t == L"true"  || t == L"yes" || t == L"on")  { out = true;  return true; }
        if (t == L"0" || t == L"false" || t == L"no"  || t == L"off") { out = false; return true; }
        return false;
    }

    // Parse a double from a wide string; allows an optional "ms" suffix.
    static bool TryParseMillisecondsW(const std::wstring& s, double& out) noexcept
    {
        if (s.empty()) {
            return false;
        }

        std::wstring t = Trim(s);
        // Allow simple "123ms" suffix (any case).
        if (t.size() >= 2) {
            wchar_t c1 = t[t.size() - 2];
            wchar_t c2 = t[t.size() - 1];
            if ((c1 == L'm' || c1 == L'M') && (c2 == L's' || c2 == L'S')) {
                t.erase(t.size() - 2);
                t = Trim(t);
            }
        }

        try {
            std::size_t idx = 0;
            double v = std::stod(t, &idx); // Wide overload: no lossy conversions.
            // Only trailing whitespace is acceptable.
            while (idx < t.size() && iswspace(static_cast<wint_t>(t[idx]))) {
                ++idx;
            }
            if (idx != t.size()) {
                return false;
            }
            out = v;
            return true;
        } catch (...) {
            return false;
        }
    }

    // Debug logging of the parsed config to the Visual Studio Output window.
    // This both improves debuggability and ensures WideToUtf8 is actually referenced,
    // avoiding MSVC's C4505 "unreferenced function with internal linkage" warning.
    static void DebugLogBootConfig(const BootConfig& cfg)
    {
        std::string modeUtf8 = WideToUtf8(cfg.presentMode);

        char buffer[256];
        // Note: snprintf returns number of chars that would have been written;
        // we don't need it here, we just ensure null termination.
        std::snprintf(
            buffer,
            sizeof(buffer),
            "BootConfig: presentMode='%s', allowTearing=%d, fixedDtMs=%.3f, maxFrameMs=%.3f\n",
            modeUtf8.c_str(),
            cfg.allowTearing ? 1 : 0,
            cfg.fixedDtMs,
            cfg.maxFrameMs
        );

        ::OutputDebugStringA(buffer);
    }

} // namespace

BootConfig GetBootConfigFromEnv()
{
    BootConfig cfg;

    if (auto mode = ReadEnvW(L"COLONY_PRESENT_MODE"); !mode.empty()) {
        cfg.presentMode = Trim(mode);
        // If you need a UTF-8 narrow copy for other systems, you can call WideToUtf8(cfg.presentMode)
        // where appropriate. For now, DebugLogBootConfig below will log it.
    }

    if (auto s = ReadEnvW(L"COLONY_PRESENT_ALLOW_TEARING"); !s.empty()) {
        bool b = false;
        if (TryParseBoolW(s, b)) {
            cfg.allowTearing = b;
        }
    }

    if (auto s = ReadEnvW(L"COLONY_SIM_FIXED_DT_MS"); !s.empty()) {
        double v = 0.0;
        if (TryParseMillisecondsW(s, v)) {
            cfg.fixedDtMs = v;
        }
    }

    if (auto s = ReadEnvW(L"COLONY_SIM_MAX_FRAME_MS"); !s.empty()) {
        double v = 0.0;
        if (TryParseMillisecondsW(s, v)) {
            cfg.maxFrameMs = v;
        }
    }

    // Basic sanitization
    if (cfg.fixedDtMs < 0.0)  cfg.fixedDtMs = 0.0;   // 0 means "variable"
    if (cfg.maxFrameMs < 1.0) cfg.maxFrameMs = 1.0;  // avoid pathological zero/negatives

    // Log what we ended up with; useful when debugging startup behaviour
    // and ensures WideToUtf8 is referenced in all build configurations.
    DebugLogBootConfig(cfg);

    return cfg;
}
