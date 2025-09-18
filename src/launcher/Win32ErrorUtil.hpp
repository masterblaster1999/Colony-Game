#pragma once

// Self-contained Windows utilities for error decoding and UTF conversions.
// Safe for Unity/Jumbo builds: defines lean macros if not already set.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

#include <string>
#include <string_view>

namespace winerr {

// ---------- UTF-8 <-> UTF-16 helpers ----------
inline std::wstring Utf8ToWide(std::string_view u8) {
    if (u8.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  u8.data(), static_cast<int>(u8.size()),
                                  nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                          u8.data(), static_cast<int>(u8.size()),
                          w.data(), n);
    return w;
}

inline std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0,
                                  w.data(), static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0,
                          w.data(), static_cast<int>(w.size()),
                          s.data(), n, nullptr, nullptr);
    return s;
}

// ---------- Win32 error name + message ----------
inline const wchar_t* ErrorName(DWORD e) noexcept {
    switch (e) {
    case ERROR_SUCCESS:               return L"ERROR_SUCCESS";
    case ERROR_INVALID_FUNCTION:      return L"ERROR_INVALID_FUNCTION";
    case ERROR_FILE_NOT_FOUND:        return L"ERROR_FILE_NOT_FOUND";
    case ERROR_PATH_NOT_FOUND:        return L"ERROR_PATH_NOT_FOUND";
    case ERROR_ACCESS_DENIED:         return L"ERROR_ACCESS_DENIED";
    case ERROR_INVALID_HANDLE:        return L"ERROR_INVALID_HANDLE";
    case ERROR_NOT_ENOUGH_MEMORY:     return L"ERROR_NOT_ENOUGH_MEMORY";
    case ERROR_OUTOFMEMORY:           return L"ERROR_OUTOFMEMORY";
    case ERROR_INVALID_PARAMETER:     return L"ERROR_INVALID_PARAMETER";
    case ERROR_NO_MORE_FILES:         return L"ERROR_NO_MORE_FILES";
    case ERROR_WRITE_PROTECT:         return L"ERROR_WRITE_PROTECT";
    case ERROR_SHARING_VIOLATION:     return L"ERROR_SHARING_VIOLATION";
    case ERROR_LOCK_VIOLATION:        return L"ERROR_LOCK_VIOLATION";
    case ERROR_BUSY:                  return L"ERROR_BUSY";
    case ERROR_ALREADY_EXISTS:        return L"ERROR_ALREADY_EXISTS";
    case ERROR_FILENAME_EXCED_RANGE:  return L"ERROR_FILENAME_EXCED_RANGE";
    case ERROR_BAD_PATHNAME:          return L"ERROR_BAD_PATHNAME";
    case ERROR_BAD_EXE_FORMAT:        return L"ERROR_BAD_EXE_FORMAT";
    case ERROR_MOD_NOT_FOUND:         return L"ERROR_MOD_NOT_FOUND";
    case ERROR_PROC_NOT_FOUND:        return L"ERROR_PROC_NOT_FOUND";
    case ERROR_DLL_INIT_FAILED:       return L"ERROR_DLL_INIT_FAILED";
    case ERROR_ENVVAR_NOT_FOUND:      return L"ERROR_ENVVAR_NOT_FOUND";
    case ERROR_DIR_NOT_EMPTY:         return L"ERROR_DIR_NOT_EMPTY";
    case ERROR_DEV_NOT_EXIST:         return L"ERROR_DEV_NOT_EXIST";
    case ERROR_BROKEN_PIPE:           return L"ERROR_BROKEN_PIPE";
    case ERROR_MR_MID_NOT_FOUND:      return L"ERROR_MR_MID_NOT_FOUND";
    case ERROR_ELEVATION_REQUIRED:    return L"ERROR_ELEVATION_REQUIRED";
    default:                          return L"";
    }
}

inline std::wstring FormatMessageW32(DWORD code) {
    LPWSTR buf = nullptr;
    DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

    std::wstring out;
    if (n && buf) {
        out.assign(buf, buf + n);
        ::LocalFree(buf);
    } else {
        out = L"(unknown error)";
    }

    // Trim trailing CR/LF and spaces that FM can add.
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ' || out.back() == L'\t'))
        out.pop_back();
    return out;
}

inline std::wstring ErrorToString(DWORD code) {
    const wchar_t* name = ErrorName(code);
    std::wstring msg = FormatMessageW32(code);
    if (name && *name) {
        return std::wstring(name) + L" (" + std::to_wstring(code) + L"): " + msg;
    }
    return L"Win32 Error " + std::to_wstring(code) + L": " + msg;
}

} // namespace winerr
