// src/platform/win/WinUtils.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <vector>

inline std::filesystem::path GetExecutableDir()
{
    std::vector<wchar_t> buf(260); // start near MAX_PATH, grow if needed
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }

        // If n < buf.size(), we got the full string (no truncation).
        if (n < buf.size()) {
            std::filesystem::path p(buf.data(), buf.data() + n);
            p.remove_filename();
            return p;
        }

        // Likely truncated -> grow and retry
        if (buf.size() > (1u << 20)) { // sanity cap (~1M wchar)
            return {};
        }
        buf.resize(buf.size() * 2);
    }
}
