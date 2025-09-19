// platform/win/WinPaths.hpp
#pragma once
#include "platform/win/WinSDK.hpp"
#include <filesystem>
#include <string>

namespace cg::win {

// Human-readable last error (useful for MsgBox on hard failures).
inline std::wstring LastErrorMessage(DWORD err = ::GetLastError()) {
    LPWSTR buf = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring out;
    if (n && buf) { out.assign(buf, buf + n); ::LocalFree(buf); }
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return out.empty() ? L"(unknown error)" : out;
}

// Growable GetModuleFileNameW (robust for long paths).
inline std::wstring GetModulePathW() {
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            std::wstring emsg = L"GetModuleFileNameW failed: " + LastErrorMessage();
            ::MessageBoxW(nullptr, emsg.c_str(), L"Colony Game", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return {};
        }
        // Success if the result clearly fits.
        if (n < buf.size() - 1) { buf.resize(n); return buf; }
        // Guard against absurd paths (~32K is the Windows NT max).
        if (buf.size() >= 32768) {
            ::MessageBoxW(nullptr, L"Executable path exceeds ~32K characters.", L"Colony Game",
                          MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return {};
        }
        buf.resize(buf.size() * 2);
    }
}

// Add \\?\ (or \\?\UNC\) only when needed and only for absolute paths.
inline std::wstring ToExtendedIfNeeded(const std::wstring& absPath) {
    auto has_prefix = [](const std::wstring& s, const wchar_t* pre) {
        return s.rfind(pre, 0) == 0;
    };
    if (absPath.empty() || has_prefix(absPath, LR"(\\?\)") || has_prefix(absPath, LR"(\\.\)"))
        return absPath;

    if (absPath.size() >= MAX_PATH) {
        // Drive-letter path?
        if (absPath.size() >= 2 && absPath[1] == L':') return LR"(\\?\)" + absPath;
        // UNC path?
        if (has_prefix(absPath, LR"(\\)")) return LR"(\\?\UNC\)" + absPath.substr(2);
    }
    return absPath;
}

} // namespace cg::win
