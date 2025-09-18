#pragma once
#include <string>
#include <string_view>
#include "platform/win/WinSDK.hpp"

namespace launcher {

// Trim trailing CR/LF from FormatMessage text
inline void trim_crlf(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
}

inline std::string win32_error_to_string(DWORD code) {
    if (code == 0u) return "The operation completed successfully.";
    LPSTR buf = nullptr;
    const DWORD len = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);

    std::string msg;
    if (len && buf) {
        msg.assign(buf, len);
        ::LocalFree(buf);
        trim_crlf(msg);
    } else {
        msg = "Unknown error (" + std::to_string(code) + ")";
    }
    return msg;
}

// Friendly stringify for HRESULT (handles both COM HRESULTs and Win32-mapped HRESULTs)
inline std::string hresult_to_string(HRESULT hr) {
    // If this is actually a Win32 error wrapped in an HRESULT, unwrap and use the Win32 path:
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        return win32_error_to_string(static_cast<DWORD>(HRESULT_CODE(hr)));
    }
    // Otherwise ask the system message tables directly about the HRESULT:
    LPSTR buf = nullptr;
    const DWORD len = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);

    std::string msg;
    if (len && buf) {
        msg.assign(buf, len);
        ::LocalFree(buf);
        trim_crlf(msg);
    } else {
        msg = "Unknown HRESULT (0x" + std::to_string(static_cast<unsigned>(hr)) + ")";
    }
    return msg;
}

} // namespace launcher
