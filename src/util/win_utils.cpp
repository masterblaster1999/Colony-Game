// src/util/win_utils.cpp

#include "util/win_utils.h"

#ifndef _WIN32
#  error "win_utils.cpp is Windows-only and should not be built on non-Windows platforms."
#endif

#include <string>
#include <filesystem>

// Prefer the project's unified Windows header if available; otherwise fall
// back to the standard Windows.h + trim common macro noise.
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

// ShellExecuteW / ShellExecuteExW live here (especially with WIN32_LEAN_AND_MEAN)
#include <shellapi.h>

namespace
{
    // Helper to format a Win32 error code into a readable string.
    std::wstring FormatSystemMessage(DWORD errorCode)
    {
        if (!errorCode)
            return {};

        LPWSTR buffer = nullptr;

        const DWORD flags =
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS;

        const DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

        const DWORD len = ::FormatMessageW(
            flags,
            nullptr,
            errorCode,
            langId,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        if (!len || !buffer)
            return {};

        std::wstring result(buffer, len);
        ::LocalFree(buffer);

        return result;
    }

    // Thin wrapper around ShellExecuteW that logs failures to the debugger.
    inline void ShellExecuteSafe(
        LPCWSTR verb,
        LPCWSTR file,
        LPCWSTR parameters,
        LPCWSTR directory,
        int     showCmd)
    {
        const HINSTANCE h = ::ShellExecuteW(
            nullptr,   // parent window
            verb,
            file,
            parameters,
            directory,
            showCmd);

        // Per ShellExecuteW docs: the return value can be treated as an INT_PTR.
        // > 32  == success
        // <= 32 == error code (SE_ERR_XXX or similar).
        const auto code = reinterpret_cast<INT_PTR>(h);
        if (code <= 32)
        {
            const DWORD lastError = ::GetLastError();

            std::wstring dbg = L"[util::ShellExecuteSafe] ShellExecuteW failed"
                               L" (code = ";
            dbg += std::to_wstring(code);
            dbg += L", GetLastError = ";
            dbg += std::to_wstring(lastError);
            dbg += L")\n";

            const std::wstring sysMsg = FormatSystemMessage(lastError);
            if (!sysMsg.empty())
            {
                dbg += sysMsg;
                if (!dbg.empty() && dbg.back() != L'\n')
                    dbg.push_back(L'\n');
            }

            ::OutputDebugStringW(dbg.c_str());
        }
    }
} // anonymous namespace

namespace util
{
    std::string Quoted(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        out.append(s);
        out.push_back('"');
        return out;
    }

    void OpenInExplorer(const std::filesystem::path& path)
    {
        const std::wstring wpath = path.wstring();

        // Check attributes to see if this is a directory
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        const bool isDir =
            attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDir)
        {
            // Just open the directory
            ShellExecuteSafe(L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }

        // Otherwise, ask Explorer to select the file
        std::wstring args = L"/select,";
        args += wpath;

        ShellExecuteSafe(L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
} // namespace util
