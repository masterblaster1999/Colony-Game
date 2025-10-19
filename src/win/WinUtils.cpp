// src/win/WinUtils.cpp

// Keep Windows headers minimal and avoid min/max macro collisions.
// Guard these in case they were provided on the compiler command line.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <Windows.h>   // Must come after the guards. Docs recommend WIN32_LEAN_AND_MEAN. 
#include "WinUtils.h"

#include <filesystem>
#include <string>
#include <vector>
#include <mutex>

// Some SDKs might not surface this macro in older headers; define the documented value if missing.
// Ref: SetSearchPathMode constants (BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE == 0x00000001)
#ifndef BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE
#  define BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE 0x00000001
#endif

namespace
{
    // Trim trailing CR/LF/spaces/dots that FormatMessageW often appends.
    inline void rtrim_newlines(std::wstring& s)
    {
        while (!s.empty())
        {
            wchar_t c = s.back();
            if (c == L'\r' || c == L'\n' || c == L' ' || c == L'.')
                s.pop_back();
            else
                break;
        }
    }
} // namespace

std::filesystem::path GetExecutableDir()
{
    // Robust growth strategy per GetModuleFileNameW docs: if the buffer is too small,
    // the return value equals the buffer size; keep doubling until it fits.
    // https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
    DWORD capacity = 1024;
    std::vector<wchar_t> buf(capacity);

    for (;;)
    {
        DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), capacity);
        if (len == 0)
        {
            // give up on failure
            return {};
        }

        // If the buffer wasn't big enough, GetModuleFileNameW returns capacity (truncation).
        if (len >= capacity - 1)
        {
            capacity *= 2;
            buf.resize(capacity);
            continue;
        }

        // Success path.
        // Construct path from the exact number of characters returned.
        std::filesystem::path exe(std::wstring(buf.data(), len));
        return exe.parent_path();
    }
}

std::wstring GetLastErrorMessage(DWORD err)
{
    if (!err) return L"(no error)";

    // Use FormatMessageW with ALLOCATE_BUFFER, FROM_SYSTEM, IGNORE_INSERTS and MAX_WIDTH_MASK
    // for cleaner single-line messages. Docs: FormatMessageW flags behavior.
    // https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-formatmessagew
    LPWSTR raw = nullptr;
    const DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_MAX_WIDTH_MASK;

    const DWORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    DWORD len = ::FormatMessageW(flags, nullptr, err, lang,
                                 reinterpret_cast<LPWSTR>(&raw), 0, nullptr);

    std::wstring out = (len && raw) ? std::wstring(raw, len) : L"(unknown error)";
    if (raw) ::LocalFree(raw);

    rtrim_newlines(out);
    return out;
}

void TryHardenDllSearch()
{
    // One-time initialization; safe if called from multiple threads.
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
        if (!k32) return;

        // Prefer SetDefaultDllDirectories if available (KB2533623+).
        // This sets the process default LoadLibraryEx search path to the safer set.
        // https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-setdefaultdlldirectories
        using SetDefaultDllDirectoriesFn = BOOL (WINAPI*)(DWORD);
        auto pSetDefaultDllDirectories = reinterpret_cast<SetDefaultDllDirectoriesFn>(
            ::GetProcAddress(k32, "SetDefaultDllDirectories"));

        if (pSetDefaultDllDirectories)
        {
            // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS == app dir + System32 + AddDllDirectory() user dirs.
            (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
        else
        {
            // Fallback for older Windows: remove the current directory from the default DLL search order.
            // https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setdlldirectoryw
            (void)::SetDllDirectoryW(L"");
        }

        // Make SearchPath() (if used elsewhere) follow the safer search semantics.
        // https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setsearchpathmode
        using SetSearchPathModeFn = BOOL (WINAPI*)(DWORD);
        auto pSetSearchPathMode = reinterpret_cast<SetSearchPathModeFn>(
            ::GetProcAddress(k32, "SetSearchPathMode"));
        if (pSetSearchPathMode)
        {
            (void)pSetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE);
        }
    });
}
